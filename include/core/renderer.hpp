#pragma once

/**
 * @file renderer.hpp
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

#include "core/vulkan_instance.hpp"
#include "core/vulkan_device.hpp"
#include "core/vulkan_swapchain.hpp"
#include "core/vulkan_render_pass.hpp"
#include "core/vulkan_pipeline.hpp"
#include "core/vulkan_framebuffer.hpp"
#include "core/vulkan_command_buffer.hpp"
#include "core/vulkan_sync.hpp"
#include "renderer/imgui_manager.hpp"
#include "renderer/terrain_renderer.hpp"
#include "renderer/skybox_renderer.hpp"
#include "renderer/model_renderer.hpp"
#include "renderer/texture_loader.hpp"
#include "renderer/light_manager.hpp"
#include "renderer/gltf_model.hpp"
#include "core/scene_config.hpp"
#include "core/resource_manager.hpp"
#include "core/game_config.hpp"
#include "renderer/fsr1_pass.hpp"
#include "renderer/cloud_system.hpp"
#include "renderer/shader_manager.hpp"
#include "renderer/water_renderer.hpp"

// 前向声明：游戏会话（Renderer 不拥有游戏逻辑，仅通过指针读取渲染所需数据）
namespace owengine { class GameSession; }

namespace owengine {

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
constexpr float MODEL_CULLING_DISTANCE = 250.0f;   // 静态模型视锥剔除最大距离

/**
 * @brief 半分辨率云合成管线资源
 *
 * 封装云合成渲染通道/帧缓冲/管线/描述符等 7 个 Vulkan 句柄，
 * 替代 renderer 中 6 个分散裸成员变量，便于批量创建/清理。
 */
struct CloudCompositeResources {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;
    VkDescriptorPool dsPool = VK_NULL_HANDLE;
    VkDescriptorSet ds = VK_NULL_HANDLE;
};

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

    void run(bool skipInit = false);

    /** @brief 设置外部 GameSession（如已设置，initVulkan 将跳过内部创建） */
    void setGameSession(GameSession* gs) noexcept { externalGameSession_ = gs; }

    // ========== 生命周期分阶段接口（供 LifecycleManager 使用） ==========

    /** @brief 初始化窗口（GLFW），第1阶段 */
    void initWindow();

    /** @brief 初始化 Vulkan 核心和所有渲染子系统，第2阶段 */
    void initVulkan();

    /** @brief 清理所有资源，按依赖逆序 */
    void cleanup();

    /** @brief 获取 GLFW 窗口指针 */
    GLFWwindow* getWindow() const { return window_; }

private:
    void mainLoop();
    void drawFrame();
    void recreateSwapchain();

    void createDescriptorSetLayouts();
    void createDescriptorPool(uint32_t maxSets, uint32_t descriptorCount);
    void createDescriptorSets();
    void updateLightUniformBuffer();
    void createColorResources();
    void cleanupColorResources();
    void setMsaaSamples(VkSampleCountFlagBits samples);

    // ========== 半分辨率云合成 ==========
    void createCloudCompositeResources();
    void cleanupCloudCompositeResources();

    // ========== drawFrame 阶段方法 ==========
    /** @brief 等待 fence → 获取交换链图像 → 重置 fence → 开始命令缓冲，返回 false 跳过帧 */
    [[nodiscard]] bool beginFrame(uint32_t& imageIndex, VkCommandBuffer& commandBuffer);
    /** @brief 从光源视角渲染阴影贴图 */
    void recordShadowPass(VkCommandBuffer cmd, Camera* cam);
    /** @brief 更新昼夜循环太阳方向/强度 */
    void updateDayNightCycle();
    /** @brief 开始主渲染通道，绑定管线和视口，返回 Camera*（nullptr 表示跳过） */
    Camera* beginMainRenderPass(VkCommandBuffer cmd, uint32_t imageIndex);
    /** @brief 绑定描述符集 + 渲染所有不透明几何体（地形/模型/玩家/树/石/草） */
    void renderOpaqueGeometry(VkCommandBuffer cmd, Camera* cam);
    /** @brief 结束主渲染通道，处理云合成 + ImGui 渲染 */
    void renderCloudAndImGui(VkCommandBuffer cmd, Camera* cam, uint32_t imageIndex);
    /** @brief FSR1 + vkEndCommandBuffer + 提交 + 呈现 */
    void submitFrame(VkCommandBuffer cmd, uint32_t imageIndex);

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
    float fsrScale_ = 1.0f;  // FSR1 在该提交未完整接线，禁用（1.0 = 全分辨率直出）

    // ========== 帧时间（渲染侧） ==========
    double profDrawMs_ = 0.0, profFenceWaitMs_ = 0.0;

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

    // 水下草地
    std::shared_ptr<Texture> grassWaterTexture_;
    VkDescriptorSet grassWaterDescriptorSet_ = VK_NULL_HANDLE;

    std::unique_ptr<LightManager> lightManager_;

    // 动态加载的静态模型（不含玩家模型，玩家模型由 GameSession 管理）
    std::unordered_map<std::string, std::unique_ptr<GLTFModel>> models_;
    std::unordered_map<std::string, VkDescriptorSet> modelDescriptorSets_;

    // 树/石/草/FSR1 系统（渲染层拥有，GameSession 通过指针调用 update）
    std::unique_ptr<class TreeSystem> treeSystem_;
    std::unique_ptr<class StoneSystem> stoneSystem_;
    std::unique_ptr<class GrassSystem> grassSystem_;
    std::unique_ptr<class Fsr1Pass> fsr1Pass_;

    // 着色器管理器
    std::unique_ptr<ShaderManager> shaderManager_;

    // 体积云系统（在所有不透明物体之后、ImGui之前渲染）
    std::unique_ptr<class CloudSystem> cloudSystem_;

    // 水面渲染器（在地形之后、半透明物体之前渲染）
    std::unique_ptr<WaterRenderer> waterRenderer_;

    // 阴影映射系统已整合至 LightManager，由光照系统统一管理

    // 半分辨率云合成管线资源（封装为结构体，替代 7 个分散裸句柄）
    CloudCompositeResources cloudComposite_;

    // ========== 游戏会话 ==========
    std::unique_ptr<GameSession> ownedGameSession_;  // 内部创建时持有
    GameSession* gameSession_ = nullptr;             // 始终指向活跃会话
    GameSession* externalGameSession_ = nullptr;     // 外部注入（覆盖内部创建）

    // ========== 累计时间 ==========
    float totalTime_ = 0.0f;       // 全局时间（风场动画等）
    float dayTime_ = 20.0f;         // 昼夜循环时间（秒，120 秒一个完整周期）20s=太阳较高便于观察阴影
    float dayCyclePeriod_ = 120.0f; // 昼夜完整周期秒数

    // ========== 游戏配置（供 drawFrame 等访问） ==========
    GameConfig gameConfig_;

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

    // ========== 输入状态跟踪（原静态局部变量） ==========
    // 背包打开/关闭首帧检测，用于在 ImGui NewFrame 前同步鼠标位置
    bool prevInvOpen_ = false;

    // ========== 云 ImGui 调试面板 ==========
    bool showCloudDebug_ = false;

    // ========== 防重复清理标志 ==========
    bool cleanedUp_ = false;

    // ========== 描述符资源 ==========
    VkDescriptorSetLayout textureDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescriptorSetLayout_ = VK_NULL_HANDLE;
    // shadowDescriptorSetLayout_ 已整合至 LightManager，通过 getShadowDescriptorSetLayout() 获取
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
