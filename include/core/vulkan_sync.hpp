#pragma once

/**
 * @file vulkan_sync.hpp
 * @brief Vulkan 同步原语管理 — 信号量/栅栏的创建与生命周期
 *
 * 归属模块：core
 * 核心职责：管理每帧的图像可用信号量、渲染完成信号量、进行中栅栏
 * 依赖关系：VulkanDevice
 * 关键设计：双缓冲同步，每帧独立 fence 防止 GPU 与 CPU 不同步
 */

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace owengine {

class VulkanDevice;

class VulkanSync {
public:
    VulkanSync(std::shared_ptr<VulkanDevice> device);
    ~VulkanSync();

    // 禁止拷贝
    VulkanSync(const VulkanSync&) = delete;
    VulkanSync& operator=(const VulkanSync&) = delete;

    void create(size_t maxFramesInFlight);
    void cleanup();
    
    [[nodiscard]] const std::vector<VkSemaphore>& getImageAvailableSemaphores() const noexcept { return imageAvailableSemaphores_; }
    [[nodiscard]] const std::vector<VkSemaphore>& getRenderFinishedSemaphores() const noexcept { return renderFinishedSemaphores_; }
    [[nodiscard]] const std::vector<VkFence>& getInFlightFences() const noexcept { return inFlightFences_; }
    [[nodiscard]] size_t getMaxFramesInFlight() const noexcept { return maxFramesInFlight_; }

private:
    std::shared_ptr<VulkanDevice> device_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    size_t maxFramesInFlight_;
};

} // namespace owengine
