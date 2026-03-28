// 物理系统实现
// 处理简单的物理和碰撞检测
#include "physics.h"
#include <algorithm>

namespace vgame {

Physics::Physics()
    : gravity(9.8f),
      groundHeight(0.0f) {
}

void Physics::update(float deltaTime) {
    // 物理更新在Camera类中处理
    // 这里主要用于碰撞检测
}

bool Physics::checkGroundCollision(const glm::vec3& position, float height) const {
    return position.y - height <= groundHeight;
}

bool Physics::checkWallCollision(const glm::vec3& position, float radius) const {
    // 检查与所有碰撞盒的碰撞
    for (const auto& box : collisionBoxes) {
        const glm::vec3& boxPos = box.first;
        const glm::vec3& boxSize = box.second;
        
        // AABB碰撞检测
        if (position.x + radius > boxPos.x - boxSize.x / 2.0f &&
            position.x - radius < boxPos.x + boxSize.x / 2.0f &&
            position.y > boxPos.y &&
            position.y < boxPos.y + boxSize.y &&
            position.z + radius > boxPos.z - boxSize.z / 2.0f &&
            position.z - radius < boxPos.z + boxSize.z / 2.0f) {
            return true;
        }
    }
    return false;
}

void Physics::addCollisionBox(const glm::vec3& position, const glm::vec3& size) {
    collisionBoxes.push_back({position, size});
}

void Physics::clearCollisionBoxes() {
    collisionBoxes.clear();
}

bool Physics::checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                                  const glm::vec3& pos2, const glm::vec3& size2) const {
    return (pos1.x - size1.x / 2.0f < pos2.x + size2.x / 2.0f &&
            pos1.x + size1.x / 2.0f > pos2.x - size2.x / 2.0f &&
            pos1.y - size1.y / 2.0f < pos2.y + size2.y / 2.0f &&
            pos1.y + size1.y / 2.0f > pos2.y - size2.y / 2.0f &&
            pos1.z - size1.z / 2.0f < pos2.z + size2.z / 2.0f &&
            pos1.z + size1.z / 2.0f > pos2.z - size2.z / 2.0f);
}

} // namespace vgame