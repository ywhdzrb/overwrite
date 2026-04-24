#ifndef CUBE_RENDERER_H
#define CUBE_RENDERER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include "i_renderer.h"

namespace owengine {

class VulkanDevice;
class VulkanCommandBuffer;

class CubeRenderer : public IRenderer {
public:
    explicit CubeRenderer(std::shared_ptr<VulkanDevice> device);
    ~CubeRenderer() override;
    
    // IRenderer 接口实现
    void create() override;
    void cleanup() override;
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    std::string getName() const override { return "CubeRenderer"; }
    bool isCreated() const override { return created_; }
    
    void setPosition(const glm::vec3& position);
    
    // 碰撞体相关
    glm::vec3 getColliderSize() const { return glm::vec3(5.0f); }  // 立方体大小 5.0
    glm::vec3 getColliderCenter() const { return position; }
    
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
    
    glm::vec3 position;
};

} // namespace owengine

#endif // CUBE_RENDERER_H