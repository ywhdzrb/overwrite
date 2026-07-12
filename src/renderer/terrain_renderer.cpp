#include "renderer/terrain_renderer.hpp"
#include "utils/logger.hpp"
#include "core/game_config.hpp"
#include "core/vulkan_device.hpp"
#include <glm/glm.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <random>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include "utils/vk_result.hpp"

// 多地貌地形渲染器实现 — 多层噪声混合 + 异步区块生成管线
//
// 地貌生成算法：
//   最终高度 = 大陆基底 + 山脉脊线(Ridged域扭曲) + 丘陵起伏 + 高原削平 - 河流侵蚀 + 细节
//
//   各层作用：
//     - 大陆基底：极低频 FBM，定义海陆格局和整体地势
//     - 山脉脊线：Ridged Multifractal + 域扭曲，产生自然弯曲的陡峭山脊
//     - 丘陵起伏：中频标准 FBM，柔和起伏的丘陵地带
//     - 高原削平：中低频噪声掩码+平坦化，形成桌面状高原
//     - 河流侵蚀：脊线噪声的负向应用，下切出河谷
//     - 细节：高频噪声增加地表质感
//
// 生物群落着色：
//   基于高度 + 坡度 + 区域掩码 判定生物群落（TerrainBiome），
//   为每个顶点赋予对应的颜色值。
//
// 线程模型：
//   - 构造函数初始化 perm，之后 perm 只读不再写入
//   - perlinNoise()/fbm()/getHeight()/computeChunkMesh() 均为 const，仅读取 perm
//   - update() 每帧在渲染线程调用，使用线程池将噪声计算卸到后台线程
//   - Vulkan 缓冲创建始终在主线程（uploadChunk）

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

} // anonymous namespace

TerrainRenderer::TerrainRenderer(std::shared_ptr<VulkanDevice> devicePtr)
    : device_(std::move(devicePtr)),
      chunkSize_(16.0f),
      renderRadius_(10),
      generationRadius_(13),       // renderRadius + 3，提前生成边界外区块
      maxChunksPerFrame_(4),       // 每帧异步任务上限，削去区块生成峰值
      created_(false) {
    // 初始化 Perlin 排列表（512 元素，种子 42）。
    // 构造函数一次性初始化，之后 perm_ 只读不写，确保 perlinNoise() 可在异步线程中安全调用。
    static std::mt19937 rng(42);
    static std::uniform_int_distribution<int> dist(0, 255);
    perm_.resize(512);
    for (int i = 0; i < 256; ++i) {
        perm_[i] = i;
    }
    for (int i = 255; i > 0; --i) {
        int j = dist(rng) % (i + 1);
        std::swap(perm_[i], perm_[j]);
    }
    for (int i = 0; i < 256; ++i) {
        perm_[256 + i] = perm_[i];
    }
}

TerrainRenderer::~TerrainRenderer() {
    cleanup();
}

void TerrainRenderer::create() {
    initBufferPool();
    created_ = true;
}

void TerrainRenderer::cleanup() {
    for (auto& pair : chunks_) {
        // Only release slot, don't destroy (pool owns buffers)
        if (pair.second.poolSlot >= 0) {
            bufferPool_[pair.second.poolSlot].inUse = false;
        }
    }
    chunks_.clear();
    cleanupBufferPool();
    created_ = false;
}

void TerrainParams::applyFromConfig(const TerrainConfig& cfg) {
    continentScale = cfg.continentScale;
    continentHeight = cfg.continentHeight;
    seaLevel = cfg.seaLevel;
    smoothFreq = cfg.smoothFreq;
    roughFreq = cfg.roughFreq;
    plainAmp = cfg.plainAmp;
    mountainAmp = cfg.mountainAmp;
    mountainRoughBlend = cfg.mountainRoughBlend;
    mountainSeedFreq = cfg.mountainSeedFreq;
    continentRawBase = cfg.continentRawBase;
    continentRawSpan = cfg.continentRawSpan;
    coastBlendStart = cfg.coastBlendStart;
    coastBlendEnd = cfg.coastBlendEnd;
    oceanDepth = cfg.oceanDepth;
    continentBias = cfg.continentBias;
}

