#ifndef VULKAN_COMMAND_BUFFER_H
#define VULKAN_COMMAND_BUFFER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace vgame {

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
    
    const std::vector<VkCommandBuffer>& getCommandBuffers() const { return commandBuffers; }

private:
    std::shared_ptr<VulkanDevice> device;
    VkRenderPass renderPass;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
};

} // namespace vgame

#endif // VULKAN_COMMAND_BUFFER_H