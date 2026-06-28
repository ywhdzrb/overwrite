#include "renderer/grass_system.hpp"
#include "core/vulkan_device.hpp"
#include "core/camera.hpp"
#include "utils/logger.hpp"
#include "utils/asset_paths.hpp"
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
    glm::mat4 view;            // 0-63   视图矩阵
    glm::mat4 proj;            // 64-127 投影矩阵
    glm::vec4 timeParams;      // 128-143 时间/风/玩家参数
    glm::vec4 playerPosVec;    // 144-159 玩家世界坐标
    glm::vec4 lightDir;        // 160-175 光照方向(xyz)+漫反射强度(w)
    glm::vec4 ambientColor;    // 176-191 xyz=环境光颜色(与terrain一致) w=未用
};
static_assert(sizeof(PushBlock) == 192, "Grass PushBlock must be 192 bytes");

} // anonymous namespace

GrassSystem::GrassSystem(std::shared_ptr<VulkanDevice> device)
    : device_(std::move(device)) {
}

GrassSystem::~GrassSystem() {
    cleanup();
}

void GrassSystem::init(const GrassConfig& cfg, VkRenderPass renderPass,
                       VkExtent2D extent, VkSampleCountFlagBits msaaSamples,
                       VkDescriptorSetLayout set0Layout,
                       VkDescriptorSetLayout set1Layout,
                       VkDescriptorSetLayout set2Layout) {
    if (initialized_) return;
    config_ = cfg;
    renderPass_ = renderPass;
    cachedExtent_ = extent;
    cachedMsaaSamples_ = msaaSamples;
    set0Layout_ = set0Layout;
    set1Layout_ = set1Layout;
    set2Layout_ = set2Layout;

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

    // 初始区块由首次 update() 根据玩家实际位置加载，避免原点预加载浪费
    Logger::info("[GrassSystem] 初始化完成");
    initialized_ = true;
}

