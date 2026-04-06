// 渲染器实现
// 负责管理整个渲染流程，包括窗口创建、Vulkan初始化和渲染循环
#include "imgui.h"
#include "renderer.h"
#include "skybox_renderer.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <limits>
#include <thread>

namespace vgame {

// Renderer构造函数
Renderer::Renderer(int width, int height, const std::string& title)
    : windowWidth(width), windowHeight(height), windowTitle(title) {
    // 启用4x MSAA抗锯齿
    msaaSamples = VK_SAMPLE_COUNT_4_BIT;
}

// Renderer析构函数
Renderer::~Renderer() {
    cleanup();
}

// 运行渲染器
// 初始化窗口和Vulkan，然后进入主循环
void Renderer::run() {
    initWindow();
    initVulkan();
    mainLoop();
}

// 初始化窗口
// 使用GLFW创建窗口并设置回调
void Renderer::initWindow() {
    glfwInit();
    
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);  // 暂时禁用窗口大小调整
    
    window = glfwCreateWindow(windowWidth, windowHeight, windowTitle.c_str(), nullptr, nullptr);
    
    if (!window) {
        throw std::runtime_error("failed to create GLFW window!");
    }
    
    glfwSetWindowUserPointer(window, this);
    // 暂时不设置帧缓冲区大小回调
    // glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Renderer::initVulkan() {
    vulkanInstance = std::make_shared<VulkanInstance>();
    vulkanInstance->initialize(window);
    
    vulkanDevice = std::make_shared<VulkanDevice>(
        vulkanInstance->getPhysicalDevice(),
        vulkanInstance->getDevice(),
        vulkanInstance->getSurface()
    );
    
    // 获取设备支持的最大 MSAA 采样数
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(vulkanInstance->getPhysicalDevice(), &physicalDeviceProperties);
    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
                                physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_64_BIT;
    else if (counts & VK_SAMPLE_COUNT_32_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_32_BIT;
    else if (counts & VK_SAMPLE_COUNT_16_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_16_BIT;
    else if (counts & VK_SAMPLE_COUNT_8_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_8_BIT;
    else if (counts & VK_SAMPLE_COUNT_4_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_4_BIT;
    else if (counts & VK_SAMPLE_COUNT_2_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_2_BIT;
    else maxMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // 确保初始 MSAA 不超过设备支持的最大值
    if (msaaSamples > maxMsaaSamples) {
        msaaSamples = maxMsaaSamples;
    }
    
    std::cout << "[Renderer] 设备支持的最大 MSAA: " << maxMsaaSamples << std::endl;
    
    swapchain = std::make_shared<VulkanSwapchain>(vulkanDevice, window);
    swapchain->create();
    
    renderPass = std::make_shared<VulkanRenderPass>(vulkanDevice, swapchain->getImageFormat(), msaaSamples);
    renderPass->create();
    
    // 创建多重采样颜色资源（如果使用MSAA）
    if (msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
        createColorResources();
    }
    
    // 创建描述符集布局（必须在 graphicsPipeline 之前）
    createDescriptorSetLayouts();
    
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {textureDescriptorSetLayout, lightDescriptorSetLayout};
    graphicsPipeline = std::make_shared<VulkanPipeline>(
        vulkanDevice,
        renderPass->getRenderPass(),
        swapchain->getExtent(),
        "shaders/shader.vert.spv",
        "shaders/shader.frag.spv",
        vgame::VertexFormat::POSITION_COLOR,
        descriptorSetLayouts,
        msaaSamples
    );
    graphicsPipeline->create();
    
    // 创建深度资源（如果使用MSAA，也需要多重采样深度资源）
    vulkanDevice->createDepthResources(swapchain->getExtent(), msaaSamples);
    
    framebuffers = std::make_shared<VulkanFramebuffer>(vulkanDevice, renderPass->getRenderPass());
    framebuffers->create(swapchain->getImageViews(), swapchain->getExtent(), colorImageView);
    
    commandBuffers = std::make_shared<VulkanCommandBuffer>(
        vulkanDevice,
        renderPass->getRenderPass(),
        graphicsPipeline->getPipeline(),
        graphicsPipeline->getPipelineLayout()
    );
    commandBuffers->create(swapchain->getImageViews().size());
    
    syncObjects = std::make_shared<VulkanSync>(vulkanDevice);
    syncObjects->create(MAX_FRAMES_IN_FLIGHT);
    
    // 不预先记录命令缓冲，每次drawFrame时动态记录
    
    // 初始化游戏逻辑组件
    camera = std::make_unique<Camera>(windowWidth, windowHeight);
    input = std::make_unique<Input>(window);
    physics = std::make_unique<Physics>();
    
    // 初始化 ECS 系统（使用 ClientWorld）
    if (useECS) {
        ecsClientWorld = std::make_unique<ecs::ClientWorld>();
        ecsClientWorld->initClientSystems(window, windowWidth, windowHeight);
        
        // 创建玩家实体
        ecsClientWorld->createClientPlayer(windowWidth, windowHeight);
        
        std::cout << "[Renderer] ECS 系统初始化完成" << std::endl;
    }
    
    floorRenderer = std::make_unique<FloorRenderer>(vulkanDevice);
    floorRenderer->create();
    cubeRenderer = std::make_unique<CubeRenderer>(vulkanDevice);
    cubeRenderer->create();
    
    // 初始化纹理加载器
    textureLoader = std::make_shared<TextureLoader>(vulkanDevice);
    
    // 初始化光源管理器
    lightManager = std::make_unique<LightManager>();
    
    // 初始化天空盒渲染器（在创建天空盒管线之前）
    skyboxRenderer = std::make_unique<SkyboxRenderer>(vulkanDevice);
    skyboxRenderer->create();
    // 加载天空盒纹理（十字形布局）
    try {
        skyboxRenderer->loadCubemapFromCrossLayout("assets/textures/skybox.jpg");
    } catch (const std::runtime_error& e) {
        std::cout << "[Renderer] 天空盒纹理加载失败: " << e.what() << std::endl;
        std::cout << "[Renderer] 将使用默认黑色背景" << std::endl;
    }
    
    // 初始化模型渲染器
    modelRenderer = std::make_unique<ModelRenderer>(vulkanDevice, textureLoader);
    modelRenderer->create();
    // 加载 OBJ 模型
    try {
        modelRenderer->loadModel("assets/models/洛茜.obj");
        modelRenderer->setPosition(glm::vec3(0.0f, 0.0f, -10.0f));
        modelRenderer->setScale(glm::vec3(1.0f, 1.0f, 1.0f));
        modelRenderer->setRotation(0.0f, 180.0f, 0.0f);  // 旋转180度，使正面朝向相机
        std::cout << "[Renderer] OBJ 模型加载成功" << std::endl;
    } catch (const std::runtime_error& e) {
        std::cout << "[Renderer] OBJ 模型加载失败: " << e.what() << std::endl;
    }
    
    // 创建天空盒管线（需要天空盒渲染器的描述符集布局）
    skyboxPipeline = std::make_shared<VulkanPipeline>(
        vulkanDevice,
        renderPass->getRenderPass(),
        swapchain->getExtent(),
        "shaders/skybox.vert.spv",
        "shaders/skybox.frag.spv",
        VertexFormat::POSITION_ONLY,
        std::vector<VkDescriptorSetLayout>{skyboxRenderer->getDescriptorSetLayout()},
        msaaSamples
    );
    skyboxPipeline->create();
    
    // 创建描述符集布局
    createDescriptorSetLayouts();
    
    // 创建描述符池
    createDescriptorPool();
    
    // 从 JSON 配置文件加载场景（光源和模型）
    SceneConfig sceneConfig = loadSceneConfig("assets/models/models.json");
    
    // 加载光源
    if (!sceneConfig.lights.empty()) {
        loadLightsFromConfig(sceneConfig);
    } else {
        // 如果配置文件中没有光源，使用默认配置
        Logger::warning("使用默认光源配置");
        lightManager->addDirectionalLight("sun", glm::vec3(0.5f, -1.0f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f), 1.0f);
        lightManager->addPointLight("point1", glm::vec3(2.0f, 3.0f, 2.0f), glm::vec3(1.0f, 1.0f, 1.0f), 2.0f, 10.0f);
        lightManager->addPointLight("point2", glm::vec3(-2.0f, 3.0f, -2.0f), glm::vec3(1.0f, 1.0f, 1.0f), 2.0f, 10.0f);
        lightManager->setAmbientColor(glm::vec3(0.5f, 0.5f, 0.5f));
        lightManager->setAmbientIntensity(0.5f);
    }
    
    // 加载模型
    if (!sceneConfig.models.empty()) {
        loadModelsFromConfig(sceneConfig.models);
    } else {
        // 如果配置文件中没有模型，使用默认配置
        Logger::warning("使用默认模型配置");
        std::vector<ModelConfig> defaultConfigs = {
            {
                "player_idle", "assets/models/player.glb", true,
                glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f), glm::vec3(0.3f),
                0, false, 0, false, true, false, "玩家空闲模型"
            },
            {
                "player_walk", "assets/models/player_walk.glb", true,
                glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f), glm::vec3(0.3f),
                0, true, 0, true, false, true, "玩家行走模型"
            }
        };
        loadModelsFromConfig(defaultConfigs);
    }
    
    // 创建描述符集（创建默认描述符集）
    createDescriptorSets();
    
    // 初始化 ImGui
    imguiManager = std::make_unique<ImGuiManager>(vulkanDevice, swapchain, renderPass, window, vulkanInstance->getInstance(), msaaSamples);
    imguiManager->init();
    
    // 设置立方体位置（在相机正前方）
    glm::vec3 cubePos(0.0f, 0.0f, -1.0f);
    cubeRenderer->setPosition(cubePos);
    
    // 添加立方体碰撞体（大小为 5.0）
    physics->addCollisionBox(cubePos, glm::vec3(5.0f, 5.0f, 5.0f));
    camera->addCollisionBox(cubePos, glm::vec3(5.0f, 5.0f, 5.0f));
    
    // 同时添加到 ECS 系统
    if (useECS && ecsClientWorld) {
        if (ecsClientWorld->getMovementSystem()) {
            ecsClientWorld->getMovementSystem()->addCollisionBox(cubePos, glm::vec3(5.0f, 5.0f, 5.0f));
        }
        if (ecsClientWorld->getPhysicsSystem()) {
            ecsClientWorld->getPhysicsSystem()->addCollisionBox(cubePos, glm::vec3(5.0f, 5.0f, 5.0f));
        }
    }
    
    // 初始化时间
    lastTime = std::chrono::high_resolution_clock::now();
}

void Renderer::mainLoop() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    // 第一帧后捕获鼠标，确保窗口已经显示
    bool firstFrame = true;
    
    // FPS 计数
    int frameCount = 0;
    float fpsTimer = 0.0f;
    
    while (!glfwWindowShouldClose(window)) {
        // 记录帧开始时间
        auto frameStartTime = std::chrono::high_resolution_clock::now();
        
        // 计算delta time
        auto currentTime = frameStartTime;
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // 限制delta time以防卡顿
        if (deltaTime > 0.1f) {
            deltaTime = 0.1f;
        }
        
        // 检查是否有延迟的 MSAA 更改
        if (pendingMsaaChange) {
            pendingMsaaChange = false;
            setMsaaSamples(pendingMsaaSamples);
        }
        
                // FPS 计算
        
                frameCount++;
        
                fpsTimer += deltaTime;
        
                if (fpsTimer >= 1.0f) {  // 每秒更新一次
        
                    currentFPS = frameCount / fpsTimer;
        
                    std::cout << "[Renderer] FPS: " << static_cast<int>(currentFPS) << std::endl;
        
                    
        
                    frameCount = 0;
        
                    fpsTimer = 0.0f;
        
                    // 重置帧时间统计
        
                    minFrameTime = 999.0f;
        
                    maxFrameTime = 0.0f;
        
                }
        
                
        
                                glfwPollEvents();
        
                
        
                                
        
                
        
                                // 第一帧后捕获鼠标
        
                
        
                                if (firstFrame) {
        
                
        
                                    if (useECS && ecsClientWorld->getInputSystem()) {
        
                
        
                                        ecsClientWorld->getInputSystem()->setCursorCaptured(true);
        
                
        
                                    } else {
        
                
        
                                        input->setCursorCaptured(true);
        
                
        
                                    }
        
                
        
                                    firstFrame = false;
        
                
        
                                    glfwPollEvents();
        
                
        
                                    std::cout << "[Renderer] 鼠标已捕获" << std::endl;
        
                
        
                                }
        
                
        
                                
        
                
        
                                // 检测 ESC 键切换开发者模式
        
                
        
                                bool escPressed = (useECS && ecsClientWorld->getInputSystem()) 
        
                
        
                                    ? ecsClientWorld->getInputSystem()->isKeyJustPressed(GLFW_KEY_ESCAPE)
        
                
        
                                    : input->isKeyJustPressed(GLFW_KEY_ESCAPE);
        
                
        
                                    
        
                
        
                                if (escPressed) {
        
                
        
                                    developerMode = !developerMode;
        
                
        
                                    if (developerMode) {
        
                
        
                                        if (useECS && ecsClientWorld->getInputSystem()) {
        
                
        
                                            ecsClientWorld->getInputSystem()->setCursorCaptured(false);
        
                
        
                                        } else {
        
                
        
                                            input->setCursorCaptured(false);
        
                
        
                                        }
        
                
        
                                        std::cout << "[Renderer] 开发者模式已开启，鼠标已释放" << std::endl;
        
                
        
                                    } else {
        
                
        
                                        if (useECS && ecsClientWorld->getInputSystem()) {
        
                
        
                                            ecsClientWorld->getInputSystem()->setCursorCaptured(true);
        
                
        
                                        } else {
        
                
        
                                            input->setCursorCaptured(true);
        
                
        
                                        }
        
                
        
                                        std::cout << "[Renderer] 开发者模式已关闭，鼠标已捕获" << std::endl;
        
                
        
                                    }
        
                
        
                                }
        
                
        
                                
        
                
        
                                // 检测按键状态（在update之前，避免状态被重置）
        
                
        
                                if (!developerMode) {
        
                
        
                                    if (useECS && ecsClientWorld->getInputSystem()) {
        
                
        
                                        // ECS 模式：输入由 ECS 系统处理，这里不需要额外操作
        
                
        
                                    } else {
        
                
        
                                        jumpInput = input->isKeyJustPressed(GLFW_KEY_SPACE);
        
                
        
                                        freeCameraToggle = input->isKeyJustPressed(GLFW_KEY_R);
        
                
        
                                        shiftInput = input->isSprintPressed();
        
                
        
                                        spaceHeld = input->isKeyPressed(GLFW_KEY_SPACE);
        
                
        
                                        
        
                
        
                                        // 更新输入
        
                
        
                                        input->update();
        
                
        
                                        
        
                
        
                                        // 处理鼠标移动
        
                
        
                                        double mouseX, mouseY;
        
                
        
                                        input->getRawMouseMovement(mouseX, mouseY);
        
                
        
                                        
        
                
        
                                        if (mouseX != 0.0 || mouseY != 0.0) {
        
                
        
                                            camera->processMouseMovement(static_cast<float>(mouseX), static_cast<float>(mouseY));
        
                
        
                                        }
        
                
        
                                    }
        
                
        
                                } else {
        
                
        
                                    // 开发者模式下重置输入状态
        
                
        
                                    jumpInput = false;
        
                
        
                                    freeCameraToggle = false;
        
                
        
                                    shiftInput = false;
        
                
        
                                    spaceHeld = false;
        
                
        
                                    if (useECS && ecsClientWorld->getInputSystem()) {
        
                
        
                                        // ECS 模式下不需要额外操作
        
                
        
                                    } else {
        
                
        
                                        input->update();
        
                
        
                                    }
        
                
        
                                }
        
                
        
                // 更新 ImGui
        
                imguiManager->newFrame();
        
                
        
                // 开发者模式下显示开发者面板，否则显示简单的FPS
        
                if (developerMode) {
        
                    renderDeveloperPanel();
        
                } else {
        
                    // 简单 FPS 显示
        
                    ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        
                    ImGui::Text("FPS: %.1f", currentFPS);
        
                    ImGui::Text("Press ESC for Dev Mode");
        
                    ImGui::SetWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        
                    ImGui::End();
        
                }
        // 更新游戏逻辑（应用时间缩放）
        if (!pauseGame) {
            updateGameLogic(deltaTime * timeScale);
        }
        
        drawFrame();
        
        // 在帧结束时重置"刚刚按下"标志
        input->resetJustPressedFlags();
        
        // 帧率限制：计算这一帧用了多少时间，如果少于目标时间则睡眠剩余时间
        auto frameEndTime = std::chrono::high_resolution_clock::now();
        frameTime = std::chrono::duration<float>(frameEndTime - frameStartTime).count();
        
        // 更新帧时间统计
        if (frameTime < minFrameTime) minFrameTime = frameTime;
        if (frameTime > maxFrameTime) maxFrameTime = frameTime;
        
        float targetFrameTime = 1.0f / targetFPS;
        if (frameTime < targetFrameTime) {
            float sleepTime = targetFrameTime - frameTime;
            // 转换为微秒
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(sleepTime * 1000000)));
        }
    }
    
