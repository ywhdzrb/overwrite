#include "renderer/tree_system.hpp"
#include "renderer/gltf_model.hpp"
#include "core/vulkan_device.hpp"
#include "renderer/texture_loader.hpp"
#include "core/camera.hpp"
#include "utils/logger.hpp"
#include "utils/asset_paths.hpp"

#include <random>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace owengine {

TreeSystem::TreeSystem(std::shared_ptr<VulkanDevice> device,
                       std::shared_ptr<TextureLoader> textureLoader,
                       VkDescriptorSetLayout descriptorSetLayout)
    : device_(std::move(device))
    , textureLoader_(std::move(textureLoader))
    , descriptorSetLayout_(descriptorSetLayout) {
}

TreeSystem::~TreeSystem() {
    cleanup();
}

void TreeSystem::init(const TreeConfig& cfg) {
    config_ = cfg;

    sharedTreeModel_ = std::make_unique<GLTFModel>(device_, textureLoader_);
    if (sharedTreeModel_->loadFromFile(AssetPaths::TREE_MODEL)) {
        // 仅树叶节点(leaf.XXX)受风，树干节点(立方体)保持静止
        sharedTreeModel_->setWindNodePrefixes({"leaf."});
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 312;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 302;
        if (vkCreateDescriptorPool(device_->getDevice(), &poolInfo, nullptr, &sharedTreePool_) == VK_SUCCESS) {
            sharedTreeModel_->createMeshDescriptorSets(descriptorSetLayout_, sharedTreePool_);
        }
        Logger::info("[TreeSystem] 共享模型加载完成, mesh数: " + std::to_string(sharedTreeModel_->getMeshCount()));
    } else {
        Logger::error("[TreeSystem] 共享模型加载失败");
    }

    // 预分配实例化阴影缓冲（最多可容纳 config_.maxTotal 棵树）
    createShadowInstanceBuffer(static_cast<uint32_t>(config_.maxTotal));

    generateTreesAtStartup(config_);
}

void TreeSystem::generateTreesAtStartup(const TreeConfig& cfg) {
    auto& c = cfg;

    trees_.resize(c.maxTotal);
    loadedChunks_.reserve(c.maxTotal * 2);

    int treeIdx = 0;
    for (int dz = -c.loadRadius; dz <= c.loadRadius; ++dz) {
        for (int dx = -c.loadRadius; dx <= c.loadRadius; ++dx) {
            if (treeIdx >= c.maxTotal) break;
            TreeChunkKey key{dx, dz};

            std::mt19937 chunkGen(key.x * 100000 + key.z);
            std::poisson_distribution<int> poisson(c.density);
            int treeCount = poisson(chunkGen);
            if (treeCount <= 0) {
                loadedChunks_.insert(key);
                continue;
            }

            float chunkWorldX = static_cast<float>(key.x) * c.chunkSize;
            float chunkWorldZ = static_cast<float>(key.z) * c.chunkSize;

            std::uniform_real_distribution<float> posOffset(1.0f, c.chunkSize - 1.0f);
            std::uniform_real_distribution<float> scaleGen(c.minScale, c.maxScale);
            std::uniform_real_distribution<float> yawGen(0.0f, 360.0f);

            for (int t = 0; t < treeCount && treeIdx < c.maxTotal; ++t) {
                for (int attempt = 0; attempt < 10; ++attempt) {
                    float wx = chunkWorldX + posOffset(chunkGen);
                    float wz = chunkWorldZ + posOffset(chunkGen);

                    float y = heightSampler_ ? heightSampler_(wx, wz) : 0.0f;
                    if (y < c.heightThreshold) continue;

                    trees_[treeIdx].id = "tree_" + std::to_string(key.x) + "_" + std::to_string(key.z) + "_" + std::to_string(t);
                    trees_[treeIdx].position = {wx, y, wz};
                    trees_[treeIdx].scale = scaleGen(chunkGen);
                    trees_[treeIdx].yaw = yawGen(chunkGen);
                    treeIdx++;
                    break;
                }
            }
            loadedChunks_.insert(key);
        }
    }
    Logger::info("[TreeSystem] 已预计算 " + std::to_string(treeIdx) + " 棵树，直接填入槽位");
}

