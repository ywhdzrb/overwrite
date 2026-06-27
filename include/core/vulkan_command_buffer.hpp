#pragma once

/**
 * @file vulkan_command_buffer.hpp
 * @brief Vulkan 命令缓冲分配器 — 纯命令池包装，负责命令缓冲的分配与释放
 *
 * 归属模块：core
 * 核心职责：分配和管理一组 VkCommandBuffer（每个交换链图像对应一个）
 * 依赖关系：VulkanDevice
 * 关键设计：
 *   - 不涉及命令录制（record），录制由 Renderer::drawFrame() 直接管理
 *   - 不存储 renderPass/pipeline/pipelineLayout，这些在录制现场动态绑定
 *   - 命令池始终从 VulkanDevice 实时获取，避免重建后句柄陈旧
 */

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace owengine {

class VulkanDevice;

/**
 * @brief Vulkan 命令缓冲分配器
 * @note 仅负责命令缓冲的创建/清理/访问，不涉及录制逻辑
 */
class VulkanCommandBuffer {
public:
    explicit VulkanCommandBuffer(std::shared_ptr<VulkanDevice> device);
    ~VulkanCommandBuffer();

    // 禁止拷贝
    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;

    /** @brief 分配 imageCount 个命令缓冲 */
    void create(size_t imageCount);

    /** @brief 释放所有命令缓冲 */
    void cleanup();

    /** @brief 获取命令缓冲数组（只读） */
    [[nodiscard]] const std::vector<VkCommandBuffer>& getCommandBuffers() const noexcept { return commandBuffers_; }

private:
    std::shared_ptr<VulkanDevice> device_;

    /** @brief 命令缓冲数组，每个交换链图像对应一个 */
    std::vector<VkCommandBuffer> commandBuffers_;
};

} // namespace owengine
