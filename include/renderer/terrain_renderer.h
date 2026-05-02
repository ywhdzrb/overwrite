#pragma once

// 地形渲染器 — 基于 Perlin 噪声的过程化地形
//
// 架构设计（异步生成管线）：
//   update() 每帧分四阶段：
//     Phase 1: 收集中已完成的异步任务 → 主线程上传 Vulkan 缓冲
//     Phase 2: 扫描 generationRadius 范围内缺失区块 → 去重（已存在/已排队）
//     Phase 3: 按距离排序，每帧最多启动 maxChunksPerFrame 个 std::async
//     Phase 4: 移除 renderRadius + 2 范围外的旧区块
//
//   computeChunkMesh()：纯计算（噪声采样 + 顶点/索引生成），无 Vulkan 调用，线程安全
//   uploadChunk()：    Vulkan 缓冲创建 + 数据上传，必须在主线程调用
//
// 关键常量（构造函数默认值）：
//   chunkSize=16         区块边长（世界单位）
//   renderRadius=10       渲染/碰撞检测半径（~314 区块）
//   generationRadius=13   预生成半径 = renderRadius + 3（提前生成边界外区块）
//   maxChunksPerFrame=4   每帧最多启动的异步任务数（削峰填谷）
//   noiseScale=0.08, heightScale=5.0, 3-octave FBM
//
// 线程安全（verified）：
//   - perlinNoise/fbm/getHeight/computeChunkMesh 均为 const，只读 perm + 局部变量
//   - perm 向量在构造函数中一次性初始化，之后永不修改 → 任意线程数并发读安全
//   - update() 修改 chunks/pendingChunks_ 仅在主线程，与上述 const 方法无数据重叠
//   - uploadChunk() 涉及 Vulkan 调用，必须在主线程

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <future>

namespace owengine {

class VulkanDevice;

// 地形顶点格式：pos + normal + color + texCoord（与着色器布局一致）
struct TerrainVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
};

// 每个区块的固定缓冲大小（17×17 顶点，16×16 四边形 × 2 三角形 × 3 索引）
constexpr VkDeviceSize CHUNK_VERTEX_BUFFER_SIZE = (17 * 17) * sizeof(TerrainVertex);
constexpr VkDeviceSize CHUNK_INDEX_BUFFER_SIZE = (16 * 16 * 6) * sizeof(uint32_t);

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
    explicit TerrainRenderer(std::shared_ptr<VulkanDevice> device);
    ~TerrainRenderer();

    void create();
    void cleanup();
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    std::string getName() const { return "TerrainRenderer"; }
    bool isCreated() const { return created_; }
    
    void update(const glm::vec3& playerPos);
    float getHeight(float x, float z) const;
    
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 baseColor;
        float metallic;
        float roughness;
        int hasTexture;
        float _pad0;
    };

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

    float perlinNoise(float x, float z) const;
    float fbm(float x, float z, int octaves) const;
    // 纯 CPU 网格生成（无 Vulkan 调用），在 std::async 后台线程中安全执行
    ChunkMesh computeChunkMesh(int chunkX, int chunkZ) const;
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
    static constexpr int BUFFER_POOL_SIZE = 512;  // 覆盖 renderRadius ≈ 314 区块 + 余量
    void initBufferPool();
    void cleanupBufferPool();
    int acquirePoolSlot();
    void releasePoolSlot(int slot);
    std::vector<BufferPoolSlot> bufferPool_;
    int nextPoolHint_ = 0;  // acquire 的起始搜索位置

    std::shared_ptr<VulkanDevice> device;
    
    std::unordered_map<ChunkKey, TerrainChunk, ChunkKeyHash> chunks;
    
    float chunkSize;
    int renderRadius;            // 渲染/卸载边界（卸载范围 = renderRadius + 2）
    int generationRadius;        // 预生成扫描半径（renderRadius + 3，提前生成边界外区块）
    int maxChunksPerFrame;       // 每帧异步任务上限，削去区块生成峰值
    float noiseScale;
    float heightScale;
    float baseHeight;
    glm::vec3 terrainColor;
    
    bool created_;
    
    std::vector<int> perm;       // Perlin 噪声排列表，构造函数初始化后仅读不写
    
    // 异步生成队列：Phase 1 消费就绪项，Phase 2 用于去重
    std::vector<PendingChunk> pendingChunks_;
};

} // namespace owengine
