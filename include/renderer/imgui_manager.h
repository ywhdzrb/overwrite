#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

#include "core/vulkan_device.h"
#include "core/vulkan_swapchain.h"
#include "core/vulkan_render_pass.h"

namespace owengine {

class ImGuiManager {
public:
    ImGuiManager(std::shared_ptr<VulkanDevice> device,
                 std::shared_ptr<VulkanSwapchain> swapchain,
                 std::shared_ptr<VulkanRenderPass> renderPass,
                 GLFWwindow* window,
                 VkInstance instance,
                 VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
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
    VkInstance instance;
    VkSampleCountFlagBits msaaSamples;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkRenderPass imguiRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> commandBuffers;

    bool initialized = false;
};

} // namespace owengine
