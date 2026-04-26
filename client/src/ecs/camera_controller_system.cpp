// 相机控制系统实现
#include "ecs/camera_controller_system.h"
#include "utils/logger.h"
#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace owengine {
namespace ecs {

CameraControllerSystem::CameraControllerSystem(World& world) : world_(world) {
    std::cout << "[CameraControllerSystem] 初始化完成" << std::endl;
}

void CameraControllerSystem::update(float deltaTime) {
    // 查找所有带有相机和相机控制器的实体
    auto view = world_.registry().view<TransformComponent, CameraComponent, CameraControllerComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& camera = view.get<CameraComponent>(entity);
        auto& controller = view.get<CameraControllerComponent>(entity);
        
        // 获取玩家实体的输入状态
        const InputStateComponent* input = nullptr;
        auto player = world_.getPlayer();
        if (world_.registry().valid(player) && world_.registry().all_of<InputStateComponent>(player)) {
            input = &world_.registry().get<InputStateComponent>(player);
        }
        
        // 处理鼠标输入
        if (input && (input->mouseDeltaX != 0.0f || input->mouseDeltaY != 0.0f)) {
            processMouseInput(controller, transform, *input);
        }
        
        // 更新相机变换
        updateCameraTransform(entity, transform, camera, controller, input, deltaTime);
        
        // 更新相机矩阵
        updateCameraMatrices(camera, transform);
        
        // 如果是主相机，同步方向到玩家移动控制器
        if (entity == world_.getMainCamera()) {
            if (world_.registry().valid(player)) {
                syncCameraToMovementController(player, transform, camera);
            }
        }
    }
}

void CameraControllerSystem::setMode(Mode mode) {
    if (currentMode_ != mode) {
        currentMode_ = mode;
        std::cout << "[CameraControllerSystem] 切换到"
                  << (mode == Mode::FirstPerson ? "第一人称" :
                      mode == Mode::ThirdPerson ? "第三人称" : "自由视角")
                  << "模式" << std::endl;
    }
}

glm::mat4 CameraControllerSystem::getMainViewMatrix() const {
    if (world_.registry().valid(world_.getMainCamera())) {
        auto& camera = world_.registry().get<CameraComponent>(world_.getMainCamera());
        auto& transform = world_.registry().get<TransformComponent>(world_.getMainCamera());
        return camera.getViewMatrix(transform);
    }
    return glm::mat4(1.0f);
}

glm::mat4 CameraControllerSystem::getMainProjectionMatrix() const {
    if (world_.registry().valid(world_.getMainCamera())) {
        auto& camera = world_.registry().get<CameraComponent>(world_.getMainCamera());
        return camera.getProjectionMatrix();
    }
    return glm::mat4(1.0f);
}

