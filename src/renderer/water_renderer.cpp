// 水面渲染器实现 — 程序化波浪网格 + 半透明水面管线
#include "renderer/water_renderer.hpp"
#include "core/vulkan_device.hpp"
#include "utils/logger.hpp"
#include "utils/vk_result.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace owengine {

WaterRenderer::WaterRenderer(std::shared_ptr<VulkanDevice> device)
    : device_(std::move(device)) {}

WaterRenderer::~WaterRenderer() {
    cleanup();
}

void WaterRenderer::init(VkRenderPass renderPass, VkExtent2D extent,
                         VkSampleCountFlagBits msaaSamples, float seaLevel) {
    if (initialized_) return;
    seaLevel_ = seaLevel;

    createGrid();
    createPipeline(renderPass, extent, msaaSamples);

    Logger::info("[WaterRenderer] 初始化完成，海平面高度=" +
                 std::to_string(seaLevel_) + " 网格=" +
                 std::to_string(GRID_SEGMENTS) + "x" +
                 std::to_string(GRID_SEGMENTS));
    initialized_ = true;
}

void WaterRenderer::cleanup() {
    if (!initialized_) return;

    VkDevice dev = device_->getDevice();
    VmaAllocator allocator = device_->getAllocator();

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indexBuffer_, indexBufferAllocation_);
        indexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, vertexBuffer_, vertexBufferAllocation_);
        vertexBuffer_ = VK_NULL_HANDLE;
    }

    initialized_ = false;
}

