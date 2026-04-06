# 客户端系统

客户端系统是客户端专用的逻辑处理类，定义在 `client/include/ecs/client_systems.h`。

---

## InputSystem

输入系统，处理键盘和鼠标输入。

### 类定义

```cpp
class InputSystem {
public:
    explicit InputSystem(World& world, GLFWwindow* window);
    ~InputSystem();
    
    void update(float deltaTime);
    
    // 查询接口
    bool isKeyPressed(int key) const;
    bool isKeyJustPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    
    void getMouseDelta(double& deltaX, double& deltaY) const;
    void setCursorCaptured(bool captured);
    bool isCursorCaptured() const { return cursorCaptured_; }
    
    // 重置帧状态
    void resetFrameState();
    
private:
    World& world_;
    GLFWwindow* window_;
    
    // 输入状态
    bool keys_[GLFW_KEY_LAST + 1]{false};
    bool previousKeys_[GLFW_KEY_LAST + 1]{false};
    bool mouseButtons_[8]{false};
    bool previousMouseButtons_[8]{false};
    
    double mouseX_{0.0};
    double mouseY_{0.0};
    double mouseDeltaX_{0.0};
    double mouseDeltaY_{0.0};
    
    bool cursorCaptured_{false};
    
    // GLFW 回调
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
};
```

### 使用示例

```cpp
World world;
InputSystem inputSystem(world, window);

// 主循环
void gameLoop(float deltaTime) {
    // 更新输入
    inputSystem.update(deltaTime);
    
    // 查询输入状态
    if (inputSystem.isKeyPressed(GLFW_KEY_W)) {
        // 向前移动
    }
    
    if (inputSystem.isKeyJustPressed(GLFW_KEY_SPACE)) {
        // 跳跃（只触发一次）
    }
    
    // 鼠标移动
    double deltaX, deltaY;
    inputSystem.getMouseDelta(deltaX, deltaY);
    
    // 帧结束重置
    inputSystem.resetFrameState();
}
```

### 处理逻辑

系统更新玩家实体的 `InputStateComponent`：

```cpp
void InputSystem::update(float deltaTime) {
    // 获取玩家实体
    auto player = world_.getPlayer();
    if (player == entt::null) return;
    
    auto& input = world_.registry().get<InputStateComponent>(player);
    
    // 更新移动输入
    input.moveForward = keys_[GLFW_KEY_W] || keys_[GLFW_KEY_UP];
    input.moveBackward = keys_[GLFW_KEY_S] || keys_[GLFW_KEY_DOWN];
    input.moveLeft = keys_[GLFW_KEY_A] || keys_[GLFW_KEY_LEFT];
    input.moveRight = keys_[GLFW_KEY_D] || keys_[GLFW_KEY_RIGHT];
    input.jump = keys_[GLFW_KEY_SPACE];
    input.sprint = keys_[GLFW_KEY_LEFT_SHIFT];
    
    // 更新鼠标输入
    input.mouseDeltaX = mouseDeltaX_;
    input.mouseDeltaY = mouseDeltaY_;
}
```

---

## CameraSystem

相机系统，更新相机矩阵。

### 类定义

```cpp
class CameraSystem {
public:
    explicit CameraSystem(World& world);
    
    void update(float deltaTime);
    
    // 获取主相机的矩阵
    glm::mat4 getMainViewMatrix() const;
    glm::mat4 getMainProjectionMatrix() const;
    
private:
    World& world_;
};
```

### 使用示例

```cpp
World world;
CameraSystem cameraSystem(world);

// 主循环
void gameLoop(float deltaTime) {
    // 更新相机
    cameraSystem.update(deltaTime);
    
    // 获取矩阵用于渲染
    glm::mat4 view = cameraSystem.getMainViewMatrix();
    glm::mat4 proj = cameraSystem.getMainProjectionMatrix();
    
    // 渲染场景
    renderScene(view, proj);
}
```

### 处理逻辑

系统处理具有 `CameraComponent` 和 `TransformComponent` 的实体：

```cpp
void CameraSystem::update(float deltaTime) {
    auto view = world_.registry().view<CameraComponent, TransformComponent>();
    
    for (auto entity : view) {
        auto& camera = view.get<CameraComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        // 更新缓存的矩阵
        camera.viewMatrix = camera.getViewMatrix(transform);
        camera.projectionMatrix = camera.getProjectionMatrix();
        camera.dirty = false;
    }
}
```

---

## ClientWorld

客户端世界管理器，扩展共享 World，添加客户端专用功能。

### 类定义

