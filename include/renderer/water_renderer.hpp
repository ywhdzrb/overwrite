#pragma once

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
 * @brief 水面渲染器 — 高度场波动方程模拟 + 程序化波浪
 *
 * 核心使用 2D 波动方程计算交互涟漪：
 *   ∂²h/∂t² = c²∇²h - damping·∂h/∂t
 * 通过计算着色器每帧更新高度场，顶点着色器采样做位移和法线。
 */
class WaterRenderer {
public:
    explicit WaterRenderer(std::shared_ptr<VulkanDevice> device);
    ~WaterRenderer();

    WaterRenderer(const WaterRenderer&) = delete;
    WaterRenderer& operator=(const WaterRenderer&) = delete;

    void init(VkRenderPass renderPass, VkExtent2D extent,
              VkSampleCountFlagBits msaaSamples, float seaLevel);
    void cleanup();

    void update(float deltaTime, const glm::vec3& sunDirection,
                float sunIntensity);

    void render(VkCommandBuffer commandBuffer,
                const glm::mat4& viewMatrix,
                const glm::mat4& projectionMatrix,
                const glm::vec3& cameraPos);

    void setWaveParams(float amp, float freq, float speed) {
        waveAmp_ = amp; waveFreq_ = freq; waveSpeed_ = speed;
    }
    void setColor(const glm::vec3& color, float alpha) {
        waterColor_ = color; waterAlpha_ = alpha;
    }
    bool isInitialized() const { return initialized_; }
    void rebuildPipeline(VkRenderPass renderPass, VkExtent2D extent,
                         VkSampleCountFlagBits msaaSamples);
    float getSeaLevel() const { return seaLevel_; }
    void setInteractionPoint(const glm::vec3& worldPos, float strength = 1.0f, float radius = 8.0f) {
        interactionPos_ = worldPos;
        interactionStrength_ = strength;
        interactionRadius_ = radius;
    }

private:
    void createGrid();
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                        VkSampleCountFlagBits msaaSamples);

    void initWaveSim();
    void cleanupWaveSim();
    void dispatchWaveSim(VkCommandBuffer cmd, const glm::vec3& cameraPos);

    struct PushConstants {
        glm::mat4 model;               // 0-63
        glm::mat4 view;                // 64-127
        glm::mat4 proj;                // 128-191
        glm::vec4 color;               // 192-207
        glm::vec4 waveParams;          // 208-223
        glm::vec4 sunDir_intensity;    // 224-239
        glm::vec4 interaction;         // 240-255
    };

    // 波动方程模拟常量
    static constexpr int WAVE_TEX_SIZE = 256;
    static constexpr float WAVE_COVERAGE = 128.0f;  // 覆盖世界范围(m)

    std::shared_ptr<VulkanDevice> device_;
    bool initialized_ = false;

    // 网格缓冲
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation indexBufferAllocation_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;

    // 图形管线
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // 波动方程计算管线
    VkPipelineLayout wavePipeLayout_ = VK_NULL_HANDLE;
    VkPipeline wavePipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout waveDsLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool waveDsPool_ = VK_NULL_HANDLE;
    VkDescriptorSet waveDescSets_[2] = {};
    VkSampler waveSampler_ = VK_NULL_HANDLE;

    // 高度场纹理（ping-pong: 0=prev, 1=curr）
    VkImage waveImages_[2] = {};
    VmaAllocation waveAllocs_[2] = {};
    VkImageView waveViews_[2] = {};
    int waveCurrIdx_ = 0;  // 当前帧作为"当前"的纹理索引

    // 参数
    float seaLevel_ = -2.0f;
    float time_ = 0.0f;
    float waveAmp_ = 0.4f;
    float waveFreq_ = 0.05f;
    float waveSpeed_ = 1.2f;
    glm::vec3 waterColor_{0.05f, 0.15f, 0.25f};
    float waterAlpha_ = 0.85f;

    static constexpr float GRID_SIZE = 500.0f;
    static constexpr int GRID_SEGMENTS = 100;

    glm::vec3 sunDirection_{0.25f, 0.55f, 0.50f};
    float sunIntensity_ = 1.0f;

    glm::vec3 interactionPos_{0.0f};
    float interactionStrength_ = 0.3f;  // 默认为轻微涟漪
    float interactionRadius_ = 8.0f;
    float currentDt_ = 1.0f / 60.0f;
    float damping_ = 0.015f;
};

} // namespace owengine
