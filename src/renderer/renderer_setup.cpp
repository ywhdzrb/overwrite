// 渲染器资源配置 — 云合成管线 / MSAA 颜色资源 / 描述符集布局与池 / 场景配置加载
// 拆自 renderer.cpp，集中管理所有一次性创建和配置逻辑
#include "core/renderer.hpp"
#include "core/scene_config.hpp"
#include "core/game_config.hpp"
#include "renderer/light.hpp"
#include "core/vulkan_device.hpp"
#include "renderer/light_manager.hpp"
#include "renderer/shader_manager.hpp"
#include "renderer/cloud_system.hpp"
#include "renderer/gltf_model.hpp"
#include "renderer/texture_loader.hpp"
#include "renderer/texture.hpp"
#include "utils/logger.hpp"
#include "utils/asset_paths.hpp"
#include "utils/vk_result.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <array>
#include <vector>

namespace owengine {

// ============================================================
// 半分辨率云合成管线资源
// ============================================================

void Renderer::createCloudCompositeResources() {
    VkDevice dev = vulkanDevice_->getDevice();
    VkFormat swapchainFormat = swapchain_->getImageFormat();

    // --- 创建合成渲染通道（与主渲染通道结构兼容：color + depth） ---
    VkAttachmentDescription colorAtt{};
    colorAtt.format = swapchainFormat;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;       // 保留已有场景内容
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAtt{};
    depthAtt.format = VK_FORMAT_D32_SFLOAT;
    depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;        // 从主渲染通道加载深度实现遮挡
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::vector<VkAttachmentDescription> attachments = {colorAtt, depthAtt};

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo rpCi{};
    rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCi.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpCi.pAttachments = attachments.data();
    rpCi.subpassCount = 1;
    rpCi.pSubpasses = &subpass;
    rpCi.dependencyCount = 1;
    rpCi.pDependencies = &dep;

    VkResult _vrRP = vkCreateRenderPass(dev, &rpCi, nullptr, &cloudComposite_.renderPass);
    if (_vrRP != VK_SUCCESS) {
        throw std::runtime_error(std::string("[Renderer] 创建云合成渲染通道失败 ") + vkResultToString(_vrRP));
    }

    // --- 创建合成管线布局 ---
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dsLayoutCi{};
    dsLayoutCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsLayoutCi.bindingCount = 1;
    dsLayoutCi.pBindings = &samplerBinding;

    VkResult _vrDSL = vkCreateDescriptorSetLayout(dev, &dsLayoutCi, nullptr, &cloudComposite_.dsLayout);
    if (_vrDSL != VK_SUCCESS) {
        throw std::runtime_error(std::string("[Renderer] 创建云合成描述符集布局失败 ") + vkResultToString(_vrDSL));
    }

    VkPipelineLayoutCreateInfo plCi{};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1;
    plCi.pSetLayouts = &cloudComposite_.dsLayout;
    plCi.pushConstantRangeCount = 0;
    plCi.pPushConstantRanges = nullptr;

    VkResult _vrPL = vkCreatePipelineLayout(dev, &plCi, nullptr, &cloudComposite_.pipelineLayout);
    if (_vrPL != VK_SUCCESS) {
        throw std::runtime_error(std::string("[Renderer] 创建云合成管线布局失败 ") + vkResultToString(_vrPL));
    }

    // --- 创建描述符池 ---
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo dpCi{};
    dpCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCi.poolSizeCount = 1;
    dpCi.pPoolSizes = &poolSize;
    dpCi.maxSets = 1;

    VkResult _vrDSP = vkCreateDescriptorPool(dev, &dpCi, nullptr, &cloudComposite_.dsPool);
    if (_vrDSP != VK_SUCCESS) {
        throw std::runtime_error(std::string("[Renderer] 创建云合成描述符池失败 ") + vkResultToString(_vrDSP));
    }

    // --- 分配和更新描述符集 ---
    VkDescriptorSetAllocateInfo dsAi{};
    dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAi.descriptorPool = cloudComposite_.dsPool;
    dsAi.descriptorSetCount = 1;
    dsAi.pSetLayouts = &cloudComposite_.dsLayout;

    VkResult _vrDSA = vkAllocateDescriptorSets(dev, &dsAi, &cloudComposite_.ds);
    if (_vrDSA != VK_SUCCESS) {
        throw std::runtime_error(std::string("[Renderer] 分配云合成描述符集失败 ") + vkResultToString(_vrDSA));
    }

    // 绑定半分辨率云纹理
    VkDescriptorImageInfo cloudImageInfo{};
    cloudImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    cloudImageInfo.imageView = cloudSystem_->getHalfResImageView();
    cloudImageInfo.sampler = cloudSystem_->getHalfResSampler();

    VkWriteDescriptorSet writeDs{};
    writeDs.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDs.dstSet = cloudComposite_.ds;
    writeDs.dstBinding = 0;
    writeDs.dstArrayElement = 0;
    writeDs.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDs.descriptorCount = 1;
    writeDs.pImageInfo = &cloudImageInfo;

    vkUpdateDescriptorSets(dev, 1, &writeDs, 0, nullptr);

    // --- 创建合成管线 ---
    VkShaderModule vertModule = shaderManager_->getModule("shaders/cloud.vert.spv");
    VkShaderModule fragModule = shaderManager_->getModule("shaders/cloud_composite.frag.spv");

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

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 0;
    vi.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    ia.primitiveRestartEnable = VK_FALSE;

    VkExtent2D fullExt = swapchain_->getExtent();
    VkViewport vp{};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width = static_cast<float>(fullExt.width);
    vp.height = static_cast<float>(fullExt.height);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = fullExt;

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
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 深度测试：不透明地形写入深度，使区块间正确遮挡
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    // Alpha 混合：src * srcAlpha + dst * (1 - srcAlpha)
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
    pi.layout = cloudComposite_.pipelineLayout;
    pi.renderPass = cloudComposite_.renderPass;
    pi.subpass = 0;

    VkResult _vrGP = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pi, nullptr,
                                              &cloudComposite_.pipeline);
    if (_vrGP != VK_SUCCESS) {
        throw std::runtime_error(std::string("[Renderer] 创建云合成管线失败 ") + vkResultToString(_vrGP));
    }

