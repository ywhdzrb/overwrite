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

    generateBladeMesh(config_.segmentsPerBlade, bladeVertices_, bladeIndices_);
    Logger::info("[GrassSystem] 草茎网格: " +
                 std::to_string(bladeVertices_.size()) + " 顶点, " +
                 std::to_string(bladeIndices_.size()) + " 索引, " +
                 "纯色弯曲面片");

    createPipeline(renderPass, extent, msaaSamples);
    createBladeBuffers();
    createInstanceBuffer(config_.maxBlades);

    // 生成初始区块（以原点为中心）
    updateChunks(glm::vec3(0.0f));

    Logger::info("[GrassSystem] 初始化完成, " + std::to_string(totalLoadedBlades_) + " 根草茎");
    initialized_ = true;
}

void GrassSystem::generateBladeMesh(int segments,
                                     std::vector<GrassVertex>& vertices,
                                     std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    if (segments < 2) segments = 2;
    int rings = segments + 1;

    vertices.reserve(static_cast<size_t>(rings) * 4);
    indices.reserve(static_cast<size_t>(segments) * 12);

    float halfW = config_.bladeWidth * 0.5f;
    float curveStrength = 0.18f;

    for (int quad = 0; quad < 2; quad++) {
        uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

        for (int i = 0; i < rings; i++) {
            float t = static_cast<float>(i) / static_cast<float>(segments);

            // 宽度锥度：基部最宽(0.7~1.0)，尖端收窄到 0
            float widthScale;
            if (t < 0.15f) {
                float bt = t / 0.15f;
                widthScale = 0.7f + bt * 0.3f;
            } else {
                float taperT = (t - 0.15f) / 0.85f;
                widthScale = 1.0f - taperT * taperT;
            }
            float w = halfW * widthScale;

            // 二次曲线：圆滑弯曲，底部直尖端弯曲最大
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

            indices.push_back(bl);
            indices.push_back(br);
            indices.push_back(tl);

            indices.push_back(br);
            indices.push_back(tr);
            indices.push_back(tl);
        }
    }
}

void GrassSystem::generateGrassInChunk(const GrassConfig& cfg, int cx, int cz,
                                        std::mt19937& rng) {
    GrassChunkKey key{cx, cz};
    if (chunkData_.count(key)) return;

    std::vector<GrassInstanceData> blades;
    blades.reserve(static_cast<size_t>(cfg.chunkSize * cfg.chunkSize * cfg.density * 1.5f));

    // λ = 每平米密度 × 区块面积，使密度描述与实际生成数一致
    double lambda = cfg.density * cfg.chunkSize * cfg.chunkSize;
    std::poisson_distribution<int> poisson(lambda);
    int count = poisson(rng);
    if (count <= 0) {
        chunkData_[key] = std::move(blades);
        return;
    }

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

    totalLoadedBlades_ += blades.size();
    chunkData_[key] = std::move(blades);
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
    std::vector<GrassChunkKey> sortedKeys;
    sortedKeys.reserve(keep.size());
    for (const auto& k : keep) sortedKeys.push_back(k);
    std::sort(sortedKeys.begin(), sortedKeys.end(),
        [px, pz](const GrassChunkKey& a, const GrassChunkKey& b) {
            int da = (a.x - px) * (a.x - px) + (a.z - pz) * (a.z - pz);
            int db = (b.x - px) * (b.x - px) + (b.z - pz) * (b.z - pz);
            return da < db;
        });
    for (const auto& key : sortedKeys) {
        if (chunkData_.count(key)) continue;
        // 检查总草茎数是否达到上限，避免撑爆显存缓冲
        if (totalLoadedBlades_ >= static_cast<size_t>(config_.maxBlades)) {
            Logger::warning("[GrassSystem] 草茎总数已达上限 " +
                         std::to_string(config_.maxBlades) + "，停止加载新区块");
            break;
        }
        std::mt19937 chunkGen(key.x * 100000 + key.z);
        generateGrassInChunk(config_, key.x, key.z, chunkGen);
    }
}

