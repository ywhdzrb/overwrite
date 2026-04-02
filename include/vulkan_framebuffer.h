#ifndef VULKAN_FRAMEBUFFER_H
#define VULKAN_FRAMEBUFFER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace vgame {

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
    
    const std::vector<VkFramebuffer>& getFramebuffers() const { return swapchainFramebuffers; }

private:
    std::shared_ptr<VulkanDevice> device;
    VkRenderPass renderPass;
    std::vector<VkFramebuffer> swapchainFramebuffers;
};

} // namespace vgame

#endif // VULKAN_FRAMEBUFFER_H