    // --- 创建合成帧缓冲 ---
    VkImageView depthView = vulkanDevice_->getDepthImageView();
    size_t imageCount = swapchain_->getImageViews().size();
    cloudComposite_.framebuffers.resize(imageCount);

    for (size_t i = 0; i < imageCount; ++i) {
        std::vector<VkImageView> fbAttachments = {
            swapchain_->getImageViews()[i],
            depthView
        };

        VkFramebufferCreateInfo fbCi{};
        fbCi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCi.renderPass = cloudComposite_.renderPass;
        fbCi.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
        fbCi.pAttachments = fbAttachments.data();
        fbCi.width = fullExt.width;
        fbCi.height = fullExt.height;
        fbCi.layers = 1;

        VkResult _vrFB = vkCreateFramebuffer(dev, &fbCi, nullptr, &cloudComposite_.framebuffers[i]);
        if (_vrFB != VK_SUCCESS) {
            throw std::runtime_error(std::string("[Renderer] 创建云合成帧缓冲失败 ") + vkResultToString(_vrFB));
        }
    }

    Logger::info("[Renderer] 云合成管线资源创建完成");
}

void Renderer::cleanupCloudCompositeResources() {
    VkDevice dev = vulkanDevice_->getDevice();

    if (cloudComposite_.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, cloudComposite_.pipeline, nullptr);
        cloudComposite_.pipeline = VK_NULL_HANDLE;
    }
    if (cloudComposite_.pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, cloudComposite_.pipelineLayout, nullptr);
        cloudComposite_.pipelineLayout = VK_NULL_HANDLE;
    }
    if (cloudComposite_.dsLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, cloudComposite_.dsLayout, nullptr);
        cloudComposite_.dsLayout = VK_NULL_HANDLE;
    }
    if (cloudComposite_.dsPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, cloudComposite_.dsPool, nullptr);
        cloudComposite_.dsPool = VK_NULL_HANDLE;
    }
    cloudComposite_.ds = VK_NULL_HANDLE;

    for (auto fb : cloudComposite_.framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(dev, fb, nullptr);
        }
    }
    cloudComposite_.framebuffers.clear();

    if (cloudComposite_.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, cloudComposite_.renderPass, nullptr);
        cloudComposite_.renderPass = VK_NULL_HANDLE;
    }
}

// ============================================================
// MSAA 颜色资源
// ============================================================

