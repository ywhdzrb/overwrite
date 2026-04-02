// 摄像机实现
// 处理第一人称和第三人称视角的摄像机控制
#include "camera.h"
#include <cmath>
#include <iostream>


namespace vgame {

Camera::Camera(int windowWidth, int windowHeight)
    : position(0.0f, 1.5f, 5.0f),
      front(0.0f, 0.0f, -1.0f),
      up(0.0f, 1.0f, 0.0f),
      right(1.0f, 0.0f, 0.0f),
      worldUp(0.0f, 1.0f, 0.0f),
      yaw(-90.0f),
      pitch(0.0f),
      movementSpeed(5.0f),
      mouseSensitivity(0.1f),
      zoom(45.0f),
      windowWidth(windowWidth),
      windowHeight(windowHeight),
      velocity(0.0f),
      isJumping(false),
      jumpForce(5.5f),
      gravity(15.0f),
      groundHeight(1.5f),
      freeCameraMode(false),
      freeCameraSpeed(15.0f),
      mode(Mode::ThirdPerson),  // 默认第三人称
      targetPosition(0.0f, 0.0f, -5.0f),
      thirdPersonDistance(8.0f),
      thirdPersonHeight(2.0f) {
    updateCameraVectors();
    updateThirdPersonCamera();
}

void Camera::toggleMode() {
    if (mode == Mode::FirstPerson) {
        mode = Mode::ThirdPerson;
        std::cout << "[Camera] 切换到第三人称模式" << std::endl;
    } else if (mode == Mode::ThirdPerson) {
        mode = Mode::FirstPerson;
        std::cout << "[Camera] 切换到第一人称模式" << std::endl;
    }
}

void Camera::updateThirdPersonCamera() {
    if (mode != Mode::ThirdPerson) return;
    
    // 计算相机在目标后方的位置
    // 相机位于目标后方，偏上方
    float horizontalDist = thirdPersonDistance * cos(glm::radians(pitch));
    float verticalDist = thirdPersonDistance * sin(glm::radians(pitch));
    
    // 根据yaw计算水平偏移
    float offsetX = horizontalDist * sin(glm::radians(yaw + 180.0f));  // +180让相机在目标后方
    float offsetZ = horizontalDist * cos(glm::radians(yaw + 180.0f));
    
    position.x = targetPosition.x + offsetX;
    position.y = targetPosition.y + thirdPersonHeight + verticalDist;
    position.z = targetPosition.z + offsetZ;
    
    // 相机朝向目标
    front = glm::normalize(targetPosition - position);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

void Camera::update(float deltaTime, bool moveForward, bool moveBackward,
                    bool moveLeft, bool moveRight, bool jump, bool freeCameraToggle,
                    bool shiftPressed, bool spaceHeld) {
    // 切换自由视角模式
    if (freeCameraToggle) {
        if (mode == Mode::Free) {
            mode = Mode::ThirdPerson;
            std::cout << "[Camera] 退出自由视角，回到第三人称" << std::endl;
        } else {
            mode = Mode::Free;
            std::cout << "[Camera] 自由视角模式已启用" << std::endl;
        }
    }
    
    if (mode == Mode::Free) {
        // 自由视角模式
        float speed = freeCameraSpeed;
        
        if (moveForward) position += front * speed * deltaTime;
        if (moveBackward) position -= front * speed * deltaTime;
        if (moveLeft) position -= right * speed * deltaTime;
        if (moveRight) position += right * speed * deltaTime;
        if (spaceHeld) position += worldUp * speed * deltaTime;
        if (shiftPressed) position -= worldUp * speed * deltaTime;
    } else if (mode == Mode::ThirdPerson) {
        // 第三人称模式：移动目标，相机跟随
        float speed = movementSpeed;
        if (shiftPressed) speed *= 2.0f;
        
        // 计算水平移动方向（基于相机朝向）
        glm::vec3 moveDir(0.0f);
        glm::vec3 forward = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
        glm::vec3 rightDir = glm::normalize(glm::vec3(right.x, 0.0f, right.z));
        
        if (moveForward) moveDir += forward;
        if (moveBackward) moveDir -= forward;
        if (moveLeft) moveDir -= rightDir;
        if (moveRight) moveDir += rightDir;
        
        if (glm::length(moveDir) > 0.0f) {
            moveDir = glm::normalize(moveDir) * speed * deltaTime;
            targetPosition += moveDir;
        }
        
        // 跳跃
        if (jump && !isJumping && targetPosition.y <= groundHeight) {
            velocity.y = jumpForce;
            isJumping = true;
        }
        
        // 重力
        if (isJumping || targetPosition.y > groundHeight) {
            velocity.y -= gravity * deltaTime;
            targetPosition.y += velocity.y * deltaTime;
            
            if (targetPosition.y <= groundHeight) {
                targetPosition.y = groundHeight;
                velocity.y = 0.0f;
                isJumping = false;
            }
        }
        
        // 更新相机位置
        updateThirdPersonCamera();
    } else {
        // 第一人称模式
        if (jump && !isJumping && position.y <= groundHeight + 0.1f) {
            velocity.y = jumpForce;
            isJumping = true;
        }

        if (isJumping || position.y > groundHeight) {
            velocity.y -= gravity * deltaTime;
            position.y += velocity.y * deltaTime;

            if (position.y <= groundHeight) {
                position.y = groundHeight;
                velocity.y = 0.0f;
                isJumping = false;
            }
        } else {
            position.y = groundHeight;
            velocity.y = 0.0f;
        }
        
        // 水平移动
        glm::vec3 horizontalVelocity(0.0f);
        
        if (moveForward) horizontalVelocity += front;
        if (moveBackward) horizontalVelocity -= front;
        if (moveLeft) horizontalVelocity -= right;
        if (moveRight) horizontalVelocity += right;
        
        if (glm::length(horizontalVelocity) > 0.0f) {
            horizontalVelocity = glm::normalize(horizontalVelocity) * movementSpeed;
        }
        
        position.x += horizontalVelocity.x * deltaTime;
        position.z += horizontalVelocity.z * deltaTime;
    }
}

void Camera::processMouseMovement(float xOffset, float yOffset) {
    xOffset *= mouseSensitivity;
    yOffset *= mouseSensitivity;

    yaw += xOffset;
    pitch -= yOffset;  // 反转Y
    
    // 限制俯仰角
    pitch = glm::clamp(pitch, -60.0f, 60.0f);
    
    if (mode == Mode::ThirdPerson) {
        updateThirdPersonCamera();
    } else {
        updateCameraVectors();
    }
}

void Camera::updateCameraVectors() {
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    
    front = glm::normalize(newFront);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

glm::mat4 Camera::getViewMatrix() const {
    if (mode == Mode::ThirdPerson) {
        // 第三人称：看向目标
        return glm::lookAt(position, targetPosition, up);
    } else {
        // 第一人称/自由视角：看向前方
        return glm::lookAt(position, position + front, up);
    }
}

glm::mat4 Camera::getProjectionMatrix() const {
    float aspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    return glm::perspective(glm::radians(zoom), aspect, 0.1f, 100.0f);
}

} // namespace vgame
