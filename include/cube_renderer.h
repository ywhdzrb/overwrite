#ifndef CUBE_RENDERER_H
#define CUBE_RENDERER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace vgame {

class VulkanDevice;
class VulkanCommandBuffer;

class CubeRenderer {
public:
    CubeRenderer(std::shared_ptr<VulkanDevice> device);
    ~CubeRenderer();
    
    void create();
    void cleanup();
    
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    
    void setPosition(const glm::vec3& position);
    
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };

private:
    void createVertexBuffer();
    void createIndexBuffer();
    void cleanupBuffers();

private:
    std::shared_ptr<VulkanDevice> device;
    
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount;
    
    glm::vec3 position;
};

} // namespace vgame

#endif // CUBE_RENDERER_H