void Renderer::createColorResources() {
    VkFormat colorFormat = swapchain_->getImageFormat();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    VkExtent2D targetExt = {std::max(1u, (uint32_t)(swapchain_->getExtent().width * fsrScale_)),
                            std::max(1u, (uint32_t)(swapchain_->getExtent().height * fsrScale_))};
    imageInfo.extent.width = targetExt.width;
    imageInfo.extent.height = targetExt.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = colorFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples = msaaSamples_;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkResult _vrCImg = vmaCreateImage(vulkanDevice_->getAllocator(), &imageInfo, &allocInfo, &colorImage_, &colorImageAllocation_, nullptr);
    if (_vrCImg != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create color image! ") + vkResultToString(_vrCImg));
    }

    VkImageViewCreateInfo imageViewInfo{};
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.image = colorImage_;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.format = colorFormat;
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewInfo.subresourceRange.baseMipLevel = 0;
    imageViewInfo.subresourceRange.levelCount = 1;
    imageViewInfo.subresourceRange.baseArrayLayer = 0;
    imageViewInfo.subresourceRange.layerCount = 1;

    VkResult _vrCView = vkCreateImageView(vulkanDevice_->getDevice(), &imageViewInfo, nullptr, &colorImageView_);
    if (_vrCView != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create color image view! ") + vkResultToString(_vrCView));
    }
}

void Renderer::cleanupColorResources() {
    if (colorImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkanDevice_->getDevice(), colorImageView_, nullptr);
        colorImageView_ = VK_NULL_HANDLE;
    }
    if (colorImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(vulkanDevice_->getAllocator(), colorImage_, colorImageAllocation_);
        colorImage_ = VK_NULL_HANDLE;
        colorImageAllocation_ = VK_NULL_HANDLE;
    }
}

void Renderer::setMsaaSamples(VkSampleCountFlagBits samples) {
    msaaSamples_ = samples;
    recreateSwapchain();
}

// ============================================================
// 描述符集布局
// ============================================================

void Renderer::createDescriptorSetLayouts() {
    // 1. 创建纹理描述符集布局 (set = 0, binding = 0)
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 1> textureBindings = {samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo textureLayoutInfo{};
    textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    textureLayoutInfo.bindingCount = static_cast<uint32_t>(textureBindings.size());
    textureLayoutInfo.pBindings = textureBindings.data();

    VkResult _vrTxt = vkCreateDescriptorSetLayout(vulkanDevice_->getDevice(), &textureLayoutInfo, nullptr, &textureDescriptorSetLayout_);
    if (_vrTxt != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create texture descriptor set layout! ") + vkResultToString(_vrTxt));
    }

    // 2. 创建光源描述符集布局 (set = 1, binding = 0)
    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 0;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lightBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 1> lightBindings = {lightBinding};

    VkDescriptorSetLayoutCreateInfo lightLayoutInfo{};
    lightLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightLayoutInfo.bindingCount = static_cast<uint32_t>(lightBindings.size());
    lightLayoutInfo.pBindings = lightBindings.data();

    VkResult _vrLit = vkCreateDescriptorSetLayout(vulkanDevice_->getDevice(), &lightLayoutInfo, nullptr, &lightDescriptorSetLayout_);
    if (_vrLit != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create light descriptor set layout! ") + vkResultToString(_vrLit));
    }
}

void Renderer::createDescriptorPool(uint32_t maxSets, uint32_t descriptorCount) {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = descriptorCount + 2;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;

    VkResult _vr = vkCreateDescriptorPool(vulkanDevice_->getDevice(), &poolInfo, nullptr, &descriptorPool_);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create descriptor pool! ") + vkResultToString(_vr));
    }
}

