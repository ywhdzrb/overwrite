// 渲染帧实现 — 6 个阶段方法 + drawFrame 编排主干
// 拆自 renderer.cpp（原 drawFrame ~520 行，现主干 ~50 行 + 6 个阶段方法）
#include "core/renderer.hpp"
#include "core/camera.hpp"
#include "core/game_session.hpp"
#include "ecs/i_game_world.hpp"
#include "utils/logger.hpp"
#include "utils/vk_result.hpp"

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace owengine {

namespace {
// 简易阶段计时器：记录各阶段耗时到静态变量，每秒汇总打印一次
struct PhaseTimer {
    // 阶段索引
    enum Phase {
        PHASE_BEGIN_FRAME,    // beginFrame (fence wait 外的开销)
        PHASE_SHADOW_PASS,    // recordShadowPass
        PHASE_BEGIN_RP,       // beginMainRenderPass
        PHASE_DAY_NIGHT,      // updateDayNightCycle
        PHASE_OPAQUE,         // renderOpaqueGeometry
        PHASE_CLOUD_IMGUI,    // renderCloudAndImGui
        PHASE_SUBMIT,         // submitFrame
        PHASE_COUNT
    };
    static const char* names[PHASE_COUNT];
    double accum[PHASE_COUNT] = {};
    int frameCount = 0;
    
    void record(Phase p, double ms) { accum[p] += ms; }
    
    void flush() {
        if (frameCount == 0) return;
        Logger::info(std::string("[DrawProfile] ") +
            names[0] + "=" + std::to_string((int)(accum[0]/frameCount)) + " " +
            names[1] + "=" + std::to_string((int)(accum[1]/frameCount)) + " " +
            names[2] + "=" + std::to_string((int)(accum[2]/frameCount)) + " " +
            names[3] + "=" + std::to_string((int)(accum[3]/frameCount)) + " " +
            names[4] + "=" + std::to_string((int)(accum[4]/frameCount)) + " " +
            names[5] + "=" + std::to_string((int)(accum[5]/frameCount)) + " " +
            names[6] + "=" + std::to_string((int)(accum[6]/frameCount)) + "ms"
        );
        for (auto& a : accum) a = 0.0;
        frameCount = 0;
    }
};
const char* PhaseTimer::names[PhaseTimer::PHASE_COUNT] = {
    "BF", "Shd", "RP", "DN", "Opa", "Cld", "Sub"
};
static PhaseTimer s_pt;
} // anonymous namespace

/**
 * @brief Phase 0: 帧同步 — 等待 fence → 获取交换链图像 → 重置 fence → 开始命令缓冲
 * @return false 表示窗口应跳过此帧（交换链过期或窗口关闭）
 */
bool Renderer::beginFrame(uint32_t& imageIndex, VkCommandBuffer& commandBuffer) {
    auto fenceT0 = std::chrono::high_resolution_clock::now();
    vkWaitForFences(vulkanDevice_->getDevice(), 1, &syncObjects_->getInFlightFences()[currentFrame_], VK_TRUE, UINT64_MAX);
    profFenceWaitMs_ = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - fenceT0).count();

    VkResult result = vkAcquireNextImageKHR(
        vulkanDevice_->getDevice(), swapchain_->getSwapchain(), UINT64_MAX,
        syncObjects_->getImageAvailableSemaphores()[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (glfwWindowShouldClose(window_)) return false;
        throw std::runtime_error(std::string("failed to acquire swap chain image! ") + vkResultToString(result));
    }

    vkResetFences(vulkanDevice_->getDevice(), 1, &syncObjects_->getInFlightFences()[currentFrame_]);

    // 更新 FSR1 输出描述符（必须在 cmd buffer 录制前）
    if (fsr1Pass_ && fsrScale_ < 1.0f && imageIndex < swapchain_->getImageViews().size()) {
        fsr1Pass_->updateOutputDescriptor(swapchain_->getImageViews()[imageIndex]);
    }

    // 重置并开始命令缓冲
    vkResetCommandBuffer(commandBuffers_->getCommandBuffers()[currentFrame_], 0);
    commandBuffer = commandBuffers_->getCommandBuffers()[currentFrame_];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    VkResult _vrBegin = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (_vrBegin != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to begin recording command buffer! ") + vkResultToString(_vrBegin));
    }
    return true;
}

/**
 * @brief Phase 1: 阴影渲染通道
 *
 * 从光源视角渲染场景深度到阴影贴图，供主渲染通道采样。
 * 渲染顺序：地形 → 静态模型 → 动态模型 → 本地玩家 → 远程玩家 → ECS 实体
 */
void Renderer::recordShadowPass(VkCommandBuffer cmd, Camera* cam) {
    if (!lightManager_->isShadowInitialized() || !cam) return;

    glm::vec3 sunDir = gameConfig_.renderer.sunDirection;
    lightManager_->updateSunShadowMatrix(sunDir, cam->getPosition());

    lightManager_->getShadowMapper()->beginShadowPass(cmd);

    VkPipelineLayout shadowPL = lightManager_->getShadowMapper()->getPipelineLayout();
    glm::mat4 lightView = lightManager_->getShadowMapper()->getLightView();
    glm::mat4 lightProj = lightManager_->getShadowMapper()->getLightProj();

    terrainRenderer_->render(cmd, shadowPL, lightView, lightProj);
    if (modelRenderer_) modelRenderer_->render(cmd, shadowPL, lightView, lightProj);

    for (auto& [id, model] : models_) {
        if (model && model->getMeshCount() > 0) {
            model->render(cmd, shadowPL, lightView, lightProj, model->getModelMatrix());
        }
    }

    // 树木投射阴影（使用实例化管线，所有树一批绘制）
    if (treeSystem_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          lightManager_->getShadowMapper()->getInstancedPipeline());
        treeSystem_->renderShadow(cmd, shadowPL, lightView, lightProj);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          lightManager_->getShadowMapper()->getPipeline());
    }
    // 石头投射阴影
    if (stoneSystem_) stoneSystem_->renderShadow(cmd, shadowPL, lightView, lightProj);

    if (gameSession_) {
        GLTFModel* playerModel = gameSession_->getActivePlayerModel();
        if (playerModel && playerModel->getMeshCount() > 0) {
            playerModel->render(cmd, shadowPL, lightView, lightProj, playerModel->getModelMatrix());
        }

        for (const auto& [clientId, rp] : gameSession_->getRemotePlayerModels()) {
            GLTFModel* activeModel = rp.wasMoving ? rp.walkModel.get() : rp.idleModel.get();
            if (activeModel && activeModel->getMeshCount() > 0) {
                activeModel->render(cmd, shadowPL, lightView, lightProj, activeModel->getModelMatrix());
            }
        }

        auto* renderSys = gameSession_->getRenderSystem();
        if (renderSys) {
            for (const auto& entry : renderSys->getRenderEntries()) {
                if (!entry.model || !entry.visible) continue;
                entry.model->render(cmd, shadowPL, lightView, lightProj, entry.modelMatrix);
            }
        }
    }

    lightManager_->getShadowMapper()->endShadowPass(cmd);
}

