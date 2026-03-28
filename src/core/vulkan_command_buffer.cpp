// Vulkan命令缓冲管理实现
// 负责创建和管理渲染命令缓冲
#include "vulkan_command_buffer.h"
#include "vulkan_device.h"
#include <stdexcept>

namespace vgame {

// VulkanCommandBuffer构造函数
VulkanCommandBuffer::VulkanCommandBuffer(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass,
                                         VkPipeline pipeline, VkPipelineLayout pipelineLayout)
    : device(device), renderPass(renderPass), pipeline(pipeline), pipelineLayout(pipelineLayout) {
}

// VulkanCommandBuffer析构函数
VulkanCommandBuffer::~VulkanCommandBuffer() {
    cleanup();
}

// 创建命令缓冲
// 为每个交换链图像创建对应的命令缓冲
void VulkanCommandBuffer::create(size_t imageCount) {
    commandPool = device->getCommandPool();
    commandBuffers.resize(imageCount);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();
    
    if (vkAllocateCommandBuffers(device->getDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void VulkanCommandBuffer::cleanup() {
    if (!commandBuffers.empty() && commandPool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device->getDevice(), commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
    }
    commandBuffers.clear();
}

void VulkanCommandBuffer::record(size_t imageIndex, VkFramebuffer framebuffer, VkExtent2D swapchainExtent,
                               const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    
    if (vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent;
    
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    
    vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) swapchainExtent.width;
    viewport.height = (float) swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffers[imageIndex], 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;
    vkCmdSetScissor(commandBuffers[imageIndex], 0, 1, &scissor);
    
    // 设置push constants（变换矩阵）
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };
    
    PushConstants pushConstants{};
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    
    vkCmdPushConstants(commandBuffers[imageIndex], pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);
    
    // 绘制三角形（3个顶点）
    vkCmdDraw(commandBuffers[imageIndex], 3, 1, 0, 0);
    
    vkCmdEndRenderPass(commandBuffers[imageIndex]);
    
    if (vkEndCommandBuffer(commandBuffers[imageIndex]) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

} // namespace vgame