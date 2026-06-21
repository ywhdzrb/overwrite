/**
 * @file entity_registry.cpp
 * @brief 集中式实体注册表实现
 */

#include "ecs/entity_registry.hpp"
#include <algorithm>

namespace owengine::ecs {

EntityRegistry::EntityRegistry(entt::registry& ecsRegistry)
    : ecsRegistry_(ecsRegistry) {
    factory_ = std::make_unique<EntityFactory>(ecsRegistry);
}

EntityRegistry::EntityId EntityRegistry::registerEntity(
    std::unique_ptr<IGameEntity> entity) {
    if (!entity || !entity->isValid()) return 0;

    EntityId id = nextId_++;
    entt::entity ee = entity->handle().getEntity();
    entityToId_[ee] = id;
    entities_[id] = std::move(entity);
    return id;
}

bool EntityRegistry::unregister(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return false;

    entt::entity ee = it->second->handle().getEntity();
    entityToId_.erase(ee);
    entities_.erase(it);
    return true;
}

bool EntityRegistry::destroy(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return false;

    entt::entity ee = it->second->handle().getEntity();
    it->second->destroy();
    entityToId_.erase(ee);
    entities_.erase(it);
    return true;
}

IGameEntity* EntityRegistry::get(EntityId id) {
    auto it = entities_.find(id);
    return (it != entities_.end()) ? it->second.get() : nullptr;
}

EntityRegistry::EntityId EntityRegistry::findId(entt::entity entity) const {
    auto it = entityToId_.find(entity);
    return (it != entityToId_.end()) ? it->second : 0;
}

std::vector<EntityRegistry::EntityId> EntityRegistry::queryByType(
    EntityType type) const {
    std::vector<EntityId> result;
    for (const auto& [id, entity] : entities_) {
        if (entity->getEntityType() == type) {
            result.push_back(id);
        }
    }
    return result;
}

EntityRegistry::EntityId EntityRegistry::queryByName(
    const std::string& name) const {
    for (const auto& [id, entity] : entities_) {
        if (entity->getName() == name) {
            return id;
        }
    }
    return 0;
}

void EntityRegistry::clear() {
    entities_.clear();
    entityToId_.clear();
}

} // namespace owengine::ecs