/**
 * @brief Phase 2a: 昼夜循环更新
 *
 * 计算太阳位置（XZ 圆轨 + Y 正弦波），更新方向光强度、环境光、
 * 草丛光照、阴影强度和体积云昼夜因子。
 * 昼夜关闭时使用配置中的固定方向。
 */
void Renderer::updateDayNightCycle() {
    if (!lightManager_) return;

    if (gameConfig_.renderer.dayNightCycle) {
        float angle = (dayTime_ / dayCyclePeriod_) * 2.0f * glm::pi<float>();
        glm::vec3 sunDir(cos(angle), sin(angle), sin(angle * 0.7f) * 0.2f);
        sunDir = glm::normalize(sunDir);
        gameConfig_.renderer.sunDirection = sunDir;

        float elevation = sunDir.y;
        float dayFactor = glm::clamp((elevation + 0.2f) / 0.5f, 0.0f, 1.0f);

        if (cloudSystem_) cloudSystem_->setDayFactor(dayFactor);

        Light* sunLight = lightManager_->getLightByName("sun");
        if (sunLight) {
            sunLight->setDirection(-sunDir);
            sunLight->setIntensity(dayFactor);
            lightManager_->setAmbientIntensity(0.3f * dayFactor + 0.05f);
        }

        if (grassSystem_) {
            grassSystem_->setGlobalLightDir(sunDir);
            grassSystem_->setLightIntensity(dayFactor);
            grassSystem_->setAmbientColor(lightManager_->getAmbient());
        }

        float shadowStr = glm::smoothstep(0.0f, 0.6f, dayFactor) * 0.7f;
        lightManager_->setShadowIntensity(shadowStr);
    } else {
        glm::vec3 fixedDir = gameConfig_.renderer.sunDirection;
        if (cloudSystem_) cloudSystem_->setDayFactor(1.0f);

        Light* sunLight = lightManager_->getLightByName("sun");
        if (sunLight) {
            sunLight->setDirection(-fixedDir);
            sunLight->setIntensity(1.0f);
            lightManager_->setAmbientIntensity(0.5f);
        }
        if (grassSystem_) {
            grassSystem_->setGlobalLightDir(fixedDir);
            grassSystem_->setLightIntensity(1.0f);
            grassSystem_->setAmbientColor(lightManager_->getAmbient());
        }
        lightManager_->setShadowIntensity(0.6f);
    }
}

