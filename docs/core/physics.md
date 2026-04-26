# 物理系统

物理系统提供基础的物理模拟，包括重力、地面检测和碰撞检测。

## 类概览

| 类名 | 文件 | 描述 |
|------|------|------|
| `Physics` | `include/core/physics.h` | 物理管理类 |

---

## Physics

处理基础物理模拟。

### 类型定义

```cpp
// 地形高度查询函数类型
using PhysicsTerrainQuery = std::function<float(float x, float z)>;
```

### 类方法

#### 构造

```cpp
Physics();
~Physics() = default;
```

#### 更新

```cpp
// 更新物理系统
void update(float deltaTime);
```

#### 碰撞检测

```cpp
// 检查地面碰撞
bool checkGroundCollision(const glm::vec3& position, float height) const;

// 检查墙壁碰撞
bool checkWallCollision(const glm::vec3& position, float radius) const;
```

#### 参数设置

```cpp
// 重力
void setGravity(float g);
float getGravity() const;

// 默认地面高度（无地形时使用）
void setGroundHeight(float height);
float getGroundHeight() const;
```

#### 地形查询

```cpp
// 设置地形高度查询回调
void setTerrainQuery(PhysicsTerrainQuery query);
void clearTerrainQuery();

// 查询地形高度
float queryTerrainHeight(float x, float z) const;
```

#### 碰撞体管理

```cpp
// 添加碰撞箱
void addCollisionBox(const glm::vec3& position, const glm::vec3& size);

// 清除所有碰撞箱
void clearCollisionBoxes();

// 获取所有碰撞箱
const std::vector<std::pair<glm::vec3, glm::vec3>>& getCollisionBoxes() const;
```

---

## 使用示例

### 基本设置

```cpp
// 创建物理系统
auto physics = std::make_unique<Physics>();

// 设置参数
physics->setGravity(15.0f);
physics->setGroundHeight(-1.5f);
```

### 地形查询

```cpp
// 设置地形高度查询
physics->setTerrainQuery([](float x, float z) -> float {
    // 返回该位置的地形高度
    return terrain.getHeight(x, z);
});

// 在某位置查询高度
float height = physics->queryTerrainHeight(10.0f, 20.0f);
```

### 碰撞箱

```cpp
// 添加静态碰撞体
physics->addCollisionBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));
physics->addCollisionBox(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(1.0f, 3.0f, 1.0f));

// 清除碰撞体
physics->clearCollisionBoxes();
```

### 地面检测

```cpp
// 检查玩家是否在地面上
glm::vec3 playerPos = player.getPosition();
float playerHeight = 1.8f;

if (physics->checkGroundCollision(playerPos, playerHeight)) {
    // 玩家在地面上
    player.setGrounded(true);
} else {
    // 玩家在空中
    player.setGrounded(false);
    // 应用重力
    player.applyGravity(deltaTime);
}
```

### 墙壁碰撞

```cpp
// 检查墙壁碰撞
glm::vec3 newPos = playerPos + velocity * deltaTime;

if (!physics->checkWallCollision(newPos, playerRadius)) {
    // 没有碰撞，可以移动
    player.setPosition(newPos);
} else {
    // 有碰撞，不移动或滑动
    // ... 处理滑动逻辑
}
```

---

## 与 Camera 的集成

物理系统通常与相机系统配合使用：

```cpp
// 设置相机的地形查询
camera.setTerrainQuery([physics](float x, float z) {
    return physics->queryTerrainHeight(x, z);
});

// 设置相机的碰撞箱
camera.clearCollisionBoxes();
for (const auto& box : physics->getCollisionBoxes()) {
    camera.addCollisionBox(box.first, box.second);
}
```

---

## 与 ECS 的集成

在 ECS 架构中，物理系统作为独立系统运行：

```cpp
// ECS 物理系统（shared/include/ecs/systems.h）
class PhysicsSystem {
public:
    explicit PhysicsSystem(World& world);
    
    void update(float deltaTime);
    
    // 碰撞体管理
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
    void clearCollisionBoxes();
    
    // 地形查询接口
    void setTerrainQuery(TerrainHeightQuery query);
    void clearTerrainQuery();
    
    // 默认地面高度
    void setDefaultGroundHeight(float height);
    float getDefaultGroundHeight() const;
};
```

### ECS 物理组件

```cpp
// shared/include/ecs/components.h
struct PhysicsComponent {
    float gravity{15.0f};
    float groundHeight{-1.5f};      // 当前站立面高度
    float jumpForce{5.5f};
    
    bool isJumping{false};
    bool isGrounded{true};          // 是否着地（核心状态）
    bool useGravity{true};
    
    // 碰撞体参数
    float colliderHeight{1.8f};
    float colliderRadius{0.3f};
    
    // 地形查询缓存
    float cachedTerrainHeight{-1.5f};
    bool terrainCacheValid{false};
};
```

---

## 实现细节

### AABB 碰撞检测

物理系统使用轴对齐包围盒（AABB）进行碰撞检测：

```cpp
private:
    bool checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                           const glm::vec3& pos2, const glm::vec3& size2) const;
```

### 地形高度查询

当没有设置地形查询回调时，使用默认地面高度：

```cpp
float queryTerrainHeight(float x, float z) const {
    if (terrainQuery) {
        return terrainQuery(x, z);
    }
    return defaultGroundHeight;
}
```

---

## 注意事项

1. **性能**：碰撞检测是 O(n) 复杂度，大量碰撞体可能影响性能
2. **精度**：AABB 碰撞是粗略检测，可能需要更精确的检测
3. **地形查询**：确保地形查询回调正确返回高度，否则角色会穿模
4. **帧率独立**：物理计算应考虑 deltaTime，确保不同帧率下行为一致
