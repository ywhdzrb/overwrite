// 共享 ECS 系统实现
#include "ecs/systems.h"
#include <iostream>
#include <algorithm>

namespace vgame {
namespace ecs {

// ==================== World 实现 ====================

World::World() {
    std::cout << "[World] 初始化完成" << std::endl;
}

entt::entity World::createEntity() {
    return registry_.create();
}

void World::destroyEntity(entt::entity entity) {
    registry_.destroy(entity);
}

entt::entity World::createPlayer() {
    auto entity = registry_.create();
    
    // 添加变换组件
    auto& transform = registry_.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 1.5f, 5.0f);
    transform.yaw = -90.0f;  // 初始朝向 -Z 轴
    transform.pitch = 0.0f;
    
    // 添加速度组件
    registry_.emplace<VelocityComponent>(entity);
    
    // 添加移动控制器
    registry_.emplace<MovementControllerComponent>(entity);
    
    // 添加物理组件
    registry_.emplace<PhysicsComponent>(entity);
    
    // 添加输入状态
    registry_.emplace<InputStateComponent>(entity);
    
    // 添加玩家标签
    registry_.emplace<PlayerTag>(entity);
    
    // 添加名称
    registry_.emplace<NameComponent>(entity, "Player");
    
    // 添加网络同步组件
    registry_.emplace<NetworkSyncComponent>(entity);
    
    // 添加实体类型
    registry_.emplace<EntityTypeComponent>(entity, EntityType::Player);
    
    player_ = entity;
    
    std::cout << "[World] 玩家实体创建成功" << std::endl;
    
    return entity;
}

// ==================== MovementSystem 实现 ====================

MovementSystem::MovementSystem(World& world) : world_(world) {
    std::cout << "[MovementSystem] 初始化完成" << std::endl;
}

void MovementSystem::addCollisionBox(const glm::vec3& position, const glm::vec3& size) {
    collisionBoxes_.push_back({position, size});
}

void MovementSystem::clearCollisionBoxes() {
    collisionBoxes_.clear();
}

bool MovementSystem::checkCollision(const glm::vec3& position, const glm::vec3& playerSize) const {
    // position.y 是脚底位置，玩家占据 [position.y, position.y + playerSize.y]
    float playerBottom = position.y;
    float playerTop = position.y + playerSize.y;
    
    for (const auto& box : collisionBoxes_) {
        const glm::vec3& boxPos = box.first;
        const glm::vec3& boxSize = box.second;
        
        // 碰撞箱占据 [boxPos.y - boxSize.y/2, boxPos.y + boxSize.y/2]
        float boxBottom = boxPos.y - boxSize.y * 0.5f;
        float boxTop = boxPos.y + boxSize.y * 0.5f;
        
        // Y 轴检测：只有当玩家在碰撞箱侧面高度范围内才算碰撞
        // 排除"站在顶上"（playerBottom >= boxTop）和"从下面穿过"（playerTop <= boxBottom）
        if (playerBottom >= boxTop - 0.01f || playerTop <= boxBottom + 0.01f) {
            continue;  // 不算碰撞
        }
        
        // X/Z 轴检测
        if (position.x + playerSize.x * 0.5f > boxPos.x - boxSize.x * 0.5f &&
            position.x - playerSize.x * 0.5f < boxPos.x + boxSize.x * 0.5f &&
            position.z + playerSize.z * 0.5f > boxPos.z - boxSize.z * 0.5f &&
            position.z - playerSize.z * 0.5f < boxPos.z + boxSize.z * 0.5f) {
            return true;
        }
    }
    return false;
}

glm::vec3 MovementSystem::resolveCollision(const glm::vec3& oldPos, const glm::vec3& newPos, const glm::vec3& playerSize) const {
    // 尝试分轴移动，找到可以移动到的位置
    glm::vec3 result = oldPos;
    
    // X 轴
    glm::vec3 testPosX = glm::vec3(newPos.x, oldPos.y, oldPos.z);
    if (!checkCollision(testPosX, playerSize)) {
        result.x = newPos.x;
    }
    
    // Z 轴
    glm::vec3 testPosZ = glm::vec3(result.x, oldPos.y, newPos.z);
    if (!checkCollision(testPosZ, playerSize)) {
        result.z = newPos.z;
    }
    
    return result;
}

void MovementSystem::update(float deltaTime) {
    // 获取玩家视图
    auto view = world_.registry().view<TransformComponent, MovementControllerComponent, InputStateComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& controller = view.get<MovementControllerComponent>(entity);
        auto& input = view.get<InputStateComponent>(entity);
        
        // 获取可选的速度组件
        VelocityComponent* velocity = nullptr;
        if (world_.registry().all_of<VelocityComponent>(entity)) {
            velocity = &world_.registry().get<VelocityComponent>(entity);
        }
        
        // 处理鼠标旋转
        if (input.mouseDeltaX != 0.0f || input.mouseDeltaY != 0.0f) {
            transform.yaw -= input.mouseDeltaX * controller.mouseSensitivity;  // 左移向右转，右移向左转
            transform.pitch += input.mouseDeltaY * controller.mouseSensitivity;  // 上移向上看，下移向下看
            
            // 限制俯仰角
            transform.pitch = glm::clamp(transform.pitch, -89.0f, 89.0f);
        }
        
        // 移动逻辑
        if (velocity) {
            float speed = controller.movementSpeed;
            if (input.sprint) speed *= controller.sprintMultiplier;
            
            // 计算水平移动
            glm::vec3 horizontalVelocity(0.0f);
            // 使用控制器方向（可能由相机同步）
            glm::vec3 front = controller.moveFront;
            glm::vec3 right = controller.moveRight;
            
            // 水平移动（忽略 Y 分量）
            front.y = 0.0f;
            front = glm::normalize(front);
            right.y = 0.0f;
            right = glm::normalize(right);
            
            if (input.moveForward) horizontalVelocity += front;
            if (input.moveBackward) horizontalVelocity -= front;
            if (input.moveLeft) horizontalVelocity -= right;
            if (input.moveRight) horizontalVelocity += right;
            
            if (glm::length(horizontalVelocity) > 0.0f) {
                horizontalVelocity = glm::normalize(horizontalVelocity) * speed;
            }
            
            // 保存旧位置
            glm::vec3 oldPos = transform.position;
            glm::vec3 playerSize(0.6f, 1.8f, 0.6f);  // 玩家碰撞体大小
            
            // 更新位置
            transform.position.x += horizontalVelocity.x * deltaTime;
            transform.position.z += horizontalVelocity.z * deltaTime;
            
            // 碰撞检测
            if (checkCollision(transform.position, playerSize)) {
                transform.position = resolveCollision(oldPos, transform.position, playerSize);
            }
        }
    }
}

