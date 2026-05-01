#include "renderer/grass_system.h"
#include "core/vulkan_device.h"
#include "core/camera.h"
#include "utils/logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_set>
#include <future>
#include <thread>

namespace owengine {

namespace {

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        Logger::error("[GrassSystem] 无法打开文件: " + filename);
        return {};
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    return buffer;
}

VkShaderModule createShaderModule_(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        Logger::error("[GrassSystem] 着色器模块创建失败");
        return VK_NULL_HANDLE;
    }
    return module;
}

struct PushBlock {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 timeParams;
    glm::vec4 playerPosVec;
};

} // anonymous namespace

GrassSystem::GrassSystem(std::shared_ptr<VulkanDevice> device)
    : device_(std::move(device)) {
}

GrassSystem::~GrassSystem() {
    cleanup();
}

void GrassSystem::init(const GrassConfig& cfg, VkRenderPass renderPass,
                       VkExtent2D extent, VkSampleCountFlagBits msaaSamples) {
    if (initialized_) return;
    config_ = cfg;

    // 生成三层 LOD 网格
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        generateBladeMesh(lod, lodVertices_[lod], lodIndices_[lod]);
        Logger::info("[GrassSystem] LOD" + std::to_string(lod) + ": " +
                     std::to_string(lodVertices_[lod].size()) + " 顶点, " +
                     std::to_string(lodIndices_[lod].size()) + " 索引");
    }

    createPipeline(renderPass, extent, msaaSamples);
    // 为每层 LOD 创建顶点/索引缓冲
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        createSingleLodBuffers(lod);
    }
    createInstanceBuffer(config_.maxBlades);

    // 生成初始区块（以原点为中心）
    updateChunks(glm::vec3(0.0f));

    Logger::info("[GrassSystem] 初始化完成, " + std::to_string(totalLoadedBlades_) + " 根草茎");
    initialized_ = true;
}

void GrassSystem::generateBladeMesh(int lod,
                                     std::vector<GrassVertex>& vertices,
                                     std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    float halfW = config_.bladeWidth * 0.5f;

    if (lod == 0) {
        // LOD0：弯曲叶片（20 顶点，48 索引）
        int segments = 4;     // 使用固定 4 分段
        int rings = segments + 1;
        vertices.reserve(static_cast<size_t>(rings) * 4);
        indices.reserve(static_cast<size_t>(segments) * 12);

        float curveStrength = 0.18f;
        for (int quad = 0; quad < 2; quad++) {
            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
            for (int i = 0; i < rings; i++) {
                float t = static_cast<float>(i) / static_cast<float>(segments);
                float widthScale;
                if (t < 0.15f) {
                    float bt = t / 0.15f;
                    widthScale = 0.7f + bt * 0.3f;
                } else {
                    float taperT = (t - 0.15f) / 0.85f;
                    widthScale = 1.0f - taperT * taperT;
                }
                float w = halfW * widthScale;
                float bend = curveStrength * t * t;
                float y = t;
                if (quad == 0) {
                    vertices.push_back({{-w, y, bend}, {0.0f, t}});
                    vertices.push_back({{ w, y, bend}, {1.0f, t}});
                } else {
                    vertices.push_back({{bend, y, -w}, {0.0f, t}});
                    vertices.push_back({{bend, y,  w}, {1.0f, t}});
                }
            }
            for (int i = 0; i < segments; i++) {
                uint32_t bl = baseIdx + i * 2;
                uint32_t br = baseIdx + i * 2 + 1;
                uint32_t tl = baseIdx + (i + 1) * 2;
                uint32_t tr = baseIdx + (i + 1) * 2 + 1;
                indices.push_back(bl); indices.push_back(br); indices.push_back(tl);
                indices.push_back(br); indices.push_back(tr); indices.push_back(tl);
            }
        }
    } else if (lod == 1) {
        // LOD1：十字交叉面片（8 顶点，12 索引），无弯曲
        vertices.reserve(8);
        indices.reserve(12);
        // Quad 1: XY 平面
        vertices.push_back({{-halfW, 0.0f, 0.0f}, {0.0f, 0.0f}});
        vertices.push_back({{-halfW, 1.0f, 0.0f}, {0.0f, 1.0f}});
        vertices.push_back({{ halfW, 1.0f, 0.0f}, {1.0f, 1.0f}});
        vertices.push_back({{ halfW, 0.0f, 0.0f}, {1.0f, 0.0f}});
        // Quad 2: ZY 平面
        vertices.push_back({{0.0f, 0.0f, -halfW}, {0.0f, 0.0f}});
        vertices.push_back({{0.0f, 1.0f, -halfW}, {0.0f, 1.0f}});
        vertices.push_back({{0.0f, 1.0f,  halfW}, {1.0f, 1.0f}});
        vertices.push_back({{0.0f, 0.0f,  halfW}, {1.0f, 0.0f}});
        // 索引
        indices.push_back(0); indices.push_back(1); indices.push_back(2);
        indices.push_back(0); indices.push_back(2); indices.push_back(3);
        indices.push_back(4); indices.push_back(5); indices.push_back(6);
        indices.push_back(4); indices.push_back(6); indices.push_back(7);
    } else {
        // LOD2：单面片（4 顶点，6 索引），略宽
        float w = halfW * 1.2f;
        vertices.reserve(4);
        indices.reserve(6);
        vertices.push_back({{-w, 0.0f, 0.0f}, {0.0f, 0.0f}});
        vertices.push_back({{-w, 1.0f, 0.0f}, {0.0f, 1.0f}});
        vertices.push_back({{ w, 1.0f, 0.0f}, {1.0f, 1.0f}});
        vertices.push_back({{ w, 0.0f, 0.0f}, {1.0f, 0.0f}});
        indices.push_back(0); indices.push_back(1); indices.push_back(2);
        indices.push_back(0); indices.push_back(2); indices.push_back(3);
    }
}

