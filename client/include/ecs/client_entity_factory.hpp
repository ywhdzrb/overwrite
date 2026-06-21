#pragma once

// 标准库
#include <memory>
#include <string>

// 第三方库
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// 项目内部
#include "ecs/entity_factory.hpp"

namespace owengine::ecs {

/**
 * @brief 客户端实体工厂
 * @note 继承 EntityFactory 并在所有创建方法中添加 RenderComponent / LightComponent
 *       适用于客户端渲染场景。
 *
 * 与 GameSession / Renderer 配合使用，确保创建的实体附带渲染所需的组件。
 */
class ClientEntityFactory : public EntityFactory {
public:
    ClientEntityFactory(entt::registry& registry,
                        const std::string& defaultModelDir = "assets/models/");

    // 覆盖基类方法以添加渲染组件
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

    /** @brief 从场景配置 JSON 批量创建实体 */
    std::vector<std::unique_ptr<IEntityHandle>> createFromSceneConfig(
        const std::string& jsonPath);

    /** @brief 设置默认模型目录 */
    void setModelDir(const std::string& dir) { modelDir_ = dir; }

private:
    /** @brief 如果 config 包含 modelPath，添加 RenderComponent */
    void applyRenderComponent(entt::entity entity, const EntityConfig& config);

    /** @brief 创建专用灯光实体 */
    std::unique_ptr<IEntityHandle> createDirectionalLight(const LightConfig& config);
    std::unique_ptr<IEntityHandle> createPointLight(const LightConfig& config);
    std::unique_ptr<IEntityHandle> createSpotLight(const LightConfig& config);

    std::string modelDir_;
};

} // namespace owengine::ecs
