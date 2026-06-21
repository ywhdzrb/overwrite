#pragma once

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
