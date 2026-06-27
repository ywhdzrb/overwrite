#pragma once

/**
 * @file vulkan_framebuffer.hpp
 * @brief Vulkan 帧缓冲管理 — 渲染通道输出绑定的创建与重建
 *
 * 归属模块：core
 * 核心职责：为每个交换链图像创建对应的 VkFramebuffer，支持交换链重建
 * 依赖关系：VulkanDevice、VkRenderPass
 * 关键设计：recreate() 封装销毁+创建流程，在交换链重建时一次性重新绑定
 */

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace owengine {

class VulkanDevice;

class VulkanFramebuffer {
public:
    VulkanFramebuffer(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass);
    ~VulkanFramebuffer();

    // 禁止拷贝
    VulkanFramebuffer(const VulkanFramebuffer&) = delete;
    VulkanFramebuffer& operator=(const VulkanFramebuffer&) = delete;

    void create(const std::vector<VkImageView>& swapchainImageViews, VkExtent2D swapchainExtent, VkImageView colorImageView = VK_NULL_HANDLE);
    void cleanup();
    void recreate(const std::vector<VkImageView>& swapchainImageViews, VkExtent2D swapchainExtent, VkImageView colorImageView = VK_NULL_HANDLE);
    
    [[nodiscard]] const std::vector<VkFramebuffer>& getFramebuffers() const noexcept { return swapchainFramebuffers_; }

private:
    std::shared_ptr<VulkanDevice> device_;
    VkRenderPass renderPass_;
    std::vector<VkFramebuffer> swapchainFramebuffers_;
};

} // namespace owengine
