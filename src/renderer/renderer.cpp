// 渲染器实现 — 纯渲染编排器
// 负责 Vulkan 管线生命周期、帧缓冲、命令缓冲录制、渲染子系统调度。
// 游戏逻辑（ECS/物理/碰撞/网络/玩家动画）已提取至 GameSession。
#include "imgui.h"
#include "core/renderer.h"
#include "core/camera.h"
#include "core/input.h"
#include "core/game_session.h"
#include "ecs/i_game_world.h"
#include "core/scene_config.h"
#include "utils/logger.h"
#include "utils/asset_paths.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <limits>
#include <random>
#include <thread>
#include <future>
#include "renderer/tree_system.h"
#include "renderer/stone_system.h"
#include "renderer/grass_system.h"
#include "core/game_config.h"

namespace owengine {

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
    
    auto queueIndices = vulkanInstance->getQueueFamilyIndices();
    vulkanDevice = std::make_shared<VulkanDevice>(
        vulkanInstance->getPhysicalDevice(),
        vulkanInstance->getDevice(),
        vulkanInstance->getSurface(),
        queueIndices.graphicsFamily.value(),
        queueIndices.presentFamily.value()
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
    
    // FSR1 开启时禁用 MSAA（低分辨率无需抗锯齿，也避免 resolve 不匹配）
    if (fsrScale_ < 1.0f) {
        msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    }
    bool useFsrFb = false;  // FSR1 framebuffer 有性能问题，临时禁用
    
    swapchain = std::make_shared<VulkanSwapchain>(vulkanDevice, window);
    swapchain->create();
    if (useFsrFb) msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    
    renderPass = std::make_shared<VulkanRenderPass>(vulkanDevice, swapchain->getImageFormat(), msaaSamples);
    renderPass->create();
    
    // FSR1 管线
    fsr1Pass_ = std::make_unique<Fsr1Pass>(vulkanDevice, swapchain->getImageFormat(), swapchain->getExtent(), fsrScale_);
    fsr1Pass_->init();
    
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
        AssetPaths::MAIN_VERT_SHADER,
        AssetPaths::MAIN_FRAG_SHADER,
        owengine::VertexFormat::POSITION_COLOR,
        descriptorSetLayouts,
        msaaSamples
    );
    graphicsPipeline->create();
    
    // 创建深度资源
    VkExtent2D renderExt = useFsrFb
        ? VkExtent2D{std::max(1u, (uint32_t)(swapchain->getExtent().width * fsrScale_)),
                     std::max(1u, (uint32_t)(swapchain->getExtent().height * fsrScale_))}
        : swapchain->getExtent();
    vulkanDevice->createDepthResources(renderExt, msaaSamples);
    
    std::vector<VkImageView> colorAttachments;
    if (useFsrFb) {
        colorAttachments.push_back(fsr1Pass_->getColorImageView());
    } else {
        colorAttachments = swapchain->getImageViews();
    }
    framebuffers = std::make_shared<VulkanFramebuffer>(vulkanDevice, renderPass->getRenderPass());
    framebuffers->create(colorAttachments, useFsrFb ? renderExt : swapchain->getExtent(), colorImageView);
    
    commandBuffers = std::make_shared<VulkanCommandBuffer>(
        vulkanDevice,
        renderPass->getRenderPass(),
        graphicsPipeline->getPipeline(),
        graphicsPipeline->getPipelineLayout()
    );
    commandBuffers->create(MAX_FRAMES_IN_FLIGHT);
    
    syncObjects = std::make_shared<VulkanSync>(vulkanDevice);
    syncObjects->create(MAX_FRAMES_IN_FLIGHT);
    
    // 不预先记录命令缓冲，每次drawFrame时动态记录

    terrainRenderer = std::make_shared<TerrainRenderer>(vulkanDevice);
    terrainRenderer->create();

    // 初始更新地形区块（玩家位置随后由 GameSession 驱动）
    terrainRenderer->update(glm::vec3(0.0f, 0.0f, 5.0f));