void TreeSystem::update(const glm::vec3& playerPos, const Camera& camera) {
    if (!sharedTreeModel_) return;
    auto& c = config_;

    int cx = static_cast<int>(std::floor(playerPos.x / c.chunkSize));
    int cz = static_cast<int>(std::floor(playerPos.z / c.chunkSize));

    for (int dz = -c.loadRadius; dz <= c.loadRadius; ++dz) {
        for (int dx = -c.loadRadius; dx <= c.loadRadius; ++dx) {
            TreeChunkKey key{cx + dx, cz + dz};
            if (loadedChunks_.count(key)) continue;
            loadedChunks_.insert(key);

            std::mt19937 chunkGen(key.x * 100000 + key.z);
            std::poisson_distribution<int> poisson(c.density);
            int treeCount = poisson(chunkGen);
            if (treeCount <= 0) continue;

            float chunkWorldX = static_cast<float>(key.x) * c.chunkSize;
            float chunkWorldZ = static_cast<float>(key.z) * c.chunkSize;

            std::uniform_real_distribution<float> posOffset(1.0f, c.chunkSize - 1.0f);
            std::uniform_real_distribution<float> scaleGen(c.minScale, c.maxScale);
            std::uniform_real_distribution<float> yawGen(0.0f, 360.0f);

            for (int t = 0; t < treeCount; ++t) {
                for (int attempt = 0; attempt < 10; ++attempt) {
                    float wx = chunkWorldX + posOffset(chunkGen);
                    float wz = chunkWorldZ + posOffset(chunkGen);

                    int slot = -1;
                    for (int i = 0; i < c.maxTotal; ++i) {
                        if (trees_[i].id.empty()) { slot = i; break; }
                    }
                    if (slot < 0) {
                        static int replaceIdx = 0;
                        slot = replaceIdx++ % c.maxTotal;
                    }

                    float y = heightSampler_ ? heightSampler_(wx, wz) : 0.0f;
                    if (y < c.heightThreshold) continue;

                    std::string id = "tree_" + std::to_string(key.x) + "_" + std::to_string(key.z) + "_" + std::to_string(t);
                    trees_[slot].id = id;
                    trees_[slot].position = {wx, y, wz};
                    trees_[slot].scale = scaleGen(chunkGen);
                    trees_[slot].yaw = yawGen(chunkGen);
                    trees_[slot].active = true;  // 重置 active（槽位可能被回收自已耗尽的树）
                    break;
                }
            }
        }
    }
}

void TreeSystem::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                        const Camera& camera, float time, float windStrength) const {
    if (!sharedTreeModel_) return;

    for (const auto& tree : trees_) {
        if (tree.id.empty() || !tree.active) continue;

        float distance = glm::length(tree.position - camera.getPosition());
        if (distance > config_.renderDistance) continue;

        auto bbox = sharedTreeModel_->getBoundingBox();
        if (!camera.getFrustum().isAABBInside(
            tree.position + bbox.first * tree.scale,
            tree.position + bbox.second * tree.scale)) {
            continue;
        }

        glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), tree.position)
                              * glm::rotate(glm::mat4(1.0f), glm::radians(tree.yaw), glm::vec3(0.0f, 1.0f, 0.0f))
                              * glm::scale(glm::mat4(1.0f), glm::vec3(tree.scale));

        sharedTreeModel_->render(commandBuffer, pipelineLayout,
                                camera.getViewMatrix(), camera.getProjectionMatrix(),
                                modelMatrix, time, windStrength);
    }
}

