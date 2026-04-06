# 共享组件

共享组件是客户端和服务端共用的数据结构，定义在 `shared/include/ecs/components.h`。

---

## TransformComponent

变换组件，存储实体的位置、旋转和缩放。

```cpp
struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // w, x, y, z
    glm::vec3 scale{1.0f};
    
    // 欧拉角（用于相机等需要欧拉角控制的场景）
    float yaw{0.0f};
    float pitch{0.0f};
    float roll{0.0f};
    
    // 获取前向量（基于欧拉角）
    glm::vec3 getFront() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;
    
    // 从欧拉角更新四元数
    void updateRotationFromEuler();
    
    // 获取模型矩阵
    glm::mat4 getModelMatrix() const;
};
```

### 使用示例

```cpp
// 创建实体并添加变换组件
auto entity = world.createEntity();
world.registry().emplace<TransformComponent>(entity,
    glm::vec3(0.0f, 1.0f, 5.0f),  // position
    glm::quat(1, 0, 0, 0),        // rotation
    glm::vec3(1.0f)               // scale
);

// 获取和修改
auto& transform = world.registry().get<TransformComponent>(entity);
transform.position.x += 5.0f;
transform.yaw = 45.0f;
transform.updateRotationFromEuler();

// 获取模型矩阵
glm::mat4 modelMatrix = transform.getModelMatrix();
```

---

## VelocityComponent

速度组件，存储线性和角速度。

```cpp
struct VelocityComponent {
    glm::vec3 linear{0.0f};    // 线性速度
    glm::vec3 angular{0.0f};   // 角速度
};
```

### 使用示例

```cpp
// 添加速度
auto& velocity = world.registry().emplace<VelocityComponent>(entity);
velocity.linear = glm::vec3(1.0f, 0.0f, 0.0f);  // 向右移动

// 在物理系统中更新位置
auto view = world.registry().view<TransformComponent, VelocityComponent>();
for (auto entity : view) {
    auto [transform, velocity] = view.get(entity);
    transform.position += velocity.linear * deltaTime;
}
```

---

## PhysicsComponent

物理组件，存储物理属性和状态。

```cpp
struct PhysicsComponent {
    float gravity{15.0f};
    float groundHeight{-1.5f};      // 当前站立面高度
    float jumpForce{5.5f};
    
    bool isJumping{false};          // 是否正在跳跃
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

### 物理系统设计说明

- `groundHeight`：当前站立面的高度（动态更新，由地形系统/碰撞检测设置）
- `isGrounded`：是否着地（核心状态，决定是否应用重力）
- 跳跃判定：仅在 `isGrounded=true` 时允许跳跃

### 使用示例

```cpp
// 添加物理组件
world.registry().emplace<PhysicsComponent>(entity,
    15.0f,    // gravity
    -1.5f,    // groundHeight
    5.5f,     // jumpForce
    false,    // isJumping
    true,     // isGrounded
    true      // useGravity
);

// 检查是否可以跳跃
auto& physics = world.registry().get<PhysicsComponent>(entity);
if (physics.isGrounded && input.jump) {
    physics.isJumping = true;
    physics.isGrounded = false;
    velocity.linear.y = physics.jumpForce;
}
```

---

## InputStateComponent

输入组件，存储输入状态。

```cpp
struct InputStateComponent {
    // 移动输入
    bool moveForward{false};
    bool moveBackward{false};
    bool moveLeft{false};
    bool moveRight{false};
    bool jump{false};
    bool sprint{false};
    bool freeCameraToggle{false};
    bool spaceHeld{false};
    bool shiftHeld{false};
    
    // 鼠标输入
    float mouseDeltaX{0.0f};
    float mouseDeltaY{0.0f};
    
    // 重置输入状态
    void reset();
};
```

### 使用示例

```cpp
// 客户端：从输入设备更新
auto& input = world.registry().get<InputStateComponent>(player);
input.moveForward = glfwGetKey(window, GLFW_KEY_W);
input.moveBackward = glfwGetKey(window, GLFW_KEY_S);
input.jump = glfwGetKey(window, GLFW_KEY_SPACE);
// ...

// 服务端：从网络接收
void handleInputPacket(const json& packet) {
    auto& input = world.registry().get<InputStateComponent>(player);
    input.moveForward = packet["forward"];
    input.moveBackward = packet["backward"];
    // ...
}
```

---

## PlayerTag

玩家标签组件，用于标识玩家实体。

```cpp
struct PlayerTag {
    uint32_t playerId{0};
    uint32_t connectionId{0};  // 用于服务器关联连接
};
```

### 使用示例

```cpp
// 标记玩家实体
world.registry().emplace<PlayerTag>(entity, playerId, connectionId);

