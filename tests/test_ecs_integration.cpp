/**
 * @file test_ecs_integration.cpp
 * @brief ECS 系统集成测试 — MovementSystem + PhysicsSystem 组合验证
 *
 * 测试 MovementSystem 和 PhysicsSystem 在单个帧流水线中的配合：
 *   1. MovementSystem::update() — 输入驱动的位置更新 + 碰撞检测
 *   2. PhysicsSystem::update() — 重力/着地/跳跃
 *
 * 验证目标：
 *   - 组合流水线不崩溃
 *   - 输入驱动移动 + 物理重力协同工作
 *   - 碰撞箱阻挡 + 碰撞箱作为地面
 *   - 跳跃时同时移动
 *   - 异步线程安全（std::async 中调用系统更新）
 */
#include <gtest/gtest.h>
#include <glm/gtc/epsilon.hpp>
#include <future>
#include <ecs/systems.hpp>
#include <ecs/components.hpp>

using namespace owengine::ecs;

// 浮点 vector 近似比较
static bool vec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

// ==================== 辅助函数：运行一个完整帧 ====================

static void runFrame(World& world, MovementSystem& movement, PhysicsSystem& physics,
                     float deltaTime = 1.0f / 60.0f) {
    movement.update(deltaTime);
    physics.update(deltaTime);
}

// ==================== 组合流水线基础 ====================

TEST(ECSIntegrationTest, MovementAndPhysicsCycleNoCrash) {
    World world;
    auto player = world.createPlayer();
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    // 设置输入：向前移动 + 跳跃
    auto& input = world.registry().get<InputStateComponent>(player);
    input.setMoveForward(true);
    input.setJump(true);

    // 运行多个帧，不应崩溃
    for (int i = 0; i < 10; ++i) {
        runFrame(world, movement, physics);
    }
    SUCCEED();
}

TEST(ECSIntegrationTest, WalkForwardChangesPosition) {
    World world;
    auto player = world.createPlayer();
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    auto& input = world.registry().get<InputStateComponent>(player);
    input.setMoveForward(true);

    // 设置移动控制器朝向 -Z（默认朝向）
    auto& controller = world.registry().get<MovementControllerComponent>(player);
    controller.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);

    // 运行 30 帧（约 0.5 秒）
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        runFrame(world, movement, physics, dt);
    }

    auto& transform = world.registry().get<TransformComponent>(player);
    // 应沿 -Z 方向移动，Z 值减小
    EXPECT_LT(transform.position.z, 4.5f);
    // Y 应保持在地面附近（PhysicsSystem 默认地面高度 -100，会一直下落直到着地...）
    // 实际上没有地面，所以 Y 会持续下降
}

// ==================== 碰撞箱地面 ====================

TEST(ECSIntegrationTest, PlayerStandsOnCollisionBox) {
    World world;
    auto player = world.createPlayer();
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    // 添加碰撞箱作为地面（位置 0, -0.5, 0，大小 10x1x10 → 顶面在 y=0）
    // 玩家初始位置 y=0.9，脚底在 0.9 - 1.8/2 = 0.0，刚好在箱顶 surface 上
    physics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(10.0f, 1.0f, 10.0f));

    float dt = 1.0f / 60.0f;
    // 运行 60 帧（1 秒），玩家应稳定站在箱顶
    for (int i = 0; i < 60; ++i) {
        runFrame(world, movement, physics, dt);
    }

    auto& transform = world.registry().get<TransformComponent>(player);
    // 脚底应在 y=0（箱顶），玩家中心 y = 0 + 1.8/2 = 0.9
    EXPECT_NEAR(transform.position.y, 0.9f, 0.05f);
    auto& physicsComp = world.registry().get<PhysicsComponent>(player);
    EXPECT_TRUE(physicsComp.isGrounded());
}

TEST(ECSIntegrationTest, WalkForwardOnCollisionBox) {
    World world;
    auto player = world.createPlayer();
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    // 大地面碰撞箱
    physics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));

    auto& input = world.registry().get<InputStateComponent>(player);
    input.setMoveForward(true);

    auto& controller = world.registry().get<MovementControllerComponent>(player);
    controller.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        runFrame(world, movement, physics, dt);
    }

    auto& transform = world.registry().get<TransformComponent>(player);
    // Z 轴应减小（向前移动），Y 应保持在箱顶
    EXPECT_LT(transform.position.z, 4.8f);
    EXPECT_NEAR(transform.position.y, 0.9f, 0.1f);
}

