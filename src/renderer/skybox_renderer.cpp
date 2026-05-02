// 天空盒渲染器实现
// 渲染天空盒背景
#include "renderer/skybox_renderer.h"
#include "core/vulkan_device.h"
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <algorithm>

// STB Image 用于加载纹理
// 注意：STB_IMAGE_IMPLEMENTATION 由 tinygltf.cc 提供
#include "stb_image.h"

namespace owengine {

SkyboxRenderer::SkyboxRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferAllocation(VK_NULL_HANDLE),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferAllocation(VK_NULL_HANDLE),
      indexCount(0),
      cubemapImage(VK_NULL_HANDLE),
      cubemapImageAllocation(VK_NULL_HANDLE),
      cubemapImageView(VK_NULL_HANDLE),
      cubemapSampler(VK_NULL_HANDLE),
      descriptorSetLayout(VK_NULL_HANDLE),
      descriptorPool(VK_NULL_HANDLE),
      descriptorSet(VK_NULL_HANDLE),
      pipelineLayout(VK_NULL_HANDLE) {
}

SkyboxRenderer::~SkyboxRenderer() {
    cleanup();
}

void SkyboxRenderer::create() {
    createVertexBuffer();
    createIndexBuffer();
    createDescriptorSetLayout();
    createDescriptorPool();
    
    // 创建管线布局（用于 push constants）
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

void SkyboxRenderer::cleanup() {
    cleanupDescriptors();
    cleanupCubemap();
    cleanupBuffers();
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
    
    // 通过已映射指针直接复制数据
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

void SkyboxRenderer::loadCubemapFromCrossLayout(const std::string& imagePath) {
    int width, height, channels;
    unsigned char* image = stbi_load(imagePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!image) {
        throw std::runtime_error("failed to load cubemap texture: " + imagePath);
    }
    
    // 十字形布局：4列 x 3行
    // 每个面是正方形，边长 S
    // 总尺寸：4S x 3S
    // +Y(上): (S, 0) -> (2S, S)
    // -X(左): (0, S) -> (S, 2S)
    // +Z(前): (S, S) -> (2S, 2S)
    // +X(右): (2S, S) -> (3S, 2S)
    // -Z(后): (3S, S) -> (4S, 2S)
    // -Y(下): (S, 2S) -> (2S, 3S)
    
    // 计算单个面的边长
    int faceSize = height / 3; // 高度是 3S
    
    // 验证尺寸
    if (width != 4 * faceSize) {
        stbi_image_free(image);
        throw std::runtime_error("invalid cross layout dimensions: expected width to be 4x face size");
    }
    
    // Vulkan 立方体贴图顺序：+X, -X, +Y, -Y, +Z, -Z
    // 十字形布局中的面位置：
    // +X(右): (2S, S) -> (3S, 2S)
    // -X(左): (0, S) -> (S, 2S)
    // +Y(上): (S, 0) -> (2S, S)
    // -Y(下): (S, 2S) -> (2S, 3S)
    // +Z(前): (S, S) -> (2S, 2S)
    // -Z(后): (3S, S) -> (4S, 2S)
    
    struct FaceRect {
        int x, y;
    };
    
    FaceRect faceRects[6] = {
        {2 * faceSize, faceSize},      // +X (右)
        {0, faceSize},                 // -X (左)
        {faceSize, 0},                 // +Y (上)
        {faceSize, 2 * faceSize},      // -Y (下)
        {faceSize, faceSize},          // +Z (前)
        {3 * faceSize, faceSize}       // -Z (后)
    };
    
    // 提取6个面的数据
    std::vector<unsigned char*> faceData(6);
    int facePixelCount = faceSize * faceSize;
    int faceDataSize = facePixelCount * 4; // RGBA
    
    for (int i = 0; i < 6; i++) {
        faceData[i] = new unsigned char[faceDataSize];
        
        // 从十字形图片中复制对应面的数据
        for (int row = 0; row < faceSize; row++) {
            for (int col = 0; col < faceSize; col++) {
                int srcX, srcY, dstX, dstY;
                
                // Vulkan 立方体贴图坐标系映射
                // 根据标准十字形布局和 Vulkan 立方体贴图采样坐标系
                if (i == 0) { // +X (右) - 直接复制
                    srcX = faceRects[i].x + col;
                    srcY = faceRects[i].y + row;
                    dstX = col;
                    dstY = row;
                } else if (i == 1) { // -X (左) - 直接复制
                    srcX = faceRects[i].x + col;
                    srcY = faceRects[i].y + row;
                    dstX = col;
                    dstY = row;
                } else if (i == 2) { // +Y (上) - 旋转180度
                    srcX = faceRects[i].x + (faceSize - 1 - col);
                    srcY = faceRects[i].y + (faceSize - 1 - row);
                    dstX = col;
                    dstY = row;
                } else if (i == 3) { // -Y (下) - 旋转180度
                    srcX = faceRects[i].x + (faceSize - 1 - col);
                    srcY = faceRects[i].y + (faceSize - 1 - row);
                    dstX = col;
                    dstY = row;
                } else if (i == 4) { // +Z (前) - 直接复制
                    srcX = faceRects[i].x + col;
                    srcY = faceRects[i].y + row;
                    dstX = col;
                    dstY = row;
                } else if (i == 5) { // -Z (后) - 直接复制
                    srcX = faceRects[i].x + col;
                    srcY = faceRects[i].y + row;
                    dstX = col;
                    dstY = row;
                }
                
                // 计算源和目标的像素索引
                int srcIdx = (srcY * width + srcX) * 4;
                int dstIdx = (dstY * faceSize + dstX) * 4;
                
                // 复制 RGBA 数据
                faceData[i][dstIdx + 0] = image[srcIdx + 0]; // R
                faceData[i][dstIdx + 1] = image[srcIdx + 1]; // G
                faceData[i][dstIdx + 2] = image[srcIdx + 2]; // B
                faceData[i][dstIdx + 3] = image[srcIdx + 3]; // A
            }
        }
    }
    
    // 创建立方体贴图
    createCubemapImage(faceData, faceSize, faceSize);
    
    // 创建图像视图和采样器
    createCubemapImageView();
    createCubemapSampler();
    
    // 创建描述符集
    createDescriptorSet();
    
    // 释放纹理数据
    for (int i = 0; i < 6; i++) {
        delete[] faceData[i];
    }
    stbi_image_free(image);
    
    std::cout << "[SkyboxRenderer] 十字形天空盒加载成功: " << imagePath << std::endl;
}

void SkyboxRenderer::loadCubemapFromFiles(const std::vector<std::string>& facePaths) {
    if (facePaths.size() != 6) {
        throw std::runtime_error("cubemap requires exactly 6 face textures!");
    }
    
    std::vector<unsigned char*> faceData(6);
    std::vector<int> widths(6), heights(6), channels(6);
    
    // 加载 6 张纹理
    for (int i = 0; i < 6; i++) {
        faceData[i] = stbi_load(facePaths[i].c_str(), &widths[i], &heights[i], &channels[i], STBI_rgb_alpha);
        if (!faceData[i]) {
            throw std::runtime_error("failed to load cubemap texture: " + facePaths[i]);
        }
    }
    
    // 验证所有纹理大小相同
    for (int i = 1; i < 6; i++) {
        if (widths[i] != widths[0] || heights[i] != heights[0]) {
            for (int j = 0; j < 6; j++) {
                stbi_image_free(faceData[j]);
            }
            throw std::runtime_error("all cubemap faces must have the same dimensions!");
        }
    }
    
    // 创建立方体贴图
    createCubemapImage(faceData, widths[0], heights[0]);
    
    // 创建图像视图和采样器
    createCubemapImageView();
    createCubemapSampler();
    
    // 创建描述符集
    createDescriptorSet();
    
    // 释放纹理数据
    for (int i = 0; i < 6; i++) {
        stbi_image_free(faceData[i]);
    }
    
    std::cout << "[SkyboxRenderer] 立方体贴图加载成功" << std::endl;
}

void SkyboxRenderer::createCubemapImage(const std::vector<unsigned char*>& faceData, int width, int height) {
    VkDeviceSize imageSize = width * height * 4; // RGBA
    
    // 创建暂存缓冲区（VMA 托管，HOST_VISIBLE 可映射）
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize * 6; // 6 个面
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocInfoOut;
    if (vmaCreateBuffer(device->getAllocator(), &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &allocInfoOut) != VK_SUCCESS) {
        throw std::runtime_error("failed to create staging buffer!");
    }
    
    // 复制所有面的数据到暂存缓冲区
    for (int i = 0; i < 6; i++) {
        memcpy(static_cast<unsigned char*>(allocInfoOut.pMappedData) + i * imageSize, faceData[i], imageSize);
    }
    
    // 创建立方体图像（VMA 托管，DEVICE_LOCAL）
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6; // 6 个面
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // 立方体贴图标志
    
    VmaAllocationCreateInfo imageAllocInfo = {};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    
    if (vmaCreateImage(device->getAllocator(), &imageInfo, &imageAllocInfo, &cubemapImage, &cubemapImageAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create cubemap image!");
    }
    
    // 过渡图像布局并复制数据
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
    
    // 过渡到传输目标布局
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = cubemapImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // 复制每个面的数据
    for (uint32_t i = 0; i < 6; i++) {
        VkBufferImageCopy region{};
        region.bufferOffset = i * imageSize;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = i;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, cubemapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    
    // 过渡到着色器只读布局
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    device->endSingleTimeCommands(commandBuffer);
    
    // 清理暂存缓冲区及其 VMA 分配
    vmaDestroyBuffer(device->getAllocator(), stagingBuffer, stagingAllocation);
}

void SkyboxRenderer::createCubemapImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = cubemapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE; // 立方体贴图视图类型
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6; // 6 个面
    
    if (vkCreateImageView(device->getDevice(), &viewInfo, nullptr, &cubemapImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create cubemap image view!");
    }
}

void SkyboxRenderer::createCubemapSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; // 立方体贴图使用 CLAMP_TO_EDGE
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    
    if (vkCreateSampler(device->getDevice(), &samplerInfo, nullptr, &cubemapSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create cubemap sampler!");
    }
}

void SkyboxRenderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;
    
    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void SkyboxRenderer::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void SkyboxRenderer::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }
    
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = cubemapImageView;
    imageInfo.sampler = cubemapSampler;
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(device->getDevice(), 1, &descriptorWrite, 0, nullptr);
}

void SkyboxRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    // 设置 push constants
    PushConstants pushConstants{};
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    
    vkCmdPushConstants(commandBuffer, this->pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);
    
    // 绑定描述符集
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           this->pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
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

void SkyboxRenderer::cleanupCubemap() {
    if (cubemapSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device->getDevice(), cubemapSampler, nullptr);
    }
    if (cubemapImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device->getDevice(), cubemapImageView, nullptr);
    }
    if (cubemapImage != VK_NULL_HANDLE) {
        vmaDestroyImage(device->getAllocator(), cubemapImage, cubemapImageAllocation);
    }
}

void SkyboxRenderer::cleanupDescriptors() {
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device->getDevice(), pipelineLayout, nullptr);
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device->getDevice(), descriptorPool, nullptr);
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device->getDevice(), descriptorSetLayout, nullptr);
    }
}

} // namespace owengine