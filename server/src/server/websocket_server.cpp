#include "server/websocket_server.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace vgame {
namespace server {

WebSocketGameServer::WebSocketGameServer(uint16_t port)
    : port_(port) {
    // 初始化 WebSocket 服务器
    server_.init_asio();
    
    // 设置回调
    server_.set_open_handler([this](ConnectionHdl hdl) { onOpen(hdl); });
    server_.set_close_handler([this](ConnectionHdl hdl) { onClose(hdl); });
    server_.set_message_handler([this](ConnectionHdl hdl, MessagePtr msg) { onMessage(hdl, msg); });
    
    std::cout << "[WebSocketServer] 初始化完成，端口: " << port_ << std::endl;
}

WebSocketGameServer::~WebSocketGameServer() {
    stop();
}

void WebSocketGameServer::start() {
    if (running_) return;
    
    running_ = true;
    
    try {
        server_.listen(port_);
        server_.start_accept();
        std::cout << "[WebSocketServer] 服务器启动，监听端口: " << port_ << std::endl;
        
        // 启动服务器发现广播
        discovery_ = std::make_unique<ServerDiscoveryBroadcaster>(port_, "OverWrite Server");
        discovery_->start();
        
    } catch (const std::exception& e) {
        std::cerr << "[WebSocketServer] 启动错误: " << e.what() << std::endl;
        running_ = false;
        return;
    }
    
    // 在独立线程中运行
    serverThread_ = std::thread([this]() {
        try {
            server_.run();
        } catch (const std::exception& e) {
            std::cerr << "[WebSocketServer] 运行错误: " << e.what() << std::endl;
        }
    });
}

void WebSocketGameServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // 停止发现广播
    if (discovery_) {
        discovery_->stop();
        discovery_.reset();
    }
    
    // 关闭所有连接
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        for (auto& [clientId, hdl] : connections_) {
            try {
                server_.close(hdl, websocketpp::close::status::going_away, "Server shutting down");
            } catch (...) {}
        }
        connections_.clear();
        reverseConnections_.clear();
    }
    
    server_.stop();
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    std::cout << "[WebSocketServer] 服务器已停止" << std::endl;
}

void WebSocketGameServer::run() {
    start();
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void WebSocketGameServer::sendToClient(const std::string& clientId, const json& message) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    auto it = connections_.find(clientId);
    if (it == connections_.end()) {
        return;  // 静默失败，客户端可能已断开
    }
    
    try {
        server_.send(it->second, message.dump(), websocketpp::frame::opcode::text);
    } catch (const std::exception& e) {
        std::cerr << "[WebSocketServer] 发送消息失败: " << e.what() << std::endl;
    }
}

void WebSocketGameServer::broadcast(const json& message) {
    std::string payload = message.dump();
    
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (const auto& [clientId, hdl] : connections_) {
        try {
            server_.send(hdl, payload, websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            std::cerr << "[WebSocketServer] 广播失败 (" << clientId << "): " << e.what() << std::endl;
        }
    }
}

void WebSocketGameServer::broadcastExcept(const std::string& excludeClientId, const json& message) {
    std::string payload = message.dump();
    
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (const auto& [clientId, hdl] : connections_) {
        if (clientId == excludeClientId) continue;
        
        try {
            server_.send(hdl, payload, websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            std::cerr << "[WebSocketServer] 广播失败 (" << clientId << "): " << e.what() << std::endl;
        }
    }
}

void WebSocketGameServer::broadcastState() {
    auto snapshots = world_.getPlayersSnapshot();
    if (snapshots.empty()) return;
    
    // 构建状态消息
    json stateMsg = {
        {"type", "state"},
        {"players", json::array()}
    };
    
    for (const auto& snapshot : snapshots) {
        stateMsg["players"].push_back({
            {"clientId", snapshot.clientId},
            {"position", {snapshot.position.x, snapshot.position.y, snapshot.position.z}},
            {"yaw", snapshot.yaw},
            {"pitch", snapshot.pitch},
            {"isJumping", snapshot.isJumping},
            {"isGrounded", snapshot.isGrounded}
        });
    }
    
    broadcast(stateMsg);
}

void WebSocketGameServer::onOpen(ConnectionHdl hdl) {
    std::string clientId = hdlToClientId(hdl);
    
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        connections_[clientId] = hdl;
        reverseConnections_[hdl] = clientId;
    }
    
    std::cout << "[WebSocketServer] 客户端连接: " << clientId << std::endl;
    
    // 在服务器世界中创建玩家实体
    world_.onPlayerConnect(clientId);
    
    // 发送欢迎消息
    json welcome = {
        {"type", "welcome"},
        {"clientId", clientId},
        {"message", "Welcome to OverWrite Server!"},
        {"players", json::array()}  // 现有玩家列表
    };
    
    // 添加现有玩家信息
    for (const auto& [existingId, entity] : world_.getConnectedPlayers()) {
        if (existingId != clientId && world_.registry().valid(entity)) {
            auto& transform = world_.registry().get<ecs::TransformComponent>(entity);
            welcome["players"].push_back({
                {"clientId", existingId},
                {"position", {transform.position.x, transform.position.y, transform.position.z}},
                {"yaw", transform.yaw},
                {"pitch", transform.pitch}
            });
        }
    }
    
    sendToClient(clientId, welcome);
    
    // 广播新玩家加入（通知其他玩家）
    auto newPlayerEntity = world_.getPlayerByClientId(clientId);
    if (world_.registry().valid(newPlayerEntity)) {
        auto& transform = world_.registry().get<ecs::TransformComponent>(newPlayerEntity);
        json joinMsg = {
            {"type", "playerJoin"},
            {"clientId", clientId},
            {"position", {transform.position.x, transform.position.y, transform.position.z}},
            {"yaw", transform.yaw},
            {"pitch", transform.pitch}
        };
        broadcastExcept(clientId, joinMsg);
    }
    
    // 回调
    if (onPlayerConnect_) {
        onPlayerConnect_(clientId);
    }
}

void WebSocketGameServer::onClose(ConnectionHdl hdl) {
    std::string clientId;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        auto it = reverseConnections_.find(hdl);
        if (it != reverseConnections_.end()) {
            clientId = it->second;
            connections_.erase(clientId);
            reverseConnections_.erase(hdl);
        }
    }
    
    if (clientId.empty()) return;
    
    std::cout << "[WebSocketServer] 客户端断开: " << clientId << std::endl;
    
    // 从服务器世界移除玩家
    world_.onPlayerDisconnect(clientId);
    
    // 广播玩家离开
    json leaveMsg = {
        {"type", "playerLeave"},
        {"clientId", clientId}
    };
    broadcastExcept(clientId, leaveMsg);
    
    // 回调
    if (onPlayerDisconnect_) {
        onPlayerDisconnect_(clientId);
    }
}

