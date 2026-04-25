// 客户端 ECS 系统实现
#include "ecs/client_systems.h"
#include "ecs/components.h"
#include <iostream>
#include <cstring>

namespace owengine {
namespace ecs {

// ==================== CameraComponent 实现 ====================

glm::mat4 CameraComponent::getViewMatrix(const TransformComponent& transform) const {
    glm::vec3 front = transform.getFront();
    return glm::lookAt(transform.position, transform.position + front, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 CameraComponent::getProjectionMatrix() const {
    float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
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
        input.freeCameraToggle = false;  // R 键功能已移除
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

// ==================== ClientWorld 实现 ====================

ClientWorld::ClientWorld() : World() {
    networkSystem_ = std::make_unique<client::NetworkSystem>();
    std::cout << "[ClientWorld] 初始化完成" << std::endl;
}

ClientWorld::~ClientWorld() {
    if (networkSystem_ && networkSystem_->isConnected()) {
        networkSystem_->disconnect();
    }
}

void ClientWorld::initClientSystems(GLFWwindow* window, int viewportWidth, int viewportHeight) {
    viewportWidth_ = viewportWidth;
    viewportHeight_ = viewportHeight;
    
    inputSystem_ = std::make_unique<InputSystem>(*this, window);
    cameraSystem_ = std::make_unique<CameraSystem>(*this);
    cameraControllerSystem_ = std::make_unique<CameraControllerSystem>(*this);
    movementSystem_ = std::make_unique<MovementSystem>(*this);
    physicsSystem_ = std::make_unique<PhysicsSystem>(*this);
    
    std::cout << "[ClientWorld] 客户端系统初始化完成，视口: " 
              << viewportWidth << "x" << viewportHeight << std::endl;
}

void ClientWorld::updateClientSystems(float deltaTime) {
    // 更新网络系统（处理接收消息）
    if (networkSystem_) {
        networkSystem_->update(deltaTime);
    }
    
    // 更新输入
    inputSystem_->update(deltaTime);
    
    // 更新移动（单机模式或客户端预测）
    movementSystem_->update(deltaTime);
    
    // 更新物理
    physicsSystem_->update(deltaTime);
    
    // 更新相机控制器（处理相机移动和方向）
    if (cameraControllerSystem_) {
        cameraControllerSystem_->update(deltaTime);
    }
    
    // 更新相机（更新矩阵）
    cameraSystem_->update(deltaTime);
    
    // 连接到服务器后发送输入（在所有系统更新之后）
    if (networkSystem_ && networkSystem_->isConnected()) {
        auto player = getPlayer();
        if (registry().valid(player)) {
            auto* input = registry().try_get<InputStateComponent>(player);
            auto* movement = registry().try_get<MovementControllerComponent>(player);
            
            if (input && movement) {
                networkSystem_->sendInput(
                    input->moveForward, input->moveBackward,
                    input->moveLeft, input->moveRight,
                    input->jump, input->sprint,
                    input->spaceHeld, input->shiftHeld,
                    input->mouseDeltaX, input->mouseDeltaY,
                    movement->moveFront, movement->moveRight
                );
            }
        }
    }
}

bool ClientWorld::connectToServer(const std::string& host, uint16_t port) {
    if (!networkSystem_) return false;
    
    // 设置回调
    networkSystem_->setOnConnected([this](const std::string& clientId) {
        std::cout << "[ClientWorld] 已连接到服务器，ID: " << clientId << std::endl;
    });
    
    networkSystem_->setOnDisconnected([this]() {
        std::cout << "[ClientWorld] 已断开服务器连接" << std::endl;
    });
    
    networkSystem_->setOnError([](const std::string& error) {
        std::cerr << "[ClientWorld] 网络错误: " << error << std::endl;
    });
    
    networkSystem_->setOnPlayerJoin([this](const client::RemotePlayer& player) {
        std::cout << "[ClientWorld] 远程玩家加入: " << player.clientId << std::endl;
        // TODO: 创建远程玩家实体用于渲染
    });
    
    networkSystem_->setOnPlayerLeave([this](const std::string& clientId) {
        std::cout << "[ClientWorld] 远程玩家离开: " << clientId << std::endl;
        // TODO: 移除远程玩家实体
    });
    
    return networkSystem_->connect(host, port);
}

void ClientWorld::disconnectFromServer() {
    if (networkSystem_ && networkSystem_->isConnected()) {
        networkSystem_->disconnect();
    }
}

bool ClientWorld::isConnectedToServer() const {
    return networkSystem_ && networkSystem_->isConnected();
}

bool ClientWorld::startServerDiscovery() {
    if (!discoveryScanner_) {
        discoveryScanner_ = std::make_unique<ServerDiscoveryScanner>();
    }
    return discoveryScanner_->start();
}

void ClientWorld::stopServerDiscovery() {
    if (discoveryScanner_) {
        discoveryScanner_->stop();
    }
}

std::vector<DiscoveredServer> ClientWorld::getDiscoveredServers() {
    if (discoveryScanner_) {
        discoveryScanner_->pruneStaleServers();
        return discoveryScanner_->getDiscoveredServers();
    }
    return {};
}

entt::entity ClientWorld::createClientPlayer(int viewportWidth, int viewportHeight) {
    // 创建基础玩家（使用共享方法）
    auto playerEntity = createPlayer();
    
    // 创建独立的相机实体
    auto cameraEntity = createEntity();
    
    // 添加相机变换组件
    auto& cameraTransform = registry().emplace<TransformComponent>(cameraEntity);
    // 初始相机位置与玩家相同（第一人称）
    auto& playerTransform = registry().get<TransformComponent>(playerEntity);
    cameraTransform.position = playerTransform.position;
    cameraTransform.yaw = playerTransform.yaw;
    cameraTransform.pitch = playerTransform.pitch;
    cameraTransform.updateRotationFromEuler();
    
    // 添加相机组件
    auto& camera = registry().emplace<CameraComponent>(cameraEntity);
    camera.viewportWidth = viewportWidth;
    camera.viewportHeight = viewportHeight;
    
    // 添加相机控制器组件
    registry().emplace<CameraControllerComponent>(cameraEntity);
    
    // 设置为主相机
    setMainCamera(cameraEntity);
    
    std::cout << "[ClientWorld] 客户端玩家实体创建成功（玩家实体: " << static_cast<uint32_t>(playerEntity) 
              << ", 相机实体: " << static_cast<uint32_t>(cameraEntity) << ")" << std::endl;
    
    return playerEntity;
}

void ClientWorld::adjustPlayerToTerrain() {
    auto player = getPlayer();
    if (!registry().valid(player)) return;
    
    auto* physics = registry().try_get<PhysicsComponent>(player);
    auto* transform = registry().try_get<TransformComponent>(player);
    if (!physics || !transform) return;
    
    // 只有物理系统有地形查询时才调整
    if (!physicsSystem_ || !physicsSystem_->hasTerrainQuery()) return;
    
    float terrainHeight = physicsSystem_->getTerrainHeight(transform->position.x, transform->position.z);
    float targetY = terrainHeight + physics->colliderHeight * 0.5f;
    transform->position.y = targetY;
    physics->groundHeight = terrainHeight;
    physics->groundNormal = glm::vec3(0.0f, 1.0f, 0.0f); // 默认上向量，物理系统会重新计算
    physics->isGrounded = true;
    physics->isJumping = false;
    
    std::cout << "[ClientWorld] 调整玩家位置到地形高度: " << terrainHeight << std::endl;
}

} // namespace ecs
} // namespace owengine
