#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include "core/i_renderer.hpp"

namespace owengine {

class VulkanDevice;

/**
 * @brief 程序化天空盒渲染器
 * @note 不再加载外部纹理，使用片段着色器中的程序化渐变色生成天空
 *       生命周期与 Renderer 绑定，遵循 RAII 四步模式
 */
class SkyboxRenderer : public IRenderer {
public:
    explicit SkyboxRenderer(std::shared_ptr<VulkanDevice> device);
    ~SkyboxRenderer() override;

    // IRenderer 接口实现
    void create() override;
    void cleanup() override;

    // 渲染天空盒（纯虚基类实现，使用默认太阳方向）
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;

    // 渲染天空盒（带自定义太阳方向的重载版本）
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
                const glm::vec3& sunDirection);

    std::string getName() const override { return "SkyboxRenderer"; }
    bool isCreated() const override { return created_; }

    // PushConstants 通过 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT 传递
    // view/proj 供顶点着色器使用，sunDir 供片段着色器使用
    struct PushConstants {
        glm::mat4 view;   // 视图矩阵（顶点着色器）
        glm::mat4 proj;   // 投影矩阵（顶点着色器）
        glm::vec4 sunDir; // 太阳方向（片段着色器），w 保留
    };
    static_assert(sizeof(PushConstants) == 144, "SkyboxRenderer::PushConstants must be 144 bytes");

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }

private:
    void createVertexBuffer();
    void createIndexBuffer();
    void cleanupBuffers();

protected:
    std::shared_ptr<VulkanDevice> device;

    // 立方体网格
    VkBuffer vertexBuffer;
    VmaAllocation vertexBufferAllocation;
    VkBuffer indexBuffer;
    VmaAllocation indexBufferAllocation;
    uint32_t indexCount;

    VkPipelineLayout pipelineLayout;
};

} // namespace owengine
