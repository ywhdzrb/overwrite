// 水面渲染器实现 — 高度场波动方程模拟 + 程序化波浪
#include "renderer/water_renderer.hpp"
#include "core/vulkan_device.hpp"
#include "utils/logger.hpp"
#include "utils/vk_result.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace owengine {

WaterRenderer::WaterRenderer(std::shared_ptr<VulkanDevice> device)
    : device_(std::move(device)) {}

WaterRenderer::~WaterRenderer() { cleanup(); }

void WaterRenderer::init(VkRenderPass renderPass, VkExtent2D extent,
                         VkSampleCountFlagBits msaaSamples, float seaLevel) {
    if (initialized_) return;
    seaLevel_ = seaLevel;

    createGrid();
    // 先创建波形模拟资源（含描述符集布局，pipeline 需要引用它们）
    initWaveSim();
    createPipeline(renderPass, extent, msaaSamples);

    Logger::info("[WaterRenderer] 初始化完成，海平面高度=" +
                 std::to_string(seaLevel_) + " 波形模拟=" +
                 std::to_string(WAVE_TEX_SIZE) + "x" +
                 std::to_string(WAVE_TEX_SIZE));
    initialized_ = true;
}

void WaterRenderer::cleanup() {
    if (!initialized_) return;

    VkDevice dev = device_->getDevice();
    VmaAllocator allocator = device_->getAllocator();

    // 先销毁图形管线，再销毁其引用的资源
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indexBuffer_, indexBufferAllocation_);
        indexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, vertexBuffer_, vertexBufferAllocation_);
        vertexBuffer_ = VK_NULL_HANDLE;
    }

    cleanupWaveSim();
    initialized_ = false;
}