void GrassSystem::generateGrassInChunk(const GrassConfig& cfg, int cx, int cz,
                                        std::mt19937& rng) {
    GrassChunkKey key{cx, cz};
    if (chunkData_.count(key)) return;

    auto blades = generateChunkBlades(cfg, cx, cz, rng);
    totalLoadedBlades_ += blades.size();
    chunkData_[key] = std::move(blades);
}

// 仅生成区块草数据，不操作 chunkData_（线程安全，供 async 调用）
std::vector<GrassInstanceData> GrassSystem::generateChunkBlades(
    const GrassConfig& cfg, int cx, int cz, std::mt19937& rng) {

    std::vector<GrassInstanceData> blades;
    blades.reserve(static_cast<size_t>(cfg.chunkSize * cfg.chunkSize * cfg.density * 1.5f));

    double lambda = cfg.density * cfg.chunkSize * cfg.chunkSize;
    std::poisson_distribution<int> poisson(lambda);
    int count = poisson(rng);
    if (count <= 0) return blades;

    float worldX0 = static_cast<float>(cx) * cfg.chunkSize;
    float worldZ0 = static_cast<float>(cz) * cfg.chunkSize;

    std::uniform_real_distribution<float> posOffset(0.0f, cfg.chunkSize);
    std::uniform_real_distribution<float> scaleGen(cfg.bladeHeightMin, cfg.bladeHeightMax);
    std::uniform_real_distribution<float> yawGen(0.0f, 6.28318f);
    std::uniform_real_distribution<float> seedGen(0.0f, 1.0f);

    for (int i = 0; i < count; i++) {
        for (int attempt = 0; attempt < 8; attempt++) {
            float wx = worldX0 + posOffset(rng);
            float wz = worldZ0 + posOffset(rng);
            float y = heightSampler_ ? heightSampler_(wx, wz) : 0.0f;

            GrassInstanceData inst;
            inst.position = {wx, y, wz};
            inst.yaw = yawGen(rng);
            inst.scale = scaleGen(rng);
            inst.windSeed = seedGen(rng);
            inst.pushState = 0.0f;
            blades.push_back(inst);
            break;
        }
    }
    return blades;
}