void TerrainRenderer::initBufferPool() {
    bufferPool_.resize(BUFFER_POOL_SIZE);
    VmaAllocator allocator = device_->getAllocator();
    for (int i = 0; i < BUFFER_POOL_SIZE; ++i) {
        auto& slot = bufferPool_[i];

        VkBufferCreateInfo vbInfo{};
        vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbInfo.size = CHUNK_VERTEX_BUFFER_SIZE;
        vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocOut;
        VkResult _vrVB = vmaCreateBuffer(allocator, &vbInfo, &allocInfo, &slot.vertexBuffer, &slot.vertexBufferAllocation, &allocOut);
        if (_vrVB != VK_SUCCESS) {
            throw std::runtime_error(std::string("failed to create pooled vertex buffer! ") + vkResultToString(_vrVB));
        }
        slot.vertexMappedData = allocOut.pMappedData;

        VkBufferCreateInfo ibInfo{};
        ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibInfo.size = CHUNK_INDEX_BUFFER_SIZE;
        ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult _vrIB = vmaCreateBuffer(allocator, &ibInfo, &allocInfo, &slot.indexBuffer, &slot.indexBufferAllocation, &allocOut);
        if (_vrIB != VK_SUCCESS) {
            throw std::runtime_error(std::string("failed to create pooled index buffer! ") + vkResultToString(_vrIB));
        }
        slot.indexMappedData = allocOut.pMappedData;

        slot.inUse = false;
    }
    nextPoolHint_ = 0;
}

