// Vulkan同步对象管理实现
// 负责创建和管理信号量和栅栏，用于帧同步
#include "vulkan_sync.h"
#include "vulkan_device.h"
#include <stdexcept>

namespace vgame {

// VulkanSync构造函数
VulkanSync::VulkanSync(std::shared_ptr<VulkanDevice> device)
    : device(device) {
}

// VulkanSync析构函数
VulkanSync::~VulkanSync() {
    cleanup();
}

// 创建同步对象
// 为每帧创建信号量和栅栏
void VulkanSync::create(size_t maxFramesInFlight) {
    this->maxFramesInFlight = maxFramesInFlight;
    
    imageAvailableSemaphores.resize(maxFramesInFlight);
    renderFinishedSemaphores.resize(maxFramesInFlight);
    inFlightFences.resize(maxFramesInFlight);
    
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        if (vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device->getDevice(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void VulkanSync::cleanup() {
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        if (imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device->getDevice(), imageAvailableSemaphores[i], nullptr);
        }
        if (renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device->getDevice(), renderFinishedSemaphores[i], nullptr);
        }
        if (inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device->getDevice(), inFlightFences[i], nullptr);
        }
    }
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
}

} // namespace vgame