// Vulkan帧缓冲管理实现
// 负责创建和管理渲染目标的帧缓冲
#include "vulkan_framebuffer.h"
#include "vulkan_device.h"
#include <stdexcept>

namespace vgame {

// VulkanFramebuffer构造函数
VulkanFramebuffer::VulkanFramebuffer(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass)
    : device(device), renderPass(renderPass) {
}

// VulkanFramebuffer析构函数
VulkanFramebuffer::~VulkanFramebuffer() {
    cleanup();
}

// 创建帧缓冲
// 为每个交换链图像创建对应的帧缓冲
void VulkanFramebuffer::create(const std::vector<VkImageView>& swapchainImageViews, VkExtent2D swapchainExtent) {
    swapchainFramebuffers.resize(swapchainImageViews.size());
    
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        VkImageView attachments[] = {
            swapchainImageViews[i]
        };
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;
        
        if (vkCreateFramebuffer(device->getDevice(), &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void VulkanFramebuffer::cleanup() {
    for (auto framebuffer : swapchainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device->getDevice(), framebuffer, nullptr);
        }
    }
    swapchainFramebuffers.clear();
}

void VulkanFramebuffer::recreate(const std::vector<VkImageView>& swapchainImageViews, VkExtent2D swapchainExtent) {
    cleanup();
    create(swapchainImageViews, swapchainExtent);
}

} // namespace vgame