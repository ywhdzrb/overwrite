#include "renderer/stone_system.h"
#include "renderer/gltf_model.h"
#include "core/vulkan_device.h"
#include "renderer/texture_loader.h"
#include "core/camera.h"
#include "utils/logger.h"
#include "utils/asset_paths.h"

#include <random>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace owengine {

StoneSystem::StoneSystem(std::shared_ptr<VulkanDevice> device,
                         std::shared_ptr<TextureLoader> textureLoader,
                         VkDescriptorSetLayout descriptorSetLayout)
    : device_(std::move(device))
    , textureLoader_(std::move(textureLoader))
    , descriptorSetLayout_(descriptorSetLayout) {
}

StoneSystem::~StoneSystem() {
    cleanup();
}

void StoneSystem::init(const StoneConfig& cfg) {
    config_ = cfg;

    sharedStoneModel_ = std::make_unique<GLTFModel>(device_, textureLoader_);
    if (sharedStoneModel_->loadFromFile(AssetPaths::STONE_MODEL)) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 512;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 502;
        if (vkCreateDescriptorPool(device_->getDevice(), &poolInfo, nullptr, &sharedStonePool_) == VK_SUCCESS) {
            sharedStoneModel_->createMeshDescriptorSets(descriptorSetLayout_, sharedStonePool_);
        }
        Logger::info("[StoneSystem] 共享石头模型加载完成, mesh数: " + std::to_string(sharedStoneModel_->getMeshCount()));
    } else {
        Logger::error("[StoneSystem] 共享石头模型加载失败");
    }

    generateStonesAtStartup(config_);
}

void StoneSystem::generateStonesAtStartup(const StoneConfig& cfg) {
    auto& c = cfg;

    stones_.resize(c.maxStones);
    loadedChunks_.reserve(c.maxStones * 2);

    int stoneIdx = 0;
    for (int dz = -c.loadRadius; dz <= c.loadRadius; ++dz) {
        for (int dx = -c.loadRadius; dx <= c.loadRadius; ++dx) {
            if (stoneIdx >= c.maxStones) break;
            StoneChunkKey key{dx, dz};

            std::mt19937 chunkGen(key.x * 200000 + key.z);
            std::poisson_distribution<int> poisson(c.density);
            int stoneCount = poisson(chunkGen);
            if (stoneCount <= 0) {
                loadedChunks_.insert(key);
                continue;
            }

            float chunkWorldX = static_cast<float>(key.x) * c.chunkSize;
            float chunkWorldZ = static_cast<float>(key.z) * c.chunkSize;

            std::uniform_real_distribution<float> posOffset(1.0f, c.chunkSize - 1.0f);
            std::uniform_real_distribution<float> scaleGen(c.minScale, c.maxScale);
            std::uniform_real_distribution<float> yawGen(0.0f, 360.0f);

            for (int t = 0; t < stoneCount && stoneIdx < c.maxStones; ++t) {
                for (int attempt = 0; attempt < 10; ++attempt) {
                    float wx = chunkWorldX + posOffset(chunkGen);
                    float wz = chunkWorldZ + posOffset(chunkGen);

                    float y = heightSampler_ ? heightSampler_(wx, wz) : 0.0f;
                    if (y < c.heightThreshold) continue;

                    stones_[stoneIdx].id = "stone_" + std::to_string(key.x) + "_" + std::to_string(key.z) + "_" + std::to_string(t);
                    stones_[stoneIdx].position = {wx, y, wz};
                    stones_[stoneIdx].scale = scaleGen(chunkGen);
                    stones_[stoneIdx].yaw = yawGen(chunkGen);
                    stoneIdx++;
                    break;
                }
            }
            loadedChunks_.insert(key);
        }
    }
    Logger::info("[StoneSystem] 已预计算 " + std::to_string(stoneIdx) + " 块石头");
}

