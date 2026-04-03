#ifndef SHARED_ECS_SYSTEMS_H
#define SHARED_ECS_SYSTEMS_H

#include <entt/entt.hpp>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "ecs/components.h"

namespace vgame {
namespace ecs {

/**
 * @brief ECS 世界管理器（共享）
 * 
 * 管理所有实体、组件的核心类
 */
class World {
public:
    World();
    ~World() = default;
    
    // 禁止拷贝
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    
    // 实体管理
    entt::entity createEntity();
    void destroyEntity(entt::entity entity);
    
    // 获取注册表（用于直接操作组件）
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    
    // 获取主相机实体
    entt::entity getMainCamera() const { return mainCamera_; }
    void setMainCamera(entt::entity camera) { mainCamera_ = camera; }
    
    // 获取玩家实体
    entt::entity getPlayer() const { return player_; }
    void setPlayer(entt::entity player) { player_ = player; }
    
    // 便捷方法：创建预设实体
    entt::entity createPlayer();
    
private:
    entt::registry registry_;
    entt::entity mainCamera_{entt::null};
    entt::entity player_{entt::null};
};

/**
 * @brief 移动系统（共享）
 * 
 * 根据输入状态更新实体位置和旋转
 */
class MovementSystem {
public:
    explicit MovementSystem(World& world);
    
    void update(float deltaTime);
    
    // 碰撞体管理
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
    void clearCollisionBoxes();
    const std::vector<std::pair<glm::vec3, glm::vec3>>& getCollisionBoxes() const { return collisionBoxes_; }
    
private:
    World& world_;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;  // position, size
    
    // 碰撞检测
    bool checkCollision(const glm::vec3& position, const glm::vec3& playerSize) const;
    glm::vec3 resolveCollision(const glm::vec3& oldPos, const glm::vec3& newPos, const glm::vec3& playerSize) const;
    
    void updateMovement(entt::entity entity, TransformComponent& transform,
                       VelocityComponent& velocity,
                       MovementControllerComponent& controller,
                       const InputStateComponent& input,
                       float deltaTime);
};

/**
 * @brief 物理系统（共享）
 * 
 * 处理重力、碰撞等物理模拟
 */
class PhysicsSystem {
public:
    explicit PhysicsSystem(World& world);
    
    void update(float deltaTime);
    
    // 碰撞体管理
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
    void clearCollisionBoxes();
    
private:
    World& world_;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;  // position, size
    
    bool checkGroundCollision(const glm::vec3& position, float height) const;
    bool checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                           const glm::vec3& pos2, const glm::vec3& size2) const;
};

} // namespace ecs
} // namespace vgame

#endif // SHARED_ECS_SYSTEMS_H
