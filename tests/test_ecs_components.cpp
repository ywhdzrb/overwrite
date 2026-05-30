/**
 * @file test_ecs_components.cpp
 * @brief ECS 组件数据默认值与基础行为测试
 *
 * 验证 shared/include/ecs/components.hpp 中所有组件的
 * 默认构造、基本状态和接口，不依赖 EnTT 注册表。
 * 只需要链接 OverWriteShared（含 glm 等依赖）。
 */
#include <gtest/gtest.h>
#include <glm/gtc/epsilon.hpp>
#include "ecs/components.hpp"

using namespace owengine::ecs;

// ==================== 常量 ====================

TEST(ECSConstantsTest, MaxDeltaTime) {
    EXPECT_FLOAT_EQ(MAX_DELTA_TIME, 0.1f);
}

TEST(ECSConstantsTest, PlayerConstants) {
    EXPECT_FLOAT_EQ(PLAYER_RADIUS, 0.3f);
    EXPECT_FLOAT_EQ(PLAYER_HEIGHT, 1.8f);
    EXPECT_FLOAT_EQ(PLAYER_MODEL_SCALE, 0.3f);
}

// ==================== VelocityComponent ====================

TEST(VelocityComponentTest, DefaultLinearZero) {
    VelocityComponent vel;
    EXPECT_EQ(vel.linear.x, 0.0f);
    EXPECT_EQ(vel.linear.y, 0.0f);
    EXPECT_EQ(vel.linear.z, 0.0f);
}

TEST(VelocityComponentTest, DefaultAngularZero) {
    VelocityComponent vel;
    EXPECT_EQ(vel.angular.x, 0.0f);
    EXPECT_EQ(vel.angular.y, 0.0f);
    EXPECT_EQ(vel.angular.z, 0.0f);
}

TEST(VelocityComponentTest, CustomLinearVelocity) {
    VelocityComponent vel{{5.0f, 0.0f, 3.0f}, {}};
    EXPECT_EQ(vel.linear.x, 5.0f);
    EXPECT_EQ(vel.linear.z, 3.0f);
}

// ==================== PhysicsComponent ====================

TEST(PhysicsComponentTest, DefaultGravity) {
    PhysicsComponent phys;
    EXPECT_FLOAT_EQ(phys.gravity, 15.0f);
}

TEST(PhysicsComponentTest, DefaultGroundHeight) {
    PhysicsComponent phys;
    EXPECT_FLOAT_EQ(phys.groundHeight, -1.5f);
}

TEST(PhysicsComponentTest, DefaultGroundNormal) {
    PhysicsComponent phys;
    EXPECT_FLOAT_EQ(phys.groundNormal.x, 0.0f);
    EXPECT_FLOAT_EQ(phys.groundNormal.y, 1.0f);
    EXPECT_FLOAT_EQ(phys.groundNormal.z, 0.0f);
}

TEST(PhysicsComponentTest, DefaultJumpForce) {
    PhysicsComponent phys;
    EXPECT_FLOAT_EQ(phys.jumpForce, 5.5f);
}

TEST(PhysicsComponentTest, DefaultGrounded) {
    PhysicsComponent phys;
    EXPECT_TRUE(phys.isGrounded);
    EXPECT_FALSE(phys.isJumping);
    EXPECT_TRUE(phys.useGravity);
}

TEST(PhysicsComponentTest, DefaultColliderParams) {
    PhysicsComponent phys;
    EXPECT_FLOAT_EQ(phys.colliderHeight, 1.8f);
    EXPECT_FLOAT_EQ(phys.colliderRadius, 0.3f);
}

TEST(PhysicsComponentTest, DefaultCacheState) {
    PhysicsComponent phys;
    EXPECT_FLOAT_EQ(phys.cachedTerrainHeight, -1.5f);
    EXPECT_FALSE(phys.terrainCacheValid);
}

TEST(PhysicsComponentTest, StateToggle) {
    PhysicsComponent phys;
    phys.isGrounded = false;
    phys.isJumping = true;
    phys.useGravity = false;

    EXPECT_FALSE(phys.isGrounded);
    EXPECT_TRUE(phys.isJumping);
    EXPECT_FALSE(phys.useGravity);
}

// ==================== InputStateComponent ====================

TEST(InputStateComponentTest, DefaultAllFalse) {
    InputStateComponent input;
    EXPECT_FALSE(input.moveForward);
    EXPECT_FALSE(input.moveBackward);
    EXPECT_FALSE(input.moveLeft);
    EXPECT_FALSE(input.moveRight);
    EXPECT_FALSE(input.jump);
    EXPECT_FALSE(input.sprint);
    EXPECT_FALSE(input.freeCameraToggle);
    EXPECT_FALSE(input.spaceHeld);
    EXPECT_FALSE(input.shiftHeld);
}

