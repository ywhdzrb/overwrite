#pragma once

// 标准库
#include <cstdint>
#include <string_view>
#include <array>
#include <algorithm>

namespace owengine {
namespace ecs {

/**
 * @brief 资源类型枚举
 * @note 新增资源类型时需同步更新 RESOURCE_DATA 表
 */
enum class ResourceType : uint8_t {
    None = 0,
    Wood,          // 木材 — 从树木采集
    Stone,         // 石材 — 从岩石采集
    IronOre,       // 铁矿石
    CopperOre,     // 铜矿石
    PlantFiber,    // 植物纤维 — 从草丛采集
};

/**
 * @brief 资源类型静态数据表
 * @note 索引与 ResourceType 枚举值一一对应
 */
struct ResourceData {
    ResourceType type;
    std::string_view name;
    std::string_view description;
    uint32_t maxStack;
    uint32_t maxPerSlot;  // 单格最大堆叠数
};

inline constexpr std::array<ResourceData, 6> RESOURCE_DATA_TABLE{{
    {ResourceType::None,       "",              "",                    0,  0},
    {ResourceType::Wood,       "木材",          "从树木采集的木材",     99, 99},
    {ResourceType::Stone,      "石材",          "从岩石采集的石材",     99, 99},
    {ResourceType::IronOre,    "铁矿石",        "铁矿石，可用于冶炼",   64, 64},
    {ResourceType::CopperOre,  "铜矿石",        "铜矿石，可用于冶炼",   64, 64},
    {ResourceType::PlantFiber, "植物纤维",      "从草丛采集的纤维",     99, 99},
}};

/**
 * @brief 获取资源类型对应的数据
 * @param type 资源类型枚举
 * @return 资源数据引用（None 类型返回空数据）
 */
inline const ResourceData& getResourceData(ResourceType type) {
    uint8_t idx = static_cast<uint8_t>(type);
    if (idx >= RESOURCE_DATA_TABLE.size()) {
        return RESOURCE_DATA_TABLE[0];
    }
    return RESOURCE_DATA_TABLE[idx];
}

/**
 * @brief 资源类型对应的中文名（用于 ImGui 显示）
 * @param type 资源类型枚举
 * @return 中文名字符串指针
 */
inline const char* resourceTypeName(ResourceType type) {
    return getResourceData(type).name.data();
}

/**
 * @brief 物品堆叠数据结构
 * 
 * 表示背包中的一个物品槽位，支持堆叠。
 * isEmpty() 检查是否为空，maxStack() 返回最大堆叠数。
 */
struct ItemStack {
    ResourceType type{ResourceType::None};
    uint32_t count{0};

    /** @brief 是否为空槽 */
    [[nodiscard]] bool isEmpty() const {
        return type == ResourceType::None || count == 0;
    }

    /** @brief 该物品类型的最大堆叠数 */
    [[nodiscard]] uint32_t maxStack() const {
        return getResourceData(type).maxPerSlot;
    }

    /** @brief 当前物品的中文名 */
    [[nodiscard]] const char* name() const {
        return resourceTypeName(type);
    }

    /** @brief 尝试加入指定数量的物品，返回实际加入的数量 */
    uint32_t add(uint32_t amount) {
        if (type == ResourceType::None) return 0;
        uint32_t maxStack = getResourceData(type).maxPerSlot;
        uint32_t canAdd = maxStack > count ? maxStack - count : 0;
        uint32_t actualAdd = std::min(canAdd, amount);
        count += actualAdd;
        return actualAdd;
    }

    /** @brief 尝试移除指定数量的物品，返回实际移除的数量 */
    uint32_t remove(uint32_t amount) {
        uint32_t actualRemove = std::min(count, amount);
        count -= actualRemove;
        if (count == 0) type = ResourceType::None;
        return actualRemove;
    }
};

/**
 * @brief 背包组件（ECS）
 * 
 * 挂载到玩家实体上，管理物品堆叠列表。
 * 默认 20 格，可通过构造参数调整。
 */
struct InventoryComponent {
    static constexpr uint32_t DEFAULT_SLOTS = 20;
    static constexpr uint32_t HOTBAR_SLOTS = 5;

    std::array<ItemStack, DEFAULT_SLOTS> slots{};
    uint32_t selectedHotbarIndex{0};  // 当前选中的快捷栏索引

    /** @brief 尝试将指定类型和数量的物品加入背包，返回实际加入数量 */
    uint32_t addItem(ResourceType type, uint32_t amount) {
        if (type == ResourceType::None || amount == 0) return 0;

        uint32_t remaining = amount;
        uint32_t maxStack = getResourceData(type).maxPerSlot;

        // 1. 先尝试堆叠到已有的同类型物品上
        for (auto& slot : slots) {
            if (remaining == 0) break;
            if (slot.type == type && slot.count < maxStack) {
                remaining -= slot.add(remaining);
            }
        }

        // 2. 再尝试放入空格
        if (remaining > 0) {
            for (auto& slot : slots) {
                if (remaining == 0) break;
                if (slot.isEmpty()) {
                    slot.type = type;
                    uint32_t canAdd = std::min(remaining, maxStack);
                    slot.count = canAdd;
                    remaining -= canAdd;
                }
            }
        }

        return amount - remaining;
    }

    /** @brief 尝试移除指定类型和数量的物品，返回实际移除数量 */
    uint32_t removeItem(ResourceType type, uint32_t amount) {
        if (type == ResourceType::None || amount == 0) return 0;

        uint32_t remaining = amount;

        // 从后往前遍历（减少碎片）
        for (auto it = slots.rbegin(); it != slots.rend(); ++it) {
            if (remaining == 0) break;
            if (it->type == type) {
                remaining -= it->remove(remaining);
            }
        }

        return amount - remaining;
    }

    /** @brief 查询指定类型的总数量 */
    [[nodiscard]] uint32_t countItem(ResourceType type) const {
        if (type == ResourceType::None) return 0;
        uint32_t total = 0;
        for (const auto& slot : slots) {
            if (slot.type == type) {
                total += slot.count;
            }
        }
        return total;
    }

    /**
     * @brief 交换两个槽位的内容（支持拖拽排序）
     * @param a,b 槽位索引（范围为 [0, DEFAULT_SLOTS)）
     * @note 如果 a == b 则无操作
     */
    void swapSlots(uint32_t a, uint32_t b) {
        if (a == b || a >= DEFAULT_SLOTS || b >= DEFAULT_SLOTS) return;
        std::swap(slots[a], slots[b]);
    }

    /** @brief 背包是否为空 */
    [[nodiscard]] bool isEmpty() const {
        for (const auto& slot : slots) {
            if (!slot.isEmpty()) return false;
        }
        return true;
    }
};

} // namespace ecs
} // namespace owengine
