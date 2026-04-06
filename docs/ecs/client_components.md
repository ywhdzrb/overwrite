# 客户端组件

客户端组件是客户端专用的数据结构，定义在 `client/include/ecs/client_components.h`。

---

## CameraComponent

相机组件，存储相机参数和缓存的矩阵。

```cpp
struct CameraComponent {
    float fov{45.0f};
    float nearPlane{0.1f};
    float farPlane{100.0f};
    int viewportWidth{800};
    int viewportHeight{600};
    
    // 缓存的矩阵
    mutable glm::mat4 viewMatrix{1.0f};
    mutable glm::mat4 projectionMatrix{1.0f};
    mutable bool dirty{true};
    
    glm::mat4 getViewMatrix(const TransformComponent& transform) const;
    glm::mat4 getProjectionMatrix() const;
};
```

### 使用示例

```cpp
// 添加相机组件
world.registry().emplace<CameraComponent>(entity,
    45.0f,    // fov
    0.1f,     // nearPlane
    100.0f,   // farPlane
    1280,     // viewportWidth
    720       // viewportHeight
);

// 获取矩阵
auto& camera = world.registry().get<CameraComponent>(cameraEntity);
auto& transform = world.registry().get<TransformComponent>(cameraEntity);
glm::mat4 view = camera.getViewMatrix(transform);
glm::mat4 proj = camera.getProjectionMatrix();
```

---

## CameraControllerComponent

相机控制器组件，存储相机控制参数。

```cpp
struct CameraControllerComponent {
    float movementSpeed{5.0f};
    float sprintMultiplier{2.0f};
    float mouseSensitivity{0.1f};
    float freeCameraSpeed{15.0f};
    
    // 模式
    bool freeCameraMode{false};
    bool isMainCamera{true};
    
    // 第三人称模式下的移动方向（由 Camera 同步）
    glm::vec3 cameraFront{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraRight{1.0f, 0.0f, 0.0f};
};
```

### 使用示例

```cpp
// 添加相机控制器
world.registry().emplace<CameraControllerComponent>(entity,
    5.0f,    // movementSpeed
    2.0f,    // sprintMultiplier
    0.1f,    // mouseSensitivity
    15.0f,   // freeCameraSpeed
    false,   // freeCameraMode
    true     // isMainCamera
);

// 切换自由视角模式
auto& controller = world.registry().get<CameraControllerComponent>(entity);
controller.freeCameraMode = !controller.freeCameraMode;
```

---

## RenderComponent

渲染组件，存储渲染相关数据。

```cpp
struct RenderComponent {
    // 模型路径
    std::string modelPath;
    
    // 变换参数
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};  // 欧拉角度数
    glm::vec3 scale{1.0f};
    
    // 可见性
    bool visible{true};
    bool castShadows{true};
    bool receiveShadows{true};
    
    // 是否需要更新
    bool dirty{true};
};
```

### 使用示例

```cpp
// 添加渲染组件
world.registry().emplace<RenderComponent>(entity,
    "assets/models/player.glb",  // modelPath
    glm::vec3(0.0f),             // position
    glm::vec3(0.0f),             // rotation
    glm::vec3(1.0f),             // scale
    true,                        // visible
    true,                        // castShadows
    true                         // receiveShadows
);

// 在渲染系统中使用
auto view = world.registry().view<RenderComponent>();
for (auto entity : view) {
    auto& render = view.get<RenderComponent>(entity);
    if (render.visible) {
        renderModel(render.modelPath, render.position, render.rotation, render.scale);
    }
}
```

---

## LightComponent

光源组件，存储光源数据。

```cpp
struct LightComponent {
    enum class Type { Directional, Point, Spot };
    
    Type type{Type::Point};
    glm::vec3 color{1.0f};
    float intensity{1.0f};
    
    // 聚光灯/方向光参数
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    
    // 点光源/聚光灯参数
    float constant{1.0f};
    float linear{0.09f};
    float quadratic{0.032f};
    
    // 聚光灯参数
    float innerCutoff{12.5f};  // 度数
    float outerCutoff{17.5f};  // 度数
    
    bool enabled{true};
    bool castShadows{false};
};
```

### 使用示例

```cpp
// 创建方向光
entt::entity sun = world.createEntity();
world.registry().emplace<LightComponent>(sun,
    LightComponent::Type::Directional,
    glm::vec3(1.0f, 0.95f, 0.9f),  // 暖白色
    1.0f,                          // intensity
    glm::vec3(-0.5f, -1.0f, -0.3f) // direction
);
world.registry().emplace<TransformComponent>(sun);

// 创建点光源
entt::entity lamp = world.createEntity();
world.registry().emplace<LightComponent>(lamp,
    LightComponent::Type::Point,
    glm::vec3(1.0f, 0.8f, 0.6f),  // 暖色
    2.0f                          // intensity
);
world.registry().emplace<TransformComponent>(lamp,
    glm::vec3(5.0f, 3.0f, 5.0f)
);

// 创建聚光灯
entt::entity flashlight = world.createEntity();
world.registry().emplace<LightComponent>(flashlight,
    LightComponent::Type::Spot,
    glm::vec3(1.0f),
    5.0f,
    glm::vec3(0.0f, -1.0f, 0.0f),  // direction
    1.0f, 0.09f, 0.032f,           // attenuation
    15.0f,                         // innerCutoff
    20.0f                          // outerCutoff
);
```

---

## 创建客户端玩家

结合共享组件和客户端组件创建完整玩家：

```cpp
entt::entity createClientPlayer(World& world, int viewportWidth, int viewportHeight) {
    auto entity = world.createEntity();
    
    // 共享组件
    world.registry().emplace<TransformComponent>(entity);
    world.registry().emplace<VelocityComponent>(entity);
    world.registry().emplace<PhysicsComponent>(entity);
    world.registry().emplace<InputStateComponent>(entity);
    world.registry().emplace<MovementControllerComponent>(entity);
    world.registry().emplace<PlayerTag>(entity, 0);
    world.registry().emplace<NetworkSyncComponent>(entity);
    world.registry().emplace<EntityTypeComponent>(entity, EntityType::Player);
    
    // 客户端专用组件
    world.registry().emplace<CameraComponent>(entity,
        45.0f, 0.1f, 100.0f, viewportWidth, viewportHeight
    );
    world.registry().emplace<CameraControllerComponent>(entity);
    world.registry().emplace<RenderComponent>(entity,
        "assets/models/player.glb"
    );
    
    world.setPlayer(entity);
    world.setMainCamera(entity);
    
    return entity;
}
```

---

## 组件依赖关系

```
玩家实体组件结构:

共享组件:
├── TransformComponent      (位置、旋转、缩放)
├── VelocityComponent       (速度)
├── PhysicsComponent        (物理状态)
├── InputStateComponent     (输入状态)
├── MovementControllerComponent (移动参数)
├── PlayerTag               (玩家标识)
├── NetworkSyncComponent    (网络同步)
└── EntityTypeComponent     (实体类型)

客户端专用组件:
├── CameraComponent         (相机参数)
├── CameraControllerComponent (相机控制)
└── RenderComponent         (渲染数据)
```
