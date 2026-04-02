// 渲染器实现
// 负责管理整个渲染流程，包括窗口创建、Vulkan初始化和渲染循环
#include "imgui.h"
#include "renderer.h"
#include "skybox_renderer.h"
#include "logger.h"
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
    floorRenderer = std::make_unique<FloorRenderer>(vulkanDevice);
    floorRenderer->create();
    cubeRenderer = std::make_unique<CubeRenderer>(vulkanDevice);
    cubeRenderer->create();
    
    // 初始化纹理加载器
    textureLoader = std::make_shared<TextureLoader>(vulkanDevice);
    
    // 初始化光源管理器
    lightManager = std::make_unique<LightManager>();
    // 添加默认方向光（类似太阳光）- 纯白色
    lightManager->addDirectionalLight("sun", glm::vec3(0.5f, -1.0f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f), 1.0f);
    // 添加几个点光源 - 纯白色
    lightManager->addPointLight("point1", glm::vec3(2.0f, 3.0f, 2.0f), glm::vec3(1.0f, 1.0f, 1.0f), 2.0f, 10.0f);
    lightManager->addPointLight("point2", glm::vec3(-2.0f, 3.0f, -2.0f), glm::vec3(1.0f, 1.0f, 1.0f), 2.0f, 10.0f);
    // 设置环境光为纯白色
    lightManager->setAmbientColor(glm::vec3(0.5f, 0.5f, 0.5f));
    lightManager->setAmbientIntensity(0.5f);
    
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
    
    // 初始化GLTF模型（在创建描述符集之前）
    gltfModel = std::make_unique<GLTFModel>(vulkanDevice, textureLoader);
    // 尝试加载GLTF模型（如果有）
    try {
        if (gltfModel->loadFromFile("assets/models/sample.gltf")) {
            gltfModel->setPosition(glm::vec3(0.0f, 0.0f, -5.0f));  // 恢复到原来的位置
            gltfModel->setScale(glm::vec3(1.0f, 1.0f, 1.0f));  // 恢复原来的缩放
            std::cout << "[Renderer] GLTF模型加载成功" << std::endl;
        }
    } catch (const std::runtime_error& e) {
        std::cout << "[Renderer] GLTF模型加载失败: " << e.what() << std::endl;
    }
    
    // 创建描述符集
    createDescriptorSets();
    
    // 初始化 ImGui
    imguiManager = std::make_unique<ImGuiManager>(vulkanDevice, swapchain, renderPass, window, vulkanInstance->getInstance());
    imguiManager->init();
    
    // 设置立方体位置（在相机正前方）
    glm::vec3 cubePos(0.0f, 0.0f, -1.0f);
    cubeRenderer->setPosition(cubePos);
    
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
    const float targetFPS = 60.0f;
    const float targetFrameTime = 1.0f / targetFPS;  // 60FPS = 16.67ms
    
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
        
        // FPS 计算
        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 1.0f) {  // 每秒更新一次
            currentFPS = frameCount / fpsTimer;
            std::cout << "[Renderer] FPS: " << static_cast<int>(currentFPS) << std::endl;
            
            frameCount = 0;
            fpsTimer = 0.0f;
        }
        
        glfwPollEvents();
        
        // 第一帧后捕获鼠标
        if (firstFrame) {
            input->setCursorCaptured(true);
            firstFrame = false;
            glfwPollEvents();
            std::cout << "[Renderer] 鼠标已捕获" << std::endl;
        }
        
        // 检测按键状态（在update之前，避免状态被重置）
        
            jumpInput = input->isKeyJustPressed(GLFW_KEY_SPACE);
        
            freeCameraToggle = input->isKeyJustPressed(GLFW_KEY_R);
        
                    
        
                    shiftInput = input->isSprintPressed();
        
                    
        
                    // 自由视角中需要持续检测空格键（用于长按上升）
        
                    spaceHeld = input->isKeyPressed(GLFW_KEY_SPACE);
        
                    
        
                    // 更新输入
        
                    input->update();            // 处理鼠标移动
            double mouseX, mouseY;
            input->getRawMouseMovement(mouseX, mouseY);
            
            if (mouseX != 0.0 || mouseY != 0.0) {
                camera->processMouseMovement(static_cast<float>(mouseX), static_cast<float>(mouseY));
            }        
        
        // 更新 ImGui
        imguiManager->newFrame();
        
        // 显示 FPS 窗口
        ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("FPS: %.1f", currentFPS);
        ImGui::SetWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::End();
        // 更新游戏逻辑
        updateGameLogic(deltaTime);
        
        drawFrame();
        
        // 在帧结束时重置"刚刚按下"标志
        input->resetJustPressedFlags();
        
        // 帧率限制：计算这一帧用了多少时间，如果少于目标时间则睡眠剩余时间
        auto frameEndTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float>(frameEndTime - frameStartTime).count();
        
        if (frameTime < targetFrameTime) {
            float sleepTime = targetFrameTime - frameTime;
            // 转换为微秒
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(sleepTime * 1000000)));
        }
    }
    
    vkDeviceWaitIdle(vulkanDevice->getDevice());
}

