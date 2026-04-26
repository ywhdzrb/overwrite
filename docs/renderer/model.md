# 模型加载

模型加载模块支持 OBJ 和 glTF 2.0 格式的 3D 模型。

## 类概览

| 类名 | 文件 | 描述 |
|------|------|------|
| `Vertex` | `include/renderer/model.h` | 顶点数据结构 |
| `Model` | `include/renderer/model.h` | OBJ 模型加载 |
| `Mesh` | `include/renderer/mesh.h` | 网格类 |
| `GLTFModel` | `include/renderer/gltf_model.h` | glTF 模型加载 |

---

## Vertex（顶点结构）

### 定义

```cpp
struct Vertex {
    glm::vec3 pos;       // 位置
    glm::vec3 normal;    // 法线
    glm::vec3 color;     // 颜色
    glm::vec2 texCoord;  // 纹理坐标
    
    bool operator==(const Vertex& other) const;
    
    // Vulkan 输入描述
    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions();
};
```

### Vulkan 输入描述

```cpp
// 绑定描述
VkVertexInputBindingDescription Vertex::getBindingDescription() {
    return {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
}

// 属性描述
std::array<VkVertexInputAttributeDescription, 4> Vertex::getAttributeDescriptions() {
    return {{
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) },       // 位置
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },    // 法线
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) },     // 颜色
        { 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) }      // 纹理坐标
    }};
}
```

---

## Model（OBJ 模型）

加载 OBJ 格式的简单模型。

### 类方法

```cpp
class Model {
public:
    Model(std::shared_ptr<VulkanDevice> device);
    ~Model();
    
    // 禁止拷贝
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    
    // 从 OBJ 文件加载
    void loadFromObj(const std::string& filename);
    
    // 清理资源
    void cleanup();
    
    // 获取数据
    const std::vector<Vertex>& getVertices() const;
    const std::vector<uint32_t>& getIndices() const;
};
```

### 使用示例

```cpp
auto model = std::make_unique<Model>(device);
model->loadFromObj("assets/models/cube.obj");

// 获取顶点数据
const auto& vertices = model->getVertices();
const auto& indices = model->getIndices();

// 创建网格
auto mesh = std::make_unique<Mesh>(device, vertices, indices);
```

---

## Mesh（网格类）

封装顶点缓冲区和索引缓冲区。

### 类方法

```cpp
class Mesh {
public:
    Mesh(std::shared_ptr<VulkanDevice> device,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
    ~Mesh();
    
    // 禁止拷贝
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    
    // 绑定顶点和索引缓冲区
    void bind(VkCommandBuffer commandBuffer) const;
    
    // 绘制
    void draw(VkCommandBuffer commandBuffer) const;
    
    // 清理资源
    void cleanup();
};
```

### 使用示例

```cpp
// 创建网格
std::vector<Vertex> vertices = { ... };
std::vector<uint32_t> indices = { ... };
auto mesh = std::make_unique<Mesh>(device, vertices, indices);

// 渲染
mesh->bind(commandBuffer);
mesh->draw(commandBuffer);
```

---

## GLTFModel（glTF 模型）

加载 glTF 2.0 格式的模型，支持网格、材质、纹理、动画和蒙皮。

### 枚举和结构体

#### AnimationPath

```cpp
enum class AnimationPath {
    Translation,  // 平移
    Rotation,     // 旋转
    Scale,        // 缩放
    Weights       // 权重
};
```

#### AnimationChannel

```cpp
struct AnimationChannel {
    int nodeIndex;       // 目标节点索引
    AnimationPath path;  // 目标属性
    int samplerIndex;    // 采样器索引
};
```

#### AnimationSampler

```cpp
struct AnimationSampler {
    std::vector<float> inputs;     // 时间戳
    std::vector<glm::vec4> outputs; // 输出值
    std::string interpolation;     // 插值方式：LINEAR, STEP, CUBICSPLINE
};
```

#### Animation

```cpp
struct Animation {
    std::string name;
    std::vector<AnimationChannel> channels;
    std::vector<AnimationSampler> samplers;
    float duration = 0.0f;
};
```

#### AnimationState

```cpp
struct AnimationState {
    int animationIndex = -1;    // 当前动画索引
    float currentTime = 0.0f;   // 当前时间
    bool playing = false;       // 是否播放中
    bool loop = true;           // 是否循环
    float speed = 1.0f;         // 播放速度
};
```

#### NodeTransform

```cpp
struct NodeTransform {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    bool hasTranslation = false;
    bool hasRotation = false;
    bool hasScale = false;
};
```

#### Joint 和 Skin

```cpp
struct Joint {
    int nodeIndex;
    glm::mat4 inverseBindMatrix;
};

struct Skin {
    std::string name;
    std::vector<Joint> joints;
    glm::mat4 inverseBindMatrix;
    int skeletonRoot = -1;
};
```

