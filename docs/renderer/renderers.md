# 渲染器组件

本文档描述各种专用渲染器。

---

## Push Constants

所有渲染器使用相同的 Push Constants 结构：

```cpp
struct PushConstants {
    glm::mat4 model;        // 模型矩阵
    glm::mat4 view;         // 视图矩阵
    glm::mat4 proj;         // 投影矩阵
    glm::vec3 baseColor;    // 基础颜色
    float metallic;         // 金属度
    float roughness;        // 粗糙度
    int hasTexture;         // 是否有纹理
    float _pad0;            // 填充
};
```

---

## FloorRenderer

渲染地面平面网格。

### 类定义

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

### 使用示例

```cpp
auto floorRenderer = std::make_unique<FloorRenderer>(device);
floorRenderer->create();

// 渲染
floorRenderer->render(commandBuffer, pipelineLayout, viewMatrix, projectionMatrix);
```

### 特点

- 生成平面网格
- 使用棋盘格着色器
- 无纹理，通过着色器生成图案

---

## CubeRenderer

渲染立方体。

### 类定义

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
    glm::vec3 getColliderSize() const;   // 返回 (5.0f, 5.0f, 5.0f)
    glm::vec3 getColliderCenter() const;
};
```

### 使用示例

```cpp
auto cubeRenderer = std::make_unique<CubeRenderer>(device);
cubeRenderer->create();

// 设置位置
cubeRenderer->setPosition(glm::vec3(10.0f, 0.0f, 5.0f));

// 渲染
cubeRenderer->render(commandBuffer, pipelineLayout, viewMatrix, projectionMatrix);

// 碰撞检测
glm::vec3 cubePos = cubeRenderer->getColliderCenter();
glm::vec3 cubeSize = cubeRenderer->getColliderSize();
```

### 特点

- 固定大小的立方体（5.0 单位）
- 支持碰撞检测
- 用于障碍物或测试物体

---

## SkyboxRenderer

渲染天空盒。

### 类定义

```cpp
class SkyboxRenderer {
public:
    SkyboxRenderer(std::shared_ptr<VulkanDevice> device);
    ~SkyboxRenderer();
    
    void create();
    void cleanup();
    
    // 从 6 张纹理加载立方体贴图
    // 顺序: 右(+X), 左(-X), 上(+Y), 下(-Y), 前(+Z), 后(-Z)
    void loadCubemapFromFiles(const std::vector<std::string>& facePaths);
    
