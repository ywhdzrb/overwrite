#include "terrain_renderer.h"
#include "vulkan_device.h"
#include <glm/glm.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <random>

namespace vgame {

namespace {

int fastFloor(float v) {
    return v > 0 ? static_cast<int>(v) : static_cast<int>(v) - 1;
}

float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float grad(int h, float x, float y) {
    int hh = h & 15;
    float u = hh < 8 ? x : y;
    float v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : 0.0f);
    return ((hh & 1) == 0 ? u : -u) + ((hh & 2) == 0 ? v : -v);
}

static std::mt19937 rng(42);
static std::uniform_int_distribution<int> dist(0, 255);
static bool permInitialized = false;

void initPerm(std::vector<int>& perm) {
    if (permInitialized) return;
    perm.resize(512);
    for (int i = 0; i < 256; ++i) perm[i] = i;
    for (int i = 255; i > 0; --i) {
        int j = dist(rng) % (i + 1);
        std::swap(perm[i], perm[j]);
    }
    for (int i = 0; i < 256; ++i) perm[256 + i] = perm[i];
    permInitialized = true;
}

} // anonymous namespace

TerrainRenderer::TerrainRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      chunkSize(16.0f),
      renderRadius(10),
      noiseScale(0.3f),
      heightScale(1.5f),
      baseHeight(0.0f),
      terrainColor(0.0f, 1.0f, 0.0f),
      created_(false) {
    static std::mt19937 rng(42);
    static std::uniform_int_distribution<int> dist(0, 255);
    perm.resize(512);
    for (int i = 0; i < 256; ++i) {
        perm[i] = i;
    }
    for (int i = 255; i > 0; --i) {
        int j = dist(rng) % (i + 1);
        std::swap(perm[i], perm[j]);
    }
    for (int i = 0; i < 256; ++i) {
        perm[256 + i] = perm[i];
    }
}

TerrainRenderer::~TerrainRenderer() {
    cleanup();
}

void TerrainRenderer::create() {
    created_ = true;
}

void TerrainRenderer::cleanup() {
    for (auto& pair : chunks) {
        cleanupChunk(pair.second);
    }
    chunks.clear();
    created_ = false;
}

float TerrainRenderer::perlinNoise(float x, float z) {
    initPerm(perm);
    
    int xi = fastFloor(x) & 255;
    int zi = fastFloor(z) & 255;
    
    float xf = x - fastFloor(x);
    float zf = z - fastFloor(z);
    
    float u = fade(xf);
    float v = fade(zf);
    
    int aa = perm[perm[xi] + zi];
    int ab = perm[perm[xi] + zi + 1];
    int ba = perm[perm[xi + 1] + zi];
    int bb = perm[perm[xi + 1] + zi + 1];
    
    float aaGrad = grad(aa, xf, zf);
    float abGrad = grad(ab, xf, zf - 1.0f);
    float baGrad = grad(ba, xf - 1.0f, zf);
    float bbGrad = grad(bb, xf - 1.0f, zf - 1.0f);
    
    float n0 = lerp(aaGrad, baGrad, u);
    float n1 = lerp(abGrad, bbGrad, u);
    
    return lerp(n0, n1, v);
}

float TerrainRenderer::fbm(float x, float z, int octaves) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        value += perlinNoise(x * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    
    return value / maxValue;
}

float TerrainRenderer::getHeight(float x, float z) {
    float noiseX = x * noiseScale;
    float noiseZ = z * noiseScale;
    float height = fbm(noiseX, noiseZ, 4) * heightScale;
    return baseHeight + height;
}

