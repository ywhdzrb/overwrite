// 游戏会话主循环更新 — 每帧：输入同步→ECS异步模拟→碰撞→动画→网络
//
// 流水线阶段：
//   Phase 1:  主线程同步输入 + 碰撞箱注入
//   Phase 2:  std::async 后台 ECS 模拟
//   Phase 3:  主线程地形/树/石/草更新（与 Phase 2 并行）
//   Phase 4:  等待异步完成
//   Phase 5-12: 飞行/相机/动画/背包/采集/快捷栏/渲染同步

#include "core/game_session.hpp"
#include "core/camera.hpp"
#include "core/input.hpp"
#include "core/vulkan_device.hpp"
#include "renderer/gltf_model.hpp"
#include "renderer/terrain_renderer.hpp"
#include "renderer/tree_system.hpp"
#include "renderer/stone_system.hpp"
#include "renderer/grass_system.hpp"
#include "ecs/client_systems.hpp"
#include "ecs/client_components.hpp"
#include "ecs/ecs.hpp"
#include "utils/logger.hpp"
#include <future>

namespace owengine {

/**
 * @brief 每帧更新游戏逻辑
 *
 * 流水线设计，异步入 ECS 模拟与主线程地形/树/草更新并行，
 * 主线程空闲时等待异步结果，最后同步背包/动画/网络状态。
 */
void GameSession::update(float deltaTime) {
    if (!ecsClientWorld_ || !camera_) return;

    // 限制 delta time 防止卡顿
    if (deltaTime > ecs::MAX_DELTA_TIME) deltaTime = ecs::MAX_DELTA_TIME;

    // FPS 统计（粗略，用于 HUD 显示）
    fpsFrameCount_++;
    fpsTimer_ += deltaTime;
    if (fpsTimer_ >= 1.0f) {
        currentFPS_ = fpsFrameCount_ / fpsTimer_;
        fpsFrameCount_ = 0;
        fpsTimer_ = 0.0f;
        profLogicMs_ = 0.0;
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

    // === Phase 4: 等待异步模拟完成，通过 get() 传播后台异常防止 std::terminate ===
    ecsFuture.get();

    // === Phase 5: 发送网络输入 ===
    ecsClientWorld_->sendNetInputs();

    // === Phase 6: 飞行模式切换（R 键） ===
    if (input_ && input_->isKeyJustPressed(GLFW_KEY_R)) {
        ecsClientWorld_->setPlayerFlying(!ecsClientWorld_->isPlayerFlying());
    }
    ecsClientWorld_->updateFlight(deltaTime,
        input_ ? input_->isKeyPressed(GLFW_KEY_SPACE) : false,
        (input_ && input_->isKeyPressed(GLFW_KEY_LEFT_SHIFT)) ||
        (input_ && input_->isKeyPressed(GLFW_KEY_RIGHT_SHIFT)));

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
                isMoving = inputComp->isMoveForward() || inputComp->isMoveBackward() ||
                          inputComp->isMoveLeft() || inputComp->isMoveRight();
                if (isMoving) {
                    float dirX = 0.0f, dirZ = 0.0f;
                    glm::vec3 camFront = camera_->getFront();
                    camFront.y = 0.0f;
                    camFront = glm::normalize(camFront);
                    glm::vec3 camRight = camera_->getRight();
                    camRight.y = 0.0f;
                    camRight = glm::normalize(camRight);
                    if (inputComp->isMoveForward())  { dirX += camFront.x; dirZ += camFront.z; }
                    if (inputComp->isMoveBackward()) { dirX -= camFront.x; dirZ -= camFront.z; }
                    if (inputComp->isMoveLeft())     { dirX -= camRight.x; dirZ -= camRight.z; }
                    if (inputComp->isMoveRight())    { dirX += camRight.x; dirZ += camRight.z; }
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
    if (input_ && input_->isKeyJustPressed(GLFW_KEY_E)) {
        inventoryOpen_ = !inventoryOpen_;
        bool captured = !inventoryOpen_;
        input_->setCursorCaptured(captured);
        if (ecsClientWorld_) {
            auto* ecsInput = ecsClientWorld_->getInputSystem();
            if (ecsInput) {
                ecsInput->resetMouseDelta();
            }
        }
    }

    // === Phase 10.5: ESC 关闭所有界面 ===
    if (input_ && input_->isKeyJustPressed(GLFW_KEY_ESCAPE)) {
        bool anyOpen = false;
        if (inventoryOpen_) {
            inventoryOpen_ = false;
            anyOpen = true;
        }
        if (anyOpen) {
            bool captured = true;
            input_->setCursorCaptured(captured);
            if (ecsClientWorld_) {
                auto* ecsInput = ecsClientWorld_->getInputSystem();
                if (ecsInput) {
                    ecsInput->resetMouseDelta();
                }
            }
        }
    }

    // === Phase 11: 采集交互（F 键采集，背包打开时不触发） ===
    {
        updateHarvestTarget();

        if (!inventoryOpen_ && input_ && input_->isKeyJustPressed(GLFW_KEY_F) && harvestTarget_.valid) {
            auto harvested = resourceNodeSystem_.harvest(harvestTarget_.position);
            if (!harvested.isEmpty()) {
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
    }

    // === Phase 12: 快捷栏选择（数字键 1-5 + 滚轮） ===
    {
        static const int hotbarKeys[5] = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5};
        auto* registry = ecsClientWorld_ ? ecsClientWorld_->getRegistry() : nullptr;
        auto* inv = registry ? registry->try_get<ecs::InventoryComponent>(ecsClientWorld_->getPlayer()) : nullptr;

        if (inv && input_) {
            for (int i = 0; i < 5; i++) {
                if (input_->isKeyJustPressed(hotbarKeys[i])) {
                    inv->selectedHotbarIndex = static_cast<uint32_t>(i);
                }
            }

            if (!inventoryOpen_ && input_) {
                double scroll = input_->consumeScrollY();
                if (scroll > 0.0) {
                    inv->selectedHotbarIndex = (inv->selectedHotbarIndex == 0)
                        ? ecs::InventoryComponent::HOTBAR_SLOTS - 1
                        : inv->selectedHotbarIndex - 1;
                } else if (scroll < 0.0) {
                    inv->selectedHotbarIndex = (inv->selectedHotbarIndex >= ecs::InventoryComponent::HOTBAR_SLOTS - 1)
                        ? 0
                        : inv->selectedHotbarIndex + 1;
                }
            }
        }
    }

    // === Phase 12.5: 放置熔炉（Q 键，背包关闭时有效） ===
    if (input_ && input_->isKeyJustPressed(GLFW_KEY_Q) && !inventoryOpen_) {
        glm::vec3 camPos = camera_->getPosition();
        glm::vec3 camFront = camera_->getFront();
        camFront.y = 0.0f;
        if (glm::length(camFront) > 0.001f) {
            camFront = glm::normalize(camFront);
        } else {
            camFront = glm::vec3(0.0f, 0.0f, -1.0f);
        }

        glm::vec3 placePos = camPos + camFront * 4.0f;

        // 采样地形高度得到 Y
        if (terrainHeightQuery_) {
            placePos.y = terrainHeightQuery_(placePos.x, placePos.z);
        }

        auto* registry = ecsClientWorld_->getRegistry();
        if (registry) {
            auto entity = registry->create();
            auto& transform = registry->emplace<ecs::TransformComponent>(entity);
            transform.position = placePos;

            registry->emplace<ecs::EntityTypeComponent>(entity, ecs::EntityType::Furnace);

            auto& render = registry->emplace<ecs::RenderComponent>(entity);
            render.modelPath = "assets/models/furnace.glb";
            render.position = placePos;
            render.dirty = true;

            std::string posStr = "(" + std::to_string(placePos.x) + ", "
                               + std::to_string(placePos.y) + ", "
                               + std::to_string(placePos.z) + ")";
            Logger::info("[Placement] 熔炉已放置于 " + posStr);
        }
    }

    // === Phase 13: ECS 渲染系统同步（模型加载/卸载/变换同步） ===
    if (renderSystem_) {
        renderSystem_->update(deltaTime);
    }

    // 清除帧输入标记（justPressed 等标记仅持续一帧）
    if (input_) input_->resetJustPressedFlags();
}

/**
 * @brief 将树/石位置的碰撞箱注入 ECS 移动/物理系统
 *
 * 每帧从 TreeSystem/StoneSystem 查询玩家周围 25m 的物体，
 * 生成对应尺寸的 AABB 碰撞箱，为 MovementSystem 和 PhysicsSystem 提供碰撞检测数据。
 */
void GameSession::injectCollisionBoxes(const glm::vec3& playerPos) {
    if (!ecsClientWorld_) return;

    auto* clientWorld = static_cast<ecs::ClientWorld*>(ecsClientWorld_.get());
    auto* moveSys = clientWorld->getMovementSystem();
    auto* physSys = clientWorld->getPhysicsSystem();
    if (!moveSys) return;

    moveSys->clearCollisionBoxes();
    if (physSys) physSys->clearCollisionBoxes();

    constexpr float PLAYER_Y_OFFSET = 0.9f;

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

    // 查询玩家附近 ECS 静态实体（熔炉等）
    {
        auto* registry = ecsClientWorld_->getRegistry();
        if (registry) {
            auto ecsView = registry->view<ecs::EntityTypeComponent, ecs::TransformComponent>();
            for (auto entity : ecsView) {
                const auto& typeComp = ecsView.get<ecs::EntityTypeComponent>(entity);
                if (typeComp.type != ecs::EntityType::Furnace) continue;

                const auto& transComp = ecsView.get<ecs::TransformComponent>(entity);
                float dx = transComp.position.x - playerPos.x;
                float dz = transComp.position.z - playerPos.z;
                if (dx * dx + dz * dz > 25.0f * 25.0f) continue;

                glm::vec3 boxSize = furnaceCollisionSize_;
                glm::vec3 boxCenter = transComp.position + furnaceBboxMin_ + boxSize * 0.5f;
                moveSys->addCollisionBox(boxCenter, boxSize);
                if (physSys) {
                    glm::vec3 physCenter = transComp.position + furnaceBboxMin_ + boxSize * 0.5f;
                    physSys->addCollisionBox(physCenter, boxSize);
                }
            }
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

} // namespace owengine
