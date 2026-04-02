// ECS 系统实现
#include "ecs/systems.h"
#include "ecs/components.h"
#include <iostream>
#include <cmath>

namespace vgame {
namespace ecs {

// ==================== World 实现 ====================

World::World() {
    // 默认创建空实体
}

entt::entity World::createEntity() {
    return registry_.create();
}

void World::destroyEntity(entt::entity entity) {
    registry_.destroy(entity);
}

void World::initSystems(GLFWwindow* window, int viewportWidth, int viewportHeight) {
    window_ = window;
    viewportWidth_ = viewportWidth;
    viewportHeight_ = viewportHeight;
    
    std::cout << "[World] 系统初始化完成，视口: " << viewportWidth << "x" << viewportHeight << std::endl;
}

void World::updateSystems(float deltaTime) {
    // 系统更新顺序：输入 -> 移动 -> 物理 -> 相机
    // 这里由外部调用各系统的 update
}

entt::entity World::createPlayer(int viewportWidth, int viewportHeight) {
    auto entity = registry_.create();
    
    // 添加变换组件
    auto& transform = registry_.emplace<TransformComponent>(entity);
    transform.position = glm::vec3(0.0f, 1.5f, 5.0f);
    transform.yaw = -90.0f;  // 初始朝向 -Z 轴
    transform.pitch = 0.0f;
    
    // 添加速度组件
    registry_.emplace<VelocityComponent>(entity);
    
    // 添加相机组件
    auto& camera = registry_.emplace<CameraComponent>(entity);
    camera.viewportWidth = viewportWidth;
    camera.viewportHeight = viewportHeight;
    
    // 添加相机控制器
    registry_.emplace<CameraControllerComponent>(entity);
    
    // 添加物理组件
    registry_.emplace<PhysicsComponent>(entity);
    
    // 添加输入状态
    registry_.emplace<InputStateComponent>(entity);
    
    // 添加玩家标签
    registry_.emplace<PlayerTag>(entity);
    
    // 添加名称
    registry_.emplace<NameComponent>(entity, "Player");
    
    player_ = entity;
    mainCamera_ = entity;
    
    std::cout << "[World] 玩家实体创建成功" << std::endl;
    
    return entity;
}

entt::entity World::createCamera(bool isMainCamera) {
    auto entity = registry_.create();
    
    auto& transform = registry_.emplace<TransformComponent>(entity);
    transform.yaw = -90.0f;
    
    auto& camera = registry_.emplace<CameraComponent>(entity);
    camera.viewportWidth = viewportWidth_;
    camera.viewportHeight = viewportHeight_;
    
    auto& controller = registry_.emplace<CameraControllerComponent>(entity);
    controller.isMainCamera = isMainCamera;
    
    if (isMainCamera) {
        mainCamera_ = entity;
    }
    
    return entity;
}

// ==================== InputSystem 实现 ====================

InputSystem::InputSystem(World& world, GLFWwindow* window)
    : world_(world), window_(window) {
    // 设置 GLFW 回调
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    
    std::cout << "[InputSystem] 初始化完成" << std::endl;
}

InputSystem::~InputSystem() {
    if (window_) {
        glfwSetWindowUserPointer(window_, nullptr);
    }
}

void InputSystem::update(float deltaTime) {
    // 更新玩家实体的输入状态
    auto view = world_.registry().view<PlayerTag, InputStateComponent>();
    
    for (auto entity : view) {
        auto& input = view.get<InputStateComponent>(entity);
        
        // 更新键盘输入
        input.moveForward = keys_[GLFW_KEY_W] || keys_[GLFW_KEY_UP];
        input.moveBackward = keys_[GLFW_KEY_S] || keys_[GLFW_KEY_DOWN];
        input.moveLeft = keys_[GLFW_KEY_A] || keys_[GLFW_KEY_LEFT];
        input.moveRight = keys_[GLFW_KEY_D] || keys_[GLFW_KEY_RIGHT];
        input.jump = isKeyJustPressed(GLFW_KEY_SPACE);
        input.sprint = keys_[GLFW_KEY_LEFT_SHIFT];
        input.freeCameraToggle = isKeyJustPressed(GLFW_KEY_R);
        input.spaceHeld = keys_[GLFW_KEY_SPACE];
        input.shiftHeld = keys_[GLFW_KEY_LEFT_SHIFT];
        
        // 更新鼠标输入
        input.mouseDeltaX = static_cast<float>(mouseDeltaX_);
        input.mouseDeltaY = static_cast<float>(mouseDeltaY_);
    }
    
    // 重置鼠标增量
    mouseDeltaX_ = 0.0;
    mouseDeltaY_ = 0.0;
    
    // 更新上一帧的键盘状态
    std::copy(std::begin(keys_), std::end(keys_), std::begin(previousKeys_));
}

bool InputSystem::isKeyPressed(int key) const {
    if (key >= 0 && key <= GLFW_KEY_LAST) {
        return keys_[key];
    }
    return false;
}

bool InputSystem::isKeyJustPressed(int key) const {
    if (key >= 0 && key <= GLFW_KEY_LAST) {
        return keys_[key] && !previousKeys_[key];
    }
    return false;
}

bool InputSystem::isMouseButtonPressed(int button) const {
    if (button >= 0 && button < 8) {
        return mouseButtons_[button];
    }
    return false;
}

void InputSystem::getMouseDelta(double& deltaX, double& deltaY) const {
    deltaX = mouseDeltaX_;
    deltaY = mouseDeltaY_;
}

void InputSystem::setCursorCaptured(bool captured) {
    cursorCaptured_ = captured;
    glfwSetInputMode(window_, GLFW_CURSOR, 
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void InputSystem::resetFrameState() {
    // 重置刚按下的状态
}

void InputSystem::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* inputSystem = static_cast<InputSystem*>(glfwGetWindowUserPointer(window));
    if (inputSystem && key >= 0 && key <= GLFW_KEY_LAST) {
        if (action == GLFW_PRESS) {
            inputSystem->keys_[key] = true;
        } else if (action == GLFW_RELEASE) {
            inputSystem->keys_[key] = false;
        }
    }
}

void InputSystem::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto* inputSystem = static_cast<InputSystem*>(glfwGetWindowUserPointer(window));
    if (inputSystem && button >= 0 && button < 8) {
        if (action == GLFW_PRESS) {
            inputSystem->mouseButtons_[button] = true;
        } else if (action == GLFW_RELEASE) {
            inputSystem->mouseButtons_[button] = false;
        }
    }
}

void InputSystem::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* inputSystem = static_cast<InputSystem*>(glfwGetWindowUserPointer(window));
    if (inputSystem) {
        inputSystem->mouseDeltaX_ = xpos - inputSystem->mouseX_;
        inputSystem->mouseDeltaY_ = ypos - inputSystem->mouseY_;
        inputSystem->mouseX_ = xpos;
        inputSystem->mouseY_ = ypos;
    }
}

// ==================== MovementSystem 实现 ====================

MovementSystem::MovementSystem(World& world) : world_(world) {
    std::cout << "[MovementSystem] 初始化完成" << std::endl;
}

void MovementSystem::addCollisionBox(const glm::vec3& position, const glm::vec3& size) {
    collisionBoxes_.push_back({position, size});
}

void MovementSystem::clearCollisionBoxes() {
    collisionBoxes_.clear();
}

bool MovementSystem::checkCollision(const glm::vec3& position, const glm::vec3& playerSize) const {
    for (const auto& box : collisionBoxes_) {
        const glm::vec3& boxPos = box.first;
        const glm::vec3& boxSize = box.second;
        
        // AABB 碰撞检测
        if (position.x + playerSize.x / 2.0f > boxPos.x - boxSize.x / 2.0f &&
            position.x - playerSize.x / 2.0f < boxPos.x + boxSize.x / 2.0f &&
            position.y + playerSize.y > boxPos.y - boxSize.y / 2.0f &&
            position.y - playerSize.y / 2.0f < boxPos.y + boxSize.y / 2.0f &&
            position.z + playerSize.z / 2.0f > boxPos.z - boxSize.z / 2.0f &&
            position.z - playerSize.z / 2.0f < boxPos.z + boxSize.z / 2.0f) {
            return true;
        }
    }
    return false;
}

glm::vec3 MovementSystem::resolveCollision(const glm::vec3& oldPos, const glm::vec3& newPos, const glm::vec3& playerSize) const {
    // 尝试分轴移动，找到可以移动到的位置
    glm::vec3 result = oldPos;
    
    // X 轴
    glm::vec3 testPosX = glm::vec3(newPos.x, oldPos.y, oldPos.z);
    if (!checkCollision(testPosX, playerSize)) {
        result.x = newPos.x;
    }
    
    // Z 轴
    glm::vec3 testPosZ = glm::vec3(result.x, oldPos.y, newPos.z);
    if (!checkCollision(testPosZ, playerSize)) {
        result.z = newPos.z;
    }
    
    return result;
}

void MovementSystem::update(float deltaTime) {
    // 获取玩家视图
    auto view = world_.registry().view<TransformComponent, CameraControllerComponent, InputStateComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& controller = view.get<CameraControllerComponent>(entity);
        auto& input = view.get<InputStateComponent>(entity);
        
        // 获取可选的速度组件
        VelocityComponent* velocity = nullptr;
        if (world_.registry().all_of<VelocityComponent>(entity)) {
            velocity = &world_.registry().get<VelocityComponent>(entity);
        }
        
        // 切换自由视角模式
        if (input.freeCameraToggle) {
            controller.freeCameraMode = !controller.freeCameraMode;
            std::cout << "[MovementSystem] 自由视角模式: " 
                      << (controller.freeCameraMode ? "开启" : "关闭") << std::endl;
        }
        
        if (controller.freeCameraMode) {
            updateFreeCamera(entity, transform, velocity, controller, input, deltaTime);
        } else {
            if (velocity) {
                updateNormalMovement(entity, transform, *velocity, controller, input, deltaTime);
            }
        }
        
        // 处理鼠标旋转（两种模式都适用）
        if (input.mouseDeltaX != 0.0f || input.mouseDeltaY != 0.0f) {
            transform.yaw -= input.mouseDeltaX * controller.mouseSensitivity;  // 左移向右转，右移向左转
            transform.pitch += input.mouseDeltaY * controller.mouseSensitivity;  // 上移向上看，下移向下看
            
            // 限制俯仰角
            transform.pitch = glm::clamp(transform.pitch, -89.0f, 89.0f);
        }
    }
}

