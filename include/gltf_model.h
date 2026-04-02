#ifndef GLTF_MODEL_H
#define GLTF_MODEL_H

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <glm/glm.hpp>
#include "vulkan_device.h"
#include "mesh.h"
#include "texture.h"
#include "texture_loader.h"

// 包含 tinygltf 头文件
#include "tiny_gltf.h"

namespace vgame {

/**
 * @brief GLTF 模型类
 * 
 * 该类负责加载和渲染 glTF 2.0 格式的 3D 模型。
 * 支持网格、材质、纹理、动画等功能。
 */
class GLTFModel {
public:
    /**
     * @brief 构造函数
     * @param device Vulkan 设备指针
     * @param textureLoader 纹理加载器指针
     */
    GLTFModel(std::shared_ptr<VulkanDevice> device,
              std::shared_ptr<TextureLoader> textureLoader);

    /**
     * @brief 析构函数
     */
    ~GLTFModel();

    /**
     * @brief 禁止拷贝构造
     */
    GLTFModel(const GLTFModel&) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    GLTFModel& operator=(const GLTFModel&) = delete;

    /**
     * @brief 加载 glTF 模型文件
     * @param filename glTF 模型文件路径（.gltf 或 .glb）
     * @return 是否加载成功
     */
    bool loadFromFile(const std::string& filename);

    /**
     * @brief 清理模型资源
     */
    void cleanup();

    /**
     * @brief 渲染模型
     * @param commandBuffer 命令缓冲区
     * @param pipelineLayout 管线布局
     * @param viewMatrix 视图矩阵
     * @param projectionMatrix 投影矩阵
     * @param modelMatrix 模型矩阵（默认为单位矩阵）
     */
    void render(VkCommandBuffer commandBuffer,
                VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix,
                const glm::mat4& projectionMatrix,
                const glm::mat4& modelMatrix = glm::mat4(1.0f));

    /**
     * @brief 渲染特定节点
     * @param commandBuffer 命令缓冲区
     * @param pipelineLayout 管线布局
     * @param viewMatrix 视图矩阵
     * @param projectionMatrix 投影矩阵
     * @param nodeIndex 节点索引
     * @param parentMatrix 父节点的变换矩阵
     */
    void renderNode(VkCommandBuffer commandBuffer,
                   VkPipelineLayout pipelineLayout,
                   const glm::mat4& viewMatrix,
                   const glm::mat4& projectionMatrix,
                   size_t nodeIndex,
                   const glm::mat4& parentMatrix = glm::mat4(1.0f));

    /**
     * @brief 获取模型的包围盒
     * @return 包围盒的 min 和 max 坐标
     */
    std::pair<glm::vec3, glm::vec3> getBoundingBox() const { return boundingBox; }

    /**
     * @brief 获取模型名称
     */
    const std::string& getName() const { return name; }

    /**
     * @brief 获取节点数量
     */
    size_t getNodeCount() const { return nodes.size(); }

    /**
     * @brief 获取网格数量
     */
    size_t getMeshCount() const { return meshes.size(); }
    
    /**
     * @brief 获取第一个纹理（用于简单的纹理绑定）
     * @return 纹理指针，如果没有纹理则返回 nullptr
     */
    std::shared_ptr<Texture> getFirstTexture() const;

    /**
     * @brief 获取材质数量
     */
    size_t getMaterialCount() const { return materials.size(); }

    /**
     * @brief 设置模型位置
     */
    void setPosition(const glm::vec3& pos) { position = pos; }

    /**
     * @brief 获取模型位置
     */
    const glm::vec3& getPosition() const { return position; }

    /**
     * @brief 设置模型旋转（欧拉角，度数）
     */
    void setRotation(float pitch, float yaw, float roll) {
        rotation = glm::vec3(pitch, yaw, roll);
    }

    /**
     * @brief 获取模型旋转
     */
    const glm::vec3& getRotation() const { return rotation; }

    /**
     * @brief 设置模型缩放
     */
    void setScale(const glm::vec3& s) { scale = s; }

    /**
     * @brief 获取模型缩放
     */
    const glm::vec3& getScale() const { return scale; }

    /**
     * @brief 获取模型变换矩阵
     */
    glm::mat4 getModelMatrix() const;

    /**
     * @brief 计算节点变换矩阵
     * @param nodeIndex 节点索引
     * @param parentMatrix 父节点的变换矩阵
     * @return 节点的变换矩阵
     */
    glm::mat4 getNodeTransform(size_t nodeIndex, const glm::mat4& parentMatrix = glm::mat4(1.0f)) const;

private:
    /**
     * @brief 加载 glTF 模型
     * @param filename 文件路径
     * @return 是否加载成功
     */
    bool loadGLTF(const std::string& filename);

    /**
     * @brief 处理 glTF 网格
     * @param gltfMesh glTF 网格对象
     * @return 网格索引
     */
    size_t processMesh(const tinygltf::Mesh& gltfMesh);

    /**
     * @brief 处理 glTF 材质
     * @param gltfMaterial glTF 材质对象
     * @return 材质索引
     */
    size_t processMaterial(const tinygltf::Material& gltfMaterial);

    /**
     * @brief 加载纹理
     * @param textureIndex 纹理索引
     * @return 纹理指针
     */
    std::shared_ptr<Texture> loadTexture(int textureIndex);

    /**
     * @brief 访问 glTF 缓冲区数据
     * @param accessor 访问器
     * @return 数据指针
     */
    const unsigned char* getBufferData(const tinygltf::Accessor& accessor) const;

    /**
     * @brief 获取访问器的数据
     * @param accessor 访问器
     * @return 数据向量
     */
    template<typename T>
    std::vector<T> getAccessorData(const tinygltf::Accessor& accessor) const;

    /**
     * @brief 计算包围盒
     */
    void calculateBoundingBox();

    /**
     * @brief 递归计算节点包围盒
     * @param nodeIndex 节点索引
     * @param parentMatrix 父节点的变换矩阵
     */
    void calculateNodeBoundingBox(size_t nodeIndex, const glm::mat4& parentMatrix);

private:
    std::shared_ptr<VulkanDevice> device;           // Vulkan 设备
    std::shared_ptr<TextureLoader> textureLoader;   // 纹理加载器

    std::string name;                               // 模型名称
    std::string directory;                          // 模型文件所在目录

    tinygltf::Model gltfModel;                      // glTF 模型数据

    // 网格数据
    struct MeshData {
        std::unique_ptr<Mesh> mesh;
        size_t materialIndex;
        glm::vec3 minBounds;
        glm::vec3 maxBounds;
    };
    std::vector<MeshData> meshes;                   // 网格列表

    // 材质数据
    struct MaterialData {
        glm::vec3 baseColor;
        float metallic;
        float roughness;
        std::shared_ptr<Texture> baseColorTexture;
        std::shared_ptr<Texture> normalTexture;
        std::shared_ptr<Texture> metallicRoughnessTexture;
        bool useBaseColorTexture;
        bool useNormalTexture;
        bool useMetallicRoughnessTexture;
    };
    std::vector<MaterialData> materials;            // 材质列表

    // 纹理缓存
    std::map<int, std::shared_ptr<Texture>> textureCache;

    // 节点数据
    struct NodeData {
        size_t meshIndex;
        glm::mat4 transform;
        std::vector<size_t> children;
    };
    std::vector<NodeData> nodes;                    // 节点列表

    // 模型变换
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;

    // 包围盒
    std::pair<glm::vec3, glm::vec3> boundingBox;
};

} // namespace vgame

#endif // GLTF_MODEL_H