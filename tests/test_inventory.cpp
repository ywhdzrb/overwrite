/**
 * @file test_inventory.cpp
 * @brief ItemStack / InventoryComponent / ResourceData 单元测试
 *
 * 测试数据层逻辑，不依赖 Vulkan/GLFW，仅需要标准库和头文件。
 * resource_types.hpp 是纯 header-only，无需链接 OverWriteShared。
 */
#include <gtest/gtest.h>
#include <cstring>
#include "ecs/resource_types.hpp"

using namespace owengine::ecs;

// ==================== ResourceData 表完整性 ====================

TEST(ResourceDataTest, TableSizeMatchesEnum) {
    // RESOURCE_DATA_TABLE 必须与 ResourceType 枚举项数一致（6 项）
    ASSERT_EQ(RESOURCE_DATA_TABLE.size(), 6);
}

TEST(ResourceDataTest, EnumValuesMatchIndices) {
    // 枚举值必须与数组索引一一对应
    EXPECT_EQ(RESOURCE_DATA_TABLE[0].type, ResourceType::None);
    EXPECT_EQ(RESOURCE_DATA_TABLE[1].type, ResourceType::Wood);
    EXPECT_EQ(RESOURCE_DATA_TABLE[2].type, ResourceType::Stone);
    EXPECT_EQ(RESOURCE_DATA_TABLE[3].type, ResourceType::IronOre);
    EXPECT_EQ(RESOURCE_DATA_TABLE[4].type, ResourceType::CopperOre);
    EXPECT_EQ(RESOURCE_DATA_TABLE[5].type, ResourceType::PlantFiber);
}

TEST(ResourceDataTest, NamesNonEmpty) {
    // 所有有效的资源类型应有非空的中文名
    EXPECT_TRUE(std::strlen(resourceTypeName(ResourceType::Wood)) > 0);
    EXPECT_TRUE(std::strlen(resourceTypeName(ResourceType::Stone)) > 0);
    EXPECT_TRUE(std::strlen(resourceTypeName(ResourceType::IronOre)) > 0);
    EXPECT_TRUE(std::strlen(resourceTypeName(ResourceType::CopperOre)) > 0);
    EXPECT_TRUE(std::strlen(resourceTypeName(ResourceType::PlantFiber)) > 0);
}

TEST(ResourceDataTest, NoneTypeHasEmptyName) {
    EXPECT_STREQ(resourceTypeName(ResourceType::None), "");
}

TEST(ResourceDataTest, MaxStackLimits) {
    // 木材/石材/植物纤维最大堆叠 99，矿石最大堆叠 64
    EXPECT_EQ(getResourceData(ResourceType::Wood).maxPerSlot, 99u);
    EXPECT_EQ(getResourceData(ResourceType::Stone).maxPerSlot, 99u);
    EXPECT_EQ(getResourceData(ResourceType::PlantFiber).maxPerSlot, 99u);
    EXPECT_EQ(getResourceData(ResourceType::IronOre).maxPerSlot, 64u);
    EXPECT_EQ(getResourceData(ResourceType::CopperOre).maxPerSlot, 64u);
}

TEST(ResourceDataTest, GetResourceDataOutOfBounds) {
    // 越界枚举应返回 None 类型（索引 0）的数据
    ResourceType invalidEnum = static_cast<ResourceType>(99);
    const auto& data = getResourceData(invalidEnum);
    EXPECT_EQ(data.type, ResourceType::None);
    EXPECT_EQ(data.maxStack, 0u);
}

TEST(ResourceDataTest, ResourceDataDescription) {
    // 验证描述文本不为空
    EXPECT_TRUE(std::strlen(getResourceData(ResourceType::Wood).description.data()) > 0);
    EXPECT_TRUE(std::strlen(getResourceData(ResourceType::Stone).description.data()) > 0);
    EXPECT_TRUE(std::strlen(getResourceData(ResourceType::IronOre).description.data()) > 0);
}

// ==================== ItemStack 基础 ====================

TEST(ItemStackTest, DefaultConstruction) {
    ItemStack stack;
    EXPECT_TRUE(stack.isEmpty());
    EXPECT_EQ(stack.count, 0u);
    EXPECT_EQ(stack.type, ResourceType::None);
}