void GrassSystem::updateChunks(const glm::vec3& playerPos) {
    int px = static_cast<int>(std::floor(playerPos.x / config_.chunkSize));
    int pz = static_cast<int>(std::floor(playerPos.z / config_.chunkSize));

    // 仅在玩家跨越区块边界时更新
    if (chunkPositionInitialized_ && px == lastPlayerChunkX_ && pz == lastPlayerChunkZ_) {
        return;
    }
    lastPlayerChunkX_ = px;
    lastPlayerChunkZ_ = pz;
    chunkPositionInitialized_ = true;

    // 构建期望加载的区块集合
    std::unordered_set<GrassChunkKey, GrassChunkKeyHash> keep;
    keep.reserve(static_cast<size_t>((config_.loadRadius * 2 + 1) * (config_.loadRadius * 2 + 1)));
    for (int dz = -config_.loadRadius; dz <= config_.loadRadius; dz++) {
        for (int dx = -config_.loadRadius; dx <= config_.loadRadius; dx++) {
            keep.insert({px + dx, pz + dz});
        }
    }

    // 卸载超出范围的区块
    for (auto it = chunkData_.begin(); it != chunkData_.end(); ) {
        if (!keep.count(it->first)) {
            totalLoadedBlades_ -= it->second.size();
            it = chunkData_.erase(it);
        } else {
            ++it;
        }
    }

    // 加载新进入范围的区块（按距离从近到远排序，确保玩家附近优先）
    // 且使用 std::async 并行生成，减少帧时间抖动
    std::vector<GrassChunkKey> sortedKeys;
    sortedKeys.reserve(keep.size());
    for (const auto& k : keep) sortedKeys.push_back(k);
    std::sort(sortedKeys.begin(), sortedKeys.end(),
        [px, pz](const GrassChunkKey& a, const GrassChunkKey& b) {
            int da = (a.x - px) * (a.x - px) + (a.z - pz) * (a.z - pz);
            int db = (b.x - px) * (b.x - px) + (b.z - pz) * (b.z - pz);
            return da < db;
        });

    // 收集需要生成的新区块
    std::vector<GrassChunkKey> newKeys;
    for (const auto& key : sortedKeys) {
        if (chunkData_.count(key)) continue;
        if (totalLoadedBlades_ >= static_cast<size_t>(config_.maxBlades)) {
            Logger::warning("[GrassSystem] 草茎总数已达上限 " +
                         std::to_string(config_.maxBlades) + "，停止加载新区块");
            break;
        }
        newKeys.push_back(key);
    }

    if (!newKeys.empty()) {
        // 并行生成区块数据（线程安全：generateChunkBlades 不修改 chunkData_）
        struct GenResult {
            GrassChunkKey key;
            std::vector<GrassInstanceData> blades;
        };
        std::vector<std::future<GenResult>> futures;
        futures.reserve(newKeys.size());

        for (const auto& key : newKeys) {
            futures.push_back(std::async(std::launch::async,
                [this, key]() -> GenResult {
                    std::mt19937 chunkGen(key.x * 100000 + key.z);
                    return {key, generateChunkBlades(config_, key.x, key.z, chunkGen)};
                }));
        }

        // 主线程依次插入结果
        for (auto& f : futures) {
            auto result = f.get();
            totalLoadedBlades_ += result.blades.size();
            chunkData_[result.key] = std::move(result.blades);
        }
    }
}

