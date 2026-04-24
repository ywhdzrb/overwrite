#ifndef RENDER_ENGINE_H
#define RENDER_ENGINE_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <functional>

namespace owengine {

class VulkanInstance;
class VulkanDevice;
class VulkanSwapchain;
class VulkanRenderPass;
class VulkanPipeline;
class VulkanFramebuffer;
class VulkanCommandBuffer;
class VulkanSync;
class SceneManager;

/**
 * @brief 渲染引擎配置
 */
struct RenderEngineConfig {
    int width = 800;
    int height = 600;
    std::string title = "OverWrite";
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_4_BIT;
    bool enableValidation = true;
    bool enableVSync = false;
    uint32_t maxFramesInFlight = 2;
};

/**
 * @brief 渲染引擎
 * 
 * 负责核心渲染流程：
 * - Vulkan 初始化
 * - 交换链管理
 * - 渲染通道
 * - 命令缓冲
 * - 同步
 */
class RenderEngine {
public:
    RenderEngine();
    ~RenderEngine();
    
    // 禁止拷贝
    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;
    
    // ========== 初始化 ==========
    
    /**
     * @brief 初始化渲染引擎
     * @param config 配置
     * @param window GLFW 窗口（可选，若为 nullptr 则自动创建）
     */
    void initialize(const RenderEngineConfig& config, GLFWwindow* window = nullptr);
    
    /**
     * @brief 清理资源
     */
    void cleanup();
    
    // ========== 渲染流程 ==========
    
    /**
     * @brief 开始帧渲染
     * @return 当前帧索引，-1 表示需要重建交换链
     */
    int beginFrame();
    
    /**
     * @brief 结束帧渲染
     */
    void endFrame();
    
    /**
     * @brief 渲染一帧
     * @param sceneManager 场景管理器
     */
    void renderFrame(SceneManager* sceneManager);
    
    /**
     * @brief 重建交换链
     */
    void recreateSwapchain();
    
    // ========== 资源访问 ==========
    
    VkDevice getDevice() const;
    VkPhysicalDevice getPhysicalDevice() const;
    VkCommandBuffer getCurrentCommandBuffer() const;
    VkRenderPass getRenderPass() const;
    VkPipelineLayout getPipelineLayout() const;
    VkExtent2D getExtent() const;
    VkSampleCountFlagBits getMsaaSamples() const { return config_.msaaSamples; }
    
    std::shared_ptr<VulkanDevice> getVulkanDevice() const { return vulkanDevice_; }
    std::shared_ptr<VulkanPipeline> getPipeline() const { return graphicsPipeline_; }
    std::shared_ptr<VulkanPipeline> getSkyboxPipeline() const { return skyboxPipeline_; }
    
    // ========== 描述符管理 ==========
    
    VkDescriptorSetLayout getTextureDescriptorLayout() const { return textureDescriptorLayout_; }
    VkDescriptorSetLayout getLightDescriptorLayout() const { return lightDescriptorLayout_; }
    VkDescriptorPool getDescriptorPool() const { return descriptorPool_; }
    
    // ========== MSAA 管理 ==========
    
    void setMsaaSamples(VkSampleCountFlagBits samples);
    VkSampleCountFlagBits getMaxMsaaSamples() const { return maxMsaaSamples_; }
    
    // ========== 回调 ==========
    
    using RenderCallback = std::function<void(VkCommandBuffer)>;
    void setRenderCallback(RenderCallback callback) { renderCallback_ = callback; }

private:
    void createDescriptorSetLayouts();
    void createDescriptorPool();
    void createColorResources();
    void cleanupColorResources();
    
    RenderEngineConfig config_;
    GLFWwindow* window_ = nullptr;
    bool ownsWindow_ = false;
    
    // Vulkan 核心
    std::shared_ptr<VulkanInstance> vulkanInstance_;
    std::shared_ptr<VulkanDevice> vulkanDevice_;
    std::shared_ptr<VulkanSwapchain> swapchain_;
    std::shared_ptr<VulkanRenderPass> renderPass_;
    std::shared_ptr<VulkanPipeline> graphicsPipeline_;
    std::shared_ptr<VulkanPipeline> skyboxPipeline_;
    std::shared_ptr<VulkanFramebuffer> framebuffers_;
    std::shared_ptr<VulkanCommandBuffer> commandBuffers_;
    std::shared_ptr<VulkanSync> syncObjects_;
    
    // MSAA 资源
    VkSampleCountFlagBits maxMsaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory_ = VK_NULL_HANDLE;
    VkImageView colorImageView_ = VK_NULL_HANDLE;
    
    // 描述符
    VkDescriptorSetLayout textureDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
    // 光源 UBO
    VkBuffer lightUniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lightUniformBufferMemory_ = VK_NULL_HANDLE;
    
    // 帧状态
    uint32_t currentFrame_ = 0;
    uint32_t maxFramesInFlight_ = 2;
    
    // 回调
    RenderCallback renderCallback_;
};

} // namespace owengine

#endif // RENDER_ENGINE_H
