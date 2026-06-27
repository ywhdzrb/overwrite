// 渲染帧实现 — 阴影渲染通道 + 主渲染通道 + 半分辨率云 + FSR1 上采样 + 提交呈现
// 拆自 renderer.cpp（drawFrame 方法约 520 行，独立文件便于维护）
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

// 每帧渲染管线核心入口
// 按阶段依次执行：等待 fence → 获取交换链图像 → 录制阴影 pass → 录制主渲染 pass → 半分辨率云/FSR1 → 提交呈现
// 前置条件：initVulkan 已完成，所有渲染子系统已初始化
void Renderer::drawFrame() {
    auto fenceT0 = std::chrono::high_resolution_clock::now();
    vkWaitForFences(vulkanDevice_->getDevice(), 1, &syncObjects_->getInFlightFences()[currentFrame_], VK_TRUE, UINT64_MAX);
    profFenceWaitMs_ = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - fenceT0).count();

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        vulkanDevice_->getDevice(),
        swapchain_->getSwapchain(),
        UINT64_MAX,
        syncObjects_->getImageAvailableSemaphores()[currentFrame_],
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (glfwWindowShouldClose(window_)) {
            return;
        }
        throw std::runtime_error(std::string("failed to acquire swap chain image! ") + vkResultToString(result));
    }

    vkResetFences(vulkanDevice_->getDevice(), 1, &syncObjects_->getInFlightFences()[currentFrame_]);

    // 更新 FSR1 输出描述符指向当前 swapchain_ 图像（必须在 cmd buffer 录制前）
    if (fsr1Pass_ && fsrScale_ < 1.0f && imageIndex < swapchain_->getImageViews().size()) {
        fsr1Pass_->updateOutputDescriptor(swapchain_->getImageViews()[imageIndex]);
    }

    vkResetCommandBuffer(commandBuffers_->getCommandBuffers()[currentFrame_], 0);

    // 手动记录命令缓冲
    VkCommandBuffer commandBuffer = commandBuffers_->getCommandBuffers()[currentFrame_];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    VkResult _vrBegin = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (_vrBegin != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to begin recording command buffer! ") + vkResultToString(_vrBegin));
    }

    // 获取摄像机（阴影 pass 和主渲染 pass 共用）
    Camera* shadowCam = gameSession_ ? gameSession_->getCamera() : nullptr;

    // ================================================================
    // 阶段 1：阴影渲染通道
    // 从光源视角渲染场景深度到阴影贴图，供主渲染通道采样
    // ================================================================
    if (lightManager_->isShadowInitialized() && shadowCam) {
        // 更新光源 VP 矩阵（使用当前太阳方向和相机位置）
        glm::vec3 sunDir = gameConfig_.renderer.sunDirection;
        lightManager_->updateSunShadowMatrix(sunDir, shadowCam->getPosition());

        // 开始阴影渲染通道（清除深度、绑定阴影管线、设置阴影视口）
        lightManager_->getShadowMapper()->beginShadowPass(commandBuffer);

        // 渲染所有投射阴影的物体（使用光源 VP 矩阵）
        VkPipelineLayout shadowPL = lightManager_->getShadowMapper()->getPipelineLayout();
        glm::mat4 lightView = lightManager_->getShadowMapper()->getLightView();
        glm::mat4 lightProj = lightManager_->getShadowMapper()->getLightProj();

        // 渲染地形（主要阴影投射体）
        terrainRenderer_->render(commandBuffer, shadowPL, lightView, lightProj);

        // 渲染静态 OBJ 模型（如房屋、建筑等）
        if (modelRenderer_) {
            modelRenderer_->render(commandBuffer, shadowPL, lightView, lightProj);
        }

        // 渲染动态加载的静态模型
        for (auto& [id, model] : models_) {
            if (model && model->getMeshCount() > 0) {
                model->render(commandBuffer, shadowPL, lightView, lightProj,
                              model->getModelMatrix());
            }
        }

        // 渲染玩家模型（投射阴影）
        if (gameSession_) {
            GLTFModel* playerModel = gameSession_->getActivePlayerModel();
            if (playerModel && playerModel->getMeshCount() > 0) {
                playerModel->render(commandBuffer, shadowPL, lightView, lightProj,
                                    playerModel->getModelMatrix());
            }

            // 渲染远程玩家模型
            for (const auto& [clientId, rp] : gameSession_->getRemotePlayerModels()) {
                GLTFModel* activeModel = rp.wasMoving ? rp.walkModel.get() : rp.idleModel.get();
                if (activeModel && activeModel->getMeshCount() > 0) {
                    activeModel->render(commandBuffer, shadowPL, lightView, lightProj,
                                        activeModel->getModelMatrix());
                }
            }

            // 渲染 ECS 驱动的实体（如资源、建筑等）
            auto* renderSys = gameSession_->getRenderSystem();
            if (renderSys) {
                const auto& entries = renderSys->getRenderEntries();
                for (const auto& entry : entries) {
                    if (!entry.model || !entry.visible) continue;
                    entry.model->render(commandBuffer, shadowPL, lightView, lightProj,
                                        entry.modelMatrix);
                }
            }
        }

        // 注意：树木和石头暂不参与阴影 pass
        // TODO(阴影): 为 TreeSystem/StoneSystem 添加接受自定义 view/proj 的 render 重载

        // 结束阴影渲染通道（阴影贴图自动转换为 SHADER_READ_ONLY_OPTIMAL）
        lightManager_->getShadowMapper()->endShadowPass(commandBuffer);
    }

    // ================================================================
    // 阶段 2：主渲染通道
    // ================================================================
    VkRenderPassBeginInfo renderPass_Info{};
    renderPass_Info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPass_Info.renderPass = renderPass_->getRenderPass();
    renderPass_Info.framebuffer = framebuffers_->getFramebuffers()[imageIndex];
    renderPass_Info.renderArea.offset = {0, 0};
    renderPass_Info.renderArea.extent = swapchain_->getExtent();

    // 清除颜色和深度缓冲
    std::vector<VkClearValue> clearValues;
    clearValues.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}}});
    clearValues.push_back({{1.0f, 0}});

    // 如果使用MSAA，需要为解析附件添加清除值
    if (msaaSamples_ > VK_SAMPLE_COUNT_1_BIT) {
        clearValues.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}}});
    }

    renderPass_Info.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPass_Info.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPass_Info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->getPipeline());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    {
        VkExtent2D vpExt = fsr1Pass_ ? fsr1Pass_->getRenderExtent() : swapchain_->getExtent();
        viewport.width = (float)vpExt.width;
        viewport.height = (float)vpExt.height;
    }
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = fsr1Pass_ ? fsr1Pass_->getRenderExtent() : swapchain_->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // 从 GameSession 获取摄像机（渲染所需视口/投影矩阵）
    Camera* cam = gameSession_ ? gameSession_->getCamera() : nullptr;
    if (!cam) {
        vkCmdEndRenderPass(commandBuffer);
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            Logger::warning("[Renderer] 无相机时结束命令缓冲失败");
        }
        return;
    }

    // === 更新昼夜循环太阳方向 ===
    if (gameConfig_.renderer.dayNightCycle) {
        // 太阳在 XZ 平面圆形轨道上运动，Y 轴为正弦波（-1~+1）
        float angle = (dayTime_ / dayCyclePeriod_) * 2.0f * glm::pi<float>();
        glm::vec3 sunDir(
            cos(angle),
            sin(angle),
            sin(angle * 0.7f) * 0.2f
        );
        sunDir = glm::normalize(sunDir);
        gameConfig_.renderer.sunDirection = sunDir;

        // 计算白天因子：-0.2(深夜)~+0.3(白天) 映射到 0~1
        float elevation = sunDir.y;
        float dayFactor = glm::clamp((elevation + 0.2f) / 0.5f, 0.0f, 1.0f);

        // 传递昼夜因子到体积云系统
        if (cloudSystem_) cloudSystem_->setDayFactor(dayFactor);

        // 更新场景平行光方向和强度（使用 light-to-scene 方向）
        if (lightManager_) {
            Light* sunLight = lightManager_->getLightByName("sun");
            if (sunLight) {
                sunLight->setDirection(-sunDir);
                sunLight->setIntensity(dayFactor);
                lightManager_->setAmbientIntensity(0.3f * dayFactor + 0.05f);
            }
        }

        // 更新草丛着色器光照
        if (grassSystem_) {
            grassSystem_->setGlobalLightDir(sunDir);
            grassSystem_->setLightIntensity(dayFactor);
            grassSystem_->setAmbientColor(lightManager_->getAmbient());
        }

        // 阴影强度随昼夜因子平滑过渡
        float shadowStr = glm::smoothstep(0.0f, 0.6f, dayFactor) * 0.7f;
        lightManager_->setShadowIntensity(shadowStr);
    } else {
        // 昼夜循环关闭：使用配置文件中的固定方向
        glm::vec3 fixedDir = gameConfig_.renderer.sunDirection;
        if (cloudSystem_) cloudSystem_->setDayFactor(1.0f);
        if (lightManager_) {
            Light* sunLight = lightManager_->getLightByName("sun");
            if (sunLight) {
                sunLight->setDirection(-fixedDir);
                sunLight->setIntensity(1.0f);
                lightManager_->setAmbientIntensity(0.5f);
            }
        }
        if (grassSystem_) {
            grassSystem_->setGlobalLightDir(fixedDir);
            grassSystem_->setLightIntensity(1.0f);
            if (lightManager_) grassSystem_->setAmbientColor(lightManager_->getAmbient());
        }
        lightManager_->setShadowIntensity(0.6f);
    }

    // 先渲染天空盒（背景，程序化渐变色+昼夜切换）
    if (skyboxRenderer_) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline_->getPipeline());
        skyboxRenderer_->render(commandBuffer, skyboxRenderer_->getPipelineLayout(),
                             cam->getViewMatrix(), cam->getProjectionMatrix(),
                             gameConfig_.renderer.sunDirection);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->getPipeline());
    }

    updateLightUniformBuffer();

    // 更新当前帧阴影 uniform 缓冲
    if (lightManager_->isShadowInitialized()) {
        lightManager_->getShadowMapper()->updateUniformBuffer(currentFrame_);
    }

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           graphicsPipeline_->getPipelineLayout(), 0, 1, &textureDescriptorSet_, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           graphicsPipeline_->getPipelineLayout(), 1, 1, &lightDescriptorSet_, 0, nullptr);

    // 绑定当前帧的阴影描述符集（set=2）
    if (lightManager_->isShadowInitialized()) {
        VkDescriptorSet frameShadowDS = lightManager_->getShadowMapper()->getDescriptorSet(currentFrame_);
        if (frameShadowDS != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   graphicsPipeline_->getPipelineLayout(), 2, 1, &frameShadowDS, 0, nullptr);
        }
    }

    // 渲染地形
    terrainRenderer_->render(commandBuffer, graphicsPipeline_->getPipelineLayout(),
                          cam->getViewMatrix(), cam->getProjectionMatrix());

    // 渲染 OBJ 模型
    if (modelRenderer_) {
        modelRenderer_->render(commandBuffer, graphicsPipeline_->getPipelineLayout(),
                            cam->getViewMatrix(), cam->getProjectionMatrix());
    }

    // 渲染玩家模型（从 GameSession 获取）
    if (gameSession_) {
        GLTFModel* playerModel = gameSession_->getActivePlayerModel();
        VkDescriptorSet playerDescSet = gameSession_->getActivePlayerDescriptorSet();
        if (playerModel && playerModel->getMeshCount() > 0) {
            if (playerDescSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       graphicsPipeline_->getPipelineLayout(), 0, 1, &playerDescSet, 0, nullptr);
            }
            playerModel->render(commandBuffer, graphicsPipeline_->getPipelineLayout(),
                              cam->getViewMatrix(), cam->getProjectionMatrix(),
                              playerModel->getModelMatrix());
        }
    }

    // 渲染动态加载的静态模型（带视锥体剔除）
    for (auto& [id, model] : models_) {
        if (model && model->getMeshCount() > 0) {
            auto bbox = model->getBoundingBox();
            glm::vec3 worldMin = model->getPosition() + bbox.first * model->getScale();
            glm::vec3 worldMax = model->getPosition() + bbox.second * model->getScale();

            glm::vec3 modelCenter = (worldMin + worldMax) * 0.5f;
            if (glm::length(modelCenter - cam->getPosition()) > MODEL_CULLING_DISTANCE) continue;
            if (!cam->getFrustum().isAABBInside(worldMin, worldMax)) continue;

            auto it = modelDescriptorSets_.find(id);
            if (it != modelDescriptorSets_.end() && it->second != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       graphicsPipeline_->getPipelineLayout(), 0, 1, &it->second, 0, nullptr);
            }
            model->render(commandBuffer, graphicsPipeline_->getPipelineLayout(),
                        cam->getViewMatrix(), cam->getProjectionMatrix(),
                        model->getModelMatrix());
        }
    }

    // 渲染 ECS 驱动的实体
    if (gameSession_) {
        auto* renderSys = gameSession_->getRenderSystem();
        if (renderSys) {
            const auto& entries = renderSys->getRenderEntries();
            for (const auto& entry : entries) {
                if (!entry.model || !entry.visible) continue;

                if (entry.descriptorSet != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           graphicsPipeline_->getPipelineLayout(),
                                           0, 1, &entry.descriptorSet, 0, nullptr);
                }

                entry.model->render(commandBuffer, graphicsPipeline_->getPipelineLayout(),
                                  cam->getViewMatrix(), cam->getProjectionMatrix(),
                                  entry.modelMatrix);
            }
        }
    }

    // 渲染树木/石头/草丛
    treeSystem_->render(commandBuffer, graphicsPipeline_->getPipelineLayout(), *cam,
                        totalTime_, gameConfig_.tree.windStrength);
    if (stoneSystem_) stoneSystem_->render(commandBuffer, graphicsPipeline_->getPipelineLayout(), *cam);
    if (grassSystem_) grassSystem_->render(commandBuffer, *cam);

    // 渲染远程玩家模型
    if (gameSession_) {
        for (const auto& [clientId, rp] : gameSession_->getRemotePlayerModels()) {
            GLTFModel* activeModel = rp.wasMoving ? rp.walkModel.get() : rp.idleModel.get();
            if (activeModel && activeModel->getMeshCount() > 0) {
                activeModel->render(commandBuffer, graphicsPipeline_->getPipelineLayout(),
                            cam->getViewMatrix(), cam->getProjectionMatrix(),
                            activeModel->getModelMatrix());
            }
        }
    }

    // 半分辨率云路径 vs 全分辨率云路径
    bool useHalfResCloud = cloudSystem_ && cloudSystem_->isHalfResEnabled()
                           && msaaSamples_ <= VK_SAMPLE_COUNT_1_BIT;
    if (useHalfResCloud) {
        // --- 半分辨率云渲染路径 ---
        vkCmdEndRenderPass(commandBuffer);

        // 过渡 swapchain 图像布局供后续合成
        VkImageMemoryBarrier swapchainBarrier{};
        swapchainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapchainBarrier.image = swapchain_->getImages()[imageIndex];
        swapchainBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapchainBarrier.srcAccessMask = 0;
        swapchainBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &swapchainBarrier);

        // 渲染半分辨率云
        cloudSystem_->renderHalfRes(commandBuffer, *cam, gameConfig_.renderer.sunDirection);

        // 开始合成渲染通道（cloud upscale + ImGui）
        VkRenderPassBeginInfo compositeRpInfo{};
        compositeRpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        compositeRpInfo.renderPass = cloudCompositeRenderPass_;
        compositeRpInfo.framebuffer = cloudCompositeFramebuffers_[imageIndex];
        compositeRpInfo.renderArea.offset = {0, 0};
        compositeRpInfo.renderArea.extent = swapchain_->getExtent();

        std::vector<VkClearValue> compositeClear(2);
        compositeClear[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        compositeClear[1].depthStencil = {1.0f, 0};
        compositeRpInfo.clearValueCount = static_cast<uint32_t>(compositeClear.size());
        compositeRpInfo.pClearValues = compositeClear.data();

        vkCmdBeginRenderPass(commandBuffer, &compositeRpInfo, VK_SUBPASS_CONTENTS_INLINE);

        // 合成半分辨率云 → 全分辨率场景（alpha 混合上采样）
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          cloudCompositePipeline_);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                cloudCompositePipelineLayout_, 0, 1,
                                &cloudCompositeDS_, 0, nullptr);
        vkCmdDraw(commandBuffer, 4, 1, 0, 0);

        // 渲染 ImGui
        imguiManager_->render(commandBuffer);

        vkCmdEndRenderPass(commandBuffer);
    } else {
        // --- 传统全分辨率云渲染路径 ---
        if (cloudSystem_ && cloudSystem_->isInitialized()) {
            cloudSystem_->render(commandBuffer, *cam, gameConfig_.renderer.sunDirection);
        }

        // 渲染 ImGui
        imguiManager_->render(commandBuffer);

        vkCmdEndRenderPass(commandBuffer);
    }

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
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

        fsr1Pass_->dispatch(commandBuffer);

        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = 0;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    VkResult _vrEnd = vkEndCommandBuffer(commandBuffer);
    if (_vrEnd != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to record command buffer! ") + vkResultToString(_vrEnd));
    }

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

    VkResult submitResult = vkQueueSubmit(vulkanDevice_->getGraphicsQueue(), 1, &submitInfo, syncObjects_->getInFlightFences()[currentFrame_]);
    if (submitResult != VK_SUCCESS) {
        Logger::error("[Renderer] vkQueueSubmit failed with error code: " + std::to_string(submitResult));
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            Logger::error("[Renderer] VK_ERROR_DEVICE_LOST - GPU device lost!");
        } else if (submitResult == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            Logger::error("[Renderer] VK_ERROR_OUT_OF_DEVICE_MEMORY - Out of device memory!");
        } else if (submitResult == VK_ERROR_OUT_OF_HOST_MEMORY) {
            Logger::error("[Renderer] VK_ERROR_OUT_OF_HOST_MEMORY - Out of host memory!");
        }

        if (glfwWindowShouldClose(window_)) {
            VkSubmitInfo emptySubmit{};
            emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            vkQueueSubmit(vulkanDevice_->getGraphicsQueue(), 1, &emptySubmit, syncObjects_->getInFlightFences()[currentFrame_]);
            return;
        }
        throw std::runtime_error(std::string("failed to submit draw command buffer! ") + vkResultToString(submitResult));
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapchain_->getSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(vulkanDevice_->getPresentQueue(), &presentInfo);

    if (fsr1Pass_ && fsrScale_ < 1.0f) fsr1Pass_->advanceFrame();

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        if (glfwWindowShouldClose(window_)) {
            return;
        }
        throw std::runtime_error(std::string("failed to present swap chain image! ") + vkResultToString(result));
    }

    currentFrame_ = (currentFrame_ + 1) % syncObjects_->getMaxFramesInFlight();
}

} // namespace owengine
