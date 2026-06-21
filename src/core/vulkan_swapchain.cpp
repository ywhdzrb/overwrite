// Vulkan交换链管理实现
// 负责创建和管理渲染交换链，实现双缓冲或三缓冲渲染
#include "core/vulkan_swapchain.hpp"
#include "core/vulkan_device.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include "utils/vk_result.hpp"
#include <algorithm>
#include <limits>

namespace owengine {

// VulkanSwapchain构造函数
VulkanSwapchain::VulkanSwapchain(const std::shared_ptr<VulkanDevice>& device, GLFWwindow* window)
    : device_(device), window_(window) {
}

// VulkanSwapchain析构函数
VulkanSwapchain::~VulkanSwapchain() {
    cleanup();
}

// 创建交换链
// 设置表面格式、呈现模式和图像范围
void VulkanSwapchain::create() {
    auto swapChainSupport = device_->querySwapChainSupport();

    VkSurfaceFormatKHR surfaceFormat = device_->chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = device_->chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = device_->chooseSwapExtent(swapChainSupport.capabilities, window_);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = device_->getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // 直接使用 VulkanDevice 已存储的队列族索引，避免重复查询
    uint32_t graphicsFamily = device_->getGraphicsQueueFamily();
    uint32_t presentFamily = device_->getPresentQueueFamily();

    // 定义队列族索引数组
    uint32_t queueFamilyIndices[] = {graphicsFamily, presentFamily};

    if (graphicsFamily != presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult _vr = vkCreateSwapchainKHR(device_->getDevice(), &createInfo, nullptr, &swapchain_);
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create swap chain! ") + vkResultToString(_vr));
    }

    vkGetSwapchainImagesKHR(device_->getDevice(), swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_->getDevice(), swapchain_, &imageCount, swapchainImages_.data());

    swapchainImageFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;

    createImageViews();
}

void VulkanSwapchain::createImageViews() {
    swapchainImageViews_.resize(swapchainImages_.size());

    for (size_t i = 0; i < swapchainImages_.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult _vr = vkCreateImageView(device_->getDevice(), &createInfo, nullptr, &swapchainImageViews_[i]);
        if (_vr != VK_SUCCESS) {
            throw std::runtime_error(std::string("failed to create image views! ") + vkResultToString(_vr));
        }
    }
}

void VulkanSwapchain::cleanup() {
    for (auto imageView : swapchainImageViews_) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device_->getDevice(), imageView, nullptr);
        }
    }

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_->getDevice(), swapchain_, nullptr);
    }

    swapchainImageViews_.clear();
    swapchainImages_.clear();
}

void VulkanSwapchain::recreate(GLFWwindow* window) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device_->getDevice());

    cleanup();
    create();
}

} // namespace owengine
