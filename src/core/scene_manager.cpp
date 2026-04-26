#include "core/scene_manager.h"
#include "core/camera.h"
#include "core/physics.h"
#include "core/vulkan_device.h"
#include "renderer/gltf_model.h"
#include "utils/logger.h"
#include <algorithm>

namespace owengine {

SceneManager::SceneManager(std::shared_ptr<VulkanDevice> device)
    : device_(device)
    , resourceManager_(std::make_unique<ResourceManager>(device))
    , lightManager_(std::make_unique<LightManager>()) {
}

SceneManager::~SceneManager() {
    cleanup();
}

void SceneManager::initialize() {
    Logger::info("SceneManager 初始化");
}

void SceneManager::cleanup() {
    cleanupRenderers();
    nodes_.clear();
    resourceManager_.reset();
    lightManager_.reset();
    camera_.reset();
    physics_.reset();
    Logger::info("SceneManager 已清理");
}

// ========== 渲染器管理 ==========

void SceneManager::registerRenderer(RendererType type, std::unique_ptr<IRenderer> renderer) {
    renderer->setRenderOrder(static_cast<int>(type));
    renderers_[type] = std::move(renderer);
    Logger::info("注册渲染器: type=" + std::to_string(static_cast<int>(type)));
}

IRenderer* SceneManager::getRenderer(RendererType type) const {
    auto it = renderers_.find(type);
    if (it != renderers_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void SceneManager::initializeRenderers() {
    for (auto& [type, renderer] : renderers_) {
        if (renderer && !renderer->isCreated()) {
            renderer->create();
        }
    }
    Logger::info("所有渲染器已初始化");
}

void SceneManager::cleanupRenderers() {
    for (auto& [type, renderer] : renderers_) {
        if (renderer && renderer->isCreated()) {
            renderer->cleanup();
        }
    }
    renderers_.clear();
    Logger::info("所有渲染器已清理");
}

std::vector<IRenderer*> SceneManager::getRenderers() const {
    std::vector<IRenderer*> result;
    for (const auto& [type, renderer] : renderers_) {
        if (renderer && renderer->isEnabled()) {
            result.push_back(renderer.get());
        }
    }
    // 按渲染顺序排序
    std::sort(result.begin(), result.end(), 
              [](const IRenderer* a, const IRenderer* b) {
                  return a->getRenderOrder() < b->getRenderOrder();
              });
    return result;
}

// ========== 场景节点管理 ==========

SceneNode* SceneManager::createNode(const std::string& id) {
    if (nodes_.find(id) != nodes_.end()) {
        Logger::warning("场景节点已存在: " + id);
        return nodes_[id].get();
    }
    
    auto node = std::make_unique<SceneNode>();
    node->id = id;
    nodes_[id] = std::move(node);
    
    return nodes_[id].get();
}

SceneNode* SceneManager::getNode(const std::string& id) {
    auto it = nodes_.find(id);
    if (it != nodes_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void SceneManager::removeNode(const std::string& id) {
    nodes_.erase(id);
}

std::vector<SceneNode*> SceneManager::getVisibleNodes() const {
    std::vector<SceneNode*> result;
    for (const auto& [id, node] : nodes_) {
        if (node && node->visible) {
            result.push_back(node.get());
        }
    }
    return result;
}

// ========== 光源管理 ==========

void SceneManager::loadLightsFromConfig(const std::vector<LightConfig>& configs) {
    for (const auto& config : configs) {
        if (config.enabled) {
            // 将 LightConfig 转换为 Light
            LightType type = LightType::POINT;
            if (config.type == "directional") type = LightType::DIRECTIONAL;
            else if (config.type == "spot") type = LightType::SPOT;
            
            Light light(config.name, type);
            light.setPosition(config.position);
            light.setDirection(config.direction);
            light.setColor(config.color);
            light.setIntensity(config.intensity);
            light.setConstant(config.constant);
            light.setLinear(config.linear);
            light.setQuadratic(config.quadratic);
            light.setInnerCutoff(config.innerCutoff);
            light.setOuterCutoff(config.outerCutoff);
            light.setShadowEnabled(config.shadowEnabled);
            light.setShadowIntensity(config.shadowIntensity);
            light.setEnabled(true);
            
            lightManager_->addLight(light);
        }
    }
    Logger::info("加载光源数量: " + std::to_string(configs.size()));
}

// ========== 相机管理 ==========

void SceneManager::setCamera(std::unique_ptr<Camera> camera) {
    camera_ = std::move(camera);
}

// ========== 场景加载 ==========

void SceneManager::loadFromConfig(const SceneConfig& config) {
    // 清理现有场景
    nodes_.clear();
    
    // 加载光源
    loadLightsFromConfig(config.lights);
    
    // 加载模型
    for (const auto& modelConfig : config.models) {
        if (!modelConfig.enabled) continue;
        
        // 加载模型资源
        auto handle = resourceManager_->loadModel(modelConfig.id, modelConfig.file);
        if (!handle.valid()) {
            Logger::warning("模型加载失败: " + modelConfig.id);
            continue;
        }
        
        // 创建场景节点
        SceneNode* node = createNode(modelConfig.id);
        node->modelId = modelConfig.id;
        node->position = modelConfig.position;
        node->rotation = modelConfig.rotation;
        node->scale = modelConfig.scale;
        node->playAnimation = modelConfig.playAnimation;
        node->animationIndex = modelConfig.animationIndex;
    }
    
    Logger::info("场景加载完成: " + std::to_string(nodes_.size()) + " 个节点");
}

void SceneManager::reloadConfig(const std::string& configPath) {
    SceneConfig config = SceneConfigLoader::load(configPath);
    loadFromConfig(config);
}

} // namespace owengine
