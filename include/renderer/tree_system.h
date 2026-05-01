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

/// 树木系统配置参数（由 game_config.json 的 "tree" 段填充）
struct TreeConfig {
    float chunkSize = 16.0f;          // 区块边长（米）
    int   loadRadius = 6;             // 加载半径（区块数）
    int   maxTotal = 300;             // 最大树木实例数
    float minScale = 0.2f;            // 最小缩放
    float maxScale = 0.5f;            // 最大缩放
    double density = 0.15;            // 泊松 λ，每区块平均树数
    float renderDistance = 250.0f;    // 渲染距离（米）
    float heightThreshold = -2.0f;    // 地形高度下限
};

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
     * @param cfg 树木系统配置参数（从 game_config.json 读取）
     */
    void init(const TreeConfig& cfg);

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

    /**
     * @brief 查询指定半径内的树木位置和缩放
     * @param x,z   世界坐标
     * @param radius 查询半径（米）
     * @return 附近树木的 (位置, 缩放) 列表
     */
    std::vector<std::pair<glm::vec3, float>> queryPositions(float x, float z, float radius) const;

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

    void generateTreesAtStartup(const TreeConfig& cfg);
    void loadTreeChunk(const TreeConfig& cfg, const glm::vec3& playerPos);

    std::shared_ptr<VulkanDevice> device_;
    std::shared_ptr<TextureLoader> textureLoader_;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

    TreeConfig config_;
    std::unique_ptr<GLTFModel> sharedTreeModel_;
    VkDescriptorPool sharedTreePool_ = VK_NULL_HANDLE;
    std::unordered_set<TreeChunkKey, TreeChunkKeyHash> loadedChunks_;
    std::vector<LoadedTree> trees_;
    std::function<float(float, float)> heightSampler_;
};

} // namespace owengine
