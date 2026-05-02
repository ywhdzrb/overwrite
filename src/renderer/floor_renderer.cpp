// 地板渲染器实现
// 渲染简单的地板平面
#include "renderer/floor_renderer.h"
#include "core/vulkan_device.h"
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace owengine {

FloorRenderer::FloorRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferAllocation(VK_NULL_HANDLE),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferAllocation(VK_NULL_HANDLE),
      indexCount(0) {
}

FloorRenderer::~FloorRenderer() {
    cleanup();
}

void FloorRenderer::create() {
    createVertexBuffer();
    createIndexBuffer();
}

void FloorRenderer::cleanup() {
    cleanupBuffers();
}

void FloorRenderer::createVertexBuffer() {
    // 定义地板顶点（一个大平面）
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec3 color;
        glm::vec2 texCoord;
    };
    
    // 地板渲染高度与碰撞高度一致 (-2.5f)
    float floorY = -2.5f;
    glm::vec3 normal(0.0f, 1.0f, 0.0f);  // 法线朝上
    glm::vec3 color(0.4f, 0.4f, 0.4f);    // 灰色
    
    std::vector<Vertex> vertices = {
        // Position             Normal              Color               TexCoord
        {{-50.0f, floorY, -50.0f}, normal, color, {0.0f, 0.0f}},
        {{ 50.0f, floorY, -50.0f}, normal, color, {50.0f, 0.0f}},
        {{ 50.0f, floorY,  50.0f}, normal, color, {50.0f, 50.0f}},
        {{-50.0f, floorY,  50.0f}, normal, color, {0.0f, 50.0f}},
    };
    
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    
    // 创建顶点缓冲区（VMA 自动管理内存）
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocInfoOut;
    if (vmaCreateBuffer(device->getAllocator(), &bufferInfo, &allocInfo, &vertexBuffer, &vertexBufferAllocation, &allocInfoOut) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }
    
    // 通过已映射指针直接复制数据
    memcpy(allocInfoOut.pMappedData, vertices.data(), (size_t) bufferSize);
}

void FloorRenderer::createIndexBuffer() {
    // 地板索引（两个三角形）
    std::vector<uint32_t> indices = {
        0, 1, 2,
        2, 3, 0
    };
    
    indexCount = static_cast<uint32_t>(indices.size());
    
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    
    // 创建索引缓冲区（VMA 自动管理内存）
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocInfoOut;
    if (vmaCreateBuffer(device->getAllocator(), &bufferInfo, &allocInfo, &indexBuffer, &indexBufferAllocation, &allocInfoOut) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer!");
    }
    
    // 通过已映射指针直接复制数据
    memcpy(allocInfoOut.pMappedData, indices.data(), (size_t) bufferSize);
}

void FloorRenderer::cleanupBuffers() {
    if (indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(device->getAllocator(), indexBuffer, indexBufferAllocation);
        indexBuffer = VK_NULL_HANDLE;
        indexBufferAllocation = VK_NULL_HANDLE;
    }
    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(device->getAllocator(), vertexBuffer, vertexBufferAllocation);
        vertexBuffer = VK_NULL_HANDLE;
        vertexBufferAllocation = VK_NULL_HANDLE;
    }
}

void FloorRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    // 设置push constants
    PushConstants pushConstants{};
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    pushConstants.baseColor = glm::vec3(1.0f);
    pushConstants.metallic = 0.0f;
    pushConstants.roughness = 1.0f;
    pushConstants.hasTexture = 0;
    pushConstants._pad0 = 0.0f;
    
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);
    
    // 绑定顶点缓冲区
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    
    // 绑定索引缓冲区
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    // 绘制地板
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

} // namespace owengine