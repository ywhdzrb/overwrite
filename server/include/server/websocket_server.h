#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include "server/server_world.h"
#include "network/server_discovery.h"
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <mutex>

namespace owengine {
namespace server {

using json = nlohmann::json;

/**
 * @brief WebSocket 游戏服务器
 * 
 * 使用 ixwebsocket 实现，处理客户端连接、消息路由、状态同步
 */
class WebSocketGameServer {
public:
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
    void setOnPlayerConnect(OnPlayerConnectCallback callback) { onPlayerConnect_ = callback; }
    void setOnPlayerDisconnect(OnPlayerDisconnectCallback callback) { onPlayerDisconnect_ = callback; }
    void setOnMessage(OnMessageCallback callback) { onMessage_ = callback; }
    
    // 获取服务器世界
    ServerWorld& getWorld() { return world_; }
    
    // 获取连接数
    size_t getConnectionCount() const;
    
private:
    // 连接信息
    struct ConnectionInfo {
        std::shared_ptr<ix::WebSocket> webSocket;
        std::string clientId;
    };
    
    // 生成客户端 ID
    std::string generateClientId() const;
    
    ix::WebSocketServer server_;
    ServerWorld world_;
    std::unique_ptr<ServerDiscoveryBroadcaster> discovery_;
    std::atomic<bool> running_{false};
    
    // 连接映射
    std::unordered_map<std::string, ConnectionInfo> connections_;
    mutable std::mutex connectionsMutex_;
    
    // 回调
    OnPlayerConnectCallback onPlayerConnect_;
    OnPlayerDisconnectCallback onPlayerDisconnect_;
    OnMessageCallback onMessage_;
    
    uint16_t port_;
};

} // namespace server
} // namespace owengine

#endif // WEBSOCKET_SERVER_H
