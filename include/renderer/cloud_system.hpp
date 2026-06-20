#pragma once

/**
 * @file cloud_system.hpp
 * @brief 体积云系统 — Nubis 风格连续噪声场 + Remap减法侵蚀
 *
 * 归属模块：renderer
 * 核心职责：管理程序化3D噪声纹理、执行体积云 Ray Marching 渲染
 * 依赖关系：VulkanDevice、Camera
 * 关键设计：云层在Y=80~120m高空，所有不透明物体之后渲染，深度测试剔除遮挡片段
 *           密度模型：4通道 Worley FBM → compressWorley → coverage remap →
 *                    heightProfile → erosion remap → densityMult
 *           侵蚀使用 Nubis 减法 remap(密度, 细节×强度, 1, 0, 1)：
 *             云心(密度高)保留，边缘(密度低)被侵蚀挖空，产生蓬松的自然云形态
 *           光照模型：Henvey-Greenstein相位 + Beer透射率 + lightMarch单散射
 *           v1暂不实现多散射近似和远景Cubemap烘培
 */

// 标准库
#include <memory>
#include <vector>
#include <random>

// 第三方库
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

namespace owengine {

class VulkanDevice;
class Camera;

/**
 * @brief 体积云系统 — Nubis 风格程序化噪声云渲染
 *
 * 生命周期：init() → 每帧 update() + render() → cleanup()
 * 渲染时机：Skybox → 不透明物体(Terrain/Model/Tree/Stone/Grass) → Cloud → ImGui
 *           透明度混合叠加，不写入深度，但深度测试阻止云遮挡不透明物体
 *
 * 核心算法：
 *   1. 密度 = remap(detailNoise, 1 - baseShape, 1) × densityMult
 *      - baseShape: 4通道Worley FBM × heightProfile × coverage
 *      - 侵蚀remap保留云心密度，挖掉边缘
 *   2. 光照 = Beer透射率 × HG相位函数 × 粉末糖效应
 *      - 每采样点执行 lightMarch (6步锥形采样) 计算太阳透射率
 *
 * v1限制：
 * - 深度提前终止未实现（深度学习测试已足够剔除）
 * - 远景Cubemap LOD2未实现（远景使用LOD1简化步进）
 * - 光照使用单散射近似
 * - 多散射和全天空环境光后续版本添加
 */
class CloudSystem {
public:
    explicit CloudSystem(std::shared_ptr<VulkanDevice> device);
    ~CloudSystem();

    CloudSystem(const CloudSystem&) = delete;
    CloudSystem& operator=(const CloudSystem&) = delete;

    /**
     * @brief 初始化云系统
     * @param renderPass 渲染通道
     * @param extent 渲染分辨率
     * @param msaaSamples MSAA采样数
     */
    void init(VkRenderPass renderPass, VkExtent2D extent, VkSampleCountFlagBits msaaSamples);

    /** @brief 清理所有Vulkan资源 */
    void cleanup();

    /**
     * @brief 每帧更新云系统
     * @param deltaTime 帧时间差
     * @param camera 摄像机（用于LOD评估）
     * @param sunDirection 太阳方向（用于光照计算）
     */
    void update(float deltaTime, const Camera& camera, const glm::vec3& sunDirection);

    /**
     * @brief 渲染体积云
     * @param commandBuffer Vulkan命令缓冲
     * @param camera 摄像机
     * @param sunDirection 太阳方向
     */
    void render(VkCommandBuffer commandBuffer, const Camera& camera, const glm::vec3& sunDirection);

    /** @brief 检查是否已初始化 */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief ImGui调试参数快照结构体（指针方式暴露，运行时直接修改）
     * @note 非线程安全——仅在主线程ImGui帧中使用
     */
    struct DebugParams {
        float* coverage;
        float* densityMultiplier;
        float* stepCountFloat;  // int as float for slider
        float* windSpeed;
        float* thinCloudHeight;
        float* thinCloudDensity;
        float* sunIntensity;
        bool*   dayNightEnabled;
        bool*   thinCloudEnabled;
    };

    /** @brief 获取调试参数指针（供ImGui面板运行时调节） */
    DebugParams getDebugParams() {
        return DebugParams{
            &cloudCoverage_, &cloudDensityMultiplier_,
            reinterpret_cast<float*>(&stepCount_),
            &windSpeed_, &thinCloudHeight_, &thinCloudDensity_,
            &sunIntensity_,
            &dayNightEnabled_, &thinCloudEnabled_
        };
    }

    /** @brief 设置风参数（从Renderer的全局风场调用） */
    void setWind(float speed, float directionAngleDeg) {
        windSpeed_ = speed;
        windDirection_ = directionAngleDeg;
    }

