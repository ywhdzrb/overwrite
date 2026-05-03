#include "core/resource_node_system.h"
#include "renderer/tree_system.h"
#include "renderer/stone_system.h"
#include "ecs/resource_types.h"
#include "utils/logger.h"

namespace owengine {

void ResourceNodeSystem::init(TreeSystem* treeSystem, StoneSystem* stoneSystem) {
    clear();
    treeSystem_ = treeSystem;
    stoneSystem_ = stoneSystem;

    if (treeSystem) {
        populateFromTrees(treeSystem);
    }
    if (stoneSystem) {
        populateFromStones(stoneSystem);
    }

    Logger::info("[ResourceNodeSystem] 初始化完成，共创建 " + std::to_string(nodes_.size()) + " 个资源节点");
}

void ResourceNodeSystem::update(float dt) {
    // 每约 1 秒扫描一次树/石系统的新实例
    reconcileTimer_ += dt;
    if (reconcileTimer_ >= 1.0f) {
        reconcileTimer_ = 0.0f;
        reconcile();
    }
}

void ResourceNodeSystem::reconcile() {
    if (!treeSystem_ && !stoneSystem_) return;

    // 扫描 TreeSystem 当前所有活跃实例，为新增的创建资源节点
    if (treeSystem_) {
        auto instances = treeSystem_->getTreeInstances();
        for (const auto& info : instances) {
            ResourceNode* existing = findNodeBySource(ResourceSourceType::Tree, info.slotIndex);
            if (existing && existing->remainingHarvests > 0) {
                continue; // 已有未耗尽节点，无需处理
            }
            // 新树 或 槽位回收（旧树已耗尽被新树替换）
            ResourceNode node;
            node.position = info.position;
            node.type = ecs::ResourceType::Wood;
            node.maxHarvests = 3 + static_cast<uint32_t>(info.scale * 5.0f);
            node.remainingHarvests = node.maxHarvests;
            node.sourceIndex = info.slotIndex;
            node.sourceType = ResourceSourceType::Tree;
            if (existing) {
                *existing = std::move(node); // 覆盖已耗尽的旧节点
            } else {
                nodes_.push_back(std::move(node));
            }
        }
    }

    // 扫描 StoneSystem 当前所有活跃实例
    if (stoneSystem_) {
        auto instances = stoneSystem_->getStoneInstances();
        for (const auto& info : instances) {
            ResourceNode* existing = findNodeBySource(ResourceSourceType::Stone, info.slotIndex);
            if (existing && existing->remainingHarvests > 0) {
                continue;
            }
            ResourceNode node;
            node.position = info.position;
            node.type = ecs::ResourceType::Stone;
            node.maxHarvests = 5 + static_cast<uint32_t>(info.scale * 3.0f);
            node.remainingHarvests = node.maxHarvests;
            node.sourceIndex = info.slotIndex;
            node.sourceType = ResourceSourceType::Stone;
            if (existing) {
                *existing = std::move(node);
            } else {
                nodes_.push_back(std::move(node));
            }
        }
    }
}

ResourceNode* ResourceNodeSystem::findNodeBySource(ResourceSourceType type, int index) {
    for (auto& node : nodes_) {
        if (node.sourceType == type && node.sourceIndex == index) {
            return &node;
        }
    }
    return nullptr;
}

std::vector<ResourceNode*> ResourceNodeSystem::queryNodes(float x, float z, float radius) {
    std::vector<ResourceNode*> result;
    float radiusSq = radius * radius;
    result.reserve(nodes_.size() / 8);

    for (auto& node : nodes_) {
        if (node.remainingHarvests <= 0) {
            continue; // 已耗尽，永久消失
        }
        float dx = node.position.x - x;
        float dz = node.position.z - z;
        if (dx * dx + dz * dz < radiusSq) {
            result.push_back(&node);
        }
    }
    return result;
}

ecs::ItemStack ResourceNodeSystem::harvest(const glm::vec3& position, float harvestRange) {
    float rangeSq = harvestRange * harvestRange;

    for (auto& node : nodes_) {
        if (node.remainingHarvests <= 0) {
            continue;
        }
        float dx = node.position.x - position.x;
        float dz = node.position.z - position.z;
        float distSq = dx * dx + dz * dz;

        if (distSq < rangeSq) {
            node.remainingHarvests--;
            if (node.remainingHarvests <= 0) {
                hideSource(node);  // 采集耗尽，隐藏对应的树/石（永久消失）
            }
            return ecs::ItemStack{node.type, 1};
        }
    }

    return ecs::ItemStack{};
}

void ResourceNodeSystem::clear() {
    // 恢复所有树/石的显示（下次 init 会重新填充）
    for (auto& node : nodes_) {
        showSource(node);
    }
    nodes_.clear();
    treeSystem_ = nullptr;
    stoneSystem_ = nullptr;
}

void ResourceNodeSystem::populateFromTrees(TreeSystem* treeSystem) {
    auto treeInstances = treeSystem->getTreeInstances();
    nodes_.reserve(nodes_.size() + treeInstances.size());

    for (const auto& info : treeInstances) {
        ResourceNode node;
        node.position = info.position;
        node.type = ecs::ResourceType::Wood;
        node.maxHarvests = 3 + static_cast<uint32_t>(info.scale * 5.0f);
        node.remainingHarvests = node.maxHarvests;
        node.sourceIndex = info.slotIndex;
        node.sourceType = ResourceSourceType::Tree;
        nodes_.push_back(std::move(node));
    }
}

void ResourceNodeSystem::populateFromStones(StoneSystem* stoneSystem) {
    auto stoneInstances = stoneSystem->getStoneInstances();
    nodes_.reserve(nodes_.size() + stoneInstances.size());

    for (const auto& info : stoneInstances) {
        ResourceNode node;
        node.position = info.position;
        node.type = ecs::ResourceType::Stone;
        node.maxHarvests = 5 + static_cast<uint32_t>(info.scale * 3.0f);
        node.remainingHarvests = node.maxHarvests;
        node.sourceIndex = info.slotIndex;
        node.sourceType = ResourceSourceType::Stone;
        nodes_.push_back(std::move(node));
    }
}

void ResourceNodeSystem::hideSource(const ResourceNode& node) {
    if (node.sourceType == ResourceSourceType::Tree && treeSystem_) {
        treeSystem_->setTreeActive(node.sourceIndex, false);
    } else if (node.sourceType == ResourceSourceType::Stone && stoneSystem_) {
        stoneSystem_->setStoneActive(node.sourceIndex, false);
    }
}

void ResourceNodeSystem::showSource(const ResourceNode& node) {
    if (node.sourceType == ResourceSourceType::Tree && treeSystem_) {
        treeSystem_->setTreeActive(node.sourceIndex, true);
    } else if (node.sourceType == ResourceSourceType::Stone && stoneSystem_) {
        stoneSystem_->setStoneActive(node.sourceIndex, true);
    }
}

} // namespace owengine