### 类方法

#### 构造与析构

```cpp
GLTFModel(std::shared_ptr<VulkanDevice> device,
          std::shared_ptr<TextureLoader> textureLoader);
~GLTFModel();

// 禁止拷贝
GLTFModel(const GLTFModel&) = delete;
GLTFModel& operator=(const GLTFModel&) = delete;
```

#### 加载和渲染

```cpp
// 加载 glTF 模型文件
bool loadFromFile(const std::string& filename);

// 清理资源
void cleanup();

// 渲染模型
void render(VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout,
            const glm::mat4& viewMatrix,
            const glm::mat4& projectionMatrix,
            const glm::mat4& modelMatrix = glm::mat4(1.0f));

// 渲染特定节点
void renderNode(VkCommandBuffer commandBuffer,
               VkPipelineLayout pipelineLayout,
               const glm::mat4& viewMatrix,
               const glm::mat4& projectionMatrix,
               size_t nodeIndex,
               const glm::mat4& parentMatrix = glm::mat4(1.0f));
```

#### 属性获取

```cpp
// 包围盒
std::pair<glm::vec3, glm::vec3> getBoundingBox() const;

// 名称
const std::string& getName() const;

// 数量
size_t getNodeCount() const;
size_t getMeshCount() const;
size_t getMaterialCount() const;

// 纹理
std::shared_ptr<Texture> getFirstTexture() const;
```

#### 变换

```cpp
void setPosition(const glm::vec3& pos);
const glm::vec3& getPosition() const;

void setRotation(float pitch, float yaw, float roll);
const glm::vec3& getRotation() const;

void setScale(const glm::vec3& s);
const glm::vec3& getScale() const;

glm::mat4 getModelMatrix() const;
glm::mat4 getNodeTransform(size_t nodeIndex, const glm::mat4& parentMatrix) const;
```

#### 细分

```cpp
void setSubdivisionIterations(int iterations);
int getSubdivisionIterations() const;
```

#### 动画控制

```cpp
// 动画数量和名称
size_t getAnimationCount() const;
const std::string& getAnimationName(size_t index) const;

// 播放动画
void playAnimation(size_t index, bool loop = true, float speed = 1.0f);
bool playAnimation(const std::string& name, bool loop = true, float speed = 1.0f);

// 停止/暂停
void stopAnimation();
void pauseAnimation();
void resumeAnimation();

// 更新动画
void updateAnimation(float deltaTime);

// 状态查询
bool isAnimationPlaying() const;
int getCurrentAnimationIndex() const;

// 参数设置
void setAnimationSpeed(float speed);
void setAnimationLoop(bool loop);

// 多动画模式
void playAllAnimations(bool loop = true, float speed = 1.0f);
bool isPlayingMultipleAnimations() const;
```

---

## 使用示例

### 基本加载和渲染

```cpp
// 创建模型
auto model = std::make_unique<GLTFModel>(device, textureLoader);

// 加载文件
if (!model->loadFromFile("assets/models/player.glb")) {
    std::cerr << "Failed to load model" << std::endl;
}

// 设置变换
model->setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
model->setRotation(0.0f, 90.0f, 0.0f);
model->setScale(glm::vec3(1.0f));

// 渲染
model->render(commandBuffer, pipelineLayout, viewMatrix, projectionMatrix);
```

### 动画播放

```cpp
// 列出所有动画
for (size_t i = 0; i < model->getAnimationCount(); ++i) {
    std::cout << "Animation " << i << ": " << model->getAnimationName(i) << std::endl;
}

// 播放动画
model->playAnimation(0, true, 1.0f);  // 循环播放第一个动画

// 或按名称播放
model->playAnimation("Walk", true, 1.0f);

// 更新动画（每帧调用）
model->updateAnimation(deltaTime);

// 检查状态
if (model->isAnimationPlaying()) {
    std::cout << "Playing animation " << model->getCurrentAnimationIndex() << std::endl;
}
```

### 网格细分

```cpp
// 设置细分迭代次数
// 每次迭代将每个三角形分成 4 个
model->setSubdivisionIterations(2);

// 加载模型（细分在加载时应用）
model->loadFromFile("assets/models/low_poly.glb");
```

---

## 支持的 glTF 功能

### 已支持

- ✅ 网格（Mesh）
- ✅ 材质（Material）
- ✅ PBR 纹理（BaseColor, Normal, MetallicRoughness）
- ✅ 节点层级（Node Hierarchy）
- ✅ 动画（Translation, Rotation, Scale）
- ✅ 蒙皮（Skinning）
- ✅ GLB 二进制格式
- ✅ GLTF + 外部资源

### 未支持

- ❌ 形态目标（Morph Targets）
- ❌ 纹理变换（Texture Transform）
- ❌ 灯光和相机
- ❌ 遮挡和自发光贴图
