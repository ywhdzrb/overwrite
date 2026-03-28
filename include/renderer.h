#ifndef RENDERER_H
#define RENDERER_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>

#include "vulkan_instance.h"
#include "vulkan_device.h"
#include "vulkan_swapchain.h"
#include "vulkan_render_pass.h"
#include "vulkan_pipeline.h"
#include "vulkan_framebuffer.h"
#include "vulkan_command_buffer.h"
#include "vulkan_sync.h"
#include "camera.h"
#include "input.h"
#include "physics.h"
#include "floor_renderer.h"

namespace vgame {

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

class Renderer {
public:
    Renderer(int width, int height, const std::string& title);
    ~Renderer();

    // 禁止拷贝
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void run();

private:
    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();
    void drawFrame();
    void recreateSwapchain();
    void updateGameLogic(float deltaTime);

    int windowWidth;
    int windowHeight;
    std::string windowTitle;
    GLFWwindow* window;

    std::shared_ptr<VulkanInstance> vulkanInstance;
    std::shared_ptr<VulkanDevice> vulkanDevice;
    std::shared_ptr<VulkanSwapchain> swapchain;
    std::shared_ptr<VulkanRenderPass> renderPass;
    std::shared_ptr<VulkanPipeline> graphicsPipeline;
    std::shared_ptr<VulkanFramebuffer> framebuffers;
    std::shared_ptr<VulkanCommandBuffer> commandBuffers;
    std::shared_ptr<VulkanSync> syncObjects;

    // 游戏逻辑组件
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Input> input;
    std::unique_ptr<Physics> physics;
    std::unique_ptr<FloorRenderer> floorRenderer;

    // 时间管理
    std::chrono::high_resolution_clock::time_point lastTime;

    uint32_t currentFrame = 0;
    bool framebufferResized = false;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace vgame

#endif // RENDERER_H