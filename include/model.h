#ifndef MODEL_H
#define MODEL_H

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <memory>

#include "vulkan_device.h"

namespace owengine {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;

    bool operator==(const Vertex& other) const {
        return pos == other.pos && normal == other.normal && 
               color == other.color && texCoord == other.texCoord;
    }

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions();
};

} // namespace owengine

// Vertex结构体的hash函数，放在std命名空间
namespace std {
    template<>
    struct hash<owengine::Vertex> {
        size_t operator()(owengine::Vertex const& vertex) const {
            size_t h = 0;
            // 简单的hash算法
            h ^= std::hash<float>()(vertex.pos.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.pos.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.pos.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.normal.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.normal.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.normal.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.color.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.color.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.color.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.texCoord.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(vertex.texCoord.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
}

namespace owengine {

class Model {
public:
    Model(std::shared_ptr<VulkanDevice> device);
    ~Model();

    // 禁止拷贝
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    void loadFromObj(const std::string& filename);
    void cleanup();
    
    const std::vector<Vertex>& getVertices() const { return vertices; }
    const std::vector<uint32_t>& getIndices() const { return indices; }

private:
    std::shared_ptr<VulkanDevice> device;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

} // namespace owengine

#endif // MODEL_H