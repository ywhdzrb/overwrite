/**
 * @file terrain_renderer.cpp
 * @brief 地形渲染器实现文件
 * 
 * 该文件实现了基于Vulkan的地形渲染器，使用Perlin噪声和分形布朗运动(FBM)
 * 生成程序化地形。支持动态调整地形高度、平滑度和细节级别。
 * 
 * 主要功能：
 * 1. 使用Perlin噪声生成基础地形高度
 * 2. 使用FBM创建自然的地形细节
 * 3. 创建和管理Vulkan顶点/索引缓冲区
 * 4. 计算顶点法线用于光照
 * 5. 渲染地形到Vulkan命令缓冲区
 * 
 * @author 游戏引擎开发团队
 * @date 2024-2026
 */

#include "terrain_renderer.h"
#include "vulkan_device.h"
#include <glm/glm.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <random>

namespace vgame {

namespace {

int fastFloor(float v) {
    return v > 0 ? static_cast<int>(v) : static_cast<int>(v) - 1;
}

float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float grad(int h, float x, float y) {
    int hh = h & 15;
    float u = hh < 8 ? x : y;
    float v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : 0.0f);
    return ((hh & 1) == 0 ? u : -u) + ((hh & 2) == 0 ? v : -v);
}

} // anonymous namespace

/**
 * @brief 地形渲染器构造函数
 * 
 * 初始化地形渲染器的所有成员变量，包括：
 * 1. Vulkan设备指针
 * 2. 缓冲区句柄（初始化为VK_NULL_HANDLE）
 * 3. 地形参数（中心位置、尺寸、噪声缩放、高度缩放、地形颜色）
 * 4. Perlin噪声置换表（permutation table）
 * 
 * @param device 共享的Vulkan设备指针，用于创建和管理Vulkan资源
 * 
 * 地形参数默认值：
 * - centerPos: (0.0f, -1.5f, 5.0f) - 地形中心位置
 * - size: 4.0f - 单个地形图块尺寸
 * - noiseScale: 0.5f - 噪声缩放因子（控制地形细节频率）
 * - heightScale: 3.0f - 高度缩放因子（控制地形起伏幅度）
 * - terrainColor: (0.0f, 0.0f, 1.0f) - 地形颜色（默认蓝色，RGB格式）
 */
TerrainRenderer::TerrainRenderer(std::shared_ptr<VulkanDevice> device)
    : device(device),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferMemory(VK_NULL_HANDLE),
      indexCount(0),
      centerPos(0.0f, 0.0f, 5.0f),
      size(4.0f),
      noiseScale(0.05f),
      heightScale(5.0f),
      terrainColor(0.0f, 1.0f, 0.0f) { 
    // 初始化随机数生成器（固定种子42以确保可重复性）
    static std::mt19937 rng(42);
    static std::uniform_int_distribution<int> dist(0, 255);
    
    // 创建Perlin噪声置换表（permutation table）
    // 置换表大小为512，包含0-255的随机排列（重复一次）
    perm.resize(512);
    
    // 初始化前256个元素为0-255的顺序值
    for (int i = 0; i < 256; ++i) {
        perm[i] = i;
    }
    
    // Fisher-Yates洗牌算法随机打乱前256个元素
    for (int i = 255; i > 0; --i) {
        int j = dist(rng) % (i + 1);
        std::swap(perm[i], perm[j]);
    }
    
    // 复制前256个元素到后256个位置，创建循环置换表
    for (int i = 0; i < 256; ++i) {
        perm[256 + i] = perm[i];
    }
}

/**
 * @brief 地形渲染器析构函数
 * 
 * 自动调用cleanup()方法清理所有Vulkan资源，防止内存泄漏。
 * 遵循RAII原则，确保资源在对象销毁时被正确释放。
 */
TerrainRenderer::~TerrainRenderer() {
    cleanup();
}

