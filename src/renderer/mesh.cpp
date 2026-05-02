// 网格实现
// 负责创建和管理顶点/索引缓冲区
#include "renderer/mesh.h"
#include "core/vulkan_device.h"
#include <stdexcept>
#include <cstring>

namespace owengine {

// Mesh构造函数
// 创建网格并初始化顶点和索引缓冲区
Mesh::Mesh(std::shared_ptr<VulkanDevice> device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    : device(device), vertexCount(static_cast<uint32_t>(vertices.size())), indexCount(static_cast<uint32_t>(indices.size())) {
    
    createVertexBuffer(vertices);
    createIndexBuffer(indices);
}

// Mesh析构函数
Mesh::~Mesh() {
    cleanup();
}

// 创建顶点缓冲区
// 使用暂存缓冲区将顶点数据上传到设备本地内存（VMA 托管内存分配）
void Mesh::createVertexBuffer(const std::vector<Vertex>& vertices) {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    
    // 创建暂存缓冲区（VMA 托管，HOST_VISIBLE 可映射）
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocInfoOut;
    if (vmaCreateBuffer(device->getAllocator(), &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &allocInfoOut) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }
    
    // 复制顶点数据到暂存缓冲区
    memcpy(allocInfoOut.pMappedData, vertices.data(), (size_t) bufferSize);
    
    // 创建设备本地顶点缓冲区（VMA 托管，DEVICE_LOCAL）
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = 0;
    
    if (vmaCreateBuffer(device->getAllocator(), &bufferInfo, &allocInfo, &vertexBuffer, &vertexBufferAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }
    
    // 通过命令缓冲区将数据从暂存缓冲区复制到设备本地缓冲区
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, vertexBuffer, 1, &copyRegion);
    device->endSingleTimeCommands(commandBuffer);
    
    // 销毁暂存缓冲区及其 VMA 分配
    vmaDestroyBuffer(device->getAllocator(), stagingBuffer, stagingAllocation);
}

void Mesh::createIndexBuffer(const std::vector<uint32_t>& indices) {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    
    // 创建暂存缓冲区（VMA 托管，HOST_VISIBLE 可映射）
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocInfoOut;
    if (vmaCreateBuffer(device->getAllocator(), &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &allocInfoOut) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer!");
    }
    
    // 复制索引数据到暂存缓冲区
    memcpy(allocInfoOut.pMappedData, indices.data(), (size_t) bufferSize);
    
    // 创建设备本地索引缓冲区（VMA 托管，DEVICE_LOCAL）
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = 0;
    
    if (vmaCreateBuffer(device->getAllocator(), &bufferInfo, &allocInfo, &indexBuffer, &indexBufferAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer!");
    }
    
    // 通过命令缓冲区将数据从暂存缓冲区复制到设备本地缓冲区
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, indexBuffer, 1, &copyRegion);
    device->endSingleTimeCommands(commandBuffer);
    
    // 销毁暂存缓冲区及其 VMA 分配
    vmaDestroyBuffer(device->getAllocator(), stagingBuffer, stagingAllocation);
}

void Mesh::bind(VkCommandBuffer commandBuffer) const {
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw(VkCommandBuffer commandBuffer) const {
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

void Mesh::cleanup() {
    // 检查 device 是否有效
    if (!device) {
        return;
    }
    
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

} // namespace owengine