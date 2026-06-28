#include "renderer/text_renderer.hpp"
#include "utils/logger.hpp"
#include "utils/vk_result.hpp"
#include <stdexcept>
#include <cstring>

namespace owengine {

// 简单的 16x16 数字纹理（0-9 和冒号）
// 每个数字占 16x16 像素，使用 1 位表示前景/背景
// 0-9 和冒号 (:)，共 11 个字符
static const unsigned char fontTextureData[64 * 256] = {
    // 这里我们使用一个简化的方法：创建一个白色纹理
    // 实际的字体数据将在运行时生成
};

// 生成简单的数字纹理
static void generateFontTexture(unsigned char* data, int width, int height) {
    // 清空数据
    memset(data, 0, width * height);
    
    // 为每个字符创建简单的数字图案
    // 每个字符 16x16 像素
    
    // 辅助函数：在指定位置绘制像素
    auto setPixel = [&](int x, int y, int value) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            data[y * width + x] = value;
        }
    };
    
    // 辅助函数：绘制数字
    auto drawDigit = [&](int digit, int offsetX, int offsetY) {
        // 简单的 7 段显示风格
        int segments[10][7] = {
            {1, 1, 1, 0, 1, 1, 1},  // 0
            {0, 1, 0, 0, 1, 0, 0},  // 1
            {1, 1, 0, 1, 0, 1, 1},  // 2
            {1, 1, 0, 1, 1, 0, 1},  // 3
            {0, 1, 1, 1, 1, 0, 0},  // 4
            {1, 0, 1, 1, 1, 0, 1},  // 5
            {1, 0, 1, 1, 1, 1, 1},  // 6
            {1, 1, 0, 0, 1, 0, 0},  // 7
            {1, 1, 1, 1, 1, 1, 1},  // 8
            {1, 1, 1, 1, 1, 0, 1}   // 9
        };
        
        // 水平线
        auto drawHLine = [&](int y, int x1, int x2) {
            for (int x = x1; x <= x2; x++) {
                setPixel(offsetX + x, offsetY + y, 255);
            }
        };
        
        // 垂直线
        auto drawVLine = [&](int x, int y1, int y2) {
            for (int y = y1; y <= y2; y++) {
                setPixel(offsetX + x, offsetY + y, 255);
            }
        };
        
        // 绘制 7 段
        int x0 = offsetX + 1;
        int x1 = offsetX + 7;
        int x2 = offsetX + 14;
        int y0 = offsetY + 1;
        int y1 = offsetY + 7;
        int y2 = offsetY + 8;
        int y3 = offsetY + 14;
        
        int* seg = segments[digit];
        
        if (seg[0]) drawHLine(y0, x0, x2);      // 顶部
        if (seg[1]) drawVLine(x2, y0, y1);      // 右上
        if (seg[2]) drawVLine(x2, y2, y3);      // 右下
        if (seg[3]) drawHLine(y3, x0, x2);      // 底部
        if (seg[4]) drawVLine(x0, y2, y3);      // 左下
        if (seg[5]) drawVLine(x0, y0, y1);      // 左上
        if (seg[6]) drawHLine(y1, x0, x2);      // 中间
    };
    
    // 绘制数字 0-9
    for (int i = 0; i < 10; i++) {
        drawDigit(i, i * 18 + 1, 1);
    }
    
    // 绘制冒号
    int colonX = 10 * 18 + 1;
    setPixel(colonX + 7, 5, 255);
    setPixel(colonX + 7, 10, 255);
}

TextRenderer::TextRenderer(std::shared_ptr<VulkanDevice> device)
    : vulkanDevice_(device) {
}

TextRenderer::~TextRenderer() {
    cleanup();
}

