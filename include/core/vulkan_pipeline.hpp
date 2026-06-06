#pragma once

/**
 * @file vulkan_pipeline.hpp
 * @brief Vulkan 图形管线管理 — 着色器编译、顶点输入、管线状态配置
 *
 * 归属模块：core
 * 核心职责：封装 VkPipeline 和 VkPipelineLayout 生命周期
 * 依赖关系：VulkanDevice、VulkanRenderPass
 * 关键设计：支持不同顶点格式（POSITION_COLOR / POSITION_ONLY），
 *           通过着色器路径自动判断天空盒/标准管线
 */

// 标准库
#include <memory>
#include <vector>
#include <string>

// 第三方库
#include <vulkan/vulkan.h>

namespace owengine {

class VulkanDevice;

enum class VertexFormat {
    POSITION_COLOR,  // 位置 + 颜色（标准）
    POSITION_ONLY    // 仅位置（天空盒）
};

class VulkanPipeline {
public:
    VulkanPipeline(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass,
                   VkExtent2D swapchainExtent, const std::string& vertexShaderPath,
                   const std::string& fragmentShaderPath,
                   VertexFormat format = VertexFormat::POSITION_COLOR,
                   const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
                   VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    ~VulkanPipeline();

    // 禁止拷贝
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    void create();
    void cleanup();
    
    VkPipeline getPipeline() const { return graphicsPipeline_; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }
    
    void setMsaaSamples(VkSampleCountFlagBits samples) { msaaSamples_ = samples; }

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    std::shared_ptr<VulkanDevice> device_;
    VkRenderPass renderPass_;
    VkExtent2D swapchainExtent_;
    std::string vertexShaderPath_;
    std::string fragmentShaderPath_;
    VkSampleCountFlagBits msaaSamples_;

    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> descriptorSetLayoutsList_;

}; // end of class VulkanPipeline

} // namespace owengine
