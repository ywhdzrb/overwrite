# 相机系统

相机系统提供了第一人称、第三人称和自由视角三种模式，并支持视锥体剔除。

## 类概览

| 类名 | 文件 | 描述 |
|------|------|------|
| `Camera` | `include/core/camera.h` | 主相机类 |
| `Frustum` | `include/core/camera.h` | 视锥体类（用于剔除） |

---

## Frustum（视锥体）

视锥体类用于视锥体剔除，判断物体是否在相机可见范围内。

### 结构体

#### FrustumPlane

```cpp
struct FrustumPlane {
    glm::vec3 normal;    // 平面法向量
    float distance;      // 平面到原点的距离
    
    // 计算点到平面的距离
    float distanceToPoint(const glm::vec3& point) const;
};
```

### 类方法

```cpp
class Frustum {
public:
    std::array<FrustumPlane, 6> planes;  // 左、右、下、上、近、远
    
    // 从视图投影矩阵提取视锥体平面
    void update(const glm::mat4& viewProjection);
    
    // 检查点是否在视锥体内
    bool isPointInside(const glm::vec3& point) const;
    
    // 检查球体是否在视锥体内
    bool isSphereInside(const glm::vec3& center, float radius) const;
    
    // 检查轴对齐包围盒是否在视锥体内
    bool isAABBInside(const glm::vec3& min, const glm::vec3& max) const;
};
```

---

## Camera

主相机类，处理位置、方向、移动和物理。

### 枚举

```cpp
enum class Mode {
    FirstPerson,    // 第一人称
    ThirdPerson,    // 第三人称
    Free            // 自由视角
};
```

### 类型定义

```cpp
// 地形高度查询函数类型
using CameraTerrainQuery = std::function<float(float x, float z)>;
```

### 类方法

#### 构造与析构

```cpp
Camera(int windowWidth, int windowHeight);
~Camera() = default;
```

#### 更新

```cpp
// 更新相机位置和方向
void update(float deltaTime,
             bool moveForward, bool moveBackward,
             bool moveLeft, bool moveRight,
             bool jump, bool freeCameraToggle,
             bool shiftPressed, bool spaceHeld);
```

#### 模式切换

```cpp
// 切换自由视角模式（可选：切回时设置的目标位置）
void toggleFreeCamera(const glm::vec3& targetPos = glm::vec3(0.0f));

// 检查是否在自由视角模式
bool isFreeCameraMode() const;

// 检查是否需要跳过同步（仅一次）
bool needsSyncSkip() const;
void clearSyncSkip();

// 设置/获取相机模式
void setMode(Mode m);
Mode getMode() const;
void toggleMode();
```

#### 鼠标输入

```cpp
// 处理鼠标移动
void processMouseMovement(float xOffset, float yOffset);
```

#### 矩阵获取

```cpp
// 获取视图矩阵
glm::mat4 getViewMatrix() const;

// 获取投影矩阵
glm::mat4 getProjectionMatrix() const;
```

#### 位置与方向

```cpp
// 获取相机属性
glm::vec3 getPosition() const;
glm::vec3 getFront() const;
glm::vec3 getRight() const;
glm::vec3 getUp() const;

// 设置相机属性
void setPosition(const glm::vec3& pos);
void setFront(const glm::vec3& f);

// 欧拉角设置
void setYaw(float y);
void setPitch(float p);
void setYawPitch(float y, float p);

// 欧拉角获取
float getYaw() const;
float getPitch() const;
```

#### 参数设置

```cpp
// 移动速度
void setMovementSpeed(float speed);
float getMovementSpeed() const;

// 鼠标灵敏度
void setMouseSensitivity(float sensitivity);
float getMouseSensitivity() const;

// 重力
void setGravity(float g);
float getGravity() const;

// 默认地面高度（无地形时使用）
void setDefaultGroundHeight(float h);
float getDefaultGroundHeight() const;

// 跳跃力
void setJumpForce(float f);
float getJumpForce() const;
```

#### 着地状态

```cpp
bool isGrounded() const;
void setGrounded(bool grounded);
```

#### 地形查询

```cpp
// 设置地形高度查询回调
void setTerrainQuery(CameraTerrainQuery query);
void clearTerrainQuery();

// 碰撞箱管理
void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
void clearCollisionBoxes();
```

#### 第三人称模式

```cpp
// 设置跟踪目标
void setTarget(const glm::vec3& target);
glm::vec3 getTarget() const;

// 设置相机距离
void setThirdPersonDistance(float d);
float getThirdPersonDistance() const;
```

#### 视锥体

```cpp
// 获取视锥体
const Frustum& getFrustum() const;

// 更新视锥体（每帧调用）
void updateFrustum();
```

---

## 使用示例

### 基本设置

```cpp
// 创建相机
Camera camera(800, 600);

// 设置参数
camera.setMovementSpeed(5.0f);
camera.setMouseSensitivity(0.1f);
camera.setDefaultGroundHeight(-1.5f);
```

### 主循环更新

```cpp
void mainLoop() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    
    // 更新相机
    camera.update(deltaTime,
                  input->isForwardPressed(),
                  input->isBackPressed(),
                  input->isLeftPressed(),
                  input->isRightPressed(),
                  input->isJumpPressed(),
                  input->isFreeCameraJustPressed(),
                  input->isSprintPressed(),
                  false);
    
    // 处理鼠标移动
    double deltaX, deltaY;
    input->getMouseDelta(deltaX, deltaY);
    camera.processMouseMovement(deltaX, deltaY);
    
    // 更新视锥体
    camera.updateFrustum();
}
```

### 获取矩阵

```cpp
// 获取视图和投影矩阵
glm::mat4 view = camera.getViewMatrix();
glm::mat4 projection = camera.getProjectionMatrix();

// 在着色器中使用
glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
```

### 地形高度查询

```cpp
// 设置地形查询回调
camera.setTerrainQuery([](float x, float z) -> float {
    // 返回该位置的地形高度
    return terrainSystem.getHeight(x, z);
});

// 清除地形查询
camera.clearTerrainQuery();
```

### 视锥体剔除

```cpp
// 更新视锥体
camera.updateFrustum();

// 获取视锥体
const Frustum& frustum = camera.getFrustum();

// 检查物体是否可见
if (frustum.isSphereInside(objectPosition, objectRadius)) {
    // 渲染物体
    renderObject(object);
}

// 检查包围盒
if (frustum.isAABBInside(aabbMin, aabbMax)) {
    // 渲染物体
    renderObject(object);
}
```

### 第三人称模式

```cpp
// 切换到第三人称
camera.setMode(Camera::Mode::ThirdPerson);

// 设置跟踪目标
camera.setTarget(playerPosition);
camera.setThirdPersonDistance(5.0f);

// 相机会自动跟踪目标
```

---

## 模式说明

### 第一人称模式

- 相机位于角色头部位置
- 相机方向与角色面向方向一致
- 适合射击类游戏

### 第三人称模式

- 相机位于角色后上方
- 可以看到角色本身
- 相机自动跟踪目标位置

### 自由视角模式

- 相机可自由移动，不受地形限制
- 飞行模式，无重力
- 适合编辑器或观察模式

---

## 物理系统

相机内置简单的物理系统：

1. **重力**：角色会自动下落直到着地
2. **跳跃**：着地状态下可以跳跃
3. **地形跟随**：通过地形查询回调实现

### 物理参数

```cpp
camera.setGravity(15.0f);           // 重力加速度
camera.setJumpForce(5.5f);          // 跳跃初速度
camera.setDefaultGroundHeight(-1.5f); // 无地形时的地面高度
```
