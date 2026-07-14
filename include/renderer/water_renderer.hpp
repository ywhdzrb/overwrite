#pragma once

/**
 * @file water_renderer.hpp
 * @brief 水面渲染器 — 程序化波浪动画 + 半透明水面
 *
 * 归属模块：renderer
 * 核心职责：在世界空间 seaLevel 高度渲染大型水面网格，
 *           顶点着色器做多层正弦波位移 + 法线扰动，
 *           片段着色器做菲涅尔反射/透射/高光。
 * 依赖关系：VulkanDevice、Camera
 * 关键设计：水面使用独立管线（alpha 混合启用），
 *           在 terrain 之后、透明物体之前渲染。
 *           网格是固定大小的平面，随玩家居中移动（类 TerrainRenderer 的区块思路），
 *           但此处简化：一张大网格覆盖可视范围 + 余量。
 */

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include "utils/asset_paths.hpp"

namespace owengine {

class VulkanDevice;
class Camera;

/**
 * @brief 水面渲染器
 *
 * 管理水面网格的顶点/索引缓冲、管线、渲染。
 * 每一帧更新累计时间用于波浪动画。
 */
class WaterRenderer {
public:
    explicit WaterRenderer(std::shared_ptr<VulkanDevice> device);
    ~WaterRenderer();

    WaterRenderer(const WaterRenderer&) = delete;
    WaterRenderer& operator=(const WaterRenderer&) = delete;

    /**
     * @brief 初始化
     * @param renderPass 渲染通道
     * @param extent 渲染分辨率
     * @param msaaSamples MSAA 采样数
     * @param seaLevel 海平面高度（从 TerrainParams 获取）
     */
    void init(VkRenderPass renderPass, VkExtent2D extent,
              VkSampleCountFlagBits msaaSamples, float seaLevel);

    /** @brief 清理 Vulkan 资源 */
    void cleanup();

    /**
     * @brief 每帧更新
     * @param deltaTime 帧间隔
     * @param sunDirection 太阳方向
     * @param sunIntensity 太阳强度
     */
    void update(float deltaTime, const glm::vec3& sunDirection,
                float sunIntensity);

    /**
     * @brief 渲染水面
     * @param commandBuffer 命令缓冲
     * @param viewMatrix 视图矩阵
     * @param projectionMatrix 投影矩阵
     * @param cameraPos 相机世界位置（用于网格跟随）
     */
    void render(VkCommandBuffer commandBuffer,
                const glm::mat4& viewMatrix,
                const glm::mat4& projectionMatrix,
                const glm::vec3& cameraPos);

    /** @brief 设置波浪参数 */
    void setWaveParams(float amp, float freq, float speed) {
        waveAmp_ = amp; waveFreq_ = freq; waveSpeed_ = speed;
    }

    /** @brief 设置水面颜色和透明度 */
    void setColor(const glm::vec3& color, float alpha) {
        waterColor_ = color; waterAlpha_ = alpha;
    }

    /** @brief 是否已初始化 */
    bool isInitialized() const { return initialized_; }

    /** @brief 重新创建管线（交换链重建时调用） */
    void rebuildPipeline(VkRenderPass renderPass, VkExtent2D extent,
                         VkSampleCountFlagBits msaaSamples);

    /** @brief 获取海平面高度 */
    float getSeaLevel() const { return seaLevel_; }

private:
    void createGrid();
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                        VkSampleCountFlagBits msaaSamples);

    struct PushConstants {
        glm::mat4 model;               // 0-63
        glm::mat4 view;                // 64-127
        glm::mat4 proj;                // 128-191
        glm::vec4 color;               // 192-207: 水面颜色 rgb + 透明度 a
        glm::vec4 waveParams;          // 208-223: waveAmp, waveFreq, waveSpeed, time
        glm::vec4 sunDir_intensity;    // 224-239: sunDir.xyz + intensity
    };
    // 240 字节，与 TerrainRenderer::PushConstants 一致
    // 注：部分 GPU 限制 maxPushConstantsSize=128，若遇兼容问题可将 model/view/proj
    // 合并为 mvp，移除非必需字段，或改用 UBO

    std::shared_ptr<VulkanDevice> device_;
    bool initialized_ = false;

    // 网格缓冲
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation indexBufferAllocation_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;

    // 管线
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // 参数
    float seaLevel_ = -2.0f;
    float time_ = 0.0f;
    float waveAmp_ = 0.4f;
    float waveFreq_ = 0.05f;
    float waveSpeed_ = 1.2f;
    glm::vec3 waterColor_{0.05f, 0.15f, 0.25f};
    float waterAlpha_ = 0.85f;

    // 网格尺寸
    static constexpr float GRID_SIZE = 500.0f;  // 总宽/深
    static constexpr int GRID_SEGMENTS = 100;    // 分段数

    // 太阳参数
    glm::vec3 sunDirection_{0.25f, 0.55f, 0.50f};
    float sunIntensity_ = 1.0f;
};

} // namespace owengine
