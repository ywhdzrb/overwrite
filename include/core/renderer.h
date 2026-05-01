#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <queue>
#include <mutex>

#include "core/vulkan_instance.h"
#include "core/vulkan_device.h"
#include "core/vulkan_swapchain.h"
#include "core/vulkan_render_pass.h"
#include "core/vulkan_pipeline.h"
#include "core/vulkan_framebuffer.h"
#include "core/vulkan_command_buffer.h"
#include "core/vulkan_sync.h"
#include "renderer/imgui_manager.h"
#include "renderer/terrain_renderer.h"
#include "renderer/skybox_renderer.h"
#include "renderer/model_renderer.h"
#include "renderer/texture_loader.h"
#include "renderer/light_manager.h"
#include "renderer/gltf_model.h"
#include "core/scene_config.h"
#include "core/resource_manager.h"
#include "renderer/fsr1_pass.h"

// 前向声明：游戏会话（Renderer 不拥有游戏逻辑，仅通过指针读取渲染所需数据）
namespace owengine { class GameSession; }

namespace owengine {

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

/**
 * @brief 纯渲染编排器
 *
 * 职责范围：Vulkan 管线生命周期、帧缓冲、命令缓冲录制、渲染子系统调度。
 * 游戏逻辑（ECS/物理/碰撞/网络/玩家动画）由 GameSession 管理。
 * Renderer 通过 gameSession_ 指针只读访问摄像机、输入、玩家模型等渲染所需数据。
 */
class Renderer {
public:
    Renderer(int width, int height, const std::string& title);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void run();

    /** @brief 设置外部 GameSession（如已设置，initVulkan 将跳过内部创建） */
    void setGameSession(GameSession* gs) { externalGameSession_ = gs; }

private:
    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();
    void drawFrame();
    void recreateSwapchain();

    void createDescriptorSetLayouts();
    void createDescriptorPool(uint32_t maxSets, uint32_t descriptorCount);
    void createDescriptorSets();
    void updateLightUniformBuffer();
    void createColorResources();
    void cleanupColorResources();
    void setMsaaSamples(VkSampleCountFlagBits samples);

    std::vector<ModelConfig> loadModelConfig(const std::string& configFile);
    void loadModelsFromConfig(const std::vector<ModelConfig>& configs);
    VkDescriptorSet createModelDescriptorSet(GLTFModel* model, const std::string& modelId, VkDescriptorPool pool = VK_NULL_HANDLE);
    SceneConfig loadSceneConfig(const std::string& configFile);
    void loadLightsFromConfig(const SceneConfig& config);
    void reloadSceneConfig();

    // ========== 窗口 ==========
    int windowWidth;
    int windowHeight;
    std::string windowTitle;
    GLFWwindow* window;

    // ========== MSAA ==========
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits maxMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory = VK_NULL_HANDLE;
    VkImageView colorImageView = VK_NULL_HANDLE;
    VkFormat colorFormat;
    float fsrScale_ = 0.67f;

    // ========== 帧时间（渲染侧） ==========
    double profDrawMs_ = 0.0, profGPUMs_ = 0.0;

    // ========== Vulkan 核心 ==========
    std::shared_ptr<VulkanInstance> vulkanInstance;
    std::shared_ptr<VulkanDevice> vulkanDevice;
    std::shared_ptr<VulkanSwapchain> swapchain;
    std::shared_ptr<VulkanRenderPass> renderPass;
    std::shared_ptr<VulkanPipeline> graphicsPipeline;
    std::shared_ptr<VulkanFramebuffer> framebuffers;
    std::shared_ptr<VulkanCommandBuffer> commandBuffers;
    std::shared_ptr<VulkanSync> syncObjects;
    std::shared_ptr<VulkanPipeline> skyboxPipeline;

    // ========== 渲染子系统 ==========
    std::shared_ptr<TerrainRenderer> terrainRenderer;
    std::unique_ptr<SkyboxRenderer> skyboxRenderer;
    std::unique_ptr<ModelRenderer> modelRenderer;
    std::unique_ptr<ImGuiManager> imguiManager;
    std::shared_ptr<TextureLoader> textureLoader;
    std::unique_ptr<LightManager> lightManager;

    // 动态加载的静态模型（不含玩家模型，玩家模型由 GameSession 管理）
    std::unordered_map<std::string, std::unique_ptr<GLTFModel>> models;
    std::unordered_map<std::string, VkDescriptorSet> modelDescriptorSets;

    // 树/石/草/FSR1 系统（渲染层拥有，GameSession 通过指针调用 update）
    std::unique_ptr<class TreeSystem> treeSystem_;
    std::unique_ptr<class StoneSystem> stoneSystem_;
    std::unique_ptr<class GrassSystem> grassSystem_;
    std::unique_ptr<class Fsr1Pass> fsr1Pass_;

    // ========== 游戏会话 ==========
    std::unique_ptr<GameSession> ownedGameSession_;  // 内部创建时持有
    GameSession* gameSession_ = nullptr;             // 始终指向活跃会话
    GameSession* externalGameSession_ = nullptr;     // 外部注入（覆盖内部创建）

    // ========== 帧率控制 ==========
    std::chrono::high_resolution_clock::time_point lastTime;
    float frameTime = 0.0f;
    float minFrameTime = 999.0f;
    float maxFrameTime = 0.0f;
    float targetFPS = 60.0f;

    // ========== MSAA 延迟更改 ==========
    bool pendingMsaaChange = false;
    VkSampleCountFlagBits pendingMsaaSamples = VK_SAMPLE_COUNT_1_BIT;

    // ========== 帧索引 ==========
    uint32_t currentFrame = 0;
    bool framebufferResized = false;

    // ========== 描述符资源 ==========
    VkDescriptorSetLayout textureDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::mutex descriptorPoolMutex_;
    VkDescriptorSet textureDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet lightDescriptorSet = VK_NULL_HANDLE;
    VkBuffer lightUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lightUniformBufferMemory = VK_NULL_HANDLE;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace owengine