void GrassSystem::rebuildPipeline() {
    VkDevice dev = device_->getDevice();
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    createPipeline(renderPass_, cachedExtent_, cachedMsaaSamples_);
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
    std::uniform_real_distribution<float> heightScaleGen(0.3f, 1.7f);
    std::uniform_real_distribution<float> widthScaleGen(0.25f, 2.5f);

    // === 连续分形噪声：用多频正弦波替代传统网格值噪声，彻底消除方块可见性 ===
    // 无整数网格结构，采样完全连续，相邻空间返回相近值。5 层非整数比叠加破坏周期性。
    auto organicNoise = [](float x, float z, int seed) -> float {
        float val = 0.0f;
        float amp = 0.5f;
        float freq = 0.03f;
        float phaseOff = static_cast<float>(seed) * 73.937f;
        for (int oct = 0; oct < 5; oct++) {
            float phase = x * freq + z * freq * 0.73f + phaseOff;
            val += amp * (std::sin(phase) * 0.5f + 0.5f);
            freq *= 2.17f;
            amp *= 0.46f;
        }
        return std::clamp(val, 0.0f, 1.0f);
    };

    // 地形坡度采样（连续，无网格）
    auto sampleSlope = [&](float x, float z) -> float {
        if (!heightSampler_) return 0.0f;
        float eps = 0.5f;
        float hx1 = heightSampler_(x + eps, z);
        float hx2 = heightSampler_(x - eps, z);
        float hz1 = heightSampler_(x, z + eps);
        float hz2 = heightSampler_(x, z - eps);
        float dx = (hx1 - hx2) / (2.0f * eps);
        float dz = (hz1 - hz2) / (2.0f * eps);
        return std::sqrt(dx * dx + dz * dz);
    };

    // 位置微抖动 ±0.3m，打破均匀分布带来的残留等间距感
    std::uniform_real_distribution<float> jitter(-0.3f, 0.3f);

    for (int i = 0; i < count; i++) {
        for (int attempt = 0; attempt < 4; attempt++) {
            float wx = worldX0 + posOffset(rng) + jitter(rng);
            float wz = worldZ0 + posOffset(rng) + jitter(rng);
            float y = heightSampler_ ? heightSampler_(wx, wz) : 0.0f;

            // 坡度驱动密度：缓坡/平地草密，陡坡草稀，连续变化网格不可见
            float slope = sampleSlope(wx, wz);
            float flatDensity = 1.0f - std::min(slope / 0.7f, 1.0f);
            flatDensity = flatDensity * flatDensity * flatDensity;

            // 有机噪声替代旧 4m 网格 clump noise
            float organicPatch = organicNoise(wx, wz, 5000);
            float organicDetail = organicNoise(wx, wz, 6000);
            float clumpValue = organicPatch * 0.65f + organicDetail * 0.35f;

            // 综合接受概率 = 坡度 × 有机簇值，坡度陡或噪声低的区域无草
            float acceptProb = flatDensity * (0.55f + clumpValue * 0.35f);
            if (heightNormGen(rng) > acceptProb) continue;

            GrassInstanceData inst;
            inst.position = {wx, y, wz};
            inst.yaw = yawGen(rng);
            // 高度：0.3~1.7 均匀分布，同区块内高矮对比达到 5.7x
            // 不用钟形避免大部分草高度接近，确保矮草(<30cm)和高草(>1m)同时可见
            float heightScale = heightScaleGen(rng);
            inst.scale = std::clamp(zoneBase * heightScale, cfg.bladeHeightMin, cfg.bladeHeightMax);
            inst.windSeed = seedGen(rng);
            // 宽度/粗细：0.25~2.5 均匀分布，10x 跨度确保肉眼可见差异
            // 不使用钟形分布（会导致大部分草茎宽度近似），改用均匀分布产生从极细到极粗的完整谱系
            inst.widthScale = widthScaleGen(rng);
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
    bool newChunksLoaded = false;

    // Step 0: 收集上一帧未完成的区块生成结果（非阻塞轮询）
    auto it = pendingChunkFutures_.begin();
    while (it != pendingChunkFutures_.end()) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = it->get();
            if (chunkData_.count(result.key) == 0) {
                totalLoadedBlades_ += result.blades.size();
                chunkData_[result.key] = std::move(result.blades);
                newChunksLoaded = true;
            }
            it = pendingChunkFutures_.erase(it);
        } else {
            ++it;
        }
    }

    int px = static_cast<int>(std::floor(playerPos.x / config_.chunkSize));
    int pz = static_cast<int>(std::floor(playerPos.z / config_.chunkSize));

    // 仅在玩家跨越区块边界时更新
    if (chunkPositionInitialized_ && px == lastPlayerChunkX_ && pz == lastPlayerChunkZ_) {
        // 新区块加载完成后强制触发 LOD 重剔除，确保草立即可见
        if (newChunksLoaded) {
            lastCullPlayerPos_ = glm::vec3(99999.0f);
        }
        return;
    }

    // 玩家跨越了区块边界：清理上一帧未完成的任务（旧位置的结果已无意义）
    pendingChunkFutures_.clear();
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
    for (auto itc = chunkData_.begin(); itc != chunkData_.end(); ) {
        if (!keep.count(itc->first)) {
            totalLoadedBlades_ -= itc->second.size();
            itc = chunkData_.erase(itc);
        } else {
            ++itc;
        }
    }

    // 加载新进入范围的区块（按距离从近到远排序，确保玩家附近优先）
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
        // 主线程预先查询邻近树/石位置，按区块中心分组传递（线程安全：主线程独占）
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

        // 并行提交所有区块生成任务，全部通过 pending futures 异步收集
        for (size_t ki = 0; ki < newKeys.size(); ki++) {
            const auto& key = newKeys[ki];
            auto nearbyTrees = std::move(treeDataPerKey[ki]);
            auto nearbyStones = std::move(stoneDataPerKey[ki]);
            auto lightDir = lightDir_;
            pendingChunkFutures_.push_back(threadPool_.enqueue(
                [this, key, nearbyTrees = std::move(nearbyTrees),
                 nearbyStones = std::move(nearbyStones), lightDir]() -> GenResult {
                    std::mt19937 chunkGen(key.x * 100000 + key.z);
                    return {key, generateChunkBlades(config_, key.x, key.z, chunkGen,
                                                     nearbyTrees, nearbyStones, lightDir)};
                }));
        }
        // 不设 newChunksLoaded—数据尚未就绪，由后续帧的 pending 轮询负责
    }

    // 新区块加载完成后强制触发 LOD 重剔除
    if (newChunksLoaded) {
        lastCullPlayerPos_ = glm::vec3(99999.0f);
    }
}

