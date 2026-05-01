#include "core/scene_config.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace owengine {

using json = nlohmann::json;

SceneConfig SceneConfigLoader::load(const std::string& filePath) {
    SceneConfig config;
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        Logger::warning("无法打开场景配置文件: " + filePath);
        return config;
    }
    
    try {
        json j;
        file >> j;
        
        // 加载环境光
        if (j.contains("ambient")) {
            auto& a = j["ambient"];
            config.ambient.color = glm::vec3(
                a.value("r", 0.1f),
                a.value("g", 0.1f),
                a.value("b", 0.1f)
            );
            config.ambient.intensity = a.value("intensity", 0.3f);
        }
        
        // 加载光源
        if (j.contains("lights")) {
            for (const auto& l : j["lights"]) {
                LightConfig lc;
                lc.id = l.value("id", "");
                lc.name = l.value("name", "");
                lc.type = l.value("type", "point");
                lc.enabled = l.value("enabled", true);
                
                if (l.contains("position")) {
                    lc.position = glm::vec3(
                        l["position"][0].get<float>(),
                        l["position"][1].get<float>(),
                        l["position"][2].get<float>()
                    );
                }
                
                if (l.contains("direction")) {
                    lc.direction = glm::vec3(
                        l["direction"][0].get<float>(),
                        l["direction"][1].get<float>(),
                        l["direction"][2].get<float>()
                    );
                }
                
                if (l.contains("color")) {
                    lc.color = glm::vec3(
                        l["color"][0].get<float>(),
                        l["color"][1].get<float>(),
                        l["color"][2].get<float>()
                    );
                }
                
                lc.intensity = l.value("intensity", 1.0f);
                lc.constant = l.value("constant", 1.0f);
                lc.linear = l.value("linear", 0.09f);
                lc.quadratic = l.value("quadratic", 0.032f);
                lc.innerCutoff = l.value("innerCutoff", 12.5f);
                lc.outerCutoff = l.value("outerCutoff", 17.5f);
                lc.shadowEnabled = l.value("shadowEnabled", false);
                lc.shadowIntensity = l.value("shadowIntensity", 0.3f);
                
                config.lights.push_back(lc);
            }
        }
        
        // 加载模型
        if (j.contains("models")) {
            for (const auto& m : j["models"]) {
                ModelConfig mc;
                mc.id = m.value("id", "");
                mc.file = m.value("file", "");
                mc.enabled = m.value("enabled", true);
                
                if (m.contains("position")) {
                    mc.position = glm::vec3(
                        m["position"][0].get<float>(),
                        m["position"][1].get<float>(),
                        m["position"][2].get<float>()
                    );
                }
                
                if (m.contains("rotation")) {
                    mc.rotation = glm::vec3(
                        m["rotation"][0].get<float>(),
                        m["rotation"][1].get<float>(),
                        m["rotation"][2].get<float>()
                    );
                }
                
                if (m.contains("scale")) {
                    mc.scale = glm::vec3(
                        m["scale"][0].get<float>(),
                        m["scale"][1].get<float>(),
                        m["scale"][2].get<float>()
                    );
                }
                
                mc.subdivisionIterations = m.value("subdivisionIterations", 0);
                mc.playAnimation = m.value("playAnimation", false);
                mc.animationIndex = m.value("animationIndex", 0);
                mc.playAllAnimations = m.value("playAllAnimations", false);
                mc.description = m.value("description", "");
                
                config.models.push_back(mc);
            }
        }
        
        Logger::info("场景配置加载成功: " + filePath);
        
    } catch (const json::exception& e) {
        Logger::error(std::string("JSON 解析错误: ") + e.what());
    }
    
    return config;
}

void SceneConfigLoader::save(const std::string& filePath, const SceneConfig& config) {
    json j;
    
    // 环境光
    j["ambient"]["r"] = config.ambient.color.r;
    j["ambient"]["g"] = config.ambient.color.g;
    j["ambient"]["b"] = config.ambient.color.b;
    j["ambient"]["intensity"] = config.ambient.intensity;
    
    // 光源
    for (const auto& l : config.lights) {
        json lj;
        lj["id"] = l.id;
        lj["name"] = l.name;
        lj["type"] = l.type;
        lj["enabled"] = l.enabled;
        lj["position"] = {l.position.x, l.position.y, l.position.z};
        lj["direction"] = {l.direction.x, l.direction.y, l.direction.z};
        lj["color"] = {l.color.x, l.color.y, l.color.z};
        lj["intensity"] = l.intensity;
        lj["constant"] = l.constant;
        lj["linear"] = l.linear;
        lj["quadratic"] = l.quadratic;
        lj["innerCutoff"] = l.innerCutoff;
        lj["outerCutoff"] = l.outerCutoff;
        lj["shadowEnabled"] = l.shadowEnabled;
        lj["shadowIntensity"] = l.shadowIntensity;
        j["lights"].push_back(lj);
    }
    
    // 模型
    for (const auto& m : config.models) {
        json mj;
        mj["id"] = m.id;
        mj["file"] = m.file;
        mj["enabled"] = m.enabled;
        mj["position"] = {m.position.x, m.position.y, m.position.z};
        mj["rotation"] = {m.rotation.x, m.rotation.y, m.rotation.z};
        mj["scale"] = {m.scale.x, m.scale.y, m.scale.z};
        mj["subdivisionIterations"] = m.subdivisionIterations;
        mj["playAnimation"] = m.playAnimation;
        mj["animationIndex"] = m.animationIndex;
        mj["playAllAnimations"] = m.playAllAnimations;
        mj["description"] = m.description;
        j["models"].push_back(mj);
    }
    
    std::ofstream file(filePath);
    if (file.is_open()) {
        file << j.dump(4);
        Logger::info("场景配置保存成功: " + filePath);
    } else {
        Logger::error("无法保存场景配置: " + filePath);
    }
}

std::vector<ModelConfig> SceneConfigLoader::loadModels(const std::string& filePath) {
    return load(filePath).models;
}

} // namespace owengine
