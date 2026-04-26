#ifndef LIGHT_H
#define LIGHT_H

#include <glm/glm.hpp>
#include <string>
#include <array>

namespace owengine {

/**
 * @brief 光源类型枚举
 */
enum class LightType {
    DIRECTIONAL,  // 方向光（如太阳光）
    POINT,        // 点光源（如灯泡）
    SPOT          // 聚光灯（如手电筒）
};

/**
 * @brief 光源类
 * 
 * 该类表示场景中的一个光源，支持方向光、点光源和聚光灯。
 */
class Light {
public:
    /**
     * @brief 构造函数
     * @param name 光源名称
     * @param type 光源类型
     */
    Light(const std::string& name, LightType type);
    
    /**
     * @brief 默认构造函数（创建白色点光源）
     */
    Light();
    
    /**
     * @brief 析构函数
     */
    ~Light() = default;

    /**
     * @brief 禁止拷贝构造
     */
    Light(const Light&) = default;

    /**
     * @brief 禁止拷贝赋值
     */
    Light& operator=(const Light&) = default;

    // ==================== 获取器和设置器 ====================

    /**
     * @brief 获取光源名称
     */
    const std::string& getName() const { return name; }

    /**
     * @brief 设置光源名称
     */
    void setName(const std::string& n) { name = n; }

    /**
     * @brief 获取光源类型
     */
    LightType getType() const { return type; }

    /**
     * @brief 设置光源类型
     */
    void setType(LightType t) { type = t; }

    /**
     * @brief 获取光源位置
     */
    const glm::vec3& getPosition() const { return position; }

    /**
     * @brief 设置光源位置
     */
    void setPosition(const glm::vec3& pos) { position = pos; }

    /**
     * @brief 获取光源方向
     */
    const glm::vec3& getDirection() const { return direction; }

    /**
     * @brief 设置光源方向
     */
    void setDirection(const glm::vec3& dir) { 
        direction = glm::normalize(dir); 
    }

    /**
     * @brief 获取光源颜色
     */
    const glm::vec3& getColor() const { return color; }

    /**
     * @brief 设置光源颜色
     */
    void setColor(const glm::vec3& c) { color = c; }

    /**
     * @brief 获取光源强度
     */
    float getIntensity() const { return intensity; }

    /**
     * @brief 设置光源强度
     */
    void setIntensity(float i) { intensity = i; }

    /**
     * @brief 获取是否启用
     */
    bool isEnabled() const { return enabled; }

    /**
     * @brief 设置是否启用
     */
    void setEnabled(bool e) { enabled = e; }

    // ==================== 点光源参数 ====================

    /**
     * @brief 获取点光源衰减常数项
     */
    float getConstant() const { return constant; }

    /**
     * @brief 设置点光源衰减常数项
     */
    void setConstant(float c) { constant = c; }

    /**
     * @brief 获取点光源衰减线性项
     */
    float getLinear() const { return linear; }

    /**
     * @brief 设置点光源衰减线性项
     */
    void setLinear(float l) { linear = l; }

    /**
     * @brief 获取点光源衰减二次项
     */
    float getQuadratic() const { return quadratic; }

    /**
     * @brief 设置点光源衰减二次项
     */
    void setQuadratic(float q) { quadratic = q; }

    // ==================== 聚光灯参数 ====================

    /**
     * @brief 获取聚光灯内切角（度数）
     */
    float getInnerCutoff() const { return innerCutoff; }

    /**
     * @brief 设置聚光灯内切角（度数）
     */
    void setInnerCutoff(float angle) { innerCutoff = angle; }

    /**
     * @brief 获取聚光灯外切角（度数）
     */
    float getOuterCutoff() const { return outerCutoff; }

    /**
     * @brief 设置聚光灯外切角（度数）
     */
    void setOuterCutoff(float angle) { outerCutoff = angle; }

    // ==================== 阴影参数 ====================

    /**
     * @brief 获取是否启用阴影
     */
    bool isShadowEnabled() const { return shadowEnabled; }

    /**
     * @brief 设置是否启用阴影
     */
    void setShadowEnabled(bool e) { shadowEnabled = e; }

    /**
     * @brief 获取阴影强度
     */
    float getShadowIntensity() const { return shadowIntensity; }

    /**
     * @brief 设置阴影强度
     */
    void setShadowIntensity(float intensity) { shadowIntensity = intensity; }

