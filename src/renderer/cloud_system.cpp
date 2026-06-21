/**
 * @file cloud_system.cpp
 * @brief 体积云系统实现 — Nubis风格程序化噪声 + 侵蚀密度 + 体积光照
 *
 * 实现细节：
 * - 噪声纹理：CPU端生成4通道3D Worley噪声（不同频率），VkImage上传
 * - 密度模型：FBM基形 × heightProfile × coverage → remap侵蚀 → 密度倍率
 * - 渲染管线：全屏四边形 + Ray Marching + lightMarch单散射
 * - LOD：根据摄像机距离调整步进次数
 */

#include "renderer/cloud_system.hpp"
#include "core/vulkan_device.hpp"
#include "core/camera.hpp"
#include "utils/logger.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace owengine {

// ============================================================
// 构造与析构
// ============================================================

CloudSystem::CloudSystem(std::shared_ptr<VulkanDevice> device)
    : device_(std::move(device)) {
}

CloudSystem::~CloudSystem() {
    cleanup();
}

// ============================================================
// 初始化
// ============================================================

void CloudSystem::init(VkRenderPass renderPass, VkExtent2D extent,
                       VkSampleCountFlagBits msaaSamples) {
    screenExtent_ = extent;

    // 步骤1：创建描述符集布局
    createDescriptorSetLayout();

    // 步骤2：创建描述符池
    createDescriptorPool();

    // 步骤3：生成并上传4通道Worley噪声纹理
    createNoiseTexture();

    // 步骤4：创建图形管线
    createPipeline(renderPass, extent, msaaSamples);

    // 步骤5：分配和更新描述符集
    createDescriptorSets();

    initialized_ = true;
    Logger::info("[CloudSystem] 体积云系统初始化完成, 步进=" +
                 std::to_string(stepCount_));
}

void CloudSystem::cleanup() {
    if (!initialized_) return;

    VkDevice dev = device_->getDevice();
    VmaAllocator alloc = device_->getAllocator();

    // 销毁管线
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }

    // 销毁描述符池和布局
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    // 销毁噪声纹理
    if (noiseImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, noiseImageView_, nullptr);
        noiseImageView_ = VK_NULL_HANDLE;
    }
    if (noiseImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(alloc, noiseImage_, noiseImageAllocation_);
        noiseImage_ = VK_NULL_HANDLE;
        noiseImageAllocation_ = VK_NULL_HANDLE;
    }
    if (noiseSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(dev, noiseSampler_, nullptr);
        noiseSampler_ = VK_NULL_HANDLE;
    }

    initialized_ = false;
    Logger::info("[CloudSystem] 体积云系统已清理");
}

// ============================================================
// 每帧更新
// ============================================================

void CloudSystem::update(float deltaTime, const Camera& camera,
                         const glm::vec3& sunDirection) {
    if (!initialized_) return;

    // 累积全局时间（驱动风动画，实时速率让云运动清晰可见）
    time_ += deltaTime;

    // 记录太阳方向
    lastSunDir_ = sunDirection;

    // 更新LOD
    LOD newLod = evaluateLOD(camera);
    if (newLod != currentLOD_) {
        currentLOD_ = newLod;
        applyLODParams(currentLOD_);
    }
}

// ============================================================
// 渲染
// ============================================================

