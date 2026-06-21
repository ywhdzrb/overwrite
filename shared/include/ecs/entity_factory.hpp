#pragma once

// 标准库
#include <string>
#include <memory>
#include <functional>
#include <optional>

// 第三方库
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

// 项目内部
#include "ecs/components.hpp"

namespace owengine::ecs {

// ============================================================
// 实体创建配置结构体
// ============================================================

/**
 * @brief 实体基础配置
 * @note 各字段均为可选，使用 std::optional 避免默认值歧义
 */
struct EntityConfig {
    std::optional<glm::vec3> position;
    std::optional<glm::quat> rotation;
    std::optional<glm::vec3> scale;
    std::optional<std::string> name;
    std::optional<std::string> modelPath;
    bool networkSync = false;
    bool hasPhysics = false;
    bool hasCollider = false;
    float colliderRadius = 0.5f;
    float colliderHeight = 1.8f;
    float gravity = 15.0f;
    float jumpForce = 5.5f;
};

/**
 * @brief 灯光配置
 */
struct LightConfig {
    enum class Type { Directional, Point, Spot };

    Type type = Type::Point;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;

    // 方向光/聚光灯方向
    glm::vec3 direction{0.0f, -1.0f, 0.0f};

    // 衰减参数（点光源/聚光灯）
    float constant  = 1.0f;
    float linear    = 0.09f;
    float quadratic = 0.032f;

    // 聚光灯角度（度数）
    float innerCutoff = 12.5f;
    float outerCutoff = 17.5f;

    bool enabled = true;
    bool castShadows = false;
};

/**
 * @brief 实体类型枚举（扩展自 EntityType，增加更多类型）
 */
enum class EntityArchetype {
    Empty,
    Player,
    NPC,
    Building,
    Item,
    Projectile,
    Light,
    Plane,
    Trigger,
    Decoration
};

// ============================================================
// IEntityHandle — 实体通用句柄接口
// ============================================================

/**
 * @brief 实体句柄抽象接口
 * @note 允许不直接依赖 enTT 类型的情况下操作实体
 */
class IEntityHandle {
public:
    virtual ~IEntityHandle() = default;

    [[nodiscard]] virtual entt::entity getEntity() const noexcept = 0;
    [[nodiscard]] virtual entt::registry* getRegistry() const noexcept = 0;
    [[nodiscard]] virtual bool valid() const noexcept = 0;
    virtual void destroy() = 0;

    // 组件访问
    template<typename T>
    [[nodiscard]] T* get() const noexcept {
        if (!valid()) return nullptr;
        return getRegistry()->try_get<T>(getEntity());
    }

    template<typename T>
    [[nodiscard]] T& emplace() {
        return getRegistry()->emplace<T>(getEntity());
    }

    template<typename T, typename... Args>
    [[nodiscard]] T& emplace(Args&&... args) {
        return getRegistry()->emplace<T>(getEntity(), std::forward<Args>(args)...);
    }

    template<typename T>
    [[nodiscard]] bool has() const noexcept {
        return valid() && getRegistry()->all_of<T>(getEntity());
    }

    template<typename T>
    void remove() {
        if (valid()) getRegistry()->remove<T>(getEntity());
    }
};

/**
 * @brief 默认实体句柄实现
 */
class EntityHandle : public IEntityHandle {
public:
    EntityHandle(entt::registry* registry, entt::entity entity)
        : registry_(registry), entity_(entity) {}

    [[nodiscard]] entt::entity getEntity() const noexcept override { return entity_; }
    [[nodiscard]] entt::registry* getRegistry() const noexcept override { return registry_; }

    [[nodiscard]] bool valid() const noexcept override {
        return registry_ && registry_->valid(entity_);
    }

    void destroy() override {
        if (valid()) {
            registry_->destroy(entity_);
            entity_ = entt::null;
        }
    }

private:
    entt::registry* registry_;
    entt::entity entity_;
};

// ============================================================
// IEntityFactory — 实体工厂抽象接口
// ============================================================

/**
 * @brief 统一实体工厂接口
 * @note 提供所有实体类型的创建方法，派生类可覆盖默认实现
 *       以添加客户端/服务端专属组件
 */
class IEntityFactory {
public:
    virtual ~IEntityFactory() = default;

    /** @brief 创建空实体 */
    virtual std::unique_ptr<IEntityHandle> createEmpty(const EntityConfig& config = {}) = 0;

    /** @brief 创建玩家 */
    virtual std::unique_ptr<IEntityHandle> createPlayer(const EntityConfig& config = {}) = 0;

