# ECS 架构概述

本项目使用实体组件系统（Entity Component System，ECS）架构，基于 [EnTT](https://github.com/skypjack/entt) 库实现。

## 架构说明

ECS 是一种数据驱动的游戏架构模式，将游戏对象分解为三个核心概念：

- **Entity（实体）**：唯一标识符，本质上是一个 ID
- **Component（组件）**：纯数据结构，不包含逻辑
- **System（系统）**：处理具有特定组件的实体

### 优势

1. **组合优于继承**：实体通过组件组合实现功能
2. **数据导向**：组件数据连续存储，提高缓存命中率
3. **灵活性**：运行时动态添加/移除组件
4. **可扩展性**：新功能通过添加新组件和系统实现

## 项目结构

```
shared/
├── include/ecs/
│   ├── components.h    # 共享组件定义
│   └── systems.h       # 共享系统定义
└── src/ecs/
    ├── components.cpp
    └── systems.cpp

client/
├── include/ecs/
│   ├── client_components.h  # 客户端专用组件
│   └── client_systems.h     # 客户端专用系统
└── src/ecs/
    └── client_systems.cpp
```

## 核心概念

### 命名空间

```cpp
namespace vgame::ecs {
    // 共享 ECS 代码
}

namespace vgame::client {
    // 客户端专用代码
}

namespace vgame::server {
    // 服务端专用代码
}
```

### World（世界）

World 是 ECS 的核心管理器，包含所有实体和组件。

```cpp
// shared/include/ecs/systems.h
class World {
public:
    World();
    
    // 实体管理
    entt::entity createEntity();
    void destroyEntity(entt::entity entity);
    
    // 注册表访问
    entt::registry& registry();
    
    // 特殊实体
    entt::entity getMainCamera() const;
    void setMainCamera(entt::entity camera);
    entt::entity getPlayer() const;
    void setPlayer(entt::entity player);
    
    // 便捷方法
    entt::entity createPlayer();
    
private:
    entt::registry registry_;
    entt::entity mainCamera_;
    entt::entity player_;
};
```

### 实体操作

```cpp
// 创建实体
entt::entity entity = world.createEntity();

// 添加组件
world.registry().emplace<TransformComponent>(entity, 
    glm::vec3(0.0f),     // position
    glm::quat(1,0,0,0),  // rotation
    glm::vec3(1.0f)      // scale
);

// 获取组件
auto& transform = world.registry().get<TransformComponent>(entity);

// 检查组件
if (world.registry().all_of<TransformComponent>(entity)) {
    // 实体有 TransformComponent
}

// 移除组件
world.registry().remove<TransformComponent>(entity);

// 销毁实体
world.destroyEntity(entity);
```

### 组件查询

```cpp
// 遍历所有有 TransformComponent 的实体
auto view = world.registry().view<TransformComponent>();
for (auto entity : view) {
    auto& transform = view.get<TransformComponent>(entity);
    // 处理 transform
}

// 遍历有多个组件的实体
auto view = world.registry().view<TransformComponent, VelocityComponent>();
for (auto entity : view) {
    auto [transform, velocity] = view.get(entity);
    // 处理...
}

// 排除某些组件
auto view = world.registry().view<TransformComponent>(entt::exclude<PhysicsComponent>);
```

---

## 组件设计原则

1. **纯数据**：组件只包含数据，不包含方法（除了简单的获取器）
2. **小而专注**：每个组件负责单一功能
3. **可序列化**：组件应易于序列化，用于网络同步

### 示例：TransformComponent

```cpp
struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    
    float yaw{0.0f};
    float pitch{0.0f};
    float roll{0.0f};
    
    // 便捷方法（不包含复杂逻辑）
    glm::vec3 getFront() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;
    void updateRotationFromEuler();
    glm::mat4 getModelMatrix() const;
};
```

---

## 系统设计原则

1. **单一职责**：每个系统负责一个特定功能
2. **无状态**：系统不存储游戏状态，只处理数据
3. **查询驱动**：系统通过组件查询获取要处理的实体

### 示例：MovementSystem

```cpp
class MovementSystem {
public:
    explicit MovementSystem(World& world);
    
    void update(float deltaTime);
    
    // 碰撞体管理
    void addCollisionBox(const glm::vec3& position, const glm::vec3& size);
    void clearCollisionBoxes();
    
private:
    World& world_;
    std::vector<std::pair<glm::vec3, glm::vec3>> collisionBoxes_;
    
    void updateMovement(entt::entity entity, ...);
};
```

---

## 模块分离

### 共享模块（Shared）

共享模块包含客户端和服务端共用的代码：

- **组件**：TransformComponent, VelocityComponent, PhysicsComponent, InputStateComponent, PlayerTag, NameComponent, MovementControllerComponent, NetworkSyncComponent, EntityTypeComponent
- **系统**：World, MovementSystem, PhysicsSystem

### 客户端模块（Client）

客户端专用代码：

- **组件**：CameraComponent, CameraControllerComponent, RenderComponent, LightComponent
- **系统**：InputSystem, CameraSystem, ClientWorld

### 服务端模块（Server）

服务端专用代码：

- **类**：ServerWorld（扩展 World，添加玩家管理）

---

## 典型使用流程

### 创建世界和系统

```cpp
// 创建客户端世界
auto world = std::make_unique<ecs::ClientWorld>();
world->initClientSystems(window, 1280, 720);
```

### 创建玩家

```cpp
// 创建客户端玩家（带相机）
entt::entity player = world->createClientPlayer(1280, 720);

// 设置初始位置
auto& transform = world->registry().get<ecs::TransformComponent>(player);
transform.position = glm::vec3(0.0f, 0.0f, 10.0f);
```

### 主循环

```cpp
void gameLoop() {
    float deltaTime = getDeltaTime();
    
    // 更新输入系统
    world->getInputSystem()->update(deltaTime);
    
    // 更新移动系统
    world->getMovementSystem()->update(deltaTime);
    
    // 更新物理系统
    world->getPhysicsSystem()->update(deltaTime);
    
    // 更新相机系统
    world->getCameraSystem()->update(deltaTime);
    
    // 网络同步
    if (world->isConnectedToServer()) {
        world->getNetworkSystem()->update(deltaTime);
    }
    
    // 渲染
    render();
}
```

---

## 与传统 OOP 的对比

### 传统 OOP

```cpp
class Player : public GameObject {
    Transform transform;
    Velocity velocity;
    Physics physics;
    
    void update(float dt) {
        // 处理移动、物理等
    }
};

class Enemy : public GameObject {
    Transform transform;
    Health health;
    AI ai;
    
    void update(float dt) {
        // 处理 AI、移动等
    }
};
```

### ECS

```cpp
// 组件
struct TransformComponent { ... };
struct VelocityComponent { ... };
struct PhysicsComponent { ... };
struct HealthComponent { ... };
struct AIComponent { ... };

// 系统
class MovementSystem {
    void update(float dt) {
        // 处理所有有 TransformComponent + VelocityComponent 的实体
    }
};

class PhysicsSystem {
    void update(float dt) {
        // 处理所有有 PhysicsComponent 的实体
    }
};

// 创建玩家
entt::entity player = world.createEntity();
world.registry().emplace<TransformComponent>(player);
world.registry().emplace<VelocityComponent>(player);
world.registry().emplace<PhysicsComponent>(player);

// 创建敌人
entt::entity enemy = world.createEntity();
world.registry().emplace<TransformComponent>(enemy);
world.registry().emplace<HealthComponent>(enemy);
world.registry().emplace<AIComponent>(enemy);
```

---

## 参考资料

- [EnTT 文档](https://github.com/skypjack/entt/wiki)
- [ECS FAQ](https://github.com/SanderMertens/ecs-faq)
- [数据导向设计](https://blog.therocode.net/2018/09/data-oriented-design)
