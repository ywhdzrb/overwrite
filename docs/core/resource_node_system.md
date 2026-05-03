# 资源节点系统

资源节点系统管理世界环境中可采集的资源实例（树木、岩石等），为玩家提供统一的采集交互接口。

## 文件位置

- **头文件**：`include/core/resource_node_system.h`
- **实现**：`src/game_session/resource_node_system.cpp`

## 设计目的

在 OverWrite 中，树木和岩石由 `TreeSystem` / `StoneSystem` 负责渲染和区块管理，但采集逻辑需要额外的状态（剩余采集次数、类型、是否已耗尽）。资源节点系统作为一层轻量数据，将「渲染实例」与「可采集状态」解耦：

- `TreeSystem` / `StoneSystem`：只负责树/石的**视觉呈现**和**区块加载**
- `ResourceNodeSystem`：负责树/石的**可采集属性**（类型、剩余次数、耗尽隐藏）

## 核心数据结构

### ResourceNode

```cpp
struct ResourceNode {
    glm::vec3 position{0.0f};           // 世界坐标
    ecs::ResourceType type;             // 资源类型（Wood / Stone 等）
    uint32_t maxHarvests{5};            // 总可采集次数
    uint32_t remainingHarvests{5};      // 剩余采集次数
    int sourceIndex{-1};                // 在源系统（树/石）中的槽位索引
    ResourceSourceType sourceType;      // 来源类型（Tree / Stone）
};
```

### ResourceSourceType

```cpp
enum class ResourceSourceType : uint8_t {
    None,
    Tree,
    Stone,
};
```

用于关联资源节点与对应的渲染实例。当节点耗尽时，通过 `sourceIndex` + `sourceType` 找到对应的树/石并隐藏。

## ResourceNodeSystem API

```cpp
class ResourceNodeSystem {
public:
    // 初始化：从现有树/石系统填充资源节点
    void init(TreeSystem* treeSystem, StoneSystem* stoneSystem);

    // 每帧更新：定期同步新区块中的树/石实例
    void update(float dt);

    // 清理所有资源节点
    void clear();

    // 查询指定圆形区域内的可采集节点
    std::vector<ResourceNode*> queryNodes(float x, float z, float radius);

    // 在指定位置尝试采集（返回 ItemStack）
    ecs::ItemStack harvest(const glm::vec3& position, float harvestRange = 2.0f);

    // 获取统计
    size_t getNodeCount() const;
    const std::vector<ResourceNode>& getNodes() const;
};
```

## 生命周期

```
GameSession::init()
  └─ resourceNodeSystem_.init(treeSystem, stoneSystem)
       ├─ populateFromTrees()   — 遍历所有活跃树木，为每棵创建资源节点
       └─ populateFromStones()  — 同理，为每块岩石创建资源节点

GameSession::update(dt)
  └─ resourceNodeSystem_.update(dt)
       ├─ reconcile()           — 每 ~1s 扫描树/石系统的新增实例
       └─ (采集逻辑在 Phase 11 中通过 harvest() 调用)

GameSession 析构
  └─ resourceNodeSystem_.clear()
```

## 关键机制

### 1. 动态区块同步（reconcile）

当玩家移动时，`TreeSystem` / `StoneSystem` 会动态加载新区块。这些新实例在 `init()` 时不存在，因此需要定期扫描。

```cpp
void reconcile() {
    // 遍历所有树实例，为尚未跟踪的创建节点
    for (const auto& instance : treeSystem_->getTreeInstances()) {
        if (instance.active && !findNodeBySource(ResourceSourceType::Tree, index)) {
            ResourceNode node;
            node.position = instance.position;
            node.type = ResourceType::Wood;
            node.maxHarvests = 3 + rand() % 5;
            node.remainingHarvests = node.maxHarvests;
            node.sourceIndex = index;
            node.sourceType = ResourceSourceType::Tree;
            nodes_.push_back(node);
        }
    }
    // 同理处理石块...
}
```

`reconcile()` 每 `~1.0s` 触发一次，避免每帧遍历的性能开销。

### 2. 采集与隐藏

```cpp
// 在 GameSession::updateHarvestTarget() 中
// 1. 使用玩家位置进行 3m 距离判定（不要求准星对准）
const float maxRange = 3.0f;
auto nearby = resourceNodeSystem_.queryNodes(playerPos.x, playerPos.z, maxRange);

// 2. 找到最近的可采集节点
for (auto* node : nearby) {
    if (node->remainingHarvests > 0) {
        float dist = glm::length(node->position - playerPos);
        if (dist < bestDist) { bestNode = node; bestDist = dist; }
    }
}

// 3. 按 F 键触发采集
if (isKeyJustPressed(GLFW_KEY_F)) {
    auto result = resourceNodeSystem_.harvest(playerPos_, 3.0f);
    // 加入背包...
}
```

采集耗尽时（`remainingHarvests` 归零），调用 `hideSource()` 隐藏对应的树/石实例（`active = false`），永久消失。

### 3. 采集规则

- **按键**：F 键（原飞行模式移至 R 键）
- **距离**：纯距离判定，3m 范围，找最近节点（无需准星对准）
- **反馈**：HUD 显示 "[F] 采集 木材/石材" 提示
- **重生**：不重生，耗尽即永久消失

## 与 GameSession 的集成

相关代码在 `GameSession::update()` 中：

| 阶段 | 功能 | 位置 |
|------|------|------|
| Phase 3 | `resourceNodeSystem_.update(dt)` | 地形更新后，同步新区块 |
| Phase 11 | `updateHarvestTarget()` 近距检测 | 更新 HUD 提示 |
| F 键 | `harvest()` + 加入背包 | 玩家交互响应 |
| Phase 10 | `E 键` 开关背包 | 背包界面管理 |

## 当前限制

1. **仅支持 Wood / Stone**：IronOre、CopperOre、PlantFiber 类型已定义但尚未有对应的世界实例
2. **无独立矿脉**：资源节点依附于地形系统现有的树/石实例，独立放置的矿脉（Step 6）尚未实现
3. **采集速度**：每次 F 键采集 1 单位，无动画/进度条
