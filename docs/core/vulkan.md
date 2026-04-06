# Vulkan 封装模块

本文档描述了项目中对 Vulkan API 的封装类。这些类提供了对 Vulkan 核心功能的高级抽象。

## 类概览

| 类名 | 文件 | 描述 |
|------|------|------|
| `VulkanInstance` | `include/vulkan_instance.h` | Vulkan 实例和设备管理 |
| `VulkanDevice` | `include/vulkan_device.h` | 逻辑设备和队列管理 |
| `VulkanSwapchain` | `include/vulkan_swapchain.h` | 交换链管理 |
| `VulkanRenderPass` | `include/vulkan_render_pass.h` | 渲染通道 |
| `VulkanPipeline` | `include/vulkan_pipeline.h` | 图形管线 |
| `VulkanFramebuffer` | `include/vulkan_framebuffer.h` | 帧缓冲区 |
| `VulkanCommandBuffer` | `include/vulkan_command_buffer.h` | 命令缓冲区 |
| `VulkanSync` | `include/vulkan_sync.h` | 同步对象 |

---

## VulkanInstance

管理 Vulkan 实例、物理设备选择、逻辑设备创建和表面。

### 结构体

#### QueueFamilyIndices

```cpp
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;  // 图形队列族索引
    std::optional<uint32_t> presentFamily;   // 呈现队列族索引
    
    bool isComplete() const;  // 检查是否找到所有必需的队列族
};
```

### 类方法

```cpp
class VulkanInstance {
public:
    VulkanInstance();
    ~VulkanInstance();
    
    // 禁止拷贝
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;
    
    // 初始化（必须在使用前调用）
    void initialize(GLFWwindow* window);
    void cleanup();
    
    // 获取器
    VkInstance getInstance() const;
    VkPhysicalDevice getPhysicalDevice() const;
    VkDevice getDevice() const;
    VkQueue getGraphicsQueue() const;
    VkQueue getPresentQueue() const;
    VkSurfaceKHR getSurface() const;
    QueueFamilyIndices getQueueFamilyIndices() const;
};
```

### 使用示例

```cpp
auto vulkanInstance = std::make_shared<VulkanInstance>();
vulkanInstance->initialize(window);

// 获取设备
VkDevice device = vulkanInstance->getDevice();

// 清理
vulkanInstance->cleanup();
```

---

## VulkanDevice

封装逻辑设备、命令池和深度缓冲区管理。

### 结构体

#### SwapChainSupportDetails

```cpp
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;       // 表面能力
    std::vector<VkSurfaceFormatKHR> formats;     // 支持的格式
    std::vector<VkPresentModeKHR> presentModes;  // 支持的呈现模式
};
```

### 类方法

```cpp
class VulkanDevice {
public:
    VulkanDevice(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface);
    ~VulkanDevice();
    
    // 禁止拷贝
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    
    // 交换链支持查询
    SwapChainSupportDetails querySwapChainSupport() const;
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) const;
    
    // 内存和命令
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    
    // 图像布局转换
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) const;
    
    // 缓冲区到图像复制
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
    
    // 深度缓冲区
    void createDepthResources(VkExtent2D extent, VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    void cleanupDepthResources();
    
    // 获取器
    VkPhysicalDevice getPhysicalDevice() const;
    VkDevice getDevice() const;
    VkSurfaceKHR getSurface() const;
    VkQueue getGraphicsQueue() const;
    VkQueue getPresentQueue() const;
    uint32_t getGraphicsQueueFamily() const;
    VkCommandPool getCommandPool() const;
    VkImage getDepthImage() const;
    VkImageView getDepthImageView() const;
};
```

---

## VulkanSwapchain

管理交换链的创建和重建。

### 类方法

```cpp
class VulkanSwapchain {
public:
    VulkanSwapchain(std::shared_ptr<VulkanDevice> device, GLFWwindow* window);
    ~VulkanSwapchain();
    
    // 禁止拷贝
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    
    void create();
    void cleanup();
    void recreate(GLFWwindow* window);  // 窗口大小改变时调用
    
    // 获取器
    VkSwapchainKHR getSwapchain() const;
    const std::vector<VkImage>& getImages() const;
    const std::vector<VkImageView>& getImageViews() const;
    VkFormat getImageFormat() const;
    VkExtent2D getExtent() const;
};
```

---

## VulkanRenderPass

管理渲染通道配置。

### 类方法

```cpp
class VulkanRenderPass {
public:
    VulkanRenderPass(std::shared_ptr<VulkanDevice> device, 
                     VkFormat swapchainImageFormat,
                     VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    ~VulkanRenderPass();
    
    // 禁止拷贝
    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;
    
    void create();
    void cleanup();
    
    VkRenderPass getRenderPass() const;
    void setMsaaSamples(VkSampleCountFlagBits samples);
};
```

---

## VulkanPipeline

管理图形管线的创建。

### 枚举

```cpp
enum class VertexFormat {
    POSITION_COLOR,  // 位置 + 颜色（标准）
    POSITION_ONLY    // 仅位置（天空盒）
};
```

### 类方法

```cpp
class VulkanPipeline {
public:
    VulkanPipeline(std::shared_ptr<VulkanDevice> device,
                   VkRenderPass renderPass,
                   VkExtent2D swapchainExtent,
                   const std::string& vertexShaderPath,
                   const std::string& fragmentShaderPath,
                   VertexFormat format = VertexFormat::POSITION_COLOR,
                   const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
                   VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    ~VulkanPipeline();
    
    // 禁止拷贝
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;
    
    void create();
    void cleanup();
    
    VkPipeline getPipeline() const;
    VkPipelineLayout getPipelineLayout() const;
    void setMsaaSamples(VkSampleCountFlagBits samples);
};
```

