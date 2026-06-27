#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace owengine {

class VulkanDevice;

class VulkanCommandBuffer {
public:
    VulkanCommandBuffer(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass, 
                       VkPipeline pipeline, VkPipelineLayout pipelineLayout);
    ~VulkanCommandBuffer();

    // 禁止拷贝
    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;

    void create(size_t imageCount);
    void cleanup();
    void record(size_t imageIndex, VkFramebuffer framebuffer, VkExtent2D swapchainExtent,
               const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    
    const std::vector<VkCommandBuffer>& getCommandBuffers() const { return commandBuffers_; }
    
    // 更新 renderPass_ 和 pipeline_（用于重建交换链时）
    void updateRenderPass(VkRenderPass newRenderPass) { renderPass_ = newRenderPass; }
    void updatePipeline(VkPipeline newPipeline) { pipeline_ = newPipeline; }
    void updatePipelineLayout(VkPipelineLayout newPipelineLayout) { pipelineLayout_ = newPipelineLayout; }

private:
    std::shared_ptr<VulkanDevice> device_;
    VkRenderPass renderPass_;
    VkPipeline pipeline_;
    VkPipelineLayout pipelineLayout_;
    
    std::vector<VkCommandBuffer> commandBuffers_;
};

} // namespace owengine
