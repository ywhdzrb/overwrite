#pragma once

// 标准库
#include <memory>
#include <string>
#include <vector>

// 第三方库
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// 项目内部
#include "ecs/entity_factory.hpp"

namespace owengine::ecs {

/**
 * @brief 游戏实体高层接口
 * @note 将 ECS 实体与渲染/逻辑绑定，提供给 GameSession 使用。
 *
 * 每个游戏对象（玩家、NPC、建筑、灯光等）对应一个 IGameEntity，
 * 它封装了底层的 entt::entity 和关联的渲染模型。
 */
class IGameEntity {
public:
    virtual ~IGameEntity() = default;

    /** @brief 获取底层 ECS 句柄 */
    [[nodiscard]] virtual IEntityHandle& handle() = 0;
    [[nodiscard]] virtual const IEntityHandle& handle() const = 0;

    // --- 基础属性 ---
    [[nodiscard]] virtual std::string getName() const = 0;
    virtual void setName(const std::string& name) = 0;

    [[nodiscard]] virtual glm::vec3 getPosition() const = 0;
    virtual void setPosition(const glm::vec3& pos) = 0;

    [[nodiscard]] virtual glm::quat getRotation() const = 0;
    virtual void setRotation(const glm::quat& rot) = 0;

    [[nodiscard]] virtual glm::vec3 getScale() const = 0;
    virtual void setScale(const glm::vec3& scale) = 0;

    [[nodiscard]] virtual EntityType getEntityType() const = 0;

    [[nodiscard]] virtual bool isValid() const = 0;

    /** @brief 销毁实体并从场景中移除 */
    virtual void destroy() = 0;
};

/**
 * @brief 默认游戏实体实现
 */
class GameEntity : public IGameEntity {
public:
    GameEntity(std::unique_ptr<IEntityHandle> handle, const std::string& name = "")
        : handle_(std::move(handle)), name_(name) {}

    IEntityHandle& handle() override { return *handle_; }
    const IEntityHandle& handle() const override { return *handle_; }

    std::string getName() const override { return name_; }
    void setName(const std::string& name) override { name_ = name; }

    glm::vec3 getPosition() const override {
        if (auto* t = handle_->get<TransformComponent>())
            return t->position;
        return glm::vec3(0.0f);
    }

    void setPosition(const glm::vec3& pos) override {
        if (auto* t = handle_->get<TransformComponent>())
            t->position = pos;
    }

    glm::quat getRotation() const override {
        if (auto* t = handle_->get<TransformComponent>())
            return t->rotation;
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    void setRotation(const glm::quat& rot) override {
        if (auto* t = handle_->get<TransformComponent>())
            t->rotation = rot;
    }

    glm::vec3 getScale() const override {
        if (auto* t = handle_->get<TransformComponent>())
            return t->scale;
        return glm::vec3(1.0f);
    }

    void setScale(const glm::vec3& scale) override {
        if (auto* t = handle_->get<TransformComponent>())
            t->scale = scale;
    }

    EntityType getEntityType() const override {
        if (auto* e = handle_->get<EntityTypeComponent>())
            return e->type;
        return EntityType::Unknown;
    }

    bool isValid() const override { return handle_ && handle_->valid(); }

    void destroy() override {
        if (handle_) handle_->destroy();
    }

private:
    std::unique_ptr<IEntityHandle> handle_;
    std::string name_;
};

// ============================================================
// 实体查询辅助（基于类型/标签过滤）
// ============================================================

/**
 * @brief 按类型查询实体
 * @return 满足该类型的实体句柄列表
 */
inline std::vector<std::unique_ptr<IGameEntity>> queryEntitiesByType(
    entt::registry& registry, EntityType type) {
    std::vector<std::unique_ptr<IGameEntity>> result;
    auto view = registry.view<EntityTypeComponent, TransformComponent>();
    for (auto entity : view) {
        if (view.get<EntityTypeComponent>(entity).type == type) {
            auto handle = std::make_unique<EntityHandle>(&registry, entity);
            result.push_back(std::make_unique<GameEntity>(std::move(handle)));
        }
    }
    return result;
}

/**
 * @brief 按名称查询实体
 */
inline std::unique_ptr<IGameEntity> queryEntityByName(
    entt::registry& registry, const std::string& name) {
    auto view = registry.view<NameComponent>();
    for (auto entity : view) {
        if (view.get<NameComponent>(entity).name == name) {
            auto handle = std::make_unique<EntityHandle>(&registry, entity);
            return std::make_unique<GameEntity>(std::move(handle), name);
        }
    }
    return nullptr;
}

} // namespace owengine::ecs
