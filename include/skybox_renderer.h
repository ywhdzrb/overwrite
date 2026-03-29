#ifndef SKYBOX_RENDERER_H
#define SKYBOX_RENDERER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>

namespace vgame {

class VulkanDevice;
class VulkanPipeline;

class SkyboxRenderer {
public:
    SkyboxRenderer(std::shared_ptr<VulkanDevice> device);
    ~SkyboxRenderer();
    
    void create();
    void cleanup();
    
    // 从 6 张纹理文件加载立方体贴图
    // 顺序: 右, 左, 上, 下, 前, 后
    void loadCubemapFromFiles(const std::vector<std::string>& facePaths);
    
    // 从十字形纹理加载立方体贴图
    void loadCubemapFromCrossLayout(const std::string& imagePath);
    
    // 渲染天空盒
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    
    struct PushConstants {
        glm::mat4 view;
        glm::mat4 proj;
    };
    
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkDescriptorSet getDescriptorSet() const { return descriptorSet; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

private:
    void createVertexBuffer();
    void createIndexBuffer();
    void createCubemapImage(const std::vector<unsigned char*>& faceData, int width, int height);
    void createCubemapImageView();
    void createCubemapSampler();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSet();
    void cleanupBuffers();
    void cleanupCubemap();
    void cleanupDescriptors();

private:
    std::shared_ptr<VulkanDevice> device;
    
    // 立方体网格
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount;
    
    // 立方体贴图
    VkImage cubemapImage;
    VkDeviceMemory cubemapImageMemory;
    VkImageView cubemapImageView;
    VkSampler cubemapSampler;
    
    // 描述符
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    
    VkPipelineLayout pipelineLayout;
};

} // namespace vgame

#endif // SKYBOX_RENDERER_H