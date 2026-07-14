#pragma once

/**
 * @file terrain_renderer.hpp
 * @brief 多地貌地形渲染器 — 山川/河流/丘陵/平原/高原程序化生成
 *
 * 归属模块：renderer
 * 核心职责：基于多层噪声混合的过程化地形生成 + 异步区块加载/卸载管线
 * 依赖关系：VulkanDevice, ThreadPool
 *
 * 多地貌算法概述：
 *   最终高度 = 大陆基底 + 山脉脊线(域扭曲Ridged) + 丘陵起伏 + 高原削平 - 河流侵蚀 + 细节
 *   通过多层噪声按权重混合，产生自然过渡的山川/河流/丘陵/平原/高原等地貌。
 *
 * 架构设计（异步生成管线）：
 *   update() 每帧分四阶段：
 *     Phase 1: 收集中已完成的异步任务 → 主线程上传 Vulkan 缓冲
 *     Phase 2: 扫描 generationRadius 范围内缺失区块 → 去重
 *     Phase 3: 按距离排序，每帧最多启动 maxChunksPerFrame 个异步任务（线程池）
 *     Phase 4: 移除 renderRadius + 2 范围外的旧区块
 *
 * 线程安全：
 *   - perlinNoise/fbm/getHeight/computeChunkMesh 均为 const，只读 perm + 局部变量
 *   - perm 向量在构造函数中一次性初始化，之后永不修改
 *   - update() 修改 chunks/pendingChunks_ 仅在主线程
 *   - uploadChunk() 涉及 Vulkan 调用，必须在主线程
 */

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <future>
#include "utils/thread_pool.hpp"
#include "utils/logger.hpp"

namespace owengine {

class VulkanDevice;

// 地形顶点格式：pos + normal + color + texCoord（与着色器布局一致）
struct TerrainVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
};

/**
 * @brief 多地貌地形生成参数
 *
 * 控制大陆/山脉/丘陵/河流/高原等不同地貌特征的噪声参数。
 * 客户端与服务端使用完全相同的参数以保证联机一致性。
 */
/**
 * @brief 多地貌地形生成参数
 *
 * 经过调优的参数组合，产生自然过渡的山川/河流/丘陵/平原/高原等地貌。
 * 核心原则：脊线噪声不做平方锐化（避免针尖突起），山脉幅度适中，
 * 域扭曲控制在不产生畸变的范围内。
 */
struct TerrainParams {
    float continentScale = 0.001f;     // 大陆噪声频率
    float continentHeight = 12.0f;     // 大陆抬升幅度
    float seaLevel = -2.0f;            // 海平面高度
    float smoothFreq = 0.008f;         // 平滑地形噪声频率
    float roughFreq = 0.025f;          // 粗糙地形噪声频率
    float plainAmp = 8.0f;             // 平原地形起伏幅度
    float mountainAmp = 16.0f;         // 山脉附加起伏幅度
    float mountainRoughBlend = 0.6f;   // 山区粗糙噪声混合比例
    float mountainSeedFreq = 0.0024f;  // 山脉区域判定噪声频率
    float continentRawBase = 0.2f;      // 大陆噪声基底偏移（factor=(raw+base)/span）
    float continentRawSpan = 0.6f;      // 大陆噪声映射跨度
    float coastBlendStart = 0.2f;       // 海岸过渡的 continentFactor 起始
    float coastBlendEnd = 0.6f;         // 海岸过渡的 continentFactor 结束
    float oceanDepth = 5.0f;            // 海洋最大深度
    float continentBias = 0.3f;         // 大陆偏置（使中心区域为陆地）

    void applyFromConfig(const class TerrainConfig& cfg);
};

// 生物群落枚举（用于顶点着色）
enum class TerrainBiome : uint8_t {
    Ocean = 0,       // 海洋
    Beach = 1,       // 沙滩
    Plains = 2,      // 平原
    Forest = 3,      // 森林
    Hills = 4,       // 丘陵
    Mountains = 5,   // 山脉
    Snow = 6,        // 雪顶
    River = 7,       // 河流
    Plateau = 8,     // 高原
    Badlands = 9,    // 荒漠/不毛之地
};

// 每个区块的固定缓冲大小（18×18 顶点，17×17 四边形 × 2 三角形 × 3 索引）
// 多一行/列使相邻区块边界重叠 1 格，彻底消除裂缝
constexpr VkDeviceSize CHUNK_VERTEX_BUFFER_SIZE = (33 * 33) * sizeof(TerrainVertex);
constexpr VkDeviceSize CHUNK_INDEX_BUFFER_SIZE = (32 * 32 * 6) * sizeof(uint32_t);

// Vulkan 侧的已就绪区块（含 GPU 缓冲句柄，来自缓冲池）
struct TerrainChunk {
    int chunkX;
    int chunkZ;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    bool isValid = false;
    int poolSlot = -1;  // 缓冲池槽位索引，-1 表示未分配
};

// 异步计算结果：纯 CPU 网格数据（不含 Vulkan 资源）
// 从 computeChunkMesh()（后台线程）→ uploadChunk()（主线程）传递
struct ChunkMesh {
    int chunkX;
    int chunkZ;
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
};

