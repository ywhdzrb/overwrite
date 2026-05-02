// 摄像机实现
// 处理第一人称和第三人称视角的摄像机控制
#include "core/camera.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>

namespace owengine {

Camera::Camera(int windowWidth, int windowHeight)
    : position_(0.0f, 1.5f, 5.0f),
      front_(0.0f, 0.0f, -1.0f),
      up_(0.0f, 1.0f, 0.0f),
      right_(1.0f, 0.0f, 0.0f),
      worldUp_(0.0f, 1.0f, 0.0f),
      yaw_(-90.0f),
      pitch_(0.0f),
      movementSpeed_(5.0f),
      mouseSensitivity_(0.1f),
      zoom_(45.0f),
      windowWidth_(windowWidth),
      windowHeight_(windowHeight),
      mode_(Mode::ThirdPerson),
      targetPosition_(0.0f, 0.0f, -5.0f),
      thirdPersonDistance_(8.0f),
      thirdPersonHeight_(2.0f) {
    updateCameraVectors();
    updateThirdPersonCamera();
}

void Camera::toggleMode() noexcept {
    if (mode_ == Mode::FirstPerson) {
        mode_ = Mode::ThirdPerson;
        std::cout << "[Camera] 切换到第三人称模式" << std::endl;
    } else if (mode_ == Mode::ThirdPerson) {
        mode_ = Mode::FirstPerson;
        std::cout << "[Camera] 切换到第一人称模式" << std::endl;
    }
}

void Camera::updateThirdPersonCamera() noexcept {
    if (mode_ != Mode::ThirdPerson) return;
    
    // 计算相机在目标后方的位置
    float horizontalDist = thirdPersonDistance_ * cos(glm::radians(pitch_));
    float verticalDist = thirdPersonDistance_ * sin(glm::radians(pitch_));
    
    // 根据yaw计算水平偏移
    float offsetX = horizontalDist * sin(glm::radians(yaw_ + 180.0f));
    float offsetZ = horizontalDist * cos(glm::radians(yaw_ + 180.0f));
    
    position_.x = targetPosition_.x + offsetX;
    position_.y = targetPosition_.y + thirdPersonHeight_ + verticalDist;
    position_.z = targetPosition_.z + offsetZ;
    
    // 相机朝向目标
    front_ = glm::normalize(targetPosition_ - position_);
    right_ = glm::normalize(glm::cross(front_, worldUp_));
    up_ = glm::normalize(glm::cross(right_, front_));
}

void Camera::processMouseMovement(float xOffset, float yOffset) noexcept {
    xOffset *= mouseSensitivity_;
    yOffset *= mouseSensitivity_;

    yaw_ -= xOffset;
    pitch_ += yOffset;
    
    // 限制俯仰角
    pitch_ = glm::clamp(pitch_, -60.0f, 60.0f);
    
    if (mode_ == Mode::ThirdPerson) {
        updateThirdPersonCamera();
    } else {
        updateCameraVectors();
    }
}

void Camera::updateCameraVectors() noexcept {
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    newFront.y = sin(glm::radians(pitch_));
    newFront.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    
    front_ = glm::normalize(newFront);
    right_ = glm::normalize(glm::cross(front_, worldUp_));
    up_ = glm::normalize(glm::cross(right_, front_));
}

glm::mat4 Camera::getViewMatrix() const noexcept {
    if (mode_ == Mode::ThirdPerson) {
        return glm::lookAt(position_, targetPosition_, up_);
    } else {
        return glm::lookAt(position_, position_ + front_, up_);
    }
}

glm::mat4 Camera::getProjectionMatrix() const noexcept {
    float aspect = static_cast<float>(windowWidth_) / static_cast<float>(windowHeight_);
    return glm::perspective(glm::radians(zoom_), aspect, 0.1f, 100.0f);
}

void Camera::updateFrustum() noexcept {
    glm::mat4 viewProjection = getProjectionMatrix() * getViewMatrix();
    frustum_.update(viewProjection);
}

// ==================== Frustum 实现 ====================

void Frustum::update(const glm::mat4& viewProjection) noexcept {
    const float* m = glm::value_ptr(viewProjection);
    
    // Left plane
    planes_[0].normal.x = m[3] + m[0];
    planes_[0].normal.y = m[7] + m[4];
    planes_[0].normal.z = m[11] + m[8];
    planes_[0].distance = m[15] + m[12];
    
    // Right plane
    planes_[1].normal.x = m[3] - m[0];
    planes_[1].normal.y = m[7] - m[4];
    planes_[1].normal.z = m[11] - m[8];
    planes_[1].distance = m[15] - m[12];
    
    // Bottom plane
    planes_[2].normal.x = m[3] + m[1];
    planes_[2].normal.y = m[7] + m[5];
    planes_[2].normal.z = m[11] + m[9];
    planes_[2].distance = m[15] + m[13];
    
    // Top plane
    planes_[3].normal.x = m[3] - m[1];
    planes_[3].normal.y = m[7] - m[5];
    planes_[3].normal.z = m[11] - m[9];
    planes_[3].distance = m[15] - m[13];
    
    // Near plane
    planes_[4].normal.x = m[3] + m[2];
    planes_[4].normal.y = m[7] + m[6];
    planes_[4].normal.z = m[11] + m[10];
    planes_[4].distance = m[15] + m[14];
    
    // Far plane
    planes_[5].normal.x = m[3] - m[2];
    planes_[5].normal.y = m[7] - m[6];
    planes_[5].normal.z = m[11] - m[10];
    planes_[5].distance = m[15] - m[14];
    
    // 归一化平面
    for (auto& plane : planes_) {
        float length = glm::length(plane.normal);
        if (length > 0.0001f) {
            plane.normal /= length;
            plane.distance /= length;
        }
    }
}

bool Frustum::isPointInside(const glm::vec3& point) const noexcept {
    for (const auto& plane : planes_) {
        if (plane.distanceToPoint(point) < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::isSphereInside(const glm::vec3& center, float radius) const noexcept {
    for (const auto& plane : planes_) {
        if (plane.distanceToPoint(center) < -radius) {
            return false;
        }
    }
    return true;
}

bool Frustum::isAABBInside(const glm::vec3& min, const glm::vec3& max) const noexcept {
    for (const auto& plane : planes_) {
        glm::vec3 positiveVertex = min;
        if (plane.normal.x >= 0) positiveVertex.x = max.x;
        if (plane.normal.y >= 0) positiveVertex.y = max.y;
        if (plane.normal.z >= 0) positiveVertex.z = max.z;
        
        if (plane.distanceToPoint(positiveVertex) < 0) {
            return false;
        }
    }
    return true;
}

} // namespace owengine
