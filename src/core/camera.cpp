// 摄像机实现
// 处理第一人称视角的摄像机控制
#include "camera.h"
#include <cmath>

namespace vgame {

Camera::Camera(int windowWidth, int windowHeight)
    : position(0.0f, 2.0f, 5.0f),  // 从上方开始
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
      jumpForce(8.0f),
      gravity(9.8f) {
    updateCameraVectors();
}

void Camera::update(float deltaTime, bool moveForward, bool moveBackward,
                    bool moveLeft, bool moveRight, bool jump) {
    // 处理跳跃
    if (jump && !isJumping) {
        velocity.y = jumpForce;
        isJumping = true;
    }
    
    // 应用重力
    velocity.y -= gravity * deltaTime;
    
    // 地面碰撞检测
    if (position.y <= 1.5f) {  // 摄像机高度
        position.y = 1.5f;
        velocity.y = 0.0f;
        isJumping = false;
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
        horizontalVelocity -= right;
    }
    if (moveRight) {
        horizontalVelocity += right;
    }
    
    // 归一化并应用速度
    if (glm::length(horizontalVelocity) > 0.0f) {
        horizontalVelocity = glm::normalize(horizontalVelocity) * movementSpeed;
    }
    
    // 更新位置
    position.x += horizontalVelocity.x * deltaTime;
    position.z += horizontalVelocity.z * deltaTime;
    position.y += velocity.y * deltaTime;
}

void Camera::processMouseMovement(float xOffset, float yOffset) {
    xOffset *= mouseSensitivity;
    yOffset *= mouseSensitivity;
    
    yaw += xOffset;
    pitch += yOffset;
    
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