/**
 * @brief 创建地形渲染器资源
 * 
 * 初始化地形渲染所需的所有Vulkan资源，包括：
 * 1. 顶点缓冲区（存储地形顶点数据）
 * 2. 索引缓冲区（存储地形三角形索引）
 * 
 * 该方法应在Vulkan设备初始化后调用，确保所有资源正确创建。
 * 如果资源创建失败，将抛出std::runtime_error异常。
 */
void TerrainRenderer::create() {
    createTerrainVertexBuffer();
    createTerrainIndexBuffer();
}

/**
 * @brief 清理地形渲染器资源
 * 
 * 释放所有Vulkan资源，包括顶点缓冲区和索引缓冲区。
 * 该方法应在渲染器不再需要时调用，或由析构函数自动调用。
 * 遵循RAII原则，确保资源正确释放。
 */
void TerrainRenderer::cleanup() {
    cleanupBuffers();
}

/**
 * @brief 计算2D Perlin噪声值
 * 
 * 实现经典的Perlin噪声算法，生成平滑、连续的随机值。
 * Perlin噪声广泛用于程序化内容生成，如地形、纹理等。
 * 
 * 算法步骤：
 * 1. 确定输入点所在的单位网格（grid cell）
 * 2. 计算网格四个角点的梯度值
 * 3. 计算输入点相对于每个角点的距离向量
 * 4. 计算每个角点的点积（梯度·距离向量）
 * 5. 使用fade函数进行平滑插值
 * 
 * @param x X坐标（通常为世界空间X坐标乘以noiseScale）
 * @param z Z坐标（通常为世界空间Z坐标乘以noiseScale）
 * @return 返回[-1.0, 1.0]范围内的噪声值
 * 
 * @note 使用置换表(perm)确保噪声的可重复性
 */
float TerrainRenderer::perlinNoise(float x, float z) {
    // 确定输入点所在的单位网格（0-255范围，使用& 255进行包装）
    int xi = fastFloor(x) & 255;
    int zi = fastFloor(z) & 255;
    
    // 计算输入点在单位网格内的相对位置（0.0到1.0）
    float xf = x - fastFloor(x);
    float zf = z - fastFloor(z);
    
    // 使用fade函数对相对位置进行平滑处理，减少网格边界的不连续性
    float u = fade(xf);
    float v = fade(zf);
    
    // 通过置换表获取网格四个角点的哈希值
    // 这些哈希值用于查找梯度向量
    int aa = perm[perm[xi] + zi];
    int ab = perm[perm[xi] + zi + 1];
    int ba = perm[perm[xi + 1] + zi];
    int bb = perm[perm[xi + 1] + zi + 1];
    
    // 计算四个角点的梯度点积值
    float aaGrad = grad(aa, xf, zf);
    float abGrad = grad(ab, xf, zf - 1.0f);
    float baGrad = grad(ba, xf - 1.0f, zf);
    float bbGrad = grad(bb, xf - 1.0f, zf - 1.0f);
    
    // 在X方向进行线性插值
    float n0 = lerp(aaGrad, baGrad, u);
    float n1 = lerp(abGrad, bbGrad, u);
    
    // 在Z方向进行线性插值，得到最终噪声值
    return lerp(n0, n1, v);
}

/**
 * @brief 计算分形布朗运动(FBM)值
 * 
 * 分形布朗运动通过叠加多个八度(octaves)的Perlin噪声来创建更自然的地形细节。
 * 每个八度具有不同的频率和振幅，高频八度添加细节，低频八度定义整体形状。
 * 
 * 算法特点：
 * 1. 使用多个八度的噪声叠加（通常4-8个）
 * 2. 每个后续八度的频率加倍（frequency *= 2.0f）
 * 3. 每个后续八度的振幅减半（amplitude *= 0.5f）
 * 4. 最终结果进行归一化，确保输出在合理范围内
 * 
 * @param x X坐标
 * @param z Z坐标
 * @param octaves 八度数，控制地形的细节级别和自相似性
 *               - 较少八度（2-3）：简单地形，缺乏细节
 *               - 中等八度（4-5）：自然地形，平衡细节和性能
 *               - 较多八度（6-8）：非常详细的地形，计算成本高
 * @return 返回归一化的FBM值，范围大致在[-1.0, 1.0]
 * 
 * @note 增加八度数会使地形更平滑自然，但也会增加计算成本
 */