void TreeSystem::renderShadow(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                               const glm::mat4& lightView, const glm::mat4& lightProj) const {
    if (!sharedTreeModel_) return;

    // 阴影距离和 LOD 分档阈值
    static constexpr float kShadowNear = 50.0f;   // 0~50m: Full LOD（全部 mesh）
    static constexpr float kShadowMax  = 150.0f;  // 50~150m: TrunkOnly LOD（仅树干），超过则范围剔除

    // 按距离分桶收集模型矩阵
    std::vector<glm::mat4> nearModels;
    std::vector<glm::mat4> farModels;
    nearModels.reserve(trees_.size() / 4);
    farModels.reserve(trees_.size() / 4);

    for (const auto& tree : trees_) {
        if (tree.id.empty() || !tree.active) continue;
        float dist = glm::length(tree.position);
        if (dist > kShadowMax) continue;          // 范围剔除

        glm::mat4 m = glm::translate(glm::mat4(1.0f), tree.position)
                    * glm::rotate(glm::mat4(1.0f), glm::radians(tree.yaw), glm::vec3(0.0f, 1.0f, 0.0f))
                    * glm::scale(glm::mat4(1.0f), glm::vec3(tree.scale));

        if (dist < kShadowNear) nearModels.push_back(m);
        else                    farModels.push_back(m);
    }

    // 一次性上传两个桶到缓冲的不同偏移位置（避免近桶数据被覆盖导致闪烁）
    uint32_t nearCount = static_cast<uint32_t>(nearModels.size());
    uint32_t farCount  = static_cast<uint32_t>(farModels.size());
    VkDeviceSize nearBytes = nearCount * sizeof(glm::mat4);
    VkDeviceSize farBytes  = farCount * sizeof(glm::mat4);

    void* mapped = nullptr;
    vmaMapMemory(device_->getAllocator(), shadowInstAlloc_, &mapped);
    if (nearCount > 0) memcpy(mapped, nearModels.data(), static_cast<size_t>(nearBytes));
    if (farCount  > 0) memcpy(static_cast<char*>(mapped) + nearBytes, farModels.data(), static_cast<size_t>(farBytes));
    vmaUnmapMemory(device_->getAllocator(), shadowInstAlloc_);

    // 近距桶（Full LOD，偏移 0）
    if (nearCount > 0) {
        sharedTreeModel_->renderShadowInstanced(commandBuffer, pipelineLayout,
                                                lightView, lightProj,
                                                shadowInstBuf_, nearCount, 0,
                                                GLTFModel::ShadowLOD::Full);
    }
    // 远距桶（TrunkOnly LOD，偏移 nearBytes）
    if (farCount > 0) {
        sharedTreeModel_->renderShadowInstanced(commandBuffer, pipelineLayout,
                                                lightView, lightProj,
                                                shadowInstBuf_, farCount, nearBytes,
                                                GLTFModel::ShadowLOD::TrunkOnly);
    }
}

void TreeSystem::createShadowInstanceBuffer(uint32_t maxTrees) {
    VkDeviceSize bufSize = static_cast<VkDeviceSize>(maxTrees) * sizeof(glm::mat4);
    VkBufferCreateInfo bufCi{};
    bufCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCi.size = bufSize;
    bufCi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCi{};
    allocCi.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocCi.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                  | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo;
    VkResult _vr = vmaCreateBuffer(device_->getAllocator(), &bufCi, &allocCi,
                                   &shadowInstBuf_, &shadowInstAlloc_, &allocInfo);
    if (_vr != VK_SUCCESS) {
        Logger::error("[TreeSystem] 创建实例化阴影缓冲失败");
        shadowInstBuf_ = VK_NULL_HANDLE;
        shadowInstAlloc_ = VK_NULL_HANDLE;
        shadowInstCapacity_ = 0;
        return;
    }
    shadowInstCapacity_ = maxTrees;
    Logger::info("[TreeSystem] 实例化阴影缓冲创建完成: " + std::to_string(maxTrees) + " 棵");
}

void TreeSystem::destroyShadowInstanceBuffer() {
    if (shadowInstBuf_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(device_->getAllocator(), shadowInstBuf_, shadowInstAlloc_);
        shadowInstBuf_ = VK_NULL_HANDLE;
        shadowInstAlloc_ = VK_NULL_HANDLE;
        shadowInstCapacity_ = 0;
    }
}

std::vector<std::pair<glm::vec3, float>> TreeSystem::queryPositions(float x, float z, float radius) const {
    std::vector<std::pair<glm::vec3, float>> result;
    float radiusSq = radius * radius;
    result.reserve(trees_.size() / 8);
    for (const auto& tree : trees_) {
        if (tree.id.empty() || !tree.active) continue;
        float dx = tree.position.x - x;
        float dz = tree.position.z - z;
        if (dx * dx + dz * dz < radiusSq) {
            result.emplace_back(tree.position, tree.scale);
        }
    }
    return result;
}

void TreeSystem::cleanup() {
    destroyShadowInstanceBuffer();
    sharedTreeModel_.reset();
    if (sharedTreePool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_->getDevice(), sharedTreePool_, nullptr);
        sharedTreePool_ = VK_NULL_HANDLE;
    }
    trees_.clear();
    loadedChunks_.clear();
}

const std::vector<TreeSystem::TreeInstanceInfo>& TreeSystem::getTreeInstances() const {
    instanceCache_.clear();
    instanceCache_.reserve(trees_.size());
    for (int i = 0; i < static_cast<int>(trees_.size()); ++i) {
        const auto& tree = trees_[i];
        if (tree.id.empty() || !tree.active) continue;
        instanceCache_.push_back({tree.position, tree.scale, i});
    }
    return instanceCache_;
}

void TreeSystem::setTreeActive(int slotIndex, bool active) {
    if (slotIndex >= 0 && slotIndex < static_cast<int>(trees_.size())) {
        trees_[slotIndex].active = active;
    }
}

} // namespace owengine
