// Vulkan帧缓冲管理实现
// 负责创建和管理渲染目标的帧缓冲
#include "core/vulkan_framebuffer.h"
#include "core/vulkan_device.h"
#include <stdexcept>

namespace owengine {

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
void VulkanFramebuffer::create(const std::vector<VkImageView>& swapchainImageViews, VkExtent2D swapchainExtent, VkImageView colorImageView) {
    swapchainFramebuffers.resize(swapchainImageViews.size());
    
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::vector<VkImageView> attachments;
        
        // 如果使用MSAA，添加多重采样颜色附件
        if (colorImageView != VK_NULL_HANDLE) {
            attachments.push_back(colorImageView);  // 多重采样颜色附件
            attachments.push_back(device->getDepthImageView());  // 深度附件
            attachments.push_back(swapchainImageViews[i]);  // 解决附件
        } else {
            attachments.push_back(swapchainImageViews[i]);  // 颜色附件
            attachments.push_back(device->getDepthImageView());  // 深度附件
        }
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
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

void VulkanFramebuffer::recreate(const std::vector<VkImageView>& swapchainImageViews, VkExtent2D swapchainExtent, VkImageView colorImageView) {
    cleanup();
    create(swapchainImageViews, swapchainExtent, colorImageView);
}

} // namespace owengine