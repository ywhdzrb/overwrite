// 阴影映射管理器实现
// 负责方向光阴影贴图资源、渲染通道/管线、描述符集管理
#include "renderer/shadow_mapper.hpp"
#include "core/vulkan_device.hpp"
#include "utils/logger.hpp"
#include <fstream>
#include <vector>
#include <array>
#include <glm/gtc/matrix_transform.hpp>

namespace owengine {

// ============================================================
// 工具函数
// ============================================================

/**
 * @brief 从文件读取 SPIR-V 着色器二进制数据
 */
static std::vector<char> readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        Logger::error("[ShadowMapper] 无法打开着色器文件: " + filename);
        return {};
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();
    return buffer;
}

/**
 * @brief 创建 VkShaderModule
 */
static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        Logger::error("[ShadowMapper] 创建着色器模块失败");
        return VK_NULL_HANDLE;
    }
    return module;
}

// ============================================================
// 初始化 / 清理
// ============================================================

void ShadowMapper::init(const std::shared_ptr<VulkanDevice>& device, uint32_t mapSize,
                        const std::vector<VkDescriptorSetLayout>& dsLayouts) {
    if (initialized_) {
        Logger::warning("[ShadowMapper] 重复初始化");
        return;
    }
    device_ = device;
    mapSize_ = mapSize;
    // 保存主管线传入的 descriptor set 布局（set=0 纹理, set=1 光照），
    // 阴影 pass 中只会绑定 set=0（由 TerrainRenderer / GLTFModel 内部绑定纹理），
    // 不绑定 set=2（阴影描述符集仅用于主 pass）
    pipelineDsLayouts_ = dsLayouts;

    Logger::info("[ShadowMapper] 初始化，分辨率 " + std::to_string(mapSize_) + "×" + std::to_string(mapSize_));

    createShadowMapImage();
    createSampler();
    createRenderPass();
    createFramebuffer();
    // 创建阴影自己的描述符集布局（set=2，用于主 pass 采样阴影），
    // 放在 createPipeline 之前，以便在管线布局中包含它
    createDescriptorSetLayout();
    createPipeline();
    createUniformBuffer();

    initialized_ = true;
    Logger::info("[ShadowMapper] 初始化完成");
}

void ShadowMapper::cleanup() {
    if (!initialized_) return;
    VkDevice dev = device_->getDevice();

    if (shadowPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, shadowPipeline_, nullptr);
        shadowPipeline_ = VK_NULL_HANDLE;
    }
    if (shadowPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, shadowPipelineLayout_, nullptr);
        shadowPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (shadowFramebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(dev, shadowFramebuffer_, nullptr);
        shadowFramebuffer_ = VK_NULL_HANDLE;
    }
    if (shadowRenderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, shadowRenderPass_, nullptr);
        shadowRenderPass_ = VK_NULL_HANDLE;
    }
    if (shadowSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(dev, shadowSampler_, nullptr);
        shadowSampler_ = VK_NULL_HANDLE;
    }
    if (shadowMapView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, shadowMapView_, nullptr);
        shadowMapView_ = VK_NULL_HANDLE;
    }
    if (shadowMapImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(device_->getAllocator(), shadowMapImage_, shadowMapAllocation_);
        shadowMapImage_ = VK_NULL_HANDLE;
        shadowMapAllocation_ = VK_NULL_HANDLE;
    }
    // 销毁每帧的 uniform 缓冲
    for (uint32_t i = 0; i < MAX_SHADOW_FRAMES; i++) {
        if (shadowUniformBuffers_[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(device_->getAllocator(), shadowUniformBuffers_[i], shadowUniformAllocations_[i]);
            shadowUniformBuffers_[i] = VK_NULL_HANDLE;
            shadowUniformAllocations_[i] = VK_NULL_HANDLE;
            shadowUniformMapped_[i] = nullptr;
        }
        shadowDescriptorSets_[i] = VK_NULL_HANDLE;
    }
    // 销毁描述符集布局（描述符集由池自动回收）
    if (dsLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, dsLayout_, nullptr);
        dsLayout_ = VK_NULL_HANDLE;
    }

    initialized_ = false;
    Logger::info("[ShadowMapper] 已清理");
}

// ============================================================
// 阴影贴图图像
// ============================================================