/**
 * @brief Phase 2b: 开始主渲染通道
 *
 * 绑定主图形管线，设置视口/裁剪，返回 Camera 指针。
 * 返回 nullptr 表示无相机可用，调用方需跳过此帧渲染。
 */
Camera* Renderer::beginMainRenderPass(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = renderPass_->getRenderPass();
    rpInfo.framebuffer = framebuffers_->getFramebuffers()[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = swapchain_->getExtent();

    std::vector<VkClearValue> clearValues;
    clearValues.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}}});
    clearValues.push_back({{1.0f, 0}});
    if (msaaSamples_ > VK_SAMPLE_COUNT_1_BIT) {
        clearValues.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}}});
    }
    rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->getPipeline());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    VkExtent2D vpExt = fsr1Pass_ ? fsr1Pass_->getRenderExtent() : swapchain_->getExtent();
    viewport.width = (float)vpExt.width;
    viewport.height = (float)vpExt.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vpExt;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    Camera* cam = gameSession_ ? gameSession_->getCamera() : nullptr;
    if (!cam) {
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            Logger::warning("[Renderer] 无相机时结束命令缓冲失败");
        }
    }
    return cam;
}

/**
 * @brief Phase 2c: 渲染所有不透明几何体
 *
 * 顺序：天空盒 → 绑定描述符集 → 地形 → 静态模型 → 玩家 → 动态模型 →
 *       ECS 实体 → 树木/石头/草丛 → 远程玩家
 * 前置条件：主渲染通道已开始（beginMainRenderPass），光照/阴影描述符未绑定
 */
