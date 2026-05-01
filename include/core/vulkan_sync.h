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
    
    const std::vector<VkSemaphore>& getImageAvailableSemaphores() const { return imageAvailableSemaphores; }
    const std::vector<VkSemaphore>& getRenderFinishedSemaphores() const { return renderFinishedSemaphores; }
    const std::vector<VkFence>& getInFlightFences() const { return inFlightFences; }
    size_t getMaxFramesInFlight() const { return maxFramesInFlight; }

private:
    std::shared_ptr<VulkanDevice> device;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    size_t maxFramesInFlight;
};

} // namespace owengine
