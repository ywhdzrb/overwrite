#ifndef FLOOR_RENDERER_H
#define FLOOR_RENDERER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace vgame {

class VulkanDevice;
class VulkanCommandBuffer;

class FloorRenderer {
public:
    FloorRenderer(std::shared_ptr<VulkanDevice> device);
    ~FloorRenderer();
    
    void create();
    void cleanup();
    
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    
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
};

} // namespace vgame

#endif // FLOOR_RENDERER_H