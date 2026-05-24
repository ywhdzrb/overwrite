#pragma once

// 标准库
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace owengine {

class TreeSystem;
class StoneSystem;

namespace ecs {
enum class ResourceType : uint8_t;
struct ItemStack;
} // namespace ecs

/// 资源节点的来源类型（用于关联树/石实例隐藏/显示）
enum class ResourceSourceType : uint8_t {
    None,
    Tree,
    Stone,
};

/**
 * @brief 资源节点 — 单个可采集资源实例
 *
 * 由 ResourceNodeSystem 统一管理，用于玩家采集交互。
 * 耗尽后对应的树/石永久消失（不重生）。
 *
 * 生命周期：
 *   - 由 ResourceNodeSystem::init() 创建（从树/石位置填充）
 *   - ResourceNodeSystem::harvest() 消耗 remainingHarvests
 *   - 归零后隐藏对应的树/石（永久消失）
 */
struct ResourceNode {
    glm::vec3 position{0.0f};
    ecs::ResourceType type;
    uint32_t maxHarvests{5};           // 总可采集次数
    uint32_t remainingHarvests{5};     // 剩余采集次数
    int sourceIndex{-1};               // 在源系统（树/石）中的槽位索引
    ResourceSourceType sourceType{ResourceSourceType::None};  // 来源类型
};

/**
 * @brief 资源节点系统
 *
 * 管理与世界环境（树、石等）关联的可采集资源实例。
 * 生命周期：在 GameSession::init 中创建并填充，
 * 在 GameSession::update 中每帧更新重生计时。
 *
 * 线程安全：所有方法在主线程调用，不涉及 GPU 资源。
 */
class ResourceNodeSystem {
public:
    ResourceNodeSystem() = default;
    ~ResourceNodeSystem() = default;

    // 禁止拷贝
    ResourceNodeSystem(const ResourceNodeSystem&) = delete;
    ResourceNodeSystem& operator=(const ResourceNodeSystem&) = delete;

    // 允许移动
    ResourceNodeSystem(ResourceNodeSystem&&) noexcept = default;
    ResourceNodeSystem& operator=(ResourceNodeSystem&&) noexcept = default;

    /**
     * @brief 初始化：从现有树/石系统填充资源节点
     * @param treeSystem 树木系统指针（用于查询树木位置）
     * @param stoneSystem 石头系统指针（用于查询石头位置）
     */
    void init(TreeSystem* treeSystem, StoneSystem* stoneSystem);

/**
 * @brief 每帧更新：定期扫描树/石系统的新实例，同步资源节点
 *
 * 当玩家移动时 TreeSystem/StoneSystem 会动态加载新区块，
 * 新加载的树/石实例需要自动创建对应的资源节点。
 * reconcile() 每隔约 1 秒扫描一次。
 *
 * @param dt 帧间隔（秒）
 */
void update(float dt);

    /** @brief 清理所有资源节点 */
    void clear();

    /**
     * @brief 查询指定圆形区域内的可采集资源节点
     * @param x,z 世界坐标
     * @param radius 查询半径（米）
     * @return 可采集资源节点指针列表
     */
    std::vector<ResourceNode*> queryNodes(float x, float z, float radius);

    /**
     * @brief 尝试在指定位置采集一次资源
     * @param position 采集位置（玩家世界坐标）
     * @param harvestRange 采集范围（米），默认 2.0
     * @return 成功时返回 ItemStack（类型 + 数量 1），失败返回空堆叠
     */
    ecs::ItemStack harvest(const glm::vec3& position, float harvestRange = 2.0f);

    /** @brief 获取总节点数量 */
    size_t getNodeCount() const { return nodes_.size(); }

    /** @brief 获取所有节点的只读引用 */
    const std::vector<ResourceNode>& getNodes() const { return nodes_; }

private:
    std::vector<ResourceNode> nodes_;
    TreeSystem* treeSystem_ = nullptr;
    StoneSystem* stoneSystem_ = nullptr;
    float reconcileTimer_ = 0.0f;   // 累计时间，每 1s 触发 reconcile

    void populateFromTrees(TreeSystem* treeSystem);
    void populateFromStones(StoneSystem* stoneSystem);

    /** @brief 定期同步：为树/石系统新增实例创建资源节点 */
    void reconcile();

    /** @brief 按来源类型和索引查找已有资源节点（未耗尽或已耗尽都返回） */
    ResourceNode* findNodeBySource(ResourceSourceType type, int index);

    /** @brief 根据 sourceIndex/sourceType 隐藏对应的树或石 */
    void hideSource(const ResourceNode& node);
    /** @brief 根据 sourceIndex/sourceType 显示对应的树或石（用于清理恢复） */
    void showSource(const ResourceNode& node);
};

} // namespace owengine