// ========== 波动方程模拟初始化 ==========
void WaterRenderer::initWaveSim() {
    VkDevice dev = device_->getDevice();
    VmaAllocator alloc = device_->getAllocator();

    VkFormat fmt = VK_FORMAT_R32_SFLOAT;
    VkExtent2D texExt = {static_cast<uint32_t>(WAVE_TEX_SIZE),
                         static_cast<uint32_t>(WAVE_TEX_SIZE)};

    // 创建 2 张高度场纹理（ping-pong）
    VkImageCreateInfo imgCi{};
    imgCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCi.imageType = VK_IMAGE_TYPE_2D;
    imgCi.format = fmt;
    imgCi.extent = {texExt.width, texExt.height, 1};
    imgCi.mipLevels = 1;
    imgCi.arrayLayers = 1;
    imgCi.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCi.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCi.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imgCi.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCi{};
    allocCi.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocCi.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    for (int i = 0; i < 2; i++) {
        VkResult vr = vmaCreateImage(alloc, &imgCi, &allocCi,
                                     &waveImages_[i], &waveAllocs_[i], nullptr);
        if (vr != VK_SUCCESS) {
            throw std::runtime_error("[Water] 创建波形纹理失败: " + vkResultToString(vr));
        }

        VkImageViewCreateInfo ivCi{};
        ivCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivCi.image = waveImages_[i];
        ivCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivCi.format = fmt;
        ivCi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vr = vkCreateImageView(dev, &ivCi, nullptr, &waveViews_[i]);
        if (vr != VK_SUCCESS) {
            throw std::runtime_error("[Water] 创建波形 ImageView 失败: " + vkResultToString(vr));
        }
    }

    // 创建采样器
    VkSamplerCreateInfo sampCi{};
    sampCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampCi.magFilter = VK_FILTER_LINEAR;
    sampCi.minFilter = VK_FILTER_LINEAR;
    sampCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCi.anisotropyEnable = VK_FALSE;
    sampCi.maxLod = 1.0f;
    VkResult vr = vkCreateSampler(dev, &sampCi, nullptr, &waveSampler_);
    if (vr != VK_SUCCESS) {
        throw std::runtime_error("[Water] 创建波形采样器失败: " + vkResultToString(vr));
    }

    // 计算管线描述符集布局: set=0: prev(sampler), curr(sampler), next(storage)
    VkDescriptorSetLayoutBinding waveBindings[3]{};
    waveBindings[0].binding = 0;
    waveBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    waveBindings[0].descriptorCount = 1;
    waveBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    waveBindings[0].pImmutableSamplers = nullptr;
    waveBindings[1].binding = 1;
    waveBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    waveBindings[1].descriptorCount = 1;
    waveBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    waveBindings[2].binding = 2;
    waveBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    waveBindings[2].descriptorCount = 1;
    waveBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dsCi{};
    dsCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsCi.bindingCount = 3;
    dsCi.pBindings = waveBindings;
    vr = vkCreateDescriptorSetLayout(dev, &dsCi, nullptr, &waveDsLayout_);
    if (vr != VK_SUCCESS) {
        throw std::runtime_error("[Water] 创建波形 DS Layout 失败: " + vkResultToString(vr));
    }

    // 计算管线布局: 7 floats = speed(c·dt), damping, uv.x, uv.y, strength, wind, texelSize
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 7;

    VkPipelineLayoutCreateInfo plCi{};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1;
    plCi.pSetLayouts = &waveDsLayout_;
    plCi.pushConstantRangeCount = 1;
    plCi.pPushConstantRanges = &pcRange;
    vr = vkCreatePipelineLayout(dev, &plCi, nullptr, &wavePipeLayout_);
    if (vr != VK_SUCCESS) {
        throw std::runtime_error("[Water] 创建计算管线布局失败: " + vkResultToString(vr));
    }

    // 加载计算着色器
    std::string compPath = AssetPaths::SHADER_DIR + std::string("wave_sim.comp.spv");
    std::ifstream file(compPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("[Water] 无法打开计算着色器: " + compPath);
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));

    VkShaderModuleCreateInfo smCi{};
    smCi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smCi.codeSize = fileSize;
    smCi.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
    VkShaderModule compModule;
    vr = vkCreateShaderModule(dev, &smCi, nullptr, &compModule);
    if (vr != VK_SUCCESS) {
        throw std::runtime_error("[Water] 创建计算着色器模块失败: " + vkResultToString(vr));
    }

    VkComputePipelineCreateInfo cpCi{};
    cpCi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpCi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpCi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpCi.stage.module = compModule;
    cpCi.stage.pName = "main";
    cpCi.layout = wavePipeLayout_;
    vr = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpCi, nullptr, &wavePipeline_);
    if (vr != VK_SUCCESS) {
        vkDestroyShaderModule(dev, compModule, nullptr);
        throw std::runtime_error("[Water] 创建计算管线失败: " + vkResultToString(vr));
    }
    vkDestroyShaderModule(dev, compModule, nullptr);

    // 创建描述符池（3 个计算 + 1 个图形）
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 8;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 4;

    VkDescriptorPoolCreateInfo dpCi{};
    dpCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCi.poolSizeCount = 2;
    dpCi.pPoolSizes = poolSizes;
    dpCi.maxSets = 8;
    vr = vkCreateDescriptorPool(dev, &dpCi, nullptr, &waveDsPool_);
    if (vr != VK_SUCCESS) {
        throw std::runtime_error("[Water] 创建波形 DS Pool 失败: " + vkResultToString(vr));
    }

    // 分配计算描述符集（2 组，每帧切换）
    std::vector<VkDescriptorSetLayout> waveLayouts(2, waveDsLayout_);
    VkDescriptorSetAllocateInfo dsAi{};
    dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAi.descriptorPool = waveDsPool_;
    dsAi.descriptorSetCount = 2;
    dsAi.pSetLayouts = waveLayouts.data();
    vr = vkAllocateDescriptorSets(dev, &dsAi, waveDescSets_);
    if (vr != VK_SUCCESS) {
        throw std::runtime_error("[Water] 分配计算 DS 失败: " + vkResultToString(vr));
    }

    // 更新计算描述符集（所有纹理保持在 GENERAL 布局，避免计算着色器 storage 写入不兼容）
    for (int i = 0; i < 2; i++) {
        VkDescriptorImageInfo prevInfo{};
        prevInfo.sampler = waveSampler_;
        prevInfo.imageView = waveViews_[i];
        prevInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo currInfo{};
        currInfo.sampler = waveSampler_;
        currInfo.imageView = waveViews_[(i + 1) % 2];
        currInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo nextInfo{};
        nextInfo.imageView = waveViews_[(i + 1) % 2];
        nextInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = waveDescSets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &prevInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = waveDescSets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &currInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = waveDescSets_[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].pImageInfo = &nextInfo;

        vkUpdateDescriptorSets(dev, 3, writes, 0, nullptr);
    }

    // 初始清空高度场（保持在 GENERAL 布局，供计算着色器读写）
    VkCommandBuffer cmd = device_->beginSingleTimeCommands();
    for (int i = 0; i < 2; i++) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = waveImages_[i];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkClearColorValue clear{};
        clear.float32[0] = 0.0f;
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, waveImages_[i], VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
    }
    device_->endSingleTimeCommands(cmd);

    waveCurrIdx_ = 0;
    Logger::info("[Water] 波动方程模拟初始化完成: " +
                 std::to_string(WAVE_TEX_SIZE) + "x" +
                 std::to_string(WAVE_TEX_SIZE) + " 覆盖 " +
                 std::to_string(int(WAVE_COVERAGE)) + "m");
}

