#include "server/server_world.h"
#include "ecs/components.h"
#include <iostream>

namespace vgame {
namespace server {

ServerWorld::ServerWorld() 
    : World()
    , movementSystem_(*this)
    , physicsSystem_(*this)
    , lastUpdateTime_(std::chrono::high_resolution_clock::now()) {
    std::cout << "[ServerWorld] 服务器世界初始化完成" << std::endl;
}

entt::entity ServerWorld::onPlayerConnect(const std::string& clientId) {
    // 检查是否已存在
    auto it = connectedPlayers_.find(clientId);
    if (it != connectedPlayers_.end()) {
        std::cout << "[ServerWorld] 玩家已存在: " << clientId << std::endl;
        return it->second;
    }
    
    // 创建玩家实体（使用共享方法）
    auto entity = createPlayer();
    connectedPlayers_[clientId] = entity;
    
    // 设置网络同步组件
    if (auto* networkSync = registry().try_get<ecs::NetworkSyncComponent>(entity)) {
        networkSync->isOwned = true;
    }
    
    std::cout << "[ServerWorld] 玩家连接: " << clientId 
              << ", 实体: " << static_cast<uint32_t>(entity) << std::endl;
    
    return entity;
}

void ServerWorld::onPlayerDisconnect(const std::string& clientId) {
    auto it = connectedPlayers_.find(clientId);
    if (it == connectedPlayers_.end()) {
        std::cout << "[ServerWorld] 玩家不存在: " << clientId << std::endl;
        return;
    }
    
    // 销毁实体
    destroyEntity(it->second);
    connectedPlayers_.erase(it);
    
    std::cout << "[ServerWorld] 玩家断开: " << clientId << std::endl;
}

entt::entity ServerWorld::getPlayerByClientId(const std::string& clientId) const {
    auto it = connectedPlayers_.find(clientId);
    if (it != connectedPlayers_.end()) {
        return it->second;
    }
    return entt::null;
}

void ServerWorld::update(float deltaTime) {
    lastDeltaTime_ = deltaTime;
    
    // 更新移动和物理系统
    movementSystem_.update(deltaTime);
    physicsSystem_.update(deltaTime);
}

std::vector<ServerWorld::PlayerSnapshot> ServerWorld::getPlayersSnapshot() const {
    std::vector<PlayerSnapshot> snapshots;
    snapshots.reserve(connectedPlayers_.size());
    
    for (const auto& [clientId, entity] : connectedPlayers_) {
        if (registry().valid(entity)) {
            auto& transform = registry().get<ecs::TransformComponent>(entity);
            auto* physics = registry().try_get<ecs::PhysicsComponent>(entity);
            
            snapshots.push_back(PlayerSnapshot{
                .clientId = clientId,
                .position = transform.position,
                .yaw = transform.yaw,
                .pitch = transform.pitch,
                .isJumping = physics ? physics->isJumping : false,
                .isGrounded = physics ? physics->isGrounded : true
            });
        }
    }
    
    return snapshots;
}

void ServerWorld::applyPlayerInput(const std::string& clientId, 
                                   const ecs::InputStateComponent& input,
                                   const glm::vec3& cameraFront,
                                   const glm::vec3& cameraRight) {
    auto entity = getPlayerByClientId(clientId);
    if (!registry().valid(entity)) {
        return;
    }
    
    // 更新输入状态组件
    if (auto* inputState = registry().try_get<ecs::InputStateComponent>(entity)) {
        *inputState = input;
    }
    
    // 更新移动控制器（同步相机方向）
    if (auto* controller = registry().try_get<ecs::MovementControllerComponent>(entity)) {
        controller->moveFront = cameraFront;
        controller->moveRight = cameraRight;
    }
}

void ServerWorld::setPlayerPosition(const std::string& clientId, const glm::vec3& position) {
    auto entity = getPlayerByClientId(clientId);
    if (!registry().valid(entity)) {
        return;
    }
    
    if (auto* transform = registry().try_get<ecs::TransformComponent>(entity)) {
        transform->position = position;
    }
}

} // namespace server
} // namespace vgame