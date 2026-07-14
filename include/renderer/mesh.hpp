#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <memory>

#include "core/vulkan_device.hpp"
#include "renderer/model.hpp"

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
    /** @brief 实例化绘制：同时绑定顶点缓冲和实例缓冲，然后绘制 instanceCount 个实例 */
    void bindAndDrawInstanced(VkCommandBuffer commandBuffer, VkBuffer instanceBuffer,
                              uint32_t instanceCount, VkDeviceSize instanceBufferOffset = 0) const;
    void cleanup();

private:
    void createVertexBuffer(const std::vector<Vertex>& vertices);
    void createIndexBuffer(const std::vector<uint32_t>& indices);

    std::shared_ptr<VulkanDevice> device;
    
    VkBuffer vertexBuffer;
    VmaAllocation vertexBufferAllocation;
    uint32_t vertexCount;
    
    VkBuffer indexBuffer;
    VmaAllocation indexBufferAllocation;
    uint32_t indexCount;
};

} // namespace owengine