void TextRenderer::create() {
    // 创建顶点缓冲（将在渲染时动态更新）
    VkDeviceSize vertexBufferSize = 1024 * sizeof(float);
    VmaAllocationCreateInfo vbAllocInfo = {};
    vbAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    createBuffer(vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 vbAllocInfo, vertexBuffer_, vertexBufferAllocation_);
    
    // 创建索引缓冲
    std::vector<uint16_t> indices = {
        0, 1, 2, 2, 3, 0  // 两个三角形组成一个四边形
    };
    VkDeviceSize indexBufferSize = indices.size() * sizeof(uint16_t);
    
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    
    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    createBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 stagingAllocInfo, stagingBuffer, stagingAllocation);
    
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(vulkanDevice_->getAllocator(), stagingAllocation, &allocInfo);
    memcpy(allocInfo.pMappedData, indices.data(), indexBufferSize);
    
    VmaAllocationCreateInfo ibAllocInfo = {};
    ibAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 ibAllocInfo, indexBuffer_, indexBufferAllocation_);
    
    copyBuffer(stagingBuffer, indexBuffer_, indexBufferSize);
    
    vmaDestroyBuffer(vulkanDevice_->getAllocator(), stagingBuffer, stagingAllocation);
    
    // 创建字体纹理
    unsigned char textureData[TEXTURE_WIDTH * TEXTURE_HEIGHT];
    generateFontTexture(textureData, TEXTURE_WIDTH, TEXTURE_HEIGHT);
    
    VkDeviceSize imageSize = TEXTURE_WIDTH * TEXTURE_HEIGHT;
    
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 stagingAllocInfo, stagingBuffer, stagingAllocation);
    
    vmaGetAllocationInfo(vulkanDevice_->getAllocator(), stagingAllocation, &allocInfo);
    memcpy(allocInfo.pMappedData, textureData, imageSize);
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = TEXTURE_WIDTH;
    imageInfo.extent.height = TEXTURE_HEIGHT;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo imgAllocInfo = {};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    
    VkResult _vrImg = vmaCreateImage(vulkanDevice_->getAllocator(), &imageInfo, &imgAllocInfo, &textureImage_, &textureImageAllocation_, nullptr);
    if (_vrImg != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create texture image! ") + vkResultToString(_vrImg));
    }
    
    // 过渡图像布局并复制数据
    VkCommandBuffer commandBuffer = vulkanDevice_->beginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {TEXTURE_WIDTH, TEXTURE_HEIGHT, 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, textureImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vulkanDevice_->endSingleTimeCommands(commandBuffer);
    
    vmaDestroyBuffer(vulkanDevice_->getAllocator(), stagingBuffer, stagingAllocation);
    
    // 创建图像视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    VkResult _vrView = vkCreateImageView(vulkanDevice_->getDevice(), &viewInfo, nullptr, &textureImageView_);
    if (_vrView != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create texture image view! ") + vkResultToString(_vrView));
    }
    
    // 创建纹理采样器
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    
    VkResult _vrSampler = vkCreateSampler(vulkanDevice_->getDevice(), &samplerInfo, nullptr, &textureSampler_);
    if (_vrSampler != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create texture sampler! ") + vkResultToString(_vrSampler));
    }
    
    // 创建描述符集布局
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
    
    VkResult _vrDSL = vkCreateDescriptorSetLayout(vulkanDevice_->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout_);
    if (_vrDSL != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create descriptor set layout! ") + vkResultToString(_vrDSL));
    }
    
    // 创建描述符池
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    
    VkResult _vrDSP = vkCreateDescriptorPool(vulkanDevice_->getDevice(), &poolInfo, nullptr, &descriptorPool_);
    if (_vrDSP != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create descriptor pool! ") + vkResultToString(_vrDSP));
    }
    
    // 创建描述符集
    VkDescriptorSetAllocateInfo allocInfoSet{};
    allocInfoSet.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfoSet.descriptorPool = descriptorPool_;
    allocInfoSet.descriptorSetCount = 1;
    allocInfoSet.pSetLayouts = &descriptorSetLayout_;
    
    VkResult _vrDS = vkAllocateDescriptorSets(vulkanDevice_->getDevice(), &allocInfoSet, &descriptorSet_);
    if (_vrDS != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to allocate descriptor sets! ") + vkResultToString(_vrDS));
    }
    
    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = textureImageView_;
    descImageInfo.sampler = textureSampler_;
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;
    
    vkUpdateDescriptorSets(vulkanDevice_->getDevice(), 1, &descriptorWrite, 0, nullptr);
    
    Logger::info("[TextRenderer] 创建成功");
}

void TextRenderer::render(VkCommandBuffer commandBuffer, const std::string& text, float x, float y, float scale) {
    // 这是一个简化版本，实际上需要完整的渲染管线
    // 这里只是预留接口
}

void TextRenderer::cleanup() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice_->getDevice(), pipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice_->getDevice(), pipelineLayout_, nullptr);
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice_->getDevice(), descriptorPool_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice_->getDevice(), descriptorSetLayout_, nullptr);
    }
    if (textureSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanDevice_->getDevice(), textureSampler_, nullptr);
    }
    if (textureImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkanDevice_->getDevice(), textureImageView_, nullptr);
    }
    if (textureImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(vulkanDevice_->getAllocator(), textureImage_, textureImageAllocation_);
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vulkanDevice_->getAllocator(), indexBuffer_, indexBufferAllocation_);
    }
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vulkanDevice_->getAllocator(), vertexBuffer_, vertexBufferAllocation_);
    }
}

void TextRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 const VmaAllocationCreateInfo& allocInfo,
                                 VkBuffer& buffer, VmaAllocation& allocation) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult _vrBuf = vmaCreateBuffer(vulkanDevice_->getAllocator(), &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
    if (_vrBuf != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to create buffer! ") + vkResultToString(_vrBuf));
    }
}

void TextRenderer::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = vulkanDevice_->beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);
    
    vulkanDevice_->endSingleTimeCommands(commandBuffer);
}

} // namespace owengine