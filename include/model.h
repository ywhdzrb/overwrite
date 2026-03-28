#ifndef MODEL_H
#define MODEL_H

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <memory>

#include "vulkan_device.h"

namespace vgame {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
};

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

} // namespace vgame

#endif // MODEL_H