void Renderer::updateGameLogic(float deltaTime) {
    // 更新摄像机
    float speed = input->isSprintPressed() ? 10.0f : 5.0f;
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
    
    // 更新物理
    physics->update(deltaTime);
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
    
    // 禁用地板渲染
    // floorRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
    //                      camera->getViewMatrix(), camera->getProjectionMatrix());
    
    // 更新光源 uniform buffer
    updateLightUniformBuffer();
    
    // 绑定描述符集（纹理 + 光源）
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           graphicsPipeline->getPipelineLayout(), 0, 1, &textureDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           graphicsPipeline->getPipelineLayout(), 1, 1, &lightDescriptorSet, 0, nullptr);
    
    // 渲染立方体
    cubeRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                        camera->getViewMatrix(), camera->getProjectionMatrix());
    
    // 渲染 OBJ 模型
    if (modelRenderer) {
        modelRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                            camera->getViewMatrix(), camera->getProjectionMatrix());
    }
    
    // 渲染 GLTF 模型（如果有）
    if (gltfModel && gltfModel->getMeshCount() > 0) {
        gltfModel->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                        camera->getViewMatrix(), camera->getProjectionMatrix(),
                        gltfModel->getModelMatrix());
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
    
    if (vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, syncObjects->getInFlightFences()[currentFrame]) != VK_SUCCESS) {
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
    
    // 重新创建渲染通道（使用MSAA样本数）
    renderPass->cleanup();
    renderPass->create();
    
    // 重新创建MSAA颜色资源
    if (msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
        createColorResources();
    }
    
    graphicsPipeline->cleanup();
    graphicsPipeline->create();
    
    skyboxPipeline->cleanup();
    skyboxPipeline->create();
    
    framebuffers->recreate(swapchain->getImageViews(), swapchain->getExtent(), colorImageView);
    
    commandBuffers->cleanup();
    commandBuffers->create(swapchain->getImageViews().size());
    
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
    
    cubeRenderer.reset();
    modelRenderer.reset();
    skyboxRenderer.reset();
    floorRenderer.reset();
    physics.reset();
    input.reset();
    camera.reset();
    
    // 清理新系统
    gltfModel.reset();
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
    
    // 纹理描述符
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    
    // 光源 uniform buffer
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;  // 2 个描述符集（纹理 + 光源）

    if (vkCreateDescriptorPool(vulkanDevice->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void Renderer::createDescriptorSets() {
    // 1. 创建纹理描述符集
    VkDescriptorSetAllocateInfo textureAllocInfo{};
    textureAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    textureAllocInfo.descriptorPool = descriptorPool;
    textureAllocInfo.descriptorSetCount = 1;
    textureAllocInfo.pSetLayouts = &textureDescriptorSetLayout;

    if (vkAllocateDescriptorSets(vulkanDevice->getDevice(), &textureAllocInfo, &textureDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate texture descriptor set!");
    }

    Logger::info("Texture descriptor set allocated successfully");
    
    // 使用 GLTF 模型的纹理更新描述符集
    if (gltfModel && gltfModel->getMeshCount() > 0) {
        std::shared_ptr<Texture> gltfTexture = gltfModel->getFirstTexture();
        if (gltfTexture) {
            Logger::info("Updating texture descriptor set with GLTF texture");
            
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = gltfTexture->getImageView();
            imageInfo.sampler = gltfTexture->getSampler();

            VkWriteDescriptorSet textureDescriptorWrite{};
            textureDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            textureDescriptorWrite.dstSet = textureDescriptorSet;
            textureDescriptorWrite.dstBinding = 0;
            textureDescriptorWrite.dstArrayElement = 0;
            textureDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureDescriptorWrite.descriptorCount = 1;
            textureDescriptorWrite.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(vulkanDevice->getDevice(), 1, &textureDescriptorWrite, 0, nullptr);
            Logger::info("Texture descriptor set updated successfully");
        } else {
            Logger::warning("GLTF model has no textures, using default");
        }
    } else {
        Logger::warning("No GLTF model loaded, using default texture");
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

void Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto renderer = reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window));
    renderer->framebufferResized = true;
}

} // namespace vgame