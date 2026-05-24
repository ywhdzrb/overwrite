// 天空盒渲染器实现
// 使用程序化片段着色器生成天空背景，无需外部纹理
#include "renderer/skybox_renderer.hpp"
#include "core/vulkan_device.hpp"
#include <stdexcept>
#include <cstring>

namespace owengine {

SkyboxRenderer::SkyboxRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferAllocation(VK_NULL_HANDLE),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferAllocation(VK_NULL_HANDLE),
      indexCount(0),
      pipelineLayout(VK_NULL_HANDLE) {
}

SkyboxRenderer::~SkyboxRenderer() {
    cleanup();
}

void SkyboxRenderer::create() {
    createVertexBuffer();
    createIndexBuffer();

    // 创建管线布局（仅 push constants，无描述符）
    VkPushConstantRange pushConstantRange{};
    // 顶点着色器需要 view/proj，片段着色器需要 sunDir
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;           // 无描述符集布局
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

void SkyboxRenderer::cleanup() {
    cleanupBuffers();
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device->getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
}

void SkyboxRenderer::createVertexBuffer() {
    // 定义立方体顶点（天空盒立方体）
    struct Vertex {
        glm::vec3 pos;
    };

    std::vector<Vertex> vertices = {
        {{-1.0f, -1.0f, -1.0f}},
        {{ 1.0f, -1.0f, -1.0f}},
        {{ 1.0f,  1.0f, -1.0f}},
        {{-1.0f,  1.0f, -1.0f}},
        {{-1.0f, -1.0f,  1.0f}},
        {{ 1.0f, -1.0f,  1.0f}},
        {{ 1.0f,  1.0f,  1.0f}},
        {{-1.0f,  1.0f,  1.0f}},
        {{-1.0f,  1.0f, -1.0f}},
        {{ 1.0f,  1.0f, -1.0f}},
        {{ 1.0f,  1.0f,  1.0f}},
        {{-1.0f,  1.0f,  1.0f}},
        {{-1.0f, -1.0f, -1.0f}},
        {{ 1.0f, -1.0f, -1.0f}},
        {{ 1.0f, -1.0f,  1.0f}},
        {{-1.0f, -1.0f,  1.0f}},
        {{ 1.0f, -1.0f, -1.0f}},
        {{ 1.0f, -1.0f,  1.0f}},
        {{ 1.0f,  1.0f,  1.0f}},
        {{ 1.0f,  1.0f, -1.0f}},
        {{-1.0f, -1.0f, -1.0f}},
        {{-1.0f, -1.0f,  1.0f}},
        {{-1.0f,  1.0f,  1.0f}},
        {{-1.0f,  1.0f, -1.0f}},
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

    memcpy(allocInfoOut.pMappedData, vertices.data(), (size_t) bufferSize);
}

void SkyboxRenderer::createIndexBuffer() {
    // 立方体索引（每个面 2 个三角形）
    std::vector<uint32_t> indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20
    };

    indexCount = static_cast<uint32_t>(indices.size());

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

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

    memcpy(allocInfoOut.pMappedData, indices.data(), (size_t) bufferSize);
}

// 基类接口实现：使用默认太阳方向
void SkyboxRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout /*pipelineLayout*/,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    render(commandBuffer, pipelineLayout, viewMatrix, projectionMatrix,
           glm::vec3(0.25f, 0.55f, 0.50f));
}

// 重载版本：带自定义太阳方向
void SkyboxRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout /*pipelineLayout*/,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
                          const glm::vec3& sunDirection) {
    // 设置 push constants（view + projection + sunDir）
    PushConstants pushConstants{};
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    pushConstants.sunDir = glm::vec4(glm::normalize(sunDirection), 0.0f);

    vkCmdPushConstants(commandBuffer, this->pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(PushConstants), &pushConstants);

    // 绑定顶点缓冲区
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // 绑定索引缓冲区
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // 绘制天空盒
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

void SkyboxRenderer::cleanupBuffers() {
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
