// Vulkan同步对象实现
#include "core/vulkan_sync.hpp"
#include "core/vulkan_device.hpp"
#include <stdexcept>
#include "utils/vk_result.hpp"

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
        VkResult _vr1 = vkCreateSemaphore(device_->getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]);
        if (_vr1 != VK_SUCCESS) {
            throw std::runtime_error(std::string("failed to create image available semaphore! ") + vkResultToString(_vr1));
        }
        VkResult _vr2 = vkCreateSemaphore(device_->getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]);
        if (_vr2 != VK_SUCCESS) {
            throw std::runtime_error(std::string("failed to create render finished semaphore! ") + vkResultToString(_vr2));
        }
        VkResult _vr3 = vkCreateFence(device_->getDevice(), &fenceInfo, nullptr, &inFlightFences_[i]);
        if (_vr3 != VK_SUCCESS) {
            throw std::runtime_error(std::string("failed to create in-flight fence! ") + vkResultToString(_vr3));
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
