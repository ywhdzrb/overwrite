#include "renderer/grass_system.h"
#include "core/vulkan_device.h"
#include "core/camera.h"
#include "utils/logger.h"
#include "utils/asset_paths.h"
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
    } else if (lod == 2) {
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
    } else {
        // LOD3：草簇（4 顶点，6 索引）— 宽扁面片模拟草堆
        // 宽度翻倍，高度减半，看起来像一丛草
        float cw = halfW * 3.0f;
        float ch = 0.4f;
        vertices.reserve(4);
        indices.reserve(6);
        vertices.push_back({{-cw, 0.0f, 0.0f}, {0.0f, 0.0f}});
        vertices.push_back({{-cw,   ch, 0.0f}, {0.0f, 1.0f}});
        vertices.push_back({{ cw,   ch, 0.0f}, {1.0f, 1.0f}});
        vertices.push_back({{ cw, 0.0f, 0.0f}, {1.0f, 0.0f}});
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
    const GrassConfig& cfg, int cx, int cz, std::mt19937& rng,
    const std::vector<std::pair<glm::vec3, float>>& nearbyTrees,
    const std::vector<std::pair<glm::vec3, float>>& nearbyStones,
    const glm::vec3& lightDir) {

    std::vector<GrassInstanceData> blades;

    // 平滑空间噪声（值噪声 + 双线性插值 + smoothstep），用于密度/高度区域一致性
    // 相邻空间位置返回相近值，避免区块边界突变
    auto smoothNoise = [](float x, float z, float gridSpacing, int seed) -> float {
        float gx = x / gridSpacing;
        float gz = z / gridSpacing;
        int ix0 = static_cast<int>(std::floor(gx));
        int iz0 = static_cast<int>(std::floor(gz));
        float fx = gx - static_cast<float>(ix0);
        float fz = gz - static_cast<float>(iz0);
        auto hash = [seed](int ix, int iz) -> float {
            uint32_t h = static_cast<uint32_t>(ix * 374761393 + iz * 668265263 + seed * 1274126177);
            h = (h ^ (h >> 13)) * 1274126177;
            h = h ^ (h >> 16);
            return static_cast<float>(h) / 4294967296.0f;
        };
        float v00 = hash(ix0, iz0);
        float v10 = hash(ix0 + 1, iz0);
        float v01 = hash(ix0, iz0 + 1);
        float v11 = hash(ix0 + 1, iz0 + 1);
        float sx = fx * fx * (3.0f - 2.0f * fx);
        float sz = fz * fz * (3.0f - 2.0f * fz);
        float top = v00 + (v10 - v00) * sx;
        float bottom = v01 + (v11 - v01) * sx;
        return top + (bottom - top) * sz;
    };

    float centerX = (static_cast<float>(cx) + 0.5f) * cfg.chunkSize;
    float centerZ = (static_cast<float>(cz) + 0.5f) * cfg.chunkSize;

    // P0: 三层噪声叠加 — 48m 粗粒度(大范围肥/贫斑块) + 24m 中粒度 + 12m 细粒度微调
    // 三层权重 4:4:2，产生从大面积生物群落到局部微气候的多尺度斑驳效果
    float densityLarge = smoothNoise(centerX, centerZ, 48.0f, 0);
    float densityCoarse = smoothNoise(centerX, centerZ, 24.0f, 500);
    float densityFine   = smoothNoise(centerX, centerZ, 12.0f, 1000);
    float densityNoise  = densityLarge * 0.4f + densityCoarse * 0.4f + densityFine * 0.2f;
    double densityMult  = 0.02 + densityNoise * 3.98;  // 0.02x ~ 4.0x（更极端的疏密对比）
    double lambda = cfg.density * cfg.chunkSize * cfg.chunkSize * densityMult;
    std::poisson_distribution<int> poisson(lambda);
    int count = poisson(rng);
    if (count <= 0) return blades;

    // 高度区域基准：三层噪声叠加，产生明显的高草区/矮草区对比
    float heightLarge  = smoothNoise(centerX, centerZ, 48.0f, 10000);
    float heightCoarse = smoothNoise(centerX, centerZ, 24.0f, 20000);
    float heightFine   = smoothNoise(centerX, centerZ, 12.0f, 30000);
    float heightNoise  = heightLarge * 0.4f + heightCoarse * 0.4f + heightFine * 0.2f;
    float zoneBase     = cfg.bladeHeightMin + heightNoise * (cfg.bladeHeightMax - cfg.bladeHeightMin);

    blades.reserve(static_cast<size_t>(count));

    float worldX0 = static_cast<float>(cx) * cfg.chunkSize;
    float worldZ0 = static_cast<float>(cz) * cfg.chunkSize;

    std::uniform_real_distribution<float> posOffset(0.0f, cfg.chunkSize);
    std::uniform_real_distribution<float> yawGen(0.0f, 6.28318f);
    std::uniform_real_distribution<float> seedGen(0.0f, 1.0f);
    std::uniform_real_distribution<float> heightNormGen(0.0f, 1.0f);

    for (int i = 0; i < count; i++) {
        for (int attempt = 0; attempt < 8; attempt++) {
            float wx = worldX0 + posOffset(rng);
            float wz = worldZ0 + posOffset(rng);
            float y = heightSampler_ ? heightSampler_(wx, wz) : 0.0f;

            GrassInstanceData inst;
            inst.position = {wx, y, wz};
            inst.yaw = yawGen(rng);
            // 区域基准高度 ±40% 波动，区块内仍有明显高度差异
            float h = (heightNormGen(rng) + heightNormGen(rng)) * 0.5f;
            float variation = 1.0f + (h - 0.5f) * 0.8f;
            inst.scale = std::clamp(zoneBase * variation, cfg.bladeHeightMin, cfg.bladeHeightMax);
            inst.windSeed = seedGen(rng);
            inst.pushState = 0.0f;

            // ================ P2: 树邻近影响 ================
            bool skipByTree = false;
            for (const auto& [treePos, treeScale] : nearbyTrees) {
                float dx = wx - treePos.x;
                float dz = wz - treePos.z;
                float dist = std::sqrt(dx * dx + dz * dz);
                // 最小影响半径：即使小树（scale=0.3）也有视觉上可见的草地影响
                float trunkR  = std::max(0.4f * treeScale, 1.0f);   // 树干周围 ≥1m 不长草
                float canopyR = std::max(3.5f * treeScale, 5.0f);   // 树冠投影 ≥5m
                float boostR  = canopyR * 1.4f;                     // 树荫外茂盛区

                if (dist < trunkR) {
                    skipByTree = true;  // 树干周围：不长草
                    break;
                } else if (dist < canopyR) {
                    // 树荫内：草绝大部分消失，剩下极矮
                    float t = (dist - trunkR) / (canopyR - trunkR);
                    float skipProb = 0.95f * (1.0f - t * t * 0.9f);
                    if (heightNormGen(rng) < skipProb) {
                        skipByTree = true;
                        break;
                    }
                    inst.scale *= 0.2f + 0.6f * t;  // 树干旁=20%高→边缘=80%高
                } else if (dist < boostR) {
                    // 树荫外稍远处：草最茂盛
                    float t = (dist - canopyR) / (boostR - canopyR);
                    inst.scale *= 1.0f + 0.4f * (1.0f - t);
                }
                // 超出 boostR 范围则不受影响
            }
            if (skipByTree) continue;

            // ================ P2+P3: 石头邻近影响 ================
            bool skipByStone = false;
            for (const auto& [stonePos, stoneScale] : nearbyStones) {
                float dx = wx - stonePos.x;
                float dz = wz - stonePos.z;
                float dist = std::sqrt(dx * dx + dz * dz);
                float stoneR = std::max(0.7f * stoneScale, 1.2f);  // 石头影响半径 ≥1.2m
                float edgeR  = stoneR * 2.0f;                      // 石缝/边缘

                if (dist < stoneR * 0.3f) {
                    skipByStone = true;  // 贴身：不长草
                    break;
                } else if (dist < stoneR) {
                    // 石头根部附近：草稀矮
                    float t = (dist - stoneR * 0.3f) / (stoneR * 0.7f);
                    float skipProb = 0.85f * (1.0f - t * 0.8f);
                    if (heightNormGen(rng) < skipProb) {
                        skipByStone = true;
                        break;
                    }
                    inst.scale *= 0.3f + 0.5f * t;
                } else if (dist < edgeR) {
                    // 石缝/边缘：草更密更高
                    float t = (dist - stoneR) / (edgeR - stoneR);
                    inst.scale *= 1.0f + 0.3f * (1.0f - t);
                }

                // P3: 石头背阴面草衰减
                if (dist < edgeR) {
                    glm::vec2 toStone = glm::normalize(glm::vec2(stonePos.x - wx, stonePos.z - wz));
                    glm::vec2 light2D = glm::normalize(glm::vec2(lightDir.x, lightDir.z));
                    // 若草在石头背向光源的一侧，dot > 0 表示草→石方向与光方向同向
                    float shadowDot = glm::dot(toStone, light2D);
                    if (shadowDot > 0.2f) {
                        float shadowStrength = std::min(shadowDot, 1.0f);
                        inst.scale *= 1.0f - shadowStrength * 0.25f;
                        // 背阴面额外密度削减：以 30% 概率跳过
                        if (heightNormGen(rng) < shadowStrength * 0.3f) {
                            skipByStone = true;
                            break;
                        }
                    }
                }
            }
            if (skipByStone) continue;

            inst.scale = std::clamp(inst.scale, cfg.bladeHeightMin, cfg.bladeHeightMax);
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

        // 主线程预先查询邻近树/石位置，按区块中心分组传递（线程安全：主线程独占）
        // 邻近查询半径：最大影响距离 ≈ 树冠茂盛区 boostR 上限 (3.5*maxScale*1.6 ≈ 11m)
        const float proximityRadius = 15.0f;
        std::vector<std::vector<std::pair<glm::vec3, float>>> treeDataPerKey(newKeys.size());
        std::vector<std::vector<std::pair<glm::vec3, float>>> stoneDataPerKey(newKeys.size());
        if (treeQuery_ || stoneQuery_) {
            for (size_t ki = 0; ki < newKeys.size(); ki++) {
                const auto& key = newKeys[ki];
                float cx = (static_cast<float>(key.x) + 0.5f) * config_.chunkSize;
                float cz = (static_cast<float>(key.z) + 0.5f) * config_.chunkSize;
                if (treeQuery_) treeDataPerKey[ki] = treeQuery_(cx, cz, proximityRadius);
                if (stoneQuery_) stoneDataPerKey[ki] = stoneQuery_(cx, cz, proximityRadius);
            }
        }

        for (size_t ki = 0; ki < newKeys.size(); ki++) {
            const auto& key = newKeys[ki];
            // 按值捕获邻近数据，线程安全
            auto nearbyTrees = treeDataPerKey[ki];
            auto nearbyStones = stoneDataPerKey[ki];
            auto lightDir = lightDir_;
            futures.push_back(std::async(std::launch::async,
                [this, key, nearbyTrees, nearbyStones, lightDir]() -> GenResult {
                    std::mt19937 chunkGen(key.x * 100000 + key.z);
                    return {key, generateChunkBlades(config_, key.x, key.z, chunkGen,
                                                     nearbyTrees, nearbyStones, lightDir)};
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

    // 若相机未明显移动，跳过 LOD 剔除（保留上一帧结果），减少每帧开销
    glm::vec3 camPos = camera.getPosition();
    glm::vec3 camDelta = camPos - lastCullCameraPos_;
    float camMoveSq = camDelta.x * camDelta.x + camDelta.y * camDelta.y + camDelta.z * camDelta.z;
    if (camMoveSq < CULL_MOVE_THRESHOLD * CULL_MOVE_THRESHOLD) {
        // 仍需要更新已上传数据中的 playerPos（用于着色器交互形变）
        return;
    }
    lastCullCameraPos_ = camPos;

    // Phase 3: 单线程遍历区块，按 LOD 分层构建可见草茎列表
    // 4 核 CPU 上 std::async 线程创建/切换开销 > 并行收益，故强制单线程
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        lodVisibleInstances_[lod].clear();
    }
    size_t totalReserve = std::min(totalLoadedBlades_,
                           static_cast<size_t>(config_.maxBlades));
    lodVisibleInstances_[0].reserve(totalReserve / 2);
    lodVisibleInstances_[1].reserve(totalReserve / 3);
    lodVisibleInstances_[2].reserve(totalReserve / 3);

    auto& frustum = camera.getFrustum();
    // 相机朝向 XZ 水平投影（用于区块级背面剔除）
    glm::vec3 camFront3D = camera.getFront();
    float frontLenSq = camFront3D.x * camFront3D.x + camFront3D.z * camFront3D.z;
    glm::vec2 camDir2D = frontLenSq > 0.0001f
        ? glm::vec2(camFront3D.x, camFront3D.z) * (1.0f / std::sqrt(frontLenSq))
        : glm::vec2(0.0f, 0.0f);

    float renderDistSq = config_.renderDistance * config_.renderDistance;
    float frustumDistSq = 400.0f;
    float densLowDist = config_.renderDistance * 0.2f;
    float densRange = config_.renderDistance * 0.8f;
    float lod0Sq = LOD_DIST_0 * LOD_DIST_0;
    float chunkMaxDist = config_.renderDistance + config_.chunkSize * 0.707f;
    float chunkMaxDistSq = chunkMaxDist * chunkMaxDist;
    float chunkSize = config_.chunkSize;

    for (const auto& pair : chunkData_) {
        const auto& key = pair.first;
        const auto& blades = pair.second;

        // 区块中心相对相机偏移（水平 2D）
        float cx = (static_cast<float>(key.x) + 0.5f) * chunkSize - camPos.x;
        float cz = (static_cast<float>(key.z) + 0.5f) * chunkSize - camPos.z;
        float chunkDistSq = cx * cx + cz * cz;
        if (chunkDistSq > chunkMaxDistSq) continue;

        // 区块级背面剔除：明显在相机后方且不太近时跳过整块
        if (chunkDistSq > 400.0f) {
            float chunkDist = std::sqrt(chunkDistSq);
            float dotHoriz = (cx * camDir2D.x + cz * camDir2D.y) / chunkDist;
            if (dotHoriz < -0.25f) continue;
        }

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
            else if (dist < LOD_DIST_2) lodVisibleInstances_[2].push_back(inst);
            else lodVisibleInstances_[3].push_back(inst);
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
    auto vertCode = readFile(AssetPaths::GRASS_VERT_SHADER);
    auto fragCode = readFile(AssetPaths::GRASS_FRAG_SHADER);
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