void GrassSystem::updatePushStates(const glm::vec3& playerPos, float deltaTime,
                                    const glm::vec3& moveDir, float speed) {
    float playerRadius = config_.playerRadius * 1.5f;
    float attackSpeed = 5.0f;   // 挤压蓄力速度（越快草茎响应越灵敏）
    float decayRate  = 3.0f;    // 弹簧恢复速率（越大回弹越快）
    float radiusSq = playerRadius * playerRadius;
    // 只有玩家附近几个区块可能有交互，快速跳过远区块
    float chunkSkipDist = playerRadius + config_.chunkSize;
    float chunkSkipDistSq = chunkSkipDist * chunkSkipDist;

    // ============ 预计算衰减因子，避免 per-blade std::exp ============
    float decayFactor = std::exp(-decayRate * deltaTime);

    // 玩家是否在有效移动（用于方向性推压）
    bool isMoving = speed > 0.1f;

    for (auto& [key, blades] : chunkData_) {
        // 区块级快速跳过：计算区块中心到玩家的平方距离
        float cx = (static_cast<float>(key.x) + 0.5f) * config_.chunkSize - playerPos.x;
        float cz = (static_cast<float>(key.z) + 0.5f) * config_.chunkSize - playerPos.z;
        if (cx * cx + cz * cz > chunkSkipDistSq) {
            continue;
        }
        for (auto& inst : blades) {
            float dx = inst.position.x - playerPos.x;
            float dy = inst.position.y - playerPos.y;
            float dz = inst.position.z - playerPos.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq < radiusSq) {
                // 玩家附近：将草茎向下挤压
                float dist = std::sqrt(distSq);
                float influence = 1.0f - (dist / playerRadius);
                float target = influence * influence;

                // 方向权重：移动时前方草被推倒，后方/侧方草保留→自然"划过"效果
                if (isMoving) {
                    float hDist = std::sqrt(dx * dx + dz * dz);
                    if (hDist > 0.3f) {
                        // player→grass 方向与 moveDir 的点积：正=前方，负=后方
                        float dot = (dx / hDist) * moveDir.x + (dz / hDist) * moveDir.z;
                        // 前方(dot=1)→dirWeight=1.0 全压；后方/侧方=拖尾区域
                        float frontWeight = std::pow(std::max(0.0f, dot), 0.4f);
                        target *= 0.15f + 0.85f * frontWeight;
                    }
                }

                inst.pushState = std::min(1.0f,
                    inst.pushState + (target - inst.pushState) * attackSpeed * deltaTime);
            } else {
                // 玩家远离：已归零则跳过，否则指数衰减
                if (inst.pushState < 0.0001f) {
                    inst.pushState = 0.0f;
                    continue;
                }
                inst.pushState *= decayFactor;
                if (inst.pushState < 0.0001f) inst.pushState = 0.0f;
            }
        }
    }
}

/**
 * @brief 更新所有可见草的推压状态并上传到 GPU（每帧执行）
 *
 * 与 LOD 剔除（Phase 3）解耦：剔除可延迟执行，但推压状态每帧刷新上传，
 * 保证玩家走近/远离时草的弯曲动画连续平滑，不发生跳跃。
 *
 * @param playerPos 玩家世界坐标
 * @param deltaTime 帧时间（秒）
 */
