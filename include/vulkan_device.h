#ifndef VULKAN_DEVICE_H
#define VULKAN_DEVICE_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <optional>

namespace vgame {

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanDevice {
public:
    VulkanDevice(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface);
    ~VulkanDevice();

    // 禁止拷贝
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    SwapChainSupportDetails querySwapChainSupport() const;
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) const;
    
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
    VkCommandPool getCommandPool() const { return commandPool; }
    VkImage getDepthImage() const { return depthImage; }
    VkImageView getDepthImageView() const { return depthImageView; }

    void createDepthResources(VkExtent2D extent);
    void cleanupDepthResources();

private:
    void createCommandPool();

    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkCommandPool commandPool;
    
    // 深度缓冲资源
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
};

} // namespace vgame

#endif // VULKAN_DEVICE_H