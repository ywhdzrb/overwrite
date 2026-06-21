/**
 * @file test_physics_system.cpp
 * @brief PhysicsSystem 物理系统单元测试
 *
 * 测试 AABB 碰撞检测、地形高度查询、重力模拟等核心逻辑。
 * 依赖 OverWriteShared 库（含 EnTT ECS 和 Logger）。
 *
 * 注意：PhysicsSystem::update 需要 TransformComponent +
 * PhysicsComponent + VelocityComponent 三者齐全。
 */
#include <gtest/gtest.h>
#include <glm/gtc/epsilon.hpp>
#include "ecs/systems.hpp"
#include "ecs/components.hpp"

using namespace owengine::ecs;

// ==================== 辅助函数 ====================

// 浮点 vector 近似比较
static bool vec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

// ==================== PhysicsSystem 构造 ====================

TEST(PhysicsSystemTest, CanBeCreated) {
    World world;
    PhysicsSystem physics(world);
    SUCCEED();
}

TEST(PhysicsSystemTest, DefaultGroundHeight) {
    World world;
    PhysicsSystem physics(world);
    EXPECT_FLOAT_EQ(physics.getDefaultGroundHeight(), -100.0f);
}

TEST(PhysicsSystemTest, HasNoTerrainQueryByDefault) {
    World world;
    PhysicsSystem physics(world);
    EXPECT_FALSE(physics.hasTerrainQuery());
}

// ==================== 碰撞箱管理 ====================

TEST(PhysicsSystemTest, AddCollisionBox) {
    World world;
    PhysicsSystem physics(world);
    physics.addCollisionBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));
    SUCCEED();
}

TEST(PhysicsSystemTest, ClearCollisionBoxes) {
    World world;
    PhysicsSystem physics(world);
    physics.addCollisionBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));
    physics.clearCollisionBoxes();
    SUCCEED();
}

// ==================== AABB 碰撞检测（私有方法测试 - 通过公有接口验证行为） ====================

TEST(PhysicsSystemTest, DefaultGroundHeightQuery) {
    World world;
    PhysicsSystem physics(world);
    // 无地形查询且无碰撞箱时，返回默认地面高度
    float h = physics.getTerrainHeight(100.0f, 200.0f);
    EXPECT_FLOAT_EQ(h, -100.0f);
}

TEST(PhysicsSystemTest, TerrainQueryInjection) {
    World world;
    PhysicsSystem physics(world);

    // 注入一个线性地形高度函数：h = x * 0.1 + z * 0.05
    physics.setTerrainQuery([](float x, float z) -> float {
        return x * 0.1f + z * 0.05f;
    });

    EXPECT_TRUE(physics.hasTerrainQuery());
    float h = physics.getTerrainHeight(100.0f, 200.0f);
    EXPECT_FLOAT_EQ(h, 100.0f * 0.1f + 200.0f * 0.05f);  // = 20.0f
}

TEST(PhysicsSystemTest, ClearTerrainQuery) {
    World world;
    PhysicsSystem physics(world);
    physics.setTerrainQuery([](float, float) { return 50.0f; });
    physics.clearTerrainQuery();
    EXPECT_FALSE(physics.hasTerrainQuery());
}

// ==================== 高度查询缓存 ====================

TEST(PhysicsSystemTest, HeightQueryCaching) {
    World world;
    PhysicsSystem physics(world);
    int queryCount = 0;

    physics.setTerrainQuery([&queryCount](float x, float z) -> float {
        ++queryCount;
        return x + z;
    });

    // 第一次查询应调用地形函数
    float h1 = physics.getTerrainHeight(10.0f, 20.0f);
    EXPECT_FLOAT_EQ(h1, 30.0f);
    int countAfterFirst = queryCount;

    // 相同坐标第二次查询应命中缓存（不调用地形函数）
    float h2 = physics.getTerrainHeight(10.0f, 20.0f);
    EXPECT_FLOAT_EQ(h2, 30.0f);
    EXPECT_EQ(queryCount, countAfterFirst);  // 缓存命中，count 不变

    // 不同坐标应重新查询
    float h3 = physics.getTerrainHeight(15.0f, 25.0f);
    EXPECT_FLOAT_EQ(h3, 40.0f);
    EXPECT_GT(queryCount, countAfterFirst);
}