void Renderer::renderOpaqueGeometry(VkCommandBuffer cmd, Camera* cam) {
    // 天空盒（使用独立管线）
    if (skyboxRenderer_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline_->getPipeline());
        skyboxRenderer_->render(cmd, skyboxRenderer_->getPipelineLayout(),
                             cam->getViewMatrix(), cam->getProjectionMatrix(),
                             gameConfig_.renderer.sunDirection);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->getPipeline());
    }

    // 更新并绑定光照/阴影描述符集
    updateLightUniformBuffer();

    if (lightManager_->isShadowInitialized()) {
        lightManager_->getShadowMapper()->updateUniformBuffer(currentFrame_);
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphicsPipeline_->getPipelineLayout(), 0, 1, &textureDescriptorSet_, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphicsPipeline_->getPipelineLayout(), 1, 1, &lightDescriptorSet_, 0, nullptr);

    if (lightManager_->isShadowInitialized()) {
        VkDescriptorSet frameShadowDS = lightManager_->getShadowMapper()->getDescriptorSet(currentFrame_);
        if (frameShadowDS != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    graphicsPipeline_->getPipelineLayout(), 2, 1, &frameShadowDS, 0, nullptr);
        }
    }

    // 地形
    terrainRenderer_->render(cmd, graphicsPipeline_->getPipelineLayout(),
                          cam->getViewMatrix(), cam->getProjectionMatrix());

    // OBJ 静态模型
    if (modelRenderer_) {
        modelRenderer_->render(cmd, graphicsPipeline_->getPipelineLayout(),
                            cam->getViewMatrix(), cam->getProjectionMatrix());
    }

    // 本地玩家模型
    if (gameSession_) {
        GLTFModel* playerModel = gameSession_->getActivePlayerModel();
        VkDescriptorSet playerDescSet = gameSession_->getActivePlayerDescriptorSet();
        if (playerModel && playerModel->getMeshCount() > 0) {
            if (playerDescSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        graphicsPipeline_->getPipelineLayout(), 0, 1, &playerDescSet, 0, nullptr);
            }
            playerModel->render(cmd, graphicsPipeline_->getPipelineLayout(),
                              cam->getViewMatrix(), cam->getProjectionMatrix(),
                              playerModel->getModelMatrix());
        }
    }

    // 动态加载的静态模型（视锥体裁剪）
    for (auto& [id, model] : models_) {
        if (!model || model->getMeshCount() == 0) continue;
        auto bbox = model->getBoundingBox();
        glm::vec3 worldMin = model->getPosition() + bbox.first * model->getScale();
        glm::vec3 worldMax = model->getPosition() + bbox.second * model->getScale();
        glm::vec3 modelCenter = (worldMin + worldMax) * 0.5f;
        if (glm::length(modelCenter - cam->getPosition()) > MODEL_CULLING_DISTANCE) continue;
        if (!cam->getFrustum().isAABBInside(worldMin, worldMax)) continue;

        auto it = modelDescriptorSets_.find(id);
        if (it != modelDescriptorSets_.end() && it->second != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    graphicsPipeline_->getPipelineLayout(), 0, 1, &it->second, 0, nullptr);
        }
        model->render(cmd, graphicsPipeline_->getPipelineLayout(),
                     cam->getViewMatrix(), cam->getProjectionMatrix(),
                     model->getModelMatrix());
    }

    // ECS 驱动的实体
    if (gameSession_) {
        auto* renderSys = gameSession_->getRenderSystem();
        if (renderSys) {
            for (const auto& entry : renderSys->getRenderEntries()) {
                if (!entry.model || !entry.visible) continue;
                if (entry.descriptorSet != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            graphicsPipeline_->getPipelineLayout(),
                                            0, 1, &entry.descriptorSet, 0, nullptr);
                }
                entry.model->render(cmd, graphicsPipeline_->getPipelineLayout(),
                                  cam->getViewMatrix(), cam->getProjectionMatrix(),
                                  entry.modelMatrix);
            }
        }
    }

    // 树木/石头/草丛
    treeSystem_->render(cmd, graphicsPipeline_->getPipelineLayout(), *cam,
                        totalTime_, gameConfig_.tree.windStrength);
    if (stoneSystem_) stoneSystem_->render(cmd, graphicsPipeline_->getPipelineLayout(), *cam);
    // 绑定水下草地纹理后渲染草
    if (grassWaterDescriptorSet_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                graphicsPipeline_->getPipelineLayout(), 0, 1,
                                &grassWaterDescriptorSet_, 0, nullptr);
    }
    if (grassSystem_) grassSystem_->render(cmd, *cam);
    // 恢复主纹理描述符集
    if (grassWaterDescriptorSet_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                graphicsPipeline_->getPipelineLayout(), 0, 1,
                                &textureDescriptorSet_, 0, nullptr);
    }

    // 远程玩家模型
    if (gameSession_) {
        for (const auto& [clientId, rp] : gameSession_->getRemotePlayerModels()) {
            GLTFModel* activeModel = rp.wasMoving ? rp.walkModel.get() : rp.idleModel.get();
            if (activeModel && activeModel->getMeshCount() > 0) {
                activeModel->render(cmd, graphicsPipeline_->getPipelineLayout(),
                            cam->getViewMatrix(), cam->getProjectionMatrix(),
                            activeModel->getModelMatrix());
            }
        }
    }

    // 水面（在所有不透明物体之后渲染，半透明混合叠加在深度缓冲之上）
    if (waterRenderer_) {
        waterRenderer_->render(cmd, cam->getViewMatrix(),
                               cam->getProjectionMatrix(), cam->getPosition());
        // 恢复主图形管线，供后续渲染（云/ImGui 等）使用
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->getPipeline());
    }
}