TEST(ItemStackTest, ConstructWithTypeAndCount) {
    ItemStack stack{ResourceType::Wood, 10};
    EXPECT_FALSE(stack.isEmpty());
    EXPECT_EQ(stack.count, 10u);
    EXPECT_EQ(stack.type, ResourceType::Wood);
}

TEST(ItemStackTest, EmptyWhenTypeIsNone) {
    ItemStack stack{ResourceType::None, 10};
    EXPECT_TRUE(stack.isEmpty());
}

TEST(ItemStackTest, EmptyWhenCountIsZero) {
    ItemStack stack{ResourceType::Wood, 0};
    EXPECT_TRUE(stack.isEmpty());
}

// ==================== ItemStack::maxStack() ====================

TEST(ItemStackTest, MaxStackWood) {
    ItemStack stack{ResourceType::Wood, 1};
    EXPECT_EQ(stack.maxStack(), 99u);
}

TEST(ItemStackTest, MaxStackIronOre) {
    ItemStack stack{ResourceType::IronOre, 1};
    EXPECT_EQ(stack.maxStack(), 64u);
}

TEST(ItemStackTest, MaxStackNone) {
    ItemStack stack;
    EXPECT_EQ(stack.maxStack(), 0u);
}

// ==================== ItemStack::name() ====================

TEST(ItemStackTest, NameReturnsChineseName) {
    ItemStack wood{ResourceType::Wood, 1};
    ItemStack iron{ResourceType::IronOre, 1};
    EXPECT_STREQ(wood.name(), "木材");
    EXPECT_STREQ(iron.name(), "铁矿石");
}

TEST(ItemStackTest, NameNoneReturnsEmpty) {
    ItemStack stack;
    EXPECT_STREQ(stack.name(), "");
}

// ==================== ItemStack::add() ====================

TEST(ItemStackTest, AddToEmptyStack) {
    ItemStack stack{ResourceType::Wood, 30};
    uint32_t added = stack.add(20);
    EXPECT_EQ(added, 20u);
    EXPECT_EQ(stack.count, 50u);
}

TEST(ItemStackTest, AddUpToMaxStack) {
    ItemStack stack{ResourceType::IronOre, 60};
    uint32_t added = stack.add(10);
    // 最多到 64（maxPerSlot）
    EXPECT_EQ(added, 4u);
    EXPECT_EQ(stack.count, 64u);
}

TEST(ItemStackTest, AddOverMaxStackReturnsDifference) {
    ItemStack stack{ResourceType::IronOre, 64};
    uint32_t added = stack.add(10);
    // 已满，加不进
    EXPECT_EQ(added, 0u);
    EXPECT_EQ(stack.count, 64u);
}

TEST(ItemStackTest, AddNoneTypeReturnsZero) {
    ItemStack stack;
    uint32_t added = stack.add(50);
    EXPECT_EQ(added, 0u);
    EXPECT_TRUE(stack.isEmpty());
}

TEST(ItemStackTest, AddZeroReturnsZero) {
    ItemStack stack{ResourceType::Wood, 10};
    uint32_t added = stack.add(0);
    EXPECT_EQ(added, 0u);
    EXPECT_EQ(stack.count, 10u);
}

// ==================== ItemStack::remove() ====================

TEST(ItemStackTest, RemovePartial) {
    ItemStack stack{ResourceType::Wood, 50};
    uint32_t removed = stack.remove(20);
    EXPECT_EQ(removed, 20u);
    EXPECT_EQ(stack.count, 30u);
}

TEST(ItemStackTest, RemoveAll) {
    ItemStack stack{ResourceType::Wood, 50};
    uint32_t removed = stack.remove(50);
    EXPECT_EQ(removed, 50u);
    EXPECT_TRUE(stack.isEmpty());
    // 移完 type 应设为 None
    EXPECT_EQ(stack.type, ResourceType::None);
}

TEST(ItemStackTest, RemoveMoreThanAvailable) {
    ItemStack stack{ResourceType::Wood, 30};
    uint32_t removed = stack.remove(100);
    EXPECT_EQ(removed, 30u);
    EXPECT_TRUE(stack.isEmpty());
}

TEST(ItemStackTest, RemoveFromEmptyReturnsZero) {
    ItemStack stack;
    uint32_t removed = stack.remove(10);
    EXPECT_EQ(removed, 0u);
    EXPECT_TRUE(stack.isEmpty());
}

