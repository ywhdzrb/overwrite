// 纹理加载器实现
#include "texture_loader.h"
#include "texture.h"
#include "vulkan_device.h"
#include "logger.h"
#include <stb_image.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace owengine {

TextureLoader::TextureLoader(std::shared_ptr<VulkanDevice> device)
    : device(device) {
    
    Logger::info("TextureLoader initialized");
}

std::shared_ptr<Texture> TextureLoader::loadTexture(const std::string& filename, bool useCache) {
    // 规范化文件路径
    std::string normalizedPath = normalizePath(filename);
    
    // 检查缓存（加锁）
    if (useCache) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = textureCache.find(normalizedPath);
        if (it != textureCache.end()) {
            Logger::info("Texture loaded from cache: " + normalizedPath);
            return it->second;
        }
    }
    
    // 加载图像文件（I/O 操作，锁外执行）
    int width, height, channelCount;
    unsigned char* pixels = loadImageFile(normalizedPath, &width, &height, &channelCount, 0);
    
    if (pixels == nullptr) {
        Logger::error("Failed to load texture: " + normalizedPath);
        return nullptr;
    }
    
    // 计算图像大小
    size_t imageSize = width * height * channelCount;
    
    // 获取纹理格式
    VkFormat format = getTextureFormat(channelCount);
    
    // 计算 Mipmap 级别数
    uint32_t mipLevels = generateMipmaps ? calculateMipLevels(width, height) : 1;
    
    // 创建纹理
    auto texture = std::make_shared<Texture>(device, width, height, format, mipLevels);
    
    // 设置采样器参数
    texture->setSamplerParameters(defaultFilterMode, defaultAddressMode,
                                 defaultAnisotropyEnabled, defaultMaxAnisotropy);
    
    // 从数据创建纹理
    try {
        texture->createFromData(pixels, imageSize, channelCount);
    } catch (const std::exception& e) {
        Logger::error("Failed to create texture from data: " + std::string(e.what()));
        stbi_image_free(pixels);
        return nullptr;
    }
    
    // 释放图像数据
    stbi_image_free(pixels);
    
    // 添加到缓存（加锁）
    if (useCache) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        textureCache[normalizedPath] = texture;
    }
    
    Logger::info("Texture loaded successfully: " + normalizedPath + 
                " (" + std::to_string(width) + "x" + std::to_string(height) + 
                ", " + std::to_string(channelCount) + " channels)");
    
    return texture;
}

std::shared_ptr<Texture> TextureLoader::loadTextureFromMemory(const unsigned char* data,
                                                               size_t size,
                                                               int width,
                                                               int height,
                                                               int channelCount) {
    if (data == nullptr || size == 0) {
        Logger::error("TextureLoader: Invalid memory data");
        return nullptr;
    }
    
    // 获取纹理格式
    VkFormat format = getTextureFormat(channelCount);
    
    // 计算 Mipmap 级别数
    uint32_t mipLevels = generateMipmaps ? calculateMipLevels(width, height) : 1;
    
    // 创建纹理
    auto texture = std::make_shared<Texture>(device, width, height, format, mipLevels);
    
    // 设置采样器参数
    texture->setSamplerParameters(defaultFilterMode, defaultAddressMode,
                                 defaultAnisotropyEnabled, defaultMaxAnisotropy);
    
    // 从数据创建纹理
    try {
        texture->createFromData(data, size, channelCount);
    } catch (const std::exception& e) {
        Logger::error("Failed to create texture from memory: " + std::string(e.what()));
        return nullptr;
    }
    
    Logger::info("Texture loaded from memory successfully (" + 
                std::to_string(width) + "x" + std::to_string(height) + 
                ", " + std::to_string(channelCount) + " channels)");
    
    return texture;
}