void WaterRenderer::cleanupWaveSim() {
    VkDevice dev = device_->getDevice();
    VmaAllocator alloc = device_->getAllocator();

    if (wavePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, wavePipeline_, nullptr);
        wavePipeline_ = VK_NULL_HANDLE;
    }
    if (wavePipeLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, wavePipeLayout_, nullptr);
        wavePipeLayout_ = VK_NULL_HANDLE;
    }
    if (waveDsLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, waveDsLayout_, nullptr);
        waveDsLayout_ = VK_NULL_HANDLE;
    }
    if (waveDsPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, waveDsPool_, nullptr);
        waveDsPool_ = VK_NULL_HANDLE;
    }
    if (waveSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(dev, waveSampler_, nullptr);
        waveSampler_ = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 2; i++) {
        if (waveViews_[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, waveViews_[i], nullptr);
            waveViews_[i] = VK_NULL_HANDLE;
        }
        if (waveImages_[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(alloc, waveImages_[i], waveAllocs_[i]);
            waveImages_[i] = VK_NULL_HANDLE;
        }
    }
}

void WaterRenderer::dispatchWaveSim(VkCommandBuffer cmd, const glm::vec3& cameraPos) {
    VkDevice dev = device_->getDevice();

    // 计算交互点在高度场中的 UV（投影到海平面）
    glm::vec2 delta = glm::vec2(interactionPos_.x, interactionPos_.z) - glm::vec2(cameraPos.x, cameraPos.z);
    float halfCov = WAVE_COVERAGE * 0.5f;
    glm::vec2 interactUV = (delta + glm::vec2(halfCov)) / glm::vec2(WAVE_COVERAGE);
    interactUV = glm::clamp(interactUV, 0.0f, 1.0f);

    // Barrier: 确保前一帧的 compute 写入完成，当前纹理可被读取（无布局转换，全程 GENERAL）
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = waveImages_[(waveCurrIdx_ + 1) % 2];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Dispatch 计算着色器
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, wavePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, wavePipeLayout_,
                            0, 1, &waveDescSets_[waveCurrIdx_], 0, nullptr);

    // PushConstants: c·dt, damping, interactUV.xy, strength, wind, texelSize
    float texelSize = 1.0f / static_cast<float>(WAVE_TEX_SIZE);
    float c = 20.0f;
    float cdt = c * currentDt_;         // 有效传播系数（波速 × 帧时间）
    float strength = std::min(interactionStrength_ * 0.5f, 1.0f);
    float windStrength = 0.5f;          // 持续风驱动强度
    struct { float data[7]; } pc = {{cdt, damping_, interactUV.x, interactUV.y, strength, windStrength, texelSize}};
    vkCmdPushConstants(cmd, wavePipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    uint32_t groups = (WAVE_TEX_SIZE + 15) / 16;
    vkCmdDispatch(cmd, groups, groups, 1);

    // Barrier: 确保 compute 写入完成，供后续读取
    VkImageMemoryBarrier compBarrier{};
    compBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    compBarrier.image = waveImages_[(waveCurrIdx_ + 1) % 2];
    compBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    compBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    compBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    compBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    compBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &compBarrier);

    // 切换 ping-pong 索引
    waveCurrIdx_ = (waveCurrIdx_ + 1) % 2;
}

