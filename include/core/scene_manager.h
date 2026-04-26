#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include "core/i_renderer.h"
#include "core/scene_config.h"
#include "core/resource_manager.h"
#include "renderer/light_manager.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

namespace owengine {

class Camera;
class Physics;
class VulkanDevice;

/**
 * @brief 场景节点
 * 
 * 表示场景中的一个可渲染对象。
 */
struct SceneNode {
    std::string id;
    std::string modelId;              // 关联的模型 ID
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};         // 欧拉角（度）
    glm::vec3 scale{1.0f};
    bool visible = true;
    bool castShadow = true;
    bool receiveShadow = true;
    
    // 动画相关
    bool playAnimation = false;
    int animationIndex = 0;
    float animationTime = 0.0f;
};

/**
 * @brief 场景管理器
 * 
 * 负责管理：
 * - 场景节点（实体）
 * - 渲染器注册和调度
 * - 光源
 * - 相机
 * - 物理系统
 */
class SceneManager {
public:
    explicit SceneManager(std::shared_ptr<VulkanDevice> device);
    ~SceneManager();
    
    // 禁止拷贝
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    
    // ========== 初始化 ==========
    
    void initialize();
    void cleanup();
    
    // ========== 渲染器管理 ==========
    
    /**
     * @brief 注册渲染器
     * @param type 渲染器类型
     * @param renderer 渲染器实例
     */
    void registerRenderer(RendererType type, std::unique_ptr<IRenderer> renderer);
    
    /**
     * @brief 获取渲染器
     */
    IRenderer* getRenderer(RendererType type) const;
    
    /**
     * @brief 初始化所有渲染器
     */
    void initializeRenderers();
    
    /**
     * @brief 清理所有渲染器
     */
    void cleanupRenderers();
    
    /**
     * @brief 获取所有渲染器（按渲染顺序排序）
     */
    std::vector<IRenderer*> getRenderers() const;
    
    // ========== 场景节点管理 ==========
    
    /**
     * @brief 创建场景节点
     */
    SceneNode* createNode(const std::string& id);
    
    /**
     * @brief 获取场景节点
     */
    SceneNode* getNode(const std::string& id);
    
    /**
     * @brief 删除场景节点
     */
    void removeNode(const std::string& id);
    
    /**
     * @brief 获取所有可见节点
     */
    std::vector<SceneNode*> getVisibleNodes() const;
    
    // ========== 光源管理 ==========
    
    /**
     * @brief 获取光源管理器
     */
    LightManager* getLightManager() { return lightManager_.get(); }
    
    /**
     * @brief 从配置加载光源
     */
    void loadLightsFromConfig(const std::vector<LightConfig>& configs);
    
    // ========== 相机管理 ==========
    
    /**
     * @brief 设置主相机
     */
    void setCamera(std::unique_ptr<Camera> camera);
    
    /**
     * @brief 获取主相机
     */
    Camera* getCamera() { return camera_.get(); }
    
    // ========== 物理管理 ==========
    
    /**
     * @brief 获取物理系统
     */
    Physics* getPhysics() { return physics_.get(); }
    
    // ========== 资源管理 ==========
    
    /**
     * @brief 获取资源管理器
     */
    ResourceManager* getResourceManager() { return resourceManager_.get(); }
    
    // ========== 场景加载 ==========
    
    /**
     * @brief 从配置加载场景
     */
    void loadFromConfig(const SceneConfig& config);
    
    /**
     * @brief 重新加载场景配置
     */
    void reloadConfig(const std::string& configPath);

private:
    std::shared_ptr<VulkanDevice> device_;
    
    // 渲染器
    std::unordered_map<RendererType, std::unique_ptr<IRenderer>> renderers_;
    
    // 场景节点
    std::unordered_map<std::string, std::unique_ptr<SceneNode>> nodes_;
    
    // 子系统
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Physics> physics_;
    std::unique_ptr<LightManager> lightManager_;
    std::unique_ptr<ResourceManager> resourceManager_;
};

} // namespace owengine

#endif // SCENE_MANAGER_H
