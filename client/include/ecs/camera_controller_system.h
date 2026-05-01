#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "ecs/client_components.h"
#include "ecs/systems.h"  // 共享系统

namespace owengine {
namespace ecs {

/**
 * @brief 相机控制系统（客户端专用）
 * 
 * 处理相机模式切换、自由视角移动、相机方向同步到移动控制器
 * 替代旧的 Camera 类的大部分功能
 */
class CameraControllerSystem {
public:
    explicit CameraControllerSystem(World& world);
    
    void update(float deltaTime);
    
    // 相机模式
    enum class Mode {
        FirstPerson,
        ThirdPerson,
        Free
    };
    
    // 设置相机模式
    void setMode(Mode mode);
    Mode getMode() const { return currentMode_; }
    
    // 第三人称相机参数
    void setThirdPersonDistance(float distance) { thirdPersonDistance_ = distance; }
    void setThirdPersonHeight(float height) { thirdPersonHeight_ = height; }
    
    // 获取主相机的视图和投影矩阵（用于渲染）
    glm::mat4 getMainViewMatrix() const;
    glm::mat4 getMainProjectionMatrix() const;
    
    // 获取相机前向和右向向量（用于移动控制器同步）
    glm::vec3 getCameraFront() const;
    glm::vec3 getCameraRight() const;
    
    // 切换自由视角模式
    void toggleFreeCamera();
    
private:
    World& world_;
    Mode currentMode_{Mode::FirstPerson};
    
    // 第三人称相机参数
    float thirdPersonDistance_{8.0f};
    float thirdPersonHeight_{2.0f};
    
    // 更新相机变换（基于模式和输入）
    void updateCameraTransform(entt::entity cameraEntity, 
                              TransformComponent& transform,
                              CameraComponent& camera,
                              CameraControllerComponent& controller,
                              const InputStateComponent* input,
                              float deltaTime);
    
    // 更新第一人称相机
    void updateFirstPersonCamera(TransformComponent& transform,
                                CameraControllerComponent& controller,
                                const InputStateComponent& input,
                                float deltaTime);
    
    // 更新第三人称相机
    void updateThirdPersonCamera(entt::entity cameraEntity,
                                TransformComponent& transform,
                                CameraComponent& camera,
                                CameraControllerComponent& controller,
                                const InputStateComponent& input,
                                float deltaTime);
    
    // 更新自由视角相机
    void updateFreeCamera(TransformComponent& transform,
                         CameraControllerComponent& controller,
                         const InputStateComponent& input,
                         float deltaTime);
    
    // 同步相机方向到移动控制器
    void syncCameraToMovementController(entt::entity playerEntity,
                                       const TransformComponent& cameraTransform,
                                       const CameraComponent& camera);
    
    // 更新相机矩阵
    void updateCameraMatrices(CameraComponent& camera,
                             const TransformComponent& transform);
    
    // 处理鼠标输入
    void processMouseInput(CameraControllerComponent& controller,
                          TransformComponent& transform,
                          const InputStateComponent& input);
};

} // namespace ecs
} // namespace owengine