// 销毁缓冲池：释放所有预先分配的缓冲区
void TerrainRenderer::cleanupBufferPool() {
    VmaAllocator allocator = device_->getAllocator();
    for (auto& slot : bufferPool_) {
        if (slot.indexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, slot.indexBuffer, slot.indexBufferAllocation);
        }
        if (slot.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, slot.vertexBuffer, slot.vertexBufferAllocation);
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
    int xi = fastFloor(x) & 255;
    int zi = fastFloor(z) & 255;
    
    float xf = x - fastFloor(x);
    float zf = z - fastFloor(z);
    
    float u = fade(xf);
    float v = fade(zf);
    
    int aa = perm_[perm_[xi] + zi];
    int ab = perm_[perm_[xi] + zi + 1];
    int ba = perm_[perm_[xi + 1] + zi];
    int bb = perm_[perm_[xi + 1] + zi + 1];
    
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

// 脊线噪声：将 FBM 输出映射为尖锐山脊形态
// 核心公式：ridge = 1 - |noise|，将[-1,1]范围折叠为[0,1]的山脊
// sharpness > 1 增强山脊对比度
// ========== 单源振幅地形生成 ==========
//
// 只用一个噪声源，山脉区域仅放大振幅，不叠加不同频率的噪声层。
// 噪声纹理模式全图一致，掩码过渡处看不到纹理断层。
float TerrainRenderer::getHeight(float x, float z) const {
    const auto& p = terrainParams_;

    // 1. 大陆基底
    float continentRaw = fbm(x * p.continentScale, z * p.continentScale, 2)
                        + p.continentBias;
    float continentElev = continentRaw * p.continentHeight;
    float continentFactor = glm::clamp(
        (continentRaw + p.continentRawBase) / p.continentRawSpan, 0.0f, 1.0f);

    // 2. 山脉掩码
    float mountainSeed = fbm(x * p.mountainSeedFreq + 50.0f,
                             z * p.mountainSeedFreq, 3);
    float mountainMask = glm::clamp((mountainSeed - 0.25f) / 0.5f, 0.0f, 1.0f)
                         * continentFactor;
    mountainMask = mountainMask * mountainMask * (3.0f - 2.0f * mountainMask);

    // 3. 双频地形噪声
    float smoothTerrain = fbm(x * p.smoothFreq, z * p.smoothFreq, 3);
    float roughTerrain  = fbm(x * p.roughFreq,  z * p.roughFreq,  3);
    float blendTerrain = glm::mix(smoothTerrain, roughTerrain,
                                  mountainMask * p.mountainRoughBlend);
    float amplitude = p.plainAmp + mountainMask * p.mountainAmp;

    // 4. 合成
    float height = continentElev + blendTerrain * amplitude;

    // 5. 海岸线平滑过渡
    float oceanFloor = p.seaLevel - p.oceanDepth * (1.0f - continentFactor);
    float landBlend = glm::smoothstep(p.coastBlendStart, p.coastBlendEnd,
                                      continentFactor);
    height = glm::mix(oceanFloor, height, landBlend);

    return height;
}

// 计算地形坡度（百分比 0~1）
float TerrainRenderer::getSlope(float x, float z) const {
    float d = 0.5f;  // 采样间隔
    float hL = getHeight(x - d, z);
    float hR = getHeight(x + d, z);
    float hD = getHeight(x, z - d);
    float hU = getHeight(x, z + d);
    
    float dx = (hR - hL) / (2.0f * d);
    float dz = (hU - hD) / (2.0f * d);
    
    // 坡度 = 水平方向梯度向量的长度
    return std::sqrt(dx * dx + dz * dz);
}

// 判定生物群落
// 综合高度、坡度、山脉掩码决定地表覆盖类型
TerrainBiome TerrainRenderer::getBiome(float x, float z, float height, float slope) const {
    const auto& p = terrainParams_;
    float heightAboveSea = height - p.seaLevel;

    // 海面以下
    if (heightAboveSea < -0.5f) {
        return TerrainBiome::Ocean;
    }

    // 沙滩（海平面附近平坦区域）
    if (heightAboveSea < 1.5f && slope < 0.3f) {
        return TerrainBiome::Beach;
    }

    // 雪顶（高海拔）
    float snowLine = 25.0f + fbm(x * 0.005f, z * 0.005f, 2) * 5.0f;
    if (heightAboveSea > snowLine) {
        return TerrainBiome::Snow;
    }

    // 山脉（陡峭高海拔）
    if (heightAboveSea > 12.0f && slope > 0.6f) {
        return TerrainBiome::Mountains;
    }

    // 高原（平坦高海拔）
    if (heightAboveSea > 10.0f && slope < 0.3f) {
        return TerrainBiome::Plateau;
    }

    // 荒漠/不毛之地（陡峭中低海拔）
    if (slope > 1.0f) {
        return TerrainBiome::Badlands;
    }

    // 丘陵（中等海拔+中等坡度）
    if ((heightAboveSea > 6.0f && slope > 0.3f) || (heightAboveSea > 3.0f && slope > 0.4f)) {
        return TerrainBiome::Hills;
    }

    // 森林（中等海拔平坦区域）
    if (heightAboveSea > 2.0f && slope < 0.35f) {
        return TerrainBiome::Forest;
    }

    // 平原（低海拔平坦区域）
    return TerrainBiome::Plains;
}

// 根据生物群落返回顶点颜色
// 每种群落有基础色 + 高度/坡度微调，产生自然的色彩过渡
glm::vec3 TerrainRenderer::getBiomeColor(TerrainBiome biome, float height, float slope) const {
    switch (biome) {
        case TerrainBiome::Ocean:
            // 深海蓝 → 浅海青
            return glm::vec3(0.05f, 0.15f, 0.30f);

        case TerrainBiome::Beach:
            // 沙滩色，随高度略微变亮
            return glm::vec3(0.76f, 0.70f, 0.50f) + glm::vec3(0.03f) * height;

        case TerrainBiome::Plains:
            // 平原鲜绿色，微调
            return glm::vec3(0.25f, 0.55f, 0.12f);

        case TerrainBiome::Forest:
            // 森林深绿色
            return glm::vec3(0.12f, 0.45f, 0.08f) + glm::vec3(0.02f, 0.05f, 0.0f) * std::min(height * 0.1f, 1.0f);

        case TerrainBiome::Hills:
            // 丘陵橄榄绿
            return glm::vec3(0.30f, 0.50f, 0.18f);

        case TerrainBiome::Mountains:
            // 山脉灰褐色，坡度越陡越灰
            {
                float grayness = glm::clamp(slope * 0.5f, 0.0f, 1.0f);
                return glm::mix(glm::vec3(0.35f, 0.30f, 0.20f), glm::vec3(0.50f, 0.48f, 0.45f), grayness);
            }

        case TerrainBiome::Snow:
            // 雪白色
            return glm::vec3(0.92f, 0.93f, 0.95f);

        case TerrainBiome::River:
            // 河谷深褐色（河床）
            return glm::vec3(0.30f, 0.22f, 0.12f);

        case TerrainBiome::Plateau:
            // 高原黄绿色
            return glm::vec3(0.45f, 0.60f, 0.20f);

        case TerrainBiome::Badlands:
            // 荒漠红褐色
            return glm::vec3(0.50f, 0.30f, 0.12f);

        default:
            return glm::vec3(0.2f, 0.5f, 0.1f);
    }
}

// 纯 CPU 网格计算（线程安全，在线程池后台线程中执行）
//
// 优化说明：
//   1. 预计算所有顶点高度存入缓存数组，法向量用相邻顶点代替独立采样
//   2. 将 getHeight 调用从每顶点 9 次降至 1 次，减少 CPU 计算量约 89%
//   3. 无 Vulkan 调用，返回 ChunkMesh 供主线程上传
ChunkMesh TerrainRenderer::computeChunkMesh(int chunkX, int chunkZ) const {
    const int segments = 16;
    const int vertsPerEdge = segments + 1;
    const float cellSize = chunkSize_ / static_cast<float>(segments);
    float startX = static_cast<float>(chunkX) * chunkSize_;
    float startZ = static_cast<float>(chunkZ) * chunkSize_;

    const int cacheSize = vertsPerEdge + 3;
    std::vector<std::vector<float>> heightCache(cacheSize, std::vector<float>(cacheSize, 0.0f));
    for (int cz = 0; cz < cacheSize; ++cz) {
        for (int cx = 0; cx < cacheSize; ++cx) {
            float wx = startX + (static_cast<float>(cx) - 1.0f) * cellSize;
            float wz = startZ + (static_cast<float>(cz) - 1.0f) * cellSize;
            heightCache[cz][cx] = getHeight(wx, wz);
        }
    }

    ChunkMesh mesh;
    mesh.chunkX = chunkX;
    mesh.chunkZ = chunkZ;
    mesh.vertices.reserve(static_cast<size_t>(vertsPerEdge) * vertsPerEdge);

    for (int z = 0; z < vertsPerEdge; ++z) {
        for (int x = 0; x < vertsPerEdge; ++x) {
            float wx = startX + static_cast<float>(x) * cellSize;
            float wz = startZ + static_cast<float>(z) * cellSize;
            int ci = x + 1;
            int cj = z + 1;
            float height = heightCache[cj][ci];

            float nx = 0.0f, nz = 0.0f;
            if (x > 0 && x < segments) {
                nx = (heightCache[cj][ci - 1] - heightCache[cj][ci + 1]) / (2.0f * cellSize);
            }
            if (z > 0 && z < segments) {
                nz = (heightCache[cj - 1][ci] - heightCache[cj + 1][ci]) / (2.0f * cellSize);
            }
            glm::vec3 normal = glm::normalize(glm::vec3(-nx, 1.0f, -nz));

            float slope = 0.0f;
            if (x > 0 && x < segments && z > 0 && z < segments) {
                float dx = (heightCache[cj][ci + 1] - heightCache[cj][ci - 1]) / (2.0f * cellSize);
                float dz = (heightCache[cj + 1][ci] - heightCache[cj - 1][ci]) / (2.0f * cellSize);
                slope = std::sqrt(dx * dx + dz * dz);
            }

            TerrainBiome biome = getBiome(wx, wz, height, slope);
            glm::vec3 color = getBiomeColor(biome, height, slope);
            // 区块边界标红以便观察裂缝
            if (x == 0 || x == segments || z == 0 || z == segments) {
                color = glm::vec3(1.0f, 0.0f, 0.0f);
            }

            mesh.vertices.push_back({
                glm::vec3(wx, height, wz),
                normal,
                color,
                glm::vec2(wx / uvScale_, wz / uvScale_)
            });
        }
    }

    mesh.indices.reserve(segments * segments * 6);
    for (int z = 0; z < segments; ++z) {
        for (int x = 0; x < segments; ++x) {
            uint32_t tl = static_cast<uint32_t>(z) * (segments + 1) + x;
            uint32_t tr = tl + 1;
            uint32_t bl = (static_cast<uint32_t>(z) + 1) * (segments + 1) + x;
            uint32_t br = bl + 1;
            mesh.indices.push_back(tl);
            mesh.indices.push_back(bl);
            mesh.indices.push_back(tr);
            mesh.indices.push_back(tr);
            mesh.indices.push_back(bl);
            mesh.indices.push_back(br);
        }
    }

    return mesh;
}

ChunkMesh TerrainRenderer::generateFlatChunk(int chunkX, int chunkZ) const {
    const int segments = 16;
    const int vertsPerEdge = segments + 1;
    const float cellSize = chunkSize_ / static_cast<float>(segments);
    float startX = static_cast<float>(chunkX) * chunkSize_;
    float startZ = static_cast<float>(chunkZ) * chunkSize_;
    
    const int cacheSize = vertsPerEdge + 3;
    std::vector<std::vector<float>> heightCache(cacheSize,
                                                 std::vector<float>(cacheSize, 0.0f));
    for (int cz = 0; cz < cacheSize; ++cz) {
        for (int cx = 0; cx < cacheSize; ++cx) {
            float wx = startX + (static_cast<float>(cx) - 1.0f) * cellSize;
            float wz = startZ + (static_cast<float>(cz) - 1.0f) * cellSize;
            heightCache[cz][cx] = getHeight(wx, wz);
        }
    }
    
    ChunkMesh mesh;
    mesh.chunkX = chunkX;
    mesh.chunkZ = chunkZ;
    mesh.vertices.reserve(static_cast<size_t>(vertsPerEdge) * vertsPerEdge);
    
    for (int z = 0; z < vertsPerEdge; ++z) {
        for (int x = 0; x < vertsPerEdge; ++x) {
            float wx = startX + static_cast<float>(x) * cellSize;
            float wz = startZ + static_cast<float>(z) * cellSize;
            int ci = x + 1;
            int cj = z + 1;
            float height = heightCache[cj][ci];
            
            // FLAT TEST: 所有高度 0，法线朝上
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            
            // 区块边界红色
            glm::vec3 color(0.2f, 0.5f, 0.2f);
            if (x == 0 || x == segments || z == 0 || z == segments) {
                color = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            
            mesh.vertices.push_back({
                glm::vec3(wx, 0.0f, wz),
                normal,
                color,
                glm::vec2(wx / uvScale_, wz / uvScale_)
            });
        }
    }
    
    for (int z = 0; z < segments; ++z) {
        for (int x = 0; x < segments; ++x) {
            uint32_t topLeft = static_cast<uint32_t>(z) * vertsPerEdge + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (static_cast<uint32_t>(z) + 1) * vertsPerEdge + x;
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
    if (chunks_.find(key) != chunks_.end()) return;
    
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
    chunk.vertexBufferAllocation = poolSlot.vertexBufferAllocation;
    chunk.indexBuffer = poolSlot.indexBuffer;
    chunk.indexBufferAllocation = poolSlot.indexBufferAllocation;
    
    // 顶点数据上传（池缓冲区已持久映射，直接 memcpy）
    VkDeviceSize vertexBufferSize = sizeof(TerrainVertex) * mesh.vertices.size();
    memcpy(poolSlot.vertexMappedData, mesh.vertices.data(), static_cast<size_t>(vertexBufferSize));
    
    // 索引数据上传
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * mesh.indices.size();
    memcpy(poolSlot.indexMappedData, mesh.indices.data(), static_cast<size_t>(indexBufferSize));
    
    chunks_[key] = chunk;
}

void TerrainRenderer::generateChunk(int chunkX, int chunkZ) {
    ChunkKey key{chunkX, chunkZ};
    if (chunks_.find(key) != chunks_.end()) return;
    ChunkMesh mesh = computeChunkMesh(chunkX, chunkZ);
    uploadChunk(mesh);
}

void TerrainRenderer::cleanupChunk(TerrainChunk& chunk) {
    // 归还缓冲区到池（不销毁，池拥有生命周期）
    releasePoolSlot(chunk.poolSlot);
    chunk.poolSlot = -1;
    chunk.vertexBuffer = VK_NULL_HANDLE;
    chunk.vertexBufferAllocation = VK_NULL_HANDLE;
    chunk.indexBuffer = VK_NULL_HANDLE;
    chunk.indexBufferAllocation = VK_NULL_HANDLE;
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
    int playerChunkX = static_cast<int>(std::floor(playerPos.x / chunkSize_));
    int playerChunkZ = static_cast<int>(std::floor(playerPos.z / chunkSize_));
    
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
    const int candidateCount = (2 * generationRadius_ + 1) * (2 * generationRadius_ + 1);
    std::vector<std::pair<int, int>> candidates;
    candidates.reserve(static_cast<size_t>(candidateCount));
    for (int dz = -generationRadius_; dz <= generationRadius_; ++dz) {
        for (int dx = -generationRadius_; dx <= generationRadius_; ++dx) {
            int chunkX = playerChunkX + dx;
            int chunkZ = playerChunkZ + dz;
            
            float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (dist > static_cast<float>(generationRadius_)) continue;
            
            ChunkKey key{chunkX, chunkZ};
            if (chunks_.find(key) != chunks_.end()) continue;
            
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
    
    // TEST: 同步生成所有区块（绕过线程池和异步）
    for (const auto& [cX, cZ] : candidates) {
        generateChunk(cX, cZ);
    }
    
    // Phase 4: Remove chunks outside render radius + margin
    std::vector<ChunkKey> toRemove;
    toRemove.reserve(chunks_.size());
    for (const auto& pair : chunks_) {
        int dx = pair.first.x - playerChunkX;
        int dz = pair.first.z - playerChunkZ;
        float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
        if (dist > renderRadius_ + 2) {
            toRemove.push_back(pair.first);
        }
    }
    
    for (const auto& key : toRemove) {
        auto it = chunks_.find(key);
        if (it != chunks_.end()) {
            cleanupChunk(it->second);
            chunks_.erase(it);
        }
    }
}

void TerrainRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    PushConstants pushConstants{};
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    pushConstants.baseColor = glm::vec3(1.0f);  // 顶点色已包含生物群落着色
    pushConstants.metallic = 0.0f;
    pushConstants.roughness = 0.5f;
    pushConstants.hasTexture = 1;  // 使用草地贴图
    pushConstants._pad0 = 0.0f;
    pushConstants.normalScale = glm::vec3(1.0f);
    
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);
    
    // 绑定地形草地纹理描述符集
    if (terrainTexDescSet_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, &terrainTexDescSet_, 0, nullptr);
    }
    
    for (const auto& pair : chunks_) {
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