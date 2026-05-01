#pragma once

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <memory>
#include <glm/glm.hpp>
#include "ecs/client_components.h"
#include "ecs/systems.h"  // 共享系统
#include "ecs/i_game_world.h"  // 游戏世界接口
#include "ecs/camera_controller_system.h"  // 相机控制系统
#include "network/network_system.h"  // 网络系统
#include "network/server_discovery.h"  // 服务器发现

namespace owengine {
namespace ecs {

/**
 * @brief 输入系统（客户端专用）
 * 
 * 处理键盘和鼠标输入，更新输入状态组件
 */
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

/**
 * @brief 相机系统（客户端专用）
 * 
 * 更新相机矩阵，处理视角旋转
 */
class CameraSystem {
public:
    explicit CameraSystem(World& world);
    
    void update(float deltaTime);
    
    // 获取主相机的视图和投影矩阵
    glm::mat4 getMainViewMatrix() const;
    glm::mat4 getMainProjectionMatrix() const;
    
private:
    World& world_;
};

/**
 * @brief 客户端世界管理器
 * 
 * 扩展共享 World，添加客户端专用功能
 */
class ClientWorld : public World, public IGameWorld {
public:
    ClientWorld();
    ~ClientWorld();
    
    // 初始化客户端系统
    void initClientSystems(GLFWwindow* window, int viewportWidth, int viewportHeight);
    
    // 更新所有客户端系统（同步调用，保持向后兼容）
    void updateClientSystems(float deltaTime);

    // 同步阶段：仅 GLFW/WebSocket 操作（必须主线程）
    void updateClientSystemsSync(float deltaTime);

    // 异步阶段：纯 CPU 模拟（移动/物理/相机，可后台线程执行）
    void updateClientSystemsAsync(float deltaTime);

    // 发送网络输入（需要在同步输入 + 异步模拟完成后调用）
    void sendNetworkInputs();
    
    // 获取客户端系统
    InputSystem* getInputSystem() { return inputSystem_.get(); }
    CameraSystem* getCameraSystem() { return cameraSystem_.get(); }
    CameraControllerSystem* getCameraControllerSystem() { return cameraControllerSystem_.get(); }
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
    
    // 创建带相机组件的玩家
    entt::entity createClientPlayer(int viewportWidth, int viewportHeight);
    
    // 地形查询
    void setTerrainQuery(TerrainHeightQuery query) {
        if (physicsSystem_) {
            physicsSystem_->setTerrainQuery(std::move(query));
            adjustPlayerToTerrain();
        }
    }
    
    // ── IGameWorld 接口实现 ────────────────
    void reset() override;
    bool isPlayerValid() const override;
    glm::vec3 getPlayerPosition() const override;
    glm::vec3 getCameraFront() const override { return getCameraFrontImpl(); }
    glm::vec3 getCameraRight() const override { return getCameraRightImpl(); }
    void setPlayerSpeed(float speed) override;
    void setPlayerSensitivity(float sens) override;
    void setPlayerDirection(const glm::vec3& front, const glm::vec3& right) override;
    void updateFlight(float dt, bool spaceHeld, bool shiftHeld) override;
    bool isPlayerFlying() const override { return isFlying_; }
    void setPlayerFlying(bool flying) override { isFlying_ = flying; }
    void syncCamera(class Camera& camera) override;
    std::vector<RemotePlayerInfo> getRemotePlayers() const override;
    std::vector<DiscoveredServerInfo> getDiscoveredServers() override;
    entt::registry* getRegistry() override { return &registry(); }

    // 桥接：IGameWorld 接口命名 → ClientWorld 现有方法
    void updateSync(float dt) override { updateClientSystemsSync(dt); }
    void updateAsync(float dt) override { updateClientSystemsAsync(dt); }
    void sendNetInputs() override { sendNetworkInputs(); }
    class InputSystem* getInputSystem() const override { return inputSystem_.get(); }
    class owengine::client::NetworkSystem* getNetworkSystem() const override { return networkSystem_.get(); }

private:
    glm::vec3 getCameraFrontImpl() const;
    glm::vec3 getCameraRightImpl() const;

    void adjustPlayerToTerrain();
    
    std::unique_ptr<InputSystem> inputSystem_;
    std::unique_ptr<CameraSystem> cameraSystem_;
    std::unique_ptr<CameraControllerSystem> cameraControllerSystem_;
    std::unique_ptr<MovementSystem> movementSystem_;
    std::unique_ptr<PhysicsSystem> physicsSystem_;
    std::unique_ptr<client::NetworkSystem> networkSystem_;
    std::unique_ptr<ServerDiscoveryScanner> discoveryScanner_;
    int viewportWidth_{800};
    int viewportHeight_{600};
    bool isFlying_ = false;
};

} // namespace ecs
} // namespace owengine