TEST(PhysicsSystemTest, HeightQueryNoCacheOnDifferentCoord) {
    World world;
    PhysicsSystem physics(world);
    physics.setTerrainQuery([](float x, float z) -> float {
        return x * 2.0f;
    });

    float h1 = physics.getTerrainHeight(5.0f, 0.0f);
    float h2 = physics.getTerrainHeight(10.0f, 0.0f);
    EXPECT_FLOAT_EQ(h1, 10.0f);
    EXPECT_FLOAT_EQ(h2, 20.0f);
}

// ==================== 法向量计算 ====================

TEST(PhysicsSystemTest, ComputeNormalWithoutTerrain) {
    World world;
    PhysicsSystem physics(world);
    // 无地形查询时，computeTerrainNormal 通过 getTerrainHeight 调用，
    // 但 computeTerrainNormal 内部检测 terrainQuery_ == null 后返回上向量
    // 注意：此方法为私有，通过 PhysicsSystem::update 间接测试

    // 创建实体，使其检查地面法向量
    auto entity = world.registry().create();
    world.registry().emplace<TransformComponent>(entity, glm::vec3(0.0f, 10.0f, 0.0f));
    world.registry().emplace<PhysicsComponent>(entity);
    auto& vel = world.registry().emplace<VelocityComponent>(entity);

    // 默认 isGrounded=true，需要设为 false 才能受重力影响
    auto& physComp = world.registry().get<PhysicsComponent>(entity);
    physComp.setGrounded(false);
    physComp.setUseGravity(true);

    // 更新一帧，应使实体下落
    // 默认 defaultGroundHeight = -100.0f，所以脚底（position.y - 0.9=9.1）远高于地面
    // 这一帧会应用重力加速下落但不会触及地面
    physics.update(0.016f);

    // 位置应下降（重力加速中）
    auto& transform = world.registry().get<TransformComponent>(entity);
    EXPECT_LT(transform.position.y, 10.0f);
}

// ==================== 重力与地面检测（集成测试） ====================

TEST(PhysicsSystemTest, EntityFallsDueToGravity) {
    World world;
    PhysicsSystem physics(world);

    auto entity = world.registry().create();
    world.registry().emplace<TransformComponent>(entity, glm::vec3(0.0f, 10.0f, 0.0f));
    world.registry().emplace<PhysicsComponent>(entity);
    auto& vel = world.registry().emplace<VelocityComponent>(entity);

    vel.linear.y = 0.0f;
    // isGrounded = true 默认，要先让它变成非着地
    auto& phys = world.registry().get<PhysicsComponent>(entity);
    phys.setGrounded(false);

    // 模拟 60 帧（~0.996 秒）
    for (int i = 0; i < 60; ++i) {
        physics.update(0.0166f);
    }

    auto& transform = world.registry().get<TransformComponent>(entity);
    // Y 位置应该显著下降（重力加速度 15m/s² × 1s ≈ 7.5m 下落距离）
    EXPECT_LT(transform.position.y, 5.0f);
}

TEST(PhysicsSystemTest, EntityLandsOnGround) {
    World world;
    PhysicsSystem physics(world);

    // 设置地面高度为 0
    physics.setDefaultGroundHeight(0.0f);

    auto entity = world.registry().create();
    // 站在地面以上一点，colliderHeight=1.8，所以脚底在 y - 0.9
    world.registry().emplace<TransformComponent>(entity, glm::vec3(0.0f, 1.0f, 0.0f));
    world.registry().emplace<PhysicsComponent>(entity);
    world.registry().emplace<VelocityComponent>(entity);

    auto& phys = world.registry().get<PhysicsComponent>(entity);
    phys.setGrounded(false);
    phys.setUseGravity(true);

    // 模拟足够多的帧让实体落地
    for (int i = 0; i < 100; ++i) {
        physics.update(0.016f);
    }

    auto& transform = world.registry().get<TransformComponent>(entity);
    auto& physicsComp = world.registry().get<PhysicsComponent>(entity);

    // 落地后 isGrounded 应为 true
    EXPECT_TRUE(physicsComp.isGrounded());
    // 脚底位置 = groundHeight + colliderHeight/2
    // groundHeight = 0 (default), colliderHeight = 1.8 → Y = 0.9
    EXPECT_NEAR(transform.position.y, 0.9f, 0.05f);
}

