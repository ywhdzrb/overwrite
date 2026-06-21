#pragma once

/**
 * @file model_cache.hpp
 * @brief 模型缓存 — 按路径去重管理 GLTFModel 实例
 *
 * 归属模块：renderer
 * 核心职责：GLTFModel 的复用加载与生命周期管理
 * 依赖关系：VulkanDevice、TextureLoader、GLTFModel
 * 关键设计：路径→模型映射，延迟加载，全局共享引用
 */

// 标准库
#include <memory>
#include <string>
#include <unordered_map>

// 第三方库
#include <vulkan/vulkan.h>

// 项目内部
#include "renderer/texture_loader.hpp"
#include "core/vulkan_device.hpp"

namespace owengine {

// 前向声明（避免循环依赖）
class GLTFModel;

/**
 * @brief 模型缓存管理器
 * @note 线程安全：非线程安全，需在渲染主线程使用。
 *       支持路径去重，同一 .glb 文件在全局只保留一个 GLTFModel 实例。
 *
 * 设计目的：
 * - 避免同一模型文件重复加载（如多个 NPC 使用同一模型）
 * - 统一管理模型纹理描述符集创建
 * - 配合 RenderSystem 实现 ECS 驱动渲染
 */
class ModelCache {
public:
    ModelCache(std::shared_ptr<VulkanDevice> device,
               std::shared_ptr<TextureLoader> textureLoader);

    ~ModelCache();

    // 禁止拷贝
    ModelCache(const ModelCache&) = delete;
    ModelCache& operator=(const ModelCache&) = delete;

    // 允许移动
    ModelCache(ModelCache&&) noexcept = default;
    ModelCache& operator=(ModelCache&&) noexcept = default;

    /**
     * @brief 获取或加载指定路径的模型
     * @param path 模型文件路径（相对项目根，如 "assets/models/npc.glb"）
     * @return GLTFModel* 缓存中的模型指针，加载失败返回 nullptr
     * @note 首次调用会执行实际加载，后续返回缓存实例
     */
    [[nodiscard]] GLTFModel* getOrLoadModel(const std::string& path);

    /**
     * @brief 为所有缓存的模型创建网格描述符集
     * @param textureDSLayout 纹理描述符集布局
     * @param pool 描述符池
     * @note 在描述符资源重创建后（如 swapchain 重建）需重新调用
     */
    void createMeshDescriptorSets(VkDescriptorSetLayout textureDSLayout,
                                  VkDescriptorPool pool);

    /**
     * @brief 为单个模型创建全局纹理描述符集
     * @param model 模型指针
     * @param textureDSLayout 纹理描述符集布局
     * @param pool 描述符池
     * @return 描述符集句柄，若模型无纹理则返回 VK_NULL_HANDLE
     */
    [[nodiscard]] VkDescriptorSet createModelDescriptorSet(
        GLTFModel* model,
        VkDescriptorSetLayout textureDSLayout,
        VkDescriptorPool pool);

    /**
     * @brief 移除指定路径的模型
     * @param path 模型文件路径
     * @note 当所有引用都释放后，模型会被销毁
     */
    void unloadModel(const std::string& path);

    /** @brief 清空所有缓存的模型 */
    void clear();

    /** @brief 缓存中的模型数量 */
    [[nodiscard]] size_t count() const { return cache_.size(); }

    /** @brief 检查指定路径是否已缓存 */
    [[nodiscard]] bool hasModel(const std::string& path) const {
        return cache_.find(path) != cache_.end();
    }

private:
    std::shared_ptr<VulkanDevice> device_;
    std::shared_ptr<TextureLoader> textureLoader_;
    std::unordered_map<std::string, std::unique_ptr<class GLTFModel>> cache_;
};

} // namespace owengine
