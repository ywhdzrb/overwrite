#include "resource_manager.h"
#include "gltf_model.h"
#include "texture.h"
#include "texture_loader.h"
#include "logger.h"
#include <stdexcept>

namespace owengine {

ResourceManager::ResourceManager(std::shared_ptr<VulkanDevice> device)
    : device_(device)
    , textureLoader_(std::make_shared<TextureLoader>(device)) {
}

ResourceManager::~ResourceManager() {
    cleanupAll();
}

// ========== 模型管理 ==========

ModelHandle ResourceManager::loadModel(const std::string& id, const std::string& filePath) {
    // 检查是否已加载
    if (hasModel(id)) {
        Logger::warning("模型已存在，返回已有实例: " + id);
        return ModelHandle{id};
    }
    
    try {
        auto model = std::make_unique<GLTFModel>(device_, textureLoader_);
        model->loadFromFile(filePath);
        
        models_[id] = std::move(model);
        Logger::info("模型加载成功: " + id + " <- " + filePath);
        
        return ModelHandle{id};
    } catch (const std::exception& e) {
        Logger::error(std::string("模型加载失败: ") + e.what());
        return ModelHandle{};
    }
}

GLTFModel* ResourceManager::getModel(ModelHandle handle) const {
    if (!handle.valid()) return nullptr;
    
    auto it = models_.find(handle.id);
    if (it != models_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool ResourceManager::hasModel(const std::string& id) const {
    return models_.find(id) != models_.end();
}

void ResourceManager::unloadModel(const std::string& id) {
    auto it = models_.find(id);
    if (it != models_.end()) {
        it->second->cleanup();
        models_.erase(it);
        
        // 同时清理描述符集
        modelDescriptorSets_.erase(id);
        
        Logger::info("模型已卸载: " + id);
    }
}

VkDescriptorSet ResourceManager::getModelDescriptorSet(ModelHandle handle) const {
    if (!handle.valid()) return VK_NULL_HANDLE;
    
    auto it = modelDescriptorSets_.find(handle.id);
    if (it != modelDescriptorSets_.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

void ResourceManager::createModelDescriptorSet(ModelHandle handle,
                                                VkDescriptorSetLayout layout,
                                                VkDescriptorPool pool) {
    if (!handle.valid()) return;
    
    // 此处需要实现描述符集创建逻辑
    // 具体实现依赖于 GLTFModel 的纹理信息
    Logger::info("为模型创建描述符集: " + handle.id);
}

// ========== 纹理管理 ==========

TextureHandle ResourceManager::loadTexture(const std::string& id, const std::string& filePath) {
    if (hasTexture(id)) {
        Logger::warning("纹理已存在，返回已有实例: " + id);
        return TextureHandle{id};
    }
    
    try {
        auto texture = textureLoader_->loadTexture(filePath);
        textures_[id] = texture;
        Logger::info("纹理加载成功: " + id + " <- " + filePath);
        
        return TextureHandle{id};
    } catch (const std::exception& e) {
        Logger::error(std::string("纹理加载失败: ") + e.what());
        return TextureHandle{};
    }
}

std::shared_ptr<Texture> ResourceManager::getTexture(TextureHandle handle) const {
    if (!handle.valid()) return nullptr;
    
    auto it = textures_.find(handle.id);
    if (it != textures_.end()) {
        return it->second;
    }
    return nullptr;
}

bool ResourceManager::hasTexture(const std::string& id) const {
    return textures_.find(id) != textures_.end();
}

// ========== 批量操作 ==========

void ResourceManager::cleanupUnused() {
    // TODO: 实现引用计数清理
    Logger::info("清理未使用资源");
}

void ResourceManager::cleanupAll() {
    // 清理所有模型
    for (auto& [id, model] : models_) {
        if (model) {
            model->cleanup();
        }
    }
    models_.clear();
    modelDescriptorSets_.clear();
    
    // 清理所有纹理
    textures_.clear();
    
    Logger::info("所有资源已清理");
}

} // namespace owengine
