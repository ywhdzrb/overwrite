#ifndef ECS_COMPONENTS_H
#define ECS_COMPONENTS_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace vgame {
namespace ecs {

/**
 * @brief 变换组件 - 位置、旋转、缩放
 */
struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // w, x, y, z
    glm::vec3 scale{1.0f};
    
    // 欧拉角（用于相机等需要欧拉角控制的场景）
    float yaw{0.0f};
    float pitch{0.0f};
    float roll{0.0f};
    
    // 获取前向量（基于欧拉角）
    glm::vec3 getFront() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;
    
    // 从欧拉角更新四元数
    void updateRotationFromEuler();
    
    // 获取模型矩阵
    glm::mat4 getModelMatrix() const;
};

/**
 * @brief 速度组件
 */
struct VelocityComponent {
    glm::vec3 linear{0.0f};
    glm::vec3 angular{0.0f};
};

/**
 * @brief 相机组件
 */
struct CameraComponent {
    float fov{45.0f};
    float nearPlane{0.1f};
    float farPlane{100.0f};
    int viewportWidth{800};
    int viewportHeight{600};
    
    // 缓存的矩阵
    mutable glm::mat4 viewMatrix{1.0f};
    mutable glm::mat4 projectionMatrix{1.0f};
    mutable bool dirty{true};
    
    glm::mat4 getViewMatrix(const TransformComponent& transform) const;
    glm::mat4 getProjectionMatrix() const;
};

/**
 * @brief 相机控制器组件
 */
struct CameraControllerComponent {
    float movementSpeed{5.0f};
    float sprintMultiplier{2.0f};
    float mouseSensitivity{0.1f};
    float freeCameraSpeed{15.0f};
    
    // 模式
    bool freeCameraMode{false};
    bool isMainCamera{true};
    
    // 第三人称模式下的移动方向（由 Camera 同步）
    glm::vec3 cameraFront{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraRight{1.0f, 0.0f, 0.0f};
};

/**
 * @brief 物理组件
 */
struct PhysicsComponent {
    float gravity{15.0f};
    float groundHeight{-1.5f};
    float jumpForce{5.5f};
    
    bool isJumping{false};
    bool isGrounded{true};
    bool useGravity{true};
    
    // 碰撞体参数
    float colliderHeight{1.8f};
    float colliderRadius{0.3f};
};

/**
 * @brief 输入组件 - 存储输入状态
 */
struct InputStateComponent {
    // 移动输入
    bool moveForward{false};
    bool moveBackward{false};
    bool moveLeft{false};
    bool moveRight{false};
    bool jump{false};
    bool sprint{false};
    bool freeCameraToggle{false};
    bool spaceHeld{false};
    bool shiftHeld{false};
    
    // 鼠标输入
    float mouseDeltaX{0.0f};
    float mouseDeltaY{0.0f};
    
    // 重置输入状态
    void reset() {
        moveForward = moveBackward = moveLeft = moveRight = false;
        jump = sprint = freeCameraToggle = false;
        spaceHeld = shiftHeld = false;
        mouseDeltaX = mouseDeltaY = 0.0f;
    }
};

/**
 * @brief 玩家标签组件
 */
struct PlayerTag {
    int playerId{0};
};

/**
 * @brief 名称组件
 */
struct NameComponent {
    std::string name;
};

/**
 * @brief 渲染组件 - 用于可渲染实体
 */
struct RenderComponent {
    // 模型路径
    std::string modelPath;
    
    // 变换参数
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};  // 欧拉角度数
    glm::vec3 scale{1.0f};
    
    // 可见性
    bool visible{true};
    bool castShadows{true};
    bool receiveShadows{true};
    
    // 是否需要更新
    bool dirty{true};
};

/**
 * @brief 光源组件
 */
struct LightComponent {
    enum class Type { Directional, Point, Spot };
    
    Type type{Type::Point};
    glm::vec3 color{1.0f};
    float intensity{1.0f};
    
    // 聚光灯/方向光参数
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    
    // 点光源/聚光灯参数
    float constant{1.0f};
    float linear{0.09f};
    float quadratic{0.032f};
    
    // 聚光灯参数
    float innerCutoff{12.5f};  // 度数
    float outerCutoff{17.5f};  // 度数
    
    bool enabled{true};
    bool castShadows{false};
};

} // namespace ecs
} // namespace vgame

#endif // ECS_COMPONENTS_H
