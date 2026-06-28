#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <memory>
#include <string>
#include <vector>
#include <array>

#include "core/vulkan_device.hpp"

namespace owengine {

class TextRenderer {
public:
    TextRenderer(std::shared_ptr<VulkanDevice> device);
    ~TextRenderer();

    void create();
    void cleanup();
    
    // 渲染文本到指定位置
    void render(VkCommandBuffer commandBuffer, 
                const std::string& text, 
                float x, float y, 
                float scale);

private:
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, 
                      const VmaAllocationCreateInfo& allocInfo, 
                      VkBuffer& buffer, VmaAllocation& allocation);
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    
    std::shared_ptr<VulkanDevice> vulkanDevice_;
    
    // 顶点缓冲
    VkBuffer vertexBuffer_;
    VmaAllocation vertexBufferAllocation_;
    
    // 索引缓冲
    VkBuffer indexBuffer_;
    VmaAllocation indexBufferAllocation_;
    
    // 纹理
    VkImage textureImage_;
    VmaAllocation textureImageAllocation_;
    VkImageView textureImageView_;
    VkSampler textureSampler_;
    
    // 描述符集
    VkDescriptorSetLayout descriptorSetLayout_;
    VkDescriptorPool descriptorPool_;
    VkDescriptorSet descriptorSet_;
    
    // 管线布局
    VkPipelineLayout pipelineLayout_;
    VkPipeline pipeline_;
    
    static constexpr uint32_t TEXTURE_WIDTH = 256;
    static constexpr uint32_t TEXTURE_HEIGHT = 64;
    static constexpr uint32_t GLYPH_WIDTH = 16;
    static constexpr uint32_t GLYPH_HEIGHT = 16;
};

} // namespace owengine
