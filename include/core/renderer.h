#pragma once

/**
 * @file renderer.h
 * @brief 纯渲染编排器 — Vulkan 管线生命周期、帧缓冲、命令缓冲录制、渲染子系统调度
 *
 * 归属模块：renderer
 * 核心职责：统一调度 skybox/terrain/model/树/石/草/FSR1 等子渲染器
 * 依赖关系：VulkanDevice、VulkanSwapchain、VulkanRenderPass、VulkanPipeline 等核心封装
 *           以及 TerrainRenderer、SkyboxRenderer、ModelRenderer、ImGuiManager 等子渲染器
 * 关键设计：游戏逻辑（ECS/物理/碰撞/网络/玩家动画）已提取至 GameSession，
 *           Renderer 通过 gameSession_ 指针只读访问摄像机/输入/玩家模型
 *           FSR1 上采样管线在 fsrScale_ < 1.0 时替代 MSAA
 */

#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

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
constexpr float MODEL_CULLING_DISTANCE = 250.0f;   // 静态模型视锥剔除最大距离

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
    void setGameSession(GameSession* gs) noexcept { externalGameSession_ = gs; }

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
    int windowWidth_;
    int windowHeight_;
    std::string windowTitle_;
    GLFWwindow* window_;

    // ========== MSAA ==========
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits maxMsaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage colorImage_ = VK_NULL_HANDLE;
    VmaAllocation colorImageAllocation_ = VK_NULL_HANDLE;
    VkImageView colorImageView_ = VK_NULL_HANDLE;
    VkFormat colorFormat_;
    float fsrScale_ = 0.67f;

    // ========== 帧时间（渲染侧） ==========
    double profDrawMs_ = 0.0, profGPUMs_ = 0.0;

    // ========== Vulkan 核心 ==========
    std::shared_ptr<VulkanInstance> vulkanInstance_;
    std::shared_ptr<VulkanDevice> vulkanDevice_;
    std::shared_ptr<VulkanSwapchain> swapchain_;
    std::shared_ptr<VulkanRenderPass> renderPass_;
    std::shared_ptr<VulkanPipeline> graphicsPipeline_;
    std::shared_ptr<VulkanFramebuffer> framebuffers_;
    std::shared_ptr<VulkanCommandBuffer> commandBuffers_;
    std::shared_ptr<VulkanSync> syncObjects_;
    std::shared_ptr<VulkanPipeline> skyboxPipeline_;

    // ========== 渲染子系统 ==========
    std::shared_ptr<TerrainRenderer> terrainRenderer_;
    std::unique_ptr<SkyboxRenderer> skyboxRenderer_;
    std::unique_ptr<ModelRenderer> modelRenderer_;
    std::unique_ptr<ImGuiManager> imguiManager_;
    std::shared_ptr<TextureLoader> textureLoader_;
    std::unique_ptr<LightManager> lightManager_;

    // 动态加载的静态模型（不含玩家模型，玩家模型由 GameSession 管理）
    std::unordered_map<std::string, std::unique_ptr<GLTFModel>> models_;
    std::unordered_map<std::string, VkDescriptorSet> modelDescriptorSets_;

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
    std::chrono::high_resolution_clock::time_point lastTime_;
    float frameTime_ = 0.0f;
    float minFrameTime_ = 999.0f;
    float maxFrameTime_ = 0.0f;
    float targetFPS_ = 60.0f;

    // ========== MSAA 延迟更改 ==========
    bool pendingMsaaChange_ = false;
    VkSampleCountFlagBits pendingMsaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

    // ========== 帧索引 ==========
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;

    // ========== 描述符资源 ==========
    VkDescriptorSetLayout textureDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::mutex descriptorPoolMutex_;
    VkDescriptorSet textureDescriptorSet_ = VK_NULL_HANDLE;
    VkDescriptorSet lightDescriptorSet_ = VK_NULL_HANDLE;
    VkBuffer lightUniformBuffer_ = VK_NULL_HANDLE;
    VmaAllocation lightUniformBufferAllocation_ = VK_NULL_HANDLE;
    void* lightUniformBufferMapped_ = nullptr;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace owengine
