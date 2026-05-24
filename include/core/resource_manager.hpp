#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <functional>

namespace owengine {

class VulkanDevice;
class TextureLoader;
class Texture;
class GLTFModel;

/**
 * @brief 模型句柄
 * 
 * 用于引用已加载的模型资源，支持引用计数。
 */
struct ModelHandle {
    std::string id;
    bool valid() const { return !id.empty(); }
    bool operator==(const ModelHandle& other) const { return id == other.id; }
};

/**
 * @brief 纹理句柄
 */
struct TextureHandle {
    std::string id;
    bool valid() const { return !id.empty(); }
    bool operator==(const TextureHandle& other) const { return id == other.id; }
};

/**
 * @brief 资源管理器
 * 
 * 统一管理所有 GPU 资源（模型、纹理、着色器等），提供：
 * - 资源加载和缓存
 * - 引用计数和自动释放
 * - 异步加载支持
 */
class ResourceManager {
public:
    /**
     * @brief 构造函数
     * @param device Vulkan 设备
     */
    explicit ResourceManager(std::shared_ptr<VulkanDevice> device);
    ~ResourceManager();
    
    // 禁止拷贝
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    
    // ========== 模型管理 ==========
    
    /**
     * @brief 加载 glTF 模型
     * @param id 模型唯一标识
     * @param filePath 模型文件路径
     * @return 模型句柄
     */
    ModelHandle loadModel(const std::string& id, const std::string& filePath);
    
    /**
     * @brief 获取模型指针
     * @param handle 模型句柄
     * @return 模型指针（nullptr 表示未找到）
     */
    GLTFModel* getModel(ModelHandle handle) const;
    
    /**
     * @brief 检查模型是否已加载
     */
    bool hasModel(const std::string& id) const;
    
    /**
     * @brief 卸载模型
     */
    void unloadModel(const std::string& id);
    
    /**
     * @brief 获取模型描述符集
     * @param handle 模型句柄
     * @return 描述符集
     */
    VkDescriptorSet getModelDescriptorSet(ModelHandle handle) const;
    
    /**
     * @brief 为模型创建描述符集
     * @param handle 模型句柄
     * @param layout 描述符集布局
     * @param pool 描述符池
     */
    void createModelDescriptorSet(ModelHandle handle, 
                                   VkDescriptorSetLayout layout,
                                   VkDescriptorPool pool);
    
    // ========== 纹理管理 ==========
    
    /**
     * @brief 加载纹理
     * @param id 纹理唯一标识
     * @param filePath 纹理文件路径
     * @return 纹理句柄
     */
    TextureHandle loadTexture(const std::string& id, const std::string& filePath);
    
    /**
     * @brief 获取纹理指针
     */
    std::shared_ptr<Texture> getTexture(TextureHandle handle) const;
    
    /**
     * @brief 检查纹理是否已加载
     */
    bool hasTexture(const std::string& id) const;
    
    // ========== 批量操作 ==========
    
    /**
     * @brief 清理所有未使用的资源
     */
    void cleanupUnused();
    
    /**
     * @brief 清理所有资源
     */
    void cleanupAll();
    
    /**
     * @brief 获取已加载模型数量
     */
    size_t getModelCount() const { return models_.size(); }
    
    /**
     * @brief 获取已加载纹理数量
     */
    size_t getTextureCount() const { return textures_.size(); }

private:
    std::shared_ptr<VulkanDevice> device_;
    std::shared_ptr<TextureLoader> textureLoader_;
    
    // 模型存储
    std::unordered_map<std::string, std::unique_ptr<GLTFModel>> models_;
    std::unordered_map<std::string, VkDescriptorSet> modelDescriptorSets_;
    
    // 纹理存储
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures_;
};

} // namespace owengine