    vkDeviceWaitIdle(vulkanDevice->getDevice());
}

void Renderer::updateGameLogic(float deltaTime) {
    // 更新视锥体（用于剔除）
    camera->updateFrustum();
    
    // 处理网络连接请求
    if (connectRequested && ecsClientWorld) {
        std::cout << "[Renderer] 正在连接到 " << serverHost << ":" << serverPort << std::endl;
        if (ecsClientWorld->connectToServer(serverHost, static_cast<uint16_t>(serverPort))) {
            std::cout << "[Renderer] 连接成功" << std::endl;
        } else {
            std::cerr << "[Renderer] 连接失败" << std::endl;
        }
        connectRequested = false;
    }
    
    // 处理断开连接请求
    if (disconnectRequested && ecsClientWorld) {
        ecsClientWorld->disconnectFromServer();
        disconnectRequested = false;
        std::cout << "[Renderer] 已断开连接" << std::endl;
    }
    
    if (useECS && ecsClientWorld) {
        // 获取玩家实体并更新设置
        auto player = ecsClientWorld->getPlayer();
        if (ecsClientWorld->registry().valid(player)) {
            auto& controller = ecsClientWorld->registry().get<ecs::MovementControllerComponent>(player);
            
            // 更新速度设置
            controller.movementSpeed = userMovementSpeed;
            controller.mouseSensitivity = userSensitivity;
            
            // 先同步相机方向（在更新系统之前，确保发送正确的方向）
            controller.moveFront = camera->getFront();
            controller.moveRight = camera->getRight();
        }
        
        // 使用 ClientWorld 统一更新所有系统
        ecsClientWorld->updateClientSystems(deltaTime);
        
        // 获取输入状态
        auto* input = ecsClientWorld->registry().try_get<ecs::InputStateComponent>(player);
        
        // 处理自由视角切换
        if (input && input->freeCameraToggle) {
            camera->toggleFreeCamera();
        }
        
        // 自由视角模式下，使用相机自己的移动逻辑
        if (camera->isFreeCameraMode() && input) {
            // 处理鼠标移动（自由视角需要单独处理）
            if (input->mouseDeltaX != 0.0f || input->mouseDeltaY != 0.0f) {
                camera->processMouseMovement(input->mouseDeltaX, input->mouseDeltaY);
            }
            
            camera->update(deltaTime,
                          input->moveForward, input->moveBackward,
                          input->moveLeft, input->moveRight,
                          input->jump, false,  // freeCameraToggle 已处理
                          input->shiftHeld, input->spaceHeld);
        } else {
            // 非自由视角模式下，从 ECS 同步到旧相机系统（用于渲染）
            if (ecsClientWorld->registry().valid(ecsClientWorld->getMainCamera())) {
                auto& transform = ecsClientWorld->registry().get<ecs::TransformComponent>(ecsClientWorld->getMainCamera());
                auto& controller = ecsClientWorld->registry().get<ecs::MovementControllerComponent>(ecsClientWorld->getMainCamera());
                
                // 第三人称模式：设置目标位置，第一人称：设置相机位置
                if (camera->getMode() == Camera::Mode::ThirdPerson) {
                    camera->setTarget(transform.position);
                } else {
                    camera->setPosition(transform.position);
                }
                camera->setYawPitch(transform.yaw, transform.pitch);
                camera->setMovementSpeed(controller.movementSpeed);
                camera->setMouseSensitivity(controller.mouseSensitivity);
            }
        }
    } else {
        // 使用旧的系统更新
        float speed = userMovementSpeed;
        if (!developerMode && input->isSprintPressed()) {
            speed *= 2.0f;
        }
        camera->setMovementSpeed(speed);
        
        camera->update(deltaTime,
                      input->isForwardPressed(),
                      input->isBackPressed(),
                      input->isLeftPressed(),
                      input->isRightPressed(),
                      jumpInput,
                      freeCameraToggle,
                      shiftInput,
                      spaceHeld);
        
        physics->update(deltaTime);
    }
    
    // 第三人称模式：将 player 模型位置同步到相机目标
    if (camera->getMode() == Camera::Mode::ThirdPerson) {
        // 检测玩家是否在移动，并计算移动方向
        bool isMoving = false;
        float moveYaw = camera->getYaw();  // 默认面向相机方向
        
        if (useECS && ecsClientWorld) {
            auto player = ecsClientWorld->getPlayer();
            if (ecsClientWorld->registry().valid(player)) {
                auto* input = ecsClientWorld->registry().try_get<ecs::InputStateComponent>(player);
                if (input) {
                    isMoving = input->moveForward || input->moveBackward || 
                              input->moveLeft || input->moveRight;
                    
                    // 计算移动方向
                    if (isMoving) {
                        float dirX = 0.0f, dirZ = 0.0f;
                        
                        // 相机的前方向（忽略Y轴）
                        glm::vec3 camFront = camera->getFront();
                        camFront.y = 0.0f;
                        camFront = glm::normalize(camFront);
                        
                        // 相机的右方向
                        glm::vec3 camRight = camera->getRight();
                        camRight.y = 0.0f;
                        camRight = glm::normalize(camRight);
                        
                        // 根据输入计算移动向量
                        if (input->moveForward) {
                            dirX += camFront.x;
                            dirZ += camFront.z;
                        }
                        if (input->moveBackward) {
                            dirX -= camFront.x;
                            dirZ -= camFront.z;
                        }
                        if (input->moveLeft) {
                            dirX -= camRight.x;
                            dirZ -= camRight.z;
                        }
                        if (input->moveRight) {
                            dirX += camRight.x;
                            dirZ += camRight.z;
                        }
                        
                        // 计算移动方向的yaw角度
                        if (dirX != 0.0f || dirZ != 0.0f) {
                            moveYaw = glm::degrees(atan2(-dirX, -dirZ));
                        }
                    }
                }
            }
        }
        
        // 选择要渲染的模型（空闲或行走）
        GLTFModel* activeModel = nullptr;
        if (isMoving && gltfWalkModel && gltfWalkModel->getMeshCount() > 0) {
            activeModel = gltfWalkModel.get();
            // 如果刚切换到移动状态，播放所有行走动画
            if (!playerWasMoving) {
                if (gltfWalkModel->getAnimationCount() > 0) {
                    gltfWalkModel->playAllAnimations(true, 1.0f);
                }
            }
        } else if (gltfModel && gltfModel->getMeshCount() > 0) {
            activeModel = gltfModel.get();
        }
        
        // 更新活动模型的位置和动画
        if (activeModel) {
            activeModel->setPosition(camera->getTarget());
            // 面向移动方向（静止时面向相机方向）
            activeModel->setRotation(0.0f, moveYaw, 0.0f);
            activeModel->updateAnimation(deltaTime);
        }
        
        playerWasMoving = isMoving;
    } else {
        // 非第三人称模式也更新动画（用于模型预览等）
        if (gltfModel) {
            gltfModel->updateAnimation(deltaTime);
        }
        if (gltfWalkModel) {
            gltfWalkModel->updateAnimation(deltaTime);
        }
        // 更新动态加载的模型动画
        for (auto& [id, model] : models) {
            if (model) {
                model->updateAnimation(deltaTime);
            }
        }
    }
    
    // 更新远程玩家模型
    if (ecsClientWorld && ecsClientWorld->isConnectedToServer()) {
        auto& remotePlayers = ecsClientWorld->getNetworkSystem()->getRemotePlayers();
        
        // 移除已离开的玩家模型
        for (auto it = remotePlayerModels.begin(); it != remotePlayerModels.end(); ) {
            if (remotePlayers.find(it->first) == remotePlayers.end()) {
                it = remotePlayerModels.erase(it);
            } else {
                ++it;
            }
        }
        
        // 更新或创建远程玩家模型
        for (const auto& [clientId, player] : remotePlayers) {
            if (!player.active) continue;
            
            auto it = remotePlayerModels.find(clientId);
            if (it == remotePlayerModels.end()) {
                // 创建新模型
                RemotePlayerModels models;
                
                // 空闲模型
                auto idleModel = std::make_unique<GLTFModel>(vulkanDevice, textureLoader);
                if (idleModel->loadFromFile("assets/models/player.glb")) {
                    idleModel->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
                    idleModel->setPosition(player.position);
                    models.idleModel = std::move(idleModel);
                }
                
                // 行走模型
                auto walkModel = std::make_unique<GLTFModel>(vulkanDevice, textureLoader);
                if (walkModel->loadFromFile("assets/models/player_walk.glb")) {
                    walkModel->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
                    walkModel->setPosition(player.position);
                    if (walkModel->getAnimationCount() > 0) {
                        walkModel->playAllAnimations(true, 1.0f);
                    }
                    models.walkModel = std::move(walkModel);
                }
                
                remotePlayerModels[clientId] = std::move(models);
            } else {
                // 更新两个模型的位置和旋转
                if (it->second.idleModel) {
                    it->second.idleModel->setPosition(player.position);
                    // 空闲时面向相机方向
                    it->second.idleModel->setRotation(0.0f, player.yaw, 0.0f);
                }
                if (it->second.walkModel) {
                    it->second.walkModel->setPosition(player.position);
                    // 移动时面向移动方向，静止时面向相机方向
                    float renderYaw = player.isMoving ? player.moveYaw : player.yaw;
                    it->second.walkModel->setRotation(0.0f, renderYaw, 0.0f);
                    
                    // 只有在移动时才播放行走动画，跳跃时保持之前的状态
                    if (player.isMoving) {
                        if (!it->second.walkModel->isAnimationPlaying()) {
                            it->second.walkModel->playAllAnimations(true, 1.0f);
                        }
                        it->second.walkModel->updateAnimation(deltaTime);
                    } else {
                        if (it->second.walkModel->isAnimationPlaying()) {
                            it->second.walkModel->stopAnimation();
                        }
                    }
                }
                
                it->second.wasMoving = player.isMoving;
            }
        }
    } else {
        // 未连接时清除所有远程玩家模型
        remotePlayerModels.clear();
    }
}

