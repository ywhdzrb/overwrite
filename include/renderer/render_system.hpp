#pragma once

/**
 * @file render_system.hpp
 * @brief ECS 驱动渲染系统 — 桥接 ECS RenderComponent 与 Vulkan GLTFModel 渲染
 *
 * 归属模块：renderer
 * 核心职责：自动遍历 ECS 注册表中持有 RenderComponent 的实体，
 *          管理对应的 GLTFModel 加载/缓存/同步/渲染
 * 依赖关系：entt::registry、ModelCache、GLTFModel、RenderComponent
 * 关键设计：按帧扫描 ECS 差异，延迟加载模型，变换自动同步
 */

// 标准库
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

// 第三方库
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

// 项目内部
#include "ecs/components.hpp"
#include "ecs/client_components.hpp"

namespace owengine {

// 前向声明
class ModelCache;
class GLTFModel;

/**
 * @brief ECS 驱动渲染系统
 * @note 线程安全：非线程安全，需在渲染主线程使用。
 *       生命周期：在 GameSession 初始化后创建，帧循环中每次 update + render，
 *       在 GameSession 清理前销毁。
 *
 * 工作流程：
 *   update() — 每帧扫描 ECS registry，对比上次状态：
 *     1. 新增实体 → 通过 ModelCache 延迟加载 GLTFModel
 *     2. 移除实体 → 标记移除，下一帧清理
 *     3. 变换变脏 → 同步 TransformComponent → GLTFModel 位置/旋转/缩放
 *   getRenderEntries() — 供 Renderer 获取可见实体的渲染列表
 *
 * 设计约束：
 *   - 不持有 Vulkan 管线绑定逻辑（由 Renderer 处理 pipeline/descriptor binding）
 *   - 不负责 ImGui 调试面板（由 Renderer 的 ImGui 面板处理）
 */
class RenderSystem {
public:
    /**
     * @brief 每条渲染条目
     */
    struct RenderEntry {
        GLTFModel* model = nullptr;            // 待渲染的 GLTFModel
        glm::mat4 modelMatrix{1.0f};            // 已同步的模型矩阵
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;  // 纹理描述符集
        bool visible = true;                    // 可见性
    };

    RenderSystem(entt::registry& registry,
                 std::shared_ptr<ModelCache> modelCache,
                 VkDescriptorSetLayout textureDSLayout,
                 VkDescriptorPool descriptorPool);

    ~RenderSystem();

    // 禁止拷贝
    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;

    // 允许移动
    RenderSystem(RenderSystem&&) noexcept = default;
    RenderSystem& operator=(RenderSystem&&) noexcept = default;

    // ========== 生命周期 ==========

    /**
     * @brief 每帧更新：扫描 ECS 差异，同步变换
     * @param deltaTime 帧间隔时间（秒）
     * @note 必须在 Renderer::drawFrame() 录制命令缓冲之前调用
     */
    void update(float deltaTime);

    /**
     * @brief 清理所有资源
     * @note ECS 注册表销毁前调用
     */
    void cleanup();

    // ========== 渲染数据查询 ==========

    /**
     * @brief 获取所有可见实体的渲染条目
     * @return 常量引用到内部渲染条目列表，每帧由 update() 重建
     * @note 返回的指针仅在下次 update() 前有效
     */
    [[nodiscard]] const std::vector<RenderEntry>& getRenderEntries() const;

    // ========== 实体管理 ==========

    /**
     * @brief 设置实体可见性
     * @param entity ECS 实体句柄
     * @param visible 是否可见
     */
    void setEntityVisible(entt::entity entity, bool visible);

    /**
     * @brief 查询实体是否可见
     * @param entity ECS 实体句柄
     * @return true 如果实体被系统跟踪且可见
     */
    [[nodiscard]] bool isEntityVisible(entt::entity entity) const;

    /**
     * @brief 检查实体是否被本系统跟踪
     * @param entity ECS 实体句柄
     */
    [[nodiscard]] bool hasEntity(entt::entity entity) const;

    /**
     * @brief 获取实体对应的模型指针
     * @param entity ECS 实体句柄
     * @return GLTFModel* 如果实体被跟踪且有加载的模型，否则 nullptr
     */
    [[nodiscard]] GLTFModel* getModelForEntity(entt::entity entity) const;

    /**
     * @brief 强制重新同步单个实体（模型变换等）
     * @param entity ECS 实体句柄
     */
    void syncEntity(entt::entity entity);

    /** @brief 当前跟踪的实体数量 */
    [[nodiscard]] size_t getTrackedCount() const { return entityModels_.size(); }

    // ========== 调试 ==========

    /** @brief 获取上次同步中新增的实体数 */
    int getLastFrameAdded() const { return lastFrameAdded_; }

    /** @brief 获取上次同步中移除的实体数 */
    int getLastFrameRemoved() const { return lastFrameRemoved_; }

private:
    /**
     * @brief 内部跟踪的已加载模型信息
     */
    struct LoadedEntity {
        std::string modelPath;                       // 模型路径
        GLTFModel* model = nullptr;                  // 缓存的模型指针
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE; // 模型纹理描述符集
        bool visible = true;                         // 可见性
    };

    /**
     * @brief 为单个实体加载模型
     * @param entity ECS 实体
     * @param renderComp 实体的 RenderComponent
     */
    void loadModelForEntity(entt::entity entity, const ecs::RenderComponent& renderComp);

    /**
     * @brief 更新实体变换到 GLTFModel
     * @param entity ECS 实体
     * @param loaded 已加载的模型条目
     */
    void syncTransform(entt::entity entity, LoadedEntity& loaded);

    /**
     * @brief 移除实体跟踪
     * @param entity ECS 实体
     */
    void removeEntity(entt::entity entity);

    // ECS 注册表引用
    entt::registry& registry_;

    // 模型缓存
    std::shared_ptr<ModelCache> modelCache_;

    // 描述符资源
    VkDescriptorSetLayout textureDSLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    // ECS 实体 → 已加载模型映射
    std::unordered_map<entt::entity, LoadedEntity> entityModels_;

    // 渲染条目缓存（每帧由 update() 重建）
    mutable std::vector<RenderEntry> renderEntries_;
    mutable bool entriesDirty_ = true;

    // 统计
    int lastFrameAdded_ = 0;
    int lastFrameRemoved_ = 0;

    // 上次已知的实体集合（用于检测移除）
    std::vector<entt::entity> knownEntities_;
};

} // namespace owengine