void GrassSystem::updatePushStates(const glm::vec3& playerPos, float deltaTime) {
    float playerRadius = config_.playerRadius * 1.5f;
    float attackSpeed = 5.0f;   // 挤压蓄力速度（越快草茎响应越灵敏）
    float decayRate  = 3.0f;    // 弹簧恢复速率（越大回弹越快）
    float radiusSq = playerRadius * playerRadius;
    // 只有玩家附近几个区块可能有交互，快速跳过远区块
    float chunkSkipDist = playerRadius + config_.chunkSize;
    float chunkSkipDistSq = chunkSkipDist * chunkSkipDist;

    for (auto& [key, blades] : chunkData_) {
        // 区块级快速跳过：计算区块中心到玩家的平方距离
        float cx = (static_cast<float>(key.x) + 0.5f) * config_.chunkSize - playerPos.x;
        float cz = (static_cast<float>(key.z) + 0.5f) * config_.chunkSize - playerPos.z;
        if (cx * cx + cz * cz > chunkSkipDistSq) {
            // 整个区块超出交互范围，pushState 保持 0，无需处理
            continue;
        }
        for (auto& inst : blades) {
            float dx = inst.position.x - playerPos.x;
            float dy = inst.position.y - playerPos.y;
            float dz = inst.position.z - playerPos.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq < radiusSq) {
                float dist = std::sqrt(distSq);
                float influence = 1.0f - (dist / playerRadius);
                float target = influence * influence;
                inst.pushState = std::min(1.0f,
                    inst.pushState + (target - inst.pushState) * attackSpeed * deltaTime);
            } else {
                // 玩家远离：指数衰减模拟弹簧恢复
                inst.pushState *= std::exp(-decayRate * deltaTime);
                if (inst.pushState < 0.001f) inst.pushState = 0.0f;
            }
        }
    }
}