void WebSocketGameServer::onMessage(ConnectionHdl hdl, MessagePtr msg) {
    std::string clientId;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        auto it = reverseConnections_.find(hdl);
        if (it != reverseConnections_.end()) {
            clientId = it->second;
        }
    }
    
    if (clientId.empty()) return;
    
    try {
        json message = json::parse(msg->get_payload());
        std::string type = message.value("type", "");
        
        if (type == "input") {
            // 解析输入状态
            ecs::InputStateComponent input;
            input.moveForward = message.value("moveForward", false);
            input.moveBackward = message.value("moveBackward", false);
            input.moveLeft = message.value("moveLeft", false);
            input.moveRight = message.value("moveRight", false);
            input.jump = message.value("jump", false);
            input.sprint = message.value("sprint", false);
            input.spaceHeld = message.value("spaceHeld", false);
            input.shiftHeld = message.value("shiftHeld", false);
            input.mouseDeltaX = message.value("mouseDeltaX", 0.0f);
            input.mouseDeltaY = message.value("mouseDeltaY", 0.0f);
            
            // 解析相机方向
            glm::vec3 cameraFront(0.0f, 0.0f, -1.0f);
            glm::vec3 cameraRight(1.0f, 0.0f, 0.0f);
            
            if (message.contains("cameraFront")) {
                auto& cf = message["cameraFront"];
                cameraFront = glm::vec3(cf[0].get<float>(), cf[1].get<float>(), cf[2].get<float>());
            }
            if (message.contains("cameraRight")) {
                auto& cr = message["cameraRight"];
                cameraRight = glm::vec3(cr[0].get<float>(), cr[1].get<float>(), cr[2].get<float>());
            }
            
            // 应用输入
            world_.applyPlayerInput(clientId, input, cameraFront, cameraRight);
            
        } else if (type == "ping") {
            sendToClient(clientId, {{"type", "pong"}, {"time", message.value("time", 0.0)}});
        }
        
        // 回调
        if (onMessage_) {
            onMessage_(clientId, message);
        }
        
    } catch (const json::parse_error& e) {
        std::cerr << "[WebSocketServer] JSON 解析错误: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WebSocketServer] 消息处理错误: " << e.what() << std::endl;
    }
}

std::string WebSocketGameServer::hdlToClientId(ConnectionHdl hdl) const {
    std::ostringstream oss;
    oss << "client_" << std::hex << reinterpret_cast<uintptr_t>(hdl.lock().get());
    return oss.str();
}

ConnectionHdl WebSocketGameServer::clientIdToHdl(const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(clientId);
    if (it != connections_.end()) {
        return it->second;
    }
    return ConnectionHdl();
}

} // namespace server
} // namespace vgame