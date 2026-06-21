#pragma once

// 标准库
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

// 第三方库
#include <entt/entt.hpp>

// 项目内部
#include "ecs/entity_factory.hpp"
#include "ecs/i_game_entity.hpp"

namespace owengine::ecs {

/**
 * @brief 集中式实体注册表
 * @note 管理场景中所有实体的生命周期，提供按 ID/类型/名称的查询。
 *       同时持有 EntityFactory 实例，统一所有实体的创建入口。
 *
 * 设计目标：
 * - 统一实体创建、销毁、查询
 * - 类型安全的 ID 管理（实体 ID 与 entt::entity 分离）
 * - 支持运行时增删改查
 */
class EntityRegistry {
public:
    /** @brief 实体唯一标识 */
    using EntityId = uint64_t;

    explicit EntityRegistry(entt::registry& ecsRegistry);

    // --- 工厂获取 ---
    IEntityFactory& factory() { return *factory_; }
    const IEntityFactory& factory() const { return *factory_; }

    // --- 注册与管理 ---

    /** @brief 注册一个已存在的实体到注册表 */
    EntityId registerEntity(std::unique_ptr<IGameEntity> entity);

    /** @brief 取消注册（不销毁实体本身） */
    bool unregister(EntityId id);

    /** @brief 取消注册并销毁实体 */
    bool destroy(EntityId id);

    // --- 查询 ---

    /** @brief 通过 ID 查询 */
    IGameEntity* get(EntityId id);

    /** @brief 通过 entt::entity 反向查询 ID */
    EntityId findId(entt::entity entity) const;

    /** @brief 按类型查询 */
    std::vector<EntityId> queryByType(EntityType type) const;

    /** @brief 按名称查询 */
    EntityId queryByName(const std::string& name) const;

    /** @brief 获取所有实体 */
    const std::unordered_map<EntityId, std::unique_ptr<IGameEntity>>& all() const {
        return entities_;
    }

    /** @brief 获取实体数量 */
    size_t count() const { return entities_.size(); }

    // --- 生命周期 ---

    /** @brief 清空所有实体 */
    void clear();

private:
    EntityId nextId_ = 1;
    entt::registry& ecsRegistry_;
    std::unique_ptr<IEntityFactory> factory_;
    std::unordered_map<EntityId, std::unique_ptr<IGameEntity>> entities_;
    std::unordered_map<entt::entity, EntityId> entityToId_;
};

} // namespace owengine::ecs
