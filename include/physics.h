#ifndef PHYSICS_H
#define PHYSICS_H

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace vgame {

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
    
    // 设置地面高度
    void setGroundHeight(float height) { groundHeight = height; }
    float getGroundHeight() const { return groundHeight; }
    
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
    float groundHeight;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes;  // position, size
};

} // namespace vgame

#endif // PHYSICS_H