void MovementSystem::updateFreeCamera(entt::entity entity, TransformComponent& transform,
                                     VelocityComponent* velocity,
                                     CameraControllerComponent& controller,
                                     const InputStateComponent& input,
                                     float deltaTime) {
    float speed = controller.freeCameraSpeed;
    
    // 自由视角模式下也检测碰撞
    glm::vec3 oldPos = transform.position;
    glm::vec3 front = transform.getFront();
    glm::vec3 right = transform.getRight();
    glm::vec3 playerSize(0.6f, 1.8f, 0.6f);  // 玩家碰撞体大小
    
    if (input.moveForward) transform.position += front * speed * deltaTime;
    if (input.moveBackward) transform.position -= front * speed * deltaTime;
    if (input.moveLeft) transform.position -= right * speed * deltaTime;
    if (input.moveRight) transform.position += right * speed * deltaTime;
    if (input.spaceHeld) transform.position += glm::vec3(0.0f, 1.0f, 0.0f) * speed * deltaTime;
    if (input.shiftHeld) transform.position -= glm::vec3(0.0f, 1.0f, 0.0f) * speed * deltaTime;
    
    // 碰撞检测
    if (checkCollision(transform.position, playerSize)) {
        transform.position = resolveCollision(oldPos, transform.position, playerSize);
    }
}