void ShadowMapper::createShadowMapImage() {
    VkDevice dev = device_->getDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = mapSize_;
    imageInfo.extent.height = mapSize_;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    // R32_SFLOAT 颜色附件法存储深度，避免深度比较在驱动级别上的不确定行为
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkResult _vr = vmaCreateImage(device_->getAllocator(), &imageInfo, &allocInfo,
                                  &shadowMapImage_, &shadowMapAllocation_, nullptr);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error("[ShadowMapper] 创建阴影贴图图像失败");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowMapImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    _vr = vkCreateImageView(dev, &viewInfo, nullptr, &shadowMapView_);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error("[ShadowMapper] 创建阴影贴图图像视图失败");
    }

    // 使用一次性命令将阴影贴图布局从 UNDEFINED 过渡到 SHADER_READ_ONLY_OPTIMAL
    // 后续帧中，阴影渲染通道从 SHADER_READ_ONLY 自动过渡到 DEPTH_ATTACHMENT
    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandPool = device_->getCommandPool();
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    vkAllocateCommandBuffers(dev, &cmdAlloc, &cmdBuf);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadowMapImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    vkQueueSubmit(device_->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(device_->getGraphicsQueue());

    vkFreeCommandBuffers(dev, device_->getCommandPool(), 1, &cmdBuf);
}

// ============================================================
// 采样器（硬件 PCF 比较模式）
// ============================================================

void ShadowMapper::createSampler() {
    VkDevice dev = device_->getDevice();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    // 常规采样（无比较模式，主着色器中手动做深度比较 + PCF）
    samplerInfo.compareEnable = VK_FALSE;
    // 边框外为白色（不在阴影贴图范围内 = 不阴影）
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkResult _vr = vkCreateSampler(dev, &samplerInfo, nullptr, &shadowSampler_);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error("[ShadowMapper] 创建采样器失败");
    }
}

// ============================================================
// 渲染通道（仅深度）
// ============================================================

void ShadowMapper::createRenderPass() {
    VkDevice dev = device_->getDevice();

    // 颜色附件（R32_SFLOAT 存储深度值）
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R32_SFLOAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // 使用 SHADER_READ_ONLY_OPTIMAL 而非 UNDEFINED，因为上一帧结束后图像处于此状态。
    // 若设为 UNDEFINED 跨帧时驱动需插入额外布局转换，可能引入状态不一致。
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // 子通道依赖
    // 外部→子通道0：从 SHADER_READ_ONLY 过渡到 COLOR_ATTACHMENT，
    // 等待上一帧（或之前的槽命令）的片段着色器读取完成后再写入。
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies = deps;

    VkResult _vr = vkCreateRenderPass(dev, &rpInfo, nullptr, &shadowRenderPass_);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error("[ShadowMapper] 创建渲染通道失败");
    }
}

// ============================================================
// 帧缓冲
// ============================================================

void ShadowMapper::createFramebuffer() {
    VkDevice dev = device_->getDevice();

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = shadowRenderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &shadowMapView_;
    fbInfo.width = mapSize_;
    fbInfo.height = mapSize_;
    fbInfo.layers = 1;

    VkResult _vr = vkCreateFramebuffer(dev, &fbInfo, nullptr, &shadowFramebuffer_);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error("[ShadowMapper] 创建帧缓冲失败");
    }
}

// ============================================================
// 阴影管线
// ============================================================

