#ifndef VULKAN_RENDER_PASS_H
#define VULKAN_RENDER_PASS_H

#include <vulkan/vulkan.h>
#include <memory>

namespace owengine {

class VulkanDevice;

class VulkanRenderPass {
public:
    VulkanRenderPass(std::shared_ptr<VulkanDevice> device, VkFormat swapchainImageFormat, VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    ~VulkanRenderPass();

    // 禁止拷贝
    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

    void create();
    void cleanup();
    
    VkRenderPass getRenderPass() const { return renderPass; }
    
    void setMsaaSamples(VkSampleCountFlagBits samples) { msaaSamples = samples; }

private:
    std::shared_ptr<VulkanDevice> device;
    VkFormat swapchainImageFormat;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;  // 默认不使用MSAA
    VkRenderPass renderPass = VK_NULL_HANDLE;
};

} // namespace owengine

#endif // VULKAN_RENDER_PASS_H