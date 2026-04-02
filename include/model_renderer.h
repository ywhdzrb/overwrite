#ifndef MODEL_RENDERER_H
#define MODEL_RENDERER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "vulkan_device.h"
#include "model.h"
#include "mesh.h"
#include "texture.h"
#include "texture_loader.h"

namespace vgame {

class ModelRenderer {
public:
    ModelRenderer(std::shared_ptr<VulkanDevice> device,
                  std::shared_ptr<TextureLoader> textureLoader);
    ~ModelRenderer();

    void create();
    void cleanup();
    void loadModel(const std::string& filename);
    void setPosition(const glm::vec3& pos);
    void setScale(const glm::vec3& scale);
    void setRotation(float pitch, float yaw, float roll);
    
    /**
     * @brief 设置纹理
     * @param texture 纹理指针
     */
    void setTexture(std::shared_ptr<Texture> texture);
    
    /**
     * @brief 获取纹理
     * @return 纹理指针
     */
    std::shared_ptr<Texture> getTexture() const { return texture; }
    
    /**
     * @brief 设置是否使用纹理
     * @param use 是否使用纹理
     */
    void setUseTexture(bool use) { useTexture = use; }
    
    /**
     * @brief 检查是否使用纹理
     * @return 是否使用纹理
     */
    bool isUsingTexture() const { return useTexture; }

    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);

private:
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 baseColor;
        float metallic;
        float roughness;
        int hasTexture;
        float _pad0;
        
        // 光源数据
        glm::vec3 lightPos;
        float lightIntensity;
        glm::vec3 lightColor;
        float _pad1;
        glm::vec3 ambientColor;
        float _pad2;
    };

    std::shared_ptr<VulkanDevice> device;
    std::shared_ptr<TextureLoader> textureLoader;
    std::unique_ptr<Model> model;
    std::unique_ptr<Mesh> mesh;
    std::shared_ptr<Texture> texture;

    glm::vec3 position;
    glm::vec3 scale;
    glm::vec3 rotation;  // 欧拉角：pitch, yaw, roll（度数）
    
    bool useTexture;
};

} // namespace vgame

#endif // MODEL_RENDERER_H