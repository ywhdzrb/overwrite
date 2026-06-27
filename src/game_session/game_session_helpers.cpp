// 游戏会话辅助模块 — 玩家模型加载、描述符集创建、采集目标检测
//
// 提供游戏会话中不直接属于主循环流水线的辅助操作：
//   模型加载（idle/walk 双模型切换）
//   模型描述符集创建（纹理绑定）
//   采集目标检测（准星指向的最近资源节点）

#include "core/game_session.hpp"
#include "core/vulkan_device.hpp"
#include "utils/asset_paths.hpp"
#include "renderer/gltf_model.hpp"
#include "renderer/texture_loader.hpp"
#include "ecs/client_systems.hpp"
#include "ecs/ecs.hpp"
#include "utils/logger.hpp"

namespace owengine {

/**
 * @brief 加载玩家模型（idle + walk 双模型）
 *
 * 从 AssetPaths 配置路径加载空闲和行走两个 GLTF 模型，
 * 为每个模型创建纹理描述符集，供渲染管线使用。
 * 若加载失败（文件不存在），仅记录警告，游戏继续运行（无玩家模型）。
 */
void GameSession::loadPlayerModels() {
    auto loadModel = [this](const std::string& path) -> std::unique_ptr<GLTFModel> {
        auto model = std::make_unique<GLTFModel>(device_, textureLoader_);
        if (model->loadFromFile(path)) {
            model->setScale(glm::vec3(ecs::PLAYER_MODEL_SCALE));
            return model;
        }
        Logger::warning("无法加载玩家模型: " + path);
        return nullptr;
    };

    // 加载空闲模型
    auto idleModel = loadModel(AssetPaths::PLAYER_IDLE_MODEL);
    if (idleModel) {
        if (descriptorPool_ != VK_NULL_HANDLE && textureDescriptorSetLayout_ != VK_NULL_HANDLE) {
            idleModel->createMeshDescriptorSets(textureDescriptorSetLayout_, descriptorPool_);
            gltfModelDescriptorSet_ = createModelDescriptorSet(idleModel.get(), descriptorPool_,
                                                               textureDescriptorSetLayout_, "player_idle");
        }
        gltfModel_ = std::move(idleModel);
    }

    // 加载行走模型
    auto walkModel = loadModel(AssetPaths::PLAYER_WALK_MODEL);
    if (walkModel) {
        if (descriptorPool_ != VK_NULL_HANDLE && textureDescriptorSetLayout_ != VK_NULL_HANDLE) {
            walkModel->createMeshDescriptorSets(textureDescriptorSetLayout_, descriptorPool_);
            gltfWalkModelDescriptorSet_ = createModelDescriptorSet(walkModel.get(), descriptorPool_,
                                                                   textureDescriptorSetLayout_, "player_walk");
        }
        gltfWalkModel_ = std::move(walkModel);
    }
}

/**
 * @brief 为单网格 GLTF 模型创建整体纹理描述符集
 *
 * 适用于单网格模型（玩家模型每个文件只有一个 mesh），
 * 为模型的第一张纹理（漫反射）创建 COMBINED_IMAGE_SAMPLER 描述符集。
 * 在 drawFrame 中绑定至 set=0 供片元着色器采样。
 */
VkDescriptorSet GameSession::createModelDescriptorSet(GLTFModel* model, VkDescriptorPool pool,
                                                       VkDescriptorSetLayout layout, const std::string& name) {
    if (!model || model->getMeshCount() == 0) return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device_->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        Logger::warning("无法为 " + name + " 分配纹理描述符集");
        return VK_NULL_HANDLE;
    }

    auto texture = model->getFirstTexture();
    if (texture) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture->getImageView();
        imageInfo.sampler = texture->getSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device_->getDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    return descriptorSet;
}

GLTFModel* GameSession::getActivePlayerModel() const {
    if (playerWasMoving_ && gltfWalkModel_ && gltfWalkModel_->getMeshCount() > 0) {
        return gltfWalkModel_.get();
    }
    if (gltfModel_ && gltfModel_->getMeshCount() > 0) {
        return gltfModel_.get();
    }
    return nullptr;
}

VkDescriptorSet GameSession::getActivePlayerDescriptorSet() const {
    if (playerWasMoving_ && gltfWalkModel_ && gltfWalkModel_->getMeshCount() > 0) {
        return gltfWalkModelDescriptorSet_;
    }
    return gltfModelDescriptorSet_;
}

ecs::IGameWorld* GameSession::getECSWorld() const {
    return ecsClientWorld_.get();
}

/**
 * @brief 更新准星指向的可采集目标
 *
 * 在玩家周围 3m 半径内查询 ResourceNodeSystem，
 * 找最近的可用（剩余采集次数 > 0）资源节点。
 * Renderer 在 ImGui HUD 中读取 harvestTarget_ 显示 [F] 提示。
 */
void GameSession::updateHarvestTarget() {
    harvestTarget_ = HarvestTarget{};
    if (!ecsClientWorld_ || resourceNodeSystem_.getNodeCount() == 0) return;

    glm::vec3 playerPos = ecsClientWorld_->getPlayerPosition();
    const float maxRange = 3.0f;

    ResourceNode* bestNode = nullptr;
    float bestDist = maxRange;

    auto nearby = resourceNodeSystem_.queryNodes(playerPos.x, playerPos.z, maxRange);
    for (auto* node : nearby) {
        if (!node || node->remainingHarvests <= 0) continue;

        float dist = glm::length(node->position - playerPos);
        if (dist < bestDist) {
            bestDist = dist;
            bestNode = node;
        }
    }

    if (bestNode) {
        harvestTarget_.valid = true;
        harvestTarget_.type = bestNode->type;
        harvestTarget_.distance = bestDist;
        harvestTarget_.position = bestNode->position;
    }
}

} // namespace owengine
