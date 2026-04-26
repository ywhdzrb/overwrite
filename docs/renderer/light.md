# 光源系统

光源系统管理场景中的光源，支持方向光、点光源和聚光灯。

## 类概览

| 类名 | 文件 | 描述 |
|------|------|------|
| `LightType` | `include/renderer/light.h` | 光源类型枚举 |
| `Light` | `include/renderer/light.h` | 光源类 |
| `ShaderLight` | `include/renderer/light.h` | 着色器光源数据结构 |
| `LightManager` | `include/renderer/light_manager.h` | 光源管理器 |

---

## LightType（光源类型枚举）

```cpp
enum class LightType {
    DIRECTIONAL,  // 方向光（如太阳光）
    POINT,        // 点光源（如灯泡）
    SPOT          // 聚光灯（如手电筒）
};
```

---

## Light

表示场景中的一个光源。

### 类方法

#### 构造

```cpp
Light(const std::string& name, LightType type);
Light();  // 默认：白色点光源
~Light() = default;

// 允许拷贝
Light(const Light&) = default;
Light& operator=(const Light&) = default;
```

#### 基本属性

```cpp
// 名称
const std::string& getName() const;
void setName(const std::string& n);

// 类型
LightType getType() const;
void setType(LightType t);

// 位置和方向
const glm::vec3& getPosition() const;
void setPosition(const glm::vec3& pos);
const glm::vec3& getDirection() const;
void setDirection(const glm::vec3& dir);

// 颜色和强度
const glm::vec3& getColor() const;
void setColor(const glm::vec3& c);
float getIntensity() const;
void setIntensity(float i);

// 启用状态
bool isEnabled() const;
void setEnabled(bool e);
```

#### 点光源参数

```cpp
// 衰减参数
float getConstant() const;
void setConstant(float c);
float getLinear() const;
void setLinear(float l);
float getQuadratic() const;
void setQuadratic(float q);
```

#### 聚光灯参数

```cpp
// 切角（度数）
float getInnerCutoff() const;
void setInnerCutoff(float angle);
float getOuterCutoff() const;
void setOuterCutoff(float angle);
```

#### 阴影参数

```cpp
bool isShadowEnabled() const;
void setShadowEnabled(bool e);
float getShadowIntensity() const;
void setShadowIntensity(float intensity);
```

#### 实用方法

```cpp
// 计算光照强度（考虑衰减）
float calculateAttenuation(float distance) const;

// 检查点是否在聚光灯范围内
bool isInSpotlightRange(const glm::vec3& point) const;

// 快速设置方法
void setupAsDirectional(const glm::vec3& direction,
                       const glm::vec3& color = glm::vec3(1.0f),
                       float intensity = 1.0f);

void setupAsPoint(const glm::vec3& position,
                 const glm::vec3& color = glm::vec3(1.0f),
                 float intensity = 1.0f,
                 float range = 10.0f);

void setupAsSpot(const glm::vec3& position,
                const glm::vec3& direction,
                const glm::vec3& color = glm::vec3(1.0f),
                float intensity = 1.0f,
                float innerCutoff = 12.5f,
                float outerCutoff = 17.5f,
                float range = 10.0f);

// 设置标准衰减参数
void setStandardAttenuation(float range);
```

---

## ShaderLight

用于着色器的光源数据结构，遵循 std140 布局。

```cpp
struct ShaderLight {
    // Offset 0-15 (16 bytes)
    int type;               // 0=方向光, 1=点光源, 2=聚光灯
    int enabled;            // 是否启用
    float _pad1[2];         // 填充

    // Offset 16-31 (16 bytes)
    glm::vec3 position;     // 位置
    float _pad2;

    // Offset 32-47 (16 bytes)
    glm::vec3 direction;    // 方向
    float _pad3;

    // Offset 48-63 (16 bytes)
    glm::vec3 color;        // 颜色
    float intensity;        // 强度

    // Offset 64-79 (16 bytes)
    float constant;         // 衰减常数项
    float linear;           // 衰减线性项
    float quadratic;        // 衰减二次项
    float _pad4;

    // Offset 80-95 (16 bytes)
    float innerCutoff;      // 聚光灯内切角（弧度）
    float outerCutoff;      // 聚光灯外切角（弧度）
    float shadowIntensity;  // 阴影强度
    float _pad5;
};
// 总大小: 96 bytes
```

### 使用数组

```cpp
using ShaderLightArray = std::array<ShaderLight, 16>;  // 最多 16 个光源
```

---

## LightManager

管理场景中的所有光源。

### 类方法

#### 构造

```cpp
LightManager();
~LightManager() = default;

// 禁止拷贝
LightManager(const LightManager&) = delete;
LightManager& operator=(const LightManager&) = delete;
```

#### 光源管理

```cpp
// 添加光源
int addLight(const Light& light);  // 返回光源 ID

// 移除光源
bool removeLight(int lightId);

// 获取光源
Light* getLight(int lightId);
const Light* getLight(int lightId) const;

// 通过名称获取
Light* getLightByName(const std::string& name);
const Light* getLightByName(const std::string& name) const;

// 获取所有光源
const std::vector<std::unique_ptr<Light>>& getLights() const;

// 清空
void clear();

// 数量
size_t getLightCount() const;
int getEnabledLightCount() const;

// 最大光源数量
static constexpr int getMaxLights();  // 返回 16
```

#### 环境光

```cpp
const glm::vec3& getAmbientColor() const;
void setAmbientColor(const glm::vec3& color);

float getAmbientIntensity() const;
void setAmbientIntensity(float intensity);

// 获取环境光（颜色 * 强度）
glm::vec3 getAmbient() const;
```

