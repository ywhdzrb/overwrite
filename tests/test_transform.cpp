#include <gtest/gtest.h>
#include <glm/gtc/epsilon.hpp>
#include "ecs/components.hpp"

using namespace owengine::ecs;

// 浮点 vector 近似比较（glm::epsilonEqual 封装）
static bool vec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

TEST(TransformTest, FrontDefault) {
    TransformComponent t;
    // yaw=0, pitch=0 → 朝向 -Z
    EXPECT_TRUE(vec3Near(t.getFront(), glm::vec3(0.0f, 0.0f, -1.0f)));
}

TEST(TransformTest, FrontYaw90) {
    TransformComponent t;
    t.yaw = 90.0f;
    EXPECT_TRUE(vec3Near(t.getFront(), glm::vec3(1.0f, 0.0f, 0.0f)));
}

TEST(TransformTest, FrontYaw180) {
    TransformComponent t;
    t.yaw = 180.0f;
    EXPECT_TRUE(vec3Near(t.getFront(), glm::vec3(0.0f, 0.0f, 1.0f)));
}

TEST(TransformTest, FrontYaw270) {
    TransformComponent t;
    t.yaw = 270.0f;
    EXPECT_TRUE(vec3Near(t.getFront(), glm::vec3(-1.0f, 0.0f, 0.0f)));
}

