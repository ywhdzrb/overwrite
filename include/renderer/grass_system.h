#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <string>
#include <random>
#include <cstddef>

namespace owengine {

class VulkanDevice;
class Camera;

/**
 * @brief 草丛系统配置参数
 *
 * 从 game_config.json 的 "grass" 段加载，所有字段有合理默认值。
 * chunkSize / loadRadius 控制草丛区块加载范围，density 控制每平米草茎密度。
 */
struct GrassConfig {
    float chunkSize = 16.0f;          // 区块边长（米）
    int   loadRadius = 5;             // 加载半径（区块数）
    int   maxBlades = 350000;         // 最大草茎实例数（显存缓冲容量）
    double density = 25.0;            // 每平米平均草茎数（泊松 λ × 区块面积）
    float renderDistance = 120.0f;    // 渲染最远距离（米）
    float bladeHeightMin = 0.25f;     // 草茎最小高度（米）
    float bladeHeightMax = 0.7f;      // 草茎最大高度（米）
    int   segmentsPerBlade = 4;       // 每根草茎分段数（3~5）
    float bladeWidth = 0.10f;         // 草茎基部宽度（米，面片草 0.10 为宜）
    float bladeThickness = 0.008f;    // 草茎基部厚度（米）
    float windStrength = 0.5f;        // 风场强度系数
    float playerRadius = 2.5f;        // 角色交互半径（米）
    float playerForce = 1.5f;         // 角色下压力度
    int   lodSegments = 2;            // LOD 分段数（远距离简化使用）
    float lodDistance = 60.0f;        // LOD 切换距离（米）
};

/**
 * @brief 单根草茎实例数据（GPU Instance Buffer 映射）
 *
 * 每个实例对应一根独立草茎，按 GrassVertex 网格实例化渲染。
 * Instanced 数据通过第二个顶点绑定传入着色器（VK_VERTEX_INPUT_RATE_INSTANCE）。
 */
struct GrassInstanceData {
    glm::vec3 position;     // 草茎根部世界坐标（地形贴合后的 Y）
    float yaw;              // 绕 Y 轴随机旋转角（弧度）
    float scale;            // 整体缩放（影响高度）
    float windSeed;         // 随机种子 [0,1]，用于着色器风场差异
    float pushState;        // 挤压弹簧状态 [0,1]，着色器角色交互恢复动画用
    float _pad0;            // 16 字节对齐填充
};

/**
 * @brief 草茎网格顶点（交叉十字面片几何体顶点）
 *
 * 每个 GrassSystem 实例包含一个静态模板网格（8 顶点十字交叉双四边形），
 * 通过实例化渲染在场景中重复绘制。
 * 顶点包含位置和纹理坐标，法线在片段着色器中计算。
 */
struct GrassVertex {
    glm::vec3 position;     // 局部坐标（根部为原点，Y 轴向上）
    glm::vec2 uv;           // 纹理坐标（用于 alpha 测试贴图采样）
};

/**
 * @brief 草丛管理器
 *
 * 负责草茎的生成、动态区块加载/卸载、LOD剔除和 Vulkan 实例化渲染。
 * 架构完全独立于 Renderer，通过依赖注入接入引擎。
 *
 * 设计要点：
 * - 草茎网格为三角棱柱分段体，非透明贴图/十字面片
 * - 全部形变/动画在顶点着色器完成（风场、角色交互弯曲）
 * - CPU 只负责位置生成和参数传递
 * - 动态区块加载：根据玩家位置实时加载/卸载草丛区块
 * - 视锥剔除 + 距离剔除 + LOD 简化三重优化
 *
 * 生命周期：init() → 每帧 update() + render() → cleanup()
 */
class GrassSystem {
public:
    explicit GrassSystem(std::shared_ptr<VulkanDevice> device);
    ~GrassSystem();

    // 禁止拷贝
    GrassSystem(const GrassSystem&) = delete;
    GrassSystem& operator=(const GrassSystem&) = delete;

    // 允许移动
    GrassSystem(GrassSystem&&) noexcept = default;
    GrassSystem& operator=(GrassSystem&&) noexcept = default;

    /**
     * @brief 初始化：生成草茎网格、创建 Vulkan 管线与缓冲区
     * @param cfg         草丛配置参数
     * @param renderPass  渲染通道（用于创建管线）
     * @param extent      交换链尺寸
     * @param msaaSamples 多重采样数
     */
    void init(const GrassConfig& cfg, VkRenderPass renderPass,
              VkExtent2D extent, VkSampleCountFlagBits msaaSamples);

    /**
     * @brief 每帧更新：根据玩家位置动态加载/卸载草丛区块，更新实例缓冲区
     * @param playerPos 玩家世界坐标
     * @param camera    摄像机（用于视锥剔除）
     * @param deltaTime 帧时间（秒）
     */
    void update(const glm::vec3& playerPos, const Camera& camera, float deltaTime);

    /**
     * @brief 渲染所有可见草茎（实例化绘制）
     * @param commandBuffer Vulkan 命令缓冲区
     * @param camera        摄像机（视图/投影矩阵）
     */
    void render(VkCommandBuffer commandBuffer, const Camera& camera);

    /**
     * @brief 设置地形高度采样器
     * @param sampler 输入 x,z → 返回地形高度 Y
     */
    void setHeightSampler(std::function<float(float x, float z)> sampler) {
        heightSampler_ = std::move(sampler);
    }

