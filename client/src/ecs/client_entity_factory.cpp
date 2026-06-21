/**
 * @file client_entity_factory.cpp
 * @brief 客户端实体工厂实现
 *
 * 扩展 EntityFactory，为所有实体添加 RenderComponent / LightComponent。
 * 使 GameSession 可以统一管理"有渲染表示的实体"。
 */

#include "ecs/client_entity_factory.hpp"
#include "ecs/client_components.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace owengine::ecs {

ClientEntityFactory::ClientEntityFactory(entt::registry& registry,
                                          const std::string& defaultModelDir)
    : EntityFactory(registry), modelDir_(defaultModelDir) {}

void ClientEntityFactory::applyRenderComponent(entt::entity entity,
                                                const EntityConfig& config) {
    if (!config.modelPath.has_value()) return;

    auto& render = getRegistry()->emplace<RenderComponent>(entity);
    render.modelPath = config.modelPath.value();

    if (config.position.has_value()) render.position = config.position.value();
    if (config.scale.has_value())    render.scale    = config.scale.value();
    if (config.rotation.has_value()) {
        glm::vec3 euler = glm::eulerAngles(config.rotation.value());
        render.rotation = glm::degrees(euler);
    }
    render.dirty = true;
}

// --- 覆盖基类方法 ---

