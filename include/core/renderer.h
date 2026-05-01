#ifndef RENDERER_H
#define RENDERER_H

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
#include "core/camera.h"
#include "core/input.h"
#include "renderer/imgui_manager.h"
#include "core/physics.h"
#include "renderer/terrain_renderer.h"
#include "renderer/skybox_renderer.h"
#include "renderer/model_renderer.h"
#include "renderer/texture_loader.h"
#include "renderer/light_manager.h"
#include "renderer/gltf_model.h"
#include "core/scene_config.h"
#include "core/resource_manager.h"

// ECS 系统（通过接口解耦）
#include "ecs/i_game_world.h"
#include "renderer/fsr1_pass.h"

// 前向声明（具体类型仅 cpp 中使用）
namespace owengine { namespace ecs { class ClientWorld; } }

namespace owengine {

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// 配置结构已移至 scene_config.h

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
    

    /**
     * @brief 创建描述符集布局
     */
    void createDescriptorSetLayouts();
    
    /**
     * @brief 创建描述符池
     * @param maxSets 最大描述符集数量
     * @param descriptorCount COMBINED_IMAGE_SAMPLER 类型的描述符数量
     */
    void createDescriptorPool(uint32_t maxSets, uint32_t descriptorCount);
    
    /**
     * @brief 创建描述符集
     */
    void createDescriptorSets();
    
    /**
     * @brief 更新光源 uniform buffer
     */
    void updateLightUniformBuffer();
    
    /**
     * @brief 创建多重采样颜色缓冲区
     */
    void createColorResources();
    
    /**
     * @brief 清理多重采样资源
     */
    void cleanupColorResources();
    
    /**
     * @brief 设置MSAA采样数
     */
    void setMsaaSamples(VkSampleCountFlagBits samples);
    
    // 树系统由独立类 TreeSystem 管理

    /**
     * @brief 加载模型配置文件
     */
    std::vector<ModelConfig> loadModelConfig(const std::string& configFile);
    
    /**
     * @brief 根据配置加载模型
     */
    void loadModelsFromConfig(const std::vector<ModelConfig>& configs);
    
    /**
     * @brief 为模型创建纹理描述符集
     * @param pool 可选：使用指定描述符池，默认使用全局 descriptorPool
     */
    VkDescriptorSet createModelDescriptorSet(GLTFModel* model, const std::string& modelId, VkDescriptorPool pool = VK_NULL_HANDLE);
    
    /**
     * @brief 加载场景配置文件
     */
    SceneConfig loadSceneConfig(const std::string& configFile);
    
    /**
     * @brief 根据配置加载光源
     */
    void loadLightsFromConfig(const SceneConfig& config);
    
    /**
     * @brief 重新加载场景配置
     */
    void reloadSceneConfig();

    int windowWidth;
    int windowHeight;
    std::string windowTitle;
    GLFWwindow* window;
    
    // MSAA配置
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits maxMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory = VK_NULL_HANDLE;
    VkImageView colorImageView = VK_NULL_HANDLE;
    VkFormat colorFormat;
    // FSR1 缩放比
    float fsrScale_ = 0.67f;

    // 帧时间分解
    double profLogicMs_ = 0.0, profDrawMs_ = 0.0, profGPUMs_ = 0.0;

    std::shared_ptr<VulkanInstance> vulkanInstance;
    std::shared_ptr<VulkanDevice> vulkanDevice;
    std::shared_ptr<VulkanSwapchain> swapchain;
    std::shared_ptr<VulkanRenderPass> renderPass;
    std::shared_ptr<VulkanPipeline> graphicsPipeline;
    std::shared_ptr<VulkanFramebuffer> framebuffers;
    std::shared_ptr<VulkanCommandBuffer> commandBuffers;
    std::shared_ptr<VulkanSync> syncObjects;
    std::shared_ptr<VulkanPipeline> skyboxPipeline;

    // 游戏逻辑组件
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Input> input;
    std::unique_ptr<Physics> physics;
    std::shared_ptr<TerrainRenderer> terrainRenderer;
    std::unique_ptr<SkyboxRenderer> skyboxRenderer;
    std::unique_ptr<ModelRenderer> modelRenderer;
    std::unique_ptr<ImGuiManager> imguiManager;

    // 新增系统
    std::shared_ptr<TextureLoader> textureLoader;
    std::unique_ptr<LightManager> lightManager;
    
    // 动态加载的模型
    std::unordered_map<std::string, std::unique_ptr<GLTFModel>> models;
    std::unordered_map<std::string, VkDescriptorSet> modelDescriptorSets;
    
    // 玩家专用模型（保持兼容）
    std::unique_ptr<GLTFModel> gltfModel;          // 玩家空闲
    std::unique_ptr<GLTFModel> gltfWalkModel;      // 玩家行走
    VkDescriptorSet gltfModelDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet gltfWalkModelDescriptorSet = VK_NULL_HANDLE;
    
    // 玩家动画状态
    bool playerWasMoving = false;
    bool playerIsFlying_ = false;
    
    // 远程玩家模型
    struct RemotePlayerModels {
        std::unique_ptr<GLTFModel> idleModel;
        std::unique_ptr<GLTFModel> walkModel;
        bool wasMoving = false;
    };
    std::unordered_map<std::string, RemotePlayerModels> remotePlayerModels;
    
    // ECS 系统（通过接口解耦，具体类型仅用于 dev panel 逃生口）
    bool useECS{true};
    std::unique_ptr<ecs::IGameWorld> ecsClientWorld;
    ecs::ClientWorld* rawClientWorld_ = nullptr;

    // 时间管理
    std::chrono::high_resolution_clock::time_point lastTime;
    
    // FPS 管理
    float currentFPS = 0.0f;
    float frameTime = 0.0f;
    float minFrameTime = 999.0f;
    float maxFrameTime = 0.0f;
    float targetFPS = 60.0f;
    
bool wireframeMode = false;
    bool pauseGame = false;
    float timeScale = 1.0f;
    float userMovementSpeed = 5.0f;
    float userSensitivity = 0.1f;
    
    // 延迟 MSAA 更改
    bool pendingMsaaChange = false;
    VkSampleCountFlagBits pendingMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // 网络连接
    char serverHost[64] = "localhost";
    int serverPort = 9002;
    bool connectRequested = false;
    bool disconnectRequested = false;
    
    // 按键状态
    bool jumpInput = false;
    bool freeCameraToggle = false;
    bool shiftInput = false;
    bool spaceHeld = false;
    bool escJustPressed = false;

    uint32_t currentFrame = 0;
    bool framebufferResized = false;

    // 描述符相关
    VkDescriptorSetLayout textureDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::mutex descriptorPoolMutex_;  // 描述符池线程安全锁（vkAllocateDescriptorSets 需外部同步）
    VkDescriptorSet textureDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet lightDescriptorSet = VK_NULL_HANDLE;
    
    // 光源 uniform buffer
    VkBuffer lightUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lightUniformBufferMemory = VK_NULL_HANDLE;

    // 树系统（独立类，Renderer 不直接管理树木数据）
    std::unique_ptr<class TreeSystem> treeSystem_;
    // 石头系统（独立类，与 TreeSystem 同模式）
    std::unique_ptr<class StoneSystem> stoneSystem_;
    // 草丛系统（实例化渲染，独立管线）
    std::unique_ptr<class GrassSystem> grassSystem_;
    // FSR1 上采样管线
    std::unique_ptr<class Fsr1Pass> fsr1Pass_;
    
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace owengine

#endif // RENDERER_H