    // 构建地形高度查询回调（渲染侧共享，GameSession 和 树/石/草系统共用）
    auto weakTerrain = std::weak_ptr<TerrainRenderer>(terrainRenderer);
    auto terrainHeightQuery = [weakTerrain](float x, float z) -> float {
        auto terrain = weakTerrain.lock();
        if (!terrain) return 0.0f;
        return terrain->getHeight(x, z);
    };

    // 初始化纹理加载器
    textureLoader = std::make_shared<TextureLoader>(vulkanDevice);

    // 初始化光源管理器
    lightManager = std::make_unique<LightManager>();

    // 初始化天空盒渲染器
    skyboxRenderer = std::make_unique<SkyboxRenderer>(vulkanDevice);
    skyboxRenderer->create();
    try {
        skyboxRenderer->loadCubemapFromCrossLayout(AssetPaths::SKYBOX_TEXTURE);
    } catch (const std::runtime_error& e) {
        Logger::warning("天空盒纹理加载失败: " + std::string(e.what()));
    }

    // 初始化模型渲染器（不含玩家模型，玩家模型由 GameSession 管理）
    modelRenderer = std::make_unique<ModelRenderer>(vulkanDevice, textureLoader);
    modelRenderer->create();

    // 创建天空盒管线
    skyboxPipeline = std::make_shared<VulkanPipeline>(
        vulkanDevice,
        renderPass->getRenderPass(),
        swapchain->getExtent(),
        AssetPaths::SKYBOX_VERT_SHADER,
        AssetPaths::SKYBOX_FRAG_SHADER,
        VertexFormat::POSITION_ONLY,
        std::vector<VkDescriptorSetLayout>{skyboxRenderer->getDescriptorSetLayout()},
        msaaSamples
    );
    skyboxPipeline->create();

    // 基础描述符池
    createDescriptorPool(20, 20);

    // 从 JSON 配置文件加载场景（光源和静态模型）
    SceneConfig sceneConfig = loadSceneConfig(AssetPaths::SCENE_CONFIG);

    // 加载光源
    if (!sceneConfig.lights.empty()) {
        loadLightsFromConfig(sceneConfig);
    } else {
        Logger::warning("使用默认光源配置");
        lightManager->addDirectionalLight("sun", glm::vec3(0.5f, -1.0f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f), 1.0f);
        lightManager->addPointLight("point1", glm::vec3(2.0f, 3.0f, 2.0f), glm::vec3(1.0f, 1.0f, 1.0f), 2.0f, 10.0f);
        lightManager->addPointLight("point2", glm::vec3(-2.0f, 3.0f, -2.0f), glm::vec3(1.0f, 1.0f, 1.0f), 2.0f, 10.0f);
        lightManager->setAmbientColor(glm::vec3(0.5f, 0.5f, 0.5f));
        lightManager->setAmbientIntensity(0.5f);
    }

    // 加载静态模型（不含玩家模型，玩家模型由 GameSession 加载）
    if (!sceneConfig.models.empty()) {
        loadModelsFromConfig(sceneConfig.models);
    }

    // 创建描述符集（默认纹理描述符集 + 光源 uniform buffer）
    createDescriptorSets();

    // 加载渲染器配置
    auto gameConfig = GameConfig::load(AssetPaths::GAME_CONFIG);
    targetFPS = gameConfig.renderer.targetFPS;

    // 初始化树系统
    treeSystem_ = std::make_unique<TreeSystem>(vulkanDevice, textureLoader, textureDescriptorSetLayout);
    treeSystem_->setHeightSampler(terrainHeightQuery);
    treeSystem_->init(gameConfig.tree);

    // 初始化石头系统
    stoneSystem_ = std::make_unique<StoneSystem>(vulkanDevice, textureLoader, textureDescriptorSetLayout);
    stoneSystem_->setHeightSampler(terrainHeightQuery);
    stoneSystem_->init(gameConfig.stone);

    // 初始化草丛系统
    grassSystem_ = std::make_unique<GrassSystem>(vulkanDevice);
    grassSystem_->setHeightSampler(terrainHeightQuery);
    grassSystem_->init(gameConfig.grass, renderPass->getRenderPass(),
                       swapchain->getExtent(), msaaSamples);