void MovementSystem::updateNormalMovement(entt::entity entity, TransformComponent& transform,
                                         VelocityComponent& velocity,
                                         CameraControllerComponent& controller,
                                         const InputStateComponent& input,
                                         float deltaTime) {
    float speed = controller.movementSpeed;
    if (input.sprint) speed *= controller.sprintMultiplier;
    
    // 计算水平移动
    glm::vec3 horizontalVelocity(0.0f);
    // 使用相机的方向（第三人称模式）
    glm::vec3 front = controller.cameraFront;
    glm::vec3 right = controller.cameraRight;
    
    // 水平移动（忽略 Y 分量）
    front.y = 0.0f;
    front = glm::normalize(front);
    right.y = 0.0f;
    right = glm::normalize(right);
    
    if (input.moveForward) horizontalVelocity += front;
    if (input.moveBackward) horizontalVelocity -= front;
    if (input.moveLeft) horizontalVelocity -= right;
    if (input.moveRight) horizontalVelocity += right;
    
    if (glm::length(horizontalVelocity) > 0.0f) {
        horizontalVelocity = glm::normalize(horizontalVelocity) * speed;
    }
    
    // 保存旧位置
    glm::vec3 oldPos = transform.position;
    glm::vec3 playerSize(0.6f, 1.8f, 0.6f);  // 玩家碰撞体大小
    
    // 更新位置
    transform.position.x += horizontalVelocity.x * deltaTime;
    transform.position.z += horizontalVelocity.z * deltaTime;
    
    // 碰撞检测
    if (checkCollision(transform.position, playerSize)) {
        transform.position = resolveCollision(oldPos, transform.position, playerSize);
    }
}

