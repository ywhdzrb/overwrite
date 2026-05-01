#pragma once

#include <entt/entt.hpp>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "ecs/components.h"

namespace owengine {
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
 * @brief 地形高度查询函数类型
 * @param x 世界坐标 X
 * @param z 世界坐标 Z
 * @return 该位置的地形高度（Y），如果没有地形返回默认地面高度
 */
using TerrainHeightQuery = std::function<float(float x, float z)>;

/**
 * @brief 物理系统（共享）
 * 
 * 处理重力、碰撞等物理模拟。
 * 更新顺序：MovementSystem → PhysicsSystem
 * 
 * 设计说明：
 * - 使用 isGrounded 状态控制重力应用，而非坐标比较
 * - 支持地形高度查询接口（通过 setTerrainQuery 注入地形系统委托）
 * - groundHeight 和 groundNormal 由地形/碰撞检测动态更新
 * - MovementSystem 负责沿坡面的平滑运动（投影水平输入到坡面），
 *   PhysicsSystem 负责重力、着地检测和容差范围内的微小修正
 * 
 * 性能优化：
 * - queryTerrainHeight / computeTerrainNormal：每帧单条目坐标缓存，
 *   避免同一帧内对同一坐标的重复 Perlin 噪声查询（中心点复用 4 次邻居查询 → 减少 1 次）
 * - computeTerrainNormal 接受预计算的 centerHeight 参数，消除中心点重复查询
 */
class PhysicsSystem {
public:
    explicit PhysicsSystem(World& world);
    
    void update(float deltaTime);
    
    // 碰撞体管理（用于建筑等 AABB 碰撞）
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
    void clearCollisionBoxes();
    
    // 地形查询接口：注入外部高度查询委托（如 TerrainRenderer::getHeight）
    void setTerrainQuery(TerrainHeightQuery query) { terrainQuery_ = std::move(query); }
    void clearTerrainQuery() { terrainQuery_ = nullptr; }
    bool hasTerrainQuery() const { return terrainQuery_ != nullptr; }
    float getTerrainHeight(float x, float z) const;
    
    // 默认地面高度（无地形注入时使用的平坦地面）
    void setDefaultGroundHeight(float height) { defaultGroundHeight_ = height; }
    float getDefaultGroundHeight() const { return defaultGroundHeight_; }
    
private:
    World& world_;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;  // position, size
    TerrainHeightQuery terrainQuery_;                               // 地形高度查询回调
    float defaultGroundHeight_{-100.0f};                            // 默认地面高度（很低以允许走到山谷）
    
    bool checkGroundCollision(const glm::vec3& position, float height) const;
    bool checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                           const glm::vec3& pos2, const glm::vec3& size2) const;
    
    // 查询地形高度（内部含单帧缓存，同一帧同一坐标只计算一次）
    float queryTerrainHeight(float x, float z) const;
    // 用中心差分法计算地形表面法向量。centerHeight 由调用方预查询，避免中心点重复计算。
    glm::vec3 computeTerrainNormal(float x, float z, float centerHeight) const;

    // 每帧单条目高度缓存：同一帧内重复查询相同坐标时直接返回。
    // mutable：逻辑上不改变对象状态，仅用于性能优化缓存。
    mutable bool cachedQueryValid_{false};
    mutable float cachedQueryX_{0.0f};
    mutable float cachedQueryZ_{0.0f};
    mutable float cachedQueryHeight_{0.0f};
};

} // namespace ecs
} // namespace owengine