    grassSystem_->setTreeQuery([this](float x, float z, float radius) {
        return treeSystem_->queryPositions(x, z, radius);
    });
    grassSystem_->setStoneQuery([this](float x, float z, float radius) {
        return stoneSystem_->queryPositions(x, z, radius);
    });
    if (auto* sunLight = lightManager->getLightByName("sun")) {
        grassSystem_->setGlobalLightDir(sunLight->getDirection());
    }

    // 初始化 ImGui
    imguiManager = std::make_unique<ImGuiManager>(vulkanDevice, swapchain, renderPass, window, vulkanInstance->getInstance(), msaaSamples);
    imguiManager->init();

    // 创建游戏会话（持有 ECS/物理/碰撞/网络/动画逻辑）
    if (externalGameSession_) {
        gameSession_ = externalGameSession_;
    } else {
        ownedGameSession_ = std::make_unique<GameSession>();
        GameSessionInitParams gsParams;
        gsParams.window = window;
        gsParams.windowWidth = windowWidth;
        gsParams.windowHeight = windowHeight;
        gsParams.device = vulkanDevice;
        gsParams.textureLoader = textureLoader;
        gsParams.terrainRenderer = terrainRenderer;
        gsParams.treeSystem = treeSystem_.get();
        gsParams.stoneSystem = stoneSystem_.get();
        gsParams.grassSystem = grassSystem_.get();
        gsParams.descriptorPool = descriptorPool;
        gsParams.textureDescriptorSetLayout = textureDescriptorSetLayout;
        gsParams.lightDescriptorSetLayout = lightDescriptorSetLayout;
        gsParams.graphicsPipelineLayout = graphicsPipeline->getPipelineLayout();
        gsParams.terrainHeightQuery = terrainHeightQuery;
        ownedGameSession_->init(gsParams);
        gameSession_ = ownedGameSession_.get();
    }

    lastTime = std::chrono::high_resolution_clock::now();
}

void Renderer::mainLoop() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    // 第一帧后捕获鼠标，确保窗口已经显示
    bool firstFrame = true;
    
    // FPS 计数
    int frameCount = 0;
    float fpsTimer = 0.0f;
    
    // 绝对帧时间目标（用 sleep_until 替代 sleep_for 避免累计漂移）
    auto nextFrameTime = std::chrono::high_resolution_clock::now();
    
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
        
        glfwPollEvents();

        // 第一帧后捕获鼠标
        if (firstFrame) {
            if (gameSession_ && gameSession_->getInput()) {
                gameSession_->getInput()->setCursorCaptured(true);
            }
            firstFrame = false;
            glfwPollEvents();
        }

        // === ImGui 新帧 + HUD ===
        imguiManager->newFrame();
        if (gameSession_) {
            ImGui::Begin("HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("FPS: %.1f", gameSession_->getCurrentFPS());
            ImGui::Text("Logic:%.1f Draw:%.1f GPU:%.1f ms",
                        gameSession_->getProfLogicMs(), profDrawMs_, profGPUMs_);
            if (auto* ecs = gameSession_->getECSWorld()) {
                glm::vec3 pos = ecs->getPlayerPosition();
                ImGui::Text("Pos: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
            }
            ImGui::SetWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::End();
        }

        // === 更新游戏逻辑（委托给 GameSession） ===
        if (gameSession_) {
            float scaledDt = gameSession_->pauseGame ? 0.0f : deltaTime * gameSession_->timeScale;
            gameSession_->update(scaledDt);
        }

        // === 渲染帧 ===
        {
            auto t0 = std::chrono::high_resolution_clock::now();
            drawFrame();
            profDrawMs_ = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
        }

        // === FPS 统计 ===
        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 1.0f) {
            float fps = float(frameCount) / fpsTimer;
            std::cout << "[Renderer] FPS: " << (int)fps
                      << " D=" << (int)profDrawMs_
                      << " G=" << (int)profGPUMs_ << "ms" << std::endl;
            frameCount = 0;
            fpsTimer = 0.0f;
            minFrameTime = 999.0f;
            maxFrameTime = 0.0f;
        }

        // === 帧率限制 ===
        if (targetFPS > 0.0f) {
            nextFrameTime += std::chrono::nanoseconds(static_cast<long long>((1.0 / targetFPS) * 1e9));
        }
        auto frameEndTime = std::chrono::high_resolution_clock::now();
        frameTime = std::chrono::duration<float>(frameEndTime - frameStartTime).count();
        if (frameTime < minFrameTime) minFrameTime = frameTime;
        if (frameTime > maxFrameTime) maxFrameTime = frameTime;
        if (targetFPS > 0.0f && frameEndTime < nextFrameTime) {
            std::this_thread::sleep_until(nextFrameTime);
        } else {
            nextFrameTime = frameEndTime;
        }
    }

    vkDeviceWaitIdle(vulkanDevice->getDevice());
}