void GrassSystem::update(const glm::vec3& playerPos, const Camera& camera,
                         float deltaTime) {
    if (!initialized_) return;
    time_ += deltaTime;
    playerPosition_ = playerPos;

    // Phase 1: 动态区块加载/卸载
    updateChunks(playerPos);

    // Phase 2: 更新草茎挤压弹簧状态（角色交互恢复动画）
    updatePushStates(playerPos, deltaTime);

    // Phase 3: 多线程并行遍历区块，按 LOD 分层构建可见草茎列表
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        lodVisibleInstances_[lod].clear();
    }
    size_t totalReserve = std::min(totalLoadedBlades_,
                           static_cast<size_t>(config_.maxBlades));
    lodVisibleInstances_[0].reserve(totalReserve / 3);
    lodVisibleInstances_[1].reserve(totalReserve / 3);
    lodVisibleInstances_[2].reserve(totalReserve / 3);

    auto& frustum = camera.getFrustum();
    glm::vec3 camPos = camera.getPosition();

    // 预计算平方距离阈值，避免每根草重复 sqrt
    float renderDistSq = config_.renderDistance * config_.renderDistance;
    float frustumDistSq = 400.0f;
    float densLowDist = config_.renderDistance * 0.2f;
    float densRange = config_.renderDistance * 0.8f;
    float lod0Sq = LOD_DIST_0 * LOD_DIST_0;
    float chunkMaxDist = config_.renderDistance + config_.chunkSize * 0.707f;
    float chunkMaxDistSq = chunkMaxDist * chunkMaxDist;

    // 收集区块键（避免多线程并发遍历 unordered_map）
    std::vector<GrassChunkKey> chunkKeys;
    chunkKeys.reserve(chunkData_.size());
    for (const auto& pair : chunkData_) {
        chunkKeys.push_back(pair.first);
    }

    // 按可用硬件线程数分块处理
    const size_t numThreads = std::min<size_t>(
        std::thread::hardware_concurrency(),
        (chunkKeys.size() + 15) / 16);  // 每线程至少 16 区块
    const size_t numActive = std::min(numThreads, chunkKeys.size());

    if (numActive <= 1) {
        // 单线程回退
        for (const auto& key : chunkKeys) {
            auto it = chunkData_.find(key);
            if (it == chunkData_.end()) continue;
            const auto& blades = it->second;
            float cx = (static_cast<float>(key.x) + 0.5f) * config_.chunkSize - camPos.x;
            float cz = (static_cast<float>(key.z) + 0.5f) * config_.chunkSize - camPos.z;
            if (cx * cx + cz * cz > chunkMaxDistSq) continue;

            for (const auto& inst : blades) {
                float dx = inst.position.x - camPos.x;
                float dy = inst.position.y - camPos.y;
                float dz = inst.position.z - camPos.z;
                float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq > renderDistSq) continue;
                if (distSq > frustumDistSq && distSq < lod0Sq &&
                    !frustum.isSphereInside(inst.position, config_.bladeHeightMax)) continue;
                float dist = std::sqrt(distSq);
                float keepProb = 1.0f;
                if (dist > densLowDist) {
                    float t = std::min((dist - densLowDist) / densRange, 1.0f);
                    keepProb = 1.0f - t * 0.9f;
                }
                if (glm::fract(inst.windSeed * 43758.5453f) > keepProb) continue;
                if (dist < LOD_DIST_0) lodVisibleInstances_[0].push_back(inst);
                else if (dist < LOD_DIST_1) lodVisibleInstances_[1].push_back(inst);
                else lodVisibleInstances_[2].push_back(inst);
            }
        }
    } else {
        // 多线程并行处理
        struct CullResult {
            std::array<std::vector<GrassInstanceData>, LOD_COUNT> lod;
        };

        const size_t chunksPerThread = (chunkKeys.size() + numActive - 1) / numActive;
        std::vector<std::future<CullResult>> futures;
        futures.reserve(numActive);

        // 每个线程处理一段连续区块
        // 只捕获只读数据避免竞态：chunkData_ 只读、预计算值是纯量
        for (size_t t = 0; t < numActive; t++) {
            size_t start = t * chunksPerThread;
            size_t end = std::min(start + chunksPerThread, chunkKeys.size());
            futures.push_back(std::async(std::launch::async,
                [&, start, end]() -> CullResult {
                    CullResult result;
                    size_t reserveEst = (end - start) * 3000;
                    for (int lod = 0; lod < LOD_COUNT; lod++) {
                        result.lod[lod].reserve(reserveEst / 3);
                    }
                    for (size_t i = start; i < end; i++) {
                        const auto& key = chunkKeys[i];
                        auto it = chunkData_.find(key);
                        if (it == chunkData_.end()) continue;
                        const auto& blades = it->second;
                        float cx = (static_cast<float>(key.x) + 0.5f) * config_.chunkSize - camPos.x;
                        float cz = (static_cast<float>(key.z) + 0.5f) * config_.chunkSize - camPos.z;
                        if (cx * cx + cz * cz > chunkMaxDistSq) continue;

                        for (const auto& inst : blades) {
                            float dx = inst.position.x - camPos.x;
                            float dy = inst.position.y - camPos.y;
                            float dz = inst.position.z - camPos.z;
                            float distSq = dx * dx + dy * dy + dz * dz;
                            if (distSq > renderDistSq) continue;
                            if (distSq > frustumDistSq && distSq < lod0Sq &&
                                !frustum.isSphereInside(inst.position, config_.bladeHeightMax)) continue;
                            float dist = std::sqrt(distSq);
                            float keepProb = 1.0f;
                            if (dist > densLowDist) {
                                float t = std::min((dist - densLowDist) / densRange, 1.0f);
                                keepProb = 1.0f - t * 0.9f;
                            }
                            if (glm::fract(inst.windSeed * 43758.5453f) > keepProb) continue;
                            if (dist < LOD_DIST_0) result.lod[0].push_back(inst);
                            else if (dist < LOD_DIST_1) result.lod[1].push_back(inst);
                            else result.lod[2].push_back(inst);
                        }
                    }
                    return result;
                }));
        }

        // 合并所有线程结果
        for (auto& f : futures) {
            auto result = f.get();
            for (int lod = 0; lod < LOD_COUNT; lod++) {
                auto& src = result.lod[lod];
                if (!src.empty()) {
                    lodVisibleInstances_[lod].insert(
                        lodVisibleInstances_[lod].end(), src.begin(), src.end());
                }
            }
        }
    }

    // Phase 4: 上传可见实例到 GPU（双缓冲交替写入）
    currentInstanceBuffer_ = (currentInstanceBuffer_ + 1) % INSTANCE_BUFFER_COUNT;
    void* mappedBuf = mappedInstanceDatas_[currentInstanceBuffer_];
    VkDeviceSize totalUpload = 0;
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        totalUpload += lodVisibleInstances_[lod].size() * sizeof(GrassInstanceData);
    }
    if (totalUpload > 0 && mappedBuf != nullptr) {
        VkDeviceSize bufferBytes = static_cast<VkDeviceSize>(instanceBufferCapacity_) *
                                   sizeof(GrassInstanceData);
        if (totalUpload > bufferBytes) {
            Logger::warning("[GrassSystem] 可见草茎数超过缓冲容量 " +
                         std::to_string(instanceBufferCapacity_));
            while (totalUpload > bufferBytes && !lodVisibleInstances_[2].empty()) {
                totalUpload -= sizeof(GrassInstanceData);
                lodVisibleInstances_[2].pop_back();
            }
        }
        VkDeviceSize offset = 0;
        for (int lod = 0; lod < LOD_COUNT; lod++) {
            lodInstanceOffsets_[lod] = offset;
            size_t bytes = lodVisibleInstances_[lod].size() * sizeof(GrassInstanceData);
            if (bytes > 0) {
                memcpy(static_cast<char*>(mappedBuf) + offset,
                       lodVisibleInstances_[lod].data(), bytes);
            }
            offset += bytes;
        }
    }
}

