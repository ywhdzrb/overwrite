// Vulkan命令缓冲管理实现
// 负责创建和管理渲染命令缓冲
#include "core/vulkan_command_buffer.hpp"
#include "core/vulkan_device.hpp"
#include <stdexcept>
#include "utils/vk_result.hpp"

namespace owengine {

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
    
    VkResult _vr = vkAllocateCommandBuffers(device->getDevice(), &allocInfo, commandBuffers.data());
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to allocate command buffers! ") + vkResultToString(_vr));
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
    
    VkResult _vr1 = vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo);
    if (_vr1 != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to begin recording command buffer! ") + vkResultToString(_vr1));
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
    
    // 绑定图形管线
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
    
    vkCmdEndRenderPass(commandBuffers[imageIndex]);
    
    VkResult _vr2 = vkEndCommandBuffer(commandBuffers[imageIndex]);
    if (_vr2 != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to record command buffer! ") + vkResultToString(_vr2));
    }
}

} // namespace owengine