void Renderer::drawFrame() {
    vkWaitForFences(vulkanDevice->getDevice(), 1, &syncObjects->getInFlightFences()[currentFrame], VK_TRUE, UINT64_MAX);
    
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        vulkanDevice->getDevice(),
        swapchain->getSwapchain(),
        UINT64_MAX,
        syncObjects->getImageAvailableSemaphores()[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex
    );
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        // 如果窗口正在关闭，不抛出异常，直接返回
        if (glfwWindowShouldClose(window)) {
            return;
        }
        throw std::runtime_error("failed to acquire swap chain image!");
    }
    
    vkResetFences(vulkanDevice->getDevice(), 1, &syncObjects->getInFlightFences()[currentFrame]);
    
    vkResetCommandBuffer(commandBuffers->getCommandBuffers()[currentFrame], /*VkCommandBufferResetFlagBits*/ 0);
    
    // 手动记录命令缓冲
    VkCommandBuffer commandBuffer = commandBuffers->getCommandBuffers()[currentFrame];
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass->getRenderPass();
    renderPassInfo.framebuffer = framebuffers->getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain->getExtent();
    
    // 清除颜色和深度缓冲
    // 根据是否使用MSAA，清除值的数量不同
    std::vector<VkClearValue> clearValues;
    clearValues.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}}});  // 颜色清除值：黑色
    clearValues.push_back({{1.0f, 0}});  // 深度清除值：1.0（最远）
    
    // 如果使用MSAA，需要为解析附件添加清除值（虽然不会被使用，但数量必须匹配）
    if (msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
        clearValues.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}}});  // 解析附件清除值
    }
    
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->getPipeline());
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) swapchain->getExtent().width;
    viewport.height = (float) swapchain->getExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // 先渲染天空盒（背景）
    if (skyboxRenderer && skyboxRenderer->getDescriptorSet() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline->getPipeline());
        skyboxRenderer->render(commandBuffer, skyboxRenderer->getPipelineLayout(),
                             camera->getViewMatrix(), camera->getProjectionMatrix());
        // 重新绑定主图形管线
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->getPipeline());
    }
    
    // 更新光源 uniform buffer
    updateLightUniformBuffer();
    
    // 绑定默认描述符集（用于地板、立方体等）
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           graphicsPipeline->getPipelineLayout(), 0, 1, &textureDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           graphicsPipeline->getPipelineLayout(), 1, 1, &lightDescriptorSet, 0, nullptr);
    
    // 渲染地板
    floorRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                         camera->getViewMatrix(), camera->getProjectionMatrix());
    
    // 渲染立方体
    cubeRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                        camera->getViewMatrix(), camera->getProjectionMatrix());
    
    // 渲染 OBJ 模型
    if (modelRenderer) {
        modelRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                            camera->getViewMatrix(), camera->getProjectionMatrix());
    }
    
    // 渲染玩家模型（第三人称模式下选择空闲或行走模型）
    if (camera->getMode() == Camera::Mode::ThirdPerson) {
        // 根据移动状态选择模型
        GLTFModel* activeModel = nullptr;
        VkDescriptorSet activeDescriptorSet = VK_NULL_HANDLE;
        if (playerWasMoving && gltfWalkModel && gltfWalkModel->getMeshCount() > 0) {
            activeModel = gltfWalkModel.get();
            activeDescriptorSet = gltfWalkModelDescriptorSet;
        } else if (gltfModel && gltfModel->getMeshCount() > 0) {
            activeModel = gltfModel.get();
            activeDescriptorSet = gltfModelDescriptorSet;
        }
        
        if (activeModel) {
            // 绑定模型的纹理描述符集
            if (activeDescriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       graphicsPipeline->getPipelineLayout(), 0, 1, &activeDescriptorSet, 0, nullptr);
            }
            activeModel->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                              camera->getViewMatrix(), camera->getProjectionMatrix(),
                              activeModel->getModelMatrix());
        }
    } else if (gltfModel && gltfModel->getMeshCount() > 0) {
        // 非第三人称模式，渲染空闲模型
        if (gltfModelDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   graphicsPipeline->getPipelineLayout(), 0, 1, &gltfModelDescriptorSet, 0, nullptr);
        }
        gltfModel->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                        camera->getViewMatrix(), camera->getProjectionMatrix(),
                        gltfModel->getModelMatrix());
    }
    
    // 渲染动态加载的模型（带视锥体剔除）
    for (auto& [id, model] : models) {
        if (model && model->getMeshCount() > 0) {
            // 视锥体剔除检测
            auto bbox = model->getBoundingBox();
            glm::vec3 worldMin = model->getPosition() + bbox.first * model->getScale();
            glm::vec3 worldMax = model->getPosition() + bbox.second * model->getScale();
            
            // 距离剔除（超过100米不渲染）
            glm::vec3 modelCenter = (worldMin + worldMax) * 0.5f;
            float distance = glm::length(modelCenter - camera->getPosition());
            if (distance > 100.0f) {
                continue;
            }
            
            // 视锥体剔除
            if (!camera->getFrustum().isAABBInside(worldMin, worldMax)) {
                continue;
            }
            
            // 绑定模型的纹理描述符集
            auto it = modelDescriptorSets.find(id);
            if (it != modelDescriptorSets.end() && it->second != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       graphicsPipeline->getPipelineLayout(), 0, 1, &it->second, 0, nullptr);
            }
            model->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                        camera->getViewMatrix(), camera->getProjectionMatrix(),
                        model->getModelMatrix());
        }
    }
    
    // 渲染远程玩家模型
    for (auto& [clientId, models] : remotePlayerModels) {
        // 根据移动状态选择模型
        GLTFModel* activeModel = models.wasMoving ? models.walkModel.get() : models.idleModel.get();
        if (activeModel && activeModel->getMeshCount() > 0) {
            activeModel->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                        camera->getViewMatrix(), camera->getProjectionMatrix(),
                        activeModel->getModelMatrix());
        }
    }
    
    // 渲染 ImGui
    imguiManager->render(commandBuffer);
    
    vkCmdEndRenderPass(commandBuffer);
    
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {syncObjects->getImageAvailableSemaphores()[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers->getCommandBuffers()[currentFrame];
    
    VkSemaphore signalSemaphores[] = {syncObjects->getRenderFinishedSemaphores()[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    VkResult submitResult = vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, syncObjects->getInFlightFences()[currentFrame]);
    if (submitResult != VK_SUCCESS) {
        // 打印详细的错误码
        std::cerr << "[Renderer] vkQueueSubmit failed with error code: " << submitResult << std::endl;
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            std::cerr << "[Renderer] VK_ERROR_DEVICE_LOST - GPU device lost!" << std::endl;
        } else if (submitResult == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            std::cerr << "[Renderer] VK_ERROR_OUT_OF_DEVICE_MEMORY - Out of device memory!" << std::endl;
        } else if (submitResult == VK_ERROR_OUT_OF_HOST_MEMORY) {
            std::cerr << "[Renderer] VK_ERROR_OUT_OF_HOST_MEMORY - Out of host memory!" << std::endl;
        }
        
        // 如果窗口正在关闭，不抛出异常，直接返回
        if (glfwWindowShouldClose(window)) {
            // 提交一个空的信号操作来恢复 fence 状态
            VkSubmitInfo emptySubmit{};
            emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &emptySubmit, syncObjects->getInFlightFences()[currentFrame]);
            return;
        }
        throw std::runtime_error("failed to submit draw command buffer!");
    }
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    
    VkSwapchainKHR swapChains[] = {swapchain->getSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    
    result = vkQueuePresentKHR(vulkanDevice->getPresentQueue(), &presentInfo);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        // 如果窗口正在关闭，不抛出异常，直接返回
        if (glfwWindowShouldClose(window)) {
            return;
        }
        throw std::runtime_error("failed to present swap chain image!");
    }
    
    currentFrame = (currentFrame + 1) % syncObjects->getMaxFramesInFlight();
}

