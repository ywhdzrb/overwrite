# 渲染器模块

渲染器模块包含主渲染器和各种专用渲染器。

## 类概览

| 类名 | 文件 | 描述 |
|------|------|------|
| `Renderer` | `include/renderer.h` | 主渲染器类 |
| `FloorRenderer` | `include/floor_renderer.h` | 地板渲染器 |
| `CubeRenderer` | `include/cube_renderer.h` | 立方体渲染器 |
| `SkyboxRenderer` | `include/skybox_renderer.h` | 天空盒渲染器 |
| `ModelRenderer` | `include/model_renderer.h` | 模型渲染器 |
| `TextRenderer` | `include/text_renderer.h` | 文本渲染器 |
| `ImGuiManager` | `include/imgui_manager.h` | ImGui 管理器 |

---

## Renderer（主渲染器）

主渲染器类，协调所有渲染组件和游戏逻辑。

### 配置结构体

#### ModelConfig

```cpp
struct ModelConfig {
    std::string id;                      // 模型ID
    std::string file;                    // 文件路径
    bool enabled = true;                 // 是否启用
    glm::vec3 position{0.0f};            // 位置
    glm::vec3 rotation{0.0f};            // 旋转（欧拉角，度）
    glm::vec3 scale{1.0f};               // 缩放
    int subdivisionIterations = 0;       // 细分迭代次数
    bool playAnimation = false;          // 是否播放动画
    int animationIndex = 0;              // 动画索引
    bool playAllAnimations = false;      // 是否播放所有动画
    bool isPlayerModel = false;          // 是否是玩家模型
    bool isPlayerWalkModel = false;      // 是否是玩家行走模型
    std::string description;             // 描述
};
```

#### AmbientConfig

```cpp
struct AmbientConfig {
    glm::vec3 color{0.1f, 0.1f, 0.1f};   // 环境光颜色
    float intensity = 0.3f;               // 环境光强度
};
```

#### LightConfig

```cpp
struct LightConfig {
    std::string id;                      // 光源ID
    std::string name;                    // 光源名称
    std::string type;                    // 类型: "directional", "point", "spot"
    bool enabled = true;                 // 是否启用
    glm::vec3 position{0.0f};            // 位置
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // 方向
    glm::vec3 color{1.0f};               // 光源颜色
    float intensity = 1.0f;              // 光照强度
    float constant = 1.0f;               // 衰减常数项
    float linear = 0.09f;                // 衰减线性项
    float quadratic = 0.032f;            // 衰减二次项
    float innerCutoff = 12.5f;           // 内切角（度）
    float outerCutoff = 17.5f;           // 外切角（度）
    bool shadowEnabled = false;
    float shadowIntensity = 0.3f;
};
```

#### SceneConfig

```cpp
struct SceneConfig {
    AmbientConfig ambient;
    std::vector<LightConfig> lights;
    std::vector<ModelConfig> models;
};
```

### 类方法

```cpp
class Renderer {
public:
    Renderer(int width, int height, const std::string& title);
    ~Renderer();
    
    // 禁止拷贝
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    
    // 运行主循环
    void run();
    
private:
    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();
    void drawFrame();
    void recreateSwapchain();
    void updateGameLogic(float deltaTime);
    void renderDeveloperPanel();
    
    // 描述符管理
    void createDescriptorSetLayouts();
    void createDescriptorPool();
    void createDescriptorSets();
    void updateLightUniformBuffer();
    
    // MSAA
    void createColorResources();
    void cleanupColorResources();
    void setMsaaSamples(VkSampleCountFlagBits samples);
    
    // 配置加载
    std::vector<ModelConfig> loadModelConfig(const std::string& configFile);
    void loadModelsFromConfig(const std::vector<ModelConfig>& configs);
    VkDescriptorSet createModelDescriptorSet(GLTFModel* model, const std::string& modelId);
    SceneConfig loadSceneConfig(const std::string& configFile);
    void loadLightsFromConfig(const SceneConfig& config);
    void reloadSceneConfig();
};
```

### 使用示例

```cpp
int main() {
    vgame::Renderer renderer(1280, 720, "OverWrite");
    renderer.run();
    return 0;
}
```

---

## FloorRenderer

渲染地面平面。

### 类方法

```cpp
class FloorRenderer {
public:
    FloorRenderer(std::shared_ptr<VulkanDevice> device);
    ~FloorRenderer();
    
    void create();
    void cleanup();
    
    void render(VkCommandBuffer commandBuffer, 
                VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, 
                const glm::mat4& projectionMatrix);
};
```

### Push Constants

```cpp
struct PushConstants {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 baseColor;
    float metallic;
    float roughness;
    int hasTexture;
    float _pad0;
};
```

---

## CubeRenderer

渲染立方体。

### 类方法

```cpp
class CubeRenderer {
public:
    CubeRenderer(std::shared_ptr<VulkanDevice> device);
    ~CubeRenderer();
    
    void create();
    void cleanup();
    
    void render(VkCommandBuffer commandBuffer, 
                VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, 
                const glm::mat4& projectionMatrix);
    
    void setPosition(const glm::vec3& position);
    
    // 碰撞体
    glm::vec3 getColliderSize() const;
    glm::vec3 getColliderCenter() const;
};
```

---

## SkyboxRenderer

