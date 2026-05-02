// OverWrite 游戏服务器
// 负责 WebSocket 联机、世界状态同步、玩家管理
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include <nlohmann/json.hpp>

#include "ecs/ecs.h"
#include "server/websocket_server.h"
#include "utils/logger.h"

std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    uint16_t port = 9002;  // 默认端口
    int tickRate = 60;     // 默认 tick rate
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        } else if (arg == "-t" || arg == "--tickrate") {
            if (i + 1 < argc) {
                tickRate = std::stoi(argv[++i]);
            }
        }
    }
    
    owengine::Logger::info("========================================");
    owengine::Logger::info("  OverWrite Game Server");
    owengine::Logger::info("========================================");
    owengine::Logger::info("Port:     " + std::to_string(port));
    owengine::Logger::info("TickRate: " + std::to_string(tickRate) + " Hz");
    
    // 设置信号处理
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // 创建服务器
    owengine::server::WebSocketGameServer server(port);
    
    // 设置回调
    server.setOnPlayerConnect([](const std::string& clientId) {
        owengine::Logger::info("[Event] 玩家加入: " + clientId);
    });
    
    server.setOnPlayerDisconnect([](const std::string& clientId) {
        owengine::Logger::info("[Event] 玩家离开: " + clientId);
    });
    
    server.setOnMessage([](const std::string& clientId, const nlohmann::json& message) {
        // 输入消息不打印，避免刷屏
        if (message.value("type", "") != "input") {
            owengine::Logger::info("[Message] 来自 " + clientId + ": " + message.dump());
        }
    });
    
    // 启动服务器
    server.start();
    
    owengine::Logger::info("服务器已启动，按 Ctrl+C 停止...");
    
    // 时间管理
    auto lastTime = std::chrono::high_resolution_clock::now();
    const auto tickDuration = std::chrono::microseconds(1000000 / tickRate);
    const auto stateBroadcastInterval = std::chrono::milliseconds(20);  // 50Hz 状态同步
    auto lastStateBroadcast = lastTime;
    
    // 统计
    int frameCount = 0;
    auto statsTime = lastTime;
    
    // 主循环
    while (g_running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // 限制 deltaTime 防止大跳跃
        deltaTime = std::min(deltaTime, owengine::ecs::MAX_DELTA_TIME);
        
        // 更新游戏世界
        server.getWorld().update(deltaTime);
        
        // 定期广播玩家状态
        if (currentTime - lastStateBroadcast >= stateBroadcastInterval) {
            server.broadcastState();
            lastStateBroadcast = currentTime;
        }
        
        // 统计信息
        frameCount++;
        if (currentTime - statsTime >= std::chrono::seconds(1)) {
            // 每秒打印一次状态
            // std::cout << "[Stats] FPS: " << frameCount 
            //           << ", Players: " << server.getConnectionCount() << std::endl;
            frameCount = 0;
            statsTime = currentTime;
        }
        
        // 睡眠以保持稳定的 tick rate
        auto sleepUntil = lastTime + tickDuration;
        auto sleepDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            sleepUntil - std::chrono::high_resolution_clock::now()
        );
        
        if (sleepDuration.count() > 0) {
            std::this_thread::sleep_for(sleepDuration);
        }
    }
    
    owengine::Logger::info("正在停止服务器...");
    server.stop();
    
    owengine::Logger::info("服务器已关闭");
    return 0;
}