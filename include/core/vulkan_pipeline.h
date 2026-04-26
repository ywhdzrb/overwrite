#ifndef VULKAN_PIPELINE_H
#define VULKAN_PIPELINE_H

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace owengine {

class VulkanDevice;

enum class VertexFormat {
    POSITION_COLOR,  // 位置 + 颜色（标准）
    POSITION_ONLY    // 仅位置（天空盒）
};

class VulkanPipeline {
public:
    VulkanPipeline(std::shared_ptr<VulkanDevice> device, VkRenderPass renderPass,
                   VkExtent2D swapchainExtent, const std::string& vertexShaderPath,
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
    
    VkPipeline getPipeline() const { return graphicsPipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    
    void setMsaaSamples(VkSampleCountFlagBits samples) { msaaSamples = samples; }

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    std::shared_ptr<VulkanDevice> device;
    VkRenderPass renderPass;
    VkExtent2D swapchainExtent;
    std::string vertexShaderPath;
    std::string fragmentShaderPath;
    VkSampleCountFlagBits msaaSamples;

    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> descriptorSetLayoutsList;

}; // end of class VulkanPipeline

} // namespace owengine

#endif // VULKAN_PIPELINE_H