// ==================== PhysicsSystem 实现 ====================

PhysicsSystem::PhysicsSystem(World& world) : world_(world) {
    std::cout << "[PhysicsSystem] 初始化完成" << std::endl;
}

void PhysicsSystem::update(float deltaTime) {
    auto view = world_.registry().view<TransformComponent, VelocityComponent, PhysicsComponent, InputStateComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& velocity = view.get<VelocityComponent>(entity);
        auto& physics = view.get<PhysicsComponent>(entity);
        auto& input = view.get<InputStateComponent>(entity);
        
        if (!physics.useGravity) continue;
        
        // 处理跳跃
        if (input.jump && !physics.isJumping && physics.isGrounded) {
            velocity.linear.y = physics.jumpForce;
            physics.isJumping = true;
            physics.isGrounded = false;
            std::cout << "[PhysicsSystem] 跳跃触发" << std::endl;
        }
        
        // 应用重力
        if (physics.isJumping || transform.position.y > physics.groundHeight) {
            velocity.linear.y -= physics.gravity * deltaTime;
            transform.position.y += velocity.linear.y * deltaTime;
            
            // 检查落地
            if (transform.position.y <= physics.groundHeight) {
                transform.position.y = physics.groundHeight;
                velocity.linear.y = 0.0f;
                physics.isJumping = false;
                physics.isGrounded = true;
            }
        } else {
            transform.position.y = physics.groundHeight;
            velocity.linear.y = 0.0f;
            physics.isGrounded = true;
        }
    }
}

void PhysicsSystem::addCollisionBox(const glm::vec3& position, const glm::vec3& size) {
    collisionBoxes_.emplace_back(position, size);
}

void PhysicsSystem::clearCollisionBoxes() {
    collisionBoxes_.clear();
}

bool PhysicsSystem::checkGroundCollision(const glm::vec3& position, float height) const {
    for (const auto& box : collisionBoxes_) {
        glm::vec3 playerPos = position;
        playerPos.y -= height * 0.5f;
        glm::vec3 playerSize(0.3f, height, 0.3f);
        
        if (checkAABBCollision(playerPos, playerSize, box.first, box.second)) {
            return true;
        }
    }
    return false;
}

bool PhysicsSystem::checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                                       const glm::vec3& pos2, const glm::vec3& size2) const {
    return (abs(pos1.x - pos2.x) < (size1.x + size2.x) * 0.5f) &&
           (abs(pos1.y - pos2.y) < (size1.y + size2.y) * 0.5f) &&
           (abs(pos1.z - pos2.z) < (size1.z + size2.z) * 0.5f);
}

// ==================== CameraSystem 实现 ====================

CameraSystem::CameraSystem(World& world) : world_(world) {
    std::cout << "[CameraSystem] 初始化完成" << std::endl;
}

void CameraSystem::update(float deltaTime) {
    auto view = world_.registry().view<TransformComponent, CameraComponent>();
    
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& camera = view.get<CameraComponent>(entity);
        
        // 更新相机矩阵缓存
        camera.viewMatrix = camera.getViewMatrix(transform);
        camera.projectionMatrix = camera.getProjectionMatrix();
        camera.dirty = false;
    }
}

glm::mat4 CameraSystem::getMainViewMatrix() const {
    if (world_.registry().valid(world_.getMainCamera())) {
        auto& camera = world_.registry().get<CameraComponent>(world_.getMainCamera());
        auto& transform = world_.registry().get<TransformComponent>(world_.getMainCamera());
        return camera.getViewMatrix(transform);
    }
    return glm::mat4(1.0f);
}

glm::mat4 CameraSystem::getMainProjectionMatrix() const {
    if (world_.registry().valid(world_.getMainCamera())) {
        auto& camera = world_.registry().get<CameraComponent>(world_.getMainCamera());
        return camera.getProjectionMatrix();
    }
    return glm::mat4(1.0f);
}

} // namespace ecs
} // namespace vgame
