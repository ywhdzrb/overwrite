#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>
#include <array>

#include "vulkan_device.h"

namespace vgame {

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
                      VkMemoryPropertyFlags properties, 
                      VkBuffer& buffer, VkDeviceMemory& memory);
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    
    std::shared_ptr<VulkanDevice> vulkanDevice;
    
    // 顶点缓冲
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    
    // 索引缓冲
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    
    // 纹理
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
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
    static constexpr uint32_t CHAR_WIDTH = 16;
    static constexpr uint32_t CHAR_HEIGHT = 16;
};

} // namespace vgame

#endif // TEXT_RENDERER_H