// ========== 网格创建 ==========
void WaterRenderer::createGrid() {
    // ... 保持不变（和之前一样）
    const int vertsPerEdge = GRID_SEGMENTS + 1;
    const float cellSize = GRID_SIZE / static_cast<float>(GRID_SEGMENTS);
    const float halfSize = GRID_SIZE * 0.5f;

    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(vertsPerEdge) * vertsPerEdge * 11);

    for (int z = 0; z < vertsPerEdge; ++z) {
        for (int x = 0; x < vertsPerEdge; ++x) {
            float wx = -halfSize + static_cast<float>(x) * cellSize;
            float wz = -halfSize + static_cast<float>(z) * cellSize;
            vertices.push_back(wx);
            vertices.push_back(0.0f);
            vertices.push_back(wz);
            vertices.push_back(0.0f);
            vertices.push_back(1.0f);
            vertices.push_back(0.0f);
            vertices.push_back(1.0f);
            vertices.push_back(1.0f);
            vertices.push_back(1.0f);
            vertices.push_back(wx * 0.1f);
            vertices.push_back(wz * 0.1f);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(GRID_SEGMENTS) * GRID_SEGMENTS * 6);

    for (int z = 0; z < GRID_SEGMENTS; ++z) {
        for (int x = 0; x < GRID_SEGMENTS; ++x) {
            uint32_t tl = static_cast<uint32_t>(z) * vertsPerEdge + x;
            uint32_t tr = tl + 1;
            uint32_t bl = (static_cast<uint32_t>(z) + 1) * vertsPerEdge + x;
            uint32_t br = bl + 1;
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    indexCount_ = static_cast<uint32_t>(indices.size());

    VkDeviceSize vertexBufferSize = vertices.size() * sizeof(float);
    VkBufferCreateInfo vbInfo{};
    vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbInfo.size = vertexBufferSize;
    vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocOut;
    VkResult _vrVB = vmaCreateBuffer(device_->getAllocator(), &vbInfo, &allocInfo,
                                     &vertexBuffer_, &vertexBufferAllocation_, &allocOut);
    if (_vrVB != VK_SUCCESS) {
        throw std::runtime_error("[WaterRenderer] 创建顶点缓冲失败: " + vkResultToString(_vrVB));
    }
    memcpy(allocOut.pMappedData, vertices.data(), static_cast<size_t>(vertexBufferSize));

    VkDeviceSize indexBufferSize = indices.size() * sizeof(uint32_t);
    VkBufferCreateInfo ibInfo{};
    ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ibInfo.size = indexBufferSize;
    ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult _vrIB = vmaCreateBuffer(device_->getAllocator(), &ibInfo, &allocInfo,
                                     &indexBuffer_, &indexBufferAllocation_, &allocOut);
    if (_vrIB != VK_SUCCESS) {
        throw std::runtime_error("[WaterRenderer] 创建索引缓冲失败: " + vkResultToString(_vrIB));
    }
    memcpy(allocOut.pMappedData, indices.data(), static_cast<size_t>(indexBufferSize));
}

// ========== 管线创建 ==========
void WaterRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                   VkSampleCountFlagBits msaaSamples) {
    VkDevice dev = device_->getDevice();

    // 管线布局：push constants + 描述符集(高度场纹理)
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plCi{};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 0;
    plCi.pSetLayouts = nullptr;
    plCi.pushConstantRangeCount = 1;
    plCi.pPushConstantRanges = &pcRange;

    VkResult _vrPL = vkCreatePipelineLayout(dev, &plCi, nullptr, &pipelineLayout_);
    if (_vrPL != VK_SUCCESS) {
        throw std::runtime_error("[WaterRenderer] 创建管线布局失败: " + vkResultToString(_vrPL));
    }

    // 着色器模块
    auto loadShader = [&](const std::string& path) -> VkShaderModule {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            throw std::runtime_error("[WaterRenderer] 无法打开着色器: " + path);
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(size));
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = size;
        ci.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
        VkShaderModule module;
        VkResult _vr = vkCreateShaderModule(dev, &ci, nullptr, &module);
        if (_vr != VK_SUCCESS)
            throw std::runtime_error("[WaterRenderer] 创建着色器模块失败: " + vkResultToString(_vr));
        return module;
    };

    VkShaderModule vertModule = loadShader(AssetPaths::SHADER_DIR + std::string("water.vert.spv"));
    VkShaderModule fragModule = loadShader(AssetPaths::SHADER_DIR + std::string("water.frag.spv"));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 11 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = 3 * sizeof(float);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset = 6 * sizeof(float);
    attrs[3].location = 3;
    attrs[3].binding = 0;
    attrs[3].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[3].offset = 9 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = extent;

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = &vp;
    vpState.scissorCount = 1;
    vpState.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = msaaSamples;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_TRUE;
    cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.colorBlendOp = VK_BLEND_OP_ADD;
    cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cbAtt.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vpState;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.layout = pipelineLayout_;
    pi.renderPass = renderPass;
    pi.subpass = 0;

    VkResult _vrGP = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_);
    if (_vrGP != VK_SUCCESS) {
        vkDestroyShaderModule(dev, fragModule, nullptr);
        vkDestroyShaderModule(dev, vertModule, nullptr);
        throw std::runtime_error("[WaterRenderer] 创建管线失败: " + vkResultToString(_vrGP));
    }
    vkDestroyShaderModule(dev, fragModule, nullptr);
    vkDestroyShaderModule(dev, vertModule, nullptr);
}