void StoneSystem::update(const glm::vec3& playerPos, const Camera& camera) {
    if (!sharedStoneModel_) return;
    auto& c = config_;

    int cx = static_cast<int>(std::floor(playerPos.x / c.chunkSize));
    int cz = static_cast<int>(std::floor(playerPos.z / c.chunkSize));

    for (int dz = -c.loadRadius; dz <= c.loadRadius; ++dz) {
        for (int dx = -c.loadRadius; dx <= c.loadRadius; ++dx) {
            StoneChunkKey key{cx + dx, cz + dz};
            if (loadedChunks_.count(key)) continue;
            loadedChunks_.insert(key);

            std::mt19937 chunkGen(key.x * 200000 + key.z);
            std::poisson_distribution<int> poisson(c.density);
            int stoneCount = poisson(chunkGen);
            if (stoneCount <= 0) continue;

            float chunkWorldX = static_cast<float>(key.x) * c.chunkSize;
            float chunkWorldZ = static_cast<float>(key.z) * c.chunkSize;

            std::uniform_real_distribution<float> posOffset(1.0f, c.chunkSize - 1.0f);
            std::uniform_real_distribution<float> scaleGen(c.minScale, c.maxScale);
            std::uniform_real_distribution<float> yawGen(0.0f, 360.0f);

            for (int t = 0; t < stoneCount; ++t) {
                for (int attempt = 0; attempt < 10; ++attempt) {
                    float wx = chunkWorldX + posOffset(chunkGen);
                    float wz = chunkWorldZ + posOffset(chunkGen);

                    int slot = -1;
                    for (int i = 0; i < c.maxStones; ++i) {
                        if (stones_[i].id.empty()) { slot = i; break; }
                    }
                    if (slot < 0) {
                        static int replaceIdx = 0;
                        slot = replaceIdx++ % c.maxStones;
                    }

                    float y = heightSampler_ ? heightSampler_(wx, wz) : 0.0f;
                    if (y < c.heightThreshold) continue;

                    std::string id = "stone_" + std::to_string(key.x) + "_" + std::to_string(key.z) + "_" + std::to_string(t);
                    stones_[slot].id = id;
                    stones_[slot].position = {wx, y, wz};
                    stones_[slot].scale = scaleGen(chunkGen);
                    stones_[slot].yaw = yawGen(chunkGen);
                    stones_[slot].active = true;  // 重置 active（槽位可能被回收自已耗尽的石头）
                    break;
                }
            }
        }
    }
}

void StoneSystem::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                         const Camera& camera) const {
    if (!sharedStoneModel_) return;

    for (const auto& stone : stones_) {
        if (stone.id.empty() || !stone.active) continue;

        float distance = glm::length(stone.position - camera.getPosition());
        if (distance > config_.renderDistance) continue;

        auto bbox = sharedStoneModel_->getBoundingBox();
        if (!camera.getFrustum().isAABBInside(
            stone.position + bbox.first * stone.scale,
            stone.position + bbox.second * stone.scale)) {
            continue;
        }

        glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), stone.position)
                              * glm::rotate(glm::mat4(1.0f), glm::radians(stone.yaw), glm::vec3(0.0f, 1.0f, 0.0f))
                              * glm::scale(glm::mat4(1.0f), glm::vec3(stone.scale));

        sharedStoneModel_->render(commandBuffer, pipelineLayout,
                                 camera.getViewMatrix(), camera.getProjectionMatrix(),
                                 modelMatrix);
    }
}

std::vector<std::pair<glm::vec3, float>> StoneSystem::queryPositions(float x, float z, float radius) const {
    std::vector<std::pair<glm::vec3, float>> result;
    float radiusSq = radius * radius;
    result.reserve(stones_.size() / 8);
    for (const auto& stone : stones_) {
        if (stone.id.empty() || !stone.active) continue;
        float dx = stone.position.x - x;
        float dz = stone.position.z - z;
        if (dx * dx + dz * dz < radiusSq) {
            result.emplace_back(stone.position, stone.scale);
        }
    }
    return result;
}

void StoneSystem::cleanup() {
    sharedStoneModel_.reset();
    if (sharedStonePool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_->getDevice(), sharedStonePool_, nullptr);
        sharedStonePool_ = VK_NULL_HANDLE;
    }
    stones_.clear();
    loadedChunks_.clear();
}

std::vector<StoneSystem::StoneInstanceInfo> StoneSystem::getStoneInstances() const {
    std::vector<StoneInstanceInfo> result;
    result.reserve(stones_.size());
    for (int i = 0; i < static_cast<int>(stones_.size()); ++i) {
        const auto& stone = stones_[i];
        if (stone.id.empty() || !stone.active) continue;
        result.push_back({stone.position, stone.scale, i});
    }
    return result;
}

void StoneSystem::setStoneActive(int slotIndex, bool active) {
    if (slotIndex >= 0 && slotIndex < static_cast<int>(stones_.size())) {
        stones_[slotIndex].active = active;
    }
}

} // namespace owengine