void GrassSystem::uploadVisibleToGpu(const glm::vec3& playerPos, float deltaTime,
                                      const glm::vec3& moveDir, float speed) {
    currentInstanceBuffer_ = (currentInstanceBuffer_ + 1) % INSTANCE_BUFFER_COUNT;
    void* mappedBuf = mappedInstanceDatas_[currentInstanceBuffer_];
    if (mappedBuf == nullptr) return;

    // 推压参数（与 updatePushStates 保持一致）
    float playerRadius = config_.playerRadius * 1.5f;
    float radiusSq = playerRadius * playerRadius;
    float attackSpeed = 5.0f;
    float decayFactor = std::exp(-3.0f * deltaTime);
    bool isMoving = speed > 0.1f;

    // 统计上传总量用于超限检查
    VkDeviceSize totalUpload = 0;
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        totalUpload += lodVisibleInstances_[lod].size() * sizeof(GrassInstanceData);
    }

    VkDeviceSize bufferBytes = static_cast<VkDeviceSize>(instanceBufferCapacity_) *
                                sizeof(GrassInstanceData);

    // 可见草超过缓冲容量时从 LOD2 尾部丢弃
    if (totalUpload > bufferBytes) {
        Logger::warning("[GrassSystem] 可见草茎数超过缓冲容量 " +
                     std::to_string(instanceBufferCapacity_));
        while (totalUpload > bufferBytes && !lodVisibleInstances_[2].empty()) {
            totalUpload -= sizeof(GrassInstanceData);
            lodVisibleInstances_[2].pop_back();
        }
    }

    // 为每根可见草刷新推压状态后上传
    VkDeviceSize offset = 0;
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        lodInstanceOffsets_[lod] = offset;
        size_t count = lodVisibleInstances_[lod].size();
        if (count > 0) {
            GrassInstanceData* instances = lodVisibleInstances_[lod].data();
            for (size_t i = 0; i < count; i++) {
                auto& inst = instances[i];
                float dx = inst.position.x - playerPos.x;
                float dy = inst.position.y - playerPos.y;
                float dz = inst.position.z - playerPos.z;
                float distSq = dx * dx + dy * dy + dz * dz;

                if (distSq < radiusSq) {
                    float dist = std::sqrt(distSq);
                    float influence = 1.0f - (dist / playerRadius);
                    float target = influence * influence;

                    // 与 updatePushStates 一致的方向权重
                    if (isMoving) {
                        float hDist = std::sqrt(dx * dx + dz * dz);
                        if (hDist > 0.3f) {
                            float dot = (dx / hDist) * moveDir.x + (dz / hDist) * moveDir.z;
                            float frontWeight = std::pow(std::max(0.0f, dot), 0.4f);
                            target *= 0.15f + 0.85f * frontWeight;
                        }
                    }

                    inst.pushState = std::min(1.0f,
                        inst.pushState + (target - inst.pushState) * attackSpeed * deltaTime);
                } else if (inst.pushState >= 0.0001f) {
                    inst.pushState *= decayFactor;
                    if (inst.pushState < 0.0001f) inst.pushState = 0.0f;
                } else {
                    inst.pushState = 0.0f;
                }
            }
            memcpy(static_cast<char*>(mappedBuf) + offset, instances, count * sizeof(GrassInstanceData));
        }
        offset += count * sizeof(GrassInstanceData);
    }
}

