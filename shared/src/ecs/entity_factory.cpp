/**
 * @file entity_factory.cpp
 * @brief 统一实体工厂实现
 *
 * 提供默认的实体创建逻辑，仅使用 shared 层组件。
 * 客户端/服务端可通过继承扩展专属组件。
 */

#include "ecs/entity_factory.hpp"

namespace owengine::ecs {

// ============================================================
// EntityBuilder 实现
// ============================================================

EntityBuilder::EntityBuilder(entt::registry& registry)
    : registry_(registry) {
    entity_ = registry_.create();
}

EntityBuilder& EntityBuilder::withPosition(const glm::vec3& pos) {
    auto& t = registry_.emplace<TransformComponent>(entity_);
    t.position = pos;
    return *this;
}

EntityBuilder& EntityBuilder::withRotation(const glm::quat& rot) {
    auto& t = registry_.emplace<TransformComponent>(entity_);
    t.rotation = rot;
    return *this;
}

EntityBuilder& EntityBuilder::withScale(const glm::vec3& scale) {
    auto& t = registry_.emplace<TransformComponent>(entity_);
    t.scale = scale;
    return *this;
}

EntityBuilder& EntityBuilder::withName(const std::string& name) {
    registry_.emplace<NameComponent>(entity_, name);
    return *this;
}

EntityBuilder& EntityBuilder::withEntityType(EntityType type) {
    registry_.emplace<EntityTypeComponent>(entity_, type);
    return *this;
}

EntityBuilder& EntityBuilder::withPhysics(float gravity, float jumpForce) {
    auto& p = registry_.emplace<PhysicsComponent>(entity_);
    p.gravity = gravity;
    p.jumpForce = jumpForce;
    registry_.emplace<VelocityComponent>(entity_);
    return *this;
}

EntityBuilder& EntityBuilder::withCollider(float radius, float height) {
    auto& p = registry_.emplace<PhysicsComponent>(entity_);
    p.colliderRadius = radius;
    p.colliderHeight = height;
    return *this;
}

EntityBuilder& EntityBuilder::withMovement(float speed, float sprintMultiplier) {
    auto& m = registry_.emplace<MovementControllerComponent>(entity_);
    m.movementSpeed = speed;
    m.sprintMultiplier = sprintMultiplier;
    return *this;
}

EntityBuilder& EntityBuilder::withNetwork(bool needsSync) {
    auto& n = registry_.emplace<NetworkSyncComponent>(entity_);
    n.needsSync = needsSync;
    return *this;
}

EntityBuilder& EntityBuilder::withModelPath(const std::string& path) {
    // 模型路径存储在 NameComponent 的可选字段中
    // 客户端层会读取并创建 RenderComponent
    if (!registry_.all_of<NameComponent>(entity_)) {
        registry_.emplace<NameComponent>(entity_, "entity");
    }
    return *this;
}

std::unique_ptr<IEntityHandle> EntityBuilder::build() {
    if (!registry_.valid(entity_)) return nullptr;
    return std::make_unique<EntityHandle>(&registry_, entity_);
}

// ============================================================
// EntityFactory 实现
// ============================================================

EntityFactory::EntityFactory(entt::registry& registry)
    : registry_(registry) {}

void EntityFactory::applyBaseConfig(entt::entity entity, const EntityConfig& config) {
    auto& t = registry_.emplace<TransformComponent>(entity);
    if (config.position.has_value())   t.position = config.position.value();
    if (config.rotation.has_value())   t.rotation = config.rotation.value();
    if (config.scale.has_value())      t.scale    = config.scale.value();
    if (config.name.has_value()) {
        registry_.emplace<NameComponent>(entity, config.name.value());
    }
}

void EntityFactory::applyPhysics(entt::entity entity, const EntityConfig& config) {
    if (!config.hasPhysics) return;
    auto& p = registry_.emplace<PhysicsComponent>(entity);
    p.gravity = config.gravity;
    p.jumpForce = config.jumpForce;
    if (config.hasCollider) {
        p.colliderRadius = config.colliderRadius;
        p.colliderHeight = config.colliderHeight;
    }
    registry_.emplace<VelocityComponent>(entity);
}

// --- 各种实体类型的创建 ---

std::unique_ptr<IEntityHandle> EntityFactory::createEmpty(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    if (config.networkSync) {
        registry_.emplace<NetworkSyncComponent>(entity);
    }
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Unknown);
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createPlayer(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    registry_.emplace<VelocityComponent>(entity);
    registry_.emplace<MovementControllerComponent>(entity);
    applyPhysics(entity, config);
    registry_.emplace<InputStateComponent>(entity);
    registry_.emplace<PlayerTag>(entity);
    if (!config.name.has_value()) {
        registry_.emplace<NameComponent>(entity, "Player");
    }
    registry_.emplace<NetworkSyncComponent>(entity);
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Player);
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createNPC(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    registry_.emplace<VelocityComponent>(entity);
    registry_.emplace<MovementControllerComponent>(entity);
    applyPhysics(entity, config);
    registry_.emplace<EntityTypeComponent>(entity, EntityType::NPC);
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createBuilding(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Building);
    // 建筑是静态物体，无 physics/velocity
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createItem(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    applyPhysics(entity, config);
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Item);
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createProjectile(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    registry_.emplace<VelocityComponent>(entity);
    applyPhysics(entity, config);
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Projectile);
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createLight(const LightConfig&) {
    // 默认工厂：灯光需要客户端 RenderComponent
    // 返回一个标记实体，客户端工厂会覆盖此方法
    auto entity = registry_.create();
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Unknown);
    registry_.emplace<NameComponent>(entity, "Light");
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createPlane(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Building);
    if (config.hasPhysics) {
        // 平面作为碰撞体（如地板/墙壁）
        applyPhysics(entity, config);
    }
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createTrigger(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Building);
    // 触发器不需要物理，但需要碰撞区域
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createDecoration(const EntityConfig& config) {
    auto entity = registry_.create();
    applyBaseConfig(entity, config);
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Unknown);
    return std::make_unique<EntityHandle>(&registry_, entity);
}

std::unique_ptr<IEntityHandle> EntityFactory::createFromArchetype(
    EntityArchetype archetype, const EntityConfig& config) {
    switch (archetype) {
        case EntityArchetype::Empty:       return createEmpty(config);
        case EntityArchetype::Player:      return createPlayer(config);
        case EntityArchetype::NPC:         return createNPC(config);
        case EntityArchetype::Building:    return createBuilding(config);
        case EntityArchetype::Item:        return createItem(config);
        case EntityArchetype::Projectile:  return createProjectile(config);
        case EntityArchetype::Light:       return createLight(LightConfig{});
        case EntityArchetype::Plane:       return createPlane(config);
        case EntityArchetype::Trigger:     return createTrigger(config);
        case EntityArchetype::Decoration:  return createDecoration(config);
        default:                           return createEmpty(config);
    }
}

void EntityFactory::destroy(entt::entity entity) {
    if (registry_.valid(entity)) {
        registry_.destroy(entity);
    }
}

} // namespace owengine::ecs