TEST(TransformTest, FrontPitchUp) {
    TransformComponent t;
    t.pitch = 90.0f;
    EXPECT_TRUE(vec3Near(t.getFront(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

TEST(TransformTest, FrontPitchDown) {
    TransformComponent t;
    t.pitch = -90.0f;
    EXPECT_TRUE(vec3Near(t.getFront(), glm::vec3(0.0f, -1.0f, 0.0f)));
}

TEST(TransformTest, RightOrthogonalToFront) {
    TransformComponent t;
    t.yaw = 45.0f;
    t.pitch = 30.0f;
    EXPECT_NEAR(glm::dot(t.getFront(), t.getRight()), 0.0f, 1e-5f);
}

TEST(TransformTest, UpOrthogonalToFrontAndRight) {
    TransformComponent t;
    t.yaw = 45.0f;
    t.pitch = 30.0f;
    glm::vec3 up = t.getUp();
    EXPECT_NEAR(glm::dot(t.getFront(), up), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::dot(t.getRight(), up), 0.0f, 1e-5f);
}

TEST(TransformTest, AllDirectionsUnitLength) {
    TransformComponent t;
    t.yaw = 45.0f;
    t.pitch = 30.0f;
    EXPECT_NEAR(glm::length(t.getFront()), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(t.getRight()), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(t.getUp()), 1.0f, 1e-5f);
}

TEST(TransformTest, ModelMatrixTranslation) {
    TransformComponent t;
    t.position = glm::vec3(10.0f, 20.0f, 30.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(t.getModelMatrix()[3]), glm::vec3(10.0f, 20.0f, 30.0f)));
}

TEST(TransformTest, ModelMatrixRotation) {
    TransformComponent t;
    t.yaw = 90.0f;
    t.updateRotationFromEuler();
    glm::vec3 xBasis = glm::normalize(glm::vec3(t.getModelMatrix()[0]));
    EXPECT_TRUE(vec3Near(xBasis, glm::vec3(0.0f, 0.0f, 1.0f)));
}

TEST(TransformTest, ModelMatrixScale) {
    TransformComponent t;
    t.scale = glm::vec3(2.0f, 3.0f, 4.0f);
    glm::mat4 m = t.getModelMatrix();
    EXPECT_TRUE(vec3Near(glm::vec3(m[0]), glm::vec3(2.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(vec3Near(glm::vec3(m[1]), glm::vec3(0.0f, 3.0f, 0.0f)));
    EXPECT_TRUE(vec3Near(glm::vec3(m[2]), glm::vec3(0.0f, 0.0f, 4.0f)));
}

TEST(TransformTest, EulerToQuaternionFront) {
    TransformComponent t;
    t.yaw = 90.0f;
    t.updateRotationFromEuler();
    glm::mat4 m = glm::mat4_cast(t.rotation);
    glm::vec3 front = glm::normalize(glm::vec3(m[2])) * -1.0f;
    EXPECT_TRUE(vec3Near(front, glm::vec3(1.0f, 0.0f, 0.0f)));
}

TEST(TransformTest, EulerRotationPreservesOrientation) {
    TransformComponent t;
    t.pitch = 45.0f;
    t.updateRotationFromEuler();
    EXPECT_NEAR(glm::determinant(glm::mat3(t.getModelMatrix())), 1.0f, 1e-5f);
}

// ==================== 扩展测试 ====================

TEST(TransformTest, FrontYaw45Pitch45) {
    TransformComponent t;
    t.yaw = 45.0f;
    t.pitch = 45.0f;
    glm::vec3 front = t.getFront();
    // yaw 45°: front.x = sin(45°)cos(45°) = 0.5, front.z = -cos(45°)cos(45°) = -0.5
    // pitch 45°: front.y = sin(45°) = 0.707
    EXPECT_NEAR(front.x, 0.5f, 1e-5f);
    EXPECT_NEAR(front.y, 0.7071068f, 1e-5f);
    EXPECT_NEAR(front.z, -0.5f, 1e-5f);
    EXPECT_NEAR(glm::length(front), 1.0f, 1e-5f);
}

TEST(TransformTest, RightAtDefaultOrientation) {
    TransformComponent t;
    // yaw=0, pitch=0 → front = -Z → right = cross(-Z, up) = X
    glm::vec3 right = t.getRight();
    EXPECT_TRUE(vec3Near(right, glm::vec3(1.0f, 0.0f, 0.0f)));
}

TEST(TransformTest, UpAtDefaultOrientation) {
    TransformComponent t;
    // yaw=0, pitch=0 → front = -Z, right = X → up = cross(X, -Z) = Y
    glm::vec3 up = t.getUp();
    EXPECT_TRUE(vec3Near(up, glm::vec3(0.0f, 1.0f, 0.0f)));
}

TEST(TransformTest, FrontYaw360SameAs0) {
    TransformComponent t;
    t.yaw = 360.0f;
    EXPECT_TRUE(vec3Near(t.getFront(), glm::vec3(0.0f, 0.0f, -1.0f)));
}

TEST(TransformTest, FrontYawMinus90) {
    TransformComponent t;
    t.yaw = -90.0f;
    EXPECT_TRUE(vec3Near(t.getFront(), glm::vec3(-1.0f, 0.0f, 0.0f)));
}

TEST(TransformTest, ModelMatrixIdentity) {
    TransformComponent t;
    glm::mat4 m = t.getModelMatrix();
    EXPECT_TRUE(vec3Near(glm::vec3(m[0]), glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(vec3Near(glm::vec3(m[1]), glm::vec3(0.0f, 1.0f, 0.0f)));
    EXPECT_TRUE(vec3Near(glm::vec3(m[2]), glm::vec3(0.0f, 0.0f, 1.0f)));
    EXPECT_TRUE(vec3Near(glm::vec3(m[3]), glm::vec3(0.0f, 0.0f, 0.0f)));
}

TEST(TransformTest, ModelMatrixCombined) {
    TransformComponent t;
    t.position = glm::vec3(5.0f, 10.0f, 15.0f);
    t.scale = glm::vec3(2.0f, 2.0f, 2.0f);
    t.yaw = 90.0f;
    t.updateRotationFromEuler();

    glm::mat4 m = t.getModelMatrix();
    // 平移到 (5, 10, 15)
    EXPECT_TRUE(vec3Near(glm::vec3(m[3]), glm::vec3(5.0f, 10.0f, 15.0f)));
    // 缩放应在旋转之前应用（先 scale → rotate → translate）
    // 组合矩阵的 3x3 部分 = R * S
    glm::mat3 rs(m);
    EXPECT_NEAR(glm::determinant(rs), 8.0f, 1e-5f);  // det = 2*2*2 = 8
}

TEST(TransformTest, EulerMultiAxisRotation) {
    TransformComponent t;
    t.yaw = 30.0f;
    t.pitch = 20.0f;
    t.roll = 10.0f;
    t.updateRotationFromEuler();

    glm::mat4 m = t.getModelMatrix();
    glm::mat3 rot(m);
    // 旋转矩阵必须是正交的（列向量单位长度且互相垂直）
    EXPECT_NEAR(glm::length(glm::vec3(rot[0])), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(glm::vec3(rot[1])), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(glm::vec3(rot[2])), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::abs(glm::determinant(rot)), 1.0f, 1e-5f);
}

TEST(TransformTest, PositionModificationDirect) {
    TransformComponent t;
    t.position = glm::vec3(-10.0f, 50.0f, -30.0f);
    EXPECT_TRUE(vec3Near(t.position, glm::vec3(-10.0f, 50.0f, -30.0f)));
    glm::mat4 m = t.getModelMatrix();
    EXPECT_TRUE(vec3Near(glm::vec3(m[3]), glm::vec3(-10.0f, 50.0f, -30.0f)));
}

TEST(TransformTest, ScalePreservesPosition) {
    TransformComponent t;
    t.position = glm::vec3(5.0f, 5.0f, 5.0f);
    t.scale = glm::vec3(10.0f, 10.0f, 10.0f);
    // 改变缩放不应影响平移
    glm::mat4 m = t.getModelMatrix();
    EXPECT_TRUE(vec3Near(glm::vec3(m[3]), glm::vec3(5.0f, 5.0f, 5.0f)));
}