// ========== 每帧更新 ==========
void WaterRenderer::update(float deltaTime, const glm::vec3& sunDirection,
                           float sunIntensity) {
    time_ += deltaTime;
    currentDt_ = deltaTime;  // 保存帧时间，供波动方程计算使用
    sunDirection_ = sunDirection;
    sunIntensity_ = sunIntensity;
}

// ========== 渲染 ==========
void WaterRenderer::render(VkCommandBuffer commandBuffer,
                           const glm::mat4& viewMatrix,
                           const glm::mat4& projectionMatrix,
                           const glm::vec3& cameraPos) {
    if (!initialized_) return;

    // 1. Dispatch 波动方程计算
    dispatchWaveSim(commandBuffer, cameraPos);

    // 2. Barrier: 确保刚计算的高度场对顶点着色器可见（保持在 GENERAL 布局）
    VkImageMemoryBarrier toRead{};
    toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.image = waveImages_[waveCurrIdx_];
    toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toRead);

    // 3. 绘制水面网格
    glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                     glm::vec3(cameraPos.x, seaLevel_, cameraPos.z));

    PushConstants pc{};
    pc.model = model;
    pc.view = viewMatrix;
    pc.proj = projectionMatrix;
    pc.color = glm::vec4(waterColor_, waterAlpha_);
    pc.waveParams = glm::vec4(waveAmp_, waveFreq_, waveSpeed_, time_);
    pc.sunDir_intensity = glm::vec4(sunDirection_, sunIntensity_);
    pc.interaction = glm::vec4(interactionPos_.x, interactionPos_.z,
                               interactionRadius_, interactionStrength_);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    VkBuffer vertexBuffers[] = {vertexBuffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
}

void WaterRenderer::rebuildPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                    VkSampleCountFlagBits msaaSamples) {
    VkDevice dev = device_->getDevice();
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    createPipeline(renderPass, extent, msaaSamples);
}

} // namespace owengine
