#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <memory>
#include <string>
#include <vector>
#include <array>

#include "core/vulkan_device.h"

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
    
    std::shared_ptr<VulkanDevice> vulkanDevice;
    
    // 顶点缓冲
    VkBuffer vertexBuffer;
    VmaAllocation vertexBufferAllocation;
    
    // 索引缓冲
    VkBuffer indexBuffer;
    VmaAllocation indexBufferAllocation;
    
    // 纹理
    VkImage textureImage;
    VmaAllocation textureImageAllocation;
    VkImageView textureImageView;
    VkSampler textureSampler;
    
    // 描述符集
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    
    // 管线布局
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    
    static constexpr uint32_t TEXTURE_WIDTH = 256;
    static constexpr uint32_t TEXTURE_HEIGHT = 64;
    static constexpr uint32_t GLYPH_WIDTH = 16;
    static constexpr uint32_t GLYPH_HEIGHT = 16;
};

} // namespace owengine
