// 纹理类实现
#include "renderer/texture.h"
#include "core/vulkan_device.h"
#include "core/vulkan_command_buffer.h"
#include "utils/logger.h"
#include <stdexcept>
#include <cstring>
#include <cmath>

namespace owengine {

Texture::Texture(std::shared_ptr<VulkanDevice> device, 
                 uint32_t width, 
                 uint32_t height, 
                 VkFormat format,
                 uint32_t mipLevels)
    : device(device),
      width(width),
      height(height),
      mipLevels(mipLevels),
      format(format),
      textureImage(VK_NULL_HANDLE),
      textureImageAllocation(VK_NULL_HANDLE),
      textureImageView(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      filterMode(VK_FILTER_LINEAR),
      addressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT),
      anisotropyEnabled(true),
      maxAnisotropy(16.0f) {
    
    if (mipLevels < 1) {
        this->mipLevels = 1;
    }
}

Texture::~Texture() {
    // 清理 Vulkan 资源
    if (textureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device->getDevice(), textureSampler, nullptr);
    }
    
    if (textureImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device->getDevice(), textureImageView, nullptr);
    }
    
    if (textureImage != VK_NULL_HANDLE) {
        vmaDestroyImage(device->getAllocator(), textureImage, textureImageAllocation);
    }
}

void Texture::createFromData(const unsigned char* pixels, size_t imageSize, int channelCount) {
    if (pixels == nullptr) {
        throw std::runtime_error("Texture: Null pixel data provided");
    }

    // 1. 创建临时缓冲区（host visible）
    VkDeviceSize bufferSize = imageSize;
    
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo stagingAllocInfoOut;
    if (vmaCreateBuffer(device->getAllocator(), &bufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, &stagingAllocInfoOut) != VK_SUCCESS) {
        throw std::runtime_error("Texture: Failed to create staging buffer");
    }
    
    memcpy(stagingAllocInfoOut.pMappedData, pixels, static_cast<size_t>(bufferSize));
    
    // 2. 创建纹理图像（device local）
    createImage(VK_IMAGE_TILING_OPTIMAL,
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // 3. 过渡图像布局并复制数据
    transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
    
    copyBufferToImage(stagingBuffer, bufferSize, width, height);
    
    // 4. 如果启用了 Mipmap，生成 Mipmap
    if (mipLevels > 1) {
        generateMipmaps();
    } else {
        transitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    
    // 5. 创建图像视图和采样器
    createImageView();
    createSampler();
    
    // 6. 清理 staging buffer
    vmaDestroyBuffer(device->getAllocator(), stagingBuffer, stagingAllocation);
}

void Texture::generateMipmaps() {
    // 检查图像格式是否支持线性 Blit
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(device->getPhysicalDevice(), format, &formatProperties);
    
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        Logger::warning("Texture: Texture image format does not support linear blitting");
        // 如果不支持线性 Blit，直接使用最高级别
        transitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        return;
    }
    
    // 创建命令缓冲区
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = textureImage;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;
    
    int32_t mipWidth = width;
    int32_t mipHeight = height;
    
    for (uint32_t i = 1; i < mipLevels; i++) {
        // 从 i-1 级别过渡到 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        
        vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0, nullptr,
                           0, nullptr,
                           1, &barrier);
        
        // Blit 从 i-1 到 i 级别
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        
        vkCmdBlitImage(commandBuffer,
                      textureImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      1, &blit,
                      VK_FILTER_LINEAR);
        
        // 从 i-1 级别过渡到 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0,
                           0, nullptr,
                           0, nullptr,
                           1, &barrier);
        
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }
    
    // 最后一个级别过渡到 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0,
                       0, nullptr,
                       0, nullptr,
                       1, &barrier);
    
    device->endSingleTimeCommands(commandBuffer);
}

void Texture::setSamplerParameters(VkFilter filterMode,
                                   VkSamplerAddressMode addressMode,
                                   bool anisotropyEnabled,
                                   float maxAnisotropy) {
    this->filterMode = filterMode;
    this->addressMode = addressMode;
    this->anisotropyEnabled = anisotropyEnabled;
    this->maxAnisotropy = maxAnisotropy;
    
    // 如果采样器已创建，重新创建
    if (textureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device->getDevice(), textureSampler, nullptr);
        textureSampler = VK_NULL_HANDLE;
        createSampler();
    }
}

void Texture::createImage(VkImageTiling tiling, 
                         VkImageUsageFlags usage) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    
    if (vmaCreateImage(device->getAllocator(), &imageInfo, &allocInfo, &textureImage, &textureImageAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Texture: Failed to create image");
    }
}

void Texture::createImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device->getDevice(), &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        throw std::runtime_error("Texture: Failed to create image view");
    }
}

void Texture::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filterMode;
    samplerInfo.minFilter = filterMode;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    
    // 各向异性过滤
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device->getPhysicalDevice(), &properties);
    
    if (anisotropyEnabled) {
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = std::min(maxAnisotropy, properties.limits.maxSamplerAnisotropy);
    } else {
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
    }
    
    // 边界颜色
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    
    // Mipmap 参数
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    
    if (vkCreateSampler(device->getDevice(), &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("Texture: Failed to create texture sampler");
    }
}

void Texture::transitionImageLayout(VkImageLayout newLayout,
                                    VkPipelineStageFlags srcStageMask,
                                    VkPipelineStageFlags dstStageMask) {
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    // 设置访问掩码
    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    } else {
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    
    vkCmdPipelineBarrier(commandBuffer,
                       srcStageMask,
                       dstStageMask,
                       0,
                       0, nullptr,
                       0, nullptr,
                       1, &barrier);
    
    device->endSingleTimeCommands(commandBuffer);
}

void Texture::copyBufferToImage(VkBuffer buffer, VkDeviceSize bufferSize,
                                uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(commandBuffer,
                          buffer,
                          textureImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1,
                          &region);
    
    device->endSingleTimeCommands(commandBuffer);
}

} // namespace owengine