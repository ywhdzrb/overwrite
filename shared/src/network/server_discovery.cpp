#include "network/server_discovery.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <ifaddrs.h>
#include <net/if.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace owengine {

using json = nlohmann::json;

// 获取本机首选 IP 地址（优先返回非虚拟、非回环的局域网 IP）
static std::string getPreferredLocalIP() {
    std::string bestIP = "127.0.0.1";
    
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return bestIP;
    }
    
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        // 跳过回环接口
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        
        // 跳过未运行的接口
        if (!(ifa->ifa_flags & IFF_RUNNING)) continue;
        
        // 只处理 IPv4
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
            std::string ip = inet_ntoa(addr->sin_addr);
            
            // 跳过虚拟网卡（198.18.x.x 是基准测试地址段）
            if (ip.substr(0, 7) == "198.18.") continue;
            
            // 跳过 Docker 网络
            if (ip.substr(0, 7) == "172.17.") continue;
            
            // 优先选择 192.168.x.x 或 10.x.x.x 这样的常见局域网地址
            if (ip.substr(0, 8) == "192.168." || ip.substr(0, 3) == "10.") {
                bestIP = ip;
                break;  // 找到好的就退出
            }
            
            // 其他地址也记录，但继续寻找更好的
            if (bestIP == "127.0.0.1") {
                bestIP = ip;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return bestIP;
}

// ==================== ServerDiscoveryBroadcaster ====================

ServerDiscoveryBroadcaster::ServerDiscoveryBroadcaster(uint16_t gamePort, const std::string& serverName)
    : gamePort_(gamePort)
    , serverName_(serverName)
    , udpSocket_(INVALID_SOCKET) {
}

ServerDiscoveryBroadcaster::~ServerDiscoveryBroadcaster() {
    stop();
}

bool ServerDiscoveryBroadcaster::start() {
    if (running_) return false;
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
#endif
    
    // 创建 UDP socket
    udpSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket_ == INVALID_SOCKET) {
        std::cerr << "[Discovery] 创建 UDP socket 失败" << std::endl;
        return false;
    }
    
    // 启用广播
    int broadcast = 1;
    if (setsockopt(udpSocket_, SOL_SOCKET, SO_BROADCAST, 
#ifdef _WIN32
                   (const char*)&broadcast, sizeof(broadcast)
#else
                   &broadcast, sizeof(broadcast)
#endif
                  ) == SOCKET_ERROR) {
        std::cerr << "[Discovery] 设置广播选项失败" << std::endl;
#ifdef _WIN32
        closesocket(udpSocket_);
#else
        close(udpSocket_);
#endif
        return false;
    }
    
    running_ = true;
    broadcastThread_ = std::thread(&ServerDiscoveryBroadcaster::broadcastLoop, this);
    
    std::cout << "[Discovery] 广播器启动，游戏端口: " << gamePort_ << std::endl;
    return true;
}

void ServerDiscoveryBroadcaster::stop() {
    if (!running_) return;
    
    running_ = false;
    
#ifdef _WIN32
    closesocket(udpSocket_);
#else
    close(udpSocket_);
#endif
    
    if (broadcastThread_.joinable()) {
        broadcastThread_.join();
    }
    
    std::cout << "[Discovery] 广播器已停止" << std::endl;
}