void CloudSystem::render(VkCommandBuffer commandBuffer, const Camera& camera,
                         const glm::vec3& sunDirection) {
    if (!initialized_) return;

    // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // 绑定描述符集
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    // 准备PushConstants
    PushConstants pc{};

    // 逆VP矩阵：将屏幕坐标转换到世界空间射线
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    pc.invViewProj = glm::inverse(viewProj);

    // 相机位置和云层范围
    glm::vec3 camPos = camera.getPosition();
    pc.cameraPos_cloudMin = glm::vec4(camPos, cloudHeightMin_);

    // 云层顶部高度 + 时间 + 风速 + 风向（着色器需要弧度）
    pc.cloudMax_time = glm::vec4(
        cloudHeightMax_,             // x: 云层顶高度
        time_,                       // y: 全局时间（风动画）
        windSpeed_,                  // z: 风速
        glm::radians(windDirection_) // w: 风向（转弧度，着色器用cos/sin）
    );

    // 步进次数、覆盖率、密度倍率、薄云层高度
    pc.params = glm::vec4(
        static_cast<float>(stepCount_),  // x
        cloudCoverage_,                  // y
        cloudDensityMultiplier_,         // z
        thinCloudEnabled_ ? thinCloudHeight_ : -1.0f  // w: -1=禁用薄云
    );

    // 太阳方向 + 昼夜亮度因子
    glm::vec3 sd = glm::normalize(sunDirection);
    float dayVis = dayNightEnabled_ ? dayFactor_ : 1.0f;
    pc.sunDir_dayFactor = glm::vec4(sd, dayVis);

    // 推送常量
    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    // 绘制全屏四边形（4个顶点，TRIANGLE_STRIP）
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

// ============================================================
// 描述符集布局创建
// ============================================================

void CloudSystem::createDescriptorSetLayout() {
    // binding 0: 3D噪声纹理采样器
    VkDescriptorSetLayoutBinding noiseBinding{};
    noiseBinding.binding = 0;
    noiseBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    noiseBinding.descriptorCount = 1;
    noiseBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    noiseBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &noiseBinding;

    if (vkCreateDescriptorSetLayout(device_->getDevice(), &layoutInfo, nullptr,
                                    &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 创建描述符集布局失败");
    }
}

// ============================================================
// 描述符池创建
// ============================================================

void CloudSystem::createDescriptorPool() {
    // 池大小：1个combined sampler
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device_->getDevice(), &poolInfo, nullptr,
                                &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 创建描述符池失败");
    }
}

// ============================================================
// 4通道Worley噪声纹理生成（CPU + Vulkan上传）
// ============================================================

void CloudSystem::createNoiseTexture() {
    // 4个通道的细胞数（每维）：2、3、5、7
    // 使用互质数而非2的幂：打破2/4/8/16的规律重复，
    // 使Worley FBM在64³纹理内产生无重复感的复杂形态
    const int cellCounts[4] = {2, 3, 5, 7};

    // 步骤1：CPU生成4通道Worley噪声
    std::vector<uint8_t> noiseData(NOISE_TEX_SIZE * NOISE_TEX_SIZE * NOISE_TEX_SIZE * 4);
    generateMultiChannelWorley(noiseData, NOISE_TEX_SIZE, cellCounts);

    // 步骤2：创建Vulkan图像（RGBA8）
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = NOISE_TEX_SIZE;
    imageInfo.extent.height = NOISE_TEX_SIZE;
    imageInfo.extent.depth = NOISE_TEX_SIZE;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(device_->getAllocator(), &imageInfo, &allocInfo,
                        &noiseImage_, &noiseImageAllocation_, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 创建噪声纹理图像失败");
    }

    // 步骤3：上传数据
    VkDeviceSize imageSize = NOISE_TEX_SIZE * NOISE_TEX_SIZE * NOISE_TEX_SIZE * 4;

    // 创建暂存缓冲区
    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo stagingAllocOut;
    if (vmaCreateBuffer(device_->getAllocator(), &stagingBufferInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAllocation, &stagingAllocOut) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 创建暂存缓冲区失败");
    }

    memcpy(stagingAllocOut.pMappedData, noiseData.data(), static_cast<size_t>(imageSize));

    // 使用单次命令上传
    VkCommandBuffer cmd = device_->beginSingleTimeCommands();

    // 图像布局转换：UNDEFINED → TRANSFER_DST
    VkImageMemoryBarrier barrierToDst{};
    barrierToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToDst.image = noiseImage_;
    barrierToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrierToDst.subresourceRange.baseMipLevel = 0;
    barrierToDst.subresourceRange.levelCount = 1;
    barrierToDst.subresourceRange.baseArrayLayer = 0;
    barrierToDst.subresourceRange.layerCount = 1;
    barrierToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrierToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierToDst.srcAccessMask = 0;
    barrierToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToDst);

    // 拷贝数据
    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {NOISE_TEX_SIZE, NOISE_TEX_SIZE, NOISE_TEX_SIZE};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, noiseImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // 图像布局转换：TRANSFER_DST → SHADER_READ_ONLY
    VkImageMemoryBarrier barrierToRead{};
    barrierToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToRead.image = noiseImage_;
    barrierToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrierToRead.subresourceRange.baseMipLevel = 0;
    barrierToRead.subresourceRange.levelCount = 1;
    barrierToRead.subresourceRange.baseArrayLayer = 0;
    barrierToRead.subresourceRange.layerCount = 1;
    barrierToRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierToRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToRead);

    device_->endSingleTimeCommands(cmd);

    // 销毁暂存缓冲区
    vmaDestroyBuffer(device_->getAllocator(), stagingBuffer, stagingAllocation);

    // 步骤4：创建图像视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = noiseImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_->getDevice(), &viewInfo, nullptr,
                           &noiseImageView_) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 创建噪声纹理图像视图失败");
    }

    // 步骤5：创建采样器
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device_->getDevice(), &samplerInfo, nullptr,
                        &noiseSampler_) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 创建噪声纹理采样器失败");
    }

    Logger::info("[CloudSystem] 4通道Worley噪声纹理已生成: " +
                 std::to_string(NOISE_TEX_SIZE) + "³ RGBA8, 细胞数=" +
                 std::to_string(cellCounts[0]) + "/" + std::to_string(cellCounts[1]) +
                 "/" + std::to_string(cellCounts[2]) + "/" + std::to_string(cellCounts[3]));
}

