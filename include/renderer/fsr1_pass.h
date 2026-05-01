#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include "core/vulkan_device.h"

namespace owengine {

/** @brief FSR1 EASU 上采样管线 */
class Fsr1Pass {
public:
    Fsr1Pass(std::shared_ptr<VulkanDevice> device, VkFormat colorFormat, VkExtent2D outputExtent, float scale = 0.67f);
    ~Fsr1Pass();
    Fsr1Pass(const Fsr1Pass&) = delete;
    Fsr1Pass& operator=(const Fsr1Pass&) = delete;

    void init();
    /** @brief 在 command buffer 外更新输出 storage image 描述符 */
    void updateOutputDescriptor(VkImageView swapchainImgView);
    /** @brief 从低分辨率 colorView 上采样到 swapchain（描述符需提前更新） */
    void dispatch(VkCommandBuffer cmd);
    /** @brief 帧推进（轮流使用描述符集避免 GPU/CPU 竞态） */
    void advanceFrame() { currentFrameIndex_ = (currentFrameIndex_ + 1) % MAX_FRAMES; }

    VkImageView getColorImageView() const { return colorView_; }
    VkExtent2D getRenderExtent() const { return renderExtent_; }
    VkExtent2D getOutputExtent() const { return outputExtent_; }
    void cleanup();

private:
    void createRenderTarget();
    void createEasuPipeline();
    void createDescriptorResources();

    std::shared_ptr<VulkanDevice> device_;
    VkExtent2D outputExtent_{}, renderExtent_{};
    float scale_ = 0.67f;
    VkFormat colorFormat_ = VK_FORMAT_R8G8B8A8_UNORM;

    // 低分辨率渲染目标
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;

    // EASU 管线
    VkPipelineLayout easuLayout_ = VK_NULL_HANDLE;
    VkPipeline easuPipeline_ = VK_NULL_HANDLE;

    // 描述符资源（双缓冲 per-frame，避免 GPU/CPU 竞态）
    static constexpr int MAX_FRAMES = 2;
    VkDescriptorSetLayout descSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descSets_[MAX_FRAMES] = {};  // 每个 in-flight frame 独立
    int currentFrameIndex_ = 0;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkBuffer constBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory constBufferMemory_ = VK_NULL_HANDLE;
    uint32_t constData_[16] = {};

    bool initialized_ = false;
};

} // namespace owengine
