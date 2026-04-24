#ifndef VULKAN_SWAPCHAIN_H
#define VULKAN_SWAPCHAIN_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <memory>

namespace owengine {

class VulkanDevice;

class VulkanSwapchain {
public:
    VulkanSwapchain(std::shared_ptr<VulkanDevice> device, GLFWwindow* window);
    ~VulkanSwapchain();

    // 禁止拷贝
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    void create();
    void cleanup();
    void recreate(GLFWwindow* window);
    
    VkSwapchainKHR getSwapchain() const { return swapchain; }
    const std::vector<VkImage>& getImages() const { return swapchainImages; }
    const std::vector<VkImageView>& getImageViews() const { return swapchainImageViews; }
    VkFormat getImageFormat() const { return swapchainImageFormat; }
    VkExtent2D getExtent() const { return swapchainExtent; }

private:
    void createImageViews();

    std::shared_ptr<VulkanDevice> device;
    GLFWwindow* window;
    
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
};

} // namespace owengine

#endif // VULKAN_SWAPCHAIN_H