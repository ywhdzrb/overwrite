/**
 * @file shader_manager.cpp
 * @brief 着色器模块管理器实现
 *
 * 提供 VkShaderModule 的按路径去重加载和缓存管理。
 * 消除 GrassSystem/Fsr1Pass/CloudSystem 等子系统中重复的 readFile+createShaderModule 样板。
 */

#include "renderer/shader_manager.hpp"
#include "core/vulkan_device.hpp"
#include "utils/logger.hpp"

#include <fstream>

namespace owengine {

ShaderManager::ShaderManager(std::shared_ptr<VulkanDevice> device)
    : device_(std::move(device)) {
}

ShaderManager::~ShaderManager() {
    cleanup();
}

VkShaderModule ShaderManager::getModule(const std::string& path) {
    // 检查缓存
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return it->second;
    }

    // 读取 SPIR-V 文件
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        Logger::error("[ShaderManager] 无法打开着色器文件: " + path);
        return VK_NULL_HANDLE;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();

    // 创建着色器模块
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule module;
    VkResult result = vkCreateShaderModule(device_->getDevice(), &createInfo, nullptr, &module);
    if (result != VK_SUCCESS) {
        Logger::error("[ShaderManager] 创建着色器模块失败: " + path);
        return VK_NULL_HANDLE;
    }

    // 缓存并返回
    cache_[path] = module;
    return module;
}

VkPipelineShaderStageCreateInfo ShaderManager::getStageInfo(
    const std::string& path, VkShaderStageFlagBits stage) {
    VkShaderModule module = getModule(path);

    VkPipelineShaderStageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = stage;
    info.module = module;
    info.pName = "main";
    return info;
}

bool ShaderManager::hasModule(const std::string& path) const {
    return cache_.find(path) != cache_.end();
}

void ShaderManager::unloadModule(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        vkDestroyShaderModule(device_->getDevice(), it->second, nullptr);
        cache_.erase(it);
    }
}

void ShaderManager::cleanup() {
    VkDevice dev = device_->getDevice();
    for (auto& [path, module] : cache_) {
        vkDestroyShaderModule(dev, module, nullptr);
    }
    cache_.clear();
}

} // namespace owengine
