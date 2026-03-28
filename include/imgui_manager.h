#ifndef IMGUI_MANAGER_H
#define IMGUI_MANAGER_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

#include "vulkan_device.h"
#include "vulkan_swapchain.h"
#include "vulkan_render_pass.h"

namespace vgame {

class ImGuiManager {
public:
    ImGuiManager(std::shared_ptr<VulkanDevice> device,
                 std::shared_ptr<VulkanSwapchain> swapchain,
                 std::shared_ptr<VulkanRenderPass> renderPass,
                 GLFWwindow* window);
    ~ImGuiManager();

    void init();
    void cleanup();
    void newFrame();
    void render(VkCommandBuffer commandBuffer);
    void onResize();

private:
    void createDescriptorPool();
    void createRenderPass();
    void createFramebuffers();
    void createCommandBuffers();

    std::shared_ptr<VulkanDevice> vulkanDevice;
    std::shared_ptr<VulkanSwapchain> swapchain;
    std::shared_ptr<VulkanRenderPass> mainRenderPass;
    GLFWwindow* window;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkRenderPass imguiRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> commandBuffers;

    bool initialized = false;
};

} // namespace vgame

#endif // IMGUI_MANAGER_H
