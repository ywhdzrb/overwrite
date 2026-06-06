#pragma once

/**
 * @file vulkan_render_pass.hpp
 * @brief Vulkan 渲染通道管理 — 渲染附件、子通道依赖、MSAA support
 *
 * 归属模块：core
 * 核心职责：封装 VkRenderPass 生命周期，支持 MSAA/非 MSAA 两种配置
 * 依赖关系：VulkanDevice、VulkanSwapchain（获取图像格式）
 * 关键设计：根据 msaaSamples_ 动态生成附件描述和子通道依赖
 */

// 标准库
#include <memory>

// 第三方库
#include <vulkan/vulkan.h>

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
    
    VkRenderPass getRenderPass() const { return renderPass_; }
    
    void setMsaaSamples(VkSampleCountFlagBits samples) { msaaSamples_ = samples; }

private:
    std::shared_ptr<VulkanDevice> device_;
    VkFormat swapchainImageFormat_;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;  // 默认不使用MSAA
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
};

} // namespace owengine
