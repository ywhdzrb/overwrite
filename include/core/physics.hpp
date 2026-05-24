#pragma once

/**
 * @file physics.h
 * @brief 物理系统 — 重力/地面碰撞/墙体碰撞/AABB碰撞检测
 *
 * 归属模块：core
 * 核心职责：提供基础物理模拟和碰撞检测
 * 依赖关系：glm 数学库
 * 关键设计：支持地形高度查询回调（PhysicsTerrainQuery），
 *           碰撞盒管理使用 pair(position, size) 向量存储
 */

#include <functional>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace owengine {

/**
 * @brief 地形高度查询函数类型
 */
using PhysicsTerrainQuery = std::function<float(float x, float z)>;

class Physics {
public:
    Physics();
    ~Physics() = default;
    
    // 更新物理系统
    void update(float deltaTime);
    
    // 碰撞检测
    [[nodiscard]] bool checkGroundCollision(const glm::vec3& position, float height) const noexcept;
    [[nodiscard]] bool checkWallCollision(const glm::vec3& position, float radius) const noexcept;
    
    // 设置重力
    void setGravity(float g) noexcept { gravity_ = g; }
    [[nodiscard]] float getGravity() const noexcept { return gravity_; }
    
    // 设置默认地面高度（无地形时使用）
    void setGroundHeight(float height) noexcept { defaultGroundHeight_ = height; }
    [[nodiscard]] float getGroundHeight() const noexcept { return defaultGroundHeight_; }
    
    // 地形查询接口
    void setTerrainQuery(PhysicsTerrainQuery query) noexcept { terrainQuery_ = std::move(query); }
    void clearTerrainQuery() noexcept { terrainQuery_ = nullptr; }
    
    // 查询地形高度
    [[nodiscard]] float queryTerrainHeight(float x, float z) const;
    
    // 添加碰撞体
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size) noexcept;
    void clearCollisionBoxes() noexcept;
    
    // 获取所有碰撞体
    [[nodiscard]] const std::vector<std::pair<glm::vec3, glm::vec3>>& getCollisionBoxes() const noexcept { return collisionBoxes_; }

private:
    // AABB碰撞检测
    [[nodiscard]] bool checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                                         const glm::vec3& pos2, const glm::vec3& size2) const noexcept;

    float gravity_;
    float defaultGroundHeight_;  // 默认地面高度（无地形时）
    PhysicsTerrainQuery terrainQuery_;  // 地形高度查询回调
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;  // position, size
};

} // namespace owengine
