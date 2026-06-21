#pragma once

/**
 * @file shader_manager.hpp
 * @brief 着色器模块管理器 — 集中式 SPIR-V 加载与缓存
 *
 * 归属模块：renderer
 * 核心职责：统一管理 VkShaderModule 的创建、缓存、生命周期
 * 依赖关系：VulkanDevice
 * 关键设计：按文件路径去重缓存，所有子系统通过本类加载着色器
 */

// 标准库
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 第三方库
#include <vulkan/vulkan.h>

namespace owengine {

// 前向声明
class VulkanDevice;

/**
 * @brief 着色器模块管理器
 * @note 线程安全：非线程安全，需在渲染主线程使用。
 *       缓存策略：同路径模块复用，在 cleanup() 时统一销毁。
 *
 * 设计目的：
 * - 消除各子系统重复的 readFile + createShaderModule 样板代码
 * - 模块级缓存：同一着色器被多个管线使用时只需加载一次
 * - 统一路径管理：确保所有子系统使用 AssetPaths 常量
 */
class ShaderManager {
public:
    explicit ShaderManager(std::shared_ptr<VulkanDevice> device);
    ~ShaderManager();

    // 禁止拷贝
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    // 允许移动
    ShaderManager(ShaderManager&&) noexcept = default;
    ShaderManager& operator=(ShaderManager&&) noexcept = default;

    /**
     * @brief 获取或加载指定路径的着色器模块
     * @param path SPIR-V 文件路径（如 "shaders/cloud.frag.spv"）
     * @return VkShaderModule 缓存的着色器模块
     * @note 首次调用会读取文件并创建模块，后续返回缓存实例
     */
    [[nodiscard]] VkShaderModule getModule(const std::string& path);

    /**
     * @brief 获取着色器阶段信息（便捷方法）
     * @param path SPIR-V 文件路径
     * @param stage 着色器阶段（VK_SHADER_STAGE_VERTEX_BIT 等）
     * @return VkPipelineShaderStageCreateInfo 可直接用于管线创建
     */
    [[nodiscard]] VkPipelineShaderStageCreateInfo getStageInfo(
        const std::string& path, VkShaderStageFlagBits stage);

    /**
     * @brief 检查指定路径的着色器是否已缓存
     */
    [[nodiscard]] bool hasModule(const std::string& path) const;

    /**
     * @brief 卸载指定着色器模块
     */
    void unloadModule(const std::string& path);

    /**
     * @brief 清空所有缓存的着色器模块
     * @note 在 Vulkan 设备销毁前调用
     */
    void cleanup();

private:
    std::shared_ptr<VulkanDevice> device_;
    std::unordered_map<std::string, VkShaderModule> cache_;
};

} // namespace owengine