void Renderer::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }
    
    vkDeviceWaitIdle(vulkanDevice->getDevice());
    
    // 清理MSAA颜色资源
    if (msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
        cleanupColorResources();
    }
    
    // 清理并重新创建深度资源
    vulkanDevice->cleanupDepthResources();
    
    swapchain->recreate(window);
    
    // 重新创建深度资源（使用MSAA样本数）
    vulkanDevice->createDepthResources(swapchain->getExtent(), msaaSamples);
    
    // 更新渲染通道的MSAA样本数并重新创建
    renderPass->setMsaaSamples(msaaSamples);
    renderPass->cleanup();
    renderPass->create();
    
    // 重新创建MSAA颜色资源
    if (msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
        createColorResources();
    }
    
    // 更新管线的MSAA样本数并重新创建
    graphicsPipeline->setMsaaSamples(msaaSamples);
    graphicsPipeline->cleanup();
    graphicsPipeline->create();
    
    skyboxPipeline->setMsaaSamples(msaaSamples);
    skyboxPipeline->cleanup();
    skyboxPipeline->create();
    
    framebuffers->recreate(swapchain->getImageViews(), swapchain->getExtent(), colorImageView);
    
    commandBuffers->cleanup();
    commandBuffers->updateRenderPass(renderPass->getRenderPass());
    commandBuffers->updatePipeline(graphicsPipeline->getPipeline());
    commandBuffers->updatePipelineLayout(graphicsPipeline->getPipelineLayout());
    commandBuffers->create(swapchain->getImageViews().size());
    
    // 重新初始化 ImGui 以匹配新的 MSAA 设置
    imguiManager.reset();
    imguiManager = std::make_unique<ImGuiManager>(vulkanDevice, swapchain, renderPass, window, vulkanInstance->getInstance(), msaaSamples);
    imguiManager->init();
    
    for (size_t i = 0; i < commandBuffers->getCommandBuffers().size(); i++) {
        commandBuffers->record(i, framebuffers->getFramebuffers()[i], swapchain->getExtent(),
                             camera->getViewMatrix(), camera->getProjectionMatrix());
    }
}

void Renderer::cleanup() {
    // 等待设备空闲，确保所有渲染操作完成
    if (vulkanDevice) {
        vkDeviceWaitIdle(vulkanDevice->getDevice());
    }
    
    // 首先清理所有依赖 Vulkan 设备的资源（必须在 vulkanDevice 销毁之前）
    // 清理远程玩家模型
    remotePlayerModels.clear();
    
    // 清理动态加载的模型
    models.clear();
    modelDescriptorSets.clear();
    
    // 清理 GLTF 模型（包含 Vulkan 缓冲区）
    gltfWalkModel.reset();
    gltfModel.reset();
    
    // 清理 ImGui（使用 Vulkan）
    imguiManager.reset();
    
    // 清理 ECS 客户端世界
    ecsClientWorld.reset();
    
    cubeRenderer.reset();
    modelRenderer.reset();
    skyboxRenderer.reset();
    floorRenderer.reset();
    physics.reset();
    input.reset();
    camera.reset();
    
    // 清理新系统
    lightManager.reset();
    textureLoader.reset();
    
    // 清理描述符集资源
    if (lightUniformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->getDevice(), lightUniformBuffer, nullptr);
    }
    if (lightUniformBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->getDevice(), lightUniformBufferMemory, nullptr);
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice->getDevice(), descriptorPool, nullptr);
    }
    if (lightDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice->getDevice(), lightDescriptorSetLayout, nullptr);
    }
    if (textureDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice->getDevice(), textureDescriptorSetLayout, nullptr);
    }
    
    syncObjects.reset();
    commandBuffers.reset();
    
    // 清理MSAA颜色资源
    cleanupColorResources();
    
    framebuffers.reset();
    skyboxPipeline.reset();
    graphicsPipeline.reset();
    renderPass.reset();
    swapchain.reset();
    vulkanDevice.reset();
    vulkanInstance.reset();
    
    if (window != nullptr) {
        glfwDestroyWindow(window);
    }
    
    glfwTerminate();
}