void GrassSystem::update(const glm::vec3& playerPos, const Camera& camera,
                         float deltaTime) {
    if (!initialized_) return;
    time_ += deltaTime;

    // 计算玩家速度（用于方向性推压：前方草被推倒，后方草回弹）
    glm::vec3 moveDir(0.0f);
    float speed = 0.0f;
    if (deltaTime > 0.0f) {
        glm::vec3 vel = (playerPos - playerPosition_) / deltaTime;
        speed = glm::length(vel);
        if (speed > 0.1f) {
            moveDir = vel / speed;
        }
    }
    playerPosition_ = playerPos;

    // Phase 1: 动态区块加载/卸载
    updateChunks(playerPos);

    // Phase 2: 更新 CPU 端持久化推压状态（每帧，保证不可见的草重新可见时状态连续）
    updatePushStates(playerPos, deltaTime, moveDir, speed);

    // 使用玩家水平位移 + 相机朝向变化做阈值：
    // 移动≥3m 或旋转≥30° 触发全量剔除，兼顾性能（不每帧 O(N)）和响应性（转头见草）
    glm::vec3 playerDelta = playerPos - lastCullPlayerPos_;
    float playerMoveSq = playerDelta.x * playerDelta.x + playerDelta.z * playerDelta.z;
    bool movedFarEnough = (playerMoveSq >= CULL_MOVE_THRESHOLD * CULL_MOVE_THRESHOLD);

    // 检测相机朝向变化：当前朝向与上次剔除时的朝向的点积
    glm::vec3 currentFront = camera.getFront();
    float frontDot = glm::dot(currentFront, lastCullCameraFront_);
    bool turnedEnough = (frontDot < CULL_ANGLE_THRESHOLD);

    bool needFullCull = (movedFarEnough || turnedEnough);

    if (needFullCull) {
        lastCullPlayerPos_ = playerPos;
        lastCullCameraFront_ = currentFront;

        // Phase 3: 全量 LOD 剔除 — 每 3m 触发一次，避免每帧 O(N) 开销
        for (int lod = 0; lod < LOD_COUNT; lod++) {
            lodVisibleInstances_[lod].clear();
        }
        size_t visibleBudget = std::min(totalLoadedBlades_,
                               static_cast<size_t>(config_.maxBlades));
        lodVisibleInstances_[0].reserve(visibleBudget / 2);
        lodVisibleInstances_[1].reserve(visibleBudget / 3);
        lodVisibleInstances_[2].reserve(visibleBudget / 3);

        auto& frustum = camera.getFrustum();
        glm::vec3 camFront3D = camera.getFront();
        float frontLenSq = camFront3D.x * camFront3D.x + camFront3D.z * camFront3D.z;
        float invFrontLen = frontLenSq > 0.0001f ? 1.0f / std::sqrt(frontLenSq) : 0.0f;
        glm::vec2 camDir2D = glm::vec2(camFront3D.x * invFrontLen, camFront3D.z * invFrontLen);

        float renderDistSq = config_.renderDistance * config_.renderDistance;
        float frustumDistSq = 400.0f;
        float densLowDist = config_.renderDistance * 0.2f;
        float densRange = config_.renderDistance * 0.8f;
        float invDensRange = 1.0f / densRange;
        float lod0Sq = LOD_DIST_0 * LOD_DIST_0;
        float chunkMaxDist = config_.renderDistance + config_.chunkSize * 0.707f;
        float chunkMaxDistSq = chunkMaxDist * chunkMaxDist;
        float chunkSize = config_.chunkSize;

        glm::vec3 camPos = camera.getPosition();
        float camPosX = camPos.x;
        float camPosY = camPos.y;
        float camPosZ = camPos.z;

        for (const auto& [key, blades] : chunkData_) {
            float cx = (static_cast<float>(key.x) + 0.5f) * chunkSize - camPosX;
            float cz = (static_cast<float>(key.z) + 0.5f) * chunkSize - camPosZ;
            float chunkDistSq = cx * cx + cz * cz;
            if (chunkDistSq > chunkMaxDistSq) continue;

            if (chunkDistSq > 400.0f) {
                float chunkDist = std::sqrt(chunkDistSq);
                float dotHoriz = (cx * camDir2D.x + cz * camDir2D.y) / chunkDist;
                if (dotHoriz < -0.25f) continue;
            }

            for (const auto& inst : blades) {
                float dx = inst.position.x - camPosX;
                float dz = inst.position.z - camPosZ;
                float distSqH = dx * dx + dz * dz;
                if (distSqH > renderDistSq) continue;

                float dy = inst.position.y - camPosY;
                float distSq = distSqH + dy * dy;
                if (distSq > renderDistSq) continue;

                if (distSq > frustumDistSq && distSq < lod0Sq &&
                    !frustum.isSphereInside(inst.position, config_.bladeHeightMax)) continue;

                float dist = std::sqrt(distSq);

                if (dist > densLowDist) {
                    float t = (dist - densLowDist) * invDensRange;
                    if (t > 1.0f) t = 1.0f;
                    float skipVal = glm::fract(inst.windSeed * 43758.5453f);
                    if (skipVal > 1.0f - t * 0.9f) continue;
                }

                if (dist < LOD_DIST_0) lodVisibleInstances_[0].push_back(inst);
                else if (dist < LOD_DIST_1) lodVisibleInstances_[1].push_back(inst);
                else if (dist < LOD_DIST_2) lodVisibleInstances_[2].push_back(inst);
                else lodVisibleInstances_[3].push_back(inst);
            }
        }
    }

    // Phase 4: 每帧更新推压状态并上传到 GPU（不随 LOD 剔除门控）
    // LOD 剔除延迟执行不影响推压动画的连续性
    uploadVisibleToGpu(playerPos, deltaTime, moveDir, speed);
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

    PushBlock pushBase;
    pushBase.view = camera.getViewMatrix();
    pushBase.proj = camera.getProjectionMatrix();
    pushBase.timeParams = glm::vec4(time_, config_.windStrength,
                                     0.0f, config_.playerForce);  // z 由 LOD 循环覆写
    pushBase.playerPosVec = glm::vec4(playerPosition_, 1.0f);
    pushBase.lightDir = glm::vec4(glm::normalize(lightDir_), lightIntensity_);
    pushBase.ambientColor = glm::vec4(ambientColor_, 0.0f);

    // 逐 LOD 层绘制，每层传入不同 LOD 等级以精简远距离风场计算
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        size_t count = lodVisibleInstances_[lod].size();
        if (count == 0) continue;

        // 覆写 timeParams.z 为当前 LOD 等级，着色器据此降级风场复杂度
        PushBlock push = pushBase;
        push.timeParams.z = static_cast<float>(lod);
        vkCmdPushConstants(commandBuffer, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushBlock), &push);

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

    // 顶点属性：位置(location=0) + UV(location=1) | 实例属性(location=2~7)
    VkVertexInputAttributeDescription attrs[8] = {};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassVertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GrassVertex, uv)};
    attrs[2] = {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassInstanceData, position)};
    attrs[3] = {3, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, yaw)};
    attrs[4] = {4, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, scale)};
    attrs[5] = {5, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, windSeed)};
    attrs[6] = {6, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, pushState)};
    attrs[7] = {7, 1, VK_FORMAT_R32_SFLOAT,        offsetof(GrassInstanceData, widthScale)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 2;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = 8;
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
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushBlock);

    // 使用与主渲染管线相同的 descriptor set 布局（set=0 纹理, set=1 光照SSBO, set=2 阴影）
    // 确保 grass.frag 可以读取 LightBuffer SSBO 和 ShadowMap
    VkDescriptorSetLayout dsLayouts[3] = {
        set0Layout_,   // set=0: 纹理（草不使用，但与主管线布局兼容）
        set1Layout_,   // set=1: LightBuffer SSBO
        set2Layout_    // set=2: ShadowMap + ShadowUniform
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 3;
    layoutInfo.pSetLayouts = dsLayouts;
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
    VmaAllocator allocator = device_->getAllocator();
    VkDeviceSize vbSize = sizeof(GrassVertex) * lodVertices_[lod].size();
    VkDeviceSize ibSize = sizeof(uint32_t) * lodIndices_[lod].size();

    auto uploadBuffer = [&](VkBuffer& buf, VmaAllocation& alloc,
                            VkDeviceSize size, VkBufferUsageFlags usage,
                            const void* data) {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocOut;
        if (vmaCreateBuffer(allocator, &info, &allocInfo, &buf, &alloc, &allocOut) != VK_SUCCESS) return false;

        if (data && size > 0) {
            memcpy(allocOut.pMappedData, data, static_cast<size_t>(size));
        }
        return true;
    };

    uploadBuffer(lodVertexBuffers_[lod], lodVertexBufferAllocations_[lod],
                 vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 lodVertices_[lod].data());
    uploadBuffer(lodIndexBuffers_[lod], lodIndexBufferAllocations_[lod],
                 ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 lodIndices_[lod].data());
}