void GrassSystem::render(VkCommandBuffer commandBuffer, const Camera& camera) {
    if (!initialized_) return;
    // 检查是否有任何 LOD 层可见
    bool anyVisible = false;
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        if (!lodVisibleInstances_[lod].empty()) { anyVisible = true; break; }
    }
    if (!anyVisible) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    PushBlock push;
    push.view = camera.getViewMatrix();
    push.proj = camera.getProjectionMatrix();
    push.timeParams = glm::vec4(time_, config_.windStrength,
                                 config_.playerRadius, config_.playerForce);
    push.playerPosVec = glm::vec4(playerPosition_, 1.0f);

    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(PushBlock), &push);

    // 逐 LOD 层绘制
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        size_t count = lodVisibleInstances_[lod].size();
        if (count == 0) continue;

        VkBuffer vbs[] = {lodVertexBuffers_[lod], instanceBuffers_[currentInstanceBuffer_]};
        VkDeviceSize offsets[] = {0, lodInstanceOffsets_[lod]};
        vkCmdBindVertexBuffers(commandBuffer, 0, 2, vbs, offsets);
        vkCmdBindIndexBuffer(commandBuffer, lodIndexBuffers_[lod], 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(commandBuffer,
                         static_cast<uint32_t>(lodIndices_[lod].size()),
                         static_cast<uint32_t>(count), 0, 0, 0);
    }
}

void GrassSystem::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                  VkSampleCountFlagBits msaaSamples) {
    auto vertCode = readFile("shaders/grass.vert.spv");
    auto fragCode = readFile("shaders/grass.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        Logger::error("[GrassSystem] 着色器文件读取失败");
        return;
    }

    VkShaderModule vertModule = createShaderModule_(device_->getDevice(), vertCode);
    VkShaderModule fragModule = createShaderModule_(device_->getDevice(), fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) return;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(GrassVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(GrassInstanceData);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    // 顶点属性：位置(location=0) + UV(location=1) | 实例属性(location=2~6)
    VkVertexInputAttributeDescription attrs[7] = {};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassVertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GrassVertex, uv)};
    attrs[2] = {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassInstanceData, position)};
    attrs[3] = {3, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, yaw)};
    attrs[4] = {4, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, scale)};
    attrs[5] = {5, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, windSeed)};
    attrs[6] = {6, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, pushState)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 2;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = 7;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = msaaSamples;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlend{};
    colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlend;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushBlock);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device_->getDevice(), &layoutInfo, nullptr,
                               &pipelineLayout_) != VK_SUCCESS) {
        Logger::error("[GrassSystem] 管线布局创建失败");
        vkDestroyShaderModule(device_->getDevice(), vertModule, nullptr);
        vkDestroyShaderModule(device_->getDevice(), fragModule, nullptr);
        return;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_->getDevice(), VK_NULL_HANDLE, 1,
                                  &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        Logger::error("[GrassSystem] 图形管线创建失败");
    }

    vkDestroyShaderModule(device_->getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(device_->getDevice(), fragModule, nullptr);
}