void Renderer::createColorResources() {
    VkFormat colorFormat = swapchain->getImageFormat();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchain->getExtent().width;
    imageInfo.extent.height = swapchain->getExtent().height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = colorFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples = msaaSamples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(vulkanDevice->getDevice(), &imageInfo, nullptr, &colorImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create color image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vulkanDevice->getDevice(), colorImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, 
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &colorImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate color image memory!");
    }
    
    vkBindImageMemory(vulkanDevice->getDevice(), colorImage, colorImageMemory, 0);
    
    VkImageViewCreateInfo imageViewInfo{};
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.image = colorImage;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.format = colorFormat;
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewInfo.subresourceRange.baseMipLevel = 0;
    imageViewInfo.subresourceRange.levelCount = 1;
    imageViewInfo.subresourceRange.baseArrayLayer = 0;
    imageViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(vulkanDevice->getDevice(), &imageViewInfo, nullptr, &colorImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create color image view!");
    }
    
    // 注意：MSAA颜色附件不需要单独的布局转换，渲染通道会自动处理
}

void Renderer::cleanupColorResources() {
    if (colorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkanDevice->getDevice(), colorImageView, nullptr);
        colorImageView = VK_NULL_HANDLE;
    }
    if (colorImage != VK_NULL_HANDLE) {
        vkDestroyImage(vulkanDevice->getDevice(), colorImage, nullptr);
        colorImage = VK_NULL_HANDLE;
    }
    if (colorImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->getDevice(), colorImageMemory, nullptr);
        colorImageMemory = VK_NULL_HANDLE;
    }
}

void Renderer::setMsaaSamples(VkSampleCountFlagBits samples) {
    msaaSamples = samples;
    // 需要重新创建渲染通道和其他资源
    recreateSwapchain();
}

void Renderer::createDescriptorSetLayouts() {
    // 1. 创建纹理描述符集布局 (set = 0, binding = 0)
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 1> textureBindings = {samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo textureLayoutInfo{};
    textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    textureLayoutInfo.bindingCount = static_cast<uint32_t>(textureBindings.size());
    textureLayoutInfo.pBindings = textureBindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice->getDevice(), &textureLayoutInfo, nullptr, &textureDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture descriptor set layout!");
    }

    // 2. 创建光源描述符集布局 (set = 1, binding = 0)
    // 光源 uniform buffer
    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 0;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lightBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 1> lightBindings = {lightBinding};

    VkDescriptorSetLayoutCreateInfo lightLayoutInfo{};
    lightLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightLayoutInfo.bindingCount = static_cast<uint32_t>(lightBindings.size());
    lightLayoutInfo.pBindings = lightBindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice->getDevice(), &lightLayoutInfo, nullptr, &lightDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create light descriptor set layout!");
    }
}

