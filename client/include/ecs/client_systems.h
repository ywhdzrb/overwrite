#ifndef CLIENT_ECS_SYSTEMS_H
#define CLIENT_ECS_SYSTEMS_H

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <memory>
#include <glm/glm.hpp>
#include "ecs/client_components.h"
#include "ecs/systems.h"  // 共享系统

namespace vgame {
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
class ClientWorld : public World {
public:
    ClientWorld();
    ~ClientWorld() = default;
    
    // 初始化客户端系统
    void initClientSystems(GLFWwindow* window, int viewportWidth, int viewportHeight);
    
    // 更新所有客户端系统
    void updateClientSystems(float deltaTime);
    
    // 获取客户端系统
    InputSystem* getInputSystem() { return inputSystem_.get(); }
    CameraSystem* getCameraSystem() { return cameraSystem_.get(); }
    
    // 创建带相机组件的玩家
    entt::entity createClientPlayer(int viewportWidth, int viewportHeight);
    
private:
    std::unique_ptr<InputSystem> inputSystem_;
    std::unique_ptr<CameraSystem> cameraSystem_;
    int viewportWidth_{800};
    int viewportHeight_{600};
};

} // namespace ecs
} // namespace vgame

#endif // CLIENT_ECS_SYSTEMS_H