// 查找所有玩家
auto view = world.registry().view<PlayerTag, TransformComponent>();
for (auto entity : view) {
    auto [tag, transform] = view.get(entity);
    // 处理玩家...
}
```

---

## NameComponent

名称组件，存储实体名称。

```cpp
struct NameComponent {
    std::string name;
};
```

### 使用示例

```cpp
world.registry().emplace<NameComponent>(entity, "Player1");

// 按名称查找
auto view = world.registry().view<NameComponent>();
for (auto entity : view) {
    auto& name = view.get<NameComponent>(entity);
    if (name.name == "Player1") {
        // 找到了
    }
}
```

---

## MovementControllerComponent

移动控制器组件，存储移动参数。

```cpp
struct MovementControllerComponent {
    float movementSpeed{5.0f};
    float sprintMultiplier{2.0f};
    float mouseSensitivity{0.1f};
    
    // 第三人称模式下的移动方向（由相机同步）
    glm::vec3 moveFront{0.0f, 0.0f, -1.0f};
    glm::vec3 moveRight{1.0f, 0.0f, 0.0f};
};
```

### 使用示例

```cpp
// 添加移动控制器
world.registry().emplace<MovementControllerComponent>(entity,
    5.0f,    // movementSpeed
    2.0f,    // sprintMultiplier
    0.1f     // mouseSensitivity
);

// 在移动系统中使用
auto view = world.registry().view<TransformComponent, VelocityComponent,
                                  MovementControllerComponent, InputStateComponent>();
for (auto entity : view) {
    auto [transform, velocity, controller, input] = view.get(entity);
    
    float speed = controller.movementSpeed;
    if (input.sprint) speed *= controller.sprintMultiplier;
    
    if (input.moveForward) {
        velocity.linear += controller.moveFront * speed;
    }
    // ...
}
```

---

## NetworkSyncComponent

网络同步组件，标记需要网络同步的实体。

```cpp
struct NetworkSyncComponent {
    uint32_t networkId{0};         // 网络唯一ID
    uint32_t lastSyncFrame{0};     // 上次同步帧
    bool needsSync{true};          // 是否需要同步
    bool isOwned{false};           // 是否被本地玩家拥有
};
```

### 使用示例

```cpp
// 标记需要同步的实体
world.registry().emplace<NetworkSyncComponent>(entity,
    generateNetworkId(),  // networkId
    0,                    // lastSyncFrame
    true,                 // needsSync
    isLocalPlayer         // isOwned
);

// 在网络系统中处理同步
auto view = world.registry().view<TransformComponent, NetworkSyncComponent>();
for (auto entity : view) {
    auto [transform, sync] = view.get(entity);
    if (sync.needsSync && sync.isOwned) {
        // 发送位置到服务器
        sendPosition(sync.networkId, transform.position);
    }
}
```

---

## EntityTypeComponent

实体类型组件，标识实体类型。

```cpp
enum class EntityType : uint8_t {
    Unknown = 0,
    Player = 1,
    NPC = 2,
    Building = 3,
    Item = 4,
    Projectile = 5,
};

struct EntityTypeComponent {
    EntityType type{EntityType::Unknown};
};
```

### 使用示例

```cpp
// 设置实体类型
world.registry().emplace<EntityTypeComponent>(entity, EntityType::Player);

// 按类型过滤
auto players = world.registry().view<EntityTypeComponent, TransformComponent>();
for (auto entity : players) {
    auto [type, transform] = players.get(entity);
    if (type.type == EntityType::Player) {
        // 处理玩家
    }
}
```

---

## 组件组合示例

### 创建完整玩家

```cpp
entt::entity createPlayer(World& world, uint32_t playerId) {
    auto entity = world.createEntity();
    
    // 基础组件
    world.registry().emplace<TransformComponent>(entity,
        glm::vec3(0.0f),
        glm::quat(1, 0, 0, 0),
        glm::vec3(1.0f)
    );
    
    world.registry().emplace<VelocityComponent>(entity);
    
    world.registry().emplace<PhysicsComponent>(entity);
    
    world.registry().emplace<InputStateComponent>(entity);
    
    // 玩家标识
    world.registry().emplace<PlayerTag>(entity, playerId);
    
    world.registry().emplace<NameComponent>(entity, "Player" + std::to_string(playerId));
    
    // 移动控制
    world.registry().emplace<MovementControllerComponent>(entity);
    
    // 网络同步
    world.registry().emplace<NetworkSyncComponent>(entity, playerId, 0, true, true);
    
    // 类型标识
    world.registry().emplace<EntityTypeComponent>(entity, EntityType::Player);
    
    return entity;
}
```
