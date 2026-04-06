#ifndef GLTF_MODEL_H
#define GLTF_MODEL_H

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
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
    // ========== 动画相关公共类型 ==========
    
    // 动画通道目标路径
    enum class AnimationPath {
        Translation,
        Rotation,
        Scale,
        Weights
    };
    
    // 动画通道
    struct AnimationChannel {
        int nodeIndex;              // 目标节点索引
        AnimationPath path;         // 目标属性
        int samplerIndex;           // 采样器索引
    };
    
    // 动画采样器
    struct AnimationSampler {
        std::vector<float> inputs;  // 时间戳
        std::vector<glm::vec4> outputs; // 输出值（vec4 可存储 translation/scale/rotation/weights）
        std::string interpolation;  // 插值方式：LINEAR, STEP, CUBICSPLINE
    };
    
    // 动画数据
    struct Animation {
        std::string name;
        std::vector<AnimationChannel> channels;
        std::vector<AnimationSampler> samplers;
        float duration = 0.0f;
    };
    
    // 动画播放状态
    struct AnimationState {
        int animationIndex = -1;    // 当前播放的动画索引
        float currentTime = 0.0f;   // 当前时间
        bool playing = false;       // 是否正在播放
        bool loop = true;           // 是否循环播放
        float speed = 1.0f;         // 播放速度
    };
    
    // 存储节点原始变换（用于动画混合）
    struct NodeTransform {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        bool hasTranslation = false;
        bool hasRotation = false;
        bool hasScale = false;
    };
    
    // 关节数据
    struct Joint {
        int nodeIndex;              // 节点索引
        glm::mat4 inverseBindMatrix; // 逆绑定矩阵
    };
    
    // 蒙皮数据
    struct Skin {
        std::string name;
        std::vector<Joint> joints;
        glm::mat4 inverseBindMatrix;
        int skeletonRoot = -1;      // 骨骼根节点
    };
    
    // ========== 构造/析构 ==========
    
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
     * @brief 设置细分迭代次数
     * @param iterations 细分迭代次数（每个三角形变成4^iterations个）
     */
    void setSubdivisionIterations(int iterations) { subdivisionIterations = iterations; }
    
    /**
     * @brief 获取细分迭代次数
     */
    int getSubdivisionIterations() const { return subdivisionIterations; }

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
    
    // ========== 动画接口 ==========
    
    /**
     * @brief 获取动画数量
     */
    size_t getAnimationCount() const { return animations.size(); }
    
    /**
     * @brief 获取动画名称
     * @param index 动画索引
     * @return 动画名称，如果索引无效返回空字符串
     */
    const std::string& getAnimationName(size_t index) const;
    
    /**
     * @brief 播放指定索引的动画
     * @param index 动画索引
     * @param loop 是否循环播放
     * @param speed 播放速度
     */
    void playAnimation(size_t index, bool loop = true, float speed = 1.0f);
    
    /**
     * @brief 播放指定名称的动画
     * @param name 动画名称
     * @param loop 是否循环播放
     * @param speed 播放速度
     * @return 是否找到并播放成功
     */
    bool playAnimation(const std::string& name, bool loop = true, float speed = 1.0f);
    
    /**
     * @brief 停止当前动画
     */
    void stopAnimation();
    
    /**
     * @brief 暂停/恢复动画
     */
    void pauseAnimation();
    void resumeAnimation();
    
    /**
     * @brief 更新动画状态
     * @param deltaTime 帧间隔时间（秒）
     */
    void updateAnimation(float deltaTime);
    
    /**
     * @brief 检查是否正在播放动画
     */
    bool isAnimationPlaying() const { return animationState.playing; }
    
    /**
     * @brief 获取当前播放的动画索引
     */
    int getCurrentAnimationIndex() const { return animationState.animationIndex; }
    
    /**
     * @brief 设置动画播放速度
     */
    void setAnimationSpeed(float speed) { animationState.speed = speed; }
    
    /**
     * @brief 设置动画是否循环
     */
    void setAnimationLoop(bool loop) { animationState.loop = loop; }
    
    /**
     * @brief 播放所有动画（用于多部件动画模型）
     * @param loop 是否循环播放
     * @param speed 播放速度
     */
    void playAllAnimations(bool loop = true, float speed = 1.0f);
    
    /**
     * @brief 检查是否在播放多个动画
     */
    bool isPlayingMultipleAnimations() const { return playAllAnimationsMode; }

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
    
    // ========== 动画内部方法 ==========
    
    /**
     * @brief 加载所有动画
     */
    void loadAnimations();
    
    /**
     * @brief 加载蒙皮数据
     */
    void loadSkins();
    
    /**
     * @brief 存储节点原始变换（用于动画）
     */
    void storeNodeTransforms();
    
    /**
     * @brief 采样动画通道
     * @param sampler 采样器
     * @param time 当前时间
     * @return 插值后的值
     */
    glm::vec4 sampleAnimationChannel(const AnimationSampler& sampler, float time) const;
    
    /**
     * @brief 应用动画到节点
     * @param animationIndex 动画索引
     * @param time 当前时间
     */
    void applyAnimation(int animationIndex, float time);
    
    /**
     * @brief 查找关键帧索引
     * @param times 时间戳数组
     * @param time 当前时间
     * @return 关键帧索引和插值因子
     */
    std::pair<size_t, float> findKeyframeIndex(const std::vector<float>& times, float time) const;
    
    /**
     * @brief 对网格进行Loop细分
     * @param vertices 顶点数组
     * @param indices 索引数组
     * @param iterations 细分迭代次数
     */
    void subdivideMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, int iterations = 1);
    
    /**
     * @brief 计算顶点法线
     * @param vertices 顶点数组
     * @param indices 索引数组
     */
    void calculateNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

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
        glm::mat4 inverseBindMatrix;  // 蒙皮用逆绑定矩阵
        int skinIndex = -1;           // 所属蒙皮索引
        int jointIndex = -1;          // 在蒙皮中的关节索引
        std::vector<size_t> children;
    };
    std::vector<NodeData> nodes;                    // 节点列表
    
    // ========== 动画系统数据 ==========
    
    std::vector<Animation> animations;              // 动画列表
    AnimationState animationState;                  // 动画播放状态
    std::vector<NodeTransform> nodeTransforms;      // 节点原始变换（动画用）
    bool playAllAnimationsMode = false;             // 是否同时播放所有动画模式
    // ========== 蒙皮系统数据 ==========
    
    std::vector<Skin> skins;                        // 蒙皮列表

    // 模型变换
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    
    // 细分参数
    int subdivisionIterations = 0;  // 细分迭代次数

    // 包围盒
    std::pair<glm::vec3, glm::vec3> boundingBox;
};

} // namespace vgame

#endif // GLTF_MODEL_H