void Renderer::createDescriptorSets() {
    // 为玩家模型创建纹理描述符集
    auto createIfNotExists = [this](GLTFModel* model, VkDescriptorSet& descriptorSet, const std::string& modelName) {
        if (descriptorSet != VK_NULL_HANDLE) return;

        if (!model || model->getMeshCount() == 0) return;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &textureDescriptorSetLayout_;

        if (vkAllocateDescriptorSets(vulkanDevice_->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
            Logger::warning("无法为 " + modelName + " 分配纹理描述符集");
            return;
        }

        std::shared_ptr<Texture> texture = model->getFirstTexture();
        if (texture) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = texture->getImageView();
            imageInfo.sampler = texture->getSampler();

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = descriptorSet;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(vulkanDevice_->getDevice(), 1, &descriptorWrite, 0, nullptr);
            Logger::info("为 " + modelName + " 创建纹理描述符集");
        }
    };

    // 保留默认纹理描述符集
    if (textureDescriptorSet_ == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo textureAllocInfo{};
        textureAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        textureAllocInfo.descriptorPool = descriptorPool_;
        textureAllocInfo.descriptorSetCount = 1;
        textureAllocInfo.pSetLayouts = &textureDescriptorSetLayout_;

        VkResult _vrTxtDS = vkAllocateDescriptorSets(vulkanDevice_->getDevice(), &textureAllocInfo, &textureDescriptorSet_);
        if (_vrTxtDS != VK_SUCCESS) {
            throw std::runtime_error(std::string("failed to allocate default texture descriptor set! ") + vkResultToString(_vrTxtDS));
        }
        Logger::info("Default texture descriptor set allocated");
    }

    // 2. 创建光源 storage buffer
    constexpr size_t MAX_LIGHTS = MAX_SHADER_LIGHTS;
    size_t lightsSize = sizeof(ShaderLight) * MAX_LIGHTS;
    size_t ambientSize = sizeof(glm::vec3);
    size_t lightCountSize = sizeof(int);
    VkDeviceSize bufferSize = lightsSize + ambientSize + lightCountSize;

    Logger::info("Creating light storage buffer: size=" + std::to_string(bufferSize)
                 + " (" + std::to_string(MAX_LIGHTS) + " lights)");

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocOut;
    VkResult _vrUBuf = vmaCreateBuffer(vulkanDevice_->getAllocator(), &bufferInfo, &allocInfo, &lightUniformBuffer_, &lightUniformBufferAllocation_, &allocOut);
    if (_vrUBuf != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create light uniform buffer! ") + vkResultToString(_vrUBuf));
    }
    lightUniformBufferMapped_ = allocOut.pMappedData;

    // 3. 创建光源描述符集
    VkDescriptorSetAllocateInfo lightAllocInfo{};
    lightAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lightAllocInfo.descriptorPool = descriptorPool_;
    lightAllocInfo.descriptorSetCount = 1;
    lightAllocInfo.pSetLayouts = &lightDescriptorSetLayout_;

    VkResult _vrLitDS = vkAllocateDescriptorSets(vulkanDevice_->getDevice(), &lightAllocInfo, &lightDescriptorSet_);
    if (_vrLitDS != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to allocate light descriptor set! ") + vkResultToString(_vrLitDS));
    }

    VkDescriptorBufferInfo bufferInfo2{};
    bufferInfo2.buffer = lightUniformBuffer_;
    bufferInfo2.offset = 0;
    bufferInfo2.range = bufferSize;

    VkWriteDescriptorSet lightDescriptorWrite{};
    lightDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lightDescriptorWrite.dstSet = lightDescriptorSet_;
    lightDescriptorWrite.dstBinding = 0;
    lightDescriptorWrite.dstArrayElement = 0;
    lightDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightDescriptorWrite.descriptorCount = 1;
    lightDescriptorWrite.pBufferInfo = &bufferInfo2;

    vkUpdateDescriptorSets(vulkanDevice_->getDevice(), 1, &lightDescriptorWrite, 0, nullptr);

    // 初始化光源数据
    updateLightUniformBuffer();

    // 4. 创建阴影描述符集（由 LightManager 中的 ShadowMapper 管理）
    if (lightManager_->isShadowInitialized()) {
        lightManager_->getShadowMapper()->allocateDescriptorSets(descriptorPool_);
        Logger::info("[Renderer] 阴影描述符集已分配");
    }
}

void Renderer::updateLightUniformBuffer() {
    ShaderLightArray lights = lightManager_->getShaderLightData();
    int lightCount = lightManager_->getEnabledLightCount();
    glm::vec3 ambientColor = lightManager_->getAmbient();

    constexpr size_t MAX_LIGHTS = MAX_SHADER_LIGHTS;
    size_t lightsSize = sizeof(ShaderLight) * MAX_LIGHTS;
    size_t ambientSize = sizeof(glm::vec3);
    size_t lightCountSize = sizeof(int);

    void* data = lightUniformBufferMapped_;

    memcpy(data, lights.data(), lightsSize);
    memcpy(static_cast<char*>(data) + lightsSize, &ambientColor, ambientSize);
    memcpy(static_cast<char*>(data) + lightsSize + ambientSize, &lightCount, lightCountSize);
}

// ==================== 场景配置加载 ====================

