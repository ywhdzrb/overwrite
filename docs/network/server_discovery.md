# 服务器发现

服务器发现模块允许客户端自动发现局域网内的游戏服务器。

---

## DiscoveredServer

发现的服务器信息结构。

```cpp
struct DiscoveredServer {
    std::string host;
    uint16_t port;
    std::string name;
    int maxPlayers{8};
    int currentPlayers{0};
    std::chrono::steady_clock::time_point lastSeen;  // 最后更新时间
};
```

---

## ServerDiscoveryBroadcaster

服务器端广播器，定期广播服务器信息。

### 类定义

```cpp
class ServerDiscoveryBroadcaster {
public:
    ServerDiscoveryBroadcaster(uint16_t gamePort, const std::string& serverName = "OverWrite Server");
    ~ServerDiscoveryBroadcaster();
    
    bool start();
    void stop();
    bool isRunning() const;
    
    // 设置服务器信息
    void setServerName(const std::string& name);
    void setMaxPlayers(int max);
    void setCurrentPlayers(int current);
    
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
```

### 使用示例

```cpp
using namespace vgame;

// 创建广播器（游戏端口 9002）
ServerDiscoveryBroadcaster broadcaster(9002, "My Game Server");

// 设置服务器信息
broadcaster.setMaxPlayers(16);
broadcaster.setCurrentPlayers(3);

// 启动广播
if (broadcaster.start()) {
    std::cout << "Server discovery started" << std::endl;
}

// 更新玩家数
broadcaster.setCurrentPlayers(4);

// 停止广播
broadcaster.stop();
```

### 广播消息格式

```json
{
    "type": "serverInfo",
    "name": "My Game Server",
    "port": 9002,
    "maxPlayers": 16,
    "currentPlayers": 4,
    "timestamp": 1234567890
}
```

---

## ServerDiscoveryScanner

客户端扫描器，监听局域网广播。

### 类定义

```cpp
class ServerDiscoveryScanner {
public:
    ServerDiscoveryScanner();
    ~ServerDiscoveryScanner();
    
    bool start();
    void stop();
    bool isRunning() const;
    
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
```

### 使用示例

```cpp
using namespace vgame;

// 创建扫描器
ServerDiscoveryScanner scanner;

// 启动扫描
if (scanner.start()) {
    std::cout << "Scanning for servers..." << std::endl;
}

// 等待发现
std::this_thread::sleep_for(std::chrono::seconds(2));

// 获取服务器列表
auto servers = scanner.getDiscoveredServers();

for (const auto& server : servers) {
    std::cout << "Found: " << server.name 
              << " at " << server.host << ":" << server.port
              << " (" << server.currentPlayers << "/" << server.maxPlayers << ")"
              << std::endl;
}

// 清除过期服务器
scanner.pruneStaleServers();

// 停止扫描
scanner.stop();
```

---

## 与 ClientWorld 集成

```cpp
// 启动服务器发现
bool ClientWorld::startServerDiscovery() {
    if (!discoveryScanner_) {
        discoveryScanner_ = std::make_unique<ServerDiscoveryScanner>();
    }
    return discoveryScanner_->start();
}

void ClientWorld::stopServerDiscovery() {
    if (discoveryScanner_) {
        discoveryScanner_->stop();
    }
}

std::vector<DiscoveredServer> ClientWorld::getDiscoveredServers() {
    if (discoveryScanner_) {
        discoveryScanner_->pruneStaleServers();
        return discoveryScanner_->getDiscoveredServers();
    }
    return {};
}
```

---

## 使用场景

### 服务器浏览器 UI

```cpp
void showServerBrowser() {
    static bool scanning = false;
    static std::vector<DiscoveredServer> servers;
    
    ImGui::Begin("Server Browser");
    
    if (ImGui::Button("Refresh")) {
        if (!scanning) {
            world->startServerDiscovery();
            scanning = true;
        }
    }
    
    if (scanning) {
        ImGui::SameLine();
        ImGui::Text("Scanning...");
        
        // 2秒后停止扫描
        static auto scanStart = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - scanStart).count() > 2.0f) {
            world->stopServerDiscovery();
            servers = world->getDiscoveredServers();
            scanning = false;
        }
    }
    
    // 显示服务器列表
    for (const auto& server : servers) {
        std::string label = server.name + " (" + 
            std::to_string(server.currentPlayers) + "/" + 
            std::to_string(server.maxPlayers) + ")";
        
        if (ImGui::Selectable(label.c_str())) {
            // 连接到选中的服务器
            connectToServer(server.host, server.port);
        }
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
            "%s:%d", server.host.c_str(), server.port);
    }
    
    ImGui::End();
}
```

### 直连服务器

```cpp
void connectToServer(const std::string& host, uint16_t port) {
    if (world->connectToServer(host, port)) {
        std::cout << "Connecting to " << host << ":" << port << std::endl;
    } else {
        std::cerr << "Failed to connect" << std::endl;
    }
}
```

---

## 网络配置

### 端口

| 端口 | 用途 |
|------|------|
| 9001 | 服务器发现（UDP 广播） |
| 9002 | 游戏 WebSocket 服务器 |

### 广播地址

服务器向以下地址广播：
- `255.255.255.255`（受限广播）
- 或特定子网广播地址（如 `192.168.1.255`）

### 防火墙配置

需要开放以下端口：

**服务器端：**
- UDP 9001（入站/出站）
- TCP 9002（入站）

**客户端：**
- UDP 9001（入站）

---

## 实现细节

### UDP 广播

```cpp
void ServerDiscoveryBroadcaster::broadcastLoop() {
    while (running_) {
        // 创建广播消息
        std::string message = createBroadcastMessage();
        
        // 广播到 255.255.255.255:9001
        sockaddr_in broadcastAddr;
        broadcastAddr.sin_family = AF_INET;
        broadcastAddr.sin_port = htons(DISCOVERY_PORT);
        broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;
        
        sendto(udpSocket_, message.c_str(), message.size(), 0,
               (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
        
        // 等待下一次广播
        std::this_thread::sleep_for(std::chrono::milliseconds(BROADCAST_INTERVAL_MS));
    }
}
```

### 客户端监听

```cpp
void ServerDiscoveryScanner::listenLoop() {
    char buffer[4096];
    sockaddr_in senderAddr;
    socklen_t senderLen = sizeof(senderAddr);
    
    while (running_) {
        // 接收广播消息
        int received = recvfrom(udpSocket_, buffer, sizeof(buffer) - 1, 0,
                               (sockaddr*)&senderAddr, &senderLen);
        
        if (received > 0) {
            buffer[received] = '\0';
            std::string message(buffer);
            std::string senderIp = inet_ntoa(senderAddr.sin_addr);
            handleBroadcast(message, senderIp);
        }
    }
}
```

---

## 限制

1. **仅局域网**：广播不会跨越路由器，仅在局域网内有效
2. **防火墙**：某些防火墙可能阻止 UDP 广播
3. **网络延迟**：发现服务器可能需要几秒钟
