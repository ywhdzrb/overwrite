#ifndef SCENE_CONFIG_H
#define SCENE_CONFIG_H

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace owengine {

/**
 * @brief 环境光配置结构
 */
struct AmbientConfig {
    glm::vec3 color{0.1f, 0.1f, 0.1f};   // 环境光颜色
    float intensity = 0.3f;               // 环境光强度
};

/**
 * @brief 光源配置结构
 */
struct LightConfig {
    std::string id;                      // 光源ID
    std::string name;                    // 光源名称
    std::string type;                    // 类型: "directional", "point", "spot"
    bool enabled = true;                 // 是否启用
    glm::vec3 position{0.0f};            // 位置（点光源、聚光灯）
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // 方向（方向光、聚光灯）
    glm::vec3 color{1.0f};               // 光源颜色
    float intensity = 1.0f;              // 光照强度
    // 衰减参数（点光源、聚光灯）
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
    // 聚光灯参数
    float innerCutoff = 12.5f;           // 内切角（度）
    float outerCutoff = 17.5f;           // 外切角（度）
    // 阴影参数
    bool shadowEnabled = false;
    float shadowIntensity = 0.3f;
};

/**
 * @brief 模型配置结构
 */
struct ModelConfig {
    std::string id;                      // 模型ID
    std::string file;                    // 文件路径
    bool enabled = true;                 // 是否启用
    glm::vec3 position{0.0f};            // 位置
    glm::vec3 rotation{0.0f};            // 旋转（欧拉角，度）
    glm::vec3 scale{1.0f};               // 缩放
    int subdivisionIterations = 0;       // 细分迭代次数
    bool playAnimation = false;          // 是否播放动画
    int animationIndex = 0;              // 动画索引
    bool playAllAnimations = false;      // 是否播放所有动画
    bool isPlayerModel = false;          // 是否是玩家模型
    bool isPlayerWalkModel = false;      // 是否是玩家行走模型
    std::string description;             // 描述
};

/**
 * @brief 场景配置结构（包含所有配置）
 */
struct SceneConfig {
    AmbientConfig ambient;
    std::vector<LightConfig> lights;
    std::vector<ModelConfig> models;
};

/**
 * @brief 场景配置加载器
 * 
 * 负责从 JSON 文件加载场景配置。
 */
class SceneConfigLoader {
public:
    /**
     * @brief 从文件加载场景配置
     * @param filePath JSON 配置文件路径
     * @return 场景配置
     */
    static SceneConfig load(const std::string& filePath);
    
    /**
     * @brief 保存场景配置到文件
     * @param filePath 目标文件路径
     * @param config 场景配置
     */
    static void save(const std::string& filePath, const SceneConfig& config);
    
    /**
     * @brief 加载模型配置列表
     * @param filePath JSON 配置文件路径
     * @return 模型配置列表
     */
    static std::vector<ModelConfig> loadModels(const std::string& filePath);
};

} // namespace owengine

#endif // SCENE_CONFIG_H
