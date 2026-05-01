#include "renderer/terrain_renderer.h"
#include "utils/logger.h"
#include "core/vulkan_device.h"
#include <glm/glm.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <random>
#include <algorithm>
#include <chrono>

// 地形渲染器实现 — 过程化 Perlin 噪声 + 异步区块生成管线
//
// 线程模型：
//   - 构造函数初始化 perm 并设置 permInitialized=true（阻止 initPerm 重跑）
//   - perlinNoise()/fbm()/getHeight()/computeChunkMesh() 均为 const，仅读取 perm
//   - update() 每帧在渲染线程调用，使用 std::async 将噪声计算卸到后台线程
//   - Vulkan 缓冲创建始终在主线程（uploadChunk）
//
// 区块生成风暴缓解：
//   原先 update() 每帧同步生成 renderRadius 内全部区块（~314 个），跨越区块边界时
//   导致单帧 ~30,000 次 FBM 调用 + Vulkan 分配 — 造成明显卡顿。
//   现在通过 generationRadius 预生成 + maxChunksPerFrame 限流 + std::async 后台计算
//   将单帧负载从同步批量降到异步扩展，削平性能峰值。

namespace owengine {

namespace {

int fastFloor(float v) {
    return v > 0 ? static_cast<int>(v) : static_cast<int>(v) - 1;
}

float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float grad(int h, float x, float y) {
    int hh = h & 15;
    float u = hh < 8 ? x : y;
    float v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : 0.0f);
    return ((hh & 1) == 0 ? u : -u) + ((hh & 2) == 0 ? v : -v);
}

static std::mt19937 rng(42);
static std::uniform_int_distribution<int> dist(0, 255);
static bool permInitialized = false;

void initPerm(std::vector<int>& perm) {
    if (permInitialized) return;
    perm.resize(512);
    for (int i = 0; i < 256; ++i) perm[i] = i;
    for (int i = 255; i > 0; --i) {
        int j = dist(rng) % (i + 1);
        std::swap(perm[i], perm[j]);
    }
    for (int i = 0; i < 256; ++i) perm[256 + i] = perm[i];
    permInitialized = true;
}

} // anonymous namespace

TerrainRenderer::TerrainRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      chunkSize(16.0f),
      renderRadius(10),
      generationRadius(13),       // renderRadius + 3，提前生成边界外区块
      maxChunksPerFrame(4),       // 每帧异步任务上限，削去区块生成峰值
      noiseScale(0.01f),
      heightScale(25.0f),
      baseHeight(0.0f),
      terrainColor(0.0f, 1.0f, 0.0f),
      created_(false) {
    // 初始化 Perlin 排列表（512 元素，种子 42）。
    // permInitialized = true 阻止 initPerm() 在 perlinNoise() 中重复运行，
    // 从而允许 perlinNoise 标记为 const 并在异步线程中安全调用。
    static std::mt19937 rng(42);
    static std::uniform_int_distribution<int> dist(0, 255);
    perm.resize(512);
    for (int i = 0; i < 256; ++i) {
        perm[i] = i;
    }
    for (int i = 255; i > 0; --i) {
        int j = dist(rng) % (i + 1);
        std::swap(perm[i], perm[j]);
    }
    for (int i = 0; i < 256; ++i) {
        perm[256 + i] = perm[i];
    }
    permInitialized = true;
}

TerrainRenderer::~TerrainRenderer() {
    cleanup();
}

void TerrainRenderer::create() {
    initBufferPool();
    created_ = true;
}

void TerrainRenderer::cleanup() {
    for (auto& pair : chunks) {
        // Only release slot, don't destroy (pool owns buffers)
        if (pair.second.poolSlot >= 0) {
            bufferPool_[pair.second.poolSlot].inUse = false;
        }
    }
    chunks.clear();
    cleanupBufferPool();
    created_ = false;
}

