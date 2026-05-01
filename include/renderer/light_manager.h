#pragma once

#include "renderer/light.h"
#include <vector>
#include <memory>
#include <map>
#include <glm/glm.hpp>

namespace owengine {

/**
 * @brief 光源管理器类
 * 
 * 该类负责管理场景中的所有光源，包括添加、删除、更新和查询光源。
 * 支持最大 16 个光源（与着色器限制匹配）。
 */
class LightManager {
public:
    /**
     * @brief 构造函数
     */
    LightManager();

    /**
     * @brief 析构函数
     */
    ~LightManager() = default;

    /**
     * @brief 禁止拷贝构造
     */
    LightManager(const LightManager&) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    LightManager& operator=(const LightManager&) = delete;

    // ==================== 光源管理 ====================

    /**
     * @brief 添加光源
     * @param light 光源对象
     * @return 光源 ID，如果添加失败则返回 -1
     */
    int addLight(const Light& light);

    /**
     * @brief 移除光源
     * @param lightId 光源 ID
     * @return 是否成功移除
     */
    bool removeLight(int lightId);

    /**
     * @brief 获取光源
     * @param lightId 光源 ID
     * @return 光源指针，如果不存在则返回 nullptr
     */
    Light* getLight(int lightId);

    /**
     * @brief 获取光源（const 版本）
     * @param lightId 光源 ID
     * @return 光源指针，如果不存在则返回 nullptr
     */
    const Light* getLight(int lightId) const;

    /**
     * @brief 通过名称获取光源
     * @param name 光源名称
     * @return 光源指针，如果不存在则返回 nullptr
     */
    Light* getLightByName(const std::string& name);

    /**
     * @brief 通过名称获取光源（const 版本）
     * @param name 光源名称
     * @return 光源指针，如果不存在则返回 nullptr
     */
    const Light* getLightByName(const std::string& name) const;

    /**
     * @brief 获取所有光源
     * @return 光源列表
     */
    const std::vector<std::unique_ptr<Light>>& getLights() const { return lights; }

    /**
     * @brief 清空所有光源
     */
    void clear();

    /**
     * @brief 获取光源数量
     * @return 光源数量
     */
    size_t getLightCount() const { return lights.size(); }

    // ==================== 环境光 ====================

    /**
     * @brief 获取环境光颜色
     */
    const glm::vec3& getAmbientColor() const { return ambientColor; }

    /**
     * @brief 设置环境光颜色
     */
    void setAmbientColor(const glm::vec3& color) { ambientColor = color; }

    /**
     * @brief 获取环境光强度
     */
    float getAmbientIntensity() const { return ambientIntensity; }

    /**
     * @brief 设置环境光强度
     */
    void setAmbientIntensity(float intensity) { ambientIntensity = intensity; }

    /**
     * @brief 获取环境光（颜色 * 强度）
     */
    glm::vec3 getAmbient() const { return ambientColor * ambientIntensity; }

    // ==================== 着色器数据 ====================

    /**
     * @brief 获取着色器光源数据数组
     * @return 光源数据数组
     */
    ShaderLightArray getShaderLightData() const;

    /**
     * @brief 获取启用的光源数量
     * @return 启用的光源数量
     */
    int getEnabledLightCount() const;

    /**
     * @brief 获取最大光源数量
     * @return 最大光源数量
     */
    static constexpr int getMaxLights() { return 16; }

    // ==================== 实用方法 ====================

    /**
     * @brief 添加方向光
     * @param name 光源名称
     * @param direction 光照方向
     * @param color 光照颜色
     * @param intensity 光照强度
     * @return 光源 ID，如果添加失败则返回 -1
     */
    int addDirectionalLight(const std::string& name,
                           const glm::vec3& direction,
                           const glm::vec3& color = glm::vec3(1.0f),
                           float intensity = 1.0f);

    /**
     * @brief 添加点光源
     * @param name 光源名称
     * @param position 光源位置
     * @param color 光照颜色
     * @param intensity 光照强度
     * @param range 光照范围
     * @return 光源 ID，如果添加失败则返回 -1
     */
    int addPointLight(const std::string& name,
                     const glm::vec3& position,
                     const glm::vec3& color = glm::vec3(1.0f),
                     float intensity = 1.0f,
                     float range = 10.0f);

    /**
     * @brief 添加聚光灯
     * @param name 光源名称
     * @param position 光源位置
     * @param direction 光照方向
     * @param color 光照颜色
     * @param intensity 光照强度
     * @param innerCutoff 内切角（度数）
     * @param outerCutoff 外切角（度数）
     * @param range 光照范围
     * @return 光源 ID，如果添加失败则返回 -1
     */
    int addSpotLight(const std::string& name,
                    const glm::vec3& position,
                    const glm::vec3& direction,
                    const glm::vec3& color = glm::vec3(1.0f),
                    float intensity = 1.0f,
                    float innerCutoff = 12.5f,
                    float outerCutoff = 17.5f,
                    float range = 10.0f);

    /**
     * @brief 更新光源
     * @param lightId 光源 ID
     * @param light 新的光源数据
     * @return 是否成功更新
     */
    bool updateLight(int lightId, const Light& light);

    /**
     * @brief 启用光源
     * @param lightId 光源 ID
     * @return 是否成功启用
     */
    bool enableLight(int lightId);

    /**
     * @brief 禁用光源
     * @param lightId 光源 ID
     * @return 是否成功禁用
     */
    bool disableLight(int lightId);

    /**
     * @brief 启用所有光源
     */
    void enableAllLights();

    /**
     * @brief 禁用所有光源
     */
    void disableAllLights();

    /**
     * @brief 获取下一个光源 ID
     * @return 下一个光源 ID
     */
    int getNextLightId() const { return nextLightId; }

private:
    /**
     * @brief 检查是否可以添加更多光源
     * @return 是否可以添加
     */
    bool canAddMoreLights() const;

private:
    std::vector<std::unique_ptr<Light>> lights;  // 光源列表
    std::map<std::string, int> nameToId;         // 名称到 ID 的映射
    int nextLightId;                              // 下一个光源 ID

    // 环境光参数
    glm::vec3 ambientColor;        // 环境光颜色
    float ambientIntensity;        // 环境光强度
};

} // namespace owengine
