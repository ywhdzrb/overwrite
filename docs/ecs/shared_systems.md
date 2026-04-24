# 共享系统

共享系统是客户端和服务端共用的逻辑处理类，定义在 `shared/include/ecs/systems.h`。

---

## World

世界管理器，管理所有实体和组件。

### 类定义

```cpp
class World {
public:
    World();
    ~World() = default;
    
    // 禁止拷贝
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    
    // 实体管理
    entt::entity createEntity();
    void destroyEntity(entt::entity entity);
    
    // 注册表访问
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    
    // 特殊实体
    entt::entity getMainCamera() const { return mainCamera_; }
    void setMainCamera(entt::entity camera) { mainCamera_ = camera; }
    entt::entity getPlayer() const { return player_; }
    void setPlayer(entt::entity player) { player_ = player; }
    
    // 便捷方法
    entt::entity createPlayer();
    
private:
    entt::registry registry_;
    entt::entity mainCamera_{entt::null};
    entt::entity player_{entt::null};
};
```

### 使用示例

```cpp
// 创建世界
World world;

// 创建实体
entt::entity entity = world.createEntity();

// 添加组件
world.registry().emplace<TransformComponent>(entity);

// 获取组件
auto& transform = world.registry().get<TransformComponent>(entity);

// 销毁实体
world.destroyEntity(entity);

// 创建预设玩家
entt::entity player = world.createPlayer();
world.setPlayer(player);
```

---

## MovementSystem

移动系统，根据输入更新实体位置和旋转。

### 类型定义

```cpp
// 地形高度查询函数类型
using TerrainHeightQuery = std::function<float(float x, float z)>;
```

### 斜坡运动

当玩家着地且地面法向量偏离垂直方向（`dot(groundNormal, up) < 0.999`）时，
MovementSystem 将水平输入投影到坡面上：

```
v_proj = v - n * (v · n)
```

其中 `n` 为 PhysicsSystem 在上帧计算出的 `groundNormal`。投影后的速度保留 Y 分量，
直接更新 `transform.position.xyz`，使角色沿地形轮廓平滑移动，避免水平移动导致的穿透/悬空锯齿。

PhysicsSystem 仅在容差范围（±0.1f）内做微小修正。两者配合实现斜坡上的平滑运动。

### 类定义

```cpp
class MovementSystem {
public:
    explicit MovementSystem(World& world);
    
    void update(float deltaTime);
    
    // 碰撞体管理
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
    void clearCollisionBoxes();
    const std::vector<std::pair<glm::vec3, glm::vec3>>& getCollisionBoxes() const;
    
private:
    World& world_;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;
    
    // 碰撞检测
    bool checkCollision(const glm::vec3& position, const glm::vec3& playerSize) const;
    glm::vec3 resolveCollision(const glm::vec3& oldPos, const glm::vec3& newPos, 
                               const glm::vec3& playerSize) const;
    
    void updateMovement(entt::entity entity, TransformComponent& transform,
                       VelocityComponent& velocity,
                       MovementControllerComponent& controller,
                       const InputStateComponent& input,
                       float deltaTime);
};
```

### 使用示例

```cpp
World world;
MovementSystem movementSystem(world);

// 添加碰撞体
movementSystem.addCollisionBox(glm::vec3(0, 0, 0), glm::vec3(2, 2, 2));
movementSystem.addCollisionBox(glm::vec3(5, 0, 5), glm::vec3(1, 3, 1));

// 主循环
void gameLoop(float deltaTime) {
    movementSystem.update(deltaTime);
}
```

### 处理逻辑

系统处理具有以下组件的实体：
- `TransformComponent`
- `VelocityComponent`
- `MovementControllerComponent`
- `InputStateComponent`

```cpp
void MovementSystem::update(float deltaTime) {
    auto view = world_.registry().view<
        TransformComponent, 
        VelocityComponent,
        MovementControllerComponent, 
        InputStateComponent
    >();
    
    for (auto entity : view) {
        auto [transform, velocity, controller, input] = view.get(entity);
        updateMovement(entity, transform, velocity, controller, input, deltaTime);
    }
}
```

---

## PhysicsSystem

物理系统，处理重力、碰撞等物理模拟。

### 类定义

```cpp
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
    bool hasTerrainQuery() const;
    
    // 默认地面高度
    void setDefaultGroundHeight(float height);
    float getDefaultGroundHeight() const;
    
private:
    World& world_;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;
    TerrainHeightQuery terrainQuery_;
    float defaultGroundHeight_{-1.5f};
    
    bool checkGroundCollision(const glm::vec3& position, float height) const;
    bool checkAABBCollision(const glm::vec3& pos1, const glm::vec3& size1,
                           const glm::vec3& pos2, const glm::vec3& size2) const;
    // 查询地形高度（内部含单帧缓存，同一帧同一坐标只计算一次）
    float queryTerrainHeight(float x, float z) const;
    // 用中心差分法计算地形表面法向量（centerHeight 由调用方预查询，避免中心点重复计算）
    glm::vec3 computeTerrainNormal(float x, float z, float centerHeight) const;

    // 每帧单条目高度缓存，mutable 仅用于性能优化
    mutable bool cachedQueryValid_{false};
    mutable float cachedQueryX_{0.0f};
    mutable float cachedQueryZ_{0.0f};
    mutable float cachedQueryHeight_{0.0f};
};
```

