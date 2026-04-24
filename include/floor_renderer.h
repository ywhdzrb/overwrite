#ifndef FLOOR_RENDERER_H
#define FLOOR_RENDERER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include "i_renderer.h"

namespace owengine {

class VulkanDevice;
class VulkanCommandBuffer;

class FloorRenderer : public IRenderer {
public:
    explicit FloorRenderer(std::shared_ptr<VulkanDevice> device);
    ~FloorRenderer() override;
    
    // IRenderer 接口实现
    void create() override;
    void cleanup() override;
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    std::string getName() const override { return "FloorRenderer"; }
    bool isCreated() const override { return created_; }
    
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 baseColor;
        float metallic;
        float roughness;
        int hasTexture;
        float _pad0;  // 填充到16字节对齐
    };

private:
    void createVertexBuffer();
    void createIndexBuffer();
    void cleanupBuffers();

protected:
    std::shared_ptr<VulkanDevice> device;
    
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount;
};

} // namespace owengine

#endif // FLOOR_RENDERER_H