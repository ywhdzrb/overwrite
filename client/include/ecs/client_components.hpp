#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace owengine {
namespace ecs {

// 前向声明共享组件
struct TransformComponent;

/**
 * @brief 相机组件（客户端专用）
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
 * @brief 相机控制器组件（客户端专用）
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
 * @brief 渲染组件（客户端专用）
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
 * @brief 光源组件（客户端专用）
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
} // namespace owengine
