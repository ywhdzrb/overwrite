#include "server/websocket_server.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>

namespace vgame {
namespace server {

WebSocketGameServer::WebSocketGameServer(uint16_t port)
    : server_(port, "0.0.0.0"), port_(port) {
    
    std::cout << "[WebSocketServer] 初始化完成，端口: " << port << std::endl;
}

WebSocketGameServer::~WebSocketGameServer() {
    stop();
}

void WebSocketGameServer::start() {
    if (running_) return;
    
    running_ = true;
    
    // 设置连接回调
    server_.setOnConnectionCallback([this](
        std::weak_ptr<ix::WebSocket> weakWebSocket,
        std::shared_ptr<ix::ConnectionState> connectionState) {
        
        auto webSocket = weakWebSocket.lock();
        if (!webSocket) return;
        
        std::string clientId = generateClientId();
        
        std::cout << "[WebSocketServer] 客户端连接: " << clientId << std::endl;
        
        // 存储连接信息
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            ConnectionInfo info;
            info.webSocket = webSocket;
            info.clientId = clientId;
            connections_[clientId] = info;
        }
        
        // 在服务器世界中创建玩家实体
        world_.onPlayerConnect(clientId);
        
        // 设置消息回调
        webSocket->setOnMessageCallback([this, clientId, webSocket](const ix::WebSocketMessagePtr& msg) {
            if (msg->type == ix::WebSocketMessageType::Open) {
                // WebSocket 握手完成，现在可以发送欢迎消息
                std::cout << "[WebSocketServer] WebSocket 握手完成: " << clientId << std::endl;
                
                // 发送欢迎消息
                json welcome = {
                    {"type", "welcome"},
                    {"clientId", clientId},
                    {"message", "Welcome to OverWrite Server!"},
                    {"players", json::array()}
                };
                
                // 添加现有玩家信息
                size_t existingPlayerCount = 0;
                for (const auto& [existingId, entity] : world_.getConnectedPlayers()) {
                    if (existingId != clientId && world_.registry().valid(entity)) {
                        auto& transform = world_.registry().get<ecs::TransformComponent>(entity);
                        welcome["players"].push_back({
                            {"clientId", existingId},
                            {"position", {transform.position.x, transform.position.y, transform.position.z}},
                            {"yaw", transform.yaw},
                            {"pitch", transform.pitch}
                        });
                        existingPlayerCount++;
                    }
                }
                
                std::cout << "[WebSocketServer] Welcome 消息发送给 " << clientId 
                          << ", 现有玩家数: " << existingPlayerCount << std::endl;
                
                webSocket->send(welcome.dump());
                
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
                
            } else if (msg->type == ix::WebSocketMessageType::Message) {
                try {
                    json message = json::parse(msg->str);
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
                        webSocket->send(json{{"type", "pong"}, {"time", message.value("time", 0.0)}}.dump());
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
            } else if (msg->type == ix::WebSocketMessageType::Close) {
                std::cout << "[WebSocketServer] 客户端断开: " << clientId << std::endl;
                
                // 从服务器世界移除玩家
                world_.onPlayerDisconnect(clientId);
                
                // 广播玩家离开
                json leaveMsg = {
                    {"type", "playerLeave"},
                    {"clientId", clientId}
                };
                broadcastExcept(clientId, leaveMsg);
                
                // 移除连接
                {
                    std::lock_guard<std::mutex> lock(connectionsMutex_);
                    connections_.erase(clientId);
                }
                
                // 回调
                if (onPlayerDisconnect_) {
                    onPlayerDisconnect_(clientId);
                }
            }
        });
    });
    
    // 启动服务器
    bool success = server_.listenAndStart();
    if (!success) {
        std::cerr << "[WebSocketServer] 监听失败" << std::endl;
        running_ = false;
        return;
    }
    
    std::cout << "[WebSocketServer] 服务器启动，监听端口: " << port_ << std::endl;
    
    // 启动服务器发现广播
    discovery_ = std::make_unique<ServerDiscoveryBroadcaster>(port_, "OverWrite Server");
    discovery_->start();
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
        for (auto& [clientId, info] : connections_) {
            if (info.webSocket) {
                info.webSocket->close(1001, "Server shutting down");
            }
        }
        connections_.clear();
    }
    
    server_.stop();
    
    std::cout << "[WebSocketServer] 服务器已停止" << std::endl;
}

void WebSocketGameServer::run() {
    start();
    // ixwebsocket 的服务器在独立线程中运行，这里等待
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void WebSocketGameServer::sendToClient(const std::string& clientId, const json& message) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    
    auto it = connections_.find(clientId);
    if (it == connections_.end() || !it->second.webSocket) {
        return;
    }
    
    it->second.webSocket->send(message.dump());
}

void WebSocketGameServer::broadcast(const json& message) {
    std::string payload = message.dump();
    
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (const auto& [clientId, info] : connections_) {
        if (info.webSocket) {
            info.webSocket->send(payload);
        }
    }
}

void WebSocketGameServer::broadcastExcept(const std::string& excludeClientId, const json& message) {
    std::string payload = message.dump();
    
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    size_t sentCount = 0;
    for (const auto& [clientId, info] : connections_) {
        if (clientId == excludeClientId) continue;
        if (info.webSocket) {
            info.webSocket->send(payload);
            sentCount++;
        }
    }
    
    std::string type = message.value("type", "unknown");
    std::cout << "[WebSocketServer] 广播 " << type << " 给 " << sentCount 
              << " 个客户端（排除 " << excludeClientId << "）" << std::endl;
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

size_t WebSocketGameServer::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    return connections_.size();
}

std::string WebSocketGameServer::generateClientId() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 999999);
    
    std::ostringstream oss;
    oss << "client_" << std::setw(6) << std::setfill('0') << dis(gen);
    return oss.str();
}

} // namespace server
} // namespace vgame