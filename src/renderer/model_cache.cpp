/**
 * @file model_cache.cpp
 * @brief 模型缓存实现
 *
 * 提供 GLTFModel 的按路径去重加载和生命周期管理。
 * 与 RenderSystem 配合使用，支持 ECS 驱动的延迟加载渲染。
 */

#include "renderer/model_cache.hpp"
#include "renderer/gltf_model.hpp"
#include "utils/logger.hpp"

#include <vector>

namespace owengine {

ModelCache::ModelCache(std::shared_ptr<VulkanDevice> device,
                       std::shared_ptr<TextureLoader> textureLoader)
    : device_(std::move(device))
    , textureLoader_(std::move(textureLoader)) {
}

ModelCache::~ModelCache() {
    clear();
}

GLTFModel* ModelCache::getOrLoadModel(const std::string& path) {
    // 检查缓存
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return it->second.get();
    }

    // 首次加载
    auto model = std::make_unique<GLTFModel>(device_, textureLoader_);
    if (!model->loadFromFile(path)) {
        Logger::error("[ModelCache] 加载模型失败: " + path);
        return nullptr;
    }

    Logger::info("[ModelCache] 加载模型: " + path);
    GLTFModel* ptr = model.get();
    cache_[path] = std::move(model);
    return ptr;
}

void ModelCache::createMeshDescriptorSets(VkDescriptorSetLayout textureDSLayout,
                                          VkDescriptorPool pool) {
    for (auto& [path, model] : cache_) {
        if (model && model->getMeshCount() > 0) {
            model->createMeshDescriptorSets(textureDSLayout, pool);
        }
    }
}

VkDescriptorSet ModelCache::createModelDescriptorSet(
    GLTFModel* model,
    VkDescriptorSetLayout textureDSLayout,
    VkDescriptorPool pool) {
    if (!model || model->getMeshCount() == 0) return VK_NULL_HANDLE;

    auto firstTex = model->getFirstTexture();
    if (!firstTex) return VK_NULL_HANDLE;

    // 创建描述符集
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureDSLayout;

    VkDescriptorSet descSet;
    if (vkAllocateDescriptorSets(device_->getDevice(), &allocInfo, &descSet) != VK_SUCCESS) {
        Logger::error("[ModelCache] 分配模型描述符集失败");
        return VK_NULL_HANDLE;
    }

    // 写入纹理描述符
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = firstTex->getImageView();
    imageInfo.sampler = firstTex->getSampler();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device_->getDevice(), 1, &descriptorWrite, 0, nullptr);

    return descSet;
}

void ModelCache::unloadModel(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        Logger::debug("[ModelCache] 卸载模型: " + path);
        cache_.erase(it);
    }
}

void ModelCache::clear() {
    Logger::debug("[ModelCache] 清空所有缓存模型");
    cache_.clear();
}

} // namespace owengine
