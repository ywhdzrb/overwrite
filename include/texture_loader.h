#ifndef TEXTURE_LOADER_H
#define TEXTURE_LOADER_H

#include <string>
#include <memory>
#include <map>
#include "texture.h"
#include "vulkan_device.h"

namespace vgame {

/**
 * @brief 纹理加载器类
 * 
 * 该类负责从文件加载纹理，并管理纹理缓存。
 * 支持多种纹理格式，包括 PNG、JPG、BMP 等。
 */
class TextureLoader {
public:
    /**
     * @brief 构造函数
     * @param device Vulkan 设备指针
     */
    explicit TextureLoader(std::shared_ptr<VulkanDevice> device);

    /**
     * @brief 析构函数
     */
    ~TextureLoader() = default;

    /**
     * @brief 禁止拷贝构造
     */
    TextureLoader(const TextureLoader&) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    TextureLoader& operator=(const TextureLoader&) = delete;

    /**
     * @brief 加载纹理
     * @param filename 纹理文件路径
     * @param useCache 是否使用缓存（默认 true）
     * @return 纹理指针，如果加载失败则返回 nullptr
     * 
     * 支持的格式：PNG、JPG、BMP、TGA、PSD、GIF、HDR、PIC、PNM
     */
    std::shared_ptr<Texture> loadTexture(const std::string& filename, bool useCache = true);

    /**
     * @brief 从内存加载纹理
     * @param data 纹理数据指针
     * @param size 数据大小
     * @param width 纹理宽度
     * @param height 纹理高度
     * @param channelCount 颜色通道数
     * @return 纹理指针，如果加载失败则返回 nullptr
     */
    std::shared_ptr<Texture> loadTextureFromMemory(const unsigned char* data,
                                                   size_t size,
                                                   int width,
                                                   int height,
                                                   int channelCount);

    /**
     * @brief 创建空纹理
     * @param width 纹理宽度
     * @param height 纹理高度
     * @param format 纹理格式
     * @return 纹理指针
     */
    std::shared_ptr<Texture> createEmptyTexture(uint32_t width,
                                                uint32_t height,
                                                VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    /**
     * @brief 卸载纹理
     * @param filename 纹理文件路径
     * @return 是否成功卸载
     */
    bool unloadTexture(const std::string& filename);

    /**
     * @brief 卸载所有纹理
     */
    void unloadAllTextures();

    /**
     * @brief 获取缓存的纹理数量
     * @return 缓存的纹理数量
     */
    size_t getCacheSize() const { return textureCache.size(); }

    /**
     * @brief 清空缓存
     */
    void clearCache();

    /**
     * @brief 检查纹理是否已加载
     * @param filename 纹理文件路径
     * @return 是否已加载
     */
    bool isTextureLoaded(const std::string& filename) const;

    /**
     * @brief 获取纹理
     * @param filename 纹理文件路径
     * @return 纹理指针，如果不存在则返回 nullptr
     */
    std::shared_ptr<Texture> getTexture(const std::string& filename) const;

    /**
     * @brief 设置默认采样器参数
     * @param filterMode 过滤模式
     * @param addressMode 寻址模式
     * @param anisotropyEnabled 是否启用各向异性过滤
     * @param maxAnisotropy 最大各向异性级别
     */
    void setDefaultSamplerParameters(VkFilter filterMode,
                                     VkSamplerAddressMode addressMode,
                                     bool anisotropyEnabled = true,
                                     float maxAnisotropy = 16.0f);

    /**
     * @brief 启用 Mipmap 生成
     * @param enable 是否启用
     */
    void setGenerateMipmaps(bool enable) { generateMipmaps = enable; }

    /**
     * @brief 检查是否启用了 Mipmap 生成
     * @return 是否启用 Mipmap 生成
     */
    bool isGenerateMipmapsEnabled() const { return generateMipmaps; }

private:
    /**
     * @brief 使用 stb_image 加载图像文件
     * @param filename 文件路径
     * @param width 输出图像宽度
     * @param height 输出图像高度
     * @param channelCount 输出颜色通道数
     * @param desiredChannels 期望的通道数（0=自动）
     * @return 图像数据指针，需要用 stbi_image_free 释放
     */
    unsigned char* loadImageFile(const std::string& filename,
                                 int* width,
                                 int* height,
                                 int* channelCount,
                                 int desiredChannels = 0);

    /**
     * @brief 获取纹理格式
     * @param channelCount 颜色通道数
     * @return Vulkan 纹理格式
     */
    VkFormat getTextureFormat(int channelCount) const;

    /**
     * @brief 计算需要的 Mipmap 级别数
     * @param width 纹理宽度
     * @param height 纹理高度
     * @return Mipmap 级别数
     */
    uint32_t calculateMipLevels(uint32_t width, uint32_t height) const;

    /**
     * @brief 规范化文件路径
     * @param path 原始路径
     * @return 规范化后的路径
     */
    std::string normalizePath(const std::string& path) const;

private:
    std::shared_ptr<VulkanDevice> device;  // Vulkan 设备
    
    // 纹理缓存（文件路径 -> 纹理指针）
    std::map<std::string, std::shared_ptr<Texture>> textureCache;

    // 默认采样器参数
    VkFilter defaultFilterMode = VK_FILTER_LINEAR;
    VkSamplerAddressMode defaultAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    bool defaultAnisotropyEnabled = true;
    float defaultMaxAnisotropy = 16.0f;

    // Mipmap 生成选项
    bool generateMipmaps = true;
};

} // namespace vgame

#endif // TEXTURE_LOADER_H