void Renderer::createDescriptorPool() {
    // 描述符池大小
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // 纹理描述符（多个模型需要多个纹理）
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 10;  // 增加以支持多个模型
    
    // 光源 uniform buffer
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 20;  // 增加以支持多个模型

    if (vkCreateDescriptorPool(vulkanDevice->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void Renderer::createDescriptorSets() {
    // 为玩家模型创建纹理描述符集（如果还没有）
    auto createIfNotExists = [this](GLTFModel* model, VkDescriptorSet& descriptorSet, const std::string& modelName) {
        if (descriptorSet != VK_NULL_HANDLE) return;  // 已经创建过
        
        if (!model || model->getMeshCount() == 0) return;
        
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &textureDescriptorSetLayout;

        if (vkAllocateDescriptorSets(vulkanDevice->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
            Logger::warning("无法为 " + modelName + " 分配纹理描述符集");
            return;
        }

        std::shared_ptr<Texture> texture = model->getFirstTexture();
        if (texture) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = texture->getImageView();
            imageInfo.sampler = texture->getSampler();

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = descriptorSet;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(vulkanDevice->getDevice(), 1, &descriptorWrite, 0, nullptr);
            Logger::info("为 " + modelName + " 创建纹理描述符集");
        }
    };

    // 为玩家模型创建描述符集
    createIfNotExists(gltfModel.get(), gltfModelDescriptorSet, "gltfModel");
    createIfNotExists(gltfWalkModel.get(), gltfWalkModelDescriptorSet, "gltfWalkModel");
    
    // 保留默认纹理描述符集（用于地板等）
    if (textureDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo textureAllocInfo{};
        textureAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        textureAllocInfo.descriptorPool = descriptorPool;
        textureAllocInfo.descriptorSetCount = 1;
        textureAllocInfo.pSetLayouts = &textureDescriptorSetLayout;

        if (vkAllocateDescriptorSets(vulkanDevice->getDevice(), &textureAllocInfo, &textureDescriptorSet) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate default texture descriptor set!");
        }
        Logger::info("Default texture descriptor set allocated");
    }

    // 2. 创建光源 uniform buffer（std140 布局）
    // 计算正确的 buffer 大小
    size_t lightsSize = sizeof(ShaderLight) * 16;  // 16 个光源 = 1536 bytes
    size_t lightCountSize = sizeof(int);  // 4 bytes
    size_t padding1Size = 12;  // 12 bytes padding (16字节对齐)
    size_t ambientSize = sizeof(glm::vec3);  // 12 bytes
    size_t padding2Size = 4;  // 4 bytes padding (16字节对齐)
    VkDeviceSize bufferSize = lightsSize + lightCountSize + padding1Size + ambientSize + padding2Size;  // 1568 bytes
    
    Logger::info("Creating light uniform buffer: size=" + std::to_string(bufferSize));
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice->getDevice(), &bufferInfo, nullptr, &lightUniformBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create light uniform buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice->getDevice(), lightUniformBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, 
                                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &lightUniformBufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice->getDevice(), lightUniformBuffer, nullptr);
        throw std::runtime_error("failed to allocate light uniform buffer memory!");
    }

    vkBindBufferMemory(vulkanDevice->getDevice(), lightUniformBuffer, lightUniformBufferMemory, 0);

    // 3. 创建光源描述符集
    VkDescriptorSetAllocateInfo lightAllocInfo{};
    lightAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lightAllocInfo.descriptorPool = descriptorPool;
    lightAllocInfo.descriptorSetCount = 1;
    lightAllocInfo.pSetLayouts = &lightDescriptorSetLayout;

    if (vkAllocateDescriptorSets(vulkanDevice->getDevice(), &lightAllocInfo, &lightDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate light descriptor set!");
    }

    // 更新光源描述符集
    VkDescriptorBufferInfo bufferInfo2{};
    bufferInfo2.buffer = lightUniformBuffer;
    bufferInfo2.offset = 0;
    bufferInfo2.range = bufferSize;

    VkWriteDescriptorSet lightDescriptorWrite{};
    lightDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lightDescriptorWrite.dstSet = lightDescriptorSet;
    lightDescriptorWrite.dstBinding = 0;
    lightDescriptorWrite.dstArrayElement = 0;
    lightDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightDescriptorWrite.descriptorCount = 1;
    lightDescriptorWrite.pBufferInfo = &bufferInfo2;

    vkUpdateDescriptorSets(vulkanDevice->getDevice(), 1, &lightDescriptorWrite, 0, nullptr);
    
    // 初始化光源数据
    updateLightUniformBuffer();
}

void Renderer::updateLightUniformBuffer() {
    // 获取光源数据
    ShaderLightArray lights = lightManager->getShaderLightData();
    int lightCount = lightManager->getEnabledLightCount();
    glm::vec3 ambientColor = lightManager->getAmbient();

    // 计算正确的 buffer 大小（std140 布局）
    // ShaderLight 现在是 96 字节 (16 * 6)
    size_t lightsSize = sizeof(ShaderLight) * 16;  // 16 个光源 = 1536 bytes
    size_t lightCountSize = sizeof(int);  // 4 bytes
    size_t padding1Size = 12;  // 12 bytes padding (16字节对齐)
    size_t ambientSize = sizeof(glm::vec3);  // 12 bytes
    size_t padding2Size = 4;  // 4 bytes padding (16字节对齐)
    size_t totalSize = lightsSize + lightCountSize + padding1Size + ambientSize + padding2Size;  // 1568 bytes

    // 映射 uniform buffer
    void* data;
    vkMapMemory(vulkanDevice->getDevice(), lightUniformBufferMemory, 0, totalSize, 0, &data);

    // 复制光源数据
    memcpy(data, lights.data(), lightsSize);
    memcpy(static_cast<char*>(data) + lightsSize, &lightCount, lightCountSize);

    // 添加 12 字节 padding
    memset(static_cast<char*>(data) + lightsSize + lightCountSize, 0, padding1Size);

    // 复制环境光颜色
    memcpy(static_cast<char*>(data) + lightsSize + lightCountSize + padding1Size, &ambientColor, ambientSize);

    // 添加 4 字节 padding
    memset(static_cast<char*>(data) + lightsSize + lightCountSize + padding1Size + ambientSize, 0, padding2Size);

    vkUnmapMemory(vulkanDevice->getDevice(), lightUniformBufferMemory);
}

void Renderer::renderDeveloperPanel() {
    // 设置 ImGui 窗口样式
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.15f, 0.95f));
    
    // 主菜单栏
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("开发者工具")) {
            ImGui::MenuItem("性能统计", nullptr, true);
            ImGui::MenuItem("相机控制", nullptr, true);
            ImGui::MenuItem("光源管理", nullptr, true);
            ImGui::MenuItem("模型控制", nullptr, true);
            ImGui::MenuItem("物理设置", nullptr, true);
            ImGui::MenuItem("渲染设置", nullptr, true);
            ImGui::Separator();
            if (ImGui::MenuItem("关闭开发者模式 (ESC)")) {
                developerMode = false;
                input->setCursorCaptured(true);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    
    // ==================== 性能统计窗口 ====================
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_FirstUseEver);
    ImGui::Begin("性能统计", nullptr, ImGuiWindowFlags_None);
    
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "FPS: %.1f", currentFPS);
    ImGui::Text("帧时间: %.2f ms", frameTime * 1000.0f);
    ImGui::Text("最小帧时间: %.2f ms", minFrameTime * 1000.0f);
    ImGui::Text("最大帧时间: %.2f ms", maxFrameTime * 1000.0f);
    ImGui::Separator();
    ImGui::Text("时间缩放: %.2fx", timeScale);
    ImGui::SameLine();
    if (ImGui::Button("-##time")) timeScale = std::max(0.1f, timeScale - 0.1f);
    ImGui::SameLine();
    if (ImGui::Button("+##time")) timeScale = std::min(4.0f, timeScale + 0.1f);
    ImGui::SameLine();
    if (ImGui::Button("重置")) timeScale = 1.0f;
    
    if (ImGui::Checkbox("暂停游戏", &pauseGame)) {
        // 暂停状态切换
    }
    
    ImGui::End();
    
    // ==================== 相机控制窗口 ====================
    ImGui::SetNextWindowPos(ImVec2(10, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("相机控制", nullptr, ImGuiWindowFlags_None);
    
    glm::vec3 camPos = camera->getPosition();
    glm::vec3 camFront = camera->getFront();
    
    ImGui::Text("位置: (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
    ImGui::Text("朝向: (%.2f, %.2f, %.2f)", camFront.x, camFront.y, camFront.z);
    
    if (ImGui::SliderFloat("移动速度", &userMovementSpeed, 1.0f, 50.0f)) {
        camera->setMovementSpeed(userMovementSpeed);
    }
    
    if (ImGui::SliderFloat("鼠标灵敏度", &userSensitivity, 0.01f, 1.0f)) {
        camera->setMouseSensitivity(userSensitivity);
    }
    
    bool freeCam = camera->isFreeCameraMode();
    if (ImGui::Checkbox("自由视角模式", &freeCam)) {
        if (freeCam) camera->toggleFreeCamera();
        else camera->toggleFreeCamera();
    }
    
    if (ImGui::Button("重置相机位置")) {
        camera->setPosition(glm::vec3(0.0f, 1.0f, 5.0f));
    }
    
    ImGui::End();
    
    // ==================== 场景管理窗口 ====================
    ImGui::SetNextWindowPos(ImVec2(320, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("场景管理", nullptr, ImGuiWindowFlags_None);
    
    // ==================== 环境光 ====================
    if (ImGui::CollapsingHeader("环境光", ImGuiTreeNodeFlags_DefaultOpen)) {
        glm::vec3 ambient = lightManager->getAmbientColor();
        float ambientIntensity = lightManager->getAmbientIntensity();
        
        if (ImGui::ColorEdit3("颜色##ambient", &ambient.x)) {
            lightManager->setAmbientColor(ambient);
        }
        if (ImGui::SliderFloat("强度##ambient", &ambientIntensity, 0.0f, 2.0f)) {
            lightManager->setAmbientIntensity(ambientIntensity);
        }
    }
    
    // ==================== 光源管理 ====================
    if (ImGui::CollapsingHeader("光源", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("光源列表 (%zu/%d)", lightManager->getLightCount(), LightManager::getMaxLights());
        
        const auto& lights = lightManager->getLights();
        static int selectedLight = -1;
        
        // 光源列表
        for (size_t i = 0; i < lights.size(); i++) {
            const Light* light = lights[i].get();
            if (!light) continue;
            
            bool selected = (selectedLight == (int)i);
            std::string label = light->getName() + " (" + 
                std::string(light->getType() == LightType::DIRECTIONAL ? "方向光" :
                           light->getType() == LightType::POINT ? "点光源" : "聚光灯") + ")";
            
            if (ImGui::Selectable(label.c_str(), selected)) {
                selectedLight = i;
            }
            
            // 右键菜单
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("删除")) {
                    lightManager->removeLight(lightManager->getNextLightId() - lights.size() + i);
                    if (selectedLight >= (int)lights.size()) selectedLight = -1;
                }
                ImGui::EndPopup();
            }
        }
        
        // 编辑选中的光源
        if (selectedLight >= 0 && selectedLight < (int)lights.size()) {
            Light* light = lightManager->getLight(lightManager->getNextLightId() - lights.size() + selectedLight);
            if (light) {
                ImGui::Separator();
                ImGui::Indent();
                
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "%s", light->getName().c_str());
                
                bool enabled = light->isEnabled();
                if (ImGui::Checkbox("启用##light", &enabled)) {
                    light->setEnabled(enabled);
                }
                ImGui::SameLine();
                
                const char* typeStr = light->getType() == LightType::DIRECTIONAL ? "方向光" :
                                     light->getType() == LightType::POINT ? "点光源" : "聚光灯";
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "[%s]", typeStr);
                
                glm::vec3 color = light->getColor();
                if (ImGui::ColorEdit3("颜色##light", &color.x)) {
                    light->setColor(color);
                }
                
                float intensity = light->getIntensity();
                if (ImGui::SliderFloat("强度##light", &intensity, 0.0f, 10.0f)) {
                    light->setIntensity(intensity);
                }
                
                if (light->getType() != LightType::DIRECTIONAL) {
                    glm::vec3 pos = light->getPosition();
                    if (ImGui::DragFloat3("位置##light", &pos.x, 0.1f)) {
                        light->setPosition(pos);
                    }
                }
                
                if (light->getType() == LightType::DIRECTIONAL || light->getType() == LightType::SPOT) {
                    glm::vec3 dir = light->getDirection();
                    if (ImGui::DragFloat3("方向##light", &dir.x, 0.1f)) {
                        light->setDirection(dir);
                    }
                }
                
                if (light->getType() == LightType::POINT || light->getType() == LightType::SPOT) {
                    if (ImGui::TreeNode("衰减参数")) {
                        float constant = light->getConstant();
                        float linear = light->getLinear();
                        float quadratic = light->getQuadratic();
                        
                        if (ImGui::SliderFloat("常数项##atten", &constant, 0.0f, 2.0f)) {
                            light->setConstant(constant);
                        }
                        if (ImGui::SliderFloat("线性项##atten", &linear, 0.0f, 1.0f)) {
                            light->setLinear(linear);
                        }
                        if (ImGui::SliderFloat("二次项##atten", &quadratic, 0.0f, 1.0f)) {
                            light->setQuadratic(quadratic);
                        }
                        ImGui::TreePop();
                    }
                }
                
                if (light->getType() == LightType::SPOT) {
                    if (ImGui::TreeNode("聚光灯参数")) {
                        float innerCutoff = light->getInnerCutoff();
                        float outerCutoff = light->getOuterCutoff();
                        
                        if (ImGui::SliderFloat("内切角", &innerCutoff, 0.0f, 90.0f)) {
                            light->setInnerCutoff(innerCutoff);
                        }
                        if (ImGui::SliderFloat("外切角", &outerCutoff, 0.0f, 90.0f)) {
                            light->setOuterCutoff(outerCutoff);
                        }
                        ImGui::TreePop();
                    }
                }
                
                if (ImGui::TreeNode("阴影参数")) {
                    bool shadowEnabled = light->isShadowEnabled();
                    if (ImGui::Checkbox("启用阴影", &shadowEnabled)) {
                        light->setShadowEnabled(shadowEnabled);
                    }
                    
                    float shadowIntensity = light->getShadowIntensity();
                    if (ImGui::SliderFloat("阴影强度", &shadowIntensity, 0.0f, 1.0f)) {
                        light->setShadowIntensity(shadowIntensity);
                    }
                    ImGui::TreePop();
                }
                
                ImGui::Unindent();
            }
        }
        
        // 添加光源按钮
        ImGui::Separator();
        if (ImGui::Button("添加方向光")) {
            lightManager->addDirectionalLight("dir_light_" + std::to_string(lightManager->getLightCount()),
                                              glm::vec3(0.0f, -1.0f, 0.0f));
        }
        ImGui::SameLine();
        if (ImGui::Button("添加点光源")) {
            lightManager->addPointLight("point_light_" + std::to_string(lightManager->getLightCount()),
                                       camera->getPosition() + camera->getFront() * 3.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("添加聚光灯")) {
            lightManager->addSpotLight("spot_light_" + std::to_string(lightManager->getLightCount()),
                                      camera->getPosition(), camera->getFront());
        }
    }
    
    // ==================== 模型管理 ====================
    if (ImGui::CollapsingHeader("模型", ImGuiTreeNodeFlags_DefaultOpen)) {
        // OBJ 模型控制
        if (modelRenderer) {
            if (ImGui::TreeNode("OBJ 模型")) {
                static glm::vec3 modelPos(0.0f, 0.0f, -10.0f);
                static glm::vec3 modelRot(0.0f, 180.0f, 0.0f);
                static glm::vec3 modelScale(1.0f, 1.0f, 1.0f);
                
                if (ImGui::DragFloat3("位置##obj", &modelPos.x, 0.1f)) {
                    modelRenderer->setPosition(modelPos);
                }
                if (ImGui::DragFloat3("旋转##obj", &modelRot.x, 1.0f)) {
                    modelRenderer->setRotation(modelRot.x, modelRot.y, modelRot.z);
                }
                if (ImGui::DragFloat3("缩放##obj", &modelScale.x, 0.01f)) {
                    modelRenderer->setScale(modelScale);
                }
                ImGui::TreePop();
            }
        }
        
        // 玩家模型控制
        if (gltfModel && gltfModel->getMeshCount() > 0) {
            if (ImGui::TreeNode("玩家模型 (GLTF)")) {
                glm::vec3 pos = gltfModel->getPosition();
                glm::vec3 rot = gltfModel->getRotation();
                glm::vec3 scale = gltfModel->getScale();
                
                if (ImGui::DragFloat3("位置##player", &pos.x, 0.1f)) {
                    gltfModel->setPosition(pos);
                }
                if (ImGui::DragFloat3("旋转##player", &rot.x, 1.0f)) {
                    gltfModel->setRotation(rot.x, rot.y, rot.z);
                }
                if (ImGui::DragFloat3("缩放##player", &scale.x, 0.01f)) {
                    gltfModel->setScale(scale);
                }
                
                ImGui::Text("网格数: %zu", gltfModel->getMeshCount());
                ImGui::Text("动画数: %zu", gltfModel->getAnimationCount());
                ImGui::TreePop();
            }
        }
        
        // 动态加载的模型
        if (!models.empty()) {
            if (ImGui::TreeNode("场景模型")) {
                for (auto& [id, model] : models) {
                    if (ImGui::TreeNode(id.c_str())) {
                        glm::vec3 pos = model->getPosition();
                        glm::vec3 rot = model->getRotation();
                        glm::vec3 scale = model->getScale();
                        
                        if (ImGui::DragFloat3(("位置##" + id).c_str(), &pos.x, 0.1f)) {
                            model->setPosition(pos);
                        }
                        if (ImGui::DragFloat3(("旋转##" + id).c_str(), &rot.x, 1.0f)) {
                            model->setRotation(rot.x, rot.y, rot.z);
                        }
                        if (ImGui::DragFloat3(("缩放##" + id).c_str(), &scale.x, 0.01f)) {
                            model->setScale(scale);
                        }
                        
                        ImGui::Text("网格数: %zu", model->getMeshCount());
                        
                        if (model->getAnimationCount() > 0) {
                            ImGui::Text("动画数: %zu", model->getAnimationCount());
                            bool playing = model->isAnimationPlaying();
                            if (ImGui::Checkbox(("播放##" + id).c_str(), &playing)) {
                                if (playing) {
                                    model->playAllAnimations(true);
                                } else {
                                    model->stopAnimation();
                                }
                            }
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }
        
        // 立方体控制
        if (cubeRenderer) {
            if (ImGui::TreeNode("立方体")) {
                static glm::vec3 cubePos(0.0f, 0.0f, -1.0f);
                
                if (ImGui::DragFloat3("位置##cube", &cubePos.x, 0.1f)) {
                    cubeRenderer->setPosition(cubePos);
                }
                ImGui::TreePop();
            }
        }
    }
    
    // 从配置文件重新加载
    ImGui::Separator();
    if (ImGui::Button("从 JSON 重新加载配置")) {
        reloadSceneConfig();
    }
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "配置文件: assets/models/models.json");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "修改仅在内存中生效");
    
    ImGui::End();
        // ==================== 物理设置窗口 ====================
    ImGui::SetNextWindowPos(ImVec2(680, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("物理设置", nullptr, ImGuiWindowFlags_None);
    
    float gravity = camera->getGravity();
    if (ImGui::SliderFloat("重力", &gravity, 0.0f, 50.0f)) {
        camera->setGravity(gravity);
        physics->setGravity(gravity);
    }
    
    float groundHeight = camera->getDefaultGroundHeight();
    if (ImGui::SliderFloat("默认地面高度", &groundHeight, -10.0f, 10.0f)) {
        camera->setDefaultGroundHeight(groundHeight);
        physics->setGroundHeight(groundHeight);
        // 同步到 ECS
        if (useECS && ecsClientWorld) {
            auto player = ecsClientWorld->getPlayer();
            if (ecsClientWorld->registry().valid(player)) {
                auto& physicsComp = ecsClientWorld->registry().get<ecs::PhysicsComponent>(player);
                physicsComp.groundHeight = groundHeight;
            }
        }
    }
    
    float jumpForce = camera->getJumpForce();
    if (ImGui::SliderFloat("跳跃力", &jumpForce, 0.0f, 20.0f)) {
        camera->setJumpForce(jumpForce);
        // 同步到 ECS
        if (useECS && ecsClientWorld) {
            auto player = ecsClientWorld->getPlayer();
            if (ecsClientWorld->registry().valid(player)) {
                auto& physicsComp = ecsClientWorld->registry().get<ecs::PhysicsComponent>(player);
                physicsComp.jumpForce = jumpForce;
            }
        }
    }
    
    ImGui::Text("碰撞体数量: %zu", physics->getCollisionBoxes().size());
    
    // 帧率控制
    ImGui::Separator();
    ImGui::Text("帧率: %.0f FPS", currentFPS);
    ImGui::SliderFloat("目标帧率", &targetFPS, 30.0f, 144.0f, "%.0f");
    
    if (ImGui::Button("添加碰撞体")) {
        physics->addCollisionBox(camera->getPosition() + camera->getFront() * 3.0f, 
                                glm::vec3(1.0f, 1.0f, 1.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("清空碰撞体")) {
        physics->clearCollisionBoxes();
    }
    
    ImGui::End();
    
    // ==================== 渲染设置窗口 ====================
    ImGui::SetNextWindowPos(ImVec2(680, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("渲染设置", nullptr, ImGuiWindowFlags_None);
    
    // MSAA 设置 - 根据设备支持动态构建选项
    std::vector<const char*> msaaLevelNames;
    std::vector<VkSampleCountFlagBits> msaaLevelValues;
    
    msaaLevelNames.push_back("关闭");
    msaaLevelValues.push_back(VK_SAMPLE_COUNT_1_BIT);
    
    if (maxMsaaSamples >= VK_SAMPLE_COUNT_2_BIT) {
        msaaLevelNames.push_back("2x");
        msaaLevelValues.push_back(VK_SAMPLE_COUNT_2_BIT);
    }
    if (maxMsaaSamples >= VK_SAMPLE_COUNT_4_BIT) {
        msaaLevelNames.push_back("4x");
        msaaLevelValues.push_back(VK_SAMPLE_COUNT_4_BIT);
    }
    if (maxMsaaSamples >= VK_SAMPLE_COUNT_8_BIT) {
        msaaLevelNames.push_back("8x");
        msaaLevelValues.push_back(VK_SAMPLE_COUNT_8_BIT);
    }
    if (maxMsaaSamples >= VK_SAMPLE_COUNT_16_BIT) {
        msaaLevelNames.push_back("16x");
        msaaLevelValues.push_back(VK_SAMPLE_COUNT_16_BIT);
    }
    
    int currentMsaa = 0;
    for (size_t i = 0; i < msaaLevelValues.size(); i++) {
        if (msaaSamples == msaaLevelValues[i]) {
            currentMsaa = i;
            break;
        }
    }
    
    if (ImGui::Combo("MSAA", &currentMsaa, msaaLevelNames.data(), msaaLevelNames.size())) {
        if (currentMsaa >= 0 && currentMsaa < (int)msaaLevelValues.size()) {
            // 延迟到下一帧开始时应用，避免在 ImGui 渲染过程中重建交换链
            pendingMsaaSamples = msaaLevelValues[currentMsaa];
            pendingMsaaChange = true;
        }
    }
    
    ImGui::Text("设备最大支持: %s", 
        maxMsaaSamples == VK_SAMPLE_COUNT_64_BIT ? "64x" :
        maxMsaaSamples == VK_SAMPLE_COUNT_32_BIT ? "32x" :
        maxMsaaSamples == VK_SAMPLE_COUNT_16_BIT ? "16x" :
        maxMsaaSamples == VK_SAMPLE_COUNT_8_BIT ? "8x" :
        maxMsaaSamples == VK_SAMPLE_COUNT_4_BIT ? "4x" :
        maxMsaaSamples == VK_SAMPLE_COUNT_2_BIT ? "2x" : "无");
    
    // 线框模式
    ImGui::Checkbox("线框模式", &wireframeMode);
    
    // 窗口大小
    ImGui::Text("窗口: %d x %d", windowWidth, windowHeight);
    
    // Vulkan 信息
    ImGui::Separator();
    ImGui::Text("Vulkan 信息");
    ImGui::Text("当前帧: %u / %u", currentFrame, MAX_FRAMES_IN_FLIGHT);
    ImGui::Text("交换链图像: %zu", swapchain->getImageViews().size());
    
    ImGui::End();
    
    // ==================== 网络连接窗口 ====================
    ImGui::SetNextWindowPos(ImVec2(680, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 280), ImGuiCond_FirstUseEver);
    ImGui::Begin("网络连接", nullptr, ImGuiWindowFlags_None);
    
    bool isConnected = ecsClientWorld && ecsClientWorld->isConnectedToServer();
    
    // 连接状态
    if (isConnected) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "已连接到服务器");
        ImGui::Text("服务器: %s:%d", serverHost, serverPort);
        
        // 显示远程玩家数量
        auto& remotePlayers = ecsClientWorld->getNetworkSystem()->getRemotePlayers();
        ImGui::Text("远程玩家: %zu", remotePlayers.size());
        
        if (ImGui::Button("断开连接")) {
            disconnectRequested = true;
        }
    } else {
        // 服务器发现
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "未连接");
        
        ImGui::Separator();
        ImGui::Text("发现的服务器:");
        
        // 获取发现的服务器列表
        static int selectedServer = -1;
        if (ecsClientWorld) {
            auto servers = ecsClientWorld->getDiscoveredServers();
            
            if (servers.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "正在搜索...");
                
                // 自动启动扫描
                static bool discoveryStarted = false;
                if (!discoveryStarted) {
                    ecsClientWorld->startServerDiscovery();
                    discoveryStarted = true;
                }
            } else {
                for (int i = 0; i < (int)servers.size(); i++) {
                    const auto& server = servers[i];
                    
                    std::string label = server.name + " (" + server.host + ":" + std::to_string(server.port) + ")";
                    label += " [" + std::to_string(server.currentPlayers) + "/" + std::to_string(server.maxPlayers) + "]";
                    
                    if (ImGui::Selectable(label.c_str(), selectedServer == i)) {
                        selectedServer = i;
                        strncpy(serverHost, server.host.c_str(), sizeof(serverHost) - 1);
                        serverPort = server.port;
                    }
                }
            }
        }
        
        ImGui::Separator();
        ImGui::Text("手动连接:");
        ImGui::InputText("服务器", serverHost, sizeof(serverHost));
        ImGui::InputInt("端口", &serverPort);
        
        if (serverPort < 1) serverPort = 1;
        if (serverPort > 65535) serverPort = 65535;
        
        if (ImGui::Button("连接")) {
            connectRequested = true;
        }
    }
    
    ImGui::End();
    
    // ==================== 快捷键帮助窗口 ====================
    ImGui::SetNextWindowPos(ImVec2(680, 430), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 150), ImGuiCond_FirstUseEver);
    ImGui::Begin("快捷键帮助", nullptr, ImGuiWindowFlags_None);
    
    ImGui::Text("ESC - 开启/关闭开发者模式");
    ImGui::Text("WASD - 移动");
    ImGui::Text("Space - 跳跃/上升");
    ImGui::Text("Shift - 加速");
    ImGui::Text("R - 切换自由视角");
    ImGui::Text("Mouse - 视角控制");
    
    ImGui::End();
    

 ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto renderer = reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window));
    renderer->framebufferResized = true;
}

// ==================== 场景配置加载 ====================

SceneConfig Renderer::loadSceneConfig(const std::string& configFile) {
    SceneConfig sceneConfig;
    
    try {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            Logger::warning("无法打开场景配置文件: " + configFile);
            return sceneConfig;
        }
        
        nlohmann::json j;
        file >> j;
        
        // 加载环境光配置
        if (j.contains("ambient")) {
            const auto& ambient = j["ambient"];
            if (ambient.contains("color")) {
                auto c = ambient["color"];
                sceneConfig.ambient.color = glm::vec3(c[0], c[1], c[2]);
            }
            sceneConfig.ambient.intensity = ambient.value("intensity", 0.3f);
        }
        
        // 加载光源配置
        if (j.contains("lights") && j["lights"].is_array()) {
            for (const auto& item : j["lights"]) {
                LightConfig config;
                config.id = item.value("id", "");
                config.name = item.value("name", "");
                config.type = item.value("type", "point");
                config.enabled = item.value("enabled", true);
                
                if (item.contains("position")) {
                    auto pos = item["position"];
                    config.position = glm::vec3(pos[0], pos[1], pos[2]);
                }
                if (item.contains("direction")) {
                    auto dir = item["direction"];
                    config.direction = glm::vec3(dir[0], dir[1], dir[2]);
                }
                if (item.contains("color")) {
                    auto c = item["color"];
                    config.color = glm::vec3(c[0], c[1], c[2]);
                }
                
                config.intensity = item.value("intensity", 1.0f);
                config.constant = item.value("constant", 1.0f);
                config.linear = item.value("linear", 0.09f);
                config.quadratic = item.value("quadratic", 0.032f);
                config.innerCutoff = item.value("innerCutoff", 12.5f);
                config.outerCutoff = item.value("outerCutoff", 17.5f);
                config.shadowEnabled = item.value("shadowEnabled", false);
                config.shadowIntensity = item.value("shadowIntensity", 0.3f);
                
                sceneConfig.lights.push_back(config);
            }
        }
        
        // 加载模型配置
        if (j.contains("models") && j["models"].is_array()) {
            for (const auto& item : j["models"]) {
                ModelConfig config;
                config.id = item.value("id", "");
                config.file = item.value("file", "");
                config.enabled = item.value("enabled", true);
                
                if (item.contains("position")) {
                    auto pos = item["position"];
                    config.position = glm::vec3(pos[0], pos[1], pos[2]);
                }
                if (item.contains("rotation")) {
                    auto rot = item["rotation"];
                    config.rotation = glm::vec3(rot[0], rot[1], rot[2]);
                }
                if (item.contains("scale")) {
                    auto sc = item["scale"];
                    config.scale = glm::vec3(sc[0], sc[1], sc[2]);
                }
                
                config.subdivisionIterations = item.value("subdivisionIterations", 0);
                config.playAnimation = item.value("playAnimation", false);
                config.animationIndex = item.value("animationIndex", 0);
                config.playAllAnimations = item.value("playAllAnimations", false);
                config.isPlayerModel = item.value("isPlayerModel", false);
                config.isPlayerWalkModel = item.value("isPlayerWalkModel", false);
                config.description = item.value("description", "");
                
                sceneConfig.models.push_back(config);
            }
        }
        
        Logger::info("从 " + configFile + " 加载了 " + 
            std::to_string(sceneConfig.lights.size()) + " 个光源和 " +
            std::to_string(sceneConfig.models.size()) + " 个模型配置");
    } catch (const std::exception& e) {
        Logger::error("加载场景配置失败: " + std::string(e.what()));
    }
    
    return sceneConfig;
}

void Renderer::loadLightsFromConfig(const SceneConfig& config) {
    // 设置环境光
    lightManager->setAmbientColor(config.ambient.color);
    lightManager->setAmbientIntensity(config.ambient.intensity);
    
    // 清除现有光源
    lightManager->clear();
    
    // 加载光源
    for (const auto& lightConfig : config.lights) {
        if (!lightConfig.enabled) {
            continue;
        }
        
        int lightId = -1;
        
        if (lightConfig.type == "directional") {
            lightId = lightManager->addDirectionalLight(
                lightConfig.name.empty() ? lightConfig.id : lightConfig.name,
                lightConfig.direction,
                lightConfig.color,
                lightConfig.intensity
            );
        } else if (lightConfig.type == "point") {
            lightId = lightManager->addPointLight(
                lightConfig.name.empty() ? lightConfig.id : lightConfig.name,
                lightConfig.position,
                lightConfig.color,
                lightConfig.intensity,
                10.0f  // 默认范围
            );
            // 设置衰减参数
            if (Light* light = lightManager->getLight(lightId)) {
                light->setConstant(lightConfig.constant);
                light->setLinear(lightConfig.linear);
                light->setQuadratic(lightConfig.quadratic);
            }
        } else if (lightConfig.type == "spot") {
            lightId = lightManager->addSpotLight(
                lightConfig.name.empty() ? lightConfig.id : lightConfig.name,
                lightConfig.position,
                lightConfig.direction,
                lightConfig.color,
                lightConfig.intensity,
                lightConfig.innerCutoff,
                lightConfig.outerCutoff,
                10.0f  // 默认范围
            );
            // 设置衰减参数
            if (Light* light = lightManager->getLight(lightId)) {
                light->setConstant(lightConfig.constant);
                light->setLinear(lightConfig.linear);
                light->setQuadratic(lightConfig.quadratic);
                light->setShadowEnabled(lightConfig.shadowEnabled);
                light->setShadowIntensity(lightConfig.shadowIntensity);
            }
        }
        
        if (lightId >= 0 && lightConfig.type == "directional") {
            if (Light* light = lightManager->getLight(lightId)) {
                light->setShadowEnabled(lightConfig.shadowEnabled);
                light->setShadowIntensity(lightConfig.shadowIntensity);
            }
        }
    }
    
    Logger::info("加载了 " + std::to_string(lightManager->getLightCount()) + " 个光源");
}

void Renderer::reloadSceneConfig() {
    Logger::info("重新加载场景配置...");
    
    SceneConfig config = loadSceneConfig("assets/models/models.json");
    
    // 重新加载光源
    if (!config.lights.empty()) {
        loadLightsFromConfig(config);
    }
    
    // 注意：模型重新加载较复杂，暂时只重新加载光源
    // 模型重新加载需要清理现有模型资源
    
    Logger::info("场景配置重新加载完成");
}

std::vector<ModelConfig> Renderer::loadModelConfig(const std::string& configFile) {
    SceneConfig sceneConfig = loadSceneConfig(configFile);
    return sceneConfig.models;
}

VkDescriptorSet Renderer::createModelDescriptorSet(GLTFModel* model, const std::string& modelId) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureDescriptorSetLayout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(vulkanDevice->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        Logger::warning("无法为模型 " + modelId + " 分配纹理描述符集");
        return VK_NULL_HANDLE;
    }

    // 获取模型纹理
    std::shared_ptr<Texture> texture = model->getFirstTexture();
    if (texture) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture->getImageView();
        imageInfo.sampler = texture->getSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(vulkanDevice->getDevice(), 1, &descriptorWrite, 0, nullptr);
        Logger::info("为模型 " + modelId + " 创建纹理描述符集成功");
    } else {
        Logger::warning("模型 " + modelId + " 没有纹理，使用默认");
    }
    
    return descriptorSet;
}