void GrassSystem::updatePushStates(const glm::vec3& playerPos, float deltaTime) {
    float playerRadius = config_.playerRadius * 1.5f;
    float attackSpeed = 5.0f;   // 挤压蓄力速度（越快草茎响应越灵敏）
    float decayRate  = 3.0f;    // 弹簧恢复速率（越大回弹越快）

    for (auto& [key, blades] : chunkData_) {
        for (auto& inst : blades) {
            float dist = glm::length(inst.position - playerPos);

            if (dist < playerRadius) {
                // 玩家在交互半径内：pushState 趋向 1.0
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

    // Phase 2: 遍历所有已加载区块，构建可见草茎列表（含剔除）
    visibleInstances_.clear();
    visibleInstances_.reserve(std::min(totalLoadedBlades_,
                              static_cast<size_t>(config_.maxBlades)));

    auto& frustum = camera.getFrustum();
    glm::vec3 camPos = camera.getPosition();

    for (const auto& [key, blades] : chunkData_) {
        // 区块级粗略剔除：检查区块中心距离
        float chunkCenterX = (static_cast<float>(key.x) + 0.5f) * config_.chunkSize;
        float chunkCenterZ = (static_cast<float>(key.z) + 0.5f) * config_.chunkSize;
        float chunkDist = std::sqrt(
            (chunkCenterX - camPos.x) * (chunkCenterX - camPos.x) +
            (chunkCenterZ - camPos.z) * (chunkCenterZ - camPos.z));
        if (chunkDist - config_.chunkSize * 0.707f > config_.renderDistance) continue;

        for (const auto& inst : blades) {
            float dist = glm::length(inst.position - camPos);
            if (dist > config_.renderDistance) continue;

            // 视锥剔除（近处不剔除避免闪烁）
            if (dist > 20.0f &&
                !frustum.isSphereInside(inst.position, config_.bladeHeightMax)) {
                continue;
            }

            // 渐进式 LOD：随距离增长平滑减少密度
            // 40% 距离内全密度→100% 距离只剩 35%
            float keepProb = 1.0f;
            if (dist > config_.renderDistance * 0.4f) {
                float t = (dist - config_.renderDistance * 0.4f) /
                          (config_.renderDistance * 0.6f);
                t = std::min(t, 1.0f);
                keepProb = (1.0f - t * 0.65f);
            }
            if (glm::fract(inst.windSeed * 43758.5453f) > keepProb) continue;

            visibleInstances_.push_back(inst);
        }
    }

    // Phase 3: 上传可见实例到 GPU
    if (!visibleInstances_.empty() && instanceBuffer_ != VK_NULL_HANDLE) {
        VkDeviceSize uploadSize = visibleInstances_.size() * sizeof(GrassInstanceData);
        VkDeviceSize bufferSize = static_cast<VkDeviceSize>(instanceBufferCapacity_) *
                                   sizeof(GrassInstanceData);

        if (uploadSize > bufferSize) {
            Logger::warning("[GrassSystem] 可见草茎数 " +
                         std::to_string(visibleInstances_.size()) +
                         " 超过缓冲容量 " + std::to_string(instanceBufferCapacity_));
            visibleInstances_.resize(instanceBufferCapacity_);
            uploadSize = bufferSize;
        }

        void* data;
        vkMapMemory(device_->getDevice(), instanceBufferMemory_, 0,
                    VK_WHOLE_SIZE, 0, &data);
        memcpy(data, visibleInstances_.data(), static_cast<size_t>(uploadSize));
        vkUnmapMemory(device_->getDevice(), instanceBufferMemory_);
    }
}

void GrassSystem::render(VkCommandBuffer commandBuffer, const Camera& camera) {
    if (!initialized_ || visibleInstances_.empty()) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkBuffer vbs[] = {bladeVertexBuffer_, instanceBuffer_};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, vbs, offsets);
    vkCmdBindIndexBuffer(commandBuffer, bladeIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    PushBlock push;
    push.view = camera.getViewMatrix();
    push.proj = camera.getProjectionMatrix();
    push.timeParams = glm::vec4(time_, config_.windStrength,
                                 config_.playerRadius, config_.playerForce);
    push.playerPosVec = glm::vec4(playerPosition_, 1.0f);

    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(PushBlock), &push);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(bladeIndices_.size()),
                     static_cast<uint32_t>(visibleInstances_.size()),
                     0, 0, 0);
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

void GrassSystem::createBladeBuffers() {
    VkDevice dev = device_->getDevice();
    VkDeviceSize vbSize = sizeof(GrassVertex) * bladeVertices_.size();
    VkDeviceSize ibSize = sizeof(uint32_t) * bladeIndices_.size();

    auto createAndUpload = [&](VkBuffer& buf, VkDeviceMemory& mem,
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

    createAndUpload(bladeVertexBuffer_, bladeVertexBufferMemory_,
                    vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    bladeVertices_.data());
    createAndUpload(bladeIndexBuffer_, bladeIndexBufferMemory_,
                    ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    bladeIndices_.data());
}

void GrassSystem::createInstanceBuffer(int maxBlades) {
    VkDevice dev = device_->getDevice();
    VkDeviceSize size = sizeof(GrassInstanceData) * maxBlades;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &info, nullptr, &instanceBuffer_) != VK_SUCCESS) return;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, instanceBuffer_, &req);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = device_->findMemoryType(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(dev, &alloc, nullptr, &instanceBufferMemory_) != VK_SUCCESS) return;
    vkBindBufferMemory(dev, instanceBuffer_, instanceBufferMemory_, 0);
    instanceBufferCapacity_ = maxBlades;
}

void GrassSystem::cleanup() {
    VkDevice dev = device_->getDevice();

    if (pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(dev, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }

    auto destroyBuf = [&](VkBuffer& b, VkDeviceMemory& m) {
        if (b != VK_NULL_HANDLE) { vkDestroyBuffer(dev, b, nullptr); b = VK_NULL_HANDLE; }
        if (m != VK_NULL_HANDLE) { vkFreeMemory(dev, m, nullptr); m = VK_NULL_HANDLE; }
    };
    destroyBuf(bladeVertexBuffer_, bladeVertexBufferMemory_);
    destroyBuf(bladeIndexBuffer_, bladeIndexBufferMemory_);
    destroyBuf(instanceBuffer_, instanceBufferMemory_);

    visibleInstances_.clear();
    chunkData_.clear();
    bladeVertices_.clear();
    bladeIndices_.clear();
    totalLoadedBlades_ = 0;
    initialized_ = false;
}

} // namespace owengine
