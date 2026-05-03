// 游戏会话实现 — 封装所有游戏逻辑（ECS/物理/碰撞/网络/动画）
//
// 与渲染层完全解耦：Renderer 不直接拥有游戏状态，
// 仅通过 getCamera()/getInput()/getActivePlayerModel() 接口读取本类维护的数据。
//
// 依赖注入：init() 接收 GameSessionInitParams，包含来自 Renderer 的共享 Vulkan 资源。
// 生命周期：init() 在 Renderer::initVulkan() 之后调用，cleanup() 在 Renderer::cleanup() 之前调用。

#include "core/game_session.h"
#include "core/camera.h"
#include "core/input.h"
#include "core/vulkan_device.h"
#include "utils/asset_paths.h"
#include "renderer/gltf_model.h"
#include "renderer/texture_loader.h"
#include "renderer/terrain_renderer.h"
#include "renderer/tree_system.h"
#include "renderer/stone_system.h"
#include "renderer/grass_system.h"
#include "ecs/client_systems.h"
#include "ecs/ecs.h"
#include "utils/logger.h"
#include <future>
#include <iostream>

namespace owengine {

GameSession::GameSession() = default;

GameSession::~GameSession() {
    cleanup();
}

void GameSession::init(const GameSessionInitParams& params) {
    window_ = params.window;
    windowWidth_ = params.windowWidth;
    windowHeight_ = params.windowHeight;
    device_ = params.device;
    textureLoader_ = params.textureLoader;
    terrainRenderer_ = params.terrainRenderer;
    treeSystem_ = params.treeSystem;
    stoneSystem_ = params.stoneSystem;
    grassSystem_ = params.grassSystem;
    descriptorPool_ = params.descriptorPool;
    textureDescriptorSetLayout_ = params.textureDescriptorSetLayout;
    lightDescriptorSetLayout_ = params.lightDescriptorSetLayout;
    graphicsPipelineLayout_ = params.graphicsPipelineLayout;
    terrainHeightQuery_ = params.terrainHeightQuery;

    // 初始化摄像机（固定窗口尺寸，后续由 Renderer 同步实际尺寸）
    camera_ = std::make_unique<Camera>(windowWidth_, windowHeight_);

    input_ = std::make_unique<Input>(window_);

    // 初始化 ECS 客户端世界
    ecsClientWorld_ = std::make_unique<ecs::ClientWorld>();
    ecsClientWorld_->initClientSystems(window_, windowWidth_, windowHeight_);
    ecsClientWorld_->createClientPlayer(windowWidth_, windowHeight_);
    std::cout << "[GameSession] ECS 系统初始化完成" << std::endl;

    // 注入地形高度查询到 ECS 物理系统
    if (terrainHeightQuery_) {
        ecsClientWorld_->setTerrainQuery(terrainHeightQuery_);
    }

    // 加载玩家模型
    loadPlayerModels();

    // 从现有树/石系统填充资源节点
    resourceNodeSystem_.init(treeSystem_, stoneSystem_);

    // 初始化时间基准
    lastTime_ = std::chrono::high_resolution_clock::now();
}

void GameSession::cleanup() {
    gltfWalkModel_.reset();
    gltfModel_.reset();

    remotePlayerModels_.clear();
    ecsClientWorld_.reset();

    camera_.reset();
    input_.reset();

    terrainRenderer_.reset();
    treeSystem_ = nullptr;
    stoneSystem_ = nullptr;
    textureLoader_.reset();
    device_.reset();
}

void GameSession::update(float deltaTime) {
    if (!ecsClientWorld_ || !camera_) return;

    // 限制 delta time 防止卡顿
    if (deltaTime > ecs::MAX_DELTA_TIME) deltaTime = ecs::MAX_DELTA_TIME;

    // FPS 统计（粗略，用于 HUD 显示）
    static int frameCount = 0;
    static float fpsTimer = 0.0f;
    frameCount++;
    fpsTimer += deltaTime;
    if (fpsTimer >= 1.0f) {
        currentFPS_ = frameCount / fpsTimer;
        frameCount = 0;
        fpsTimer = 0.0f;
        profLogicMs_ = 0.0;  // reset each second, actual value set per frame
    }

    // 更新视锥体（用于 ECS 内部剔除）
    camera_->updateFrustum();

    // 处理网络连接/断开请求
    handleNetworkRequests();

    // 将用户设置同步到 ECS
    ecsClientWorld_->setPlayerSpeed(userMovementSpeed);
    ecsClientWorld_->setPlayerSensitivity(userSensitivity);
    ecsClientWorld_->setPlayerDirection(camera_->getFront(), camera_->getRight());

    if (inventoryOpen_ && ecsClientWorld_) {
        auto* ecsInput = ecsClientWorld_->getInputSystem();
        if (ecsInput) ecsInput->resetMouseDelta();
    }

    // === Phase 1: 同步阶段（输入 + 网络接收，必须主线程） ===
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        ecsClientWorld_->updateSync(deltaTime);
        profLogicMs_ = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0).count();
    }

    // 获取玩家当前位置（异步期间主线程读取）
    glm::vec3 playerPos = ecsClientWorld_->getPlayerPosition();

    // === Phase 1.5: 注入树/石碰撞箱 ===
    injectCollisionBoxes(playerPos);

    // === Phase 2: 异步阶段（纯 CPU 模拟，后台线程） ===
    auto ecsFuture = std::async(std::launch::async, [this, deltaTime]() {
        ecsClientWorld_->updateAsync(deltaTime);
    });

    // === Phase 3: 主线程地形/树/石/草/资源节点更新（与异步 ECS 并行） ===
    if (terrainRenderer_) terrainRenderer_->update(playerPos);
    if (treeSystem_) treeSystem_->update(playerPos, *camera_);
    if (stoneSystem_) stoneSystem_->update(playerPos, *camera_);
    if (grassSystem_) grassSystem_->update(playerPos, *camera_, deltaTime);
    resourceNodeSystem_.update(deltaTime);

    // === Phase 4: 等待异步模拟完成 ===
    ecsFuture.wait();

    // === Phase 5: 发送网络输入 ===
    ecsClientWorld_->sendNetInputs();

    // === Phase 6: 飞行模式切换（R 键） ===
    {
        static bool prevR = false;
        bool curR = glfwGetKey(window_, GLFW_KEY_R) == GLFW_PRESS;
        if (curR && !prevR) ecsClientWorld_->setPlayerFlying(!ecsClientWorld_->isPlayerFlying());
        prevR = curR;
    }
    ecsClientWorld_->updateFlight(deltaTime,
        glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS,
        glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

    // === Phase 7: 从 ECS 同步到摄像机 ===
    ecsClientWorld_->syncCamera(*camera_);
    playerIsFlying_ = ecsClientWorld_->isPlayerFlying();

    // === Phase 8: 第三人称玩家模型动画 ===
    if (camera_->getMode() == Camera::Mode::ThirdPerson) {
        bool isMoving = false;
        float moveYaw = camera_->getYaw();

        if (ecsClientWorld_->isPlayerValid()) {
            auto* r = ecsClientWorld_->getRegistry();
            auto* clientWorld = static_cast<ecs::ClientWorld*>(ecsClientWorld_.get());
            auto player = clientWorld->getPlayer();
            auto* inputComp = r ? r->try_get<ecs::InputStateComponent>(player) : nullptr;
            if (inputComp) {
                isMoving = inputComp->moveForward || inputComp->moveBackward ||
                          inputComp->moveLeft || inputComp->moveRight;
                if (isMoving) {
                    float dirX = 0.0f, dirZ = 0.0f;
                    glm::vec3 camFront = camera_->getFront();
                    camFront.y = 0.0f;
                    camFront = glm::normalize(camFront);
                    glm::vec3 camRight = camera_->getRight();
                    camRight.y = 0.0f;
                    camRight = glm::normalize(camRight);
                    if (inputComp->moveForward)  { dirX += camFront.x; dirZ += camFront.z; }
                    if (inputComp->moveBackward) { dirX -= camFront.x; dirZ -= camFront.z; }
                    if (inputComp->moveLeft)     { dirX -= camRight.x; dirZ -= camRight.z; }
                    if (inputComp->moveRight)    { dirX += camRight.x; dirZ += camRight.z; }
                    if (dirX != 0.0f || dirZ != 0.0f) {
                        moveYaw = glm::degrees(atan2(-dirX, -dirZ));
                    }
                }
            }
        }

        // 切换空闲/行走模型并更新动画
        GLTFModel* activeModel = nullptr;
        if (isMoving && gltfWalkModel_ && gltfWalkModel_->getMeshCount() > 0) {
            activeModel = gltfWalkModel_.get();
            if (!playerWasMoving_) {
                if (gltfWalkModel_->getAnimationCount() > 0) {
                    gltfWalkModel_->playAllAnimations(true, 1.0f);
                }
            }
        } else if (gltfModel_ && gltfModel_->getMeshCount() > 0) {
            activeModel = gltfModel_.get();
        }

        if (activeModel) {
            activeModel->setPosition(camera_->getTarget());
            activeModel->setRotation(0.0f, moveYaw, 0.0f);
            activeModel->updateAnimation(deltaTime);
        }
        playerWasMoving_ = isMoving;
    } else {
        // 非第三人称模式也更新动画
        if (gltfModel_) gltfModel_->updateAnimation(deltaTime);
        if (gltfWalkModel_) gltfWalkModel_->updateAnimation(deltaTime);
    }

    // === Phase 9: 远程玩家模型管理 ===
    updateRemotePlayers(deltaTime);

    // === Phase 10: 背包开关（E 键） ===
    {
        static bool prevE = false;
        bool curE = glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS;
        if (curE && !prevE) {
            inventoryOpen_ = !inventoryOpen_;
            // 打开背包时释放鼠标，关闭时恢复捕获
            bool captured = !inventoryOpen_;
            if (input_) {
                input_->setCursorCaptured(captured);
            }
            if (ecsClientWorld_) {
                auto* ecsInput = ecsClientWorld_->getInputSystem();
                if (ecsInput) {
                    ecsInput->setCursorCaptured(captured);
                    ecsInput->resetMouseDelta();
                }
            }
        }
        prevE = curE;
    }

    // === Phase 10.5: ESC 关闭所有界面 ===
    {
        static bool prevEsc = false;
        bool curEsc = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (curEsc && !prevEsc) {
            bool anyOpen = false;
            if (inventoryOpen_) {
                inventoryOpen_ = false;
                anyOpen = true;
            }
            if (anyOpen) {
                bool captured = true;
                if (input_) input_->setCursorCaptured(captured);
                if (ecsClientWorld_) {
                    auto* ecsInput = ecsClientWorld_->getInputSystem();
                    if (ecsInput) {
                        ecsInput->setCursorCaptured(captured);
                        ecsInput->resetMouseDelta();
                    }
                }
            }
        }
        prevEsc = curEsc;
    }

    // === Phase 11: 采集交互（F 键采集，背包打开时不触发） ===
    {
        // 更新准星指向的目标（背包关闭时显示提示）
        updateHarvestTarget();

        if (!inventoryOpen_) {
            static bool prevFHarvest = false;
            bool curFHarvest = glfwGetKey(window_, GLFW_KEY_F) == GLFW_PRESS;
            if (curFHarvest && !prevFHarvest && harvestTarget_.valid) {
                auto harvested = resourceNodeSystem_.harvest(harvestTarget_.position);
                if (!harvested.isEmpty()) {
                    // 加入玩家背包
                    auto* clientWorld = static_cast<ecs::ClientWorld*>(ecsClientWorld_.get());
                    auto* registry = clientWorld->getRegistry();
                    if (registry) {
                        auto player = clientWorld->getPlayer();
                        if (auto* inv = registry->try_get<ecs::InventoryComponent>(player)) {
                            inv->addItem(harvested.type, harvested.count);
                            Logger::info("[Harvest] 采集到 " + std::string(harvested.name()));
                        }
                    }
                }
            }
            prevFHarvest = curFHarvest;
        }
    }

    // === Phase 12: 快捷栏选择（数字键 1-5 + 滚轮） ===
    {
        static bool prevHotbarKeys[5] = {false};
        static const int hotbarKeys[5] = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5};
        auto* registry = ecsClientWorld_ ? ecsClientWorld_->getRegistry() : nullptr;
        auto* inv = registry ? registry->try_get<ecs::InventoryComponent>(ecsClientWorld_->getPlayer()) : nullptr;

        if (inv) {
            // 数字键 1-5 直接选择
            for (int i = 0; i < 5; i++) {
                bool cur = glfwGetKey(window_, hotbarKeys[i]) == GLFW_PRESS;
                if (cur && !prevHotbarKeys[i]) {
                    inv->selectedHotbarIndex = static_cast<uint32_t>(i);
                }
                prevHotbarKeys[i] = cur;
            }

            // 滚轮切换（仅在背包关闭时生效，避免与背包内滚动冲突）
            if (!inventoryOpen_ && input_) {
                double scroll = input_->consumeScrollY();
                if (scroll > 0.0) {
                    // 上滚 ← 前一个
                    inv->selectedHotbarIndex = (inv->selectedHotbarIndex == 0)
                        ? ecs::InventoryComponent::HOTBAR_SLOTS - 1
                        : inv->selectedHotbarIndex - 1;
                } else if (scroll < 0.0) {
                    // 下滚 → 后一个
                    inv->selectedHotbarIndex = (inv->selectedHotbarIndex >= ecs::InventoryComponent::HOTBAR_SLOTS - 1)
                        ? 0
                        : inv->selectedHotbarIndex + 1;
                }
            }
        }
    }

    // 清除帧输入标记（justPressed 等标记仅持续一帧）
    if (input_) input_->resetJustPressedFlags();
}