    // 从十字形布局纹理加载
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
auto skyboxRenderer = std::make_unique<SkyboxRenderer>(device);
skyboxRenderer->create();

// 从 6 个面加载
std::vector<std::string> faces = {
    "assets/skybox/right.jpg",
    "assets/skybox/left.jpg",
    "assets/skybox/top.jpg",
    "assets/skybox/bottom.jpg",
    "assets/skybox/front.jpg",
    "assets/skybox/back.jpg"
};
skyboxRenderer->loadCubemapFromFiles(faces);

// 或从十字布局加载
skyboxRenderer->loadCubemapFromCrossLayout("assets/skybox/skybox_cross.jpg");

// 渲染（必须在其他物体之前渲染）
glm::mat4 skyboxView = glm::mat4(glm::mat3(viewMatrix));  // 移除平移
skyboxRenderer->render(commandBuffer, pipelineLayout, skyboxView, projectionMatrix);
```

### 特点

- 使用立方体贴图
- 独立的管线和描述符集
- 视图矩阵移除平移分量以保持无限远效果

### 渲染顺序

天空盒应最先渲染：

```cpp
// 1. 天空盒
glDepthMask(GL_FALSE);  // 禁用深度写入
skyboxRenderer->render(...);
glDepthMask(GL_TRUE);

// 2. 其他物体
floorRenderer->render(...);
modelRenderer->render(...);
```

---

## ModelRenderer

渲染简单模型（OBJ 格式）。

### 类定义

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

### 使用示例

```cpp
auto modelRenderer = std::make_unique<ModelRenderer>(device, textureLoader);
modelRenderer->create();

// 加载模型
modelRenderer->loadModel("assets/models/cube.obj");

// 设置变换
modelRenderer->setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
modelRenderer->setRotation(0.0f, 45.0f, 0.0f);
modelRenderer->setScale(glm::vec3(2.0f));

// 设置纹理
auto texture = textureLoader->loadTexture("assets/textures/wood.png");
modelRenderer->setTexture(texture);
modelRenderer->setUseTexture(true);

// 渲染
modelRenderer->render(commandBuffer, pipelineLayout, viewMatrix, projectionMatrix);
```

---

## TextRenderer

渲染文本（基础实现）。

### 类定义

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

### 使用示例

```cpp
auto textRenderer = std::make_unique<TextRenderer>(device);
textRenderer->create();

// 渲染文本
textRenderer->render(commandBuffer, "Hello World", 100.0f, 100.0f, 1.0f);
```

### 特点

- 使用预生成的位图字体
- 固定大小：256x64 像素纹理
- 字符大小：16x16 像素
- 支持 ASCII 字符

### 限制

- 仅支持基本 ASCII 字符
- 不支持 Unicode
- 不支持动态字体大小

---

## ImGuiManager

管理 ImGui 集成。

### 类定义

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
    void onResize();
};
```

### 使用示例

```cpp
// 初始化
auto imguiManager = std::make_unique<ImGuiManager>(
    device, swapchain, renderPass, window, instance
);
imguiManager->init();

// 主循环
void mainLoop() {
    // 开始新帧
    imguiManager->newFrame();
    
    // 构建 UI
    ImGui::Begin("Debug Panel");
    ImGui::Text("FPS: %.1f", fps);
    ImGui::SliderFloat("Speed", &speed, 0.0f, 10.0f);
    
    if (ImGui::Button("Reset")) {
        resetGame();
    }
    ImGui::End();
    
    // 渲染
    imguiManager->render(commandBuffer);
}

// 窗口大小改变时
void onResize() {
    imguiManager->onResize();
}
```

### 常用 ImGui 控件

```cpp
// 文本
ImGui::Text("Hello, World!");

// 按钮
if (ImGui::Button("Click Me")) {
    // 按钮被点击
}

// 滑块
ImGui::SliderFloat("Value", &value, 0.0f, 100.0f);
ImGui::SliderInt("Count", &count, 0, 100);

// 复选框
ImGui::Checkbox("Enable", &enabled);

// 颜色选择器
ImGui::ColorEdit3("Color", &color.x);

// 下拉列表
const char* items[] = { "Option A", "Option B", "Option C" };
ImGui::Combo("Select", &selectedIndex, items, IM_ARRAYSIZE(items));

// 输入框
ImGui::InputText("Name", buffer, sizeof(buffer));
ImGui::InputFloat("Number", &number);

// 分组
ImGui::BeginGroup();
// ... 控件
ImGui::EndGroup();

// 折叠区域
if (ImGui::CollapsingHeader("Advanced Settings")) {
    // ... 控件
}
```

---

## 渲染顺序

推荐的渲染顺序：

```cpp
void renderFrame() {
    // 1. 开始渲染通道
    vkCmdBeginRenderPass(...);
    
    // 2. 天空盒（禁用深度写入）
    vkCmdDepthWriteEnable(false);
    skyboxRenderer->render(...);
    vkCmdDepthWriteEnable(true);
    
    // 3. 不透明物体
    floorRenderer->render(...);
    cubeRenderer->render(...);
    
    // 4. 模型
    for (auto& model : models) {
        model->render(...);
    }
    
    // 5. 透明物体（从远到近排序）
    // ...
    
    // 6. UI（ImGui）
    imguiManager->render(...);
    
    // 7. 结束渲染通道
    vkCmdEndRenderPass(...);
}
```