void Renderer::drawFrame() {
    auto fenceT0 = std::chrono::high_resolution_clock::now();
    vkWaitForFences(vulkanDevice->getDevice(), 1, &syncObjects->getInFlightFences()[currentFrame], VK_TRUE, UINT64_MAX);
    profGPUMs_ = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - fenceT0).count();
    
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
    
    // 更新 FSR1 输出描述符指向当前 swapchain 图像（必须在 cmd buffer 录制前）
    if (fsr1Pass_ && fsrScale_ < 1.0f && imageIndex < swapchain->getImageViews().size()) {
        fsr1Pass_->updateOutputDescriptor(swapchain->getImageViews()[imageIndex]);
    }
    
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
    {
        VkExtent2D vpExt = fsr1Pass_ ? fsr1Pass_->getRenderExtent() : swapchain->getExtent();
        viewport.width = (float)vpExt.width;
        viewport.height = (float)vpExt.height;
    }
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = fsr1Pass_ ? fsr1Pass_->getRenderExtent() : swapchain->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // 从 GameSession 获取摄像机（渲染所需视口/投影矩阵）
    Camera* cam = gameSession_ ? gameSession_->getCamera() : nullptr;
    if (!cam) {
        vkCmdEndRenderPass(commandBuffer);
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {}
        return;
    }

    // 先渲染天空盒（背景）
    if (skyboxRenderer && skyboxRenderer->getDescriptorSet() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline->getPipeline());
        skyboxRenderer->render(commandBuffer, skyboxRenderer->getPipelineLayout(),
                             cam->getViewMatrix(), cam->getProjectionMatrix());
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->getPipeline());
    }

    updateLightUniformBuffer();

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           graphicsPipeline->getPipelineLayout(), 0, 1, &textureDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           graphicsPipeline->getPipelineLayout(), 1, 1, &lightDescriptorSet, 0, nullptr);

    // 渲染地形
    terrainRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                          cam->getViewMatrix(), cam->getProjectionMatrix());

    // 渲染 OBJ 模型
    if (modelRenderer) {
        modelRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                            cam->getViewMatrix(), cam->getProjectionMatrix());
    }

    // 渲染玩家模型（从 GameSession 获取）
    if (gameSession_) {
        GLTFModel* playerModel = gameSession_->getActivePlayerModel();
        VkDescriptorSet playerDescSet = gameSession_->getActivePlayerDescriptorSet();
        if (playerModel && playerModel->getMeshCount() > 0) {
            if (playerDescSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       graphicsPipeline->getPipelineLayout(), 0, 1, &playerDescSet, 0, nullptr);
            }
            playerModel->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                              cam->getViewMatrix(), cam->getProjectionMatrix(),
                              playerModel->getModelMatrix());
        }
    }

    // 渲染动态加载的静态模型（带视锥体剔除）
    for (auto& [id, model] : models) {
        if (model && model->getMeshCount() > 0) {
            auto bbox = model->getBoundingBox();
            glm::vec3 worldMin = model->getPosition() + bbox.first * model->getScale();
            glm::vec3 worldMax = model->getPosition() + bbox.second * model->getScale();

            glm::vec3 modelCenter = (worldMin + worldMax) * 0.5f;
            if (glm::length(modelCenter - cam->getPosition()) > 250.0f) continue;
            if (!cam->getFrustum().isAABBInside(worldMin, worldMax)) continue;

            auto it = modelDescriptorSets.find(id);
            if (it != modelDescriptorSets.end() && it->second != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       graphicsPipeline->getPipelineLayout(), 0, 1, &it->second, 0, nullptr);
            }
            model->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                        cam->getViewMatrix(), cam->getProjectionMatrix(),
                        model->getModelMatrix());
        }
    }

    // 渲染树木/石头/草丛
    treeSystem_->render(commandBuffer, graphicsPipeline->getPipelineLayout(), *cam);
    if (stoneSystem_) stoneSystem_->render(commandBuffer, graphicsPipeline->getPipelineLayout(), *cam);
    if (grassSystem_) grassSystem_->render(commandBuffer, *cam);

    // 渲染远程玩家模型（从 GameSession 获取）
    if (gameSession_) {
        for (const auto& [clientId, rp] : gameSession_->getRemotePlayerModels()) {
            GLTFModel* activeModel = rp.wasMoving ? rp.walkModel.get() : rp.idleModel.get();
            if (activeModel && activeModel->getMeshCount() > 0) {
                activeModel->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                            cam->getViewMatrix(), cam->getProjectionMatrix(),
                            activeModel->getModelMatrix());
            }
        }
    }
    
    // 渲染 ImGui
    imguiManager->render(commandBuffer);
    
    vkCmdEndRenderPass(commandBuffer);

    // FSR1 上采样（仅 fsrScale_ < 1.0 时生效）
    if (fsr1Pass_ && fsrScale_ < 1.0f) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image = swapchain->getImages()[imageIndex];
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

        fsr1Pass_->dispatch(commandBuffer);

        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = 0;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

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
    
    // FSR1 帧推进（仅开启时切换描述符集）
    if (fsr1Pass_ && fsrScale_ < 1.0f) fsr1Pass_->advanceFrame();
    
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
    
    // 重建 FSR1 管线（新尺寸）
    VkExtent2D renderExt;
    bool useFsrFb = false;
    if (useFsrFb) {
        fsr1Pass_->cleanup();
        fsr1Pass_ = std::make_unique<Fsr1Pass>(vulkanDevice, swapchain->getImageFormat(), swapchain->getExtent(), fsrScale_);
        fsr1Pass_->init();
        renderExt = fsr1Pass_->getRenderExtent();
    } else {
        renderExt = swapchain->getExtent();
        if (fsr1Pass_) {
            fsr1Pass_->cleanup();
            fsr1Pass_ = std::make_unique<Fsr1Pass>(vulkanDevice, swapchain->getImageFormat(), swapchain->getExtent(), fsrScale_);
            fsr1Pass_->init();
        }
    }

    // 重新创建深度资源
    vulkanDevice->createDepthResources(renderExt, msaaSamples);
    
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
    
    // 重建帧缓冲
    if (useFsrFb) {
        std::vector<VkImageView> att = {fsr1Pass_->getColorImageView()};
        framebuffers->recreate(att, renderExt, colorImageView);
    } else {
        framebuffers->recreate(swapchain->getImageViews(), renderExt, colorImageView);
    }
    commandBuffers->cleanup();
    commandBuffers->updateRenderPass(renderPass->getRenderPass());
    commandBuffers->updatePipeline(graphicsPipeline->getPipeline());
    commandBuffers->updatePipelineLayout(graphicsPipeline->getPipelineLayout());
    commandBuffers->create(useFsrFb ? 1 : swapchain->getImageViews().size());
    
    // 重新初始化 ImGui 以匹配新的 MSAA 设置
    imguiManager.reset();
    imguiManager = std::make_unique<ImGuiManager>(vulkanDevice, swapchain, renderPass, window, vulkanInstance->getInstance(), msaaSamples);
    imguiManager->init();
    
    // 记录命令缓冲（所有使用 frame buffer 0）
    VkFramebuffer fb = framebuffers->getFramebuffers()[0];
    Camera* cam = gameSession_ ? gameSession_->getCamera() : nullptr;
    glm::mat4 viewMat = cam ? cam->getViewMatrix() : glm::mat4(1.0f);
    glm::mat4 projMat = cam ? cam->getProjectionMatrix() : glm::mat4(1.0f);
    for (size_t i = 0; i < commandBuffers->getCommandBuffers().size(); i++) {
        commandBuffers->record(i, fb, renderExt, viewMat, projMat);
    }
}

