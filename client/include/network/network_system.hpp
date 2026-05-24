#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace owengine {
namespace client {

using json = nlohmann::json;

/**
 * @brief 远程玩家数据
 */
struct RemotePlayer {
    std::string clientId;
    glm::vec3 position{0.0f};
    glm::vec3 targetPosition{0.0f};  // 插值目标
    glm::vec3 lastPosition{0.0f};    // 上一帧位置（用于检测移动）
    float yaw{0.0f};
    float targetYaw{0.0f};
    float pitch{0.0f};
    float targetPitch{0.0f};
    float moveYaw{0.0f};             // 移动方向（面向方向）
    bool isJumping{false};
    bool isGrounded{true};
    bool isMoving{false};            // 是否在移动
    bool active{true};
};

/**
 * @brief 网络系统状态
 */
enum class NetworkState {
    Disconnected,
    Connecting,
    Connected,
    Error
};

/**
 * @brief 客户端网络系统
 * 
 * 处理与服务器的 WebSocket 连接：
 * - 连接管理
 * - 输入同步
 * - 状态接收
 * - 远程玩家插值
 */
class NetworkSystem {
public:
    using OnConnectedCallback = std::function<void(const std::string& clientId)>;
    using OnDisconnectedCallback = std::function<void()>;
    using OnErrorCallback = std::function<void(const std::string& error)>;
    using OnPlayerJoinCallback = std::function<void(const RemotePlayer& player)>;
    using OnPlayerLeaveCallback = std::function<void(const std::string& clientId)>;
    
    NetworkSystem();
    ~NetworkSystem();
    
    // 连接管理
    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    bool isConnected() const { return state_ == NetworkState::Connected; }
    NetworkState getState() const { return state_; }
    const std::string& getClientId() const { return clientId_; }
    const std::string& getLastError() const { return lastError_; }
    
    // 发送输入状态
    void sendInput(
        bool moveForward, bool moveBackward, bool moveLeft, bool moveRight,
        bool jump, bool sprint, bool spaceHeld, bool shiftHeld,
        float mouseDeltaX, float mouseDeltaY,
        const glm::vec3& cameraFront, const glm::vec3& cameraRight
    );
    
    // 发送心跳
    void sendPing();
    
    // 更新（处理消息队列，插值远程玩家）
    void update(float deltaTime);
    
    // 获取远程玩家
    const std::unordered_map<std::string, RemotePlayer>& getRemotePlayers() const {
        return remotePlayers_;
    }
    
    // 回调设置
    void setOnConnected(OnConnectedCallback callback) { onConnected_ = callback; }
    void setOnDisconnected(OnDisconnectedCallback callback) { onDisconnected_ = callback; }
    void setOnError(OnErrorCallback callback) { onError_ = callback; }
    void setOnPlayerJoin(OnPlayerJoinCallback callback) { onPlayerJoin_ = callback; }
    void setOnPlayerLeave(OnPlayerLeaveCallback callback) { onPlayerLeave_ = callback; }
    
    // 插值设置
    void setInterpolationSpeed(float speed) { interpolationSpeed_ = speed; }
    
private:
    // 内部实现类（PIMPL 模式隐藏 ixwebsocket 细节）
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    // 状态
    NetworkState state_{NetworkState::Disconnected};
    std::string clientId_;
    std::string lastError_;
    
    // 远程玩家（主线程访问）
    std::unordered_map<std::string, RemotePlayer> remotePlayers_;
    
    // 插值
    float interpolationSpeed_{10.0f};
    
    // 回调
    OnConnectedCallback onConnected_;
    OnDisconnectedCallback onDisconnected_;
    OnErrorCallback onError_;
    OnPlayerJoinCallback onPlayerJoin_;
    OnPlayerLeaveCallback onPlayerLeave_;
    
    // 远程玩家插值
    void interpolateRemotePlayers(float deltaTime);
    
    // 内部消息处理
    void processMessages();
    void handleMessage(const json& message);
    void sendMessage(const json& message);
};

} // namespace client
} // namespace owengine
