#include "renderer/tree_system.h"
#include "renderer/gltf_model.h"
#include "core/vulkan_device.h"
#include "renderer/texture_loader.h"
#include "core/camera.h"
#include "utils/logger.h"

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
    if (sharedTreeModel_->loadFromFile("assets/models/tree.glb")) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 312;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
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
                    break;
                }
            }
        }
    }
}

void TreeSystem::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                        const Camera& camera) const {
    if (!sharedTreeModel_) return;

    for (const auto& tree : trees_) {
        if (tree.id.empty()) continue;

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
                                modelMatrix);
    }
}

void TreeSystem::cleanup() {
    sharedTreeModel_.reset();
    if (sharedTreePool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_->getDevice(), sharedTreePool_, nullptr);
        sharedTreePool_ = VK_NULL_HANDLE;
    }
    trees_.clear();
    loadedChunks_.clear();
}

} // namespace owengine
