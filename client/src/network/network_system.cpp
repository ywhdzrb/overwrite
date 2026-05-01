#include "network/network_system.h"
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <iostream>
#include <queue>
#include <mutex>

namespace owengine {
namespace client {

/**
 * @brief NetworkSystem 内部实现（封装 ix::WebSocket）
 */
class NetworkSystem::Impl {
public:
    ix::WebSocket webSocket;
    std::atomic<bool> connected{false};

    // 消息队列（网络线程 → 主线程）
    std::queue<json> incomingMessages;
    std::mutex incomingMutex;
};

NetworkSystem::NetworkSystem()
    : impl_(std::make_unique<Impl>()) {
    // ix::initNetSystem 在每个进程只需调用一次（Windows WSAStartup，Linux 无操作）
    static bool netInitDone = false;
    if (!netInitDone) {
        ix::initNetSystem();
        netInitDone = true;
    }
}

NetworkSystem::~NetworkSystem() {
    disconnect();
}

bool NetworkSystem::connect(const std::string& host, uint16_t port) {
    if (state_ == NetworkState::Connected || state_ == NetworkState::Connecting) {
        return false;
    }

    // 重新创建 Impl 确保干净状态（ixwebsocket 不支持重复 start/stop 同一实例）
    impl_ = std::make_unique<Impl>();

    state_ = NetworkState::Connecting;
    lastError_.clear();

    try {
        std::string uri = "ws://" + host + ":" + std::to_string(port);

        impl_->webSocket.setUrl(uri);
        // 禁用自动重连——由上层逻辑控制连接生命周期
        impl_->webSocket.disableAutomaticReconnection();

        // 设置统一消息回调（在 ixwebsocket 后台线程中调用）
        impl_->webSocket.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            switch (msg->type) {
                case ix::WebSocketMessageType::Open:
                    impl_->connected = true;
                    state_ = NetworkState::Connected;
                    std::cout << "[NetworkSystem] 已连接到服务器" << std::endl;
                    break;

                case ix::WebSocketMessageType::Close:
                    impl_->connected = false;
                    state_ = NetworkState::Disconnected;
                    std::cout << "[NetworkSystem] 已断开连接" << std::endl;
                    if (onDisconnected_) {
                        onDisconnected_();
                    }
                    break;

                case ix::WebSocketMessageType::Error:
                    impl_->connected = false;
                    state_ = NetworkState::Error;
                    lastError_ = "连接错误: " + msg->errorInfo.reason;
                    std::cerr << "[NetworkSystem] " << lastError_ << std::endl;
                    if (onError_) {
                        onError_(lastError_);
                    }
                    break;

                case ix::WebSocketMessageType::Message:
                    try {
                        json message = json::parse(msg->str);
                        std::lock_guard<std::mutex> lock(impl_->incomingMutex);
                        impl_->incomingMessages.push(message);
                    } catch (const json::parse_error& e) {
                        std::cerr << "[NetworkSystem] JSON 解析错误: " << e.what() << std::endl;
                    }
                    break;
            }
        });

        // 启动后台线程（内部管理，stop() 时自动 join）
        impl_->webSocket.start();

        return true;

    } catch (const std::exception& e) {
        state_ = NetworkState::Error;
        lastError_ = std::string("连接异常: ") + e.what();
        std::cerr << "[NetworkSystem] " << lastError_ << std::endl;
        return false;
    }
}

void NetworkSystem::disconnect() {
    // stop() 同步关闭连接并等待后台线程退出，无需手动 join
    impl_->webSocket.stop();

    impl_->connected = false;
    state_ = NetworkState::Disconnected;
}

void NetworkSystem::sendInput(
    bool moveForward, bool moveBackward, bool moveLeft, bool moveRight,
    bool jump, bool sprint, bool spaceHeld, bool shiftHeld,
    float mouseDeltaX, float mouseDeltaY,
    const glm::vec3& cameraFront, const glm::vec3& cameraRight
) {
    if (!impl_->connected) return;

    json message = {
        {"type", "input"},
        {"moveForward", moveForward},
        {"moveBackward", moveBackward},
        {"moveLeft", moveLeft},
        {"moveRight", moveRight},
        {"jump", jump},
        {"sprint", sprint},
        {"spaceHeld", spaceHeld},
        {"shiftHeld", shiftHeld},
        {"mouseDeltaX", mouseDeltaX},
        {"mouseDeltaY", mouseDeltaY},
        {"cameraFront", {cameraFront.x, cameraFront.y, cameraFront.z}},
        {"cameraRight", {cameraRight.x, cameraRight.y, cameraRight.z}}
    };

    sendMessage(message);
}