// ==================== 碰撞箱阻挡 ====================

TEST(ECSIntegrationTest, CollisionBoxBlocksMovement) {
    World world;
    auto player = world.createPlayer();
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    // 地面
    physics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    // 阻挡墙（在 -Z 方向 3 单位处）
    movement.addCollisionBox(glm::vec3(0.0f, 1.0f, 3.0f), glm::vec3(0.5f, 2.0f, 0.5f));

    auto& input = world.registry().get<InputStateComponent>(player);
    input.setMoveForward(true);

    // 朝向 -Z（即向 Z 正方向移动... 实际上是 +Z 方向）
    // 玩家初始在 (0, 0, 5)，向前是 -Z 方向
    // 等一下，game_session.cpp 中控制逻辑到底是哪个方向？
    // 让我看一下 input.setMoveForward 对应的控制逻辑
    // 在 MovementSystem::update 中：
    //   if (input.isMoveForward()) horizontalVelocity += front;
    // 其中 front = controller.moveFront，默认是 (0, 0, -1)
    // 所以向前是 -Z 方向
    // 阻挡墙在 z=3，玩家从 z=5 向前移动，应该撞到 z=3 处的墙
    // 不对，如果玩家朝向是 -Z 且控制器 moveFront = (0,0,-1)，向前移动会让 z 减小
    // 玩家从 z=5 向前 → z=3 应该被墙阻挡

    auto& controller = world.registry().get<MovementControllerComponent>(player);
    controller.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {  // 2 秒应该足够走到墙边
        runFrame(world, movement, physics, dt);
    }

    auto& transform = world.registry().get<TransformComponent>(player);
    // 碰撞箱在 z=3，玩家半径 0.3，应停在 z ≈ 3 + 0.3 = 3.3 处（面向 -Z 方向移动时）
    // 实际上碰撞检测检查玩家 AABB，所以边沿在 3.0 - 0.3 = 2.7 处
    // 不对，玩家向前（-Z）移动，应停在 z ≈ 3 + 0.3 = 3.3 （以玩家中心计）
    // 墙在 z=3，大小 0.5，墙的范围 z: [2.75, 3.25]
    // 玩家中心 z 应停在 3.25 + 0.3 = 3.55 附近
    EXPECT_GT(transform.position.z, 3.0f);  // 被墙挡住，没有穿过
}

// ==================== 跳跃 + 移动 ====================

TEST(ECSIntegrationTest, JumpWhileMovingForward) {
    World world;
    auto player = world.createPlayer();
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    // 地面
    physics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));

    auto& input = world.registry().get<InputStateComponent>(player);
    input.setMoveForward(true);
    input.setJump(true);  // 跳跃

    auto& controller = world.registry().get<MovementControllerComponent>(player);
    controller.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);

    auto& velocity = world.registry().get<VelocityComponent>(player);

    float dt = 1.0f / 60.0f;
    // 第一帧：系统检测到跳跃指令，设置向上速度
    runFrame(world, movement, physics, dt);

    // 跳跃时 Y 速度应 > 0
    EXPECT_GT(velocity.linear.y, 0.0f);

    // 继续运行几帧，玩家应处于空中
    for (int i = 0; i < 10; ++i) {
        runFrame(world, movement, physics, dt);
    }

    auto& transform = world.registry().get<TransformComponent>(player);
    auto& physicsComp = world.registry().get<PhysicsComponent>(player);
    // 玩家应该在跳跃后处于空中（Y > 0.9）
    // 落地前应该在 y > 0.9 的位置
    // 实际上跳跃力 5.5, 重力 15, 大约在 0.366s 后到达最高点
    // 10 帧 = 0.166s，所以还在上升或接近最高点
    bool inAir = transform.position.y > 1.0f || !physicsComp.isGrounded();
    EXPECT_TRUE(inAir);
}

// ==================== 异步线程安全 ====================