// ==================== PhysicsSystem 实现 ====================

PhysicsSystem::PhysicsSystem(World& world) : world_(world) {
    std::cout << "[PhysicsSystem] 初始化完成" << std::endl;
}

void PhysicsSystem::addCollisionBox(const glm::vec3& position, const glm::vec3& size) {
    collisionBoxes_.emplace_back(position, size);
}

void PhysicsSystem::clearCollisionBoxes() {
    collisionBoxes_.clear();
}

bool PhysicsSystem::checkGroundCollision(const glm::vec3& position, float height) const {
    for (const auto& box : collisionBoxes_) {
        glm::vec3 playerPos = position;
        playerPos.y -= height * 0.5f;
        glm::vec3 playerSize(0.3f, height, 0.3f);
        
        if (checkAABBCollision(playerPos, playerSize, box.first, box.second)) {
            return true;
        }
    }
    return false;
}

bool PhysicsSystem::checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                                       const glm::vec3& pos2, const glm::vec3& size2) const {
    return (abs(pos1.x - pos2.x) < (size1.x + size2.x) * 0.5f) &&
           (abs(pos1.y - pos2.y) < (size1.y + size2.y) * 0.5f) &&
           (abs(pos1.z - pos2.z) < (size1.z + size2.z) * 0.5f);
}

float PhysicsSystem::queryTerrainHeight(float x, float z) const {
    float maxHeight = defaultGroundHeight_;
    
    // 检查所有碰撞箱，找到该位置最高的站立面
    for (const auto& box : collisionBoxes_) {
        const glm::vec3& boxPos = box.first;
        const glm::vec3& boxSize = box.second;
        
        // 检查 (x, z) 是否在碰撞箱的水平范围内
        float halfSizeX = boxSize.x * 0.5f;
        float halfSizeZ = boxSize.z * 0.5f;
        
        if (x >= boxPos.x - halfSizeX && x <= boxPos.x + halfSizeX &&
            z >= boxPos.z - halfSizeZ && z <= boxPos.z + halfSizeZ) {
            // 碰撞箱顶面高度
            float boxTop = boxPos.y + boxSize.y * 0.5f;
            if (boxTop > maxHeight) {
                maxHeight = boxTop;
            }
        }
    }
    
    // 如果有自定义地形查询，取两者最大值
    if (terrainQuery_) {
        float terrainH = terrainQuery_(x, z);
        return std::max(maxHeight, terrainH);
    }
    
    return maxHeight;
}

void PhysicsSystem::update(float deltaTime) {
    auto view = world_.registry().view<TransformComponent, PhysicsComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& physics = view.get<PhysicsComponent>(entity);
        
        // 获取可选的速度组件和输入状态
        VelocityComponent* velocity = nullptr;
        InputStateComponent* input = nullptr;
        if (world_.registry().all_of<VelocityComponent>(entity)) {
            velocity = &world_.registry().get<VelocityComponent>(entity);
        }
        if (world_.registry().all_of<InputStateComponent>(entity)) {
            input = &world_.registry().get<InputStateComponent>(entity);
        }
        
        if (!velocity || !physics.useGravity) continue;
        
        // 查询当前位置的地形高度
        float terrainHeight = queryTerrainHeight(transform.position.x, transform.position.z);
        physics.cachedTerrainHeight = terrainHeight;
        physics.terrainCacheValid = true;
        
        // 跳跃输入处理：仅在着地时允许跳跃
        if (input && input->jump && physics.isGrounded && !physics.isJumping) {
            velocity->linear.y = physics.jumpForce;
            physics.isJumping = true;
            physics.isGrounded = false;
        }
        
        // 空中物理：非着地状态应用重力
        if (!physics.isGrounded) {
            velocity->linear.y -= physics.gravity * deltaTime;
            transform.position.y += velocity->linear.y * deltaTime;
            
            // 着地检测：位置低于地形高度
            if (transform.position.y <= terrainHeight) {
                transform.position.y = terrainHeight;
                velocity->linear.y = 0.0f;
                physics.isJumping = false;
                physics.isGrounded = true;
                physics.groundHeight = terrainHeight;  // 更新站立面高度
            }
        } else {
            // 着地状态检查
            if (transform.position.y < terrainHeight) {
                // 位置低于地形，修正到地形高度
                transform.position.y = terrainHeight;
            } else if (transform.position.y > terrainHeight + 0.01f) {
                // 位置高于地形（悬浮），触发掉落
                physics.isGrounded = false;
            }
            velocity->linear.y = 0.0f;
            physics.groundHeight = terrainHeight;
        }
    }
}

} // namespace ecs
} // namespace vgame
