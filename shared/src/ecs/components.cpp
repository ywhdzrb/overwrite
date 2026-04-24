// 共享 ECS 组件实现
#include "ecs/components.h"
#include <glm/gtc/matrix_transform.hpp>

namespace owengine {
namespace ecs {

glm::vec3 TransformComponent::getFront() const {
    // 基于欧拉角计算前向量 (yaw=0 时朝向 -Z 轴，即屏幕内)
    glm::vec3 front;
    front.x = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = -cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    return glm::normalize(front);
}

glm::vec3 TransformComponent::getRight() const {
    return glm::normalize(glm::cross(getFront(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 TransformComponent::getUp() const {
    return glm::normalize(glm::cross(getRight(), getFront()));
}

void TransformComponent::updateRotationFromEuler() {
    rotation = glm::quat(glm::vec3(glm::radians(pitch), glm::radians(yaw), glm::radians(roll)));
}

glm::mat4 TransformComponent::getModelMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = model * glm::mat4_cast(rotation);
    model = glm::scale(model, scale);
    return model;
}

} // namespace ecs
} // namespace owengine