void TerrainRenderer::generateChunk(int chunkX, int chunkZ) {
    ChunkKey key{chunkX, chunkZ};
    if (chunks.find(key) != chunks.end()) {
        return;
    }
    
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec3 color;
        glm::vec2 texCoord;
    };
    
    const int segments = 16;
    float startX = static_cast<float>(chunkX) * chunkSize;
    float startZ = static_cast<float>(chunkZ) * chunkSize;
    
    std::vector<Vertex> vertices;
    vertices.reserve((segments + 1) * (segments + 1));
    
    for (int z = 0; z <= segments; ++z) {
        for (int x = 0; x <= segments; ++x) {
            float worldX = startX + static_cast<float>(x) / segments * chunkSize;
            float worldZ = startZ + static_cast<float>(z) / segments * chunkSize;
            
            float height = getHeight(worldX, worldZ);
            float posY = height;
            
            float dNoise = 0.05f;
            float hL = getHeight(worldX - dNoise, worldZ);
            float hR = getHeight(worldX + dNoise, worldZ);
            float hD = getHeight(worldX, worldZ - dNoise);
            float hU = getHeight(worldX, worldZ + dNoise);
            
            float nx = (hL - hR) / (2.0f * dNoise);
            float nz = (hD - hU) / (2.0f * dNoise);
            glm::vec3 normal = glm::normalize(glm::vec3(-nx, 1.0f, -nz));
            
            float height01 = (height - baseHeight) / heightScale;
            height01 = glm::clamp(height01 * 0.5f + 0.5f, 0.0f, 1.0f);
            glm::vec3 color(height01 * terrainColor.x, height01 * terrainColor.y, height01 * terrainColor.z);
            
            Vertex vert;
            vert.pos = glm::vec3(worldX, posY, worldZ);
            vert.normal = normal;
            vert.color = color;
            vert.texCoord = glm::vec2(static_cast<float>(x) / segments, static_cast<float>(z) / segments);
            vertices.push_back(vert);
        }
    }
    
    std::vector<uint32_t> indices;
    for (int z = 0; z < segments; ++z) {
        for (int x = 0; x < segments; ++x) {
            uint32_t topLeft = z * (segments + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (segments + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;
            
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    
    TerrainChunk chunk;
    chunk.chunkX = chunkX;
    chunk.chunkZ = chunkZ;
    chunk.indexCount = static_cast<uint32_t>(indices.size());
    chunk.isValid = true;
    
    VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
    VkBufferCreateInfo vbInfo{};
    vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbInfo.size = vertexBufferSize;
    vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device->getDevice(), &vbInfo, nullptr, &chunk.vertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->getDevice(), chunk.vertexBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &chunk.vertexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }
    
    vkBindBufferMemory(device->getDevice(), chunk.vertexBuffer, chunk.vertexBufferMemory, 0);
    
    void* data;
    vkMapMemory(device->getDevice(), chunk.vertexBufferMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t) vertexBufferSize);
    vkUnmapMemory(device->getDevice(), chunk.vertexBufferMemory);
    
    VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
    VkBufferCreateInfo ibInfo{};
    ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ibInfo.size = indexBufferSize;
    ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device->getDevice(), &ibInfo, nullptr, &chunk.indexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer!");
    }
    
    vkGetBufferMemoryRequirements(device->getDevice(), chunk.indexBuffer, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &chunk.indexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate index buffer memory!");
    }
    
    vkBindBufferMemory(device->getDevice(), chunk.indexBuffer, chunk.indexBufferMemory, 0);
    
    vkMapMemory(device->getDevice(), chunk.indexBufferMemory, 0, indexBufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t) indexBufferSize);
    vkUnmapMemory(device->getDevice(), chunk.indexBufferMemory);
    
    chunks[key] = chunk;
}

void TerrainRenderer::cleanupChunk(TerrainChunk& chunk) {
    if (chunk.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->getDevice(), chunk.indexBuffer, nullptr);
    }
    if (chunk.indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->getDevice(), chunk.indexBufferMemory, nullptr);
    }
    if (chunk.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->getDevice(), chunk.vertexBuffer, nullptr);
    }
    if (chunk.vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->getDevice(), chunk.vertexBufferMemory, nullptr);
    }
}

void TerrainRenderer::update(const glm::vec3& playerPos) {
    int playerChunkX = static_cast<int>(std::floor(playerPos.x / chunkSize));
    int playerChunkZ = static_cast<int>(std::floor(playerPos.z / chunkSize));
    
    for (int dz = -renderRadius; dz <= renderRadius; ++dz) {
        for (int dx = -renderRadius; dx <= renderRadius; ++dx) {
            int chunkX = playerChunkX + dx;
            int chunkZ = playerChunkZ + dz;
            
            float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (dist <= renderRadius) {
                generateChunk(chunkX, chunkZ);
            }
        }
    }
    
    std::vector<ChunkKey> toRemove;
    for (const auto& pair : chunks) {
        int dx = pair.first.x - playerChunkX;
        int dz = pair.first.z - playerChunkZ;
        float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
        if (dist > renderRadius + 2) {
            toRemove.push_back(pair.first);
        }
    }
    
    for (const auto& key : toRemove) {
        auto it = chunks.find(key);
        if (it != chunks.end()) {
            cleanupChunk(it->second);
            chunks.erase(it);
        }
    }
}

void TerrainRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    PushConstants pushConstants{};
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    pushConstants.baseColor = terrainColor;
    pushConstants.metallic = 0.0f;
    pushConstants.roughness = 1.0f;
    pushConstants.hasTexture = 0;
    pushConstants._pad0 = 0.0f;
    
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);
    
    for (const auto& pair : chunks) {
        const auto& chunk = pair.second;
        if (!chunk.isValid) continue;
        
        VkBuffer vertexBuffers[] = {chunk.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, chunk.indexCount, 1, 0, 0, 0);
    }
}

} // namespace vgame