/**
 * @brief Phase 2d: 结束主渲染通道 + 云合成 + ImGui
 *
 * 半分辨率云路径：结束主 RP → barrier → 渲染半分辨率云 → 合成 RP（上采样+ImGui）
 * 全分辨率云路径：全分辨率云 → ImGui → 结束主 RP
 */
void Renderer::renderCloudAndImGui(VkCommandBuffer cmd, Camera* cam, uint32_t imageIndex) {
    bool useHalfRes = cloudSystem_ && cloudSystem_->isHalfResEnabled()
                      && msaaSamples_ <= VK_SAMPLE_COUNT_1_BIT;

    if (useHalfRes) {
        vkCmdEndRenderPass(cmd);

        // 过渡 swapchain 图像布局供合成通道使用
        VkImageMemoryBarrier swapchainBarrier{};
        swapchainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapchainBarrier.image = swapchain_->getImages()[imageIndex];
        swapchainBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapchainBarrier.srcAccessMask = 0;
        swapchainBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &swapchainBarrier);

        // 渲染半分辨率云
        cloudSystem_->renderHalfRes(cmd, *cam, gameConfig_.renderer.sunDirection);

        // 合成渲染通道（半分辨率云上采样 + ImGui）
        VkRenderPassBeginInfo compositeRpInfo{};
        compositeRpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        compositeRpInfo.renderPass = cloudComposite_.renderPass;
        compositeRpInfo.framebuffer = cloudComposite_.framebuffers[imageIndex];
        compositeRpInfo.renderArea.offset = {0, 0};
        compositeRpInfo.renderArea.extent = swapchain_->getExtent();

        std::vector<VkClearValue> compositeClear(2);
        compositeClear[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        compositeClear[1].depthStencil = {1.0f, 0};
        compositeRpInfo.clearValueCount = static_cast<uint32_t>(compositeClear.size());
        compositeRpInfo.pClearValues = compositeClear.data();

        vkCmdBeginRenderPass(cmd, &compositeRpInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cloudComposite_.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                cloudComposite_.pipelineLayout, 0, 1,
                                &cloudComposite_.ds, 0, nullptr);
        vkCmdDraw(cmd, 4, 1, 0, 0);

        imguiManager_->render(cmd);
        vkCmdEndRenderPass(cmd);
    } else {
        // 全分辨率云路径
        if (cloudSystem_ && cloudSystem_->isInitialized()) {
            cloudSystem_->render(cmd, *cam, gameConfig_.renderer.sunDirection);
        }
        imguiManager_->render(cmd);
        vkCmdEndRenderPass(cmd);
    }
}

/**
 * @brief Phase 3: FSR1 上采样 + 提交 + 呈现
 *
 * 结束命令缓冲 → FSR1 dispatch（可选）→ vkQueueSubmit → vkQueuePresentKHR
 * 处理 VK_ERROR_OUT_OF_DATE_KHR 交换链重建和帧索引推进。
 */
