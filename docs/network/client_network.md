# 客户端网络系统

客户端网络系统处理与服务器的 WebSocket 连接。

---

## RemotePlayer

远程玩家数据结构。

```cpp
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
```

---

## NetworkState

网络状态枚举。

```cpp
enum class NetworkState {
    Disconnected,  // 断开连接
    Connecting,    // 连接中
    Connected,     // 已连接
    Error          // 错误
};
```

---

## NetworkSystem

客户端网络系统，处理与服务器的通信。

### 类定义

```cpp
namespace vgame::client {

class NetworkSystem {
public:
    // 回调类型
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
    bool isConnected() const;
    NetworkState getState() const;
    const std::string& getClientId() const;
    const std::string& getLastError() const;
    
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
    const std::unordered_map<std::string, RemotePlayer>& getRemotePlayers() const;
    
    // 回调设置
    void setOnConnected(OnConnectedCallback callback);
    void setOnDisconnected(OnDisconnectedCallback callback);
    void setOnError(OnErrorCallback callback);
    void setOnPlayerJoin(OnPlayerJoinCallback callback);
    void setOnPlayerLeave(OnPlayerLeaveCallback callback);
    
    // 插值设置
    void setInterpolationSpeed(float speed);
};

} // namespace vgame::client
```

### 使用示例

#### 基本连接

```cpp
using namespace vgame::client;

auto network = std::make_unique<NetworkSystem>();

// 设置回调
network->setOnConnected([](const std::string& clientId) {
    std::cout << "Connected as " << clientId << std::endl;
});

network->setOnDisconnected([]() {
    std::cout << "Disconnected from server" << std::endl;
});

network->setOnError([](const std::string& error) {
    std::cerr << "Network error: " << error << std::endl;
});

// 连接到服务器
if (network->connect("localhost", 9002)) {
    std::cout << "Connecting..." << std::endl;
}

// 主循环
void gameLoop(float deltaTime) {
    network->update(deltaTime);
}
```

#### 发送输入

```cpp
void sendPlayerInput() {
    network->sendInput(
        input.isForwardPressed(),
        input.isBackPressed(),
        input.isLeftPressed(),
        input.isRightPressed(),
        input.isJumpPressed(),
        input.isSprintPressed(),
        input.isSpaceHeld(),
        input.isShiftHeld(),
        mouseDeltaX,
        mouseDeltaY,
        camera.getFront(),
        camera.getRight()
    );
}
```

#### 处理远程玩家

```cpp
network->setOnPlayerJoin([](const RemotePlayer& player) {
    std::cout << "Player joined: " << player.clientId << std::endl;
    // 创建远程玩家模型
});

network->setOnPlayerLeave([](const std::string& clientId) {
    std::cout << "Player left: " << clientId << std::endl;
    // 移除远程玩家模型
});

// 渲染远程玩家
void renderRemotePlayers() {
    const auto& players = network->getRemotePlayers();
    
    for (const auto& [clientId, player] : players) {
        if (player.active) {
            // 使用插值后的位置
            renderPlayerModel(player.position, player.yaw, player.isMoving);
        }
    }
}
```

---

## 消息协议

### 客户端发送

#### 输入同步

```json
{
    "type": "input",
    "forward": true,
    "backward": false,
    "left": false,
    "right": true,
    "jump": false,
    "sprint": true,
    "mouseDeltaX": 1.5,
    "mouseDeltaY": -0.5,
    "cameraFront": [0.0, 0.0, -1.0],
    "cameraRight": [1.0, 0.0, 0.0]
}
```

#### 心跳

```json
{
    "type": "ping",
    "timestamp": 1234567890
}
```

### 服务端发送

#### 连接确认

```json
{
    "type": "connected",
    "clientId": "player_123",
    "position": [0.0, 0.0, 10.0]
}
```

#### 玩家列表