TEST(ItemStackTest, RemoveZeroReturnsZero) {
    ItemStack stack{ResourceType::Wood, 30};
    uint32_t removed = stack.remove(0);
    EXPECT_EQ(removed, 0u);
    EXPECT_EQ(stack.count, 30u);
}

// ==================== InventoryComponent 基础 ====================

TEST(InventoryTest, DefaultInventoryEmpty) {
    InventoryComponent inv;
    EXPECT_TRUE(inv.isEmpty());
    EXPECT_EQ(inv.selectedHotbarIndex, 0u);
    EXPECT_EQ(inv.slots.size(), InventoryComponent::DEFAULT_SLOTS);
    EXPECT_EQ(inv.slots.size(), 20u);
}

TEST(InventoryTest, HotbarSlotsConstant) {
    EXPECT_EQ(InventoryComponent::HOTBAR_SLOTS, 5u);
}

TEST(InventoryTest, EmptyAfterConstruction) {
    InventoryComponent inv;
    for (const auto& slot : inv.slots) {
        EXPECT_TRUE(slot.isEmpty());
    }
}

// ==================== InventoryComponent::addItem() ====================

TEST(InventoryTest, AddItemToEmptySlot) {
    InventoryComponent inv;
    uint32_t added = inv.addItem(ResourceType::Wood, 10);
    EXPECT_EQ(added, 10u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 10u);
    EXPECT_FALSE(inv.isEmpty());
}

TEST(InventoryTest, AddItemStacksOnExisting) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    uint32_t added = inv.addItem(ResourceType::Wood, 30);
    // 应该堆叠到同一格（50+30=80 ≤ 99）
    EXPECT_EQ(added, 30u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 80u);
}

TEST(InventoryTest, AddItemFillsSlotThenNextSlot) {
    InventoryComponent inv;
    // 按计划在第 1 格放 99 个木材，第 2 格放 99 个 → 第 3 格开始用
    uint32_t added = inv.addItem(ResourceType::Wood, 99);   // 第一格满
    EXPECT_EQ(added, 99u);
    added = inv.addItem(ResourceType::Wood, 99);             // 第二格满
    EXPECT_EQ(added, 99u);
    added = inv.addItem(ResourceType::Wood, 50);             // 第三格部分
    EXPECT_EQ(added, 50u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 248u);      // 99+99+50=248
}

TEST(InventoryTest, AddItemMultipleResourcesSeparately) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    inv.addItem(ResourceType::Stone, 30);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 50u);
    EXPECT_EQ(inv.countItem(ResourceType::Stone), 30u);
}

TEST(InventoryTest, AddItemNoneTypeReturnsZero) {
    InventoryComponent inv;
    uint32_t added = inv.addItem(ResourceType::None, 10);
    EXPECT_EQ(added, 0u);
    EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTest, AddItemZeroAmountReturnsZero) {
    InventoryComponent inv;
    uint32_t added = inv.addItem(ResourceType::Wood, 0);
    EXPECT_EQ(added, 0u);
}

TEST(InventoryTest, AddItemOverflowWhenFull) {
    // 装满所有 20 格，再添加应该返回 0
    InventoryComponent inv;
    // 每格放 99 个木材 × 20 格 = 1980
    for (int i = 0; i < 20; ++i) {
        inv.addItem(ResourceType::Wood, 99);
    }
    uint32_t added = inv.addItem(ResourceType::Wood, 50);
    EXPECT_EQ(added, 0u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 1980u);
}

TEST(InventoryTest, AddItemPartialOverflowWhenAlmostFull) {
    InventoryComponent inv;
    // 填满 19 格（19×99=1881），最后一格放 80
    for (int i = 0; i < 19; ++i) {
        inv.addItem(ResourceType::Wood, 99);
    }
    inv.addItem(ResourceType::Wood, 80);  // 第 20 格 80
    // 再加 50：第 20 格只能加 19（从 80 到 99）
    uint32_t added = inv.addItem(ResourceType::Wood, 50);
    EXPECT_EQ(added, 19u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 1881u + 99u);
}

// ==================== InventoryComponent::removeItem() ====================

TEST(InventoryTest, RemoveItemBasic) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    uint32_t removed = inv.removeItem(ResourceType::Wood, 20);
    EXPECT_EQ(removed, 20u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 30u);
}

