#include "server/websocket_server.hpp"
#include "network/protocol.hpp"
#include "utils/logger.hpp"
#include <sstream>
#include <iomanip>
#include <random>
#include <vector>

namespace owengine {
namespace server {

WebSocketGameServer::WebSocketGameServer(uint16_t port)
    : server_(port, "0.0.0.0"), port_(port) {
    
    Logger::info("[WebSocketServer] 初始化完成，端口: " + std::to_string(port));
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
        
        Logger::info("[WebSocketServer] 客户端连接: " + clientId);
        
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
                Logger::info("[WebSocketServer] WebSocket 握手完成: " + clientId);
                
                // 发送欢迎消息
                json welcome = {
                    {"t", network::MSG_WELCOME},
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
                
                Logger::info(std::string("[WebSocketServer] Welcome 消息发送给 ") + clientId 
                          + ", 现有玩家数: " + std::to_string(existingPlayerCount));
                
                webSocket->send(welcome.dump());
                
                // 广播新玩家加入（通知其他玩家）
                auto newPlayerEntity = world_.getPlayerByClientId(clientId);
                if (world_.registry().valid(newPlayerEntity)) {
                    auto& transform = world_.registry().get<ecs::TransformComponent>(newPlayerEntity);
                    json joinMsg = {
                        {"t", network::MSG_PLAYER_JOIN},
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
                    // 优先处理二进制输入消息（高频路径）
                    if (msg->binary) {
                        network::InputMessage inputMsg;
                        if (network::unpackInputMessage(msg->str, inputMsg)) {
                            ecs::InputStateComponent input;
                            bool moveF, moveB, moveL, moveR, jump, sprint, spaceH, shiftH;
                            network::unpackButtons(inputMsg.buttons,
                                moveF, moveB, moveL, moveR,
                                jump, sprint, spaceH, shiftH);
                            input.setMoveForward(moveF);
                            input.setMoveBackward(moveB);
                            input.setMoveLeft(moveL);
                            input.setMoveRight(moveR);
                            input.setJump(jump);
                            input.setSprint(sprint);
                            input.setSpaceHeld(spaceH);
                            input.setShiftHeld(shiftH);
                            input.mouseDeltaX = static_cast<float>(inputMsg.mouseDeltaX) * 0.0001f;
                            input.mouseDeltaY = static_cast<float>(inputMsg.mouseDeltaY) * 0.0001f;
                            
                            glm::vec3 cameraFront(
                                inputMsg.cameraFront[0],
                                inputMsg.cameraFront[1],
                                inputMsg.cameraFront[2]);
                            glm::vec3 cameraRight(
                                inputMsg.cameraRight[0],
                                inputMsg.cameraRight[1],
                                inputMsg.cameraRight[2]);
                            
                            world_.applyPlayerInput(clientId, input, cameraFront, cameraRight);
                            
                            if (onMessage_) {
                                json cbMsg = {{"t", network::MSG_INPUT}, {"type", "input"}};
                                onMessage_(clientId, cbMsg);
                            }
                        }
                    } else {
                        // JSON 消息（低频路径：welcome/state/backward compat）
                        json message = json::parse(msg->str);
                        
                        // 优先读整型 "t" 字段，回退到字符串 "type"
                        network::MessageType mType = network::MSG_UNKNOWN;
                        if (message.contains("t") && message["t"].is_number_integer()) {
                            mType = static_cast<network::MessageType>(message["t"].get<int>());
                        } else {
                            mType = network::messageTypeFromName(message.value("type", ""));
                        }
                        
                        if (mType == network::MSG_INPUT) {
                            // JSON 格式输入（向后兼容）
                            ecs::InputStateComponent input;
                            input.setMoveForward(message.value("moveForward", false));
                            input.setMoveBackward(message.value("moveBackward", false));
                            input.setMoveLeft(message.value("moveLeft", false));
                            input.setMoveRight(message.value("moveRight", false));
                            input.setJump(message.value("jump", false));
                            input.setSprint(message.value("sprint", false));
                            input.setSpaceHeld(message.value("spaceHeld", false));
                            input.setShiftHeld(message.value("shiftHeld", false));
                            input.mouseDeltaX = message.value("mouseDeltaX", 0.0f);
                            input.mouseDeltaY = message.value("mouseDeltaY", 0.0f);
                            
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
                            world_.applyPlayerInput(clientId, input, cameraFront, cameraRight);
                            
                        } else if (mType == network::MSG_PING) {
                            webSocket->send(json{{"t", network::MSG_PONG}, {"type", "pong"}, {"time", message.value("time", 0.0)}}.dump());
                        }
                        
                        // 回调
                        if (onMessage_) {
                            onMessage_(clientId, message);
                        }
                    }
                    
                } catch (const json::parse_error& e) {
                    Logger::error(std::string("[WebSocketServer] JSON 解析错误: ") + e.what());
                } catch (const std::exception& e) {
                    Logger::error(std::string("[WebSocketServer] 消息处理错误: ") + e.what());
                }
            } else if (msg->type == ix::WebSocketMessageType::Close) {
                Logger::info("[WebSocketServer] 客户端断开: " + clientId);
                
                // 从服务器世界移除玩家
                world_.onPlayerDisconnect(clientId);
                
                // 广播玩家离开
                json leaveMsg = {
                    {"t", network::MSG_PLAYER_LEAVE},
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
        Logger::error("[WebSocketServer] 监听失败");
        running_ = false;
        return;
    }
    
    Logger::info("[WebSocketServer] 服务器启动，监听端口: " + std::to_string(port_));
    
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
    
    Logger::info("[WebSocketServer] 服务器已停止");
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
    
    // 在锁外拷贝连接列表，避免发送慢客户端时长时间占用锁
    std::vector<std::shared_ptr<ix::WebSocket>> targets;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        targets.reserve(connections_.size());
        for (const auto& [clientId, info] : connections_) {
            targets.push_back(info.webSocket);
        }
    }
    
    for (auto& ws : targets) {
        if (ws) ws->send(payload);
    }
}

void WebSocketGameServer::broadcastExcept(const std::string& excludeClientId, const json& message) {
    std::string payload = message.dump();
    
    // 在锁外拷贝连接列表
    std::vector<std::pair<std::string, std::shared_ptr<ix::WebSocket>>> targets;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        targets.reserve(connections_.size());
        for (const auto& [clientId, info] : connections_) {
            targets.emplace_back(clientId, info.webSocket);
        }
    }
    
    size_t sentCount = 0;
    for (auto& [clientId, ws] : targets) {
        if (clientId == excludeClientId) continue;
        if (ws) {
            ws->send(payload);
            sentCount++;
        }
    }
    
    std::string type = message.value("type", "unknown");
    Logger::info(std::string("[WebSocketServer] 广播 ") + type + " 给 " + std::to_string(sentCount) 
              + " 个客户端（排除 " + excludeClientId + "）");
}

void WebSocketGameServer::broadcastState() {
    auto snapshots = world_.getPlayersSnapshot();
    if (snapshots.empty()) return;
    
    // 构建状态消息
    json stateMsg = {
        {"t", network::MSG_STATE},
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
} // namespace owengine