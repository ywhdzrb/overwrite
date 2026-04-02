// 摄像机实现
// 处理第一人称视角的摄像机控制
#include "camera.h"
#include <cmath>
#include <iostream>


namespace vgame {

Camera::Camera(int windowWidth, int windowHeight)
    : position(0.0f, 1.5f, 5.0f),  // 恢复到原来的位置
      front(0.0f, 0.0f, -1.0f),
      up(0.0f, 1.0f, 0.0f),
      right(1.0f, 0.0f, 0.0f),
      worldUp(0.0f, 1.0f, 0.0f),
      yaw(-90.0f),
      pitch(0.0f),  // 平视前方，不向下看
      movementSpeed(5.0f),
      mouseSensitivity(0.1f),  // 更高的灵敏度
      zoom(45.0f),
      windowWidth(windowWidth),
      windowHeight(windowHeight),
      velocity(0.0f),
      isJumping(false),
      jumpForce(5.5f),  // 跳跃初速度，可以跳到约1.5米高
      gravity(15.0f),  // 重力加速度，使下落更快
      groundHeight(1.5f),  // 地面高度
      freeCameraMode(false),
      freeCameraSpeed(15.0f) {  // 自由视角速度更快
    updateCameraVectors();
}

void Camera::update(float deltaTime, bool moveForward, bool moveBackward,
                    bool moveLeft, bool moveRight, bool jump, bool freeCameraToggle,
                    bool shiftPressed, bool spaceHeld) {
    // 切换自由视角模式（只在按键刚按下时触发）
    if (freeCameraToggle) {
        freeCameraMode = !freeCameraMode;
        if (freeCameraMode) {
            std::cout << "[Camera] 自由视角模式已启用" << std::endl;
        } else {
            std::cout << "[Camera] 自由视角模式已禁用" << std::endl;
        }
    }
    
    if (freeCameraMode) {
        // 自由视角模式：可以自由移动，不受物理限制
        float speed = freeCameraSpeed;
        
        if (moveForward) {
            position += front * speed * deltaTime;
        }
        if (moveBackward) {
            position -= front * speed * deltaTime;
        }
        if (moveLeft) {
            position -= right * speed * deltaTime;
        }
        if (moveRight) {
            position += right * speed * deltaTime;
        }
        
        // 空格键向上移动（持续检测，可以长按）
        if (spaceHeld) {
            position += worldUp * speed * deltaTime;
        }

        // Shift键向下移动
        if (shiftPressed) {
            position -= worldUp * speed * deltaTime;
        }
    } else {
        // 普通模式：有物理限制
        // 处理跳跃（物理模拟）

        // 按下空格键且在地面附近时，开始跳跃
        if (jump && !isJumping && position.y <= groundHeight + 0.1f) {
            velocity.y = jumpForce;  // 给一个向上的初始速度
            isJumping = true;
            std::cout << "[Camera] 跳跃触发: position.y=" << position.y << ", velocity.y=" << velocity.y << ", groundHeight=" << groundHeight << std::endl;
        }

        // 如果在跳跃中或不在地面上，应用重力
        if (isJumping || position.y > groundHeight) {
            float prevVelocityY = velocity.y;
            velocity.y -= gravity * deltaTime;  // 重力向下
            float prevPositionY = position.y;
            position.y += velocity.y * deltaTime;  // 更新高度

            if (isJumping) {
                static int debugCounter = 0;
                debugCounter++;
                if (debugCounter < 10) {  // 只输出前10帧，避免刷屏
                    std::cout << "[Camera] 跳跃更新: prevVelocityY=" << prevVelocityY << ", newVelocityY=" << velocity.y
                              << ", prevPositionY=" << prevPositionY << ", newPositionY=" << position.y << std::endl;
                }
            }

            // 检查是否落地
            if (position.y <= groundHeight) {
                position.y = groundHeight;
                velocity.y = 0.0f;
                isJumping = false;
                std::cout << "[Camera] 落地: position.y=" << position.y << std::endl;
            }
        } else {
            // 确保在地面上时不会下沉
            position.y = groundHeight;
            velocity.y = 0.0f;
        }
        
        // 处理移动（水平方向）
        glm::vec3 horizontalVelocity(0.0f);
        
        if (moveForward) {
            horizontalVelocity += front;
        }
        if (moveBackward) {
            horizontalVelocity -= front;
        }
        if (moveLeft) {
            horizontalVelocity -= right;  // 向左移动：减去右向量
        }
        if (moveRight) {
            horizontalVelocity += right;  // 向右移动：加上右向量
        }
        
        // 归一化并应用速度
        if (glm::length(horizontalVelocity) > 0.0f) {
            horizontalVelocity = glm::normalize(horizontalVelocity) * movementSpeed;
        }
        
        // 更新位置
        position.x += horizontalVelocity.x * deltaTime;
        position.z += horizontalVelocity.z * deltaTime;
    }
}

void Camera::processMouseMovement(float xOffset, float yOffset) {
    xOffset *= mouseSensitivity;
    yOffset *= mouseSensitivity;

    yaw += xOffset;  // X 轴方向：鼠标向左，xOffset为负，yaw减少（向左转）
    pitch -= yOffset;  // Y 轴方向反转：鼠标向上，镜头向上看
    
    // 限制俯仰角
    if (pitch > 89.0f) {
        pitch = 89.0f;
    }
    if (pitch < -89.0f) {
        pitch = -89.0f;
    }
    
    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    // 根据欧拉角计算前向量
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    
    front = glm::normalize(newFront);
    
    // 重新计算右向量和上向量
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    float aspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    return glm::perspective(glm::radians(zoom), aspect, 0.1f, 100.0f);
}

} // namespace vgame