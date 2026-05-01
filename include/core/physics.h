#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>

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
    bool checkGroundCollision(const glm::vec3& position, float height) const;
    bool checkWallCollision(const glm::vec3& position, float radius) const;
    
    // 设置重力
    void setGravity(float g) { gravity = g; }
    float getGravity() const { return gravity; }
    
    // 设置默认地面高度（无地形时使用）
    void setGroundHeight(float height) { defaultGroundHeight = height; }
    float getGroundHeight() const { return defaultGroundHeight; }
    
    // 地形查询接口
    void setTerrainQuery(PhysicsTerrainQuery query) { terrainQuery = std::move(query); }
    void clearTerrainQuery() { terrainQuery = nullptr; }
    
    // 查询地形高度
    float queryTerrainHeight(float x, float z) const;
    
    // 添加碰撞体
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
    void clearCollisionBoxes();
    
    // 获取所有碰撞体
    const std::vector<std::pair<glm::vec3, glm::vec3>>& getCollisionBoxes() const { return collisionBoxes; }

private:
    // AABB碰撞检测
    bool checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                           const glm::vec3& pos2, const glm::vec3& size2) const;

private:
    float gravity;
    float defaultGroundHeight;  // 默认地面高度（无地形时）
    PhysicsTerrainQuery terrainQuery;  // 地形高度查询回调
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes;  // position, size
};

} // namespace owengine
