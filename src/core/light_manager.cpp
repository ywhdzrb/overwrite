// 光源管理器实现
#include "renderer/light_manager.h"
#include "utils/logger.h"
#include <algorithm>

namespace owengine {

LightManager::LightManager()
    : nextLightId(0),
      ambientColor(0.5f, 0.5f, 0.5f),  // 提高环境光颜色
      ambientIntensity(1.0f) {
    
    Logger::info("LightManager initialized");
}

int LightManager::addLight(const Light& light) {
    if (!canAddMoreLights()) {
        Logger::error("LightManager: Maximum number of lights reached (" + 
                     std::to_string(getMaxLights()) + ")");
        return -1;
    }
    
    int lightId = nextLightId++;
    auto lightPtr = std::make_unique<Light>(light);
    
    // 添加到列表
    lights.push_back(std::move(lightPtr));
    
    // 添加到名称映射
    nameToId[light.getName()] = lightId;
    
    Logger::info("Light added: " + light.getName() + " (ID: " + std::to_string(lightId) + ")");
    
    return lightId;
}

bool LightManager::removeLight(int lightId) {
    // 查找光源
    auto it = std::find_if(lights.begin(), lights.end(),
                          [lightId](const std::unique_ptr<Light>& light) {
                              return light && light->getName() == "light_" + std::to_string(lightId);
                          });
    
    if (it == lights.end()) {
        Logger::warning("LightManager: Light not found (ID: " + std::to_string(lightId) + ")");
        return false;
    }
    
    // 从名称映射中移除
    nameToId.erase((*it)->getName());
    
    // 从列表中移除
    lights.erase(it);
    
    Logger::info("Light removed (ID: " + std::to_string(lightId) + ")");
    
    return true;
}

Light* LightManager::getLight(int lightId) {
    // 通过名称查找（假设名称格式为 "light_{id}"）
    std::string name = "light_" + std::to_string(lightId);
    return getLightByName(name);
}

const Light* LightManager::getLight(int lightId) const {
    std::string name = "light_" + std::to_string(lightId);
    return getLightByName(name);
}

Light* LightManager::getLightByName(const std::string& name) {
    auto it = std::find_if(lights.begin(), lights.end(),
                          [&name](const std::unique_ptr<Light>& light) {
                              return light && light->getName() == name;
                          });
    
    return (it != lights.end()) ? it->get() : nullptr;
}

const Light* LightManager::getLightByName(const std::string& name) const {
    auto it = std::find_if(lights.begin(), lights.end(),
                          [&name](const std::unique_ptr<Light>& light) {
                              return light && light->getName() == name;
                          });
    
    return (it != lights.end()) ? it->get() : nullptr;
}

void LightManager::clear() {
    lights.clear();
    nameToId.clear();
    nextLightId = 0;
    
    Logger::info("LightManager cleared");
}

ShaderLightArray LightManager::getShaderLightData() const {
    ShaderLightArray shaderLights;
    
    // 初始化所有光源为禁用状态
    for (auto& shaderLight : shaderLights) {
        shaderLight.enabled = 0;
    }
    
    // 填充光源数据
    int index = 0;
    for (const auto& light : lights) {
        if (index >= getMaxLights()) {
            Logger::warning("LightManager: Exceeded maximum light count");
            break;
        }

        if (light->isEnabled()) {
            auto& shaderLight = shaderLights[index];

            // 转换光源类型
            switch (light->getType()) {
                case LightType::DIRECTIONAL:
                    shaderLight.type = 0;
                    break;
                case LightType::POINT:
                    shaderLight.type = 1;
                    break;
                case LightType::SPOT:
                    shaderLight.type = 2;
                    break;
            }

            shaderLight.enabled = 1;
            shaderLight._pad1[0] = 0.0f;
            shaderLight._pad1[1] = 0.0f;
            shaderLight.position = light->getPosition();
            shaderLight._pad2 = 0.0f;
            shaderLight.direction = light->getDirection();
            shaderLight._pad3 = 0.0f;
            shaderLight.color = light->getColor();
            shaderLight.intensity = light->getIntensity();
            shaderLight.constant = light->getConstant();
            shaderLight.linear = light->getLinear();
            shaderLight.quadratic = light->getQuadratic();
            shaderLight._pad4 = 0.0f;
            shaderLight.innerCutoff = glm::cos(glm::radians(light->getInnerCutoff()));
            shaderLight.outerCutoff = glm::cos(glm::radians(light->getOuterCutoff()));
            shaderLight.shadowIntensity = light->isShadowEnabled() ? light->getShadowIntensity() : 0.0f;
            shaderLight._pad5 = 0.0f;

            index++;
        }
    }
    
    return shaderLights;
}

int LightManager::getEnabledLightCount() const {
    return static_cast<int>(std::count_if(lights.begin(), lights.end(),
                                         [](const std::unique_ptr<Light>& light) {
                                             return light && light->isEnabled();
                                         }));
}

int LightManager::addDirectionalLight(const std::string& name,
                                      const glm::vec3& direction,
                                      const glm::vec3& color,
                                      float intensity) {
    Light light(name, LightType::DIRECTIONAL);
    light.setupAsDirectional(direction, color, intensity);
    return addLight(light);
}

int LightManager::addPointLight(const std::string& name,
                                const glm::vec3& position,
                                const glm::vec3& color,
                                float intensity,
                                float range) {
    Light light(name, LightType::POINT);
    light.setupAsPoint(position, color, intensity, range);
    return addLight(light);
}

int LightManager::addSpotLight(const std::string& name,
                               const glm::vec3& position,
                               const glm::vec3& direction,
                               const glm::vec3& color,
                               float intensity,
                               float innerCutoff,
                               float outerCutoff,
                               float range) {
    Light light(name, LightType::SPOT);
    light.setupAsSpot(position, direction, color, intensity, innerCutoff, outerCutoff, range);
    return addLight(light);
}

bool LightManager::updateLight(int lightId, const Light& light) {
    Light* existingLight = getLight(lightId);
    if (existingLight == nullptr) {
        Logger::warning("LightManager: Light not found for update (ID: " + std::to_string(lightId) + ")");
        return false;
    }
    
    // 更新光源数据
    *existingLight = light;
    
    Logger::info("Light updated (ID: " + std::to_string(lightId) + ")");
    
    return true;
}

bool LightManager::enableLight(int lightId) {
    Light* light = getLight(lightId);
    if (light == nullptr) {
        Logger::warning("LightManager: Light not found (ID: " + std::to_string(lightId) + ")");
        return false;
    }
    
    light->setEnabled(true);
    Logger::info("Light enabled (ID: " + std::to_string(lightId) + ")");
    
    return true;
}

bool LightManager::disableLight(int lightId) {
    Light* light = getLight(lightId);
    if (light == nullptr) {
        Logger::warning("LightManager: Light not found (ID: " + std::to_string(lightId) + ")");
        return false;
    }
    
    light->setEnabled(false);
    Logger::info("Light disabled (ID: " + std::to_string(lightId) + ")");
    
    return true;
}

void LightManager::enableAllLights() {
    for (auto& light : lights) {
        light->setEnabled(true);
    }
    Logger::info("All lights enabled");
}

void LightManager::disableAllLights() {
    for (auto& light : lights) {
        light->setEnabled(false);
    }
    Logger::info("All lights disabled");
}

bool LightManager::canAddMoreLights() const {
    return lights.size() < static_cast<size_t>(getMaxLights());
}

} // namespace owengine