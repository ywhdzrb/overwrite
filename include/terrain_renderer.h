#ifndef TERRAIN_CHUNK_H
#define TERRAIN_CHUNK_H

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vgame {

class VulkanDevice;

struct TerrainChunk {
    int chunkX;
    int chunkZ;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount;
    bool isValid;
};

class TerrainRenderer {
public:
    explicit TerrainRenderer(std::shared_ptr<VulkanDevice> device);
    ~TerrainRenderer();

    void create();
    void cleanup();
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    std::string getName() const { return "TerrainRenderer"; }
    bool isCreated() const { return created_; }
    
    void update(const glm::vec3& playerPos);
    float getHeight(float x, float z);
    
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

private:
    struct ChunkKey {
        int x, z;
        
        bool operator==(const ChunkKey& other) const {
            return x == other.x && z == other.z;
        }
    };
    
    struct ChunkKeyHash {
        size_t operator()(const ChunkKey& key) const {
            return std::hash<int>()(key.x * 1000 + key.z);
        }
    };

    float perlinNoise(float x, float z);
    float fbm(float x, float z, int octaves);
    void generateChunk(int chunkX, int chunkZ);
    void cleanupChunk(TerrainChunk& chunk);

    std::shared_ptr<VulkanDevice> device;
    
    std::unordered_map<ChunkKey, TerrainChunk, ChunkKeyHash> chunks;
    
    float chunkSize;
    int renderRadius;
    float noiseScale;
    float heightScale;
    float baseHeight;
    glm::vec3 terrainColor;
    
    bool created_;
    
    std::vector<int> perm;
};

} // namespace vgame

#endif // TERRAIN_CHUNK_H