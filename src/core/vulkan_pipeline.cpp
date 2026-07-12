// Vulkan图形管线管理实现
// 负责创建和管理图形渲染管线，包括着色器
#include "core/vulkan_pipeline.hpp"
#include "core/vulkan_device.hpp"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <array>
#include <glm/glm.hpp>
#include "renderer/model_renderer.hpp"
#include "utils/vk_result.hpp"
#include "utils/logger.hpp"

namespace owengine {

// VulkanPipeline构造函数
VulkanPipeline::VulkanPipeline(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass,
                               VkExtent2D swapchainExtent, const std::string& vertexShaderPath,
                               const std::string& fragmentShaderPath,
                               VertexFormat format,
                               const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
                               VkSampleCountFlagBits msaaSamples)
    : device_(device), renderPass_(renderPass), swapchainExtent_(swapchainExtent),
      vertexShaderPath_(vertexShaderPath), fragmentShaderPath_(fragmentShaderPath),
      msaaSamples_(msaaSamples), vertexFormat_(format) {
    // 存储描述符集布局
    descriptorSetLayoutsList_ = descriptorSetLayouts;
}

// VulkanPipeline析构函数
VulkanPipeline::~VulkanPipeline() {
    cleanup();
}

// 创建图形管线
// 加载着色器并配置渲染管线状态
void VulkanPipeline::create() {
    auto vertShaderCode = readFile(vertexShaderPath_);
    auto fragShaderCode = readFile(fragmentShaderPath_);

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // 根据顶点格式设置顶点属性
    VkVertexInputBindingDescription bindingDescription{};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};

    // 使用构造函数传入的顶点格式枚举，替代字符串匹配
    bool isSkybox = (vertexFormat_ == VertexFormat::POSITION_ONLY);

    if (isSkybox) {
        // 天空盒：仅位置
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(glm::vec3);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributeDescription{};
        attributeDescription.binding = 0;
        attributeDescription.location = 0;
        attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescription.offset = 0;

        attributeDescriptions.push_back(attributeDescription);
    } else {
        // 标准：位置 + 法线 + 颜色 + 纹理坐标（与 Vertex 结构匹配）
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(glm::vec3) * 3 + sizeof(glm::vec2);  // position + normal + color + texCoord
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // 位置属性（location = 0）
        VkVertexInputAttributeDescription positionAttr{};
        positionAttr.binding = 0;
        positionAttr.location = 0;
        positionAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
        positionAttr.offset = 0;
        attributeDescriptions.push_back(positionAttr);

        // 法线属性（location = 1）
        VkVertexInputAttributeDescription normalAttr{};
        normalAttr.binding = 0;
        normalAttr.location = 1;
        normalAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
        normalAttr.offset = sizeof(glm::vec3);
        attributeDescriptions.push_back(normalAttr);

        // 颜色属性（location = 2）
        VkVertexInputAttributeDescription colorAttr{};
        colorAttr.binding = 0;
        colorAttr.location = 2;
        colorAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
        colorAttr.offset = sizeof(glm::vec3) * 2;
        attributeDescriptions.push_back(colorAttr);

        // 纹理坐标属性（location = 3）
        VkVertexInputAttributeDescription texCoordAttr{};
        texCoordAttr.binding = 0;
        texCoordAttr.location = 3;
        texCoordAttr.format = VK_FORMAT_R32G32_SFLOAT;
        texCoordAttr.offset = sizeof(glm::vec3) * 3;
        attributeDescriptions.push_back(texCoordAttr);
    }

    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) swapchainExtent_.width;
    viewport.height = (float) swapchainExtent_.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent_;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // 禁用背面剔除（不同模型格式缠绕顺序不一致）
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = msaaSamples_;

// 深度测试状态
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    if (isSkybox) {
        // 天空盒：禁用深度写入，但启用深度测试（LEQUAL），确保天空盒在最远处
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    } else {
        // 标准物体：启用深度测试和写入，LEQUAL 防止相邻区块共享顶点处的裂缝
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    }
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 定义 push constant 范围（用于传递变换矩阵）
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;

    // 根据顶点格式设置 push constant 大小
    if (isSkybox) {
        pushConstantRange.size = sizeof(glm::mat4) * 2;  // view + projection (天空盒)
    } else {
        pushConstantRange.size = sizeof(ModelRenderer::PushConstants);
    }

    // 获取设备 maxPushConstantsSize 限制
    VkPhysicalDeviceProperties _pcProps;
    vkGetPhysicalDeviceProperties(device_->getPhysicalDevice(), &_pcProps);
    uint32_t _pcLimit = _pcProps.limits.maxPushConstantsSize;

    if (pushConstantRange.size > _pcLimit) {
        Logger::warning("[VulkanPipeline] PushConstants size " + std::to_string(pushConstantRange.size)
                        + " exceeds device limit of " + std::to_string(_pcLimit)
                        + ". Clamping to limit - expect rendering issues.");
        pushConstantRange.size = _pcLimit;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    // 设置描述符集布局
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayoutsList_.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayoutsList_.empty() ? nullptr : descriptorSetLayoutsList_.data();

    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkResult _vr1 = vkCreatePipelineLayout(device_->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout_);
    if (_vr1 != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create pipeline layout! ") + vkResultToString(_vr1));
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;  // 添加深度测试状态
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkResult _vr2 = vkCreateGraphicsPipelines(device_->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_);
    if (_vr2 != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create graphics pipeline! ") + vkResultToString(_vr2));
    }

    vkDestroyShaderModule(device_->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(device_->getDevice(), vertShaderModule, nullptr);
}

void VulkanPipeline::cleanup() {
    if (graphicsPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_->getDevice(), graphicsPipeline_, nullptr);
        graphicsPipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_->getDevice(), pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
}

VkShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    VkResult _vr = vkCreateShaderModule(device_->getDevice(), &createInfo, nullptr, &shaderModule);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create shader module! ") + vkResultToString(_vr));
    }

    return shaderModule;
}

std::vector<char> VulkanPipeline::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

} // namespace owengine