// ============================================================
// 多通道Worley噪声生成
// ============================================================

void CloudSystem::generateMultiChannelWorley(std::vector<uint8_t>& data, int size,
                                              const int cellCounts[4]) {
    // data已经resize为size×size×size×4
    for (int ch = 0; ch < 4; ch++) {
        int cellCount = cellCounts[ch];
        if (cellCount < 1) cellCount = 1;
        float cellSizeF = static_cast<float>(size) / static_cast<float>(cellCount);
        int gridSize = cellCount + 2; // 多一圈边界保证无缝

        // 生成特征点
        struct FeaturePoint { float x, y, z; };
        std::vector<FeaturePoint> features(gridSize * gridSize * gridSize);
        std::mt19937 rng(12345 + ch * 7919); // 每个通道独立种子
        std::uniform_real_distribution<float> posDist(0.0f, cellSizeF);

        for (int cz = 0; cz < gridSize; cz++) {
            for (int cy = 0; cy < gridSize; cy++) {
                for (int cx = 0; cx < gridSize; cx++) {
                    int idx = cz * gridSize * gridSize + cy * gridSize + cx;
                    features[idx] = {
                        static_cast<float>(cx) * cellSizeF + posDist(rng),
                        static_cast<float>(cy) * cellSizeF + posDist(rng),
                        static_cast<float>(cz) * cellSizeF + posDist(rng)
                    };
                }
            }
        }

        // 为每个体素计算到最近特征点的距离
        float maxDist = sqrtf(3.0f) * cellSizeF;
        for (int z = 0; z < size; z++) {
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    float px = static_cast<float>(x);
                    float py = static_cast<float>(y);
                    float pz = static_cast<float>(z);

                    int cx = static_cast<int>(floorf(px / cellSizeF));
                    int cy = static_cast<int>(floorf(py / cellSizeF));
                    int cz = static_cast<int>(floorf(pz / cellSizeF));
                    cx = glm::clamp(cx, 0, gridSize - 1);
                    cy = glm::clamp(cy, 0, gridSize - 1);
                    cz = glm::clamp(cz, 0, gridSize - 1);

                    // 搜索相邻3×3×3=27个cell
                    float minDist = std::numeric_limits<float>::max();

                    for (int dz = -1; dz <= 1; dz++) {
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                int scx = cx + dx;
                                int scy = cy + dy;
                                int scz = cz + dz;

                                if (scx < 0 || scx >= gridSize ||
                                    scy < 0 || scy >= gridSize ||
                                    scz < 0 || scz >= gridSize) continue;

                                int idx = scz * gridSize * gridSize + scy * gridSize + scx;
                                const auto& fp = features[idx];

                                float dx_ = px - fp.x;
                                float dy_ = py - fp.y;
                                float dz_ = pz - fp.z;
                                float dist = dx_ * dx_ + dy_ * dy_ + dz_ * dz_;

                                if (dist < minDist) minDist = dist;
                            }
                        }
                    }

                    minDist = sqrtf(minDist);
                    float normalized = minDist / maxDist;

                    // 反转并幂次调整，使云絮更自然
                    float value = powf(1.0f - normalized, 1.5f);
                    value = glm::clamp(value, 0.0f, 1.0f);

                    int pixelIdx = (z * size + y) * size + x;
                    data[pixelIdx * 4 + ch] = static_cast<uint8_t>(value * 255.0f);
                }
            }
        }
    }
}

