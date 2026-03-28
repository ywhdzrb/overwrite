// 渲染器实现
// 负责管理整个渲染流程，包括窗口创建、Vulkan初始化和渲染循环
#include "imgui.h"
#include "renderer.h"
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
    
    renderPass = std::make_shared<VulkanRenderPass>(vulkanDevice, swapchain->getImageFormat());
    renderPass->create();
    
    graphicsPipeline = std::make_shared<VulkanPipeline>(
        vulkanDevice,
        renderPass->getRenderPass(),
        swapchain->getExtent(),
        "shaders/shader.vert.spv",
        "shaders/shader.frag.spv"
    );
    graphicsPipeline->create();
    
    // 创建深度资源
    vulkanDevice->createDepthResources(swapchain->getExtent());
    
    framebuffers = std::make_shared<VulkanFramebuffer>(vulkanDevice, renderPass->getRenderPass());
    framebuffers->create(swapchain->getImageViews(), swapchain->getExtent());
    
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
    
    // 初始化 ImGui
    imguiManager = std::make_unique<ImGuiManager>(vulkanDevice, swapchain, renderPass, window);
    imguiManager->init();
    
    // 设置立方体位置
    cubeRenderer->setPosition(glm::vec3(0.0f, 0.5f, -2.0f));
    
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
        
        // 更新输入
        input->update();
        
        // 处理鼠标移动
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
    
    // 使用 Input 类检测空格键
    bool jumpInput = input->isJumpJustPressed();
    
    camera->update(deltaTime,
                  input->isForwardPressed(),
                  input->isBackPressed(),
                  input->isLeftPressed(),
                  input->isRightPressed(),
                  jumpInput);
    
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
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // 颜色清除值：黑色
    clearValues[1].depthStencil = {1.0f, 0};  // 深度清除值：1.0（最远）
    
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
    
    // 渲染地板
    floorRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                         camera->getViewMatrix(), camera->getProjectionMatrix());
    
    // 渲染立方体
    cubeRenderer->render(commandBuffer, graphicsPipeline->getPipelineLayout(),
                        camera->getViewMatrix(), camera->getProjectionMatrix());
    
    
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
    
    // 清理并重新创建深度资源
    vulkanDevice->cleanupDepthResources();
    
    swapchain->recreate(window);
    
    // 重新创建深度资源
    vulkanDevice->createDepthResources(swapchain->getExtent());
    
    renderPass->cleanup();
    renderPass->create();
    
    graphicsPipeline->cleanup();
    graphicsPipeline->create();
    
    framebuffers->recreate(swapchain->getImageViews(), swapchain->getExtent());
    
    commandBuffers->cleanup();
    commandBuffers->create(swapchain->getImageViews().size());
    
    for (size_t i = 0; i < commandBuffers->getCommandBuffers().size(); i++) {
        commandBuffers->record(i, framebuffers->getFramebuffers()[i], swapchain->getExtent(),
                             camera->getViewMatrix(), camera->getProjectionMatrix());
    }
}

void Renderer::cleanup() {
    cubeRenderer.reset();
    floorRenderer.reset();
    physics.reset();
    input.reset();
    camera.reset();
    
    syncObjects.reset();
    commandBuffers.reset();
    framebuffers.reset();
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

void Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto renderer = reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window));
    renderer->framebufferResized = true;
}

} // namespace vgame