SceneConfig Renderer::loadSceneConfig(const std::string& configFile) {
    SceneConfig sceneConfig;

    try {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            Logger::warning("无法打开场景配置文件: " + configFile);
            return sceneConfig;
        }

        nlohmann::json j;
        file >> j;

        // 加载环境光配置
        if (j.contains("ambient")) {
            const auto& ambient = j["ambient"];
            if (ambient.contains("color")) {
                auto c = ambient["color"];
                sceneConfig.ambient.color = glm::vec3(c[0], c[1], c[2]);
            }
            sceneConfig.ambient.intensity = ambient.value("intensity", 0.3f);
        }

        // 加载光源配置
        if (j.contains("lights") && j["lights"].is_array()) {
            for (const auto& item : j["lights"]) {
                LightConfig config;
                config.id = item.value("id", "");
                config.name = item.value("name", "");
                config.type = item.value("type", "point");
                config.enabled = item.value("enabled", true);

                if (item.contains("position")) {
                    auto pos = item["position"];
                    config.position = glm::vec3(pos[0], pos[1], pos[2]);
                }
                if (item.contains("direction")) {
                    auto dir = item["direction"];
                    config.direction = glm::vec3(dir[0], dir[1], dir[2]);
                }
                if (item.contains("color")) {
                    auto c = item["color"];
                    config.color = glm::vec3(c[0], c[1], c[2]);
                }

                config.intensity = item.value("intensity", 1.0f);
                config.constant = item.value("constant", 1.0f);
                config.linear = item.value("linear", 0.09f);
                config.quadratic = item.value("quadratic", 0.032f);
                config.innerCutoff = item.value("innerCutoff", 12.5f);
                config.outerCutoff = item.value("outerCutoff", 17.5f);
                config.shadowEnabled = item.value("shadowEnabled", false);
                config.shadowIntensity = item.value("shadowIntensity", 0.3f);

                sceneConfig.lights.push_back(config);
            }
        }

        // 加载模型配置
        if (j.contains("models") && j["models"].is_array()) {
            for (const auto& item : j["models"]) {
                ModelConfig config;
                config.id = item.value("id", "");
                config.file = item.value("file", "");
                config.enabled = item.value("enabled", true);

                if (item.contains("position")) {
                    auto pos = item["position"];
                    config.position = glm::vec3(pos[0], pos[1], pos[2]);
                }
                if (item.contains("rotation")) {
                    auto rot = item["rotation"];
                    config.rotation = glm::vec3(rot[0], rot[1], rot[2]);
                }
                if (item.contains("scale")) {
                    auto sc = item["scale"];
                    config.scale = glm::vec3(sc[0], sc[1], sc[2]);
                }

                config.subdivisionIterations = item.value("subdivisionIterations", 0);
                config.playAnimation = item.value("playAnimation", false);
                config.animationIndex = item.value("animationIndex", 0);
                config.playAllAnimations = item.value("playAllAnimations", false);
                config.description = item.value("description", "");

                if (item.contains("hiddenMeshNames")) {
                    for (const auto& h : item["hiddenMeshNames"]) {
                        config.hiddenMeshNames.push_back(h.get<std::string>());
                    }
                }

                sceneConfig.models.push_back(config);
            }
        }

        Logger::info("从 " + configFile + " 加载了 " +
            std::to_string(sceneConfig.lights.size()) + " 个光源和 " +
            std::to_string(sceneConfig.models.size()) + " 个模型配置");
    } catch (const std::exception& e) {
        Logger::error("加载场景配置失败: " + std::string(e.what()));
    }

    return sceneConfig;
}