    /** @brief 设置昼夜亮度因子（Renderer主循环推进昼夜时调用） */
    void setDayFactor(float factor) { dayFactor_ = glm::clamp(factor, 0.0f, 1.0f); }

private:
    // ========== 内部数据结构 ==========

    /**
     * @brief PushConstants（128 bytes，Vulkan最小保证值）
     *
     * @note 布局：5组 = 64 + 16×4 = 128 bytes
     *       sunDir.xyz 送入着色器用于光照计算（HG相位+lightMarch）
     *       dayFactor 驱动昼夜颜色变化（从 sunDir.y 计算）
     *       windSpeed/windAngle 驱动纹理偏移风动画
     *       thinCloudHeight 控制第二层薄云起始高度
     */
    struct PushConstants {
        glm::mat4 invViewProj;              // 0-63:  逆VP矩阵
        glm::vec4 cameraPos_cloudMin;       // 64-79:  camPos.xyz + cloudHeightMin
        glm::vec4 cloudMax_time;            // 80-95:  cloudHeightMax + time + windSpeed + windAngle
        glm::vec4 params;                   // 96-111: stepCount + coverage + densityMult + thinCloudHeight
        glm::vec4 sunDir_dayFactor;         // 112-127: sunDir.xyz + dayFactor
    };
    static_assert(sizeof(PushConstants) == 128, "PushConstants must be exactly 128 bytes");

    // ========== 初始化子方法 ==========
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createNoiseTexture();
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                        VkSampleCountFlagBits msaaSamples);
    void createDescriptorSets();

    // ========== 噪声生成 ==========
    /**
     * @brief 生成4通道3D Worley噪声纹理（CPU端）
     * @param[out] data 输出缓冲区（size×size×size×4 bytes, RGBA）
     * @param size 纹理尺寸（每个维度）
     * @param cellCounts 4个通道的细胞数数组
     *
     * @note 每个通道独立生成不同频率的Worley噪声，用于FBM分形叠加
     *       输出为uint8[0,255]，Vulkan采样器自动映射到float[0,1]
     */
    static void generateMultiChannelWorley(std::vector<uint8_t>& data, int size,
                                            const int cellCounts[4]);

    // ========== LOD管理 ==========
    enum class LOD { Detail, Medium, Far };

    /** @brief 根据距离评估当前LOD级别 */
    LOD evaluateLOD(const Camera& camera) const;

    /** @brief 根据LOD设置步进次数 */
    void applyLODParams(LOD lod);

    // ========== 成员变量 ==========
    std::shared_ptr<VulkanDevice> device_;
    bool initialized_ = false;

    // --- 管线 ---
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // --- 描述符 ---
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    // --- 3D噪声纹理（64³, RGBA8, 4通道Worley不同频率） ---
    // 保持64³保证GPU缓存友好：1MB可完全驻留L2缓存，采样延迟低
    static constexpr int NOISE_TEX_SIZE = 64;
    VkImage noiseImage_ = VK_NULL_HANDLE;
    VmaAllocation noiseImageAllocation_ = VK_NULL_HANDLE;
    VkImageView noiseImageView_ = VK_NULL_HANDLE;
    VkSampler noiseSampler_ = VK_NULL_HANDLE;

    // --- 运行时参数 ---
    float time_ = 0.0f;
    float cloudHeightMin_ = 80.0f;              // 云层底部高度
    float cloudHeightMax_ = 120.0f;             // 云层顶部高度
    float cloudCoverage_ = 0.55f;               // 云覆盖量 [0,1]（控制覆盖阈值偏移，更大 = 更多云）
    float cloudDensityMultiplier_ = 1.5f;       // 密度倍率（最终密度乘数，配合EXTINCTION=0.5使用）
    float sunIntensity_ = 1.0f;                 // 太阳光照强度
    glm::vec3 lastSunDir_{0.25f, 0.55f, 0.50f}; // 上一帧太阳方向

    // --- 风参数（由Renderer全局风场驱动） ---
    float windSpeed_ = 0.3f;                    // 风速 m/s
    float windDirection_ = 0.0f;                // 风向角度（度，0=+X轴）

    // --- 昼夜循环 ---
    float dayFactor_ = 1.0f;                    // 昼夜亮度因子 [0,1]
    bool  dayNightEnabled_ = true;              // 是否启用昼夜颜色变化

    // --- 薄云层参数 ---
    float thinCloudHeight_ = 180.0f;            // 薄云底部高度 m
    float thinCloudDensity_ = 0.3f;             // 薄云密度乘数 [0,1]
    bool  thinCloudEnabled_ = false;            // 是否启用薄云层（默认关，地面视角体验有限）

    // --- LOD ---
    LOD currentLOD_ = LOD::Detail;
    int stepCount_ = 64;

    // --- 屏幕参数 ---
    VkExtent2D screenExtent_{};
};

} // namespace owengine
