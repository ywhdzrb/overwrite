// Vulkan帧缓冲管理实现
// 负责创建和管理渲染目标的帧缓冲
#include "core/vulkan_framebuffer.hpp"
#include "core/vulkan_device.hpp"
#include <stdexcept>

namespace owengine {

// VulkanFramebuffer构造函数
VulkanFramebuffer::VulkanFramebuffer(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass)
    : device_(device), renderPass_(renderPass) {
}

// VulkanFramebuffer析构函数
VulkanFramebuffer::~VulkanFramebuffer() {
    cleanup();
}

// 创建帧缓冲
// 为每个交换链图像创建对应的帧缓冲
void VulkanFramebuffer::create(const std::vector<VkImageView>& swapchainImageViews, VkExtent2D swapchainExtent, VkImageView colorImageView) {
    swapchainFramebuffers_.resize(swapchainImageViews.size());
    
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::vector<VkImageView> attachments;
        
        if (colorImageView != VK_NULL_HANDLE) {
            attachments.push_back(colorImageView);
            attachments.push_back(device_->getDepthImageView());
            attachments.push_back(swapchainImageViews[i]);
        } else {
            attachments.push_back(swapchainImageViews[i]);
            attachments.push_back(device_->getDepthImageView());
        }
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;
        
        if (vkCreateFramebuffer(device_->getDevice(), &framebufferInfo, nullptr, &swapchainFramebuffers_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void VulkanFramebuffer::cleanup() {
    for (auto framebuffer : swapchainFramebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_->getDevice(), framebuffer, nullptr);
        }
    }
    swapchainFramebuffers_.clear();
}

void VulkanFramebuffer::recreate(const std::vector<VkImageView>& swapchainImageViews, VkExtent2D swapchainExtent, VkImageView colorImageView) {
    cleanup();
    create(swapchainImageViews, swapchainExtent, colorImageView);
}

} // namespace owengine