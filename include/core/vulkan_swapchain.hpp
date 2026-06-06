#pragma once

/**
 * @file vulkan_swapchain.hpp
 * @brief Vulkan 交换链管理 — 创建/销毁/重建交换链、图像视图
 *
 * 归属模块：core
 * 核心职责：封装 VkSwapchainKHR 生命周期，处理窗口大小变化时的重建
 * 依赖关系：VulkanDevice、GLFW
 * 关键设计：通过 VulkanDevice 获取队列族索引，避免重复查询
 */

// 标准库
#include <vector>
#include <memory>

// 第三方库
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

namespace owengine {

class VulkanDevice;

class VulkanSwapchain {
public:
    VulkanSwapchain(std::shared_ptr<VulkanDevice> device, GLFWwindow* window);
    ~VulkanSwapchain();

    // 禁止拷贝
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    void create();
    void cleanup();
    void recreate(GLFWwindow* window);
    
    VkSwapchainKHR getSwapchain() const { return swapchain_; }
    const std::vector<VkImage>& getImages() const { return swapchainImages_; }
    const std::vector<VkImageView>& getImageViews() const { return swapchainImageViews_; }
    VkFormat getImageFormat() const { return swapchainImageFormat_; }
    VkExtent2D getExtent() const { return swapchainExtent_; }

private:
    void createImageViews();

    std::shared_ptr<VulkanDevice> device_;
    GLFWwindow* window_;
    
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkFormat swapchainImageFormat_;
    VkExtent2D swapchainExtent_;
};

} // namespace owengine