void Renderer::loadLightsFromConfig(const SceneConfig& config) {
    lightManager_->setAmbientColor(config.ambient.color);
    lightManager_->setAmbientIntensity(config.ambient.intensity);

    lightManager_->clear();

    for (const auto& lightConfig : config.lights) {
        if (!lightConfig.enabled) {
            continue;
        }

        int lightId = -1;

        if (lightConfig.type == "directional") {
            lightId = lightManager_->addDirectionalLight(
                lightConfig.name.empty() ? lightConfig.id : lightConfig.name,
                lightConfig.direction,
                lightConfig.color,
                lightConfig.intensity
            );
        } else if (lightConfig.type == "point") {
            lightId = lightManager_->addPointLight(
                lightConfig.name.empty() ? lightConfig.id : lightConfig.name,
                lightConfig.position,
                lightConfig.color,
                lightConfig.intensity,
                10.0f
            );
            if (Light* light = lightManager_->getLight(lightId)) {
                light->setConstant(lightConfig.constant);
                light->setLinear(lightConfig.linear);
                light->setQuadratic(lightConfig.quadratic);
            }
        } else if (lightConfig.type == "spot") {
            lightId = lightManager_->addSpotLight(
                lightConfig.name.empty() ? lightConfig.id : lightConfig.name,
                lightConfig.position,
                lightConfig.direction,
                lightConfig.color,
                lightConfig.intensity,
                lightConfig.innerCutoff,
                lightConfig.outerCutoff,
                10.0f
            );
            if (Light* light = lightManager_->getLight(lightId)) {
                light->setConstant(lightConfig.constant);
                light->setLinear(lightConfig.linear);
                light->setQuadratic(lightConfig.quadratic);
                light->setShadowEnabled(lightConfig.shadowEnabled);
                light->setShadowIntensity(lightConfig.shadowIntensity);
            }
        }

        if (lightId >= 0 && lightConfig.type == "directional") {
            if (Light* light = lightManager_->getLight(lightId)) {
                light->setShadowEnabled(lightConfig.shadowEnabled);
                light->setShadowIntensity(lightConfig.shadowIntensity);
            }
        }
    }

    Logger::info("加载了 " + std::to_string(lightManager_->getLightCount()) + " 个光源");
}

void Renderer::reloadSceneConfig() {
    Logger::info("重新加载场景配置...");

    SceneConfig config = loadSceneConfig(AssetPaths::SCENE_CONFIG);

    if (!config.lights.empty()) {
        loadLightsFromConfig(config);
    }

    Logger::info("场景配置重新加载完成");
}

std::vector<ModelConfig> Renderer::loadModelConfig(const std::string& configFile) {
    SceneConfig sceneConfig = loadSceneConfig(configFile);
    return sceneConfig.models;
}

VkDescriptorSet Renderer::createModelDescriptorSet(GLTFModel* model, const std::string& modelId, VkDescriptorPool pool) {
    VkDescriptorPool targetPool = (pool != VK_NULL_HANDLE) ? pool : descriptorPool_;

    std::unique_lock<std::mutex> poolLock(descriptorPoolMutex_, std::defer_lock);
    if (pool == VK_NULL_HANDLE) poolLock.lock();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = targetPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureDescriptorSetLayout_;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(vulkanDevice_->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        Logger::warning("无法为模型 " + modelId + " 分配纹理描述符集");
        return VK_NULL_HANDLE;
    }

    if (pool == VK_NULL_HANDLE) poolLock.unlock();

    std::shared_ptr<Texture> texture = model->getFirstTexture();
    if (texture) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture->getImageView();
        imageInfo.sampler = texture->getSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(vulkanDevice_->getDevice(), 1, &descriptorWrite, 0, nullptr);
        Logger::info("为模型 " + modelId + " 创建纹理描述符集成功");
    } else {
        Logger::warning("模型 " + modelId + " 没有纹理，使用默认");
    }

    return descriptorSet;
}

void Renderer::loadModelsFromConfig(const std::vector<ModelConfig>& configs) {
    for (const auto& config : configs) {
        if (!config.enabled) {
            Logger::info("模型 " + config.id + " 已禁用，跳过");
            continue;
        }

        auto model = std::make_unique<GLTFModel>(vulkanDevice_, textureLoader_);
        model->setPosition(config.position);
        model->setRotation(config.rotation.x, config.rotation.y, config.rotation.z);
        model->setScale(config.scale);

        if (config.subdivisionIterations > 0) {
            model->setSubdivisionIterations(config.subdivisionIterations);
        }

        if (!model->loadFromFile(config.file)) {
            Logger::error("加载模型失败: " + config.file);
            continue;
        }

        model->createMeshDescriptorSets(textureDescriptorSetLayout_, descriptorPool_);

        if (!config.hiddenMeshNames.empty()) {
            model->setHiddenNodeNames(config.hiddenMeshNames);
        }

        if (config.playAllAnimations && model->getAnimationCount() > 0) {
            model->playAllAnimations(true, 1.0f);
        } else if (config.playAnimation && model->getAnimationCount() > config.animationIndex) {
            model->playAnimation(config.animationIndex, true, 1.0f);
        }

        VkDescriptorSet descriptorSet = createModelDescriptorSet(model.get(), config.id);

        models_[config.id] = std::move(model);
        modelDescriptorSets_[config.id] = descriptorSet;

        Logger::info("模型 " + config.id + " 已加载");
    }
}

} // namespace owengine