#### 着色器数据

```cpp
// 获取着色器光源数据数组
ShaderLightArray getShaderLightData() const;
```

#### 快捷方法

```cpp
// 添加方向光
int addDirectionalLight(const std::string& name,
                       const glm::vec3& direction,
                       const glm::vec3& color = glm::vec3(1.0f),
                       float intensity = 1.0f);

// 添加点光源
int addPointLight(const std::string& name,
                 const glm::vec3& position,
                 const glm::vec3& color = glm::vec3(1.0f),
                 float intensity = 1.0f,
                 float range = 10.0f);

// 添加聚光灯
int addSpotLight(const std::string& name,
                const glm::vec3& position,
                const glm::vec3& direction,
                const glm::vec3& color = glm::vec3(1.0f),
                float intensity = 1.0f,
                float innerCutoff = 12.5f,
                float outerCutoff = 17.5f,
                float range = 10.0f);

// 更新光源
bool updateLight(int lightId, const Light& light);

// 启用/禁用
bool enableLight(int lightId);
bool disableLight(int lightId);
void enableAllLights();
void disableAllLights();
```

---

## 使用示例

### 创建光源

```cpp
auto lightManager = std::make_unique<LightManager>();

// 设置环境光
lightManager->setAmbientColor(glm::vec3(0.1f, 0.1f, 0.15f));
lightManager->setAmbientIntensity(0.3f);

// 添加太阳光（方向光）
int sunId = lightManager->addDirectionalLight(
    "sun",
    glm::vec3(-0.5f, -1.0f, -0.3f),  // 方向
    glm::vec3(1.0f, 0.95f, 0.9f),    // 暖白色
    1.0f
);

// 添加点光源（灯泡）
int bulbId = lightManager->addPointLight(
    "bulb",
    glm::vec3(5.0f, 3.0f, 5.0f),     // 位置
    glm::vec3(1.0f, 0.8f, 0.6f),     // 暖色
    2.0f,
    15.0f                            // 范围
);

// 添加聚光灯（手电筒）
int flashlightId = lightManager->addSpotLight(
    "flashlight",
    glm::vec3(0.0f, 2.0f, 0.0f),     // 位置
    glm::vec3(0.0f, -1.0f, 0.0f),    // 方向
    glm::vec3(1.0f),
    5.0f,
    15.0f,                           // 内切角
    20.0f,                           // 外切角
    20.0f                            // 范围
);
```

### 更新光源

```cpp
// 获取并修改光源
Light* light = lightManager->getLight(sunId);
if (light) {
    light->setDirection(newDirection);
    light->setIntensity(1.5f);
}

// 或使用更新方法
Light newLight("sun", LightType::DIRECTIONAL);
newLight.setupAsDirectional(newDirection, glm::vec3(1.0f), 1.5f);
lightManager->updateLight(sunId, newLight);
```

### 上传到着色器

```cpp
// 获取着色器数据
ShaderLightArray lightData = lightManager->getShaderLightData();

// 更新 uniform buffer
void* data;
vkMapMemory(device, lightUniformBufferMemory, 0, sizeof(lightData), 0, &data);
memcpy(data, lightData.data(), sizeof(lightData));
vkUnmapMemory(device, lightUniformBufferMemory);
```

### 动态光源

```cpp
// 在游戏循环中更新光源位置
void update(float deltaTime) {
    Light* flashlight = lightManager->getLightByName("flashlight");
    if (flashlight) {
        // 跟随玩家
        flashlight->setPosition(player.position);
        flashlight->setDirection(player.getFront());
    }
}
```

---

## 光源类型详解

### 方向光（Directional）

- 无位置，只有方向
- 光线平行，适合模拟太阳光
- 无衰减

```cpp
light.setupAsDirectional(
    glm::vec3(-0.5f, -1.0f, -0.3f),  // 方向
    glm::vec3(1.0f),                  // 颜色
    1.0f                              // 强度
);
```

### 点光源（Point）

- 从一点向所有方向发射
- 有衰减（距离越远越暗）
- 适合灯泡、火把等

```cpp
light.setupAsPoint(
    glm::vec3(0.0f, 5.0f, 0.0f),     // 位置
    glm::vec3(1.0f, 0.8f, 0.6f),     // 暖色
    2.0f,                            // 强度
    15.0f                            // 范围（影响衰减）
);
```

### 聚光灯（Spot）

- 从一点向锥形方向发射
- 有内外切角控制光锥
- 适合手电筒、舞台灯

```cpp
light.setupAsSpot(
    glm::vec3(0.0f, 2.0f, 0.0f),     // 位置
    glm::vec3(0.0f, -1.0f, 0.0f),    // 方向
    glm::vec3(1.0f),
    5.0f,
    15.0f,                           // 内切角（度）
    20.0f,                           // 外切角（度）
    20.0f                            // 范围
);
```

---

## 衰减公式

点光源和聚光灯使用以下衰减公式：

```
attenuation = 1.0 / (constant + linear * d + quadratic * d²)
```

其中 `d` 是光源到表面的距离。

### 预设衰减值

| 范围 | Constant | Linear | Quadratic |
|------|----------|--------|-----------|
| 7    | 1.0      | 0.7    | 1.8       |
| 13   | 1.0      | 0.35   | 0.44      |
| 20   | 1.0      | 0.22   | 0.20      |
| 32   | 1.0      | 0.14   | 0.07      |
| 50   | 1.0      | 0.09   | 0.032     |
| 65   | 1.0      | 0.07   | 0.017     |
| 100  | 1.0      | 0.045  | 0.0075    |

使用 `setStandardAttenuation(range)` 可自动设置合适的衰减参数。