void Renderer::submitFrame(VkCommandBuffer cmd, uint32_t imageIndex) {
    // FSR1 上采样（仅 fsrScale_ < 1.0 时生效）
    if (fsr1Pass_ && fsrScale_ < 1.0f) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image = swapchain_->getImages()[imageIndex];
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

        fsr1Pass_->dispatch(cmd);

        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = 0;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    VkResult _vrEnd = vkEndCommandBuffer(cmd);
    if (_vrEnd != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to record command buffer! ") + vkResultToString(_vrEnd));
    }

    // 提交命令缓冲
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {syncObjects_->getImageAvailableSemaphores()[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_->getCommandBuffers()[currentFrame_];

    VkSemaphore signalSemaphores[] = {syncObjects_->getRenderFinishedSemaphores()[currentFrame_]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult = vkQueueSubmit(vulkanDevice_->getGraphicsQueue(), 1, &submitInfo,
                                          syncObjects_->getInFlightFences()[currentFrame_]);
    if (submitResult != VK_SUCCESS) {
        Logger::error("[Renderer] vkQueueSubmit failed with error code: " + std::to_string(submitResult));
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            Logger::error("[Renderer] VK_ERROR_DEVICE_LOST");
        } else if (submitResult == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            Logger::error("[Renderer] VK_ERROR_OUT_OF_DEVICE_MEMORY");
        } else if (submitResult == VK_ERROR_OUT_OF_HOST_MEMORY) {
            Logger::error("[Renderer] VK_ERROR_OUT_OF_HOST_MEMORY");
        }
        if (glfwWindowShouldClose(window_)) {
            VkSubmitInfo emptySubmit{};
            emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            vkQueueSubmit(vulkanDevice_->getGraphicsQueue(), 1, &emptySubmit,
                          syncObjects_->getInFlightFences()[currentFrame_]);
            return;
        }
        throw std::runtime_error(std::string("failed to submit draw command buffer! ") + vkResultToString(submitResult));
    }

    // 呈现
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapchain_->getSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(vulkanDevice_->getPresentQueue(), &presentInfo);

    if (fsr1Pass_ && fsrScale_ < 1.0f) fsr1Pass_->advanceFrame();

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        if (glfwWindowShouldClose(window_)) return;
        throw std::runtime_error(std::string("failed to present swap chain image! ") + vkResultToString(result));
    }

    currentFrame_ = (currentFrame_ + 1) % syncObjects_->getMaxFramesInFlight();
}

// ==================== drawFrame 主干 ====================

/**
 * @brief 渲染帧主干
 *
 * 编排 3 个阶段的顺序执行：
 *   Phase 0: 帧同步 → 获取交换链图像 → 开始命令缓冲
 *   Phase 1: 阴影渲染通道（光源视角深度）
 *   Phase 2: 主渲染通道（昼夜更新 → 天空盒 → 几何体 → 云/ImGui）
 *   Phase 3: FSR1 → 提交 → 呈现
 */
void Renderer::drawFrame() {
    auto pf0 = std::chrono::high_resolution_clock::now();
    uint32_t imageIndex;
    VkCommandBuffer commandBuffer;
    if (!beginFrame(imageIndex, commandBuffer)) return;
    s_pt.record(PhaseTimer::PHASE_BEGIN_FRAME,
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pf0).count());

    Camera* cam = gameSession_ ? gameSession_->getCamera() : nullptr;

    auto pf1 = std::chrono::high_resolution_clock::now();
    recordShadowPass(commandBuffer, cam);
    s_pt.record(PhaseTimer::PHASE_SHADOW_PASS,
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pf1).count());

    auto pf2 = std::chrono::high_resolution_clock::now();
    Camera* mainCam = beginMainRenderPass(commandBuffer, imageIndex);
    if (!mainCam) return;
    s_pt.record(PhaseTimer::PHASE_BEGIN_RP,
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pf2).count());

    auto pf3 = std::chrono::high_resolution_clock::now();
    updateDayNightCycle();
    s_pt.record(PhaseTimer::PHASE_DAY_NIGHT,
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pf3).count());

    auto pf4 = std::chrono::high_resolution_clock::now();
    renderOpaqueGeometry(commandBuffer, mainCam);
    s_pt.record(PhaseTimer::PHASE_OPAQUE,
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pf4).count());

    auto pf5 = std::chrono::high_resolution_clock::now();
    renderCloudAndImGui(commandBuffer, mainCam, imageIndex);
    s_pt.record(PhaseTimer::PHASE_CLOUD_IMGUI,
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pf5).count());

    auto pf6 = std::chrono::high_resolution_clock::now();
    submitFrame(commandBuffer, imageIndex);
    s_pt.record(PhaseTimer::PHASE_SUBMIT,
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pf6).count());

    // 每秒汇总打印阶段耗时
    s_pt.frameCount++;
    static auto s_lastPt = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    if (std::chrono::duration<double>(now - s_lastPt).count() >= 1.0) {
        s_pt.flush();
        s_lastPt = now;
    }
}

} // namespace owengine