    /**
     * @brief 设置玩家世界坐标（用于着色器交互形变）
     */
    void setPlayerPosition(const glm::vec3& pos) { playerPosition_ = pos; }

    /** @brief 获取当前已加载草茎总数（所有区块，包含被遮挡的） */
    size_t getBladeCount() const { return totalLoadedBlades_; }

    /** @brief 获取当前可见草茎数（剔除后） */
    size_t getVisibleBladeCount() const { return visibleInstances_.size(); }

    /** @brief 清理所有 GPU 资源 */
    void cleanup();

private:
    // ==================== 区块键定义 ====================

    struct GrassChunkKey {
        int x, z;
        bool operator==(const GrassChunkKey& o) const {
            return x == o.x && z == o.z;
        }
    };
    struct GrassChunkKeyHash {
        size_t operator()(const GrassChunkKey& k) const {
            return std::hash<int>()(k.x * 1000 + k.z);
        }
    };

    // ==================== 草茎网格生成 ====================

    /**
     * @brief 生成单根草茎的几何体（三角棱柱分段结构）
     *
     * 设计细节：
     * - 三角棱柱截面：3 个顶点（背面顶点 + 正面两个顶点）形成扁平三角
     * - 从根部到顶部逐步收窄（97% 锥度），顶部几近闭合
     * - 法线由各面法线加权平均计算，非硬编码
     * - 自然曲率：顶部轻微前倾（quadratic curve）
     * - 6 个三角形/段：3 侧面各 2 三角
     *
     * @param segments  分段数（3~5）
     * @param vertices  输出顶点数组
     * @param indices   输出索引数组
     */
    void generateBladeMesh(int segments,
                           std::vector<GrassVertex>& vertices,
                           std::vector<uint32_t>& indices);

    // ==================== 区块管理 ====================

    /**
     * @brief 根据玩家位置更新区块加载/卸载
     *
     * - 计算玩家所在区块坐标
     * - 卸载超出 loadRadius 的远端区块
     * - 加载新进入 loadRadius 的区块
     * - 仅在玩家跨越区块边界时执行（避免每帧重复计算）
     *
     * @param playerPos 玩家世界坐标
     */
    void updateChunks(const glm::vec3& playerPos);

    /**
     * @brief 在指定区块内生成草茎，写入 chunkData_
     * @param cfg   配置
     * @param cx    区块 X 索引
     * @param cz    区块 Z 索引
     * @param rng   随机数生成器（已用区块坐标播种）
     */
    void generateGrassInChunk(const GrassConfig& cfg, int cx, int cz,
                              std::mt19937& rng);

    // ==================== 挤压弹簧状态 ====================

    /**
     * @brief 更新各草茎的挤压弹簧状态
     *
     * 玩家靠近时 pushState 快速增加至 1.0，
     * 玩家远离时指数衰减至 0.0（弹簧恢复）。
     * 状态存储在 chunkData_ 各实例中跨帧持久化。
     */
    void updatePushStates(const glm::vec3& playerPos, float deltaTime);

    // ==================== Vulkan 资源 ====================

    /** @brief 创建图形管线（实例化输入绑定） */
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                        VkSampleCountFlagBits msaaSamples);

    /** @brief 创建草茎网格的顶点/索引缓冲 */
    void createBladeBuffers();

    /** @brief 创建实例数据缓冲（动态，HOST_VISIBLE，每帧更新） */
    void createInstanceBuffer(int maxBlades);



    // ==================== 成员变量 ====================

    std::shared_ptr<VulkanDevice> device_;

    // 配置
    GrassConfig config_;
    float time_ = 0.0f;

    // 地形高度采样器
    std::function<float(float, float)> heightSampler_;

    // 玩家位置（用于着色器交互）
    glm::vec3 playerPosition_ = glm::vec3(0.0f);

    // --- 区块数据管理 ---

    // 区块数据：key=区块坐标，value=该区块所有草茎实例（持久化存储）
    std::unordered_map<GrassChunkKey, std::vector<GrassInstanceData>,
                       GrassChunkKeyHash> chunkData_;

    // 当前帧可见草茎（剔除后），直接上传 GPU
    std::vector<GrassInstanceData> visibleInstances_;

    // 已加载草茎总数（所有区块之和，用于 getBladeCount()）
    size_t totalLoadedBlades_ = 0;

    // 上一帧玩家所在区块坐标（用于检测区块边界跨越）
    int lastPlayerChunkX_ = 0;
    int lastPlayerChunkZ_ = 0;
    bool chunkPositionInitialized_ = false;

    // --- 草茎网格几何体（单根模板，共享给所有实例） ---

    std::vector<GrassVertex> bladeVertices_;
    std::vector<uint32_t> bladeIndices_;

    // --- Vulkan 资源 ---

    // 管线
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    // 草茎网格缓冲
    VkBuffer bladeVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory bladeVertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer bladeIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory bladeIndexBufferMemory_ = VK_NULL_HANDLE;

    // 实例数据缓冲（动态，HOST_VISIBLE）
    VkBuffer instanceBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory instanceBufferMemory_ = VK_NULL_HANDLE;
    int instanceBufferCapacity_ = 0;   // 当前缓冲可容纳的最大实例数

    // 状态
    bool created_ = false;
    bool initialized_ = false;
};

} // namespace owengine