TEST(InventoryTest, RemoveItemAll) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    uint32_t removed = inv.removeItem(ResourceType::Wood, 50);
    EXPECT_EQ(removed, 50u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 0u);
    EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTest, RemoveItemFromPartialStack) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    inv.addItem(ResourceType::Wood, 60);  // 堆叠到同一格（50+60=110 → 占两个 slot）
    // 实际上 addItem 会尝试堆叠，50+60 → 第一格 99，多余 11 到第二格
    // 所以有两格有木材：99 和 11
    uint32_t removed = inv.removeItem(ResourceType::Wood, 20);
    // 从后往前遍历（第二格 11 先减完，再从第一格减 9）
    EXPECT_EQ(removed, 20u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 90u);
}

TEST(InventoryTest, RemoveItemMoreThanAvailable) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 30);
    uint32_t removed = inv.removeItem(ResourceType::Wood, 100);
    EXPECT_EQ(removed, 30u);
    EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTest, RemoveItemNoneTypeReturnsZero) {
    InventoryComponent inv;
    uint32_t removed = inv.removeItem(ResourceType::None, 10);
    EXPECT_EQ(removed, 0u);
}

TEST(InventoryTest, RemoveNonexistentItem) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 30);
    uint32_t removed = inv.removeItem(ResourceType::Stone, 10);
    EXPECT_EQ(removed, 0u);
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 30u);
}

// ==================== InventoryComponent::swapSlots() ====================

TEST(InventoryTest, SwapSlots) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);   // slot 0
    inv.addItem(ResourceType::Stone, 30);  // slot 1
    inv.swapSlots(0, 1);
    EXPECT_EQ(inv.slots[0].type, ResourceType::Stone);
    EXPECT_EQ(inv.slots[0].count, 30u);
    EXPECT_EQ(inv.slots[1].type, ResourceType::Wood);
    EXPECT_EQ(inv.slots[1].count, 50u);
}

TEST(InventoryTest, SwapSlotsSameIndexNoOp) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    inv.swapSlots(0, 0);
    EXPECT_EQ(inv.slots[0].type, ResourceType::Wood);
    EXPECT_EQ(inv.slots[0].count, 50u);
}

TEST(InventoryTest, SwapSlotsOutOfRangeNoOp) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    inv.swapSlots(0, 99);  // 越界
    EXPECT_EQ(inv.slots[0].type, ResourceType::Wood);
    EXPECT_EQ(inv.slots[0].count, 50u);
}

// ==================== InventoryComponent::countItem() ====================

TEST(InventoryTest, CountItemAcrossMultipleSlots) {
    InventoryComponent inv;
    // 故意分开两格放
    inv.slots[0] = {ResourceType::Wood, 40};
    inv.slots[1] = {ResourceType::Wood, 30};
    EXPECT_EQ(inv.countItem(ResourceType::Wood), 70u);
}

TEST(InventoryTest, CountItemNoneTypeReturnsZero) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    EXPECT_EQ(inv.countItem(ResourceType::None), 0u);
}

TEST(InventoryTest, CountItemNonExistentReturnsZero) {
    InventoryComponent inv;
    EXPECT_EQ(inv.countItem(ResourceType::IronOre), 0u);
}

// ==================== InventoryComponent::isEmpty() ====================

TEST(InventoryTest, IsEmptyAfterRemoveAll) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    inv.removeItem(ResourceType::Wood, 50);
    EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTest, IsEmptyWithMixedOperations) {
    InventoryComponent inv;
    inv.addItem(ResourceType::Wood, 50);
    inv.addItem(ResourceType::Stone, 30);
    inv.removeItem(ResourceType::Wood, 50);
    EXPECT_FALSE(inv.isEmpty());
    inv.removeItem(ResourceType::Stone, 30);
    EXPECT_TRUE(inv.isEmpty());
}

// ==================== InventoryComponent::selectedHotbarIndex ====================

TEST(InventoryTest, SelectedHotbarIndexBounds) {
    InventoryComponent inv;
    // 默认选中索引 0，始终应在 [0, HOTBAR_SLOTS) 范围内
    EXPECT_LT(inv.selectedHotbarIndex, InventoryComponent::HOTBAR_SLOTS);
}