```json
{
    "type": "playerList",
    "players": [
        {
            "clientId": "player_456",
            "position": [5.0, 0.0, 5.0],
            "yaw": 45.0,
            "isMoving": true
        }
    ]
}
```

#### 玩家加入

```json
{
    "type": "playerJoin",
    "player": {
        "clientId": "player_789",
        "position": [0.0, 0.0, 0.0],
        "yaw": 0.0
    }
}
```

#### 玩家离开

```json
{
    "type": "playerLeave",
    "clientId": "player_789"
}
```

#### 状态同步

```json
{
    "type": "state",
    "players": [
        {
            "clientId": "player_456",
            "position": [5.1, 0.0, 5.2],
            "yaw": 46.0,
            "pitch": 0.0,
            "isJumping": false,
            "isGrounded": true,
            "isMoving": true
        }
    ]
}
```

---

## 插值系统

客户端使用插值来平滑远程玩家的移动。

### 插值参数

```cpp
// 设置插值速度（默认 10.0）
network->setInterpolationSpeed(10.0f);
```

### 插值逻辑

```cpp
void NetworkSystem::interpolateRemotePlayers(float deltaTime) {
    for (auto& [clientId, player] : remotePlayers_) {
        // 位置插值
        player.position = glm::mix(
            player.position,
            player.targetPosition,
            interpolationSpeed_ * deltaTime
        );
        
        // 旋转插值
        player.yaw = glm::mix(player.yaw, player.targetYaw, interpolationSpeed_ * deltaTime);
        player.pitch = glm::mix(player.pitch, player.targetPitch, interpolationSpeed_ * deltaTime);
        
        // 检测移动状态
        float moveDistance = glm::distance(player.position, player.lastPosition);
        player.isMoving = moveDistance > 0.01f;
        player.lastPosition = player.position;
    }
}
```

---

## 完整示例

```cpp
#include "network/network_system.h"
#include <iostream>

using namespace vgame::client;

class GameClient {
public:
    GameClient() {
        // 设置回调
        network_.setOnConnected([this](const std::string& clientId) {
            clientId_ = clientId;
            std::cout << "Connected as " << clientId << std::endl;
        });
        
        network_.setOnDisconnected([this]() {
            std::cout << "Disconnected" << std::endl;
        });
        
        network_.setOnPlayerJoin([this](const RemotePlayer& player) {
            std::cout << "Player joined: " << player.clientId << std::endl;
            // 创建远程玩家实体
        });
        
        network_.setOnPlayerLeave([this](const std::string& clientId) {
            std::cout << "Player left: " << clientId << std::endl;
            // 移除远程玩家实体
        });
    }
    
    bool connect(const std::string& host, uint16_t port) {
        return network_.connect(host, port);
    }
    
    void update(float deltaTime) {
        network_.update(deltaTime);
        
        // 发送输入
        if (network_.isConnected()) {
            sendInput();
        }
        
        // 渲染远程玩家
        renderRemotePlayers();
    }
    
private:
    NetworkSystem network_;
    std::string clientId_;
    
    void sendInput() {
        network_.sendInput(
            isKeyPressed('W'),
            isKeyPressed('S'),
            isKeyPressed('A'),
            isKeyPressed('D'),
            isKeyPressed(' '),
            isKeyPressed(GLFW_KEY_LEFT_SHIFT),
            false,
            false,
            mouseDeltaX_,
            mouseDeltaY_,
            camera_.getFront(),
            camera_.getRight()
        );
    }
    
    void renderRemotePlayers() {
        const auto& players = network_.getRemotePlayers();
        for (const auto& [id, player] : players) {
            // 渲染玩家模型
        }
    }
};

int main() {
    GameClient client;
    
    if (!client.connect("localhost", 9002)) {
        std::cerr << "Failed to connect" << std::endl;
        return 1;
    }
    
    // 主循环
    while (running) {
        float deltaTime = getDeltaTime();
        client.update(deltaTime);
    }
    
    return 0;
}
```