// 初始化缓冲池：预分配 BUFFER_POOL_SIZE 组 vertex + index 缓冲区
void TerrainRenderer::initBufferPool() {
    bufferPool_.resize(BUFFER_POOL_SIZE);
    for (int i = 0; i < BUFFER_POOL_SIZE; ++i) {
        auto& slot = bufferPool_[i];
        VkBufferCreateInfo vbInfo{};
        vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbInfo.size = CHUNK_VERTEX_BUFFER_SIZE;
        vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device->getDevice(), &vbInfo, nullptr, &slot.vertexBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pooled vertex buffer!");
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device->getDevice(), slot.vertexBuffer, &memReqs);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = device->findMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &slot.vertexBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate pooled vertex buffer memory!");
        }
        vkBindBufferMemory(device->getDevice(), slot.vertexBuffer, slot.vertexBufferMemory, 0);

        VkBufferCreateInfo ibInfo{};
        ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibInfo.size = CHUNK_INDEX_BUFFER_SIZE;
        ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device->getDevice(), &ibInfo, nullptr, &slot.indexBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pooled index buffer!");
        }

        vkGetBufferMemoryRequirements(device->getDevice(), slot.indexBuffer, &memReqs);
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = device->findMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &slot.indexBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate pooled index buffer memory!");
        }
        vkBindBufferMemory(device->getDevice(), slot.indexBuffer, slot.indexBufferMemory, 0);

        slot.inUse = false;
    }
    nextPoolHint_ = 0;
}

// 销毁缓冲池：释放所有预先分配的缓冲区
void TerrainRenderer::cleanupBufferPool() {
    for (auto& slot : bufferPool_) {
        if (slot.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device->getDevice(), slot.indexBuffer, nullptr);
        }
        if (slot.indexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device->getDevice(), slot.indexBufferMemory, nullptr);
        }
        if (slot.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device->getDevice(), slot.vertexBuffer, nullptr);
        }
        if (slot.vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device->getDevice(), slot.vertexBufferMemory, nullptr);
        }
    }
    bufferPool_.clear();
    nextPoolHint_ = 0;
}

// 从缓冲池中获取一个空闲槽位（O(n) 扫描，从上次分配位置附近开始）
int TerrainRenderer::acquirePoolSlot() {
    for (int i = 0; i < BUFFER_POOL_SIZE; ++i) {
        int idx = (nextPoolHint_ + i) % BUFFER_POOL_SIZE;
        if (!bufferPool_[idx].inUse) {
            bufferPool_[idx].inUse = true;
            nextPoolHint_ = (idx + 1) % BUFFER_POOL_SIZE;
            return idx;
        }
    }
    // 所有槽位已满（正常情况下不应发生，因为 chunk 总数 ≤ BUFFER_POOL_SIZE）
    Logger::error("[地形] 缓冲池耗尽！增大 BUFFER_POOL_SIZE");
    return -1;
}

// 归还槽位到缓冲池
void TerrainRenderer::releasePoolSlot(int slot) {
    if (slot >= 0 && slot < BUFFER_POOL_SIZE) {
        bufferPool_[slot].inUse = false;
    }
}

float TerrainRenderer::perlinNoise(float x, float z) const {
    // perm is already initialized by the constructor
    int xi = fastFloor(x) & 255;
    int zi = fastFloor(z) & 255;
    
    float xf = x - fastFloor(x);
    float zf = z - fastFloor(z);
    
    float u = fade(xf);
    float v = fade(zf);
    
    int aa = perm[perm[xi] + zi];
    int ab = perm[perm[xi] + zi + 1];
    int ba = perm[perm[xi + 1] + zi];
    int bb = perm[perm[xi + 1] + zi + 1];
    
    float aaGrad = grad(aa, xf, zf);
    float abGrad = grad(ab, xf, zf - 1.0f);
    float baGrad = grad(ba, xf - 1.0f, zf);
    float bbGrad = grad(bb, xf - 1.0f, zf - 1.0f);
    
    float n0 = lerp(aaGrad, baGrad, u);
    float n1 = lerp(abGrad, bbGrad, u);
    
    return lerp(n0, n1, v);
}

