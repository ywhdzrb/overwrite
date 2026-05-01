#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

#include "core/vulkan_device.h"
#include "renderer/model.h"

namespace owengine {

class Mesh {
public:
    Mesh(std::shared_ptr<VulkanDevice> device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    ~Mesh();

    // 禁止拷贝
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void bind(VkCommandBuffer commandBuffer) const;
    void draw(VkCommandBuffer commandBuffer) const;
    void cleanup();

private:
    void createVertexBuffer(const std::vector<Vertex>& vertices);
    void createIndexBuffer(const std::vector<uint32_t>& indices);

    std::shared_ptr<VulkanDevice> device;
    
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    uint32_t vertexCount;
    
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount;
};

} // namespace owengine