    /** @brief 创建NPC */
    virtual std::unique_ptr<IEntityHandle> createNPC(const EntityConfig& config = {}) = 0;

    /** @brief 创建建筑（静态网格物体） */
    virtual std::unique_ptr<IEntityHandle> createBuilding(const EntityConfig& config = {}) = 0;

    /** @brief 创建可拾取物品 */
    virtual std::unique_ptr<IEntityHandle> createItem(const EntityConfig& config = {}) = 0;

    /** @brief 创建投射物 */
    virtual std::unique_ptr<IEntityHandle> createProjectile(const EntityConfig& config = {}) = 0;

    /** @brief 创建灯光（仅客户端有效） */
    virtual std::unique_ptr<IEntityHandle> createLight(const LightConfig& config = {}) = 0;

    /** @brief 创建平面（地面/墙壁/触发器区域） */
    virtual std::unique_ptr<IEntityHandle> createPlane(const EntityConfig& config = {}) = 0;

    /** @brief 创建触发器区域 */
    virtual std::unique_ptr<IEntityHandle> createTrigger(const EntityConfig& config = {}) = 0;

    /** @brief 创建装饰物（不参与交互的静态物体） */
    virtual std::unique_ptr<IEntityHandle> createDecoration(const EntityConfig& config = {}) = 0;

    /** @brief 从蓝图创建实体 */
    virtual std::unique_ptr<IEntityHandle> createFromArchetype(
        EntityArchetype archetype, const EntityConfig& config = {}) = 0;

    /** @brief 销毁实体 */
    virtual void destroy(entt::entity entity) = 0;

    /** @brief 获取底层 registry */
    [[nodiscard]] virtual entt::registry* getRegistry() const noexcept = 0;
};

// ============================================================
// EntityBuilder — 流式构建器
// ============================================================

/**
 * @brief 流式实体构建器
 * @note 链式调用装配组件，最后调用 build() 完成创建
 *
 * 用法：
 *   EntityBuilder(registry)
 *       .withPosition({0, 1, 0})
 *       .withName("Guard")
 *       .withModel("assets/models/guard.glb")
 *       .withPhysics()
 *       .build();
 */
class EntityBuilder {
public:
    explicit EntityBuilder(entt::registry& registry);

    EntityBuilder& withPosition(const glm::vec3& pos);
    EntityBuilder& withRotation(const glm::quat& rot);
    EntityBuilder& withScale(const glm::vec3& scale);
    EntityBuilder& withName(const std::string& name);
    EntityBuilder& withEntityType(EntityType type);

    // 物理与碰撞
    EntityBuilder& withPhysics(float gravity = 15.0f, float jumpForce = 5.5f);
    EntityBuilder& withCollider(float radius = 0.5f, float height = 1.8f);

    // 运动与网络
    EntityBuilder& withMovement(float speed = 5.0f, float sprintMultiplier = 2.0f);
    EntityBuilder& withNetwork(bool needsSync = true);

    // 模型（仅客户端）
    EntityBuilder& withModelPath(const std::string& path);

    /** @brief 完成构建并返回句柄 */
    std::unique_ptr<IEntityHandle> build();

private:
    entt::registry& registry_;
    entt::entity entity_{entt::null};
};

// ============================================================
// EntityFactory — 默认工厂实现
// ============================================================

/**
 * @brief 默认实体工厂
 * @note 只操作共享组件（Transform/Velocity/Physics/PlayerTag/Name等）。
 *       客户端需通过 ClientEntityFactory 获得渲染/灯光支持。
 */
class EntityFactory : public IEntityFactory {
public:
    explicit EntityFactory(entt::registry& registry);

    std::unique_ptr<IEntityHandle> createEmpty(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createPlayer(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createNPC(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createBuilding(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createItem(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createProjectile(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createLight(const LightConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createPlane(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createTrigger(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createDecoration(const EntityConfig& config = {}) override;
    std::unique_ptr<IEntityHandle> createFromArchetype(
        EntityArchetype archetype, const EntityConfig& config = {}) override;
    void destroy(entt::entity entity) override;

    [[nodiscard]] entt::registry* getRegistry() const noexcept override { return &registry_; }

protected:
    /** @brief 应用基础配置到实体（位置/旋转/缩放/名称/实体类型） */
    void applyBaseConfig(entt::entity entity, const EntityConfig& config);

    /** @brief 应用物理配置 */
    void applyPhysics(entt::entity entity, const EntityConfig& config);

private:
    entt::registry& registry_;
};

} // namespace owengine::ecs