### 使用示例

```cpp
auto pipeline = std::make_shared<VulkanPipeline>(
    device,
    renderPass->getRenderPass(),
    swapchain->getExtent(),
    "shaders/shader.vert.spv",
    "shaders/shader.frag.spv",
    VertexFormat::POSITION_COLOR,
    { descriptorSetLayout }
);
pipeline->create();
```

---

## VulkanFramebuffer

管理帧缓冲区。

### 类方法

```cpp
class VulkanFramebuffer {
public:
    VulkanFramebuffer(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass);
    ~VulkanFramebuffer();
    
    // 禁止拷贝
    VulkanFramebuffer(const VulkanFramebuffer&) = delete;
    VulkanFramebuffer& operator=(const VulkanFramebuffer&) = delete;
    
    void create(const std::vector<VkImageView>& swapchainImageViews,
                VkExtent2D swapchainExtent,
                VkImageView colorImageView = VK_NULL_HANDLE);  // MSAA 颜色附件
    void cleanup();
    void recreate(const std::vector<VkImageView>& swapchainImageViews,
                  VkExtent2D swapchainExtent,
                  VkImageView colorImageView = VK_NULL_HANDLE);
    
    const std::vector<VkFramebuffer>& getFramebuffers() const;
};
```

---

## VulkanCommandBuffer

管理命令缓冲区的创建和录制。

### 类方法

```cpp
class VulkanCommandBuffer {
public:
    VulkanCommandBuffer(std::shared_ptr<VulkanDevice> device,
                        VkRenderPass renderPass,
                        VkPipeline pipeline,
                        VkPipelineLayout pipelineLayout);
    ~VulkanCommandBuffer();
    
    // 禁止拷贝
    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    
    void create(size_t imageCount);
    void cleanup();
    void record(size_t imageIndex,
                VkFramebuffer framebuffer,
                VkExtent2D swapchainExtent,
                const glm::mat4& viewMatrix,
                const glm::mat4& projectionMatrix);
    
    const std::vector<VkCommandBuffer>& getCommandBuffers() const;
    
    // 更新引用（重建交换链时使用）
    void updateRenderPass(VkRenderPass newRenderPass);
    void updatePipeline(VkPipeline newPipeline);
    void updatePipelineLayout(VkPipelineLayout newPipelineLayout);
};
```

---

## VulkanSync

管理同步对象（信号量和栅栏）。

### 类方法

```cpp
class VulkanSync {
public:
    VulkanSync(std::shared_ptr<VulkanDevice> device);
    ~VulkanSync();
    
    // 禁止拷贝
    VulkanSync(const VulkanSync&) = delete;
    VulkanSync& operator=(const VulkanSync&) = delete;
    
    void create(size_t maxFramesInFlight);
    void cleanup();
    
    const std::vector<VkSemaphore>& getImageAvailableSemaphores() const;
    const std::vector<VkSemaphore>& getRenderFinishedSemaphores() const;
    const std::vector<VkFence>& getInFlightFences() const;
    size_t getMaxFramesInFlight() const;
};
```

---

## 典型使用流程

```cpp
// 1. 创建实例
auto vulkanInstance = std::make_shared<VulkanInstance>();
vulkanInstance->initialize(window);

// 2. 创建设备包装器
auto vulkanDevice = std::make_shared<VulkanDevice>(
    vulkanInstance->getPhysicalDevice(),
    vulkanInstance->getDevice(),
    vulkanInstance->getSurface()
);

// 3. 创建交换链
auto swapchain = std::make_shared<VulkanSwapchain>(vulkanDevice, window);
swapchain->create();

// 4. 创建渲染通道
auto renderPass = std::make_shared<VulkanRenderPass>(
    vulkanDevice,
    swapchain->getImageFormat()
);
renderPass->create();

// 5. 创建管线
auto pipeline = std::make_shared<VulkanPipeline>(
    vulkanDevice,
    renderPass->getRenderPass(),
    swapchain->getExtent(),
    "shader.vert.spv",
    "shader.frag.spv"
);
pipeline->create();

// 6. 创建帧缓冲区
auto framebuffers = std::make_shared<VulkanFramebuffer>(
    vulkanDevice,
    renderPass->getRenderPass()
);
framebuffers->create(swapchain->getImageViews(), swapchain->getExtent());

// 7. 创建同步对象
auto sync = std::make_shared<VulkanSync>(vulkanDevice);
sync->create(2);  // 2 帧在飞

// 8. 渲染循环
while (!glfwWindowShouldClose(window)) {
    // 获取下一帧图像
    vkAcquireNextImageKHR(...);
    
    // 录制命令
    commandBuffers->record(...);
    
    // 提交和呈现
    vkQueueSubmit(...);
    vkQueuePresentKHR(...);
}
```

## 窗口大小改变处理

当窗口大小改变时，需要重建交换链相关资源：

```cpp
void onWindowResize() {
    vkDeviceWaitIdle(device);
    
    swapchain->recreate(window);
    renderPass->cleanup();
    renderPass->create();
    pipeline->cleanup();
    pipeline->create();
    framebuffers->recreate(swapchain->getImageViews(), swapchain->getExtent());
}
```
