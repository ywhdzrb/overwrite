#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include <string>
#include <functional>
#include <vector>
#include <array>

namespace vgame {

/**
 * @brief 视锥体平面
 */
struct FrustumPlane {
    glm::vec3 normal;
    float distance;
    
    float distanceToPoint(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

/**
 * @brief 视锥体类，用于视锥体剔除
 */
class Frustum {
public:
    std::array<FrustumPlane, 6> planes; // left, right, bottom, top, near, far
    
    /**
     * @brief 从视图投影矩阵提取视锥体平面
     */
    void update(const glm::mat4& viewProjection);
    
    /**
     * @brief 检查点是否在视锥体内
     */
    bool isPointInside(const glm::vec3& point) const;
    
    /**
     * @brief 检查球体是否在视锥体内
     * @param center 球心
     * @param radius 半径
     */
    bool isSphereInside(const glm::vec3& center, float radius) const;
    
    /**
     * @brief 检查轴对齐包围盒是否在视锥体内
     * @param min 最小点
     * @param max 最大点
     */
    bool isAABBInside(const glm::vec3& min, const glm::vec3& max) const;
};

/**
 * @brief 地形高度查询函数类型（Camera 使用）
 */
using CameraTerrainQuery = std::function<float(float x, float z)>;

class Camera {
public:
    enum class Mode {
        FirstPerson,    // 第一人称
        ThirdPerson,    // 第三人称
        Free            // 自由视角
    };

    Camera(int windowWidth, int windowHeight);
    ~Camera() = default;

    // 更新摄像机位置和方向
    void update(float deltaTime, bool moveForward, bool moveBackward, 
                bool moveLeft, bool moveRight, bool jump, bool freeCameraToggle,
                bool shiftPressed, bool spaceHeld);
    
    // 切换自由视角模式
    void toggleFreeCamera() { 
        freeCameraMode = !freeCameraMode; 
        if (freeCameraMode) {
            mode = Mode::Free;
        } else {
            mode = Mode::ThirdPerson;
        }
    }
    
    // 检查是否在自由视角模式
    bool isFreeCameraMode() const { return freeCameraMode; }
    
    // 切换相机模式
    void setMode(Mode m) { mode = m; }
    Mode getMode() const { return mode; }
    void toggleMode();
    
    // 处理鼠标移动
    void processMouseMovement(float xOffset, float yOffset);
    
    // 获取视图矩阵
    glm::mat4 getViewMatrix() const;
    
    // 获取投影矩阵
    glm::mat4 getProjectionMatrix() const;
    
    // 获取摄像机位置
    glm::vec3 getPosition() const { return position; }
    
    // 获取摄像机方向
    glm::vec3 getFront() const { return front; }
    
    // 获取摄像机右向量
    glm::vec3 getRight() const { return right; }
    
    // 获取摄像机上向量
    glm::vec3 getUp() const { return up; }
    
    // 设置摄像机位置
    void setPosition(const glm::vec3& pos) { position = pos; }
    
    // 设置摄像机朝向（直接设置 front 向量）
    void setFront(const glm::vec3& f) { front = f; }
    
    // 设置欧拉角（会重新计算 front 向量）
    void setYaw(float y) { yaw = y; if (mode == Mode::ThirdPerson) updateThirdPersonCamera(); else updateCameraVectors(); }
    void setPitch(float p) { pitch = p; if (mode == Mode::ThirdPerson) updateThirdPersonCamera(); else updateCameraVectors(); }
    void setYawPitch(float y, float p) { yaw = y; pitch = p; if (mode == Mode::ThirdPerson) updateThirdPersonCamera(); else updateCameraVectors(); }
    
    // 获取欧拉角
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    
    // 设置摄像机速度
    void setMovementSpeed(float speed) { movementSpeed = speed; }
    
    // 设置鼠标灵敏度
    void setMouseSensitivity(float sensitivity) { mouseSensitivity = sensitivity; }
    
    // 获取摄像机速度
    float getMovementSpeed() const { return movementSpeed; }
    
    // 获取鼠标灵敏度
    float getMouseSensitivity() const { return mouseSensitivity; }
    
    // 设置重力
    void setGravity(float g) { gravity = g; }
    float getGravity() const { return gravity; }
    
    // 设置默认地面高度（无地形时使用）
    void setDefaultGroundHeight(float h) { defaultGroundHeight = h; }
    float getDefaultGroundHeight() const { return defaultGroundHeight; }
    
    // 设置跳跃力
    void setJumpForce(float f) { jumpForce = f; }
    float getJumpForce() const { return jumpForce; }
    
    // 着地状态
    bool isGrounded() const { return isGrounded_; }
    void setGrounded(bool grounded) { isGrounded_ = grounded; }
    
    // 地形查询接口
    void setTerrainQuery(CameraTerrainQuery query) { terrainQuery_ = std::move(query); }
    void clearTerrainQuery() { terrainQuery_ = nullptr; }
    
    // 碰撞箱管理（用于地形高度查询）
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size) { collisionBoxes_.emplace_back(position, size); }
    void clearCollisionBoxes() { collisionBoxes_.clear(); }
    
    // 设置跟踪目标（第三人称模式）
    void setTarget(const glm::vec3& target) { targetPosition = target; }
    glm::vec3 getTarget() const { return targetPosition; }
    
    // 设置第三人称相机距离
    void setThirdPersonDistance(float d) { thirdPersonDistance = d; }
    float getThirdPersonDistance() const { return thirdPersonDistance; }
    
    // 获取视锥体（用于剔除）
    const Frustum& getFrustum() const { return frustum; }
    
    /**
     * @brief 更新视锥体（每帧调用）
     */
    void updateFrustum();

private:
    // 更新摄像机向量
    void updateCameraVectors();
    
    // 更新第三人称相机位置
    void updateThirdPersonCamera();
    
    // 查询地形高度
    float queryTerrainHeight(float x, float z) const;
    
    // 摄像机属性
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    
    // 欧拉角
    float yaw;
    float pitch;
    
    // 摄像机选项
    float movementSpeed;
    float mouseSensitivity;
    float zoom;
    
    // 窗口尺寸
    int windowWidth;
    int windowHeight;
    
    // 物理相关
    glm::vec3 velocity;
    bool isJumping;
    bool isGrounded_;               // 着地状态
    float jumpForce;
    float gravity;
    float defaultGroundHeight;      // 默认地面高度（无地形时）
    
    // 地形查询
    CameraTerrainQuery terrainQuery_;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;  // position, size
    
    // 自由视角模式
    bool freeCameraMode;
    float freeCameraSpeed;
    
    // 第三人称模式
    Mode mode;
    glm::vec3 targetPosition;       // 跟踪目标位置
    float thirdPersonDistance;      // 相机与目标的距离
    float thirdPersonHeight;        // 相机高度偏移
    
    // 视锥体（用于剔除）
    Frustum frustum;
};

} // namespace vgame

#endif // CAMERA_H