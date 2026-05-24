#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <functional>

namespace owengine {

/**
 * @brief 渲染器抽象接口
 * 
 * 所有具体渲染器（FloorRenderer、CubeRenderer、SkyboxRenderer、ModelRenderer 等）
 * 都应实现此接口，以支持统一的渲染管线和多态调用。
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /**
     * @brief 初始化渲染器资源
     * 
     * 创建顶点缓冲、索引缓冲、描述符集等 GPU 资源。
     * 必须在首次 render() 调用前执行。
     */
    virtual void create() = 0;

    /**
     * @brief 清理渲染器资源
     * 
     * 释放所有 GPU 资源。应与 create() 配对调用。
     */
    virtual void cleanup() = 0;

    /**
     * @brief 执行渲染
     * 
     * @param commandBuffer Vulkan 命令缓冲区
     * @param pipelineLayout 管线布局
     * @param viewMatrix 视图矩阵
     * @param projectionMatrix 投影矩阵
     */
    virtual void render(VkCommandBuffer commandBuffer, 
                        VkPipelineLayout pipelineLayout,
                        const glm::mat4& viewMatrix, 
                        const glm::mat4& projectionMatrix) = 0;

    /**
     * @brief 获取渲染器名称（用于调试和日志）
     */
    virtual std::string getName() const = 0;

    /**
     * @brief 检查渲染器是否已初始化
     */
    virtual bool isCreated() const = 0;

    /**
     * @brief 设置渲染器启用/禁用状态
     */
    virtual void setEnabled(bool enabled) { enabled_ = enabled; }

    /**
     * @brief 检查渲染器是否启用
     */
    virtual bool isEnabled() const { return enabled_; }

    /**
     * @brief 设置渲染优先级（数值越小越先渲染）
     */
    virtual void setRenderOrder(int order) { renderOrder_ = order; }

    /**
     * @brief 获取渲染优先级
     */
    virtual int getRenderOrder() const { return renderOrder_; }

protected:
    bool enabled_ = true;
    int renderOrder_ = 0;
    bool created_ = false;
};

/**
 * @brief 渲染器类型枚举
 */
enum class RendererType {
    UNKNOWN = 0,
    SKYBOX = 10,      // 最先渲染（背景）
    FLOOR = 20,       // 地面
    MODEL = 30,       // 3D 模型
    CUBE = 40,        // 调试立方体
    TEXT = 50,        // 文字
    UI = 100          // UI 元素（最后渲染）
};

} // namespace owengine
