/**
 * @file render_system.cpp
 * @brief ECS 驱动渲染系统实现
 *
 * 工作流程：
 *   update() 每帧扫描 ECS registry 中带 RenderComponent+TransformComponent 的实体，
 *   对比上次状态自动加载/卸载 GLTFModel，同步变换，生成渲染条目列表。
 *
 * 与 Renderer 的交互：
 *   getRenderEntries() 返回当前可见实体列表，
 *   Renderer 在 drawFrame() 中遍历此列表并调用 GLTFModel::render()。
 */

#include "renderer/render_system.hpp"
#include "renderer/model_cache.hpp"
#include "renderer/gltf_model.hpp"
#include "utils/logger.hpp"

#include <algorithm>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace owengine {

// ============================================================
// 构造与析构
// ============================================================

RenderSystem::RenderSystem(entt::registry& registry,
                           std::shared_ptr<ModelCache> modelCache,
                           VkDescriptorSetLayout textureDSLayout,
                           VkDescriptorPool descriptorPool)
    : registry_(registry)
    , modelCache_(std::move(modelCache))
    , textureDSLayout_(textureDSLayout)
    , descriptorPool_(descriptorPool) {
}

RenderSystem::~RenderSystem() {
    cleanup();
}

// ============================================================
// 每帧更新：扫描 ECS 差异，加载/卸载/同步模型
// ============================================================

void RenderSystem::update(float deltaTime) {
    lastFrameAdded_ = 0;
    lastFrameRemoved_ = 0;

    // 1. 扫描当前 ECS 中所有带 RenderComponent 的实体
    auto view = registry_.view<ecs::RenderComponent, ecs::TransformComponent>();

    // 构建当前实体集合（先计数再预留空间）
    size_t viewCount = 0;
    for (auto it = view.begin(); it != view.end(); ++it) ++viewCount;
    std::vector<entt::entity> currentEntities;
    currentEntities.reserve(viewCount);

    for (auto entity : view) {
        currentEntities.push_back(entity);

        auto it = entityModels_.find(entity);
        if (it == entityModels_.end()) {
            // 新增实体：加载模型
            const auto& renderComp = view.get<ecs::RenderComponent>(entity);
            loadModelForEntity(entity, renderComp);
            lastFrameAdded_++;
        } else {
            // 已有实体：变换同步
            auto& loaded = it->second;
            if (loaded.visible) {
                syncTransform(entity, loaded);
            }
        }
    }

    // 2. 检测已移除的实体
    if (!knownEntities_.empty()) {
        for (auto oldEntity : knownEntities_) {
            auto it = entityModels_.find(oldEntity);
            if (it != entityModels_.end()) {
                // 检查是否仍在当前视图中
                bool found = false;
                for (auto e : currentEntities) {
                    if (e == oldEntity) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    removeEntity(oldEntity);
                    lastFrameRemoved_++;
                }
            }
        }
    }

    // 3. 更新已知实体列表
    knownEntities_ = std::move(currentEntities);

    // 4. 标记渲染条目为脏，下次 getRenderEntries() 时重建
    entriesDirty_ = true;
}

// ============================================================
// 渲染条目查询
// ============================================================

const std::vector<RenderSystem::RenderEntry>& RenderSystem::getRenderEntries() const {
    if (!entriesDirty_) return renderEntries_;

    renderEntries_.clear();
    renderEntries_.reserve(entityModels_.size());

    for (const auto& [entity, loaded] : entityModels_) {
        if (!loaded.visible || loaded.model == nullptr) continue;

        // 检查实体是否仍然有效
        if (!registry_.valid(entity)) continue;

        // 获取模型矩阵
        const auto* transformComp = registry_.try_get<ecs::TransformComponent>(entity);
        glm::mat4 modelMatrix = transformComp
            ? transformComp->getModelMatrix()
            : glm::mat4(1.0f);

        RenderEntry entry;
        entry.model = loaded.model;
        entry.modelMatrix = modelMatrix;
        entry.descriptorSet = loaded.descriptorSet;
        entry.visible = true;

        renderEntries_.push_back(entry);
    }

    entriesDirty_ = false;
    return renderEntries_;
}

// ============================================================
// 实体管理
// ============================================================

void RenderSystem::setEntityVisible(entt::entity entity, bool visible) {
    auto it = entityModels_.find(entity);
    if (it != entityModels_.end()) {
        it->second.visible = visible;
        entriesDirty_ = true;
    }
}

bool RenderSystem::isEntityVisible(entt::entity entity) const {
    auto it = entityModels_.find(entity);
    return it != entityModels_.end() && it->second.visible;
}

bool RenderSystem::hasEntity(entt::entity entity) const {
    return entityModels_.find(entity) != entityModels_.end();
}

GLTFModel* RenderSystem::getModelForEntity(entt::entity entity) const {
    auto it = entityModels_.find(entity);
    return it != entityModels_.end() ? it->second.model : nullptr;
}

void RenderSystem::syncEntity(entt::entity entity) {
    auto it = entityModels_.find(entity);
    if (it != entityModels_.end()) {
        syncTransform(entity, it->second);
        entriesDirty_ = true;
    } else if (registry_.valid(entity)) {
        // 实体未跟踪但有效，尝试加载
        const auto* renderComp = registry_.try_get<ecs::RenderComponent>(entity);
        if (renderComp) {
            loadModelForEntity(entity, *renderComp);
        }
    }
}

// ============================================================
// 清理
// ============================================================

void RenderSystem::cleanup() {
    entityModels_.clear();
    renderEntries_.clear();
    knownEntities_.clear();
    entriesDirty_ = true;
}

// ============================================================
// 私有辅助方法
// ============================================================

void RenderSystem::loadModelForEntity(entt::entity entity,
                                      const ecs::RenderComponent& renderComp) {
    if (renderComp.modelPath.empty()) return;
    if (!renderComp.visible) return;

    // 通过 ModelCache 加载或获取模型
    GLTFModel* model = modelCache_->getOrLoadModel(renderComp.modelPath);
    if (!model) return;

    LoadedEntity loaded;
    loaded.modelPath = renderComp.modelPath;
    loaded.model = model;
    loaded.visible = renderComp.visible;

    // 创建模型描述符集（使用模型第一个纹理）
    loaded.descriptorSet = modelCache_->createModelDescriptorSet(
        model, textureDSLayout_, descriptorPool_);

    // 设置 GLTFModel 的基本变换
    model->setPosition(renderComp.position);
    model->setScale(renderComp.scale);
    // RenderComponent::rotation 是欧拉角度数（pitch/yaw/roll 度数）
    model->setRotation(renderComp.rotation.x, renderComp.rotation.y, renderComp.rotation.z);

    entityModels_[entity] = std::move(loaded);
    entriesDirty_ = true;
}

void RenderSystem::syncTransform(entt::entity entity, LoadedEntity& loaded) {
    const auto* transformComp = registry_.try_get<ecs::TransformComponent>(entity);
    if (!transformComp) return;

    loaded.model->setPosition(transformComp->position);
    // 从 TransformComponent 的四元数转换为欧拉角度数
    glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(transformComp->rotation));
    loaded.model->setRotation(eulerDeg.x, eulerDeg.y, eulerDeg.z);
    loaded.model->setScale(transformComp->scale);
}

void RenderSystem::removeEntity(entt::entity entity) {
    auto it = entityModels_.find(entity);
    if (it != entityModels_.end()) {
        // 模型缓存由 ModelCache 管理，不在此处销毁
        entityModels_.erase(it);
        entriesDirty_ = true;
    }
}

} // namespace owengine
