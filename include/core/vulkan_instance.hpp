#pragma once

/**
 * @file vulkan_instance.hpp
 * @brief Vulkan 实例管理 — 实例/调试信使/表面/物理设备
 *
 * 归属模块：core
 * 核心职责：封装 Vulkan 实例初始化，提供 instance/surface/physicalDevice 访问接口
 *           逻辑设备创建已移至 VulkanDevice，VulkanInstance 只负责实例层级
 * 依赖关系：Vulkan SDK、GLFW
 * 关键设计：RAII 管理，资源在 initialize() 中创建，在 cleanup()/析构中销毁
 *           支持 Debug 验证层（NDEBUG 条件编译）
 */

#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

namespace owengine {

/**
 * @brief 队列族索引结构体
 * 用于传递图形队列和呈现队列的族索引
 */
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

    /** @brief 提前销毁表面，在 Vulkan 设备销毁前调用以断开 X11 连接 */
    void destroySurfaceEarly();

    [[nodiscard]] VkInstance getInstance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VkSurfaceKHR getSurface() const noexcept { return surface_; }

    /** @brief 查询验证层是否启用（供 VulkanDevice 构造时传入） */
    [[nodiscard]] bool isValidationEnabled() const noexcept { return enableValidationLayers_; }

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();

    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device);
    [[nodiscard]] bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    [[nodiscard]] std::vector<const char*> getRequiredExtensions();
    [[nodiscard]] bool checkValidationLayerSupport();

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    // 调试信使辅助方法
    static VkResult createDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger);

    static void destroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks* pAllocator);

    // 成员变量
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;

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
