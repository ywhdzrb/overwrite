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
    for (const auto& box : collisionBoxes_) {
        const glm::vec3& boxPos = box.first;
        const glm::vec3& boxSize = box.second;
        
        // AABB 碰撞检测
        if (position.x + playerSize.x / 2.0f > boxPos.x - boxSize.x / 2.0f &&
            position.x - playerSize.x / 2.0f < boxPos.x + boxSize.x / 2.0f &&
            position.y + playerSize.y > boxPos.y - boxSize.y / 2.0f &&
            position.y - playerSize.y / 2.0f < boxPos.y + boxSize.y / 2.0f &&
            position.z + playerSize.z / 2.0f > boxPos.z - boxSize.z / 2.0f &&
            position.z - playerSize.z / 2.0f < boxPos.z + boxSize.z / 2.0f) {
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

void PhysicsSystem::update(float deltaTime) {
    auto view = world_.registry().view<TransformComponent, PhysicsComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& physics = view.get<PhysicsComponent>(entity);
        
        // 获取可选的速度组件
        VelocityComponent* velocity = nullptr;
        if (world_.registry().all_of<VelocityComponent>(entity)) {
            velocity = &world_.registry().get<VelocityComponent>(entity);
        }
        
        if (velocity && physics.useGravity) {
            // 应用重力
            if (physics.isJumping || transform.position.y > physics.groundHeight) {
                velocity->linear.y -= physics.gravity * deltaTime;
                transform.position.y += velocity->linear.y * deltaTime;
                
                // 检查落地
                if (transform.position.y <= physics.groundHeight) {
                    transform.position.y = physics.groundHeight;
                    velocity->linear.y = 0.0f;
                    physics.isJumping = false;
                    physics.isGrounded = true;
                }
            } else {
                transform.position.y = physics.groundHeight;
                velocity->linear.y = 0.0f;
                physics.isGrounded = true;
            }
        }
    }
}

} // namespace ecs
} // namespace vgame
