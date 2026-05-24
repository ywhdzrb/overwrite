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

/// 石头系统配置参数（由 game_config.json 的 "stone" 段填充）
struct StoneConfig {
    float chunkSize = 16.0f;          // 区块边长（米）
    int   loadRadius = 6;             // 加载半径（区块数）
    int   maxStones = 500;            // 最大石头实例数
    float minScale = 0.3f;            // 最小缩放
    float maxScale = 1.5f;            // 最大缩放
    double density = 0.3;             // 泊松 λ，每区块平均石头数
    float renderDistance = 200.0f;    // 渲染距离（米）
    float heightThreshold = -2.0f;    // 地形高度下限
};

/**
 * @brief 石头系统
 *
 * 管理所有石头实例的生成、更新和渲染。
 * 所有石头实例共享同一个 stone.gltf 模型的几何体和纹理，
 * 每块石头只存储位置/姿态数据，避免每块石头创建独立 GPU 缓冲。
 *
 * 依赖注入：通过构造函数传入 VulkanDevice、TextureLoader、描述符集布局。
 */
class StoneSystem {
public:
    StoneSystem(std::shared_ptr<VulkanDevice> device,
                std::shared_ptr<TextureLoader> textureLoader,
                VkDescriptorSetLayout descriptorSetLayout);
    ~StoneSystem();

    // 禁止拷贝
    StoneSystem(const StoneSystem&) = delete;
    StoneSystem& operator=(const StoneSystem&) = delete;

    // 允许移动
    StoneSystem(StoneSystem&&) noexcept = default;
    StoneSystem& operator=(StoneSystem&&) noexcept = default;

    /**
     * @brief 初始化：加载 stone.gltf 模型，创建共享描述符池，预计算石头位置
     * @param cfg 石头系统配置参数（从 game_config.json 读取）
     */
    void init(const StoneConfig& cfg);

    /**
     * @brief 每帧更新：根据玩家位置动态加载/卸载石头区块
     * @param playerPos 玩家世界坐标
     * @param camera 用于视锥体剔除和距离剔除
     */
    void update(const glm::vec3& playerPos, const Camera& camera);

    /**
     * @brief 渲染所有可见石头
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

    /// 获取石头实例数量
    size_t getStoneCount() const { return stones_.size(); }

    /// 获取共享石头模型指针
    GLTFModel* getSharedModel() const { return sharedStoneModel_.get(); }

    /**
     * @brief 查询指定半径内的石头位置和缩放
     * @param x,z   世界坐标
     * @param radius 查询半径（米）
     * @return 附近石头的 (位置, 缩放) 列表
     */
    std::vector<std::pair<glm::vec3, float>> queryPositions(float x, float z, float radius) const;

    /// 石头实例的公开信息（包含槽位索引）
    struct StoneInstanceInfo {
        glm::vec3 position;
        float scale;
        int slotIndex;
    };

    /**
     * @brief 获取所有石头实例的详细信息（含槽位索引）
     */
    std::vector<StoneInstanceInfo> getStoneInstances() const;

    /**
     * @brief 设置石头实例的可见状态（采集耗尽后隐藏，重生后恢复）
     * @param slotIndex 石头槽位索引
     * @param active true=可见, false=隐藏
     */
    void setStoneActive(int slotIndex, bool active);

private:
    struct StoneChunkKey {
        int x, z;
        bool operator==(const StoneChunkKey& o) const { return x == o.x && z == o.z; }
    };
    struct StoneChunkKeyHash {
        size_t operator()(const StoneChunkKey& k) const {
            return std::hash<int>()(k.x * 1000 + k.z);
        }
    };
    struct LoadedStone {
        std::string id;
        glm::vec3 position;
        float scale = 1.0f;
        float yaw = 0.0f;
        bool active = true;      // false=采集耗尽已隐藏
    };

    void generateStonesAtStartup(const StoneConfig& cfg);
    void loadStoneChunk(const StoneConfig& cfg, const glm::vec3& playerPos);

    std::shared_ptr<VulkanDevice> device_;
    std::shared_ptr<TextureLoader> textureLoader_;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

    StoneConfig config_;
    std::unique_ptr<GLTFModel> sharedStoneModel_;
    VkDescriptorPool sharedStonePool_ = VK_NULL_HANDLE;
    std::unordered_set<StoneChunkKey, StoneChunkKeyHash> loadedChunks_;
    std::vector<LoadedStone> stones_;
    std::function<float(float, float)> heightSampler_;
};

} // namespace owengine
