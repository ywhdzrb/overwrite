// 摄像机实现
// 处理第一人称视角的摄像机控制
#include "camera.h"
#include <cmath>
#include <iostream>


namespace vgame {

Camera::Camera(int windowWidth, int windowHeight)
    : position(0.0f, 1.5f, 5.0f),  // 从地面高度开始
      front(0.0f, 0.0f, -1.0f),
      up(0.0f, 1.0f, 0.0f),
      right(1.0f, 0.0f, 0.0f),
      worldUp(0.0f, 1.0f, 0.0f),
      yaw(-90.0f),
      pitch(-20.0f),  // 稍微向下看，以便能看到地板
      movementSpeed(5.0f),
      mouseSensitivity(0.1f),  // 更高的灵敏度
      zoom(45.0f),
      windowWidth(windowWidth),
      windowHeight(windowHeight),
      velocity(0.0f),
      isJumping(false),
      jumpForce(5.5f),  // 跳跃初速度，可以跳到约1.5米高
      gravity(15.0f) {  // 重力加速度，使下落更快
    updateCameraVectors();
}

void Camera::update(float deltaTime, bool moveForward, bool moveBackward,
                    bool moveLeft, bool moveRight, bool jump) {
    // 处理跳跃（物理模拟）
    const float groundHeight = 1.5f;
    
    // 按下空格键且在地面时，开始跳跃
    if (jump && !isJumping && position.y >= groundHeight - 0.01f) {
        velocity.y = -jumpForce;  // 给一个向上的初始速度（y轴向下为正，所以向上是负值）
        isJumping = true;
    }
    
    // 如果在跳跃中或不在地面上，应用重力
    if (isJumping || position.y < groundHeight - 0.001f) {
        velocity.y += gravity * deltaTime;  // 重力加速（向下为正）
        position.y += velocity.y * deltaTime;  // 更新高度
        
        // 检查是否落地
        if (position.y >= groundHeight) {
            position.y = groundHeight;
            velocity.y = 0.0f;
            isJumping = false;
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

void Camera::processMouseMovement(float xOffset, float yOffset) {
    xOffset *= mouseSensitivity;
    yOffset *= mouseSensitivity;
    
    yaw += xOffset;  // X 轴方向：鼠标向左，xOffset为负，yaw减少（向左转）
    pitch += yOffset;  // Y 轴方向保持不变：鼠标向上，镜头向上看
    
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