渲染天空盒。

### 类方法

```cpp
class SkyboxRenderer {
public:
    SkyboxRenderer(std::shared_ptr<VulkanDevice> device);
    ~SkyboxRenderer();
    
    void create();
    void cleanup();
    
    // 从 6 张纹理文件加载立方体贴图
    // 顺序: 右, 左, 上, 下, 前, 后
    void loadCubemapFromFiles(const std::vector<std::string>& facePaths);
    
    // 从十字形纹理加载立方体贴图
    void loadCubemapFromCrossLayout(const std::string& imagePath);
    
    // 渲染
    void render(VkCommandBuffer commandBuffer, 
                VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, 
                const glm::mat4& projectionMatrix);
    
    // 获取器
    VkPipelineLayout getPipelineLayout() const;
    VkDescriptorSet getDescriptorSet() const;
    VkDescriptorSetLayout getDescriptorSetLayout() const;
};
```

### 使用示例

```cpp
auto skybox = std::make_unique<SkyboxRenderer>(device);
skybox->create();

// 加载天空盒
std::vector<std::string> faces = {
    "skybox/right.jpg",
    "skybox/left.jpg",
    "skybox/top.jpg",
    "skybox/bottom.jpg",
    "skybox/front.jpg",
    "skybox/back.jpg"
};
skybox->loadCubemapFromFiles(faces);

// 渲染
skybox->render(commandBuffer, pipelineLayout, viewMatrix, projectionMatrix);
```

---

## ModelRenderer

渲染简单模型。

### 类方法

```cpp
class ModelRenderer {
public:
    ModelRenderer(std::shared_ptr<VulkanDevice> device,
                  std::shared_ptr<TextureLoader> textureLoader);
    ~ModelRenderer();
    
    void create();
    void cleanup();
    
    void loadModel(const std::string& filename);
    
    void setPosition(const glm::vec3& pos);
    void setScale(const glm::vec3& scale);
    void setRotation(float pitch, float yaw, float roll);
    
    void setTexture(std::shared_ptr<Texture> texture);
    std::shared_ptr<Texture> getTexture() const;
    
    void setUseTexture(bool use);
    bool isUsingTexture() const;
    
    void render(VkCommandBuffer commandBuffer, 
                VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, 
                const glm::mat4& projectionMatrix);
};
```

---

## TextRenderer

渲染文本（基础实现）。

### 类方法

```cpp
class TextRenderer {
public:
    TextRenderer(std::shared_ptr<VulkanDevice> device);
    ~TextRenderer();
    
    void create();
    void cleanup();
    
    void render(VkCommandBuffer commandBuffer, 
                const std::string& text, 
                float x, float y, 
                float scale);
};
```

---

## ImGuiManager

管理 ImGui 集成。

### 类方法

```cpp
class ImGuiManager {
public:
    ImGuiManager(std::shared_ptr<VulkanDevice> device,
                 std::shared_ptr<VulkanSwapchain> swapchain,
                 std::shared_ptr<VulkanRenderPass> renderPass,
                 GLFWwindow* window,
                 VkInstance instance,
                 VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    ~ImGuiManager();
    
    void init();
    void cleanup();
    void newFrame();
    void render(VkCommandBuffer commandBuffer);
    void onResize();  // 窗口大小改变时调用
};
```

### 使用示例

```cpp
// 初始化
auto imgui = std::make_unique<ImGuiManager>(
    device, swapchain, renderPass, window, instance
);
imgui->init();

// 主循环
void mainLoop() {
    // 开始新帧
    imgui->newFrame();
    
    // 构建 UI
    ImGui::Begin("Debug");
    ImGui::Text("FPS: %.1f", fps);
    ImGui::End();
    
    // 渲染
    imgui->render(commandBuffer);
}
```

---

## 渲染流程

典型的渲染流程：

```cpp
void drawFrame() {
    // 1. 等待上一帧完成
    vkWaitForFences(...);
    
    // 2. 获取下一帧图像
    uint32_t imageIndex;
    vkAcquireNextImageKHR(..., &imageIndex);
    
    // 3. 重置栅栏
    vkResetFences(...);
    
    // 4. 录制命令缓冲区
    vkResetCommandBuffer(...);
    
    // 开始渲染通道
    vkCmdBeginRenderPass(...);
    
    // 绑定管线
    vkCmdBindPipeline(...);
    
    // 渲染场景
    skyboxRenderer->render(...);
    floorRenderer->render(...);
    cubeRenderer->render(...);
    modelRenderer->render(...);
    
    // 渲染 UI
    imguiManager->render(...);
    
    // 结束渲染通道
    vkCmdEndRenderPass(...);
    
    // 5. 提交命令
    vkQueueSubmit(...);
    
    // 6. 呈现
    vkQueuePresentKHR(...);
    
    // 7. 更新帧计数
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
```

---

## 开发者面板

Renderer 内置开发者面板，通过 `renderDeveloperPanel()` 方法渲染：

- FPS 和帧时间统计
- 相机位置和方向
- 光源控制
- 模型控制
- MSAA 设置
- 网络连接

启用开发者模式：

```cpp
// 按 F3 切换开发者模式
if (input->isKeyJustPressed(GLFW_KEY_F3)) {
    developerMode = !developerMode;
}
```