std::unique_ptr<IEntityHandle> ClientEntityFactory::createEmpty(const EntityConfig& config) {
    auto handle = EntityFactory::createEmpty(config);
    if (handle && handle->valid()) {
        applyRenderComponent(handle->getEntity(), config);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createPlayer(const EntityConfig& config) {
    auto handle = EntityFactory::createPlayer(config);
    if (handle && handle->valid()) {
        // 如果未指定玩家模型，不自动附加 RenderComponent
        //（由 GameSession 通过 PlayerModelSystem 加载 GLTF 模型）
        applyRenderComponent(handle->getEntity(), config);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createNPC(const EntityConfig& config) {
    auto handle = EntityFactory::createNPC(config);
    if (handle && handle->valid()) {
        applyRenderComponent(handle->getEntity(), config);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createBuilding(const EntityConfig& config) {
    auto handle = EntityFactory::createBuilding(config);
    if (handle && handle->valid()) {
        applyRenderComponent(handle->getEntity(), config);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createItem(const EntityConfig& config) {
    auto handle = EntityFactory::createItem(config);
    if (handle && handle->valid()) {
        applyRenderComponent(handle->getEntity(), config);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createProjectile(const EntityConfig& config) {
    auto handle = EntityFactory::createProjectile(config);
    if (handle && handle->valid()) {
        applyRenderComponent(handle->getEntity(), config);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createLight(const LightConfig& config) {
    switch (config.type) {
        case LightConfig::Type::Directional: return createDirectionalLight(config);
        case LightConfig::Type::Point:       return createPointLight(config);
        case LightConfig::Type::Spot:        return createSpotLight(config);
        default:                             return createPointLight(config);
    }
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createDirectionalLight(const LightConfig& config) {
    auto entity = getRegistry()->create();
    auto& light = getRegistry()->emplace<LightComponent>(entity);
    light.type = LightComponent::Type::Directional;
    light.color = config.color;
    light.intensity = config.intensity;
    light.direction = config.direction;
    light.enabled = config.enabled;
    light.castShadows = config.castShadows;
    getRegistry()->emplace<TransformComponent>(entity);
    getRegistry()->emplace<EntityTypeComponent>(entity, EntityType::Unknown);
    getRegistry()->emplace<NameComponent>(entity, "DirectionalLight");
    return std::make_unique<EntityHandle>(getRegistry(), entity);
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createPointLight(const LightConfig& config) {
    auto entity = getRegistry()->create();
    auto& light = getRegistry()->emplace<LightComponent>(entity);
    light.type = LightComponent::Type::Point;
    light.color = config.color;
    light.intensity = config.intensity;
    light.constant = config.constant;
    light.linear = config.linear;
    light.quadratic = config.quadratic;
    light.enabled = config.enabled;
    getRegistry()->emplace<TransformComponent>(entity);
    getRegistry()->emplace<EntityTypeComponent>(entity, EntityType::Unknown);
    getRegistry()->emplace<NameComponent>(entity, "PointLight");
    return std::make_unique<EntityHandle>(getRegistry(), entity);
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createSpotLight(const LightConfig& config) {
    auto entity = getRegistry()->create();
    auto& light = getRegistry()->emplace<LightComponent>(entity);
    light.type = LightComponent::Type::Spot;
    light.color = config.color;
    light.intensity = config.intensity;
    light.direction = config.direction;
    light.constant = config.constant;
    light.linear = config.linear;
    light.quadratic = config.quadratic;
    light.innerCutoff = config.innerCutoff;
    light.outerCutoff = config.outerCutoff;
    light.enabled = config.enabled;
    light.castShadows = config.castShadows;
    getRegistry()->emplace<TransformComponent>(entity);
    getRegistry()->emplace<EntityTypeComponent>(entity, EntityType::Unknown);
    getRegistry()->emplace<NameComponent>(entity, "SpotLight");
    return std::make_unique<EntityHandle>(getRegistry(), entity);
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createPlane(const EntityConfig& config) {
    // 创建一个带有默认平面模型的实体
    EntityConfig planeConfig = config;
    if (!planeConfig.modelPath.has_value()) {
        planeConfig.modelPath = modelDir_ + "plane.glb";
    }
    auto handle = EntityFactory::createPlane(planeConfig);
    if (handle && handle->valid()) {
        applyRenderComponent(handle->getEntity(), planeConfig);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createTrigger(const EntityConfig& config) {
    EntityConfig triggerConfig = config;
    if (!triggerConfig.modelPath.has_value()) {
        triggerConfig.modelPath = modelDir_ + "trigger_volume.glb";
    }
    auto handle = EntityFactory::createTrigger(triggerConfig);
    if (handle && handle->valid()) {
        applyRenderComponent(handle->getEntity(), triggerConfig);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createDecoration(const EntityConfig& config) {
    auto handle = EntityFactory::createDecoration(config);
    if (handle && handle->valid()) {
        applyRenderComponent(handle->getEntity(), config);
    }
    return handle;
}

std::unique_ptr<IEntityHandle> ClientEntityFactory::createFromArchetype(
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

std::vector<std::unique_ptr<IEntityHandle>>
ClientEntityFactory::createFromSceneConfig(const std::string& jsonPath) {
    std::vector<std::unique_ptr<IEntityHandle>> entities;
    std::ifstream file(jsonPath);
    if (!file.is_open()) return entities;

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("models") && j["models"].is_array()) {
            const auto& models = j["models"];
            for (const auto& m : models) {
                EntityConfig cfg;
                std::string type = m.value("type", "");
                if (m.contains("position")) {
                    cfg.position = glm::vec3(
                        m["position"][0].get<float>(),
                        m["position"][1].get<float>(),
                        m["position"][2].get<float>()
                    );
                }
                if (m.contains("scale")) {
                    cfg.scale = glm::vec3(
                        m["scale"][0].get<float>(),
                        m["scale"][1].get<float>(),
                        m["scale"][2].get<float>()
                    );
                }
                cfg.name = m.value("id", "");
                cfg.modelPath = m.value("file", "");

                // 根据类型或 modelPath 自动选择合适的蓝图
                if (type == "player") {
                    entities.push_back(createPlayer(cfg));
                } else if (type == "building" || cfg.modelPath.has_value()) {
                    entities.push_back(createBuilding(cfg));
                } else {
                    entities.push_back(createDecoration(cfg));
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        // 静默处理：scene.json 中没有可解析的模型条目
    }

    return entities;
}

} // namespace owengine::ecs
