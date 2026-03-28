#ifndef VULKAN_RENDER_PASS_H
#define VULKAN_RENDER_PASS_H

#include <vulkan/vulkan.h>
#include <memory>

namespace vgame {

class VulkanDevice;

class VulkanRenderPass {
public:
    VulkanRenderPass(std::shared_ptr<VulkanDevice> device, VkFormat swapchainImageFormat);
    ~VulkanRenderPass();

    // 禁止拷贝
    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

    void create();
    void cleanup();
    
    VkRenderPass getRenderPass() const { return renderPass; }

private:
    std::shared_ptr<VulkanDevice> device;
    VkFormat swapchainImageFormat;
    VkRenderPass renderPass = VK_NULL_HANDLE;
};

} // namespace vgame

#endif // VULKAN_RENDER_PASS_H