std::shared_ptr<Texture> TextureLoader::createEmptyTexture(uint32_t width,
                                                           uint32_t height,
                                                           VkFormat format) {
    // 创建空纹理
    uint32_t mipLevels = generateMipmaps ? calculateMipLevels(width, height) : 1;
    auto texture = std::make_shared<Texture>(device, width, height, format, mipLevels);
    
    // 设置采样器参数
    texture->setSamplerParameters(defaultFilterMode, defaultAddressMode,
                                 defaultAnisotropyEnabled, defaultMaxAnisotropy);
    
    // 创建空图像数据（白色）
    int channelCount = (format == VK_FORMAT_R8G8B8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB) ? 4 : 1;
    size_t imageSize = width * height * channelCount;
    unsigned char* emptyData = new unsigned char[imageSize];
    
    // 填充白色
    for (size_t i = 0; i < imageSize; i += channelCount) {
        if (channelCount >= 3) {
            emptyData[i] = 255;     // R
            emptyData[i + 1] = 255; // G
            emptyData[i + 2] = 255; // B
        }
        if (channelCount == 4) {
            emptyData[i + 3] = 255; // A
        }
    }
    
    // 从数据创建纹理
    try {
        texture->createFromData(emptyData, imageSize, channelCount);
    } catch (const std::exception& e) {
        Logger::error("Failed to create empty texture: " + std::string(e.what()));
        delete[] emptyData;
        return nullptr;
    }
    
    delete[] emptyData;
    
    Logger::info("Empty texture created successfully (" + 
                std::to_string(width) + "x" + std::to_string(height) + ")");
    
    return texture;
}

bool TextureLoader::unloadTexture(const std::string& filename) {
    std::string normalizedPath = normalizePath(filename);
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = textureCache.find(normalizedPath);
    if (it != textureCache.end()) {
        textureCache.erase(it);
        Logger::info("Texture unloaded: " + normalizedPath);
        return true;
    }
    
    Logger::warning("Texture not found in cache: " + normalizedPath);
    return false;
}

void TextureLoader::unloadAllTextures() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    textureCache.clear();
    Logger::info("All textures unloaded");
}

void TextureLoader::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    textureCache.clear();
    Logger::info("Texture cache cleared");
}

bool TextureLoader::isTextureLoaded(const std::string& filename) const {
    std::string normalizedPath = normalizePath(filename);
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return textureCache.find(normalizedPath) != textureCache.end();
}

std::shared_ptr<Texture> TextureLoader::getTexture(const std::string& filename) const {
    std::string normalizedPath = normalizePath(filename);
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = textureCache.find(normalizedPath);
    if (it != textureCache.end()) {
        return it->second;
    }
    return nullptr;
}

void TextureLoader::setDefaultSamplerParameters(VkFilter filterMode,
                                                VkSamplerAddressMode addressMode,
                                                bool anisotropyEnabled,
                                                float maxAnisotropy) {
    defaultFilterMode = filterMode;
    defaultAddressMode = addressMode;
    defaultAnisotropyEnabled = anisotropyEnabled;
    defaultMaxAnisotropy = maxAnisotropy;
    
    Logger::info("Default sampler parameters updated");
}

unsigned char* TextureLoader::loadImageFile(const std::string& filename,
                                            int* width,
                                            int* height,
                                            int* channelCount,
                                            int desiredChannels) {
    if (width == nullptr || height == nullptr || channelCount == nullptr) {
        Logger::error("TextureLoader: Invalid output parameters");
        return nullptr;
    }
    
    // 使用 stb_image 加载图像
    stbi_set_flip_vertically_on_load(false);  // Vulkan 的纹理坐标系不需要翻转
    
    unsigned char* pixels = stbi_load(filename.c_str(), width, height, channelCount, desiredChannels);
    
    if (pixels == nullptr) {
        Logger::error("Failed to load image file: " + filename);
        return nullptr;
    }
    
    return pixels;
}

VkFormat TextureLoader::getTextureFormat(int channelCount) const {
    switch (channelCount) {
        case 1:
            return VK_FORMAT_R8_SRGB;
        case 2:
            return VK_FORMAT_R8G8_SRGB;
        case 3:
            return VK_FORMAT_R8G8B8_SRGB;
        case 4:
            return VK_FORMAT_R8G8B8A8_SRGB;
        default:
            Logger::warning("Unknown channel count: " + std::to_string(channelCount) + 
                           ", using R8G8B8A8_SRGB");
            return VK_FORMAT_R8G8B8A8_SRGB;
    }
}

uint32_t TextureLoader::calculateMipLevels(uint32_t width, uint32_t height) const {
    // 计算需要的 Mipmap 级别数
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

std::string TextureLoader::normalizePath(const std::string& path) const {
    std::string normalized = path;
    
    // 替换反斜杠为正斜杠
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    
    // 移除重复的斜杠
    size_t pos = 0;
    while ((pos = normalized.find("//", pos)) != std::string::npos) {
        normalized.replace(pos, 2, "/");
    }
    
    return normalized;
}

} // namespace owengine