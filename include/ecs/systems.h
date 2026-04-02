#ifndef ECS_SYSTEMS_H
#define ECS_SYSTEMS_H

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <memory>
#include <glm/glm.hpp>

namespace vgame {
namespace ecs {

// 前向声明组件
struct TransformComponent;
struct VelocityComponent;
struct CameraComponent;
struct CameraControllerComponent;
struct PhysicsComponent;
struct InputStateComponent;

// 前向声明系统
class InputSystem;
class MovementSystem;
class PhysicsSystem;
class CameraSystem;
class RenderSystem;

/**
 * @brief ECS 世界管理器
 * 
 * 管理所有实体、组件和系统的核心类
 */
class World {
public:
    World();
    ~World() = default;
    
    // 禁止拷贝
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    
    // 实体管理
    entt::entity createEntity();
    void destroyEntity(entt::entity entity);
    
    // 获取注册表（用于直接操作组件）
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    
    // 系统管理
    void initSystems(GLFWwindow* window, int viewportWidth, int viewportHeight);
    void updateSystems(float deltaTime);
    
    // 获取主相机实体
    entt::entity getMainCamera() const { return mainCamera_; }
    void setMainCamera(entt::entity camera) { mainCamera_ = camera; }
    
    // 获取玩家实体
    entt::entity getPlayer() const { return player_; }
    
    // 便捷方法：创建预设实体
    entt::entity createPlayer(int viewportWidth, int viewportHeight);
    entt::entity createCamera(bool isMainCamera = true);
    
private:
    entt::registry registry_;
    entt::entity mainCamera_{entt::null};
    entt::entity player_{entt::null};
    
    // 系统指针（未来可以改为更灵活的系统管理）
    GLFWwindow* window_{nullptr};
    int viewportWidth_{800};
    int viewportHeight_{600};
};

/**
 * @brief 输入系统
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
 * @brief 移动系统
 * 
 * 根据输入状态更新实体位置和旋转
 */
class MovementSystem {
public:
    explicit MovementSystem(World& world);
    
    void update(float deltaTime);
    
private:
    World& world_;
    
    void updateFreeCamera(entt::entity entity, TransformComponent& transform,
                         VelocityComponent* velocity,
                         CameraControllerComponent& controller,
                         const InputStateComponent& input,
                         float deltaTime);
    
    void updateNormalMovement(entt::entity entity, TransformComponent& transform,
                             VelocityComponent& velocity,
                             CameraControllerComponent& controller,
                             const InputStateComponent& input,
                             float deltaTime);
};

/**
 * @brief 物理系统
 * 
 * 处理重力、碰撞等物理模拟
 */
class PhysicsSystem {
public:
    explicit PhysicsSystem(World& world);
    
    void update(float deltaTime);
    
    // 碰撞体管理
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
    void clearCollisionBoxes();
    
private:
    World& world_;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;  // position, size
    
    bool checkGroundCollision(const glm::vec3& position, float height) const;
    bool checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                           const glm::vec3& pos2, const glm::vec3& size2) const;
};

/**
 * @brief 相机系统
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

} // namespace ecs
} // namespace vgame

#endif // ECS_SYSTEMS_H
