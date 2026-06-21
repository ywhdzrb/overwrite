// Vulkan同步对象实现
#include "core/vulkan_sync.hpp"
#include "core/vulkan_device.hpp"
#include <stdexcept>

namespace owengine {

VulkanSync::VulkanSync(std::shared_ptr<VulkanDevice> device)
    : device_(device) {
}

VulkanSync::~VulkanSync() {
    cleanup();
}

void VulkanSync::create(size_t maxFramesInFlight) {
    maxFramesInFlight_ = maxFramesInFlight;
    imageAvailableSemaphores_.resize(maxFramesInFlight_);
    renderFinishedSemaphores_.resize(maxFramesInFlight_);
    inFlightFences_.resize(maxFramesInFlight_);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < maxFramesInFlight_; i++) {
        if (vkCreateSemaphore(device_->getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_->getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_->getDevice(), &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void VulkanSync::cleanup() {
    for (size_t i = 0; i < maxFramesInFlight_; i++) {
        if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_->getDevice(), imageAvailableSemaphores_[i], nullptr);
        }
        if (renderFinishedSemaphores_[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_->getDevice(), renderFinishedSemaphores_[i], nullptr);
        }
        if (inFlightFences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device_->getDevice(), inFlightFences_[i], nullptr);
        }
    }
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();
    inFlightFences_.clear();
    maxFramesInFlight_ = 0;
}

} // namespace owengine
