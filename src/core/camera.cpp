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
      isGrounded_(true),
      jumpForce(5.5f),
      gravity(15.0f),
      defaultGroundHeight(-1.5f),
      terrainQuery_(nullptr),
      freeCameraMode(false),
      freeCameraSpeed(15.0f),
      mode(Mode::ThirdPerson),  // 默认第三人称
      targetPosition(0.0f, 0.0f, -5.0f),
      thirdPersonDistance(8.0f),
      thirdPersonHeight(2.0f) {
    updateCameraVectors();
    updateThirdPersonCamera();
}

float Camera::queryTerrainHeight(float x, float z) const {
    float maxHeight = defaultGroundHeight;
    
    // 检查所有碰撞箱，找到该位置最高的站立面
    for (const auto& box : collisionBoxes_) {
        const glm::vec3& boxPos = box.first;
        const glm::vec3& boxSize = box.second;
        
        // 检查 (x, z) 是否在碰撞箱的水平范围内
        float halfSizeX = boxSize.x * 0.5f;
        float halfSizeZ = boxSize.z * 0.5f;
        
        if (x >= boxPos.x - halfSizeX && x <= boxPos.x + halfSizeX &&
            z >= boxPos.z - halfSizeZ && z <= boxPos.z + halfSizeZ) {
            // 碰撞箱顶面高度
            float boxTop = boxPos.y + boxSize.y * 0.5f;
            if (boxTop > maxHeight) {
                maxHeight = boxTop;
            }
        }
    }
    
    // 如果有自定义地形查询，取两者最大值
    if (terrainQuery_) {
        float terrainH = terrainQuery_(x, z);
        return std::max(maxHeight, terrainH);
    }
    
    return maxHeight;
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
        // 自由视角模式：相机和 Player 同时移动
        float speed = freeCameraSpeed;
        
        glm::vec3 moveDelta(0.0f);
        if (moveForward) moveDelta += front * speed * deltaTime;
        if (moveBackward) moveDelta -= front * speed * deltaTime;
        if (moveLeft) moveDelta -= right * speed * deltaTime;
        if (moveRight) moveDelta += right * speed * deltaTime;
        if (spaceHeld) moveDelta += worldUp * speed * deltaTime;
        if (shiftPressed) moveDelta -= worldUp * speed * deltaTime;
        
        // 同时移动相机和 Player
        position += moveDelta;
        targetPosition += moveDelta;
    } else if (mode == Mode::ThirdPerson) {
        // 第三人称模式：移动目标，相机跟随
        float speed = movementSpeed;
        
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
        
        // 飞行模式：空格上升，Shift 下降
        if (spaceHeld) {
            targetPosition.y += speed * deltaTime;
        }
        if (shiftPressed) {
            targetPosition.y -= speed * deltaTime;
            // 防止低于地形
            float terrainHeight = queryTerrainHeight(targetPosition.x, targetPosition.z);
            if (targetPosition.y < terrainHeight) {
                targetPosition.y = terrainHeight;
            }
        }
        
        // 更新相机位置
        updateThirdPersonCamera();
    } else {
        // 第一人称模式
        // 查询当前位置的地形高度
        float terrainHeight = queryTerrainHeight(position.x, position.z);
        
        // 跳跃输入处理：仅在着地时允许跳跃
        if (jump && isGrounded_ && !isJumping) {
            velocity.y = jumpForce;
            isJumping = true;
            isGrounded_ = false;
        }

        // 空中物理：非着地状态应用重力
        if (!isGrounded_) {
            velocity.y -= gravity * deltaTime;
            position.y += velocity.y * deltaTime;

            // 着地检测
            if (position.y <= terrainHeight) {
                position.y = terrainHeight;
                velocity.y = 0.0f;
                isJumping = false;
                isGrounded_ = true;
            }
        } else {
            // 着地状态检查
            if (position.y < terrainHeight) {
                // 位置低于地形，修正到地形高度
                position.y = terrainHeight;
            } else if (position.y > terrainHeight + 0.01f) {
                // 位置高于地形（悬浮），触发掉落
                isGrounded_ = false;
            }
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

    yaw -= xOffset;  // 左移向右转，右移向左转
    pitch += yOffset;  // 上移向上看，下移向下看
    
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