void GrassSystem::createSingleLodBuffers(int lod) {
    VkDevice dev = device_->getDevice();
    VkDeviceSize vbSize = sizeof(GrassVertex) * lodVertices_[lod].size();
    VkDeviceSize ibSize = sizeof(uint32_t) * lodIndices_[lod].size();

    auto uploadBuffer = [&](VkBuffer& buf, VkDeviceMemory& mem,
                            VkDeviceSize size, VkBufferUsageFlags usage,
                            const void* data) {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev, &info, nullptr, &buf) != VK_SUCCESS) return false;

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, buf, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = device_->findMemoryType(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(dev, &alloc, nullptr, &mem) != VK_SUCCESS) return false;
        vkBindBufferMemory(dev, buf, mem, 0);

        if (data && size > 0) {
            void* mapped;
            vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &mapped);
            memcpy(mapped, data, static_cast<size_t>(size));
            vkUnmapMemory(dev, mem);
        }
        return true;
    };

    uploadBuffer(lodVertexBuffers_[lod], lodVertexBufferMemories_[lod],
                 vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 lodVertices_[lod].data());
    uploadBuffer(lodIndexBuffers_[lod], lodIndexBufferMemories_[lod],
                 ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 lodIndices_[lod].data());
}

void GrassSystem::createInstanceBuffer(int maxBlades) {
    VkDevice dev = device_->getDevice();
    VkDeviceSize size = sizeof(GrassInstanceData) * maxBlades;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    for (int i = 0; i < INSTANCE_BUFFER_COUNT; i++) {
        if (vkCreateBuffer(dev, &info, nullptr, &instanceBuffers_[i]) != VK_SUCCESS) {
            Logger::error("[GrassSystem] 实例缓冲 " + std::to_string(i) + " 创建失败");
            return;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, instanceBuffers_[i], &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = device_->findMemoryType(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(dev, &alloc, nullptr, &instanceBufferMemories_[i]) != VK_SUCCESS) {
            Logger::error("[GrassSystem] 实例缓冲内存 " + std::to_string(i) + " 分配失败");
            return;
        }
        vkBindBufferMemory(dev, instanceBuffers_[i], instanceBufferMemories_[i], 0);
        vkMapMemory(dev, instanceBufferMemories_[i], 0, VK_WHOLE_SIZE, 0, &mappedInstanceDatas_[i]);
    }
    instanceBufferCapacity_ = maxBlades;
    currentInstanceBuffer_ = 0;
}

void GrassSystem::cleanup() {
    VkDevice dev = device_->getDevice();

    if (pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(dev, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }

    // 清理三层 LOD 的顶点/索引缓冲
    auto destroyBuf = [&](VkBuffer& b, VkDeviceMemory& m) {
        if (b != VK_NULL_HANDLE) { vkDestroyBuffer(dev, b, nullptr); b = VK_NULL_HANDLE; }
        if (m != VK_NULL_HANDLE) { vkFreeMemory(dev, m, nullptr); m = VK_NULL_HANDLE; }
    };
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        destroyBuf(lodVertexBuffers_[lod], lodVertexBufferMemories_[lod]);
        destroyBuf(lodIndexBuffers_[lod], lodIndexBufferMemories_[lod]);
    }
    // 清理双缓冲实例缓冲
    for (int i = 0; i < INSTANCE_BUFFER_COUNT; i++) {
        if (mappedInstanceDatas_[i] != nullptr) {
            vkUnmapMemory(dev, instanceBufferMemories_[i]);
            mappedInstanceDatas_[i] = nullptr;
        }
        destroyBuf(instanceBuffers_[i], instanceBufferMemories_[i]);
    }

    for (int lod = 0; lod < LOD_COUNT; lod++) {
        lodVisibleInstances_[lod].clear();
        lodVertices_[lod].clear();
        lodIndices_[lod].clear();
    }
    chunkData_.clear();
    totalLoadedBlades_ = 0;
    initialized_ = false;
}

} // namespace owengine