float TerrainRenderer::fbm(float x, float z, int octaves) {
    float value = 0.0f;      // 累积的噪声值
    float amplitude = 1.0f;  // 当前八度的振幅（初始为1.0）
    float frequency = 1.0f;  // 当前八度的频率（初始为1.0）
    float maxValue = 0.0f;   // 最大可能值（用于归一化）
    
    // 叠加多个八度的噪声
    for (int i = 0; i < octaves; ++i) {
        // 计算当前八度的噪声贡献
        value += perlinNoise(x * frequency, z * frequency) * amplitude;
        maxValue += amplitude;  // 累积最大可能值
        
        // 为下一个八度调整参数
        amplitude *= 0.5f;   // 振幅减半（每个后续八度贡献减半）
        frequency *= 2.0f;   // 频率加倍（每个后续八度细节加倍）
    }
    
    // 归一化结果，确保输出在合理范围内
    return value / maxValue;
}

/**
 * @brief 获取指定位置的地形高度
 * 
 * 结合噪声缩放、FBM和高度缩放计算最终的地形高度。
 * 这是地形生成的核心函数，将程序化噪声转换为实际的高度值。
 * 
 * 计算步骤：
 * 1. 应用噪声缩放（noiseScale）调整输入坐标
 * 2. 使用FBM计算基础高度（使用4个八度）
 * 3. 应用高度缩放（heightScale）调整高度幅度
 * 4. 加上基准高度（centerPos.y）得到最终高度
 * 
 * @param x 世界空间X坐标
 * @param z 世界空间Z坐标
 * @return 返回地形在(x,z)位置的高度值（Y坐标）
 * 
 * @note 调整noiseScale和heightScale可以显著改变地形外观：
 *       - noiseScale控制地形细节频率
 *       - heightScale控制地形起伏幅度
 *       - octaves参数（当前硬编码为4）控制平滑度
 */
float TerrainRenderer::getHeight(float x, float z) {
    float gridExtent = size * gridSize / 2.0f;
    
    float baseHeight = centerPos.y;
    
    if (x >= centerPos.x - gridExtent && x <= centerPos.x + gridExtent &&
        z >= centerPos.z - gridExtent && z <= centerPos.z + gridExtent) {
        
        float noiseX = x * noiseScale;
        float noiseZ = z * noiseScale;
        
        float h = fbm(noiseX, noiseZ, 4) * heightScale;
        return baseHeight + h;
    }
    
    return baseHeight;
}

/**
 * @brief 创建顶点缓冲区
 * 
 * 生成地形网格的顶点数据并创建Vulkan顶点缓冲区。
 * 顶点数据包括位置、法线、颜色和纹理坐标。
 * 
 * 网格生成参数：
 * - gridSize: 地形网格的图块数量（16×16图块）
 * - segmentsPerTile: 每个图块的细分段数（每图块2段）
 * - totalSegments: 总细分段数（gridSize × segmentsPerTile）
 * 
 * 顶点计算步骤：
 * 1. 计算每个顶点的世界坐标
 * 2. 使用FBM计算高度值
 * 3. 通过中心差分法计算法线
 * 4. 根据高度生成颜色（绿色渐变）
 * 5. 计算纹理坐标
 * 
 * @note 网格分辨率由gridSize和segmentsPerTile控制：
 *       - 增加segmentsPerTile提高细节但增加顶点数
 *       - 当前设置生成(16×2+1)² = 1089个顶点
 */
