#ifndef MODEL_RENDERER_H
#define MODEL_RENDERER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "vulkan_device.h"
#include "model.h"
#include "mesh.h"

namespace vgame {

class ModelRenderer {
public:
    ModelRenderer(std::shared_ptr<VulkanDevice> device);
    ~ModelRenderer();

    void create();
    void cleanup();
    void loadModel(const std::string& filename);
    void setPosition(const glm::vec3& pos);
    void setScale(const glm::vec3& scale);
    void setRotation(float pitch, float yaw, float roll);

    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);

private:
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };

    std::shared_ptr<VulkanDevice> device;
    std::unique_ptr<Model> model;
    std::unique_ptr<Mesh> mesh;

    glm::vec3 position;
    glm::vec3 scale;
    glm::vec3 rotation;  // 欧拉角：pitch, yaw, roll（度数）
};

} // namespace vgame

#endif // MODEL_RENDERER_H