glm::vec3 CameraControllerSystem::getCameraFront() const {
    if (world_.registry().valid(world_.getMainCamera())) {
        auto& transform = world_.registry().get<TransformComponent>(world_.getMainCamera());
        return transform.getFront();
    }
    return glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 CameraControllerSystem::getCameraRight() const {
    if (world_.registry().valid(world_.getMainCamera())) {
        auto& transform = world_.registry().get<TransformComponent>(world_.getMainCamera());
        return transform.getRight();
    }
    return glm::vec3(1.0f, 0.0f, 0.0f);
}

void CameraControllerSystem::toggleFreeCamera() {
    if (currentMode_ == Mode::Free) {
        setMode(Mode::ThirdPerson);
    } else {
        setMode(Mode::Free);
    }
}

// ==================== 私有方法实现 ====================

void CameraControllerSystem::updateCameraTransform(entt::entity cameraEntity,
                                                  TransformComponent& transform,
                                                  CameraComponent& camera,
                                                  CameraControllerComponent& controller,
                                                  const InputStateComponent* input,
                                                  float deltaTime) {
    if (!input) return;
    
    // 根据模式更新相机
    switch (currentMode_) {
        case Mode::FirstPerson:
            updateFirstPersonCamera(transform, controller, *input, deltaTime);
            break;
        case Mode::ThirdPerson:
            updateThirdPersonCamera(cameraEntity, transform, camera, controller, *input, deltaTime);
            break;
        case Mode::Free:
            updateFreeCamera(transform, controller, *input, deltaTime);
            break;
    }
    
    // 处理自由视角切换输入
    if (input->freeCameraToggle) {
        toggleFreeCamera();
    }
}

void CameraControllerSystem::updateFirstPersonCamera(TransformComponent& transform,
                                                    CameraControllerComponent& controller,
                                                    const InputStateComponent& input,
                                                    float deltaTime) {
    // 第一人称：相机位置与玩家位置同步
    auto player = world_.getPlayer();
    if (!world_.registry().valid(player)) return;
    
    auto& playerTransform = world_.registry().get<TransformComponent>(player);
    transform.position = playerTransform.position;
    
    // 旋转已由鼠标输入处理，无需额外更新
}

void CameraControllerSystem::updateThirdPersonCamera(entt::entity cameraEntity,
                                                    TransformComponent& transform,
                                                    CameraComponent& camera,
                                                    CameraControllerComponent& controller,
                                                    const InputStateComponent& input,
                                                    float deltaTime) {
    // 获取玩家实体（相机应该跟随的实体）
    auto player = world_.getPlayer();
    if (!world_.registry().valid(player)) return;
    
    auto& playerTransform = world_.registry().get<TransformComponent>(player);
    
    // 计算相机在玩家后方的位置
    float horizontalDist = thirdPersonDistance_ * cos(glm::radians(transform.pitch));
    float verticalDist = thirdPersonDistance_ * sin(glm::radians(transform.pitch));
    
    // 根据yaw计算水平偏移（yaw=0时相机在+Z方向，即目标后方）
    float offsetX = -horizontalDist * sin(glm::radians(transform.yaw));
    float offsetZ = horizontalDist * cos(glm::radians(transform.yaw));
    
    // 设置相机位置：玩家位置 + 偏移
    transform.position.x = playerTransform.position.x + offsetX;
    transform.position.y = playerTransform.position.y + thirdPersonHeight_ + verticalDist;
    transform.position.z = playerTransform.position.z + offsetZ;
    
    // 相机朝向玩家
    glm::vec3 target = playerTransform.position;
    target.y += 1.0f;  // 看向玩家头部高度
    
    // 计算相机到目标的方向向量
    glm::vec3 direction = glm::normalize(target - transform.position);
    
    // 从方向向量计算欧拉角（适配新的 yaw 定义：yaw=0 → front=-Z）
    float pitch = glm::degrees(asin(direction.y));
    float yaw = glm::degrees(atan2(direction.x, -direction.z));
    
    // 限制俯仰角，避免万向节锁
    transform.pitch = glm::clamp(pitch, -89.0f, 89.0f);
    transform.yaw = yaw;
    
    // 更新前向量
    transform.updateRotationFromEuler();
}

void CameraControllerSystem::updateFreeCamera(TransformComponent& transform,
                                             CameraControllerComponent& controller,
                                             const InputStateComponent& input,
                                             float deltaTime) {
    // 自由视角：相机可以自由移动
    float speed = controller.freeCameraSpeed;
    if (input.sprint) speed *= controller.sprintMultiplier;
    
    glm::vec3 front = transform.getFront();
    glm::vec3 right = transform.getRight();
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    
    glm::vec3 moveDelta(0.0f);
    if (input.moveForward) moveDelta += front * speed * deltaTime;
    if (input.moveBackward) moveDelta -= front * speed * deltaTime;
    if (input.moveLeft) moveDelta -= right * speed * deltaTime;
    if (input.moveRight) moveDelta += right * speed * deltaTime;
    if (input.spaceHeld) moveDelta += worldUp * speed * deltaTime;
    if (input.shiftHeld) moveDelta -= worldUp * speed * deltaTime;
    
    transform.position += moveDelta;
}

void CameraControllerSystem::syncCameraToMovementController(entt::entity playerEntity,
                                                           const TransformComponent& cameraTransform,
                                                           const CameraComponent& camera) {
    if (!world_.registry().valid(playerEntity)) return;
    
    // 获取玩家的移动控制器组件
    auto* controller = world_.registry().try_get<MovementControllerComponent>(playerEntity);
    if (!controller) return;
    
    // 同步相机方向到移动控制器
    controller->moveFront = cameraTransform.getFront();
    controller->moveRight = cameraTransform.getRight();
    

    
    // 水平化方向（忽略Y分量，用于水平移动）
    controller->moveFront.y = 0.0f;
    if (glm::length(controller->moveFront) > 0.0f) {
        controller->moveFront = glm::normalize(controller->moveFront);
    }
    
    controller->moveRight.y = 0.0f;
    if (glm::length(controller->moveRight) > 0.0f) {
        controller->moveRight = glm::normalize(controller->moveRight);
    }
    

}

void CameraControllerSystem::updateCameraMatrices(CameraComponent& camera,
                                                 const TransformComponent& transform) {
    // 更新相机矩阵缓存
    camera.viewMatrix = camera.getViewMatrix(transform);
    camera.projectionMatrix = camera.getProjectionMatrix();
    camera.dirty = false;
}

void CameraControllerSystem::processMouseInput(CameraControllerComponent& controller,
                                              TransformComponent& transform,
                                              const InputStateComponent& input) {
    // 处理鼠标旋转（右移增加 yaw，使视角右转）
    transform.yaw += input.mouseDeltaX * controller.mouseSensitivity;
    transform.pitch += input.mouseDeltaY * controller.mouseSensitivity;
    
    // 限制俯仰角
    transform.pitch = glm::clamp(transform.pitch, -89.0f, 89.0f);
    
    // 更新四元数旋转
    transform.updateRotationFromEuler();
}

} // namespace ecs
} // namespace owengine