TEST(ECSIntegrationTest, AsyncUpdateDoesNotCrash) {
    World world;
    auto player = world.createPlayer();
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    // 地面
    physics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(10.0f, 1.0f, 10.0f));

    auto& input = world.registry().get<InputStateComponent>(player);
    input.setMoveForward(true);

    auto& controller = world.registry().get<MovementControllerComponent>(player);
    controller.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);

    float dt = 1.0f / 60.0f;

    // 模拟 GameSession 中 std::async 的用法
    // 注意：EnTT registry 不是线程安全的，但 MovementSystem 和 PhysicsSystem
    // 在同一线程中顺序执行，所以这里的 async 测试仅验证 async 模式本身不崩溃，
    // 不验证真正的并行安全（真正的并行安全由 GameSession 的更新顺序保证）
    for (int frame = 0; frame < 30; ++frame) {
        auto future = std::async(std::launch::async, [&world, &movement, &physics, dt]() {
            movement.update(dt);
            physics.update(dt);
        });
        // 通过 get() 传播异常
        EXPECT_NO_THROW(future.get());
    }
}

TEST(ECSIntegrationTest, AsyncUpdateProducesSameResult) {
    World world;
    auto player = world.createPlayer();
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    physics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(10.0f, 1.0f, 10.0f));

    auto& input = world.registry().get<InputStateComponent>(player);
    input.setMoveForward(true);

    auto& controller = world.registry().get<MovementControllerComponent>(player);
    controller.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);

    float dt = 1.0f / 60.0f;

    // 异步运行 60 帧
    for (int i = 0; i < 60; ++i) {
        auto future = std::async(std::launch::async, [&world, &movement, &physics, dt]() {
            movement.update(dt);
            physics.update(dt);
        });
        future.get();
    }

    auto& asyncTransform = world.registry().get<TransformComponent>(player);
    float asyncZ = asyncTransform.position.z;

    // 同步运行同样次数，应产生相同结果
    World syncWorld;
    auto syncPlayer = syncWorld.createPlayer();
    MovementSystem syncMovement(syncWorld);
    PhysicsSystem syncPhysics(syncWorld);

    syncPhysics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(10.0f, 1.0f, 10.0f));

    auto& syncInput = syncWorld.registry().get<InputStateComponent>(syncPlayer);
    syncInput.setMoveForward(true);

    auto& syncController = syncWorld.registry().get<MovementControllerComponent>(syncPlayer);
    syncController.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);

    for (int i = 0; i < 60; ++i) {
        syncMovement.update(dt);
        syncPhysics.update(dt);
    }

    auto& syncTransform = syncWorld.registry().get<TransformComponent>(syncPlayer);
    // 异步和同步执行结果应一致
    EXPECT_NEAR(syncTransform.position.z, asyncZ, 0.001f);
    EXPECT_NEAR(syncTransform.position.y, asyncTransform.position.y, 0.001f);
}

// ==================== 多实体场景 ====================

TEST(ECSIntegrationTest, MultiplePlayersAllPhysicsUpdated) {
    World world;
    MovementSystem movement(world);
    PhysicsSystem physics(world);

    // 地面
    physics.addCollisionBox(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));

    // 创建 3 个玩家，分布在不同的 X 位置
    auto p1 = world.createPlayer();
    auto& t1 = world.registry().get<TransformComponent>(p1);
    t1.position.x = -3.0f;

    auto p2 = world.createPlayer();
    auto& t2 = world.registry().get<TransformComponent>(p2);
    t2.position.x = 0.0f;

    auto p3 = world.createPlayer();
    auto& t3 = world.registry().get<TransformComponent>(p3);
    t3.position.x = 3.0f;

    // 所有玩家向前移动
    auto& input1 = world.registry().get<InputStateComponent>(p1);
    input1.setMoveForward(true);
    auto& input2 = world.registry().get<InputStateComponent>(p2);
    input2.setMoveForward(true);
    auto& input3 = world.registry().get<InputStateComponent>(p3);
    input3.setMoveForward(true);

    // 设置移动方向
    auto& c1 = world.registry().get<MovementControllerComponent>(p1);
    c1.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);
    auto& c2 = world.registry().get<MovementControllerComponent>(p2);
    c2.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);
    auto& c3 = world.registry().get<MovementControllerComponent>(p3);
    c3.moveFront = glm::vec3(0.0f, 0.0f, -1.0f);

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        runFrame(world, movement, physics, dt);
    }

    // 所有玩家 Z 应减小（向前移动）
    EXPECT_LT(t1.position.z, 4.5f);
    EXPECT_LT(t2.position.z, 4.5f);
    EXPECT_LT(t3.position.z, 4.5f);
    // Y 都应在箱顶
    EXPECT_NEAR(t1.position.y, 0.9f, 0.1f);
    EXPECT_NEAR(t2.position.y, 0.9f, 0.1f);
    EXPECT_NEAR(t3.position.y, 0.9f, 0.1f);
}