TEST(PhysicsSystemTest, JumpSetsVelocity) {
    World world;
    PhysicsSystem physics(world);

    auto entity = world.registry().create();
    world.registry().emplace<TransformComponent>(entity, glm::vec3(0.0f, 0.9f, 0.0f));
    auto& phys = world.registry().emplace<PhysicsComponent>(entity);
    world.registry().emplace<VelocityComponent>(entity);
    auto& input = world.registry().emplace<InputStateComponent>(entity);

    // 设置着地状态，请求跳跃
    phys.setGrounded(true);
    phys.setJumping(false);
    input.setJump(true);

    physics.update(0.016f);

    auto& vel = world.registry().get<VelocityComponent>(entity);
    // 跳跃应给 Y 轴正向速度
    EXPECT_GT(vel.linear.y, 0.0f);
    EXPECT_TRUE(phys.isJumping());
}

TEST(PhysicsSystemTest, CannotJumpWhileAirborne) {
    World world;
    PhysicsSystem physics(world);

    auto entity = world.registry().create();
    world.registry().emplace<TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
    auto& phys = world.registry().emplace<PhysicsComponent>(entity);
    auto& vel = world.registry().emplace<VelocityComponent>(entity);
    auto& input = world.registry().emplace<InputStateComponent>(entity);

    // 空中状态，请求跳跃
    phys.setGrounded(false);
    phys.setJumping(false);
    vel.linear.y = -2.0f;
    input.setJump(true);

    // 记录跳跃前的 Y 速度
    float yVelBefore = vel.linear.y;

    physics.update(0.016f);

    // 空中的跳跃请求应被忽略
    EXPECT_FALSE(phys.isJumping());
    // Y 速度应仍为负（重力作用）
    EXPECT_LT(vel.linear.y, 0.0f);
}

// ==================== 碰撞盒地面检测 ====================

TEST(PhysicsSystemTest, CollisionBoxAsGround) {
    World world;
    PhysicsSystem physics(world);

    // 添加一个碰撞箱作为地面：位置 (0, -0.5, 0)，大小 (10, 1, 10)
    // 顶面高度 = -0.5 + 0.5 = 0.0
    physics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(10.0f, 1.0f, 10.0f));

    auto entity = world.registry().create();
    world.registry().emplace<TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
    world.registry().emplace<PhysicsComponent>(entity);
    world.registry().emplace<VelocityComponent>(entity);

    auto& phys = world.registry().get<PhysicsComponent>(entity);
    phys.setGrounded(false);

    // 模拟下落
    for (int i = 0; i < 200; ++i) {
        physics.update(0.016f);
    }

    auto& transform = world.registry().get<TransformComponent>(entity);
    // 应落在碰撞箱顶面 (0.0) + colliderHeight/2 (0.9) = 0.9
    EXPECT_NEAR(transform.position.y, 0.9f, 0.1f);
    EXPECT_TRUE(phys.isGrounded());
}

// ==================== 多实体物理 ====================

TEST(PhysicsSystemTest, MultipleEntitiesPhysics) {
    World world;
    PhysicsSystem physics(world);
    physics.setDefaultGroundHeight(0.0f);

    // 创建三个不同高度的实体
    struct TestEntity {
        entt::entity e;
        TransformComponent* transform;
        PhysicsComponent* physics;
    };

    std::vector<TestEntity> entities;
    for (int i = 0; i < 3; ++i) {
        auto e = world.registry().create();
        float height = static_cast<float>(5 + i * 3);
        world.registry().emplace<TransformComponent>(e, glm::vec3(0.0f, height, 0.0f));
        auto& phys = world.registry().emplace<PhysicsComponent>(e);
        phys.setGrounded(false);
        world.registry().emplace<VelocityComponent>(e);

        entities.push_back({
            e,
            &world.registry().get<TransformComponent>(e),
            &world.registry().get<PhysicsComponent>(e)
        });
    }

    // 模拟 200 帧使全部落地
    for (int i = 0; i < 200; ++i) {
        physics.update(0.016f);
    }

    // 所有实体都应落地
    for (auto& te : entities) {
        EXPECT_TRUE(te.physics->isGrounded())
            << "实体应该已落地";
        EXPECT_NEAR(te.transform->position.y, 0.9f, 0.1f)
            << "实体 Y 位置应在地面高度";
    }
}

// ==================== 地形高度查询异常 ====================

TEST(PhysicsSystemTest, TerrainQueryExceptionHandling) {
    World world;
    PhysicsSystem physics(world);

    // 注入会抛异常的地形查询
    physics.setTerrainQuery([](float, float) -> float {
        throw std::runtime_error("terrain failure");
    });

    // 不应崩溃，应返回默认地面高度
    float h = physics.getTerrainHeight(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(h, -100.0f);  // 默认地面高度
}