TEST(InputStateComponentTest, DefaultMouseDeltasZero) {
    InputStateComponent input;
    EXPECT_FLOAT_EQ(input.mouseDeltaX, 0.0f);
    EXPECT_FLOAT_EQ(input.mouseDeltaY, 0.0f);
}

TEST(InputStateComponentTest, ResetClearsAll) {
    InputStateComponent input;
    input.moveForward = true;
    input.jump = true;
    input.mouseDeltaX = 10.0f;

    input.reset();

    EXPECT_FALSE(input.moveForward);
    EXPECT_FALSE(input.jump);
    EXPECT_FLOAT_EQ(input.mouseDeltaX, 0.0f);
    EXPECT_FLOAT_EQ(input.mouseDeltaY, 0.0f);
}

// ==================== PlayerTag ====================

TEST(PlayerTagTest, DefaultValues) {
    PlayerTag tag;
    EXPECT_EQ(tag.playerId, 0u);
    EXPECT_EQ(tag.connectionId, 0u);
}

TEST(PlayerTagTest, CustomValues) {
    PlayerTag tag{42, 100};
    EXPECT_EQ(tag.playerId, 42u);
    EXPECT_EQ(tag.connectionId, 100u);
}

// ==================== NameComponent ====================

TEST(NameComponentTest, DefaultEmpty) {
    NameComponent name;
    EXPECT_TRUE(name.name.empty());
}

TEST(NameComponentTest, CustomName) {
    NameComponent name{"测试玩家"};
    EXPECT_EQ(name.name, "测试玩家");
}

// ==================== NetworkSyncComponent ====================

TEST(NetworkSyncComponentTest, DefaultValues) {
    NetworkSyncComponent net;
    EXPECT_EQ(net.networkId, 0u);
    EXPECT_EQ(net.lastSyncFrame, 0u);
    EXPECT_TRUE(net.needsSync);
    EXPECT_FALSE(net.isOwned);
}

TEST(NetworkSyncComponentTest, AfterSync) {
    NetworkSyncComponent net;
    net.networkId = 100;
    net.lastSyncFrame = 42;
    net.needsSync = false;
    net.isOwned = true;

    EXPECT_EQ(net.networkId, 100u);
    EXPECT_EQ(net.lastSyncFrame, 42u);
    EXPECT_FALSE(net.needsSync);
    EXPECT_TRUE(net.isOwned);
}

// ==================== EntityTypeComponent ====================

TEST(EntityTypeComponentTest, DefaultUnknown) {
    EntityTypeComponent etc;
    EXPECT_EQ(etc.type, EntityType::Unknown);
}

TEST(EntityTypeComponentTest, SetPlayerType) {
    EntityTypeComponent etc;
    etc.type = EntityType::Player;
    EXPECT_EQ(etc.type, EntityType::Player);
}

// ==================== EntityType 枚举值 ====================

TEST(EntityTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<int>(EntityType::Unknown), 0);
    EXPECT_EQ(static_cast<int>(EntityType::Player), 1);
    EXPECT_EQ(static_cast<int>(EntityType::NPC), 2);
    EXPECT_EQ(static_cast<int>(EntityType::Building), 3);
    EXPECT_EQ(static_cast<int>(EntityType::Item), 4);
    EXPECT_EQ(static_cast<int>(EntityType::Projectile), 5);
}

// ==================== MovementControllerComponent ====================

TEST(MovementControllerComponentTest, DefaultValues) {
    MovementControllerComponent mcc;
    EXPECT_FLOAT_EQ(mcc.movementSpeed, 5.0f);
    EXPECT_FLOAT_EQ(mcc.sprintMultiplier, 2.0f);
    EXPECT_FLOAT_EQ(mcc.mouseSensitivity, 0.1f);
}

TEST(MovementControllerComponentTest, DefaultDirections) {
    MovementControllerComponent mcc;
    EXPECT_FLOAT_EQ(mcc.moveFront.z, -1.0f);  // 默认朝向 -Z
    EXPECT_FLOAT_EQ(mcc.moveRight.x, 1.0f);   // 默认右向 +X
}

TEST(MovementControllerComponentTest, AirControl) {
    MovementControllerComponent mcc;
    EXPECT_FLOAT_EQ(mcc.airControlFactor, 0.2f);
}
