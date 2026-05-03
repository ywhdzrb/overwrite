#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <glm/glm.hpp>

#include "core/resource_node_system.h"

namespace owengine {

// 前向声明
class Camera;
class Input;
class VulkanDevice;
class TerrainRenderer;
class TextureLoader;
class GLTFModel;
class GrassSystem;

namespace ecs {
class ClientWorld;
class IGameWorld;
} // namespace ecs

// 远程玩家模型（持有两个 GLTF 模型用于空闲/行走切换）
struct RemotePlayerModels {
    std::unique_ptr<GLTFModel> idleModel;
    std::unique_ptr<GLTFModel> walkModel;
    bool wasMoving = false;
};

// 初始化参数结构体（从 Renderer 传入）
struct GameSessionInitParams {
    GLFWwindow* window = nullptr;
    int windowWidth = 800;
    int windowHeight = 600;

    std::shared_ptr<VulkanDevice> device;
    std::shared_ptr<TextureLoader> textureLoader;
    std::shared_ptr<TerrainRenderer> terrainRenderer;

    // 非拥有指针：树/石位置查询（GameSession 用于碰撞箱注入）
    class TreeSystem* treeSystem = nullptr;
    class StoneSystem* stoneSystem = nullptr;

    // 草丛系统（仅 update 触发，非拥有）
    class GrassSystem* grassSystem = nullptr;

    // Vulkan 描述符资源（用于玩家模型创建）
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout graphicsPipelineLayout = VK_NULL_HANDLE;

    // 地形高度查询回调
    std::function<float(float, float)> terrainHeightQuery;
};

/**
 * @brief 游戏会话管理类
 *
 * 负责所有游戏逻辑（ECS/物理/碰撞/网络/动画），与渲染层解耦。
 * 生命周期：在 Renderer 初始化 Vulkan 之后创建，在 Renderer cleanup 之前销毁。
 *
 * 线程模型：
 *   - update() 在主线程调用，内部使用 std::async 将 ECS 模拟卸到后台
 *   - getCamera()/getInput() 供 Renderer 在 drawFrame 中读取
 */
class GameSession {
public:
    GameSession();
    ~GameSession();

    // 禁止拷贝
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    /** @brief 初始化游戏会话（ECS/玩家模型/动画/网络） */
    void init(const GameSessionInitParams& params);

    /** @brief 每帧更新游戏逻辑（输入→ECS→碰撞→动画→网络） */
    void update(float deltaTime);

    /** @brief 清理资源 */
    void cleanup();

    // ========== 共享资源（供 Renderer 使用） ==========

    /** @brief 获取摄像机（Renderer 用于 view/proj 矩阵） */
    Camera* getCamera() const { return camera_.get(); }

    /** @brief 获取输入（Renderer 用于 ImGui 调试面板） */
    Input* getInput() const { return input_.get(); }

    /** @brief 获取当前活动的玩家模型 */
    GLTFModel* getActivePlayerModel() const;

    /** @brief 获取当前活动的玩家模型描述符集 */
    VkDescriptorSet getActivePlayerDescriptorSet() const;

    /** @brief 玩家是否在移动中 */
    bool isPlayerMoving() const { return playerWasMoving_; }

    /** @brief 玩家是否在飞行模式 */
    bool isPlayerFlying() const { return playerIsFlying_; }

    /** @brief 获取远程玩家模型映射 */
    const std::unordered_map<std::string, RemotePlayerModels>& getRemotePlayerModels() const {
        return remotePlayerModels_;
    }

    /** @brief 获取 ECS 客户端世界 */
    ecs::IGameWorld* getECSWorld() const;

    /** @brief 获取资源节点系统（供 ImGui 调试面板和采集系统使用） */
    ResourceNodeSystem* getResourceNodeSystem() { return &resourceNodeSystem_; }

    // ========== 游戏状态（供 ImGui 调试面板读写） ==========

    // FPS 性能
    float getCurrentFPS() const { return currentFPS_; }
    double getProfLogicMs() const { return profLogicMs_; }

    // 渲染模式（Renderer 需读取）
    bool wireframeMode = false;
    bool pauseGame = false;
    float timeScale = 1.0f;
    float userMovementSpeed = 5.0f;
    float userSensitivity = 0.1f;

    // 网络连接
    char serverHost[64] = "localhost";
    int serverPort = 9002;
    bool connectRequested = false;
    bool disconnectRequested = false;

    // MSAA 延迟更改（Render 侧需读取）
    bool pendingMsaaChange = false;
    VkSampleCountFlagBits pendingMsaaSamples = VK_SAMPLE_COUNT_1_BIT;

private:
    void loadPlayerModels();
    void updateRemotePlayers(float deltaTime);
    void injectCollisionBoxes(const glm::vec3& playerPos);
    void handleNetworkRequests();

    /** @brief 为单网格 GLTF 模型创建整体纹理描述符集 */
    VkDescriptorSet createModelDescriptorSet(class GLTFModel* model, VkDescriptorPool pool,
                                             VkDescriptorSetLayout layout, const std::string& name);

    // === 拥有所有权 ===
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<ecs::ClientWorld> ecsClientWorld_;

    // 玩家模型
    std::unique_ptr<GLTFModel> gltfModel_;
    std::unique_ptr<GLTFModel> gltfWalkModel_;
    VkDescriptorSet gltfModelDescriptorSet_ = VK_NULL_HANDLE;
    VkDescriptorSet gltfWalkModelDescriptorSet_ = VK_NULL_HANDLE;
    bool playerWasMoving_ = false;
    bool playerIsFlying_ = false;

    // 远程玩家模型
    std::unordered_map<std::string, RemotePlayerModels> remotePlayerModels_;

    // === 非拥有引用 ===
    GLFWwindow* window_ = nullptr;
    int windowWidth_ = 800;
    int windowHeight_ = 600;
    std::shared_ptr<VulkanDevice> device_;
    std::shared_ptr<TextureLoader> textureLoader_;
    std::shared_ptr<TerrainRenderer> terrainRenderer_;
    class TreeSystem* treeSystem_ = nullptr;
    class StoneSystem* stoneSystem_ = nullptr;
    class GrassSystem* grassSystem_ = nullptr;
    ResourceNodeSystem resourceNodeSystem_;

    // Vulkan 资源（用于模型描述符集创建）
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout graphicsPipelineLayout_ = VK_NULL_HANDLE;

    // 地形高度查询回调
    std::function<float(float, float)> terrainHeightQuery_;

    // 时间与性能
    std::chrono::high_resolution_clock::time_point lastTime_;
    float currentFPS_ = 0.0f;
    double profLogicMs_ = 0.0;

    // 背包状态
    bool inventoryOpen_ = false;

public:
    /** @brief 背包是否打开（供 Renderer 绘制 ImGui 窗口） */
    bool isInventoryOpen() const { return inventoryOpen_; }

    /** @brief 设置背包打开/关闭状态 */
    void setInventoryOpen(bool open) { inventoryOpen_ = open; }

    // ========== 采集交互 ==========

    /**
     * @brief 玩家准星指向的可采集目标信息
     */
    struct HarvestTarget {
        bool valid = false;
        ecs::ResourceType type;
        float distance = 0.0f;
        glm::vec3 position{0.0f};
    };

    /** @brief 获取当前准星指向的可采集目标 */
    const HarvestTarget& getHarvestTarget() const { return harvestTarget_; }

private:
    void updateHarvestTarget();

    HarvestTarget harvestTarget_;
};

} // namespace owengine
