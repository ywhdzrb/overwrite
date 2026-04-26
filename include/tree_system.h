#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_set>
#include <glm/glm.hpp>

namespace owengine {

class VulkanDevice;
class GLTFModel;
class TextureLoader;
class Camera;

/**
 * @brief 树木系统
 *
 * 管理所有树木实例的生成、更新和渲染。
 * 所有树实例共享同一个 tree.glb 模型的几何体和纹理，
 * 每棵树只存储位置/姿态数据，避免每棵树创建独立 GPU 缓冲。
 *
 * 依赖注入：通过构造函数传入 VulkanDevice、TextureLoader、描述符集布局。
 */
class TreeSystem {
public:
    TreeSystem(std::shared_ptr<VulkanDevice> device,
               std::shared_ptr<TextureLoader> textureLoader,
               VkDescriptorSetLayout descriptorSetLayout);
    ~TreeSystem();

    // 禁止拷贝
    TreeSystem(const TreeSystem&) = delete;
    TreeSystem& operator=(const TreeSystem&) = delete;

    // 允许移动
    TreeSystem(TreeSystem&&) noexcept = default;
    TreeSystem& operator=(TreeSystem&&) noexcept = default;

    /**
     * @brief 初始化：加载 tree.glb 模型，创建共享描述符池，预计算树木位置
     */
    void init();

    /**
     * @brief 每帧更新：根据玩家位置动态加载/卸载树木区块
     * @param playerPos 玩家世界坐标
     * @param camera 用于视锥体剔除和距离剔除
     */
    void update(const glm::vec3& playerPos, const Camera& camera);

    /**
     * @brief 渲染所有可见树木
     */
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const Camera& camera) const;

    /**
     * @brief 设置地形高度采样器（注入外部高度查询委托，如 TerrainRenderer::getHeight）
     * @param sampler 输入世界坐标 x,z，返回该点地形高度 Y
     */
    void setHeightSampler(std::function<float(float x, float z)> sampler) { heightSampler_ = std::move(sampler); }

    /**
     * @brief 清理 GPU 资源
     */
    void cleanup();

    /// 获取树木实例数量
    size_t getTreeCount() const { return trees_.size(); }

    /// 获取共享树木模型指针
    GLTFModel* getSharedModel() const { return sharedTreeModel_.get(); }

private:
    struct TreeChunkKey {
        int x, z;
        bool operator==(const TreeChunkKey& o) const { return x == o.x && z == o.z; }
    };
    struct TreeChunkKeyHash {
        size_t operator()(const TreeChunkKey& k) const {
            return std::hash<int>()(k.x * 1000 + k.z);
        }
    };
    struct LoadedTree {
        std::string id;
        glm::vec3 position;
        float scale = 1.0f;
        float yaw = 0.0f;
    };

    void generateTreesAtStartup();
    void loadTreeChunk(const glm::vec3& playerPos);

    static constexpr float CHUNK_SIZE = 16.0f;
    static constexpr int LOAD_RADIUS = 6;
    static constexpr int MAX_TOTAL = 300;

    std::shared_ptr<VulkanDevice> device_;
    std::shared_ptr<TextureLoader> textureLoader_;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

    std::unique_ptr<GLTFModel> sharedTreeModel_;
    VkDescriptorPool sharedTreePool_ = VK_NULL_HANDLE;
    std::unordered_set<TreeChunkKey, TreeChunkKeyHash> loadedChunks_;
    std::vector<LoadedTree> trees_;
    std::function<float(float, float)> heightSampler_;
};

} // namespace owengine
