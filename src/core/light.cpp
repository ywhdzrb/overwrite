// 光源类实现
#include "light.h"
#include "logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace vgame {

Light::Light(const std::string& name, LightType type)
    : name(name),
      type(type),
      position(0.0f, 0.0f, 0.0f),
      direction(0.0f, -1.0f, 0.0f),
      color(1.0f, 1.0f, 1.0f),
      intensity(1.0f),
      enabled(true),
      constant(1.0f),
      linear(0.09f),
      quadratic(0.032f),
      innerCutoff(12.5f),
      outerCutoff(17.5f),
      shadowEnabled(false),
      shadowIntensity(0.8f) {
    
    Logger::info("Light created: " + name + " (type: " + 
                std::to_string(static_cast<int>(type)) + ")");
}

Light::Light()
    : Light("default_light", LightType::POINT) {
}

float Light::calculateAttenuation(float distance) const {
    // 如果是方向光，没有衰减
    if (type == LightType::DIRECTIONAL) {
        return 1.0f;
    }
    
    // 计算衰减：1.0 / (constant + linear * distance + quadratic * distance^2)
    float attenuation = 1.0f / (constant + linear * distance + quadratic * distance * distance);
    
    // 限制衰减范围
    return glm::clamp(attenuation, 0.0f, 1.0f);
}

bool Light::isInSpotlightRange(const glm::vec3& point) const {
    // 如果不是聚光灯，总是返回 true
    if (type != LightType::SPOT) {
        return true;
    }
    
    // 计算从光源到点的方向
    glm::vec3 lightToPoint = glm::normalize(point - position);
    
    // 计算与光照方向的夹角
    float theta = glm::dot(lightToPoint, -direction);
    float thetaCos = glm::cos(glm::radians(theta));
    float outerCutoffCos = glm::cos(glm::radians(outerCutoff));
    
    // 检查是否在聚光灯范围内
    return thetaCos > outerCutoffCos;
}

void Light::setupAsDirectional(const glm::vec3& direction,
                               const glm::vec3& color,
                               float intensity) {
    type = LightType::DIRECTIONAL;
    this->direction = glm::normalize(direction);
    this->color = color;
    this->intensity = intensity;
    this->enabled = true;
}

void Light::setupAsPoint(const glm::vec3& position,
                         const glm::vec3& color,
                         float intensity,
                         float range) {
    type = LightType::POINT;
    this->position = position;
    this->color = color;
    this->intensity = intensity;
    this->enabled = true;
    
    // 设置标准衰减参数
    setStandardAttenuation(range);
}

void Light::setupAsSpot(const glm::vec3& position,
                       const glm::vec3& direction,
                       const glm::vec3& color,
                       float intensity,
                       float innerCutoff,
                       float outerCutoff,
                       float range) {
    type = LightType::SPOT;
    this->position = position;
    this->direction = glm::normalize(direction);
    this->color = color;
    this->intensity = intensity;
    this->innerCutoff = innerCutoff;
    this->outerCutoff = outerCutoff;
    this->enabled = true;
    
    // 设置标准衰减参数
    setStandardAttenuation(range);
}

void Light::setStandardAttenuation(float range) {
    // 根据光照范围设置标准衰减参数
    // 这些值来自经验和实验，提供了良好的衰减效果
    
    // 距离 7
    if (range <= 7.0f) {
        constant = 1.0f;
        linear = 0.7f;
        quadratic = 1.8f;
    }
    // 距离 13
    else if (range <= 13.0f) {
        constant = 1.0f;
        linear = 0.35f;
        quadratic = 0.44f;
    }
    // 距离 20
    else if (range <= 20.0f) {
        constant = 1.0f;
        linear = 0.22f;
        quadratic = 0.20f;
    }
    // 距离 32
    else if (range <= 32.0f) {
        constant = 1.0f;
        linear = 0.14f;
        quadratic = 0.07f;
    }
    // 距离 50
    else if (range <= 50.0f) {
        constant = 1.0f;
        linear = 0.09f;
        quadratic = 0.032f;
    }
    // 距离 65
    else if (range <= 65.0f) {
        constant = 1.0f;
        linear = 0.07f;
        quadratic = 0.017f;
    }
    // 距离 100
    else if (range <= 100.0f) {
        constant = 1.0f;
        linear = 0.045f;
        quadratic = 0.0075f;
    }
    // 距离 160
    else if (range <= 160.0f) {
        constant = 1.0f;
        linear = 0.027f;
        quadratic = 0.0028f;
    }
    // 距离 200
    else if (range <= 200.0f) {
        constant = 1.0f;
        linear = 0.022f;
        quadratic = 0.0019f;
    }
    // 距离 325
    else if (range <= 325.0f) {
        constant = 1.0f;
        linear = 0.014f;
        quadratic = 0.0007f;
    }
    // 距离 600
    else if (range <= 600.0f) {
        constant = 1.0f;
        linear = 0.007f;
        quadratic = 0.0002f;
    }
    // 超远距离
    else {
        constant = 1.0f;
        linear = 0.0014f;
        quadratic = 0.000007f;
    }
}

} // namespace vgame