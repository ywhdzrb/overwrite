// OBJ模型渲染器实现
// 用于加载和渲染OBJ格式的3D模型
#include "model_renderer.h"
#include "vulkan_device.h"
#include <stdexcept>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vgame {

ModelRenderer::ModelRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferMemory(VK_NULL_HANDLE),
      indexCount(0),
      position(0.0f, 0.0f, 0.0f),
      scale(1.0f, 1.0f, 1.0f),
      rotation(0.0f, 0.0f, 0.0f) {
    model = std::make_unique<Model>(device);
}

ModelRenderer::~ModelRenderer() {
    cleanup();
}

void ModelRenderer::create() {
    // 模型数据在loadModel中加载
}

void ModelRenderer::cleanup() {
    cleanupBuffers();
}

void ModelRenderer::loadModel(const std::string& filename) {
    // 加载OBJ文件
    model->loadFromObj(filename);

    // 创建顶点和索引缓冲区
    createVertexBuffer();
    createIndexBuffer();
}

void ModelRenderer::setPosition(const glm::vec3& pos) {
    position = pos;
}

void ModelRenderer::setScale(const glm::vec3& s) {
    scale = s;
}

void ModelRenderer::setRotation(float pitch, float yaw, float roll) {
    rotation = glm::vec3(pitch, yaw, roll);
}

void ModelRenderer::createVertexBuffer() {
    const auto& vertices = model->getVertices();
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
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device->getDevice(), vertexBufferMemory);
}

void ModelRenderer::createIndexBuffer() {
    const auto& indices = model->getIndices();
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
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device->getDevice(), indexBufferMemory);
}

void ModelRenderer::cleanupBuffers() {
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

void ModelRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    // 设置push constants
    PushConstants pushConstants{};

    // 创建模型矩阵（包含位置、旋转和缩放）
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));  // pitch
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));  // yaw
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));  // roll
    model = glm::scale(model, scale);

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

    // 绘制模型
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

} // namespace vgame