void ServerDiscoveryBroadcaster::broadcastLoop() {
    struct sockaddr_in broadcastAddr;
    std::memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(DISCOVERY_PORT);
    broadcastAddr.sin_addr.s_addr = inet_addr("255.255.255.255");
    
    while (running_) {
        std::string message = createBroadcastMessage();
        
        ssize_t sent = sendto(udpSocket_, message.c_str(), message.size(), 0,
                              (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
        
        if (sent == SOCKET_ERROR) {
            // 忽略错误，继续广播
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(BROADCAST_INTERVAL_MS));
    }
}

std::string ServerDiscoveryBroadcaster::createBroadcastMessage() {
    json msg = {
        {"type", "overwrite_server"},
        {"host", getPreferredLocalIP()},  // 主动包含服务器 IP
        {"port", gamePort_},
        {"name", serverName_},
        {"maxPlayers", maxPlayers_},
        {"currentPlayers", currentPlayers_}
    };
    return msg.dump();
}

// ==================== ServerDiscoveryScanner ====================

ServerDiscoveryScanner::ServerDiscoveryScanner()
    : udpSocket_(INVALID_SOCKET) {
}

ServerDiscoveryScanner::~ServerDiscoveryScanner() {
    stop();
}

bool ServerDiscoveryScanner::start() {
    if (running_) return false;
    
    // 创建 UDP socket
    udpSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket_ == INVALID_SOCKET) {
        return false;
    }
    
    // 启用地址重用
    int reuse = 1;
#ifdef _WIN32
    setsockopt(udpSocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
#else
    setsockopt(udpSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    
    // 绑定到发现端口
    struct sockaddr_in listenAddr;
    std::memset(&listenAddr, 0, sizeof(listenAddr));
    listenAddr.sin_family = AF_INET;
    listenAddr.sin_port = htons(DISCOVERY_PORT);
    listenAddr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(udpSocket_, (struct sockaddr*)&listenAddr, sizeof(listenAddr)) == SOCKET_ERROR) {
#ifdef _WIN32
        closesocket(udpSocket_);
#else
        close(udpSocket_);
#endif
        return false;
    }
    
    running_ = true;
    listenThread_ = std::thread(&ServerDiscoveryScanner::listenLoop, this);
    
    std::cout << "[Discovery] 扫描器启动，监听端口: " << DISCOVERY_PORT << std::endl;
    return true;
}

void ServerDiscoveryScanner::stop() {
    if (!running_) return;
    
    running_ = false;
    
#ifdef _WIN32
    closesocket(udpSocket_);
#else
    close(udpSocket_);
#endif
    udpSocket_ = INVALID_SOCKET;
    
    if (listenThread_.joinable()) {
        listenThread_.join();
    }
    
    std::cout << "[Discovery] 扫描器已停止" << std::endl;
}

std::vector<DiscoveredServer> ServerDiscoveryScanner::getDiscoveredServers() const {
    std::lock_guard<std::mutex> lock(serversMutex_);
    return discoveredServers_;
}

void ServerDiscoveryScanner::pruneStaleServers() {
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(serversMutex_);
    discoveredServers_.erase(
        std::remove_if(discoveredServers_.begin(), discoveredServers_.end(),
            [&now](const DiscoveredServer& server) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - server.lastSeen
                ).count();
                return elapsed > STALE_TIMEOUT_MS;
            }),
        discoveredServers_.end()
    );
}

void ServerDiscoveryScanner::listenLoop() {
    char buffer[1024];
    struct sockaddr_in senderAddr;
    socklen_t senderLen = sizeof(senderAddr);
    
    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms
#ifdef _WIN32
    setsockopt(udpSocket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    setsockopt(udpSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    
    while (running_) {
        ssize_t received = recvfrom(udpSocket_, buffer, sizeof(buffer) - 1, 0,
                                    (struct sockaddr*)&senderAddr, &senderLen);
        
        if (received <= 0) {
            continue;  // 超时或错误，继续
        }
        
        buffer[received] = '\0';
        std::string message(buffer);
        std::string senderIp = inet_ntoa(senderAddr.sin_addr);
        
        handleBroadcast(message, senderIp);
    }
}

void ServerDiscoveryScanner::handleBroadcast(const std::string& message, const std::string& senderIp) {
    try {
        json msg = json::parse(message);
        
        if (msg.value("type", "") != "overwrite_server") {
            return;
        }
        
        DiscoveredServer server;
        // 优先使用消息中的 host，如果没有则使用发送者 IP
        server.host = msg.value("host", senderIp);
        server.port = msg.value("port", 9002);
        server.name = msg.value("name", "Unknown Server");
        server.maxPlayers = msg.value("maxPlayers", 8);
        server.currentPlayers = msg.value("currentPlayers", 0);
        server.lastSeen = std::chrono::steady_clock::now();
        
        std::lock_guard<std::mutex> lock(serversMutex_);
        
        // 查找是否已存在
        bool found = false;
        for (auto& s : discoveredServers_) {
            if (s.host == server.host && s.port == server.port) {
                s = server;  // 更新
                found = true;
                break;
            }
        }
        
        if (!found) {
            discoveredServers_.push_back(server);
            std::cout << "[Discovery] 发现服务器: " << server.name 
                      << " (" << server.host << ":" << server.port << ")" << std::endl;
        }
        
    } catch (const json::parse_error&) {
        // 忽略解析错误
    }
}

} // namespace owengine
