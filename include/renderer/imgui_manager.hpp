#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

#include "core/vulkan_device.hpp"
#include "core/vulkan_swapchain.hpp"
#include "core/vulkan_render_pass.hpp"

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

    std::shared_ptr<VulkanDevice> vulkanDevice_;
    std::shared_ptr<VulkanSwapchain> swapchain_;
    std::shared_ptr<VulkanRenderPass> mainRenderPass_;
    GLFWwindow* window_;
    VkInstance instance_;
    VkSampleCountFlagBits msaaSamples_;

    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkRenderPass imguiRenderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkCommandBuffer> commandBuffers_;

    bool initialized_ = false;
};

} // namespace owengine