void NetworkSystem::sendPing() {
    if (!impl_->connected) return;

    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration<double>(now.time_since_epoch()).count();

    json message = {
        {"type", "ping"},
        {"time", timestamp}
    };

    sendMessage(message);
}

void NetworkSystem::sendMessage(const json& message) {
    if (!impl_->connected) return;

    auto sendInfo = impl_->webSocket.send(message.dump());

    if (!sendInfo.success) {
        std::cerr << "[NetworkSystem] 发送失败" << std::endl;
    }
}

void NetworkSystem::update(float deltaTime) {
    // 处理接收到的消息
    processMessages();

    // 插值远程玩家
    interpolateRemotePlayers(deltaTime);
}

void NetworkSystem::processMessages() {
    std::queue<json> messages;
    {
        std::lock_guard<std::mutex> lock(impl_->incomingMutex);
        messages = std::move(impl_->incomingMessages);
        impl_->incomingMessages = std::queue<json>();
    }

    while (!messages.empty()) {
        handleMessage(messages.front());
        messages.pop();
    }
}

void NetworkSystem::handleMessage(const json& message) {
    std::string type = message.value("type", "");

    // 只打印重要消息类型
    if (type != "state" && type != "pong") {
        std::cout << "[NetworkSystem] 收到消息类型: " << type << std::endl;
    }

    if (type == "welcome") {
        // 连接成功，获取客户端 ID
        clientId_ = message.value("clientId", "");
        std::cout << "[NetworkSystem] 客户端 ID: " << clientId_ << std::endl;

        // 解析现有玩家列表
        if (message.contains("players") && message["players"].is_array()) {
            for (const auto& playerData : message["players"]) {
                RemotePlayer player;
                player.clientId = playerData.value("clientId", "");
                if (playerData.contains("position") && playerData["position"].is_array()) {
                    player.position.x = playerData["position"][0].get<float>();
                    player.position.y = playerData["position"][1].get<float>();
                    player.position.z = playerData["position"][2].get<float>();
                    player.targetPosition = player.position;
                }
                player.yaw = playerData.value("yaw", 0.0f);
                player.targetYaw = player.yaw;
                player.pitch = playerData.value("pitch", 0.0f);
                player.targetPitch = player.pitch;
                player.active = true;

                if (!player.clientId.empty()) {
                    remotePlayers_[player.clientId] = player;
                    if (onPlayerJoin_) {
                        onPlayerJoin_(player);
                    }
                }
            }
        }

        if (onConnected_) {
            onConnected_(clientId_);
        }

    } else if (type == "state") {
        // 如果还没收到 welcome 消息，跳过 state 处理（避免把自己添加为远程玩家）
        if (clientId_.empty()) {
            return;
        }

        // 更新玩家状态
        if (message.contains("players") && message["players"].is_array()) {
            for (const auto& playerData : message["players"]) {
                std::string playerId = playerData.value("clientId", "");
                if (playerId.empty() || playerId == clientId_) continue;  // 跳过自己

                auto it = remotePlayers_.find(playerId);
                if (it != remotePlayers_.end()) {
                    // 更新现有玩家（插值目标）
                    if (playerData.contains("position") && playerData["position"].is_array()) {
                        it->second.targetPosition.x = playerData["position"][0].get<float>();
                        it->second.targetPosition.y = playerData["position"][1].get<float>();
                        it->second.targetPosition.z = playerData["position"][2].get<float>();
                    }
                    it->second.targetYaw = playerData.value("yaw", it->second.yaw);
                    it->second.targetPitch = playerData.value("pitch", it->second.pitch);
                    it->second.isJumping = playerData.value("isJumping", false);
                    it->second.isGrounded = playerData.value("isGrounded", true);
                } else {
                    // 玩家不在列表中，自动添加（可能是 playerJoin 消息丢失）
                    RemotePlayer player;
                    player.clientId = playerId;
                    if (playerData.contains("position") && playerData["position"].is_array()) {
                        player.position.x = playerData["position"][0].get<float>();
                        player.position.y = playerData["position"][1].get<float>();
                        player.position.z = playerData["position"][2].get<float>();
                        player.targetPosition = player.position;
                    }
                    player.yaw = playerData.value("yaw", 0.0f);
                    player.targetYaw = player.yaw;
                    player.pitch = playerData.value("pitch", 0.0f);
                    player.targetPitch = player.pitch;
                    player.active = true;

                    remotePlayers_[playerId] = player;
                    std::cout << "[NetworkSystem] 从状态同步添加玩家: " << playerId << std::endl;
                    if (onPlayerJoin_) {
                        onPlayerJoin_(player);
                    }
                }
            }
        }

    } else if (type == "playerJoin") {
        // 新玩家加入
        RemotePlayer player;
        player.clientId = message.value("clientId", "");
        if (message.contains("position") && message["position"].is_array()) {
            player.position.x = message["position"][0].get<float>();
            player.position.y = message["position"][1].get<float>();
            player.position.z = message["position"][2].get<float>();
            player.targetPosition = player.position;
        }
        player.yaw = message.value("yaw", 0.0f);
        player.targetYaw = player.yaw;
        player.pitch = message.value("pitch", 0.0f);
        player.targetPitch = player.pitch;
        player.active = true;

        if (!player.clientId.empty() && player.clientId != clientId_) {
            remotePlayers_[player.clientId] = player;
            std::cout << "[NetworkSystem] 玩家加入: " << player.clientId << std::endl;
            if (onPlayerJoin_) {
                onPlayerJoin_(player);
            }
        }

    } else if (type == "playerLeave") {
        // 玩家离开
        std::string playerId = message.value("clientId", "");
        if (!playerId.empty() && playerId != clientId_) {
            auto it = remotePlayers_.find(playerId);
            if (it != remotePlayers_.end()) {
                std::cout << "[NetworkSystem] 玩家离开: " << playerId << std::endl;
                if (onPlayerLeave_) {
                    onPlayerLeave_(playerId);
                }
                remotePlayers_.erase(it);
            }
        }

    } else if (type == "pong") {
        // 心跳响应，可用于计算延迟
    }
}

