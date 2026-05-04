#include <gtest/gtest.h>
#include <glm/gtc/epsilon.hpp>
#include "ecs/components.h"

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