// ============================================================
// 管线创建
// ============================================================

void CloudSystem::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                 VkSampleCountFlagBits msaaSamples) {
    VkDevice dev = device_->getDevice();

    // 步骤1：创建管线布局
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 创建管线布局失败");
    }

    // 步骤2：加载着色器SPIR-V
    auto readFile = [](const std::string& filename) -> std::vector<char> {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("[CloudSystem] 无法打开着色器文件: " + filename);
        }
        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        file.close();
        return buffer;
    };

    auto vertCode = readFile("shaders/cloud.vert.spv");
    auto fragCode = readFile("shaders/cloud.frag.spv");

    // 创建着色器模块
    auto createShaderModule = [dev](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module;
        if (vkCreateShaderModule(dev, &createInfo, nullptr, &module) != VK_SUCCESS) {
            throw std::runtime_error("[CloudSystem] 创建着色器模块失败");
        }
        return module;
    };

    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);

    // 着色器阶段
    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStageInfo, fragStageInfo};

    // 步骤3：无顶点输入（全屏四边形）
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // 步骤4：输入装配
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 步骤5：视口和裁剪
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

    // 步骤6：光栅化
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 步骤7：多重采样
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = msaaSamples;

    // 步骤8：深度测试（只读，防止云遮挡不透明物体）
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 步骤9：透明度混合
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 步骤10：创建图形管线
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
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
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                   &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 创建图形管线失败");
    }

    // 清理着色器模块
    vkDestroyShaderModule(dev, vertModule, nullptr);
    vkDestroyShaderModule(dev, fragModule, nullptr);

    Logger::info("[CloudSystem] 体积云图形管线已创建");
}

// ============================================================
// 描述符集创建
// ============================================================

void CloudSystem::createDescriptorSets() {
    // 分配描述符集
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;

    if (vkAllocateDescriptorSets(device_->getDevice(), &allocInfo,
                                  &descriptorSet_) != VK_SUCCESS) {
        throw std::runtime_error("[CloudSystem] 分配描述符集失败");
    }

    // 更新描述符集（Binding 0: 噪声纹理）
    VkDescriptorImageInfo noiseImageInfo{};
    noiseImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    noiseImageInfo.imageView = noiseImageView_;
    noiseImageInfo.sampler = noiseSampler_;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &noiseImageInfo;

    vkUpdateDescriptorSets(device_->getDevice(), 1, &descriptorWrite, 0, nullptr);
}

// ============================================================
// LOD管理
// ============================================================

CloudSystem::LOD CloudSystem::evaluateLOD(const Camera& camera) const {
    glm::vec3 camPos = camera.getPosition();
    float cloudCenterY = (cloudHeightMin_ + cloudHeightMax_) * 0.5f;
    float yDist = std::abs(camPos.y - cloudCenterY);

    // 计算视线方向（相机→目标），用于判断"目光穿过了多远才碰到云"
    glm::vec3 forward = glm::normalize(camera.getTarget() - camPos);
    float viewUp = std::abs(forward.y); // 1=垂直看天, 0=水平看地平线

    // 有效距离 = Y轴距离 / 视线仰角
    // 看天顶时：viewUp≈1 → 有效距离≈yDist (~98m) → Detail
    // 看45°时：viewUp≈0.7 → 有效距离≈140m → Medium
    // 看地平线时：viewUp≈0 → 有效距离很大 → Far
    // 下限钳制0.05避免除零，上限钳制1000防止极端值
    float effectiveDist = yDist / std::max(viewUp, 0.05f);
    effectiveDist = std::min(effectiveDist, 1000.0f);

    if (effectiveDist < 120.0f) {
        return LOD::Detail;
    } else if (effectiveDist < 300.0f) {
        return LOD::Medium;
    } else {
        return LOD::Far;
    }
}

void CloudSystem::applyLODParams(LOD lod) {
    // 步进次数 = 主步进 × 光照步进，回退到原版数值
    // 画质由 IGN 抖动 + 输出抖动 + 距离衰减补偿，步进本身不需要增加
    switch (lod) {
        case LOD::Detail:
            stepCount_ = 48;   // 48×4=192次密度求值，同原版
            break;
        case LOD::Medium:
            stepCount_ = 24;   // 24×4=96，同原版
            break;
        case LOD::Far:
            stepCount_ = 12;   // 12×4=48，同原版
            break;
    }
}

} // namespace owengine
