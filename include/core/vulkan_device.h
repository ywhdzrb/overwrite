#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <optional>
#include <mutex>

namespace owengine {

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanDevice {
public:
    VulkanDevice(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
                 uint32_t graphicsQueueFamily, uint32_t presentQueueFamily);
    ~VulkanDevice();

    // 禁止拷贝
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    SwapChainSupportDetails querySwapChainSupport() const;
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) const;

    // 设置首选呈现模式（默认 Mailbox，可切换为 Fifo 以稳定帧间隔）
    void setPreferMailbox(bool enable) { preferMailbox_ = enable; }
    bool getPreferMailbox() const { return preferMailbox_; }
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, 
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) const;
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;

    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    VkSurfaceKHR getSurface() const { return surface; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }
    uint32_t getGraphicsQueueFamily() const { return graphicsQueueFamily; }
    uint32_t getPresentQueueFamily() const { return presentQueueFamily_; }
    VkCommandPool getCommandPool() const { return commandPool; }
    std::mutex& getQueueMutex() const { return queueMutex_; }
    VkImage getDepthImage() const { return depthImage; }
    VkImageView getDepthImageView() const { return depthImageView; }

    void createDepthResources(VkExtent2D extent, VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    void cleanupDepthResources();

private:
    void createCommandPool();

    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    uint32_t graphicsQueueFamily = 0;
    uint32_t presentQueueFamily_ = 0;
    VkCommandPool commandPool;

    // 呈现模式偏好：true=优先 Mailbox（默认），false=始终使用 Fifo
    bool preferMailbox_ = true;
    
    // 线程安全：保护 commandPool 的分配/释放
    mutable std::mutex commandPoolMutex_;
    // 线程安全：保护 graphicsQueue 的 vkQueueSubmit（支持后台线程资源加载）
    mutable std::mutex queueMutex_;
    
    // 深度缓冲资源
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
};

} // namespace owengine