void ShadowMapper::createPipeline() {
    VkDevice dev = device_->getDevice();

    auto vertCode = readShaderFile("shaders/shadow.vert.spv");
    auto fragCode = readShaderFile("shaders/shadow.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        throw std::runtime_error("[ShadowMapper] 着色器文件缺失，请先编译着色器");
    }

    VkShaderModule vertModule = createShaderModule(dev, vertCode);
    VkShaderModule fragModule = createShaderModule(dev, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        throw std::runtime_error("[ShadowMapper] 创建着色器模块失败");
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    // ===== 顶点输入（与主着色器一致） =====
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(glm::vec3) * 3 + sizeof(glm::vec2);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attrDescs{};
    attrDescs[0].binding = 0;
    attrDescs[0].location = 0;
    attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[0].offset = 0;
    attrDescs[1].binding = 0;
    attrDescs[1].location = 1;
    attrDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[1].offset = sizeof(glm::vec3);
    attrDescs[2].binding = 0;
    attrDescs[2].location = 2;
    attrDescs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[2].offset = sizeof(glm::vec3) * 2;
    attrDescs[3].binding = 0;
    attrDescs[3].location = 3;
    attrDescs[3].format = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[3].offset = sizeof(glm::vec3) * 3;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bindingDesc;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vi.pVertexAttributeDescriptions = attrDescs.data();

    // ===== 输入装配 =====
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ia.primitiveRestartEnable = VK_FALSE;

    // ===== 视口（动态） =====
    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount = 1;

    // ===== 光栅化（深度偏移） =====
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;
    raster.depthBiasConstantFactor = 0.0f;
    raster.depthBiasClamp = 0.0f;
    raster.depthBiasSlopeFactor = 0.0f;

    // ===== 多重采样 =====
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ===== 颜色混合（R32_SFLOAT 不需要混合） =====
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // ===== 深度模板（颜色附着法不需要深度写入） =====
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    // ===== 动态状态 =====
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    // ===== Push Constants（240 字节，与主着色器一致） =====
    // 使用 VERTEX|FRAGMENT 匹配子渲染器的推送阶段（即使阴影仅有顶点着色器）
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = 240;

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    // 包含与主管线相同的描述符集布局，确保子渲染器调用 vkCmdBindDescriptorSets 时不会崩溃
    plInfo.setLayoutCount = static_cast<uint32_t>(pipelineDsLayouts_.size());
    plInfo.pSetLayouts = pipelineDsLayouts_.data();
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;

    VkResult _vr = vkCreatePipelineLayout(dev, &plInfo, nullptr, &shadowPipelineLayout_);
    if (_vr != VK_SUCCESS) {
        vkDestroyShaderModule(dev, vertModule, nullptr);
        vkDestroyShaderModule(dev, fragModule, nullptr);
        throw std::runtime_error("[ShadowMapper] 创建管线布局失败");
    }

    // ===== 创建图形管线 =====
    VkGraphicsPipelineCreateInfo gpInfo{};
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &vi;
    gpInfo.pInputAssemblyState = &ia;
    gpInfo.pViewportState = &vpState;
    gpInfo.pRasterizationState = &raster;
    gpInfo.pMultisampleState = &ms;
    gpInfo.pDepthStencilState = &ds;
    gpInfo.pColorBlendState = &colorBlending;
    gpInfo.pDynamicState = &dynamic;
    gpInfo.layout = shadowPipelineLayout_;
    gpInfo.renderPass = shadowRenderPass_;
    gpInfo.subpass = 0;

    _vr = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &shadowPipeline_);

    vkDestroyShaderModule(dev, vertModule, nullptr);
    vkDestroyShaderModule(dev, fragModule, nullptr);

    if (_vr != VK_SUCCESS) {
        throw std::runtime_error("[ShadowMapper] 创建阴影管线失败");
    }
}

// ============================================================
// Shadow Uniform Buffer（光源 VP + 参数）
// ============================================================

/**
 * @brief ShadowUniform 的 GPU 布局（std140 对齐）
 *
 * struct ShadowUniform {
 *     mat4 lightVP;          // offset 0,   size 64
 *     float shadowIntensity; // offset 64,  size 4
 *     float _pad[3];         // offset 68,  size 12（对齐到 80）
 * };
 * total: 80 bytes
 */
struct ShadowUniform {
    glm::mat4 lightVP;
    float shadowIntensity;
    float _pad[3];
};
static_assert(sizeof(ShadowUniform) == 80, "ShadowUniform must be 80 bytes");

void ShadowMapper::createUniformBuffer() {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(ShadowUniform);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    ShadowUniform initData{};
    initData.lightVP = glm::mat4(1.0f);
    initData.shadowIntensity = 0.5f;

    for (uint32_t i = 0; i < MAX_SHADOW_FRAMES; i++) {
        VmaAllocationInfo allocOut;
        VkResult _vr = vmaCreateBuffer(device_->getAllocator(), &bufferInfo, &allocInfo,
                                       &shadowUniformBuffers_[i], &shadowUniformAllocations_[i], &allocOut);
        if (_vr != VK_SUCCESS) {
            throw std::runtime_error("[ShadowMapper] 创建 uniform 缓冲失败");
        }
        shadowUniformMapped_[i] = allocOut.pMappedData;
        memcpy(shadowUniformMapped_[i], &initData, sizeof(ShadowUniform));
    }
}

void ShadowMapper::updateUniformBuffer(uint32_t frameIndex) {
    if (frameIndex >= MAX_SHADOW_FRAMES) return;
    // 直接写入 HOST_VISIBLE + HOST_COHERENT 映射内存
    ShadowUniform data{};
    data.lightVP = lightVP_;
    data.shadowIntensity = shadowIntensity_;
    memcpy(shadowUniformMapped_[frameIndex], &data, sizeof(ShadowUniform));
}

// ============================================================
// 阴影 pass 管理
// ============================================================

