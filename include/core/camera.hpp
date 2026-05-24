#pragma once

/**
 * @file camera.h
 * @brief 相机模块 — 视角/投影矩阵计算、欧拉角/向量更新、第一/三人称/自由视角模式
 *
 * 归属模块：core
 * 核心职责：提供纯数学相机逻辑，不含物理/碰撞/地形查询
 * 依赖关系：glm 数学库
 * 关键设计：renderer 通过 getViewMatrix/getProjectionMatrix 只读访问，
 *           相机位置由 ECS 通过 syncCamera() 驱动
 */

#include <array>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace owengine {

/**
 * @brief 视锥体平面
 */
struct FrustumPlane {
    glm::vec3 normal;
    float distance;
    
    [[nodiscard]] float distanceToPoint(const glm::vec3& point) const noexcept {
        return glm::dot(normal, point) + distance;
    }
};

/**
 * @brief 视锥体类，用于视锥体剔除
 */
class Frustum {
public:
    std::array<FrustumPlane, 6> planes_; // left, right, bottom, top, near, far
    
    void update(const glm::mat4& viewProjection) noexcept;
    [[nodiscard]] bool isPointInside(const glm::vec3& point) const noexcept;
    [[nodiscard]] bool isSphereInside(const glm::vec3& center, float radius) const noexcept;
    [[nodiscard]] bool isAABBInside(const glm::vec3& min, const glm::vec3& max) const noexcept;
};

/**
 * @brief 纯相机类 — 仅处理视角矩阵/投影矩阵数学
 *
 * 职责范围：视图/投影矩阵计算、欧拉角/向量更新、第一/三人称/自由视角模式切换。
 * 物理/碰撞/地形查询已移除（由 ECS PhysicsSystem 和 GameSession 处理）。
 * 相机位置由 ECS 通过 syncCamera() 驱动，Camera::update() 已废弃。
 */
class Camera {
public:
    enum class Mode {
        FirstPerson,    // 第一人称
        ThirdPerson,    // 第三人称
        Free            // 自由视角
    };

    Camera(int windowWidth, int windowHeight);
    ~Camera() = default;

    // 切换自由视角模式
    void toggleFreeCamera(const glm::vec3& targetPos = glm::vec3(0.0f)) noexcept {
        freeCameraMode_ = !freeCameraMode_; 
        if (freeCameraMode_) {
            mode_ = Mode::Free;
        } else {
            mode_ = Mode::ThirdPerson;
            if (targetPos != glm::vec3(0.0f)) {
                targetPosition_ = targetPos;
                skipSyncOnce_ = true;
            }
        }
    }
    
    [[nodiscard]] bool isFreeCameraMode() const noexcept { return freeCameraMode_; }
    [[nodiscard]] bool needsSyncSkip() const noexcept { return skipSyncOnce_; }
    void clearSyncSkip() noexcept { skipSyncOnce_ = false; }
    
    void setMode(Mode m) noexcept { mode_ = m; }
    [[nodiscard]] Mode getMode() const noexcept { return mode_; }
    void toggleMode() noexcept;
    
    void processMouseMovement(float xOffset, float yOffset) noexcept;
    
    [[nodiscard]] glm::mat4 getViewMatrix() const noexcept;
    [[nodiscard]] glm::mat4 getProjectionMatrix() const noexcept;
    
    [[nodiscard]] glm::vec3 getPosition() const noexcept { return position_; }
    [[nodiscard]] glm::vec3 getFront() const noexcept { return front_; }
    [[nodiscard]] glm::vec3 getRight() const noexcept { return right_; }
    [[nodiscard]] glm::vec3 getUp() const noexcept { return up_; }
    
    void setPosition(const glm::vec3& pos) noexcept { position_ = pos; }
    void setFront(const glm::vec3& f) noexcept { front_ = f; }
    
    void setYaw(float y) noexcept { yaw_ = y; if (mode_ == Mode::ThirdPerson) updateThirdPersonCamera(); else updateCameraVectors(); }
    void setPitch(float p) noexcept { pitch_ = p; if (mode_ == Mode::ThirdPerson) updateThirdPersonCamera(); else updateCameraVectors(); }
    void setYawPitch(float y, float p) noexcept { yaw_ = y; pitch_ = p; if (mode_ == Mode::ThirdPerson) updateThirdPersonCamera(); else updateCameraVectors(); }
    
    [[nodiscard]] float getYaw() const noexcept { return yaw_; }
    [[nodiscard]] float getPitch() const noexcept { return pitch_; }
    
    void setMovementSpeed(float speed) noexcept { movementSpeed_ = speed; }
    void setMouseSensitivity(float sensitivity) noexcept { mouseSensitivity_ = sensitivity; }
    [[nodiscard]] float getMovementSpeed() const noexcept { return movementSpeed_; }
    [[nodiscard]] float getMouseSensitivity() const noexcept { return mouseSensitivity_; }
    
    void setTarget(const glm::vec3& target) noexcept { targetPosition_ = target; }
    [[nodiscard]] glm::vec3 getTarget() const noexcept { return targetPosition_; }
    
    void setThirdPersonDistance(float d) noexcept { thirdPersonDistance_ = d; }
    [[nodiscard]] float getThirdPersonDistance() const noexcept { return thirdPersonDistance_; }
    
    [[nodiscard]] const Frustum& getFrustum() const noexcept { return frustum_; }
    void updateFrustum() noexcept;

private:
    void updateCameraVectors() noexcept;
    void updateThirdPersonCamera() noexcept;

    glm::vec3 position_{0.0f, 1.5f, 5.0f};
    glm::vec3 front_{0.0f, 0.0f, -1.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};
    glm::vec3 worldUp_{0.0f, 1.0f, 0.0f};
    
    float yaw_{-90.0f};
    float pitch_{0.0f};
    float movementSpeed_{5.0f};
    float mouseSensitivity_{0.1f};
    float zoom_{45.0f};
    
    int windowWidth_{0};
    int windowHeight_{0};
    
    bool freeCameraMode_{false};
    bool skipSyncOnce_{false};
    
    Mode mode_{Mode::ThirdPerson};
    glm::vec3 targetPosition_{0.0f, 0.0f, -5.0f};
    float thirdPersonDistance_{8.0f};
    float thirdPersonHeight_{2.0f};
    
    Frustum frustum_;
};

} // namespace owengine