void TerrainRenderer::createTerrainVertexBuffer() {
    // 顶点数据结构定义
    struct Vertex {
        glm::vec3 pos;      // 顶点位置 (x, y, z)
        glm::vec3 normal;   // 顶点法线 (用于光照计算)
        glm::vec3 color;    // 顶点颜色 (基于高度的绿色渐变)
        glm::vec2 texCoord; // 纹理坐标 (u, v)
};
    
    const int gridSize = 16;
    const int segmentsPerTile = 8;
    const int totalSegments = gridSize * segmentsPerTile;
    
    std::vector<Vertex> vertices;
    vertices.reserve((totalSegments + 1) * (totalSegments + 1));
    
    float startX = centerPos.x - size * gridSize / 2.0f;
    float startZ = centerPos.z - size * gridSize / 2.0f;
    
    for (int z = 0; z <= totalSegments; ++z) {
        for (int x = 0; x <= totalSegments; ++x) {
            float worldX = startX + static_cast<float>(x) / totalSegments * (size * gridSize);
            float worldZ = startZ + static_cast<float>(z) / totalSegments * (size * gridSize);
            
            float noiseX = worldX * noiseScale;
            float noiseZ = worldZ * noiseScale;
            
            float height = fbm(noiseX, noiseZ, 4) * heightScale;
            float posY = centerPos.y + height;
            
            float dNoise = 0.05f;
            float hL = fbm(noiseX - dNoise, noiseZ, 4) * heightScale;
            float hR = fbm(noiseX + dNoise, noiseZ, 4) * heightScale;
            float hD = fbm(noiseX, noiseZ - dNoise, 4) * heightScale;
            float hU = fbm(noiseX, noiseZ + dNoise, 4) * heightScale;
            
            float nx = (hL - hR) / (2.0f * dNoise * heightScale);
            float nz = (hD - hU) / (2.0f * dNoise * heightScale);
            glm::vec3 normal = glm::normalize(glm::vec3(-nx, 1.0f, -nz));
            
            float height01 = (height / heightScale + 1.0f) * 0.5f;
            height01 = glm::clamp(height01, 0.0f, 1.0f);
            glm::vec3 color(height01 * terrainColor.x, height01 * terrainColor.y, height01 * terrainColor.z);
            
            Vertex vert;
            vert.pos = glm::vec3(worldX, posY, worldZ);
            vert.normal = normal;
            vert.color = color;
            vert.texCoord = glm::vec2(static_cast<float>(x) / totalSegments,
                                   static_cast<float>(z) / totalSegments);
            vertices.push_back(vert);
        }
    }
    
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->getDevice(), vertexBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }
    
    vkBindBufferMemory(device->getDevice(), vertexBuffer, vertexBufferMemory, 0);
    
    void* data;
    vkMapMemory(device->getDevice(), vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t) bufferSize);
    vkUnmapMemory(device->getDevice(), vertexBufferMemory);
}

void TerrainRenderer::createTerrainIndexBuffer() {
    const int gridSize = 16;
    const int segmentsPerTile = 8;
    const int totalSegments = gridSize * segmentsPerTile;
    
    std::vector<uint32_t> indices;
    
    for (int z = 0; z < totalSegments; ++z) {
        for (int x = 0; x < totalSegments; ++x) {
            uint32_t topLeft = z * (totalSegments + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (totalSegments + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;
            
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    
    indexCount = static_cast<uint32_t>(indices.size());
    
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &indexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->getDevice(), indexBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &indexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate index buffer memory!");
    }
    
    vkBindBufferMemory(device->getDevice(), indexBuffer, indexBufferMemory, 0);
    
    void* data;
    vkMapMemory(device->getDevice(), indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t) bufferSize);
    vkUnmapMemory(device->getDevice(), indexBufferMemory);
}

void TerrainRenderer::cleanupBuffers() {
    if (indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->getDevice(), indexBuffer, nullptr);
    }
    if (indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->getDevice(), indexBufferMemory, nullptr);
    }
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->getDevice(), vertexBuffer, nullptr);
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->getDevice(), vertexBufferMemory, nullptr);
    }
}

void TerrainRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                          const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    PushConstants pushConstants{};
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.view = viewMatrix;
    pushConstants.proj = projectionMatrix;
    pushConstants.baseColor = terrainColor;  // 使用terrainColor参数
    pushConstants.metallic = 0.0f;
    pushConstants.roughness = 1.0f;
    pushConstants.hasTexture = 0;
    pushConstants._pad0 = 0.0f;
    
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);
    
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

} // namespace vgame