class TerrainRenderer {
public:
    explicit TerrainRenderer(std::shared_ptr<VulkanDevice> devicePtr);
    ~TerrainRenderer();

    void create();
    void cleanup();
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    std::string getName() const { return "TerrainRenderer"; }
    bool isCreated() const { return created_; }
    
    void update(const glm::vec3& playerPos);
    float getHeight(float x, float z) const;

    /** @brief 设置地形纹理描述符集（binding=0 草地, binding=1 海底） */
    void setTexture(VkDescriptorSet descSet) { terrainTexDescSet_ = descSet; }

    /** @brief 从配置加载地形生成参数 */
    void applyConfig(const TerrainConfig& cfg) { terrainParams_.applyFromConfig(cfg); }
    
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 baseColor;
        float metallic;
        float roughness;
        int hasTexture;
        float _pad0;
        float windTime;      // 累计时间（秒），与 shader.vert/frag 布局一致
        float windStrength;  // 风场强度（地形为 0）
        glm::vec3 normalScale;  // 逆缩放因子（CPU 计算，用于法线矩阵）
    };
    static_assert(sizeof(PushConstants) == 240, "TerrainRenderer::PushConstants must be 240 bytes");

private:
    struct ChunkKey {
        int x, z;
        
        bool operator==(const ChunkKey& other) const {
            return x == other.x && z == other.z;
        }
    };
    
    struct ChunkKeyHash {
        size_t operator()(const ChunkKey& key) const {
            return std::hash<int>()(key.x * 1000 + key.z);
        }
    };

    struct PendingChunk {
        int chunkX;
        int chunkZ;
        std::future<ChunkMesh> future;  // 异步任务句柄，Phase 1 轮询就绪，Phase 2 用于去重
    };

    // ========== 噪声基础方法 ==========
    float perlinNoise(float x, float z) const;
    float fbm(float x, float z, int octaves) const;

    // ========== 地形辅助方法 ==========
    float getSlope(float x, float z) const;
    TerrainBiome getBiome(float x, float z, float height, float slope) const;
    glm::vec3 getBiomeColor(TerrainBiome biome, float height, float slope) const;

    // 纯 CPU 网格生成（无 Vulkan 调用），在 std::async 后台线程中安全执行
    ChunkMesh computeChunkMesh(int chunkX, int chunkZ) const;
    ChunkMesh generateFlatChunk(int chunkX, int chunkZ) const;
    // 将已计算的网格数据上传到 Vulkan 缓冲（从缓冲池取用，必须在主线程调用）
    void uploadChunk(const ChunkMesh& mesh);
    // 同步备用路径：直接 computeChunkMesh + uploadChunk（不经过异步管线）
    void generateChunk(int chunkX, int chunkZ);
    void cleanupChunk(TerrainChunk& chunk);

    // === 缓冲池管理 ===
    // 缓冲池槽：预分配的一组 vertex + index 缓冲区，避免运行时反复 vkCreateBuffer/vkAllocateMemory
    struct BufferPoolSlot {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
        void* vertexMappedData = nullptr;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;
        void* indexMappedData = nullptr;
        bool inUse = false;
    };
    static constexpr int BUFFER_POOL_SIZE = 640;  // 覆盖 generationRadius ≈ 531 区块 + 余量
    void initBufferPool();
    void cleanupBufferPool();
    int acquirePoolSlot();
    void releasePoolSlot(int slot);
    std::vector<BufferPoolSlot> bufferPool_;
    int nextPoolHint_ = 0;  // acquire 的起始搜索位置

    std::shared_ptr<VulkanDevice> device_;
    
    std::unordered_map<ChunkKey, TerrainChunk, ChunkKeyHash> chunks_;
    
    float chunkSize_;
    int renderRadius_;            // 渲染/卸载边界（卸载范围 = renderRadius + 2）
    int generationRadius_;        // 预生成扫描半径（renderRadius + 3，提前生成边界外区块）
    int maxChunksPerFrame_;       // 每帧异步任务上限，削去区块生成峰值
    TerrainParams terrainParams_; // 多地貌地形生成参数
    
    bool created_;
    
    std::vector<int> perm_;       // Perlin 噪声排列表，构造函数初始化后仅读不写
    
    // 异步生成队列：Phase 1 消费就绪项，Phase 2 用于去重
    std::vector<PendingChunk> pendingChunks_;

    // 玩家世界坐标，由 update() 设置，供 render() 做距离裁剪
    glm::vec3 lastPlayerPos_{0.0f};

    // 地形纹理描述符集（binding=0 草地 BaseColor, binding=1 海底砂石），绑定到 set=0 供 shader.frag 采样
    VkDescriptorSet terrainTexDescSet_ = VK_NULL_HANDLE;
    // 地形纹理世界空间平铺缩放：UV = worldPos / uvScale_
    float uvScale_ = 4.0f;

    // 固定线程池：替代 std::async，避免每帧创建销毁线程
    // 与 GrassSystem 各自独立池，互不干扰
    ThreadPool threadPool_;
};

} // namespace owengine