void WaterRenderer::createGrid() {
    const int vertsPerEdge = GRID_SEGMENTS + 1;
    const float cellSize = GRID_SIZE / static_cast<float>(GRID_SEGMENTS);
    const float halfSize = GRID_SIZE * 0.5f;

    // 生成顶点
    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(vertsPerEdge) * vertsPerEdge * 8);

    for (int z = 0; z < vertsPerEdge; ++z) {
        for (int x = 0; x < vertsPerEdge; ++x) {
            float wx = -halfSize + static_cast<float>(x) * cellSize;
            float wz = -halfSize + static_cast<float>(z) * cellSize;

            // pos (3) + normal (3) + color (3) + texcoord (2) = 11 floats
            // 保持与 TerrainVertex 格式兼容：pos + normal + color + texCoord
            vertices.push_back(wx);
            vertices.push_back(0.0f);  // 海平面高度，由顶点着色器波浪位移
            vertices.push_back(wz);

            // 法线（朝上，顶点着色器会扰动）
            vertices.push_back(0.0f);
            vertices.push_back(1.0f);
            vertices.push_back(0.0f);

            // 颜色（未使用，白色）
            vertices.push_back(1.0f);
            vertices.push_back(1.0f);
            vertices.push_back(1.0f);

            // 纹理坐标（世界空间位置）
            vertices.push_back(wx * 0.1f);
            vertices.push_back(wz * 0.1f);
        }
    }

    // 生成索引
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(GRID_SEGMENTS) * GRID_SEGMENTS * 6);

    for (int z = 0; z < GRID_SEGMENTS; ++z) {
        for (int x = 0; x < GRID_SEGMENTS; ++x) {
            uint32_t tl = static_cast<uint32_t>(z) * vertsPerEdge + x;
            uint32_t tr = tl + 1;
            uint32_t bl = (static_cast<uint32_t>(z) + 1) * vertsPerEdge + x;
            uint32_t br = bl + 1;

            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    indexCount_ = static_cast<uint32_t>(indices.size());

    // 上传顶点缓冲
    VkDeviceSize vertexBufferSize = vertices.size() * sizeof(float);
    VkBufferCreateInfo vbInfo{};
    vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbInfo.size = vertexBufferSize;
    vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocOut;
    VkResult _vrVB = vmaCreateBuffer(device_->getAllocator(), &vbInfo, &allocInfo,
                                     &vertexBuffer_, &vertexBufferAllocation_, &allocOut);
    if (_vrVB != VK_SUCCESS) {
        throw std::runtime_error("[WaterRenderer] 创建顶点缓冲失败: " +
                                 vkResultToString(_vrVB));
    }
    memcpy(allocOut.pMappedData, vertices.data(), static_cast<size_t>(vertexBufferSize));

    // 上传索引缓冲
    VkDeviceSize indexBufferSize = indices.size() * sizeof(uint32_t);
    VkBufferCreateInfo ibInfo{};
    ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ibInfo.size = indexBufferSize;
    ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult _vrIB = vmaCreateBuffer(device_->getAllocator(), &ibInfo, &allocInfo,
                                     &indexBuffer_, &indexBufferAllocation_, &allocOut);
    if (_vrIB != VK_SUCCESS) {
        throw std::runtime_error("[WaterRenderer] 创建索引缓冲失败: " +
                                 vkResultToString(_vrIB));
    }
    memcpy(allocOut.pMappedData, indices.data(), static_cast<size_t>(indexBufferSize));

    Logger::info("[WaterRenderer] 网格创建完成: " +
                 std::to_string(vertices.size() / 11) + " 顶点, " +
                 std::to_string(indices.size() / 3) + " 三角形");
}

void WaterRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                   VkSampleCountFlagBits msaaSamples) {
    VkDevice dev = device_->getDevice();

    // 管线布局（仅 push constants）
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plCi{};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.pushConstantRangeCount = 1;
    plCi.pPushConstantRanges = &pcRange;
    plCi.setLayoutCount = 0;

    VkResult _vrPL = vkCreatePipelineLayout(dev, &plCi, nullptr, &pipelineLayout_);
    if (_vrPL != VK_SUCCESS) {
        throw std::runtime_error("[WaterRenderer] 创建管线布局失败: " +
                                 vkResultToString(_vrPL));
    }

    // 着色器模块
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;

    auto loadShader = [&](const std::string& path) -> VkShaderModule {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("[WaterRenderer] 无法打开着色器: " + path);
        }
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(size));

        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = size;
        ci.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

        VkShaderModule module;
        VkResult _vr = vkCreateShaderModule(dev, &ci, nullptr, &module);
        if (_vr != VK_SUCCESS) {
            throw std::runtime_error("[WaterRenderer] 创建着色器模块失败: " +
                                     vkResultToString(_vr));
        }
        return module;
    };

    vertModule = loadShader(AssetPaths::SHADER_DIR + std::string("water.vert.spv"));
    fragModule = loadShader(AssetPaths::SHADER_DIR + std::string("water.frag.spv"));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // 顶点输入（与 TerrainVertex 格式兼容）
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 11 * sizeof(float);  // pos(3) + normal(3) + color(3) + texCoord(2)
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0].location = 0;  // position
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;

    attrs[1].location = 1;  // normal
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = 3 * sizeof(float);

    attrs[2].location = 2;  // color
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset = 6 * sizeof(float);

    attrs[3].location = 3;  // texCoord
    attrs[3].binding = 0;
    attrs[3].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[3].offset = 9 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = extent;

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = &vp;
    vpState.scissorCount = 1;
    vpState.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = msaaSamples;

    // 深度测试：只测试不写入，确保半透明水面不影响 depth buffer，使海底地形可见
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    // Alpha 混合（透明水面）
    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_TRUE;
    cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.colorBlendOp = VK_BLEND_OP_ADD;
    cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cbAtt.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vpState;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.layout = pipelineLayout_;
    pi.renderPass = renderPass;
    pi.subpass = 0;

    VkResult _vrGP = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pi, nullptr,
                                                &pipeline_);
    if (_vrGP != VK_SUCCESS) {
        // 清理临时着色器模块
        vkDestroyShaderModule(dev, fragModule, nullptr);
        vkDestroyShaderModule(dev, vertModule, nullptr);
        throw std::runtime_error("[WaterRenderer] 创建管线失败: " +
                                 vkResultToString(_vrGP));
    }

    // 清理临时着色器模块
    vkDestroyShaderModule(dev, fragModule, nullptr);
    vkDestroyShaderModule(dev, vertModule, nullptr);

    Logger::info("[WaterRenderer] 管线创建完成");
}

void WaterRenderer::update(float deltaTime, const glm::vec3& sunDirection,
                           float sunIntensity) {
    time_ += deltaTime;
    sunDirection_ = sunDirection;
    sunIntensity_ = sunIntensity;
}

void WaterRenderer::render(VkCommandBuffer commandBuffer,
                           const glm::mat4& viewMatrix,
                           const glm::mat4& projectionMatrix,
                           const glm::vec3& cameraPos) {
    if (!initialized_) return;

    // 网格跟随相机居中
    glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                     glm::vec3(cameraPos.x, seaLevel_, cameraPos.z));

    PushConstants pc{};
    pc.model = model;
    pc.view = viewMatrix;
    pc.proj = projectionMatrix;
    pc.color = glm::vec4(waterColor_, waterAlpha_);
    pc.waveParams = glm::vec4(waveAmp_, waveFreq_, waveSpeed_, time_);
    pc.sunDir_intensity = glm::vec4(sunDirection_, sunIntensity_);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    VkBuffer vertexBuffers[] = {vertexBuffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
}

void WaterRenderer::rebuildPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                    VkSampleCountFlagBits msaaSamples) {
    VkDevice dev = device_->getDevice();
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    // 管线布局不变（纯 push constants）
    createPipeline(renderPass, extent, msaaSamples);
}

} // namespace owengine
