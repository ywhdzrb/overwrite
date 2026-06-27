#pragma once

/**
 * @file shadow_mapper.hpp
 * @brief 阴影映射管理器 — 方向光阴影贴图创建、渲染与采样
 *
 * 归属模块：renderer
 * 核心职责：管理方向光（太阳）的阴影贴图，提供阴影渲染通道和管线，
 *          在主渲染 pass 中采样阴影贴图实现动态阴影效果。
 *
 * 设计要点：
 * - 单张阴影贴图 2048×2048，VK_FORMAT_D32_SFLOAT
 * - 正交投影，以相机为中心动态更新
 * - 深度偏移缓解阴影粉刺
 * - 硬件 PCF（VK_FILTER_LINEAR + 比较模式）
 * - 阴影描述符集 (set=2)：阴影贴图采样器 + 光源 VP 矩阵 UBO
 *
 * 生命周期：Renderer 持有，每帧调用 updateLightMatrix + beginShadowPass/endShadowPass
 */

// 标准库
#include <memory>
#include <vector>

// 第三方库
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

namespace owengine {

class VulkanDevice;

/**
 * @brief 阴影映射管理器
 * @note 非线程安全，必须在渲染线程调用
 */
class ShadowMapper {
    // 并发帧数（必须与 Renderer::MAX_FRAMES_IN_FLIGHT 一致）
    static constexpr uint32_t MAX_SHADOW_FRAMES = 2;

public:
    ShadowMapper() = default;
    ~ShadowMapper() = default;

    ShadowMapper(const ShadowMapper&) = delete;
    ShadowMapper& operator=(const ShadowMapper&) = delete;
    ShadowMapper(ShadowMapper&&) noexcept = default;
    ShadowMapper& operator=(ShadowMapper&&) noexcept = default;

    /**
     * @brief 初始化阴影映射器
     * @param device   Vulkan 设备
     * @param mapSize  阴影贴图分辨率（默认 2048）
     * @param dsLayouts 主管线的描述符集布局列表（让阴影管线布局包含相同集合，兼容 vkCmdBindDescriptorSets）
     */
    void init(const std::shared_ptr<VulkanDevice>& device, uint32_t mapSize = 2048,
              const std::vector<VkDescriptorSetLayout>& dsLayouts = {});

    /** @brief 清理所有 Vulkan 资源 */
    void cleanup();

    // ==================== 阴影 pass 管理 ====================

    /**
     * @brief 开始阴影渲染通道（清除深度、绑定管线、设置视口）
     */
    void beginShadowPass(VkCommandBuffer commandBuffer);

    /**
     * @brief 结束阴影渲染通道
     */
    void endShadowPass(VkCommandBuffer commandBuffer);

    /**
     * @brief 更新光源 VP 矩阵（每帧调用）
     * @param lightDir  光源方向（场景→光源，归一化）
     * @param cameraPos 相机世界坐标
     * @param orthoSize 正交投影包围盒半边长（默认 150.0）
     */
    void updateLightMatrix(const glm::vec3& lightDir,
                           const glm::vec3& cameraPos,
                           float orthoSize = 150.0f);

    // ==================== 描述符集管理 ====================

    /**
     * @brief 创建描述符集布局 (set=2) — 在 init 时调用
     */
    void createDescriptorSetLayout();

    /**
     * @brief 分配并更新每帧的阴影描述符集
     * @param descriptorPool 外部描述符池
     */
    void allocateDescriptorSets(VkDescriptorPool descriptorPool);

    // ==================== 访问器 ====================

    VkRenderPass getRenderPass() const { return shadowRenderPass_; }
    VkPipeline getPipeline() const { return shadowPipeline_; }
    VkPipelineLayout getPipelineLayout() const { return shadowPipelineLayout_; }
    uint32_t getMapSize() const { return mapSize_; }
    const glm::mat4& getLightVP() const { return lightVP_; }
    const glm::mat4& getLightView() const { return lightView_; }
    const glm::mat4& getLightProj() const { return lightProj_; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return dsLayout_; }

    /**
     * @brief 获取指定帧的阴影描述符集
     * @param frameIndex 当前帧索引（0 ~ MAX_SHADOW_FRAMES-1）
     */
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const {
        return (frameIndex < MAX_SHADOW_FRAMES) ? shadowDescriptorSets_[frameIndex] : VK_NULL_HANDLE;
    }

    bool isInitialized() const { return initialized_; }

    /**
     * @brief 更新指定帧的阴影 uniform 缓冲（直接写映射内存）
     * @param frameIndex 当前帧索引（0 ~ MAX_SHADOW_FRAMES-1）
     */
    void updateUniformBuffer(uint32_t frameIndex);

    /**
     * @brief 设置阴影强度（随昼夜循环变化，实现平滑过渡）
     * @param intensity 0.0=无阴影 ~ 1.0=最强阴影
     */
    void setShadowIntensity(float intensity) { shadowIntensity_ = intensity; }
    float getShadowIntensity() const { return shadowIntensity_; }

private:
    void createShadowMapImage();
    void createSampler();
    void createRenderPass();
    void createFramebuffer();
    void createPipeline();
    void createUniformBuffer();

    std::shared_ptr<VulkanDevice> device_;
    // 主管线描述符集布局列表，阴影管线布局需要包含相同布局以兼容 vkCmdBindDescriptorSets
    std::vector<VkDescriptorSetLayout> pipelineDsLayouts_;
    bool initialized_ = false;

    uint32_t mapSize_ = 2048;

    // 阴影贴图资源
    VkImage shadowMapImage_ = VK_NULL_HANDLE;
    VmaAllocation shadowMapAllocation_ = VK_NULL_HANDLE;
    VkImageView shadowMapView_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;

    // 帧缓冲
    VkFramebuffer shadowFramebuffer_ = VK_NULL_HANDLE;

    // 渲染通道
    VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;

    // 管线
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;

    // 阴影 uniform 缓冲（光源 VP + 参数），每帧独立以防帧并发撕裂
    VkBuffer shadowUniformBuffers_[MAX_SHADOW_FRAMES] = {};
    VmaAllocation shadowUniformAllocations_[MAX_SHADOW_FRAMES] = {};
    void* shadowUniformMapped_[MAX_SHADOW_FRAMES] = {};

    // 描述符（set=2），每帧独立
    VkDescriptorSetLayout dsLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet shadowDescriptorSets_[MAX_SHADOW_FRAMES] = {};

    // 阴影参数
    glm::mat4 lightVP_{1.0f};
    glm::mat4 lightView_{1.0f};
    glm::mat4 lightProj_{1.0f};
    float depthBiasConstant_ = 0.5f;
    float depthBiasSlope_ = 0.3f;
    float shadowIntensity_ = 0.6f;
};

} // namespace owengine