void ShadowMapper::beginShadowPass(VkCommandBuffer commandBuffer) {
    VkClearValue clearValue{};
    clearValue.color.float32[0] = 1.0f; // R = 1.0（最远距离）

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = shadowRenderPass_;
    rpInfo.framebuffer = shadowFramebuffer_;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {mapSize_, mapSize_};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(mapSize_);
    vp.height = static_cast<float>(mapSize_);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = {mapSize_, mapSize_};
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);
}

void ShadowMapper::endShadowPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

// ============================================================
// 光源 VP 矩阵更新
// ============================================================

void ShadowMapper::updateLightMatrix(const glm::vec3& lightDir,
                                      const glm::vec3& cameraPos,
                                      float orthoSize) {
    // 视点位于相机位置沿光照方向偏移，形成从光源看向场景的视图
    glm::vec3 up = glm::abs(lightDir.y) < 0.99f
                   ? glm::vec3(0.0f, 1.0f, 0.0f)
                   : glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 lightPos = cameraPos + lightDir * orthoSize * 2.0f;
    lightView_ = glm::lookAt(lightPos, cameraPos, up);

    // 正交投影（near/far 为正，GLM 内部处理符号）
    float nearPlane = orthoSize * 0.5f;
    float farPlane = orthoSize * 3.0f;
    lightProj_ = glm::ortho(-orthoSize, orthoSize,
                             -orthoSize, orthoSize,
                             nearPlane, farPlane);

    // === 纹素对齐（Texel Snap）：消除相机移动时阴影边缘的亚纹素闪烁 ===
    // 将相机在光源空间的位置投影到 NDC，对齐到最近的纹素中心，
    // 这样相机移动时正交视锥体边界不会发生亚纹素抖动。
    float ndcTexelSize = 2.0f / static_cast<float>(mapSize_);
    glm::vec4 camLight = lightView_ * glm::vec4(cameraPos, 1.0f);
    // X 轴对齐：将 ndcX 对齐到最近的 ndcTexelSize 倍数
    float ndcX = camLight.x / orthoSize;
    float snappedX = glm::floor(ndcX / ndcTexelSize + 0.5f) * ndcTexelSize;
    lightProj_[3][0] += snappedX - ndcX;
    // Y 轴对齐
    float ndcY = camLight.y / orthoSize;
    float snappedY = glm::floor(ndcY / ndcTexelSize + 0.5f) * ndcTexelSize;
    lightProj_[3][1] += snappedY - ndcY;

    lightVP_ = lightProj_ * lightView_;
}

// ============================================================
// 描述符集创建
// ============================================================

void ShadowMapper::createDescriptorSetLayout() {
    VkDevice dev = device_->getDevice();

    // 描述符集布局 set=2:
    //   binding=0: 阴影贴图采样器 (COMBINED_IMAGE_SAMPLER)
    //   binding=1: 阴影 uniform 缓冲 (UNIFORM_BUFFER)
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkResult _vr = vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &dsLayout_);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error("[ShadowMapper] 创建描述符集布局失败");
    }
    Logger::info("[ShadowMapper] 描述符集布局创建完成");
}

void ShadowMapper::allocateDescriptorSets(VkDescriptorPool descriptorPool) {
    VkDevice dev = device_->getDevice();
    if (dsLayout_ == VK_NULL_HANDLE) {
        Logger::error("[ShadowMapper] 描述符集布局未创建，无法分配");
        return;
    }

    // 为每帧分配一个描述符集
    std::array<VkDescriptorSetLayout, MAX_SHADOW_FRAMES> layouts;
    for (uint32_t i = 0; i < MAX_SHADOW_FRAMES; i++) {
        layouts[i] = dsLayout_;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = MAX_SHADOW_FRAMES;
    allocInfo.pSetLayouts = layouts.data();

    VkResult _vr = vkAllocateDescriptorSets(dev, &allocInfo, shadowDescriptorSets_);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error("[ShadowMapper] 分配描述符集失败");
    }

    // 每帧描述符集：共享同一阴影贴图采样器，但绑定各自的 uniform 缓冲
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = shadowMapView_;
    imageInfo.sampler = shadowSampler_;

    for (uint32_t i = 0; i < MAX_SHADOW_FRAMES; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = shadowUniformBuffers_[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ShadowUniform);

        VkWriteDescriptorSet writes[2];

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].pNext = nullptr;
        writes[0].dstSet = shadowDescriptorSets_[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].pNext = nullptr;
        writes[1].dstSet = shadowDescriptorSets_[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(dev, 2, writes, 0, nullptr);
    }

    Logger::info("[ShadowMapper] 阴影描述符集分配完成（" + std::to_string(MAX_SHADOW_FRAMES) + " 帧）");
}

} // namespace owengine