void GameSession::handleNetworkRequests() {
    if (!ecsClientWorld_) return;

    if (connectRequested) {
        std::cout << "[GameSession] 正在连接到 " << serverHost << ":" << serverPort << std::endl;
        if (ecsClientWorld_->connectToServer(serverHost, static_cast<uint16_t>(serverPort))) {
            std::cout << "[GameSession] 连接成功" << std::endl;
        } else {
            std::cerr << "[GameSession] 连接失败" << std::endl;
        }
        connectRequested = false;
    }

    if (disconnectRequested) {
        ecsClientWorld_->disconnectFromServer();
        disconnectRequested = false;
        std::cout << "[GameSession] 已断开连接" << std::endl;
    }
}

void GameSession::injectCollisionBoxes(const glm::vec3& playerPos) {
    if (!ecsClientWorld_) return;

    auto* clientWorld = static_cast<ecs::ClientWorld*>(ecsClientWorld_.get());
    auto* moveSys = clientWorld->getMovementSystem();
    auto* physSys = clientWorld->getPhysicsSystem();
    if (!moveSys) return;

    moveSys->clearCollisionBoxes();
    if (physSys) physSys->clearCollisionBoxes();

    constexpr float PLAYER_Y_OFFSET = 0.9f;  // colliderHeight(1.8) / 2

    // 查询玩家附近树木（半径 25m）
    if (treeSystem_) {
        auto trees = treeSystem_->queryPositions(playerPos.x, playerPos.z, 25.0f);
        for (const auto& [pos, scale] : trees) {
            float r = std::max(0.5f, 0.3f * scale);
            float h = 3.0f;
            glm::vec3 boxPos = pos;
            boxPos.y += PLAYER_Y_OFFSET;
            moveSys->addCollisionBox(boxPos, glm::vec3(r * 2, h, r * 2));
        }
    }

    // 查询玩家附近石头
    if (stoneSystem_) {
        auto stones = stoneSystem_->queryPositions(playerPos.x, playerPos.z, 25.0f);
        for (const auto& [pos, scale] : stones) {
            float r = std::max(0.3f, 0.25f * scale);
            float h = 0.8f * scale;
            glm::vec3 boxPos = pos;
            boxPos.y += PLAYER_Y_OFFSET;
            moveSys->addCollisionBox(boxPos, glm::vec3(r * 2, h, r * 2));
            if (physSys) {
                glm::vec3 physPos = pos;
                physPos.y += h * 0.5f;
                physSys->addCollisionBox(physPos, glm::vec3(r * 2, h, r * 2));
            }
        }
    }
}

void GameSession::loadPlayerModels() {
    // 从配置文件加载玩家模型：idle + walk
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

ecs::IGameWorld* GameSession::getECSWorld() const {
    return ecsClientWorld_.get();
}

void GameSession::updateHarvestTarget() {
    harvestTarget_ = HarvestTarget{};
    if (!ecsClientWorld_ || resourceNodeSystem_.getNodeCount() == 0) return;

    // 使用玩家位置而非相机位置（第三人称相机在玩家身后 8m）
    glm::vec3 playerPos = ecsClientWorld_->getPlayerPosition();
    const float maxRange = 3.0f;

    // 纯距离判定：找最近的可采集节点（无需准星对准）
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
