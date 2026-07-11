// 游戏会话实现 — 封装所有游戏逻辑（ECS/物理/碰撞/网络/动画）
//
// 与渲染层完全解耦：Renderer 不直接拥有游戏状态，
// 仅通过 getCamera()/getInput()/getActivePlayerModel() 接口读取本类维护的数据。
//
// 依赖注入：init() 接收 GameSessionInitParams，包含来自 Renderer 的共享 Vulkan 资源。
// 生命周期：init() 在 Renderer::initVulkan() 之后调用，cleanup() 在 Renderer::cleanup() 之前调用。

#include "core/game_session.hpp"
#include "core/camera.hpp"
#include "core/input.hpp"
#include "core/vulkan_device.hpp"
#include "renderer/gltf_model.hpp"
#include "renderer/texture_loader.hpp"
#include "renderer/terrain_renderer.hpp"
#include "ecs/client_systems.hpp"
#include "ecs/ecs.hpp"
#include "utils/logger.hpp"

namespace owengine {

GameSession::GameSession() = default;

GameSession::~GameSession() {
    cleanup();
}

/**
 * @brief 初始化游戏会话
 *
 * 注入依赖 → 创建摄像机/输入/ECS世界 → 加载模型 → 初始化时间基准
 */
void GameSession::init(const GameSessionInitParams& params) {
    window_ = params.window;
    windowWidth_ = params.windowWidth;
    windowHeight_ = params.windowHeight;
    device_ = params.device;
    textureLoader_ = params.textureLoader;
    terrainRenderer_ = params.terrainRenderer;
    treeSystem_ = params.treeSystem;
    stoneSystem_ = params.stoneSystem;
    grassSystem_ = params.grassSystem;
    descriptorPool_ = params.descriptorPool;
    textureDescriptorSetLayout_ = params.textureDescriptorSetLayout;
    lightDescriptorSetLayout_ = params.lightDescriptorSetLayout;
    graphicsPipelineLayout_ = params.graphicsPipelineLayout;
    terrainHeightQuery_ = params.terrainHeightQuery;

    // 初始化摄像机（固定窗口尺寸，后续由 Renderer 同步实际尺寸）
    camera_ = std::make_unique<Camera>(windowWidth_, windowHeight_);

    input_ = std::make_unique<Input>(window_);

    // 初始化 ECS 客户端世界
    ecsClientWorld_ = std::make_unique<ecs::ClientWorld>();
    ecsClientWorld_->initClientSystems(window_, windowWidth_, windowHeight_);
    ecsClientWorld_->createClientPlayer(windowWidth_, windowHeight_);
    Logger::info("[GameSession] ECS 系统初始化完成");

    // 注入地形高度查询到 ECS 物理系统
    if (terrainHeightQuery_) {
        ecsClientWorld_->setTerrainQuery(terrainHeightQuery_);
    }

    // 初始化模型缓存与 ECS 渲染系统
    modelCache_ = std::make_shared<ModelCache>(device_, textureLoader_);
    renderSystem_ = std::make_unique<RenderSystem>(
        ecsClientWorld_->registry(),
        modelCache_,
        textureDescriptorSetLayout_,
        descriptorPool_);
    Logger::info("[GameSession] ECS 渲染系统初始化完成");

    // 加载玩家模型
    loadPlayerModels();

    // 预加载熔炉模型并获取实际包围盒尺寸（用于碰撞箱）
    {
        auto* furnaceModel = modelCache_->getOrLoadModel("assets/models/furnace.glb");
        if (furnaceModel) {
            auto bbox = furnaceModel->getBoundingBox();
            glm::vec3 size = bbox.second - bbox.first;
            if (size.x > 0.01f && size.y > 0.01f && size.z > 0.01f) {
                furnaceCollisionSize_ = size;
                furnaceBboxMin_ = bbox.first;
                Logger::info("[Furnace] 模型实际尺寸: (" +
                             std::to_string(size.x) + ", " +
                             std::to_string(size.y) + ", " +
                             std::to_string(size.z) + ")");
            }
        }
    }

    // 从现有树/石系统填充资源节点
    resourceNodeSystem_.init(treeSystem_, stoneSystem_);

    // 初始化时间基准
    lastTime_ = std::chrono::high_resolution_clock::now();
}

/**
 * @brief 按依赖逆序清理所有资源
 */
void GameSession::cleanup() {
    // 先清理渲染系统（模型引用）
    if (renderSystem_) {
        renderSystem_->cleanup();
        renderSystem_.reset();
    }
    modelCache_.reset();

    gltfWalkModel_.reset();
    gltfModel_.reset();

    remotePlayerModels_.clear();
    ecsClientWorld_.reset();

    camera_.reset();
    input_.reset();

    terrainRenderer_.reset();
    treeSystem_ = nullptr;
    stoneSystem_ = nullptr;
    textureLoader_.reset();
    device_.reset();
}

} // namespace owengine
