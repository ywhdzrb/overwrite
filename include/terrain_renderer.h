#ifndef TERRAIN_RENDERER_H
#define TERRAIN_RENDERER_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "i_renderer.h"

namespace vgame {

class VulkanDevice;

class TerrainRenderer : public IRenderer {
public:
    explicit TerrainRenderer(std::shared_ptr<VulkanDevice> device);
    ~TerrainRenderer() override;

    void create() override;
    void cleanup() override;
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    std::string getName() const override { return "TerrainRenderer"; }
    bool isCreated() const override { return created_; }
    
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 baseColor;
        float metallic;
        float roughness;
        int hasTexture;
        float _pad0;
    };

float getHeight(float x, float z);
    
private:
    float perlinNoise(float x, float z);
    float fbm(float x, float z, int octaves = 4);
    void createTerrainVertexBuffer();
    void createTerrainIndexBuffer();
    void cleanupBuffers();

protected:
    std::shared_ptr<VulkanDevice> device;
    
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount;
    
    glm::vec3 centerPos;
    float size;
    float noiseScale;
    float heightScale;
    glm::vec3 terrainColor;  // 地形颜色控制参数
    
    std::vector<int> perm;
    
    static constexpr int gridSize = 16;
    static constexpr int segmentsPerTile = 2;
    static constexpr int totalSegments = gridSize * segmentsPerTile;
};

} // namespace vgame

#endif // TERRAIN_RENDERER_H
