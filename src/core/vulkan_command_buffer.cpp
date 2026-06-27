// Vulkan命令缓冲分配器实现
// 负责命令缓冲的分配与释放，不涉及录制逻辑（录制由 Renderer::drawFrame() 直接管理）
#include "core/vulkan_command_buffer.hpp"
#include "core/vulkan_device.hpp"
#include <stdexcept>
#include "utils/vk_result.hpp"

namespace owengine {

// VulkanCommandBuffer构造函数
VulkanCommandBuffer::VulkanCommandBuffer(std::shared_ptr<VulkanDevice> device)
    : device_(device) {
}

// VulkanCommandBuffer析构函数
VulkanCommandBuffer::~VulkanCommandBuffer() {
    cleanup();
}

// 创建命令缓冲
// 为每个交换链图像创建对应的命令缓冲
void VulkanCommandBuffer::create(size_t imageCount) {
    VkCommandPool cmdPool = device_->getCommandPool();
    commandBuffers_.resize(imageCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers_.size();

    VkResult _vr = vkAllocateCommandBuffers(device_->getDevice(), &allocInfo, commandBuffers_.data());
    if (_vr != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to allocate command buffers! ") + vkResultToString(_vr));
    }
}

void VulkanCommandBuffer::cleanup() {
    if (!commandBuffers_.empty()) {
        VkCommandPool cmdPool = device_->getCommandPool();
        if (cmdPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_->getDevice(), cmdPool,
                                 static_cast<uint32_t>(commandBuffers_.size()),
                                 commandBuffers_.data());
        }
    }
    commandBuffers_.clear();
}

} // namespace owengine
