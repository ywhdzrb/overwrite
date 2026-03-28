#ifndef VULKAN_PIPELINE_H
#define VULKAN_PIPELINE_H

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace vgame {

class VulkanDevice;

class VulkanPipeline {
public:
    VulkanPipeline(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass, 
                   VkExtent2D swapchainExtent, const std::string& vertexShaderPath, 
                   const std::string& fragmentShaderPath);
    ~VulkanPipeline();

    // 禁止拷贝
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    void create();
    void cleanup();
    
    VkPipeline getPipeline() const { return graphicsPipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    std::shared_ptr<VulkanDevice> device;
    VkRenderPass renderPass;
    VkExtent2D swapchainExtent;
    std::string vertexShaderPath;
    std::string fragmentShaderPath;
    
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};

} // namespace vgame

#endif // VULKAN_PIPELINE_H