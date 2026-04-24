#ifndef SERVER_WORLD_H
#define SERVER_WORLD_H

#include "ecs/systems.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <chrono>
#include <vector>

namespace owengine {
namespace server {

/**
 * @brief 服务器端 ECS 世界
 * 
 * 复用共享 ECS，添加服务器专用功能：
 * - 玩家连接/断开管理
 * - 状态同步
 * - 实体所有权管理
 */
class ServerWorld : public ecs::World {
public:
    ServerWorld();
    ~ServerWorld() = default;
    
    // 玩家管理
    /**
     * @brief 玩家连接时创建实体
     * @param clientId 客户端唯一标识
     * @return 玩家实体
     */
    entt::entity onPlayerConnect(const std::string& clientId);
    
    /**
     * @brief 玩家断开时清理
     * @param clientId 客户端唯一标识
     */
    void onPlayerDisconnect(const std::string& clientId);
    
    /**
     * @brief 获取客户端对应的玩家实体
     */
    entt::entity getPlayerByClientId(const std::string& clientId) const;
    
    /**
     * @brief 获取所有连接的玩家
     */
    const std::unordered_map<std::string, entt::entity>& getConnectedPlayers() const {
        return connectedPlayers_;
    }
    
    // 服务器更新
    void update(float deltaTime);
    
    // 状态快照（用于同步）
    struct PlayerSnapshot {
        std::string clientId;
        glm::vec3 position;
        float yaw;
        float pitch;
        bool isJumping;
        bool isGrounded;
    };
    
    /**
     * @brief 获取所有玩家的状态快照
     */
    std::vector<PlayerSnapshot> getPlayersSnapshot() const;
    
    /**
     * @brief 应用客户端输入到玩家实体
     */
    void applyPlayerInput(const std::string& clientId, 
                         const ecs::InputStateComponent& input,
                         const glm::vec3& cameraFront,
                         const glm::vec3& cameraRight);
    
    /**
     * @brief 设置玩家初始位置
     */
    void setPlayerPosition(const std::string& clientId, const glm::vec3& position);
    
    // 游戏循环控制
    float getDeltaTime() const { return lastDeltaTime_; }
    
private:
    std::unordered_map<std::string, entt::entity> connectedPlayers_;
    ecs::MovementSystem movementSystem_;
    ecs::PhysicsSystem physicsSystem_;
    float lastDeltaTime_{0.016f};
    
    // 时间管理
    std::chrono::high_resolution_clock::time_point lastUpdateTime_;
};

} // namespace server
} // namespace owengine

#endif // SERVER_WORLD_H