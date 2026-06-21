#pragma once

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
