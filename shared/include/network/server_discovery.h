#ifndef SERVER_DISCOVERY_H
#define SERVER_DISCOVERY_H

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

namespace owengine {

/**
 * @brief 服务器发现广播器（服务端）
 * 
 * 定期在局域网广播服务器信息，让客户端能自动发现
 */
class ServerDiscoveryBroadcaster {
public:
    ServerDiscoveryBroadcaster(uint16_t gamePort, const std::string& serverName = "OverWrite Server");
    ~ServerDiscoveryBroadcaster();
    
    bool start();
    void stop();
    bool isRunning() const { return running_; }
    
    void setServerName(const std::string& name) { serverName_ = name; }
    void setMaxPlayers(int max) { maxPlayers_ = max; }
    void setCurrentPlayers(int current) { currentPlayers_ = current; }
    
private:
    void broadcastLoop();
    std::string createBroadcastMessage();
    
    uint16_t gamePort_;
    std::string serverName_;
    int maxPlayers_{8};
    int currentPlayers_{0};
    
    SOCKET udpSocket_;
    std::thread broadcastThread_;
    std::atomic<bool> running_{false};
    
    static constexpr uint16_t DISCOVERY_PORT = 9001;
    static constexpr int BROADCAST_INTERVAL_MS = 1000;
};

/**
 * @brief 服务器发现扫描器（客户端）
 * 
 * 监听局域网广播，发现可用的游戏服务器
 */
struct DiscoveredServer {
    std::string host;
    uint16_t port;
    std::string name;
    int maxPlayers{8};
    int currentPlayers{0};
    std::chrono::steady_clock::time_point lastSeen;
};

class ServerDiscoveryScanner {
public:
    ServerDiscoveryScanner();
    ~ServerDiscoveryScanner();
    
    bool start();
    void stop();
    bool isRunning() const { return running_; }
    
    // 获取发现的服务器列表
    std::vector<DiscoveredServer> getDiscoveredServers() const;
    
    // 清除过期服务器（超过 3 秒未更新）
    void pruneStaleServers();
    
private:
    void listenLoop();
    void handleBroadcast(const std::string& message, const std::string& senderIp);
    
    SOCKET udpSocket_;
    std::thread listenThread_;
    std::atomic<bool> running_{false};
    
    mutable std::mutex serversMutex_;
    std::vector<DiscoveredServer> discoveredServers_;
    
    static constexpr uint16_t DISCOVERY_PORT = 9001;
    static constexpr int STALE_TIMEOUT_MS = 3000;
};

} // namespace owengine

#endif // SERVER_DISCOVERY_H