float TerrainRenderer::fbm(float x, float z, int octaves) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        value += perlinNoise(x * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    
    return value / maxValue;
}

float TerrainRenderer::getHeight(float x, float z) const {
    float noiseX = x * noiseScale;
    float noiseZ = z * noiseScale;
    float height = fbm(noiseX, noiseZ, 3) * heightScale;
    return baseHeight + height;
}

// 纯 CPU 网格计算（线程安全，在 std::async 后台线程中执行）
//
// 每个区块生成流程：
//   1. 17×17 顶点网格采样 Perlin 噪声（fbm 4 octaves）
//   2. 有限差分法计算顶点法向量（±0.05 偏移采样邻居高度）
//   3. 高度归一化 + 颜色映射
//   4. 生成 16×16 四边形索引（每个四边形 2 三角形 = 6 索引）
//
// 无 Vulkan 调用：仅读取成员的噪声/颜色参数，返回 ChunkMesh 供主线程上传。
ChunkMesh TerrainRenderer::computeChunkMesh(int chunkX, int chunkZ) const {
    const int segments = 16;
    float startX = static_cast<float>(chunkX) * chunkSize;
    float startZ = static_cast<float>(chunkZ) * chunkSize;
    
    ChunkMesh mesh;
    mesh.chunkX = chunkX;
    mesh.chunkZ = chunkZ;
    mesh.vertices.reserve((segments + 1) * (segments + 1));
    
    for (int z = 0; z <= segments; ++z) {
        for (int x = 0; x <= segments; ++x) {
            float worldX = startX + static_cast<float>(x) / segments * chunkSize;
            float worldZ = startZ + static_cast<float>(z) / segments * chunkSize;
            
            float height = getHeight(worldX, worldZ);
            
            float dNoise = 0.05f;
            float hL = getHeight(worldX - dNoise, worldZ);
            float hR = getHeight(worldX + dNoise, worldZ);
            float hD = getHeight(worldX, worldZ - dNoise);
            float hU = getHeight(worldX, worldZ + dNoise);
            
            float nx = (hL - hR) / (2.0f * dNoise);
            float nz = (hD - hU) / (2.0f * dNoise);
            glm::vec3 normal = glm::normalize(glm::vec3(-nx, 1.0f, -nz));
            
            float height01 = (height - baseHeight) / heightScale;
            height01 = glm::clamp(height01 * 0.5f + 0.5f, 0.0f, 1.0f);
            // 高度多色渐变：低处褐色 → 中段绿色 → 高处黄绿色
            glm::vec3 lowColor(0.35f, 0.25f, 0.12f);    // 低处：泥土褐色
            glm::vec3 midColor(0.18f, 0.65f, 0.14f);    // 中段：鲜绿色
            glm::vec3 highColor(0.55f, 0.78f, 0.25f);   // 高处：浅黄绿
            glm::vec3 color;
            if (height01 < 0.5f) {
                float t = height01 / 0.5f;
                color = glm::mix(lowColor, midColor, t);
            } else {
                float t = (height01 - 0.5f) / 0.5f;
                color = glm::mix(midColor, highColor, t);
            }
            
            TerrainVertex vert;
            vert.pos = glm::vec3(worldX, height, worldZ);
            vert.normal = normal;
            vert.color = color;
            vert.texCoord = glm::vec2(static_cast<float>(x) / segments, static_cast<float>(z) / segments);
            mesh.vertices.push_back(vert);
        }
    }
    
    for (int z = 0; z < segments; ++z) {
        for (int x = 0; x < segments; ++x) {
            uint32_t topLeft = static_cast<uint32_t>(z) * (segments + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (static_cast<uint32_t>(z) + 1) * (segments + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;
            
            mesh.indices.push_back(topLeft);
            mesh.indices.push_back(bottomLeft);
            mesh.indices.push_back(topRight);
            mesh.indices.push_back(topRight);
            mesh.indices.push_back(bottomLeft);
            mesh.indices.push_back(bottomRight);
        }
    }
    
    return mesh;
}

// 将 computeChunkMesh 的输出上传到 Vulkan（从缓冲池取用，必须在主线程调用）
//
// 从缓冲池获取预先分配的 vertex + index 缓冲区，直接 memcpy 数据，
// 避免运行时 vkCreateBuffer/vkAllocateMemory 的 GPU 内存分配开销。
void TerrainRenderer::uploadChunk(const ChunkMesh& mesh) {
    ChunkKey key{mesh.chunkX, mesh.chunkZ};
    if (chunks.find(key) != chunks.end()) return;
    
    int slot = acquirePoolSlot();
    if (slot < 0) return;  // 池耗尽，丢弃该区块
    
    TerrainChunk chunk;
    chunk.chunkX = mesh.chunkX;
    chunk.chunkZ = mesh.chunkZ;
    chunk.indexCount = static_cast<uint32_t>(mesh.indices.size());
    chunk.isValid = true;
    chunk.poolSlot = slot;
    
    // 从池中获取预分配的缓冲区句柄
    auto& poolSlot = bufferPool_[slot];
    chunk.vertexBuffer = poolSlot.vertexBuffer;
    chunk.vertexBufferMemory = poolSlot.vertexBufferMemory;
    chunk.indexBuffer = poolSlot.indexBuffer;
    chunk.indexBufferMemory = poolSlot.indexBufferMemory;
    
    // 顶点数据上传（池缓冲区大小固定 ≥ 实际数据，直接 memcpy）
    void* data;
    VkDeviceSize vertexBufferSize = sizeof(TerrainVertex) * mesh.vertices.size();
    vkMapMemory(device->getDevice(), chunk.vertexBufferMemory, 0, VK_WHOLE_SIZE, 0, &data);
    memcpy(data, mesh.vertices.data(), static_cast<size_t>(vertexBufferSize));
    vkUnmapMemory(device->getDevice(), chunk.vertexBufferMemory);
    
    // 索引数据上传
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * mesh.indices.size();
    vkMapMemory(device->getDevice(), chunk.indexBufferMemory, 0, VK_WHOLE_SIZE, 0, &data);
    memcpy(data, mesh.indices.data(), static_cast<size_t>(indexBufferSize));
    vkUnmapMemory(device->getDevice(), chunk.indexBufferMemory);
    
    chunks[key] = chunk;
}

void TerrainRenderer::generateChunk(int chunkX, int chunkZ) {
    ChunkKey key{chunkX, chunkZ};
    if (chunks.find(key) != chunks.end()) return;
    ChunkMesh mesh = computeChunkMesh(chunkX, chunkZ);
    uploadChunk(mesh);
}

void TerrainRenderer::cleanupChunk(TerrainChunk& chunk) {
    // 归还缓冲区到池（不销毁，池拥有生命周期）
    releasePoolSlot(chunk.poolSlot);
    chunk.poolSlot = -1;
    chunk.vertexBuffer = VK_NULL_HANDLE;
    chunk.vertexBufferMemory = VK_NULL_HANDLE;
    chunk.indexBuffer = VK_NULL_HANDLE;
    chunk.indexBufferMemory = VK_NULL_HANDLE;
}

// 异步区块更新管线（每帧由渲染线程调用）
//
// 四阶段流程：
//   Phase 1 — 消费已完成的异步任务，将网格数据上传到 Vulkan
//   Phase 2 — 扫描 generationRadius 范围内的缺失区块，去重后收集候选列表
//   Phase 3 — 按玩家距离排序候选列表，启动最多 maxChunksPerFrame 个 std::async
//   Phase 4 — 移除 renderRadius + 2 范围外的旧区块（卸载余量 2 防止抖动）
//
// 预生成机制：
//   generationRadius = renderRadius + 3，区块在玩家到达前 3 个区块距离就开始生成，
//   确保玩家走进渲染半径时区块已就绪。
//
// 速率限制：
//   maxChunksPerFrame = 4 将单帧负载从 ~314 次同步生成降低到 4 次异步启动，
//   多数区块在后台线程中静默完成，不会阻塞渲染帧。初始场景约需 3-4 秒渐进加载完毕。
void TerrainRenderer::update(const glm::vec3& playerPos) {
    int playerChunkX = static_cast<int>(std::floor(playerPos.x / chunkSize));
    int playerChunkZ = static_cast<int>(std::floor(playerPos.z / chunkSize));
    
    // Phase 1: Process completed async tasks
    for (auto it = pendingChunks_.begin(); it != pendingChunks_.end(); ) {
        if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ChunkMesh mesh = it->future.get();
            uploadChunk(mesh);
            it = pendingChunks_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Phase 2: Find chunks that need generation (use generationRadius for preloading)
    std::vector<std::pair<int, int>> candidates;
    for (int dz = -generationRadius; dz <= generationRadius; ++dz) {
        for (int dx = -generationRadius; dx <= generationRadius; ++dx) {
            int chunkX = playerChunkX + dx;
            int chunkZ = playerChunkZ + dz;
            
            float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (dist > static_cast<float>(generationRadius)) continue;
            
            ChunkKey key{chunkX, chunkZ};
            if (chunks.find(key) != chunks.end()) continue;
            
            // Check if already pending
            bool alreadyPending = false;
            for (const auto& p : pendingChunks_) {
                if (p.chunkX == chunkX && p.chunkZ == chunkZ) {
                    alreadyPending = true;
                    break;
                }
            }
            if (alreadyPending) continue;
            
            candidates.emplace_back(chunkX, chunkZ);
        }
    }
    
    // Phase 3: Sort by distance to player, limit per frame
    std::sort(candidates.begin(), candidates.end(),
        [playerChunkX, playerChunkZ](const auto& a, const auto& b) {
            int dax = a.first - playerChunkX;
            int daz = a.second - playerChunkZ;
            int dbx = b.first - playerChunkX;
            int dbz = b.second - playerChunkZ;
            return (dax * dax + daz * daz) < (dbx * dbx + dbz * dbz);
        });
    
    int launchCount = 0;
    for (const auto& [cX, cZ] : candidates) {
        if (launchCount >= maxChunksPerFrame) break;
        
        PendingChunk pending;
        pending.chunkX = cX;
        pending.chunkZ = cZ;
        pending.future = std::async(std::launch::async, [this, cX, cZ]() {
            return computeChunkMesh(cX, cZ);
        });
        pendingChunks_.push_back(std::move(pending));
        launchCount++;
    }
    
    // Phase 4: Remove chunks outside render radius + margin
    std::vector<ChunkKey> toRemove;
    for (const auto& pair : chunks) {
        int dx = pair.first.x - playerChunkX;
        int dz = pair.first.z - playerChunkZ;
        float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
        if (dist > renderRadius + 2) {
            toRemove.push_back(pair.first);
        }
    }
    
    for (const auto& key : toRemove) {
        auto it = chunks.find(key);
        if (it != chunks.end()) {
            cleanupChunk(it->second);
            chunks.erase(it);
        }
    }
}

void TerrainRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    PushConstants pushConstants{};
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    pushConstants.baseColor = terrainColor;
    pushConstants.metallic = 0.0f;
    pushConstants.roughness = 0.35f;  // 降低粗糙度产生镜面高光，地形呈现光滑感
    pushConstants.hasTexture = 0;
    pushConstants._pad0 = 0.0f;
    
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);
    
    for (const auto& pair : chunks) {
        const auto& chunk = pair.second;
        if (!chunk.isValid) continue;
        
        VkBuffer vertexBuffers[] = {chunk.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, chunk.indexCount, 1, 0, 0, 0);
    }
}

} // namespace owengine