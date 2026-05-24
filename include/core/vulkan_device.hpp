#pragma once

/**
 * @file vulkan_device.h
 * @brief Vulkan 设备管理 — 逻辑设备封装、队列/命令池/VMA/深度资源管理
 *
 * 归属模块：core
 * 核心职责：封装 VkDevice、VmaAllocator、命令池、深度缓冲等资源生命周期
 * 依赖关系：Vulkan SDK、VMA、GLFW
 * 关键设计：线程安全 — queueMutex_ 保护 vkQueueSubmit，commandPoolMutex_ 保护命令分配
 *           支持 swapchain 查询（querySwapChainSupport）和呈现模式切换（Mailbox/Fifo）
 */

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace owengine {

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanDevice {
public:
    VulkanDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
                 uint32_t graphicsQueueFamily, uint32_t presentQueueFamily);
    ~VulkanDevice();

    // 禁止拷贝
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] SwapChainSupportDetails querySwapChainSupport() const;
    [[nodiscard]] VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const noexcept;
    [[nodiscard]] VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const noexcept;
    [[nodiscard]] VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) const noexcept;

    // 设置首选呈现模式（默认 Mailbox，可切换为 Fifo 以稳定帧间隔）
    void setPreferMailbox(bool enable) noexcept { preferMailbox_ = enable; }
    [[nodiscard]] bool getPreferMailbox() const noexcept { return preferMailbox_; }
    
    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, 
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) const;
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;

    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VkDevice getDevice() const noexcept { return device_; }
    [[nodiscard]] VkSurfaceKHR getSurface() const noexcept { return surface_; }
    [[nodiscard]] VkQueue getGraphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] VkQueue getPresentQueue() const noexcept { return presentQueue_; }
    [[nodiscard]] uint32_t getGraphicsQueueFamily() const noexcept { return graphicsQueueFamily_; }
    [[nodiscard]] uint32_t getPresentQueueFamily() const noexcept { return presentQueueFamily_; }
    [[nodiscard]] VkCommandPool getCommandPool() const noexcept { return commandPool_; }
    [[nodiscard]] std::mutex& getQueueMutex() const noexcept { return queueMutex_; }
    [[nodiscard]] VmaAllocator getAllocator() const noexcept { return allocator_; }
    [[nodiscard]] VkImage getDepthImage() const noexcept { return depthImage_; }
    [[nodiscard]] VkImageView getDepthImageView() const noexcept { return depthImageView_; }

    void createDepthResources(VkExtent2D extent, VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    void cleanupDepthResources();

private:
    void createCommandPool();

    VkInstance instance_;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    uint32_t presentQueueFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // 呈现模式偏好：true=优先 Mailbox（默认），false=始终使用 Fifo
    bool preferMailbox_ = true;
    
    // 线程安全：保护 commandPool 的分配/释放
    mutable std::mutex commandPoolMutex_;
    // 线程安全：保护 graphicsQueue 的 vkQueueSubmit（支持后台线程资源加载）
    mutable std::mutex queueMutex_;
    
    // 深度缓冲资源
    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
};

} // namespace owengine
