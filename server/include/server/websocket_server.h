#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include "server/server_world.h"
#include "network/server_discovery.h"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>

namespace vgame {
namespace server {

using WebSocketServer = websocketpp::server<websocketpp::config::asio>;
using ConnectionHdl = websocketpp::connection_hdl;
using MessagePtr = websocketpp::server<websocketpp::config::asio>::message_ptr;
using json = nlohmann::json;

/**
 * @brief WebSocket 游戏服务器
 * 
 * 处理客户端连接、消息路由、状态同步
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
    size_t getConnectionCount() const { return connections_.size(); }
    
private:
    // WebSocket 回调
    void onOpen(ConnectionHdl hdl);
    void onClose(ConnectionHdl hdl);
    void onMessage(ConnectionHdl hdl, MessagePtr msg);
    
    // 连接管理
    std::string hdlToClientId(ConnectionHdl hdl) const;
    ConnectionHdl clientIdToHdl(const std::string& clientId) const;
    
    WebSocketServer server_;
    ServerWorld world_;
    std::unique_ptr<ServerDiscoveryBroadcaster> discovery_;
    std::thread serverThread_;
    std::atomic<bool> running_{false};
    
    // 连接映射
    std::unordered_map<std::string, ConnectionHdl> connections_;
    std::map<ConnectionHdl, std::string, std::owner_less<ConnectionHdl>> reverseConnections_;
    mutable std::mutex connectionsMutex_;
    
    // 回调
    OnPlayerConnectCallback onPlayerConnect_;
    OnPlayerDisconnectCallback onPlayerDisconnect_;
    OnMessageCallback onMessage_;
    
    uint16_t port_;
};

} // namespace server
} // namespace vgame

#endif // WEBSOCKET_SERVER_H