void GrassSystem::createInstanceBuffer(int maxBlades) {
    VmaAllocator allocator = device_->getAllocator();
    VkDeviceSize size = sizeof(GrassInstanceData) * maxBlades;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (int i = 0; i < INSTANCE_BUFFER_COUNT; i++) {
        VmaAllocationInfo allocOut;
        if (vmaCreateBuffer(allocator, &info, &allocInfo, &instanceBuffers_[i], &instanceBufferAllocations_[i], &allocOut) != VK_SUCCESS) {
            Logger::error("[GrassSystem] 实例缓冲 " + std::to_string(i) + " 创建失败");
            return;
        }
        mappedInstanceDatas_[i] = allocOut.pMappedData;
    }
    instanceBufferCapacity_ = maxBlades;
    currentInstanceBuffer_ = 0;
}

void GrassSystem::cleanup() {
    VmaAllocator allocator = device_->getAllocator();
    VkDevice dev = device_->getDevice();

    if (pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(dev, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }

    // 清理四层 LOD 的顶点/索引缓冲
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        if (lodVertexBuffers_[lod] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, lodVertexBuffers_[lod], lodVertexBufferAllocations_[lod]);
            lodVertexBuffers_[lod] = VK_NULL_HANDLE;
            lodVertexBufferAllocations_[lod] = VK_NULL_HANDLE;
        }
        if (lodIndexBuffers_[lod] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, lodIndexBuffers_[lod], lodIndexBufferAllocations_[lod]);
            lodIndexBuffers_[lod] = VK_NULL_HANDLE;
            lodIndexBufferAllocations_[lod] = VK_NULL_HANDLE;
        }
    }
    // 清理双缓冲实例缓冲
    for (int i = 0; i < INSTANCE_BUFFER_COUNT; i++) {
        mappedInstanceDatas_[i] = nullptr;
        if (instanceBuffers_[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, instanceBuffers_[i], instanceBufferAllocations_[i]);
            instanceBuffers_[i] = VK_NULL_HANDLE;
            instanceBufferAllocations_[i] = VK_NULL_HANDLE;
        }
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
