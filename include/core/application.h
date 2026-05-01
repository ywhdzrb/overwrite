#ifndef APPLICATION_H
#define APPLICATION_H

#include "core/render_engine.h"
#include "core/scene_manager.h"
#include "core/scene_config.h"
#include "core/input.h"
#include "renderer/imgui_manager.h"
#include <memory>
#include <chrono>
#include <string>

namespace owengine {

namespace ecs {
class ClientWorld;
}

/**
 * @brief 应用程序配置
 */
struct ApplicationConfig {
    RenderEngineConfig render;
    std::string sceneConfigPath = "assets/models/models.json";
    bool useECS = true;
    bool enableDeveloperMode = true;
    float targetFPS = 60.0f;
};

/**
 * @brief 应用程序主类
 * 
 * 协调所有子系统：
 * - RenderEngine（渲染）
 * - SceneManager（场景）
 * - Input（输入）
 * - ImGuiManager（UI）
 * - ECS（实体组件系统）
 */
class Application {
public:
    Application();
    explicit Application(const ApplicationConfig& config);
    ~Application();
    
    // 禁止拷贝
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    
    // ========== 生命周期 ==========
    
    /**
     * @brief 初始化应用程序
     */
    void initialize();
    
    /**
     * @brief 运行主循环
     */
    void run();
    
    /**
     * @brief 关闭应用程序
     */
    void shutdown();
    
    // ========== 子系统访问 ==========
    
    RenderEngine* getRenderEngine() { return renderEngine_.get(); }
    SceneManager* getSceneManager() { return sceneManager_.get(); }
    Input* getInput() { return input_.get(); }
    ImGuiManager* getImGuiManager() { return imguiManager_.get(); }
    ecs::ClientWorld* getECSWorld() { return ecsWorld_.get(); }
    
    // ========== 配置 ==========
    
    /**
     * @brief 获取配置
     */
    const ApplicationConfig& getConfig() const { return config_; }
    
    /**
     * @brief 重新加载场景
     */
    void reloadScene();
    

    void setWireframeMode(bool enabled) { wireframeMode_ = enabled; }
    bool isWireframeMode() const { return wireframeMode_; }
    
    void setPaused(bool paused) { paused_ = paused; }
    bool isPaused() const { return paused_; }
    
    void setTimeScale(float scale) { timeScale_ = scale; }
    float getTimeScale() const { return timeScale_; }

private:
    void initWindow();
    void initVulkan();
    void initScene();
    void initECS();
    void initImGui();
    
    void mainLoop();
    void update(float deltaTime);
    void render();
    
    void processInput();
    void updateGameLogic(float deltaTime);
    void renderDeveloperPanel();
    
    // 配置
    ApplicationConfig config_;
    
    // 窗口
    GLFWwindow* window_ = nullptr;
    
    // 核心子系统
    std::unique_ptr<RenderEngine> renderEngine_;
    std::unique_ptr<SceneManager> sceneManager_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<ImGuiManager> imguiManager_;
    
    // ECS
    std::unique_ptr<ecs::ClientWorld> ecsWorld_;
    
    // 时间管理
    std::chrono::high_resolution_clock::time_point lastTime_;
    float currentFPS_ = 0.0f;
    float frameTime_ = 0.0f;
    
    // 开发者状态
    bool wireframeMode_ = false;
    bool paused_ = false;
    float timeScale_ = 1.0f;
    
    // 运行状态
    bool running_ = false;
    bool initialized_ = false;
};

} // namespace owengine

#endif // APPLICATION_H
