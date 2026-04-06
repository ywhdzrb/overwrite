# 服务端网络系统

服务端网络系统处理客户端连接和消息路由。

---

## WebSocketGameServer

WebSocket 游戏服务器，处理客户端连接、消息路由和状态同步。

### 类定义

```cpp
namespace vgame::server {

class WebSocketGameServer {
public:
    // 回调类型
    using OnPlayerConnectCallback = std::function<void(const std::string& clientId)>;
    using OnPlayerDisconnectCallback = std::function<void(const std::string& clientId)>;
    using OnMessageCallback = std::function<void(const std::string& clientId, const json& message)>;
    
    WebSocketGameServer(uint16_t port);
    ~WebSocketGameServer();
    
    // 启动/停止
    void start();
    void stop();
    void run();  // 阻塞运行
    
    // 发送消息
    void sendToClient(const std::string& clientId, const json& message);
    void broadcast(const json& message);
    void broadcastExcept(const std::string& excludeClientId, const json& message);
    
    // 状态同步
    void broadcastState();  // 广播所有玩家状态
    
    // 回调设置
    void setOnPlayerConnect(OnPlayerConnectCallback callback);
    void setOnPlayerDisconnect(OnPlayerDisconnectCallback callback);
    void setOnMessage(OnMessageCallback callback);
    
    // 获取服务器世界
    ServerWorld& getWorld();
    
    // 获取连接数
    size_t getConnectionCount() const;
};

} // namespace vgame::server
```

### 使用示例

#### 基本服务器

```cpp
using namespace vgame::server;

int main() {
    WebSocketGameServer server(9002);
    
    // 设置回调
    server.setOnPlayerConnect([](const std::string& clientId) {
        std::cout << "Player connected: " << clientId << std::endl;
    });
    
    server.setOnPlayerDisconnect([](const std::string& clientId) {
        std::cout << "Player disconnected: " << clientId << std::endl;
    });
    
    server.setOnMessage([](const std::string& clientId, const json& msg) {
        std::cout << "Message from " << clientId << ": " << msg << std::endl;
        // 处理消息
    });
    
    // 启动服务器
    server.start();
    std::cout << "Server started on port 9002" << std::endl;
    
    // 阻塞运行
    server.run();
    
    return 0;
}
```

#### 与 ServerWorld 集成

```cpp
WebSocketGameServer server(9002);

// 设置玩家连接处理
server.setOnPlayerConnect([&server](const std::string& clientId) {
    // 在 ECS 世界中创建玩家实体
    auto& world = server.getWorld();
    entt::entity player = world.onPlayerConnect(clientId);
    
    // 发送连接确认
    json response;
    response["type"] = "connected";
    response["clientId"] = clientId;
    
    auto& transform = world.registry().get<ecs::TransformComponent>(player);
    response["position"] = {transform.position.x, transform.position.y, transform.position.z};
    
    server.sendToClient(clientId, response);
    
    // 广播玩家加入
    json joinMsg;
    joinMsg["type"] = "playerJoin";
    joinMsg["player"]["clientId"] = clientId;
    joinMsg["player"]["position"] = {transform.position.x, transform.position.y, transform.position.z};
    server.broadcastExcept(clientId, joinMsg);
});

// 设置玩家断开处理
server.setOnPlayerDisconnect([&server](const std::string& clientId) {
    server.getWorld().onPlayerDisconnect(clientId);
    
    // 广播玩家离开
    json msg;
    msg["type"] = "playerLeave";
    msg["clientId"] = clientId;
    server.broadcast(msg);
});

// 设置消息处理
server.setOnMessage([&server](const std::string& clientId, const json& msg) {
    auto& world = server.getWorld();
    
    if (msg["type"] == "input") {
        // 应用输入到玩家
        ecs::InputStateComponent input;
        input.moveForward = msg.value("forward", false);
        input.moveBackward = msg.value("backward", false);
        input.moveLeft = msg.value("left", false);
        input.moveRight = msg.value("right", false);
        input.jump = msg.value("jump", false);
        input.sprint = msg.value("sprint", false);
        input.mouseDeltaX = msg.value("mouseDeltaX", 0.0f);
        input.mouseDeltaY = msg.value("mouseDeltaY", 0.0f);
        
        glm::vec3 cameraFront(
            msg["cameraFront"][0].get<float>(),
            msg["cameraFront"][1].get<float>(),
            msg["cameraFront"][2].get<float>()
        );
        glm::vec3 cameraRight(
            msg["cameraRight"][0].get<float>(),
            msg["cameraRight"][1].get<float>(),
            msg["cameraRight"][2].get<float>()
        );
        
        world.applyPlayerInput(clientId, input, cameraFront, cameraRight);
    }
});

// 游戏循环
void gameLoop(float deltaTime) {
    server.getWorld().update(deltaTime);
    server.broadcastState();
}
```

---

## ServerWorld

服务器端 ECS 世界，扩展共享 World。

### 类定义

