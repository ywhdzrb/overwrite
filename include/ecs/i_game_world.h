#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "core/camera.h"

// 前向声明
struct GLFWwindow;

namespace owengine::ecs {
class InputSystem;
}

namespace owengine::client {
class NetworkSystem;
struct RemotePlayer;
} // namespace owengine::client

namespace owengine {
namespace ecs {

/// 地形高度查询函数类型
using TerrainHeightQuery = std::function<float(float x, float z)>;

/// 远程玩家信息（轻量结构，供渲染层使用）
struct RemotePlayerInfo {
    std::string clientId;
    bool isMoving = false;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float scale = 1.0f;
};

/// 发现的服务端信息（轻量结构，供开发者面板使用）
struct DiscoveredServerInfo {
    std::string name;
    std::string host;
    uint16_t port = 0;
    int pingMs = 0;
    int currentPlayers = 0;
    int maxPlayers = 0;
};

/**
 * @brief 游戏世界抽象接口
 *
 * 将 ECS 游戏逻辑层与渲染层解耦。
 * Renderer 仅通过此接口操作游戏世界，不再直接依赖 concrete ClientWorld。
 */
class IGameWorld {
public:
    virtual ~IGameWorld() = default;

    // ── 生命周期 ──────────────────────────────
    virtual void initClientSystems(GLFWwindow* window, int w, int h) = 0;
    virtual entt::entity createClientPlayer(int w, int h) = 0;
    virtual void setTerrainQuery(TerrainHeightQuery query) = 0;
    virtual void reset() = 0;

    // ── 帧管线 ────────────────────────────────
    virtual void updateSync(float dt) = 0;   // 输入 + 网络接收（主线程）
    virtual void updateAsync(float dt) = 0;  // 模拟（可后台线程）
    virtual void sendNetInputs() = 0;        // 网络发送

    // ── 玩家查询 ──────────────────────────────
    virtual bool isPlayerValid() const = 0;
    virtual entt::entity getPlayerEntity() const = 0;
    virtual glm::vec3 getPlayerPosition() const = 0;
    virtual glm::vec3 getCameraFront() const = 0;
    virtual glm::vec3 getCameraRight() const = 0;

    // ── 玩家控制 ──────────────────────────────
    virtual void setPlayerSpeed(float speed) = 0;
    virtual void setPlayerSensitivity(float sens) = 0;
    virtual void setPlayerDirection(const glm::vec3& front, const glm::vec3& right) = 0;

    // ── 飞行模式 ──────────────────────────────
    virtual void updateFlight(float dt, bool spaceHeld, bool shiftHeld) = 0;
    virtual bool isPlayerFlying() const = 0;
    virtual void setPlayerFlying(bool flying) = 0;

    // ── 相机同步（ECS → 旧 Camera 类） ──────
    virtual void syncCamera(class Camera& camera) = 0;

    // ── 输入系统 ──────────────────────────────
    virtual class InputSystem* getInputSystem() const = 0;

    // ── 网络 ──────────────────────────────────
    virtual bool connectToServer(const std::string& host, uint16_t port) = 0;
    virtual void disconnectFromServer() = 0;
    virtual bool isConnectedToServer() const = 0;

    // 远程玩家（注：Renderer 据此同步 GPU 模型）
    virtual std::vector<RemotePlayerInfo> getRemotePlayers() const = 0;
    virtual class owengine::client::NetworkSystem* getNetworkSystem() const = 0;

    // ── 服务端发现 ────────────────────────────
    virtual std::vector<DiscoveredServerInfo> getDiscoveredServers() = 0;
    virtual bool startServerDiscovery() = 0;
    virtual void stopServerDiscovery() = 0;

    // ── 开发者面板逃生口（谨慎使用） ─────────
    virtual entt::registry* getRegistry() = 0;
};

} // namespace ecs
} // namespace owengine
