#pragma once

/**
 * @file vulkan_instance.h
 * @brief Vulkan 实例管理 — 实例/物理设备/逻辑设备/表面创建与生命周期
 *
 * 归属模块：core
 * 核心职责：封装 Vulkan 初始化流程，提供 instance/device/surface/queue 访问接口
 * 依赖关系：Vulkan SDK、GLFW
 * 关键设计：RAII 管理，资源在 initialize() 中创建，在 cleanup()/析构中销毁
 *           支持 Debug 验证层（NDEBUG 条件编译），队列族索引通过 QueueFamilyIndices 结构体传递
 */

#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

namespace owengine {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] bool isComplete() const noexcept {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

class VulkanInstance {
public:
    VulkanInstance();
    ~VulkanInstance();

    // 禁止拷贝
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    void initialize(GLFWwindow* window);
    void cleanup();

    [[nodiscard]] VkInstance getInstance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VkDevice getDevice() const noexcept { return device_; }
    [[nodiscard]] VkQueue getGraphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] VkQueue getPresentQueue() const noexcept { return presentQueue_; }
    [[nodiscard]] VkSurfaceKHR getSurface() const noexcept { return surface_; }
    
    [[nodiscard]] QueueFamilyIndices getQueueFamilyIndices() const noexcept { return queueFamilyIndices_; }

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    
    [[nodiscard]] std::vector<const char*> getRequiredExtensions();
    [[nodiscard]] bool checkValidationLayerSupport();
    
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    // 成员变量
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    
    QueueFamilyIndices queueFamilyIndices_;
    
    const std::vector<const char*> validationLayers_{
        "VK_LAYER_KHRONOS_validation"
    };
    
#ifdef NDEBUG
    const bool enableValidationLayers_{false};
#else
    const bool enableValidationLayers_{true};
#endif
};

} // namespace owengine