```cpp
class ClientWorld : public World {
public:
    ClientWorld();
    ~ClientWorld();
    
    // 初始化客户端系统
    void initClientSystems(GLFWwindow* window, int viewportWidth, int viewportHeight);
    
    // 更新所有客户端系统
    void updateClientSystems(float deltaTime);
    
    // 获取客户端系统
    InputSystem* getInputSystem() { return inputSystem_.get(); }
    CameraSystem* getCameraSystem() { return cameraSystem_.get(); }
    MovementSystem* getMovementSystem() { return movementSystem_.get(); }
    PhysicsSystem* getPhysicsSystem() { return physicsSystem_.get(); }
    client::NetworkSystem* getNetworkSystem() { return networkSystem_.get(); }
    
    // 网络连接
    bool connectToServer(const std::string& host, uint16_t port);
    void disconnectFromServer();
    bool isConnectedToServer() const;
    
    // 服务器发现
    bool startServerDiscovery();
    void stopServerDiscovery();
    std::vector<DiscoveredServer> getDiscoveredServers();
    
    // 创建带相机组件的玩家
    entt::entity createClientPlayer(int viewportWidth, int viewportHeight);
    
private:
    std::unique_ptr<InputSystem> inputSystem_;
    std::unique_ptr<CameraSystem> cameraSystem_;
    std::unique_ptr<MovementSystem> movementSystem_;
    std::unique_ptr<PhysicsSystem> physicsSystem_;
    std::unique_ptr<client::NetworkSystem> networkSystem_;
    std::unique_ptr<ServerDiscoveryScanner> discoveryScanner_;
    
    int viewportWidth_{800};
    int viewportHeight_{600};
};
```

### 使用示例

```cpp
// 创建客户端世界
auto world = std::make_unique<ecs::ClientWorld>();

// 初始化
world->initClientSystems(window, 1280, 720);

// 创建玩家
entt::entity player = world->createClientPlayer(1280, 720);

// 主循环
void gameLoop(float deltaTime) {
    // 更新所有系统
    world->updateClientSystems(deltaTime);
    
    // 渲染
    glm::mat4 view = world->getCameraSystem()->getMainViewMatrix();
    glm::mat4 proj = world->getCameraSystem()->getMainProjectionMatrix();
    render(view, proj);
}

// 网络连接
void connect(const std::string& host, uint16_t port) {
    if (world->connectToServer(host, port)) {
        std::cout << "Connected to server" << std::endl;
    }
}

// 服务器发现
void findServers() {
    world->startServerDiscovery();
    
    // 等待发现完成
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto servers = world->getDiscoveredServers();
    for (const auto& server : servers) {
        std::cout << server.name << " at " << server.host << ":" << server.port << std::endl;
    }
    
    world->stopServerDiscovery();
}
```

### 系统更新顺序

```cpp
void ClientWorld::updateClientSystems(float deltaTime) {
    // 1. 输入系统
    inputSystem_->update(deltaTime);
    
    // 2. 移动系统
    movementSystem_->update(deltaTime);
    
    // 3. 物理系统
    physicsSystem_->update(deltaTime);
    
    // 4. 相机系统
    cameraSystem_->update(deltaTime);
    
    // 5. 网络系统
    if (networkSystem_ && networkSystem_->isConnected()) {
        networkSystem_->update(deltaTime);
    }
    
    // 6. 重置输入帧状态
    inputSystem_->resetFrameState();
}
```

---

## 完整客户端示例

```cpp
#include "ecs/ecs.h"
#include <GLFW/glfw3.h>

int main() {
    // 初始化 GLFW
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Game", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    
    // 创建客户端世界
    auto world = std::make_unique<vgame::ecs::ClientWorld>();
    world->initClientSystems(window, 1280, 720);
    
    // 创建玩家
    auto player = world->createClientPlayer(1280, 720);
    auto& transform = world->registry().get<vgame::ecs::TransformComponent>(player);
    transform.position = glm::vec3(0.0f, 0.0f, 10.0f);
    
    // 设置物理地形
    world->getPhysicsSystem()->setTerrainQuery([](float x, float z) {
        return 0.0f;  // 平地
    });
    
    // 捕获鼠标
    world->getInputSystem()->setCursorCaptured(true);
    
    // 主循环
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    while (!glfwWindowShouldClose(window)) {
        // 计算 deltaTime
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // 更新系统
        world->updateClientSystems(deltaTime);
        
        // 渲染
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // ... 渲染代码
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // 清理
    world.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}
```

---

## 与渲染器集成

```cpp
void render(vgame::ecs::ClientWorld& world, VkCommandBuffer cmdBuffer) {
    // 获取相机矩阵
    auto* cameraSystem = world.getCameraSystem();
    glm::mat4 view = cameraSystem->getMainViewMatrix();
    glm::mat4 proj = cameraSystem->getMainProjectionMatrix();
    
    // 渲染所有有 RenderComponent 的实体
    auto view = world.registry().view<vgame::ecs::RenderComponent, vgame::ecs::TransformComponent>();
    
    for (auto entity : view) {
        auto& render = view.get<vgame::ecs::RenderComponent>(entity);
        auto& transform = view.get<vgame::ecs::TransformComponent>(entity);
        
        if (render.visible) {
            glm::mat4 model = transform.getModelMatrix();
            // 渲染模型
            renderModel(cmdBuffer, render.modelPath, model, view, proj);
        }
    }
}
```