void Renderer::loadModelsFromConfig(const std::vector<ModelConfig>& configs) {
    for (const auto& config : configs) {
        if (!config.enabled) {
            Logger::info("模型 " + config.id + " 已禁用，跳过");
            continue;
        }
        
        // 创建模型
        auto model = std::make_unique<GLTFModel>(vulkanDevice, textureLoader);
        model->setPosition(config.position);
        model->setRotation(config.rotation.x, config.rotation.y, config.rotation.z);
        model->setScale(config.scale);
        
        if (config.subdivisionIterations > 0) {
            model->setSubdivisionIterations(config.subdivisionIterations);
        }
        
        // 加载模型文件
        if (!model->loadFromFile(config.file)) {
            Logger::error("加载模型失败: " + config.file);
            continue;
        }
        
        // 创建纹理描述符集
        VkDescriptorSet descriptorSet = createModelDescriptorSet(model.get(), config.id);
        
        // 处理特殊模型
        if (config.isPlayerModel) {
            gltfModel = std::move(model);
            gltfModelDescriptorSet = descriptorSet;
            Logger::info("玩家模型已加载: " + config.file);
        } else if (config.isPlayerWalkModel) {
            gltfWalkModel = std::move(model);
            gltfWalkModelDescriptorSet = descriptorSet;
            Logger::info("玩家行走模型已加载: " + config.file);
        } else {
            // 普通模型存入 map
            models[config.id] = std::move(model);
            modelDescriptorSets[config.id] = descriptorSet;
            
            // 播放动画
            if (config.playAllAnimations && model->getAnimationCount() > 0) {
                model->playAllAnimations(true, 1.0f);
            } else if (config.playAnimation && model->getAnimationCount() > config.animationIndex) {
                model->playAnimation(config.animationIndex, true, 1.0f);
            }
            
            Logger::info("模型 " + config.id + " 已加载");
        }
    }
}

} // namespace vgame