// 立方体渲染器实现
// 渲染简单的彩色立方体
#include "cube_renderer.h"
#include "vulkan_device.h"
#include <stdexcept>
#include <cstring>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vgame {

CubeRenderer::CubeRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferMemory(VK_NULL_HANDLE),
      indexCount(0),
      position(0.0f, 0.0f, 0.0f) {
}

CubeRenderer::~CubeRenderer() {
    cleanup();
}

void CubeRenderer::create() {
    createVertexBuffer();
    createIndexBuffer();
}

void CubeRenderer::cleanup() {
    cleanupBuffers();
}

void CubeRenderer::setPosition(const glm::vec3& pos) {
    position = pos;
}

void CubeRenderer::createVertexBuffer() {
    // 定义立方体顶点（位置、颜色和纹理坐标）
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 color;
        glm::vec2 texCoord;
    };
    
    // 立方体的 8 个顶点
    // 每个面使用 4 个顶点，但为了简单起见，每个面使用独立的顶点
    // 这样可以为每个面设置不同的颜色
    // 立方体大小为 5.0（从 -2.5 到 2.5）
    // 所有面都是纯红色，便于观察
    std::vector<Vertex> vertices = {
        // 前面（红色）
        {{-2.5f, -2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 2.5f, -2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 2.5f,  2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-2.5f,  2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        
        // 后面（红色）
        {{-2.5f, -2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 2.5f, -2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 2.5f,  2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-2.5f,  2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        
        // 左面（红色）
        {{-2.5f, -2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-2.5f, -2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-2.5f,  2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-2.5f,  2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        
        // 右面（红色）
        {{ 2.5f, -2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 2.5f, -2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 2.5f,  2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 2.5f,  2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        
        // 上面（红色）
        {{-2.5f,  2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 2.5f,  2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 2.5f,  2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-2.5f,  2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        
        // 下面（红色）
        {{-2.5f, -2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 2.5f, -2.5f, -2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 2.5f, -2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-2.5f, -2.5f,  2.5f},  {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
    };
    
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    
    // 创建顶点缓冲区
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }
    
    // 分配内存
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->getDevice(), vertexBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }
    
    vkBindBufferMemory(device->getDevice(), vertexBuffer, vertexBufferMemory, 0);
    
    // 复制数据到缓冲区
    void* data;
    vkMapMemory(device->getDevice(), vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t) bufferSize);
    vkUnmapMemory(device->getDevice(), vertexBufferMemory);
}

void CubeRenderer::createIndexBuffer() {
    // 立方体索引（6 个面，每个面 2 个三角形）
    // 使用逆时针（counter-clockwise）顺序，以便正确渲染
    std::vector<uint32_t> indices = {
        // 前面（从正面看，逆时针）
        0, 3, 2,
        2, 1, 0,
        // 后面（从背面看，逆时针）
        5, 4, 7,
        7, 6, 5,
        // 左面（从左面看，逆时针）
        8, 11, 10,
        10, 9, 8,
        // 右面（从右面看，逆时针）
        13, 12, 15,
        15, 14, 13,
        // 上面（从上面看，逆时针）
        16, 19, 18,
        18, 17, 16,
        // 下面（从下面看，逆时针）
        21, 20, 23,
        23, 22, 21
    };
    
    indexCount = static_cast<uint32_t>(indices.size());
    
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    
    // 创建索引缓冲区
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &indexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer!");
    }
    
    // 分配内存
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->getDevice(), indexBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &indexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate index buffer memory!");
    }
    
    vkBindBufferMemory(device->getDevice(), indexBuffer, indexBufferMemory, 0);
    
    // 复制数据到缓冲区
    void* data;
    vkMapMemory(device->getDevice(), indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t) bufferSize);
    vkUnmapMemory(device->getDevice(), indexBufferMemory);
}

void CubeRenderer::cleanupBuffers() {
    if (indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->getDevice(), indexBuffer, nullptr);
    }
    if (indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->getDevice(), indexBufferMemory, nullptr);
    }
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->getDevice(), vertexBuffer, nullptr);
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->getDevice(), vertexBufferMemory, nullptr);
    }
}

void CubeRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    // 设置push constants
    PushConstants pushConstants{};
    
    // 创建模型矩阵（包含位置）
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    
    pushConstants.model = model;
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);
    
    // 绑定顶点缓冲区
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    
    // 绑定索引缓冲区
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    // 绘制立方体
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

} // namespace vgame