    // ==================== 实用方法 ====================

    /**
     * @brief 计算光照强度（考虑衰减）
     * @param distance 光源到表面的距离
     * @return 光照强度（0-1）
     */
    float calculateAttenuation(float distance) const;

    /**
     * @brief 检查点是否在聚光灯范围内
     * @param point 要检查的点
     * @return 是否在范围内
     */
    bool isInSpotlightRange(const glm::vec3& point) const;

    /**
     * @brief 设置为方向光（类似太阳光）
     * @param direction 光照方向
     * @param color 光照颜色
     * @param intensity 光照强度
     */
    void setupAsDirectional(const glm::vec3& direction, 
                           const glm::vec3& color = glm::vec3(1.0f), 
                           float intensity = 1.0f);

    /**
     * @brief 设置为点光源（类似灯泡）
     * @param position 光源位置
     * @param color 光照颜色
     * @param intensity 光照强度
     * @param range 光照范围
     */
    void setupAsPoint(const glm::vec3& position, 
                     const glm::vec3& color = glm::vec3(1.0f), 
                     float intensity = 1.0f,
                     float range = 10.0f);

    /**
     * @brief 设置为聚光灯（类似手电筒）
     * @param position 光源位置
     * @param direction 光照方向
     * @param color 光照颜色
     * @param intensity 光照强度
     * @param innerCutoff 内切角（度数）
     * @param outerCutoff 外切角（度数）
     * @param range 光照范围
     */
    void setupAsSpot(const glm::vec3& position,
                    const glm::vec3& direction,
                    const glm::vec3& color = glm::vec3(1.0f),
                    float intensity = 1.0f,
                    float innerCutoff = 12.5f,
                    float outerCutoff = 17.5f,
                    float range = 10.0f);

    /**
     * @brief 设置标准衰减参数
     * @param range 光照范围
     */
    void setStandardAttenuation(float range);

private:
    std::string name;       // 光源名称
    LightType type;         // 光源类型
    
    // 通用参数
    glm::vec3 position;     // 位置（用于点光源和聚光灯）
    glm::vec3 direction;    // 方向（用于方向光和聚光灯）
    glm::vec3 color;        // 光照颜色
    float intensity;        // 光照强度
    bool enabled;           // 是否启用

    // 点光源衰减参数
    float constant;         // 常数项衰减
    float linear;           // 线性项衰减
    float quadratic;        // 二次项衰减

    // 聚光灯参数
    float innerCutoff;      // 内切角（度数）
    float outerCutoff;      // 外切角（度数）

    // 阴影参数
    bool shadowEnabled;     // 是否启用阴影
    float shadowIntensity;  // 阴影强度
};

/**
 * @brief 着色器光源数据结构
 * 
 * 注意：此结构体必须与着色器中的 std140 布局完全匹配
 * vec3 会被填充到 16 字节（vec4）
 */
// 注意：此结构体必须与着色器中的 std140 布局完全匹配
// std140 规则：每个成员都要 16 字节对齐
struct ShaderLight {
    // Offset 0-15 (16 bytes)
    int type;               // 0=方向光, 1=点光源, 2=聚光灯
    int enabled;            // 是否启用
    float _pad1[2];         // 填充到 16 字节

    // Offset 16-31 (16 bytes)
    glm::vec3 position;     // 位置
    float _pad2;            // 填充到 16 字节

    // Offset 32-47 (16 bytes)
    glm::vec3 direction;    // 方向
    float _pad3;            // 填充到 16 字节

    // Offset 48-63 (16 bytes)
    glm::vec3 color;        // 颜色
    float intensity;        // 强度

    // Offset 64-79 (16 bytes)
    float constant;         // 衰减常数项
    float linear;           // 衰减线性项
    float quadratic;        // 衰减二次项
    float _pad4;            // 填充到 16 字节

    // Offset 80-95 (16 bytes)
    float innerCutoff;      // 聚光灯内切角（弧度）
    float outerCutoff;      // 聚光灯外切角（弧度）
    float shadowIntensity;  // 阴影强度
    float _pad5;            // 填充到 16 字节
};
// 总大小: 96 bytes (16 * 6)

/**
 * @brief 着色器光源数据数组
 */
using ShaderLightArray = std::array<ShaderLight, 16>;

} // namespace owengine

#endif // LIGHT_H