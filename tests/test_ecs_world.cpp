/**
 * @file test_ecs_world.cpp
 * @brief ECS World 实体管理单元测试
 *
 * 测试共享 World 类的实体创建/销毁、玩家预设、
 * 相机/玩家引用，以及多实体场景。
 * 依赖 OverWriteShared 库（含 EnTT ECS）。
 */
#include <gtest/gtest.h>
#include "ecs/systems.hpp"
#include "ecs/components.hpp"

using namespace owengine::ecs;

// ==================== 基础实体管理 ====================

TEST(ECSTest, WorldCanBeCreated) {
    World world;
    SUCCEED();  // 创建不崩溃即通过
}

TEST(ECSTest, CreateEntity) {
    World world;
    entt::entity e = world.createEntity();
    EXPECT_TRUE(e != entt::null);
}

TEST(ECSTest, CreateMultipleEntities) {
    World world;
    auto e1 = world.createEntity();
    auto e2 = world.createEntity();
    auto e3 = world.createEntity();
    EXPECT_NE(e1, e2);
    EXPECT_NE(e2, e3);
    EXPECT_NE(e1, e3);
}

TEST(ECSTest, DestroyEntity) {
    World world;
    auto e = world.createEntity();
    EXPECT_NO_THROW(world.destroyEntity(e));
}

TEST(ECSTest, DestroyNonexistentEntityNoThrow) {
    World world;
    // EnTT destroy 对已销毁的实体不会崩溃
    auto e = world.createEntity();
    world.destroyEntity(e);
    EXPECT_NO_THROW(world.destroyEntity(e));
}

TEST(ECSTest, EntityCanHaveComponent) {
    World world;
    auto e = world.createEntity();
    auto& pos = world.registry().emplace<TransformComponent>(e);
    pos.position = glm::vec3(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(world.registry().all_of<TransformComponent>(e));
    auto& readback = world.registry().get<TransformComponent>(e);
    EXPECT_EQ(readback.position.x, 1.0f);
    EXPECT_EQ(readback.position.y, 2.0f);
    EXPECT_EQ(readback.position.z, 3.0f);
}

TEST(ECSTest, MultipleComponentsOnEntity) {
    World world;
    auto e = world.createEntity();
    world.registry().emplace<TransformComponent>(e);
    world.registry().emplace<VelocityComponent>(e);
    world.registry().emplace<NameComponent>(e, "测试实体");

    EXPECT_TRUE(world.registry().all_of<TransformComponent>(e));
    EXPECT_TRUE(world.registry().all_of<VelocityComponent>(e));
    EXPECT_TRUE(world.registry().all_of<NameComponent>(e));
}

// ==================== 玩家实体创建 ====================

TEST(ECSTest, CreatePlayer) {
    World world;
    auto player = world.createPlayer();
    EXPECT_TRUE(player != entt::null);
}

TEST(ECSTest, CreatePlayerHasAllComponents) {
    World world;
    auto player = world.createPlayer();

    EXPECT_TRUE(world.registry().all_of<TransformComponent>(player));
    EXPECT_TRUE(world.registry().all_of<VelocityComponent>(player));
    EXPECT_TRUE(world.registry().all_of<MovementControllerComponent>(player));
    EXPECT_TRUE(world.registry().all_of<PhysicsComponent>(player));
    EXPECT_TRUE(world.registry().all_of<InputStateComponent>(player));
    EXPECT_TRUE(world.registry().all_of<PlayerTag>(player));
    EXPECT_TRUE(world.registry().all_of<NameComponent>(player));
    EXPECT_TRUE(world.registry().all_of<NetworkSyncComponent>(player));
    EXPECT_TRUE(world.registry().all_of<EntityTypeComponent>(player));
}

TEST(ECSTest, CreatePlayerInitialPosition) {
    World world;
    auto player = world.createPlayer();
    auto& transform = world.registry().get<TransformComponent>(player);
    // 玩家初始位置在 (0, 0.9, 5)
    EXPECT_FLOAT_EQ(transform.position.x, 0.0f);
    EXPECT_FLOAT_EQ(transform.position.y, 0.9f);
    EXPECT_FLOAT_EQ(transform.position.z, 5.0f);
}

TEST(ECSTest, CreatePlayerHasPlayerTag) {
    World world;
    auto player = world.createPlayer();
    auto& tag = world.registry().get<PlayerTag>(player);
    EXPECT_EQ(tag.playerId, 0u);
}

TEST(ECSTest, CreatePlayerSetsPlayerRef) {
    World world;
    auto player = world.createPlayer();
    EXPECT_EQ(world.getPlayer(), player);
}

// ==================== 相机引用 ====================

TEST(ECSTest, CameraDefaultsToNull) {
    World world;
    EXPECT_TRUE(world.getMainCamera() == entt::null);
}

TEST(ECSTest, SetAndGetCamera) {
    World world;
    auto camera = world.createEntity();
    world.setMainCamera(camera);
    EXPECT_EQ(world.getMainCamera(), camera);
}

TEST(ECSTest, OverrideCamera) {
    World world;
    auto cam1 = world.createEntity();
    auto cam2 = world.createEntity();
    world.setMainCamera(cam1);
    world.setMainCamera(cam2);
    EXPECT_EQ(world.getMainCamera(), cam2);
}

// ==================== 玩家引用 ====================

TEST(ECSTest, PlayerDefaultsToNull) {
    World world;
    EXPECT_TRUE(world.getPlayer() == entt::null);
}

TEST(ECSTest, SetAndGetPlayer) {
    World world;
    auto entity = world.createEntity();
    world.setPlayer(entity);
    EXPECT_EQ(world.getPlayer(), entity);
}

TEST(ECSTest, CreateMultiplePlayers) {
    World world;
    auto p1 = world.createPlayer();
    auto p2 = world.createPlayer();  // 第二次创建会覆盖 player_ 引用
    EXPECT_EQ(world.getPlayer(), p2);
    EXPECT_NE(p1, p2);
}

// ==================== 注册表操作 ====================

TEST(ECSTest, RegistryIsAccessible) {
    World world;
    auto& registry = world.registry();
    auto e = registry.create();
    registry.emplace<TransformComponent>(e);
    EXPECT_TRUE(registry.all_of<TransformComponent>(e));
}

TEST(ECSTest, RegistrySizeAfterCreateAndDestroy) {
    World world;
    std::vector<entt::entity> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(world.createEntity());
    }
    for (auto e : entities) {
        world.destroyEntity(e);
    }
    // 新创建的实体应该用已回收的 ID
    auto fresh = world.createEntity();
    EXPECT_TRUE(fresh != entt::null);
}
