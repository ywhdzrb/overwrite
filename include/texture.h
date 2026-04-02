#ifndef TEXTURE_H
#define TEXTURE_H

#include <vulkan/vulkan.h>
#include <string>
#include <memory>

namespace vgame {

/**
 * @brief 纹理类，封装 Vulkan 纹理资源
 * 
 * 该类负责管理 Vulkan 纹理对象的创建、使用和销毁。
 * 支持多种纹理格式，包括 RGBA8、RGB8 等。
 */
class Texture {
public:
    /**
     * @brief 构造函数
     * @param device Vulkan 设备指针
     * @param width 纹理宽度（像素）
     * @param height 纹理高度（像素）
     * @param format 纹理格式
     * @param mipLevels Mipmap 级别数
     */
    Texture(std::shared_ptr<class VulkanDevice> device, 
            uint32_t width, 
            uint32_t height, 
            VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
            uint32_t mipLevels = 1);

    /**
     * @brief 析构函数
     */
    ~Texture();

    /**
     * @brief 禁止拷贝构造
     */
    Texture(const Texture&) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    Texture& operator=(const Texture&) = delete;

    /**
     * @brief 从图像数据创建纹理
     * @param pixels 图像数据指针
     * @param imageSize 图像数据大小
     * @param channelCount 颜色通道数（3=RGB, 4=RGBA）
     */
    void createFromData(const unsigned char* pixels, size_t imageSize, int channelCount);

    /**
     * @brief 生成 Mipmap
     */
    void generateMipmaps();

    /**
     * @brief 获取纹理图像
     * @return Vulkan 图像句柄
     */
    VkImage getImage() const { return textureImage; }

    /**
     * @brief 获取纹理图像视图
     * @return Vulkan 图像视图句柄
     */
    VkImageView getImageView() const { return textureImageView; }

    /**
     * @brief 获取纹理采样器
     * @return Vulkan 采样器句柄
     */
    VkSampler getSampler() const { return textureSampler; }

    /**
     * @brief 获取纹理宽度
     * @return 纹理宽度
     */
    uint32_t getWidth() const { return width; }

    /**
     * @brief 获取纹理高度
     * @return 纹理高度
     */
    uint32_t getHeight() const { return height; }

    /**
     * @brief 获取 Mipmap 级别数
     * @return Mipmap 级别数
     */
    uint32_t getMipLevels() const { return mipLevels; }

    /**
     * @brief 获取纹理格式
     * @return 纹理格式
     */
    VkFormat getFormat() const { return format; }

    /**
     * @brief 设置采样器参数
     * @param filterMode 过滤模式（VK_FILTER_NEAREST 或 VK_FILTER_LINEAR）
     * @param addressMode 寻址模式（VK_SAMPLER_ADDRESS_MODE_*）
     * @param anisotropyEnabled 是否启用各向异性过滤
     * @param maxAnisotropy 最大各向异性级别
     */
    void setSamplerParameters(VkFilter filterMode,
                             VkSamplerAddressMode addressMode,
                             bool anisotropyEnabled = true,
                             float maxAnisotropy = 16.0f);

private:
    /**
     * @brief 创建 Vulkan 图像
     * @param tiling 图像平铺模式
     * @param usage 图像使用标志
     * @param properties 内存属性
     * @return 创建的图像
     */
    void createImage(VkImageTiling tiling, 
                    VkImageUsageFlags usage, 
                    VkMemoryPropertyFlags properties);

    /**
     * @brief 创建图像视图
     */
    void createImageView();

    /**
     * @brief 创建纹理采样器
     */
    void createSampler();

    /**
     * @brief 过渡图像布局
     * @param newLayout 新的布局
     * @param srcStageMask 源阶段掩码
     * @param dstStageMask 目标阶段掩码
     */
    void transitionImageLayout(VkImageLayout newLayout,
                              VkPipelineStageFlags srcStageMask,
                              VkPipelineStageFlags dstStageMask);

    /**
     * @brief 复制缓冲区到图像
     * @param buffer 源缓冲区
     * @param bufferSize 缓冲区大小
     * @param width 图像宽度
     * @param height 图像高度
     */
    void copyBufferToImage(VkBuffer buffer, VkDeviceSize bufferSize,
                          uint32_t width, uint32_t height);

private:
    std::shared_ptr<class VulkanDevice> device;  // Vulkan 设备
    VkImage textureImage;                         // 纹理图像
    VkDeviceMemory textureImageMemory;            // 纹理图像内存
    VkImageView textureImageView;                 // 纹理图像视图
    VkSampler textureSampler;                     // 纹理采样器

    uint32_t width;                               // 纹理宽度
    uint32_t height;                              // 纹理高度
    uint32_t mipLevels;                           // Mipmap 级别数
    VkFormat format;                              // 纹理格式

    VkFilter filterMode;                          // 过滤模式
    VkSamplerAddressMode addressMode;             // 寻址模式
    bool anisotropyEnabled;                       // 是否启用各向异性过滤
    float maxAnisotropy;                          // 最大各向异性级别
};

} // namespace vgame

#endif // TEXTURE_H