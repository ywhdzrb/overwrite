// 游戏会话网络模块 — 连接管理 + 远程玩家同步
//
// 连接管理通过 GameSession 的 public 成员 connectRequested/disconnectRequested
// 由 ImGui 面板（Render 侧）写入，本模块在主线程 update 中消费处理。
// 远程玩家模型跟随 ECS NetworkSystem 的远程玩家列表创建/销毁/位置同步。

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
 * @brief 处理网络连接/断开请求
 *
 * 消费 connectRequested/disconnectRequested 标记（由 ImGui 面板设置），
 * 调用 ECS ClientWorld 的连接/断开方法。
 */
void GameSession::handleNetworkRequests() {
    if (!ecsClientWorld_) return;

    if (connectRequested) {
        Logger::info("[GameSession] 正在连接到 " + std::string(serverHost) + ":" + std::to_string(serverPort));
        if (ecsClientWorld_->connectToServer(serverHost, static_cast<uint16_t>(serverPort))) {
            Logger::info("[GameSession] 连接成功");
        } else {
            Logger::error("[GameSession] 连接失败");
        }
        connectRequested = false;
    }

    if (disconnectRequested) {
        ecsClientWorld_->disconnectFromServer();
        disconnectRequested = false;
        Logger::info("[GameSession] 已断开连接");
    }
}

/**
 * @brief 更新远程玩家模型
 *
 * 遍历 ECS NetworkSystem 中的远程玩家列表，为新增玩家创建
 * idle/walk GLTF 模型对，为现有玩家更新位置/旋转/动画状态，
 * 清理已离开玩家的模型资源。
 */
void GameSession::updateRemotePlayers(float deltaTime) {
    if (!ecsClientWorld_ || !ecsClientWorld_->isConnectedToServer()) {
        remotePlayerModels_.clear();
        return;
    }

    auto* clientWorld = static_cast<ecs::ClientWorld*>(ecsClientWorld_.get());
    if (!clientWorld) return;

    auto& remotePlayers = clientWorld->getNetworkSystem()->getRemotePlayers();

    // 移除已离开的玩家
    for (auto it = remotePlayerModels_.begin(); it != remotePlayerModels_.end(); ) {
        if (remotePlayers.find(it->first) == remotePlayers.end()) {
            it = remotePlayerModels_.erase(it);
        } else {
            ++it;
        }
    }

    // 更新或创建远程玩家模型
    for (const auto& [clientId, player] : remotePlayers) {
        if (!player.active) continue;

        auto it = remotePlayerModels_.find(clientId);
        if (it == remotePlayerModels_.end()) {
            // 创建新远程玩家模型
            RemotePlayerModels models;
            auto idleModel = std::make_unique<GLTFModel>(device_, textureLoader_);
            if (idleModel->loadFromFile(AssetPaths::PLAYER_IDLE_MODEL)) {
                idleModel->setScale(glm::vec3(ecs::PLAYER_MODEL_SCALE));

                if (descriptorPool_ != VK_NULL_HANDLE && textureDescriptorSetLayout_ != VK_NULL_HANDLE) {
                    idleModel->createMeshDescriptorSets(textureDescriptorSetLayout_, descriptorPool_);
                }
                models.idleModel = std::move(idleModel);
            }

            auto walkModel = std::make_unique<GLTFModel>(device_, textureLoader_);
            if (walkModel->loadFromFile(AssetPaths::PLAYER_WALK_MODEL)) {
                walkModel->setScale(glm::vec3(ecs::PLAYER_MODEL_SCALE));
                walkModel->setPosition(player.position);
                if (walkModel->getAnimationCount() > 0) {
                    walkModel->playAllAnimations(true, 1.0f);
                }
                if (descriptorPool_ != VK_NULL_HANDLE && textureDescriptorSetLayout_ != VK_NULL_HANDLE) {
                    walkModel->createMeshDescriptorSets(textureDescriptorSetLayout_, descriptorPool_);
                }
                models.walkModel = std::move(walkModel);
            }

            remotePlayerModels_[clientId] = std::move(models);
        } else {
            // 更新现有模型位置/动画
            if (it->second.idleModel) {
                it->second.idleModel->setPosition(player.position);
                it->second.idleModel->setRotation(0.0f, player.yaw, 0.0f);
            }
            if (it->second.walkModel) {
                it->second.walkModel->setPosition(player.position);
                float renderYaw = player.isMoving ? player.moveYaw : player.yaw;
                it->second.walkModel->setRotation(0.0f, renderYaw, 0.0f);

                if (player.isMoving) {
                    if (!it->second.walkModel->isAnimationPlaying()) {
                        it->second.walkModel->playAllAnimations(true, 1.0f);
                    }
                    it->second.walkModel->updateAnimation(deltaTime);
                } else {
                    if (it->second.walkModel->isAnimationPlaying()) {
                        it->second.walkModel->stopAnimation();
                    }
                }
            }
            it->second.wasMoving = player.isMoving;
        }
    }
}

} // namespace owengine