void Renderer::cleanup() {
    // 等待设备空闲，确保所有渲染操作完成
    if (vulkanDevice) {
        vkDeviceWaitIdle(vulkanDevice->getDevice());
    }
    
    // 清理游戏会话（必须在 Vulkan 资源销毁前）
    ownedGameSession_.reset();
    gameSession_ = nullptr;

    // 清理动态加载的模型
    models.clear();
    modelDescriptorSets.clear();
    
    // 清理 ImGui（使用 Vulkan）
    imguiManager.reset();
    
    modelRenderer.reset();
    skyboxRenderer.reset();
    terrainRenderer.reset();
    
    // 清理新系统
    lightManager.reset();
    textureLoader.reset();
    
    // 清理树木（由 TreeSystem 统一管理）
    treeSystem_.reset();
    // 清理石头系统
    stoneSystem_.reset();
    // 清理草丛系统
    grassSystem_.reset();
    // 清理 FSR1 管线
    fsr1Pass_.reset();

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
    VkExtent2D targetExt = {std::max(1u, (uint32_t)(swapchain->getExtent().width * fsrScale_)),
                            std::max(1u, (uint32_t)(swapchain->getExtent().height * fsrScale_))};
    imageInfo.extent.width = targetExt.width;
    imageInfo.extent.height = targetExt.height;
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

void Renderer::createDescriptorPool(uint32_t maxSets, uint32_t descriptorCount) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // 纹理描述符（每个 mesh 需要一个 sampler，每个模型需要一个额外 sampler）
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = descriptorCount;
    
    // 光源 uniform buffer
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;

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
                config.description = item.value("description", "");
                
                if (item.contains("hiddenMeshNames")) {
                    for (const auto& h : item["hiddenMeshNames"]) {
                        config.hiddenMeshNames.push_back(h.get<std::string>());
                    }
                }
                
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
    
    SceneConfig config = loadSceneConfig(AssetPaths::SCENE_CONFIG);
    
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

VkDescriptorSet Renderer::createModelDescriptorSet(GLTFModel* model, const std::string& modelId, VkDescriptorPool pool) {
    VkDescriptorPool targetPool = (pool != VK_NULL_HANDLE) ? pool : descriptorPool;
    
    // 从全局池分配描述符集时需要加锁（vkAllocateDescriptorSets 要求外部同步）
    std::unique_lock<std::mutex> poolLock(descriptorPoolMutex_, std::defer_lock);
    if (pool == VK_NULL_HANDLE) poolLock.lock();
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = targetPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureDescriptorSetLayout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(vulkanDevice->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        Logger::warning("无法为模型 " + modelId + " 分配纹理描述符集");
        return VK_NULL_HANDLE;
    }
    
    if (pool == VK_NULL_HANDLE) poolLock.unlock();

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
        
        // 为每个 mesh 创建独立的纹理描述符集
        model->createMeshDescriptorSets(textureDescriptorSetLayout, descriptorPool);
        
        // 设置隐藏的节点名称
        if (!config.hiddenMeshNames.empty()) {
            model->setHiddenNodeNames(config.hiddenMeshNames);
        }
        
        // 播放动画（必须在 std::move 之前，model 不能为空）
        if (config.playAllAnimations && model->getAnimationCount() > 0) {
            model->playAllAnimations(true, 1.0f);
        } else if (config.playAnimation && model->getAnimationCount() > config.animationIndex) {
            model->playAnimation(config.animationIndex, true, 1.0f);
        }
        
        // 创建纹理描述符集（用于没有材质的 mesh 的回退）
        VkDescriptorSet descriptorSet = createModelDescriptorSet(model.get(), config.id);
        
        // 普通模型存入 map
        models[config.id] = std::move(model);
        modelDescriptorSets[config.id] = descriptorSet;
        
        Logger::info("模型 " + config.id + " 已加载");
    }
}

} // namespace owengine