### 使用示例

```cpp
World world;
PhysicsSystem physicsSystem(world);

// 设置默认地面高度
physicsSystem.setDefaultGroundHeight(-1.5f);

// 设置地形查询
physicsSystem.setTerrainQuery([](float x, float z) -> float {
    return terrain.getHeight(x, z);
});

// 添加碰撞体
physicsSystem.addCollisionBox(glm::vec3(0, 0, 0), glm::vec3(2, 2, 2));

// 主循环
void gameLoop(float deltaTime) {
    physicsSystem.update(deltaTime);
}
```

### 处理逻辑

系统处理具有以下组件的实体：
- `TransformComponent`
- `VelocityComponent`
- `PhysicsComponent`

```cpp
void PhysicsSystem::update(float deltaTime) {
    auto view = world_.registry().view<
        TransformComponent, 
        VelocityComponent,
        PhysicsComponent
    >();
    
    for (auto entity : view) {
        auto [transform, velocity, physics] = view.get(entity);
        
        // 应用重力
        if (!physics.isGrounded && physics.useGravity) {
            velocity.linear.y -= physics.gravity * deltaTime;
        }
        
        // 更新位置
        transform.position += velocity.linear * deltaTime;
        
        // 地面检测
        float groundHeight = queryTerrainHeight(transform.position.x, 
                                                transform.position.z);
        if (transform.position.y <= groundHeight) {
            transform.position.y = groundHeight;
            physics.isGrounded = true;
            physics.isJumping = false;
            velocity.linear.y = 0.0f;
        }
    }
}
```

---

## 设计说明

### 系统更新顺序

更新顺序在 `client_systems.cpp` 中定义：

```cpp
void ClientWorld::updateClientSystems(float deltaTime) {
    // 1. 输入系统
    inputSystem_->update(deltaTime);
    // 2. 移动系统（水平输入投影到坡面 → 更新 position.xyz）
    movementSystem_->update(deltaTime);
    // 3. 物理系统（重力/着地/跳跃 + 容差范围内的微小修正）
    physicsSystem_->update(deltaTime);
    // 4. 相机系统
    cameraControllerSystem_->update(deltaTime);
}
```

MovementSystem 先运行，将输入投影到坡面并更新所有三维坐标；
PhysicsSystem 后运行，仅做重力/跳跃/容差修正。两者不互相覆盖 Y 坐标。

### 地形高度查询

物理系统使用 `isGrounded` 状态控制重力应用，而非坐标比较：

- `groundHeight`：动态值，由地形系统更新
- `groundNormal`：由 `computeTerrainNormal()` 用中心差分法计算
- `isGrounded`：核心状态，决定是否应用重力
- 通过 `setTerrainQuery()` 注入地形系统委托（如 `TerrainRenderer::getHeight`）

### 高度缓存

`queryTerrainHeight()` 包含每帧单条目坐标缓存：
同一帧内重复查询相同 `(x, z)` 坐标时直接返回缓存值，
避免重复的 Perlin 噪声计算。尤其 `computeTerrainNormal()` 需要查询
中心点 + 4 个邻居点，其中 4 个邻居点可能被后续正常查询命中缓存。
传入 `centerHeight` 参数进一步消除中心点重复查询。

### 碰撞检测

使用 AABB（轴对齐包围盒）进行碰撞检测：

```cpp
bool checkAABBCollision(
    const glm::vec3& pos1, const glm::vec3& size1,
    const glm::vec3& pos2, const glm::vec3& size2
) const;
```

### 系统顺序

推荐的系统更新顺序：

```cpp
void updateSystems(float deltaTime) {
    // 1. 输入系统（客户端）
    inputSystem.update(deltaTime);
    
    // 2. 移动系统
    movementSystem.update(deltaTime);
    
    // 3. 物理系统
    physicsSystem.update(deltaTime);
    
    // 4. 其他系统...
}
```

---

## 组件依赖

| 系统 | 必需组件 | 可选组件 |
|------|----------|----------|
| MovementSystem | TransformComponent, VelocityComponent, MovementControllerComponent, InputStateComponent | - |
| PhysicsSystem | TransformComponent, VelocityComponent, PhysicsComponent | - |

---

## 扩展系统

可以创建自定义系统：

```cpp
class HealthSystem {
public:
    explicit HealthSystem(World& world) : world_(world) {}
    
    void update(float deltaTime) {
        auto view = world_.registry().view<HealthComponent>();
        for (auto entity : view) {
            auto& health = view.get<HealthComponent>(entity);
            if (health.current <= 0) {
                // 处理死亡
                world_.destroyEntity(entity);
            }
        }
    }
    
private:
    World& world_;
};
```