```cpp
class ServerWorld : public ecs::World {
public:
    ServerWorld();
    ~ServerWorld() = default;
    
    // 玩家管理
    entt::entity onPlayerConnect(const std::string& clientId);
    void onPlayerDisconnect(const std::string& clientId);
    entt::entity getPlayerByClientId(const std::string& clientId) const;
    const std::unordered_map<std::string, entt::entity>& getConnectedPlayers() const;
    
    // 服务器更新
    void update(float deltaTime);
    
    // 状态快照
    struct PlayerSnapshot {
        std::string clientId;
        glm::vec3 position;
        float yaw;
        float pitch;
        bool isJumping;
        bool isGrounded;
    };
    
    std::vector<PlayerSnapshot> getPlayersSnapshot() const;
    
    // 应用客户端输入
    void applyPlayerInput(const std::string& clientId,
                         const ecs::InputStateComponent& input,
                         const glm::vec3& cameraFront,
                         const glm::vec3& cameraRight);
    
    // 设置玩家位置
    void setPlayerPosition(const std::string& clientId, const glm::vec3& position);
    
    // 游戏循环控制
    float getDeltaTime() const;
    
private:
    std::unordered_map<std::string, entt::entity> connectedPlayers_;
    ecs::MovementSystem movementSystem_;
    ecs::PhysicsSystem physicsSystem_;
    float lastDeltaTime_{0.016f};
    std::chrono::high_resolution_clock::time_point lastUpdateTime_;
};
```

### 使用示例

```cpp
ServerWorld world;

// 玩家连接
entt::entity player = world.onPlayerConnect("player_123");
world.setPlayerPosition("player_123", glm::vec3(0.0f, 0.0f, 10.0f));

// 应用输入
ecs::InputStateComponent input;
input.moveForward = true;
world.applyPlayerInput("player_123", input, cameraFront, cameraRight);

// 更新
world.update(deltaTime);

// 获取快照
auto snapshot = world.getPlayersSnapshot();
for (const auto& player : snapshot) {
    std::cout << player.clientId << ": " << player.position.x << std::endl;
}

// 玩家断开
world.onPlayerDisconnect("player_123");
```

---

## 状态同步

### 广播状态

```cpp
void WebSocketGameServer::broadcastState() {
    auto snapshot = world_.getPlayersSnapshot();
    
    json msg;
    msg["type"] = "state";
    msg["players"] = json::array();
    
    for (const auto& player : snapshot) {
        json p;
        p["clientId"] = player.clientId;
        p["position"] = {player.position.x, player.position.y, player.position.z};
        p["yaw"] = player.yaw;
        p["pitch"] = player.pitch;
        p["isJumping"] = player.isJumping;
        p["isGrounded"] = player.isGrounded;
        p["isMoving"] = glm::length(player.position) > 0.01f;
        
        msg["players"].push_back(p);
    }
    
    broadcast(msg);
}
```

---

## 完整服务器示例

```cpp
#include "server/websocket_server.h"
#include <iostream>
#include <chrono>

using namespace vgame::server;

int main() {
    WebSocketGameServer server(9002);
    
    // 设置回调
    server.setOnPlayerConnect([&server](const std::string& clientId) {
        std::cout << "Player connected: " << clientId << std::endl;
        
        auto& world = server.getWorld();
        entt::entity player = world.onPlayerConnect(clientId);
        world.setPlayerPosition(clientId, glm::vec3(0.0f, 0.0f, 10.0f));
        
        // 发送连接确认
        json msg;
        msg["type"] = "connected";
        msg["clientId"] = clientId;
        msg["position"] = {0.0f, 0.0f, 10.0f};
        server.sendToClient(clientId, msg);
        
        // 广播玩家加入
        json joinMsg;
        joinMsg["type"] = "playerJoin";
        joinMsg["player"]["clientId"] = clientId;
        joinMsg["player"]["position"] = {0.0f, 0.0f, 10.0f};
        server.broadcastExcept(clientId, joinMsg);
    });
    
    server.setOnPlayerDisconnect([&server](const std::string& clientId) {
        std::cout << "Player disconnected: " << clientId << std::endl;
        server.getWorld().onPlayerDisconnect(clientId);
        
        json msg;
        msg["type"] = "playerLeave";
        msg["clientId"] = clientId;
        server.broadcast(msg);
    });
    
    server.setOnMessage([&server](const std::string& clientId, const json& msg) {
        if (msg["type"] == "input") {
            ecs::InputStateComponent input;
            input.moveForward = msg.value("forward", false);
            input.moveBackward = msg.value("backward", false);
            input.moveLeft = msg.value("left", false);
            input.moveRight = msg.value("right", false);
            input.jump = msg.value("jump", false);
            input.sprint = msg.value("sprint", false);
            input.mouseDeltaX = msg.value("mouseDeltaX", 0.0f);
            input.mouseDeltaY = msg.value("mouseDeltaY", 0.0f);
            
            glm::vec3 front(msg["cameraFront"][0], msg["cameraFront"][1], msg["cameraFront"][2]);
            glm::vec3 right(msg["cameraRight"][0], msg["cameraRight"][1], msg["cameraRight"][2]);
            
            server.getWorld().applyPlayerInput(clientId, input, front, right);
        }
    });
    
    // 启动服务器
    server.start();
    std::cout << "Server started on port 9002" << std::endl;
    
    // 游戏循环
    auto lastTime = std::chrono::high_resolution_clock::now();
    const float tickRate = 1.0f / 60.0f;  // 60 Hz
    
    while (true) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        
        if (deltaTime >= tickRate) {
            server.getWorld().update(deltaTime);
            server.broadcastState();
            lastTime = currentTime;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    server.stop();
    return 0;
}
```