void NetworkSystem::interpolateRemotePlayers(float deltaTime) {
    for (auto& [clientId, player] : remotePlayers_) {
        if (!player.active) continue;

        // 保存上一帧位置
        player.lastPosition = player.position;

        // 位置插值
        player.position = glm::mix(player.position, player.targetPosition,
                                   std::min(1.0f, interpolationSpeed_ * deltaTime));

        // 角度插值
        float yawDiff = player.targetYaw - player.yaw;
        // 处理角度跨越 -180/180
        while (yawDiff > 180.0f) yawDiff -= 360.0f;
        while (yawDiff < -180.0f) yawDiff += 360.0f;
        player.yaw += yawDiff * std::min(1.0f, interpolationSpeed_ * deltaTime);

        player.pitch += (player.targetPitch - player.pitch) * std::min(1.0f, interpolationSpeed_ * deltaTime);

        // 检测是否在移动，并计算移动方向
        glm::vec3 moveDir = player.position - player.lastPosition;
        float moveDistance = glm::length(moveDir);
        player.isMoving = moveDistance > 0.001f;

        // 跳跃状态：使用服务器同步的状态，或者通过 Y 坐标变化趋势判断
        float yVelocity = player.position.y - player.lastPosition.y;
        if (std::abs(yVelocity) > 0.01f) {
            player.isJumping = true;
        } else if (player.isGrounded) {
            player.isJumping = false;
        }

        if (player.isMoving) {
            // 计算移动方向的角度
            moveDir.y = 0.0f;
            if (glm::length(moveDir) > 0.0001f) {
                moveDir = glm::normalize(moveDir);
                // 注意：模型的前方是 -Z，所以用 -moveDir
                player.moveYaw = glm::degrees(atan2(-moveDir.x, -moveDir.z));
            }
        }
    }
}

} // namespace client
} // namespace owengine
