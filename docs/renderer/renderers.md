# 渲染器组件

本文档描述各种专用渲染器。

---

## Push Constants

FloorRenderer 和 ModelRenderer 使用相同的 Push Constants 结构，SkyboxRenderer 使用简化的 Push Constants（仅包含 view/proj）：

```cpp
// FloorRenderer / ModelRenderer 通用 Push Constants
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

// SkyboxRenderer 专用 Push Constants（仅需视图和投影，无模型变换）
// struct SkyboxRenderer::PushConstants {
//     glm::mat4 view;
//     glm::mat4 proj;
// };
```

---

## FloorRenderer

渲染地面平面网格。

### 类定义

```cpp
class FloorRenderer : public IRenderer {
public:
    explicit FloorRenderer(std::shared_ptr<VulkanDevice> device);
    ~FloorRenderer() override;
    
    // IRenderer 接口实现
    void create() override;
    void cleanup() override;
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    std::string getName() const override { return "FloorRenderer"; }
    bool isCreated() const override { return created_; }
    
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 baseColor;
        float metallic;
        float roughness;
        int hasTexture;
        float _pad0;  // 填充到16字节对齐
    };
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

## SkyboxRenderer

渲染天空盒。

### 类定义

```cpp
class SkyboxRenderer : public IRenderer {
public:
    explicit SkyboxRenderer(std::shared_ptr<VulkanDevice> device);
    ~SkyboxRenderer() override;
    
    // IRenderer 接口实现
    void create() override;
    void cleanup() override;
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    std::string getName() const override { return "SkyboxRenderer"; }
    bool isCreated() const override { return created_; }
    
    // 从 6 张纹理加载立方体贴图
    // 顺序: 右(+X), 左(-X), 上(+Y), 下(-Y), 前(+Z), 后(-Z)
    void loadCubemapFromFiles(const std::vector<std::string>& facePaths);
    
    // 从十字形布局纹理加载
    void loadCubemapFromCrossLayout(const std::string& imagePath);
    
    // SkyboxRenderer 专用 Push Constants（仅 view/proj，无 model 变换）
    struct PushConstants {
        glm::mat4 view;
        glm::mat4 proj;
    };
    
    // 获取器
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkDescriptorSet getDescriptorSet() const { return descriptorSet; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
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
class ModelRenderer : public IRenderer {
public:
    ModelRenderer(std::shared_ptr<VulkanDevice> device,
                  std::shared_ptr<TextureLoader> textureLoader);
    ~ModelRenderer() override;
    
    // IRenderer 接口实现
    void create() override;
    void cleanup() override;
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    std::string getName() const override { return "ModelRenderer"; }
    bool isCreated() const override { return created_; }
    
    void loadModel(const std::string& filename);
    
    void setPosition(const glm::vec3& pos);
    void setScale(const glm::vec3& scale);
    void setRotation(float pitch, float yaw, float roll);
    
    void setTexture(std::shared_ptr<Texture> texture);
    std::shared_ptr<Texture> getTexture() const;
    
    void setUseTexture(bool use);
    bool isUsingTexture() const;
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

---

## TerrainRenderer

动态地形渲染器，使用 Perlin 噪声（4-octave FBM）生成高度图，支持异步区块加载和卸载。

### 架构：异步生成管线

为消除区块边界跨越时的卡顿，`update()` 将原先的同步批量生成改为四阶段异步管线：

1. **Phase 1 — 消费已完成任务**：轮询 `std::future`，将后台线程计算好的网格通过 `uploadChunk()` 上传到 Vulkan
2. **Phase 2 — 扫描候选区块**：在 `generationRadius` 范围内查找缺失区块，去重（已存在/已排队）
3. **Phase 3 — 启动异步任务**：按玩家距离排序，每帧最多启动 `maxChunksPerFrame` 个 `std::async`
4. **Phase 4 — 卸载旧区块**：移除 `renderRadius + 2` 范围外的区块

计算与上传分离：
- `computeChunkMesh()` — 纯 CPU 网格生成（噪声采样 + 顶点/索引），无 Vulkan 调用，在 `std::async` 后台线程安全执行
- `uploadChunk()` — Vulkan 缓冲创建 + 数据上传，必须在主线程调用

### 类定义

```cpp
class TerrainRenderer {
public:
    explicit TerrainRenderer(std::shared_ptr<VulkanDevice> device);
    ~TerrainRenderer();

    void create();
    void cleanup();
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    
    void update(const glm::vec3& playerPos);  // 异步区块更新管线
    float getHeight(float x, float z) const;   // 获取地形高度（线程安全，仅读取）

private:
    // 纯 CPU 网格生成（线程安全，在 std::async 后台线程中执行）
    ChunkMesh computeChunkMesh(int chunkX, int chunkZ) const;
    // 将已计算的网格数据上传到 Vulkan（必须在主线程调用）
    void uploadChunk(const ChunkMesh& mesh);
};
```

### 参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| chunkSize | 16.0f | 区块边长（世界单位） |
| renderRadius | 10 | 渲染/卸载边界（卸载范围 = renderRadius + 2） |
| generationRadius | 13 | 预生成扫描半径（renderRadius + 3，提前生成边界外区块） |
| maxChunksPerFrame | 4 | 每帧异步任务上限，削去区块生成峰值 |
| noiseScale | 0.08f | 噪声缩放因子 |
| heightScale | 5.0f | 高度缩放因子 |
| baseHeight | 0.0f | 基准高度 |

### 性能数据

- **原先**：跨越区块边界时单帧同步生成 ~314 个区块 → ~30,000 次 FBM 调用 + Vulkan 分配
- **现在**：每帧最多启动 4 个异步任务，多数区块在后台静默完成。初始场景约 3-4 秒渐进加载完毕。
- **高度查询**：`getHeight()` 标记 `const`，构造函数一次性初始化排列表后永不修改，支持从作业线程安全调用

### 线程安全

- 构造函数初始化 `perm` 排列表并设置 `permInitialized = true`，阻止 `initPerm()` 重复运行
- `perlinNoise()` / `fbm()` / `getHeight()` / `computeChunkMesh()` 均为 `const`，仅读取成员数据
- `perm` 向量在构造函数后永不修改，可安全并发读取

### 使用方法

```cpp
// 初始化
terrainRenderer = std::make_unique<TerrainRenderer>(vulkanDevice);
terrainRenderer->create();

// 每帧更新（基于玩家位置）
terrainRenderer->update(playerPosition);

// 渲染
terrainRenderer->render(commandBuffer, pipelineLayout, viewMatrix, projMatrix);

// 地形高度查询
auto terrainQuery = [this](float x, float z) -> float {
    return terrainRenderer->getHeight(x, z);
};
physicsSystem->setTerrainQuery(terrainQuery);
```

---

### TerrainQuery (服务端)

服务端使用的地形高度查询，与客户端使用相同的噪声算法。

```cpp
// 使用方法
physicsSystem_->setTerrainQuery(TerrainQuery::getHeight);
```
