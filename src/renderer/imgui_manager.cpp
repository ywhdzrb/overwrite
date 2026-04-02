#include "imgui_manager.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include <iostream>

namespace vgame {

ImGuiManager::ImGuiManager(std::shared_ptr<VulkanDevice> device,
                           std::shared_ptr<VulkanSwapchain> swapchain,
                           std::shared_ptr<VulkanRenderPass> renderPass,
                           GLFWwindow* window,
                           VkInstance instance)
    : vulkanDevice(device), swapchain(swapchain), mainRenderPass(renderPass), window(window), instance(instance) {
}

ImGuiManager::~ImGuiManager() {
    cleanup();
}

void ImGuiManager::init() {
    if (initialized) return;

    // 创建 IMGUI 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // 设置风格
    ImGui::StyleColorsDark();
    
    // 加载中文字体
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    
    // 尝试加载思源黑体
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        "/usr/share/fonts/adobe-source-han-sans/SourceHanSansCN-Bold.otf",
        18.0f, &config,
        io.Fonts->GetGlyphRangesChineseFull()
    );
    
    if (font) {
        io.FontDefault = font;
        std::cout << "[ImGuiManager] 中文字体加载成功" << std::endl;
    } else {
        // 回退到默认字体
        io.Fonts->AddFontDefault();
        std::cout << "[ImGuiManager] 使用默认字体" << std::endl;
    }

    // 初始化 GLFW 后端
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // 创建描述符池
    createDescriptorPool();

    // 初始化 Vulkan 后端
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_3;
    init_info.Instance = instance;
    init_info.PhysicalDevice = vulkanDevice->getPhysicalDevice();
    init_info.Device = vulkanDevice->getDevice();
    init_info.QueueFamily = 0;  // 需要获取正确的队列族索引
    init_info.Queue = vulkanDevice->getGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = descriptorPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = static_cast<uint32_t>(swapchain->getImageViews().size());
    
    // 设置管线信息
    init_info.PipelineInfoMain.RenderPass = mainRenderPass->getRenderPass();
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }

    // 字体现在自动加载，不需要手动调用 CreateFontsTexture

    initialized = true;
    std::cout << "[ImGuiManager] 初始化成功" << std::endl;
}

void ImGuiManager::cleanup() {
    if (!initialized) return;

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice->getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    initialized = false;
}

void ImGuiManager::createDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(vulkanDevice->getDevice(), &pool_info, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool for ImGui");
    }
}

void ImGuiManager::newFrame() {
    if (!initialized) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::render(VkCommandBuffer commandBuffer) {
    if (!initialized) return;

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
}

void ImGuiManager::onResize() {
    if (!initialized) return;
    ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(swapchain->getImageViews().size()));
}

} // namespace vgame
