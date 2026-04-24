// 共享 ECS 系统实现
//
// 更新顺序（client_systems.cpp 中定义）：
//   1. MovementSystem::update() — 输入驱动的位置更新 + 斜坡投影
//   2. PhysicsSystem::update() — 重力/着地/跳跃 + 地形高度查询
//
// 斜坡运动设计：MovementSystem 将水平输入投影到坡面上（v_proj = v - n*(v·n)），
// 产生带 Y 分量的三维速度，直接更新 position.xyz 使角色沿地形轮廓平滑移动。
// PhysicsSystem 仅在容差范围内做微小修正（足部穿透/悬空 < 0.1f）。
// 两者配合避免了 "水平移动 → Y 不变 → 物理弹回" 的锯齿形卡顿。
#include "ecs/systems.h"
#include "logger.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace owengine {
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
    transform.position = glm::vec3(0.0f, 0.9f, 5.0f);
    transform.yaw = 0.0f;  // 初始朝向 -Z 轴（屏幕内方向）
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
            transform.yaw += input.mouseDeltaX * controller.mouseSensitivity;  // 右移右转，左移左转
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
                
                // 获取物理组件（用于空中控制和坡度处理）
                if (world_.registry().all_of<PhysicsComponent>(entity)) {
                    const auto& physics = world_.registry().get<PhysicsComponent>(entity);
                    
                    // 空中控制：如果不在着地状态，降低控制力
                    if (!physics.isGrounded) {
                        horizontalVelocity *= controller.airControlFactor;
                    }
                    // 坡度处理：如果着地且地面不是水平的，将移动向量投影到斜坡面。
                    // 公式：v_proj = v - n * (v·n)，其中 n 为 PhysicsSystem 上一帧计算的 groundNormal。
                    // 投影后的速度产生 Y 分量，使角色沿坡面平滑运动，避免水平移动导致穿透/悬空。
                    else if (glm::dot(physics.groundNormal, glm::vec3(0.0f, 1.0f, 0.0f)) < 0.999f) {
                        // 投影到地面平面：v_proj = v - n * (v·n)
                        glm::vec3 projected = horizontalVelocity - physics.groundNormal * glm::dot(horizontalVelocity, physics.groundNormal);
                        // 重新归一化以保持速度大小，同时保留坡度产生的 Y 分量
                        if (glm::length(projected) > 0.001f) {
                            horizontalVelocity = glm::normalize(projected) * speed;
                        }
                    }
                }
            }
            
            // 保存旧位置
            glm::vec3 oldPos = transform.position;
            glm::vec3 playerSize(0.6f, 1.8f, 0.6f);  // 玩家碰撞体大小
            
            // 更新位置（含坡度投影的 Y 分量，使角色沿坡面平滑运动）
            transform.position.x += horizontalVelocity.x * deltaTime;
            transform.position.y += horizontalVelocity.y * deltaTime;
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
    // 检查单条目缓存（同一帧内相同坐标避免重复计算）
    if (cachedQueryValid_ && cachedQueryX_ == x && cachedQueryZ_ == z) {
        return cachedQueryHeight_;
    }
    
    cachedQueryValid_ = false;  // 暂时关闭缓存，在统一出口处重新缓存
    
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
        try {
            float terrainH = terrainQuery_(x, z);
            maxHeight = std::max(maxHeight, terrainH);
        } catch (...) {
            // 地形查询失败，返回碰撞箱高度
            Logger::warning("[PhysicsSystem] Terrain query failed at (" + 
                         std::to_string(x) + ", " + std::to_string(z) + ")");
        }
    }
    
    // 缓存结果（统一出口）
    cachedQueryX_ = x;
    cachedQueryZ_ = z;
    cachedQueryHeight_ = maxHeight;
    cachedQueryValid_ = true;
    return maxHeight;
}

glm::vec3 PhysicsSystem::computeTerrainNormal(float x, float z, float centerHeight) const {
    const float eps = 0.1f;
    
    // 如果没有地形查询，返回上向量
    if (!terrainQuery_) {
        Logger::debug("[PhysicsSystem] computeTerrainNormal: terrainQuery_ is null, returning up vector");
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    
    // 使用中心差分计算梯度（复用已传入的 centerHeight，免重复查询）
    float heightXPlus = queryTerrainHeight(x + eps, z);
    float heightXMinus = queryTerrainHeight(x - eps, z);
    float heightZPlus = queryTerrainHeight(x, z + eps);
    float heightZMinus = queryTerrainHeight(x, z - eps);
    
    // 计算偏导数（用传入的 centerHeight 替代中心查询结果）
    float dhdx = (heightXPlus - heightXMinus) / (2.0f * eps);
    float dhdz = (heightZPlus - heightZMinus) / (2.0f * eps);
    
    // 法向量 = normalize(-dh/dx, 1, -dh/dz)
    return glm::normalize(glm::vec3(-dhdx, 1.0f, -dhdz));
}

float PhysicsSystem::getTerrainHeight(float x, float z) const {
    return queryTerrainHeight(x, z);
}

void PhysicsSystem::update(float deltaTime) {
    // 重置每帧高度缓存
    cachedQueryValid_ = false;
    
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
            float oldY = transform.position.y;
            transform.position.y += velocity->linear.y * deltaTime;
            
            // 脚底位置
            float footY = transform.position.y - physics.colliderHeight * 0.5f;
            
            // 检测脚是否穿过地形
            if (footY <= terrainHeight) {
                // 脚到达地形表面
                transform.position.y = terrainHeight + physics.colliderHeight * 0.5f;
                velocity->linear.y = 0.0f;
                physics.isJumping = false;
                physics.isGrounded = true;
                physics.groundHeight = terrainHeight;
                physics.groundNormal = computeTerrainNormal(transform.position.x, transform.position.z, terrainHeight);
            }
        } else {
            float footY = transform.position.y - physics.colliderHeight * 0.5f;
            float targetY = terrainHeight + physics.colliderHeight * 0.5f;
            
            // 如果脚低于地形，修正位置
            if (footY < terrainHeight) {
                transform.position.y = targetY;
                velocity->linear.y = 0.0f;
                physics.groundHeight = terrainHeight;
                physics.groundNormal = computeTerrainNormal(transform.position.x, transform.position.z, terrainHeight);
            } else if (footY > terrainHeight + 0.1f) {
                // 脚离地超过0.1f，开始下落
                physics.isGrounded = false;
                physics.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                // 玩家在地面上（在容差范围内）
                velocity->linear.y = 0.0f;
                physics.groundHeight = terrainHeight;
                physics.groundNormal = computeTerrainNormal(transform.position.x, transform.position.z, terrainHeight);
            }
        }
    }
}

} // namespace ecs
} // namespace owengine
