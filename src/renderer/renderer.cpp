// 渲染器实现 — 纯渲染编排器
// 负责 Vulkan 管线生命周期、帧缓冲、命令缓冲录制、渲染子系统调度。
// 游戏逻辑（ECS/物理/碰撞/网络/玩家动画）已提取至 GameSession。
#include <algorithm>
#include <fstream>
#include <future>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>

#include <nlohmann/json.hpp>

#include "imgui.h"

#include "core/camera.hpp"
#include "core/game_config.hpp"
#include "core/game_session.hpp"
#include "core/input.hpp"
#include "core/renderer.hpp"
#include "core/scene_config.hpp"
#include "ecs/ecs.hpp"
#include "ecs/i_game_world.hpp"
#include "renderer/grass_system.hpp"
#include "renderer/stone_system.hpp"
#include "renderer/tree_system.hpp"
#include "utils/asset_paths.hpp"
#include "utils/logger.hpp"
#include "utils/vk_result.hpp"

namespace owengine {

// Renderer构造函数
Renderer::Renderer(int width, int height, const std::string& title)
    : windowWidth_(width), windowHeight_(height), windowTitle_(title) {
    // 启用4x MSAA抗锯齿
    msaaSamples_ = VK_SAMPLE_COUNT_4_BIT;
}

// Renderer析构函数
Renderer::~Renderer() {
    if (!cleanedUp_) {
        cleanup();
    }
}

// 运行渲染器
// 默认初始化窗口和Vulkan并进入主循环
// 如果 skipInit=true，假设外部已调用 initWindow + initVulkan
void Renderer::run(bool skipInit) {
    if (!skipInit) {
        initWindow();
        initVulkan();
    }
    mainLoop();
}

// 初始化窗口
void Renderer::initWindow() {
    glfwInit();
    
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    
    // 从配置文件读取全屏标志
    GameConfig cfg = GameConfig::load(AssetPaths::GAME_CONFIG);
    if (cfg.renderer.fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            windowWidth_ = mode->width;
            windowHeight_ = mode->height;
            window_ = glfwCreateWindow(windowWidth_, windowHeight_, windowTitle_.c_str(), monitor, nullptr);
        } else {
            window_ = glfwCreateWindow(windowWidth_, windowHeight_, windowTitle_.c_str(), nullptr, nullptr);
        }
    } else {
        window_ = glfwCreateWindow(windowWidth_, windowHeight_, windowTitle_.c_str(), nullptr, nullptr);
    }
    
    if (!window_) {
        throw std::runtime_error("failed to create GLFW window!");
    }
    
    glfwSetWindowUserPointer(window_, this);
}

void Renderer::initVulkan() {
    vulkanInstance_ = std::make_shared<VulkanInstance>();
    vulkanInstance_->initialize(window_);

    // 创建 VulkanDevice（内部自动枚举队列族、创建逻辑设备、命令池和 VMA）
    vulkanDevice_ = std::make_shared<VulkanDevice>(
        vulkanInstance_->getInstance(),
        vulkanInstance_->getPhysicalDevice(),
        vulkanInstance_->getSurface(),
        vulkanInstance_->isValidationEnabled()
    );
    
    // 获取设备支持的最大 MSAA 采样数
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(vulkanInstance_->getPhysicalDevice(), &physicalDeviceProperties);
    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
                                physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) maxMsaaSamples_ = VK_SAMPLE_COUNT_64_BIT;
    else if (counts & VK_SAMPLE_COUNT_32_BIT) maxMsaaSamples_ = VK_SAMPLE_COUNT_32_BIT;
    else if (counts & VK_SAMPLE_COUNT_16_BIT) maxMsaaSamples_ = VK_SAMPLE_COUNT_16_BIT;
    else if (counts & VK_SAMPLE_COUNT_8_BIT) maxMsaaSamples_ = VK_SAMPLE_COUNT_8_BIT;
    else if (counts & VK_SAMPLE_COUNT_4_BIT) maxMsaaSamples_ = VK_SAMPLE_COUNT_4_BIT;
    else if (counts & VK_SAMPLE_COUNT_2_BIT) maxMsaaSamples_ = VK_SAMPLE_COUNT_2_BIT;
    else maxMsaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    
    // 确保初始 MSAA 不超过设备支持的最大值
    if (msaaSamples_ > maxMsaaSamples_) {
        msaaSamples_ = maxMsaaSamples_;
    }
    
    Logger::info("[Renderer] 设备支持的最大 MSAA: " + std::to_string(maxMsaaSamples_));
    
    // FSR1 开启时禁用 MSAA（低分辨率无需抗锯齿，也避免 resolve 不匹配）
    if (fsrScale_ < 1.0f) {
        msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    }
    swapchain_ = std::make_shared<VulkanSwapchain>(vulkanDevice_, window_);
    swapchain_->create();
    
    renderPass_ = std::make_shared<VulkanRenderPass>(vulkanDevice_, swapchain_->getImageFormat(), msaaSamples_);
    renderPass_->create();
    
    // FSR1 管线
    fsr1Pass_ = std::make_unique<Fsr1Pass>(vulkanDevice_, swapchain_->getImageFormat(), swapchain_->getExtent(), fsrScale_);
    fsr1Pass_->init();
    
    // 创建多重采样颜色资源（如果使用MSAA）
    if (msaaSamples_ > VK_SAMPLE_COUNT_1_BIT) {
        createColorResources();
    }
    
    // 创建描述符集布局（必须在 graphicsPipeline_ 之前）
    createDescriptorSetLayouts();

    // 初始化光源管理器（阴影系统作为光照系统的子模块，由 LightManager 统一管理生命周期）
    lightManager_ = std::make_unique<LightManager>();

    // 初始化阴影映射器（方向光阴影贴图，分辨率 2048×2048）
    // 阴影管线布局只需要 set=0（纹理），因为阴影 pass 中只有纹理描述符集被绑定
    lightManager_->initShadow(vulkanDevice_, 2048, {textureDescriptorSetLayout_});

    // 阴影描述符集布局（set=2）通过 LightManager 获取，供主着色器采样阴影贴图

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {
        textureDescriptorSetLayout_,
        lightDescriptorSetLayout_,
        lightManager_->getShadowDescriptorSetLayout()
    };
    graphicsPipeline_ = std::make_shared<VulkanPipeline>(
        vulkanDevice_,
        renderPass_->getRenderPass(),
        swapchain_->getExtent(),
        AssetPaths::MAIN_VERT_SHADER,
        AssetPaths::MAIN_FRAG_SHADER,
        owengine::VertexFormat::POSITION_COLOR,
        descriptorSetLayouts,
        msaaSamples_
    );
    graphicsPipeline_->create();
    
    // 创建深度资源
    vulkanDevice_->createDepthResources(swapchain_->getExtent(), msaaSamples_);
    
    std::vector<VkImageView> colorAttachments = swapchain_->getImageViews();
    framebuffers_ = std::make_shared<VulkanFramebuffer>(vulkanDevice_, renderPass_->getRenderPass());
    framebuffers_->create(colorAttachments, swapchain_->getExtent(), colorImageView_);
    
    commandBuffers_ = std::make_shared<VulkanCommandBuffer>(
        vulkanDevice_
    );
    commandBuffers_->create(MAX_FRAMES_IN_FLIGHT);
    
    syncObjects_ = std::make_shared<VulkanSync>(vulkanDevice_);
    syncObjects_->create(MAX_FRAMES_IN_FLIGHT);
    
    // 不预先记录命令缓冲，每次drawFrame时动态记录

    terrainRenderer_ = std::make_shared<TerrainRenderer>(vulkanDevice_);
    terrainRenderer_->create();

    // 初始更新地形区块（玩家位置随后由 GameSession 驱动）
    terrainRenderer_->update(glm::vec3(0.0f, 0.0f, 5.0f));

    // 构建地形高度查询回调（渲染侧共享，GameSession 和 树/石/草系统共用）
    auto weakTerrain = std::weak_ptr<TerrainRenderer>(terrainRenderer_);
    auto terrainHeightQuery = [weakTerrain](float x, float z) -> float {
        auto terrain = weakTerrain.lock();
        if (!terrain) return 0.0f;
        return terrain->getHeight(x, z);
    };

    // 初始化纹理加载器
    textureLoader_ = std::make_shared<TextureLoader>(vulkanDevice_);
    shaderManager_ = std::make_unique<ShaderManager>(vulkanDevice_);

    // 光源管理器已在 createDescriptorSetLayouts() 之后初始化，此处不再重复创建

    // 初始化天空盒渲染器（程序化渐变色，无需纹理加载）
    skyboxRenderer_ = std::make_unique<SkyboxRenderer>(vulkanDevice_);
    skyboxRenderer_->create();

    // 初始化模型渲染器（不含玩家模型，玩家模型由 GameSession 管理）
    modelRenderer_ = std::make_unique<ModelRenderer>(vulkanDevice_, textureLoader_);
    modelRenderer_->create();

    // 创建天空盒管线
    // 创建天空盒管线（无描述符，仅 push constants）
    skyboxPipeline_ = std::make_shared<VulkanPipeline>(
        vulkanDevice_,
        renderPass_->getRenderPass(),
        swapchain_->getExtent(),
        AssetPaths::SKYBOX_VERT_SHADER,
        AssetPaths::SKYBOX_FRAG_SHADER,
        VertexFormat::POSITION_ONLY,
        std::vector<VkDescriptorSetLayout>{},  // 天空盒无描述符
        msaaSamples_
    );
    skyboxPipeline_->create();

    // 基础描述符池（+2 给阴影贴图描述符集预留）
    createDescriptorPool(29, 28);

    // 从 JSON 配置文件加载场景（光源和静态模型）
    SceneConfig sceneConfig = loadSceneConfig(AssetPaths::SCENE_CONFIG);

    // 加载光源
    if (!sceneConfig.lights.empty()) {
        loadLightsFromConfig(sceneConfig);
    } else {
        Logger::warning("使用默认光源配置");
        lightManager_->addDirectionalLight("sun", glm::vec3(0.5f, -1.0f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f), 1.0f);
        lightManager_->addPointLight("point1", glm::vec3(2.0f, 3.0f, 2.0f), glm::vec3(1.0f, 1.0f, 1.0f), 2.0f, 10.0f);
        lightManager_->addPointLight("point2", glm::vec3(-2.0f, 3.0f, -2.0f), glm::vec3(1.0f, 1.0f, 1.0f), 2.0f, 10.0f);
        lightManager_->setAmbientColor(glm::vec3(0.5f, 0.5f, 0.5f));
        lightManager_->setAmbientIntensity(0.5f);
    }

    // 加载静态模型（不含玩家模型，玩家模型由 GameSession 加载）
    if (!sceneConfig.models.empty()) {
        loadModelsFromConfig(sceneConfig.models);
    }

    // 创建描述符集（默认纹理描述符集 + 光源 uniform buffer）
    createDescriptorSets();

    {
        // 加载草地 BaseColor 纹理，为地形渲染创建专用描述符集
        std::shared_ptr<Texture> grassTex = textureLoader_->loadTexture("assets/textures/grass/Poliigon_GrassPatchyGround_4585_BaseColor.jpg");
        if (grassTex) {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool_;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout_;

            VkDescriptorSet terrainDescSet;
            if (vkAllocateDescriptorSets(vulkanDevice_->getDevice(), &allocInfo, &terrainDescSet) == VK_SUCCESS) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = grassTex->getImageView();
                imageInfo.sampler = grassTex->getSampler();

                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = terrainDescSet;
                descriptorWrite.dstBinding = 0;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pImageInfo = &imageInfo;

                vkUpdateDescriptorSets(vulkanDevice_->getDevice(), 1, &descriptorWrite, 0, nullptr);
                terrainRenderer_->setTexture(terrainDescSet);
                Logger::info("地形草地纹理已加载并绑定");
            } else {
                Logger::warning("无法为地形草地纹理分配描述符集");
            }
        } else {
            Logger::warning("地形草地纹理加载失败，地形和草将使用程序化颜色");
        }
    }

    // 加载渲染器配置
    gameConfig_ = GameConfig::load(AssetPaths::GAME_CONFIG);
    targetFPS_ = gameConfig_.renderer.targetFPS;

    // 初始化树系统
    treeSystem_ = std::make_unique<TreeSystem>(vulkanDevice_, textureLoader_, textureDescriptorSetLayout_);
    treeSystem_->setHeightSampler(terrainHeightQuery);
    treeSystem_->init(gameConfig_.tree);

    // 初始化石头系统
    stoneSystem_ = std::make_unique<StoneSystem>(vulkanDevice_, textureLoader_, textureDescriptorSetLayout_);
    stoneSystem_->setHeightSampler(terrainHeightQuery);
    stoneSystem_->init(gameConfig_.stone);

    // 初始化草丛系统（传递 descriptor set 布局，使 grass.frag 可从 SSBO 读取光照/阴影）
    grassSystem_ = std::make_unique<GrassSystem>(vulkanDevice_);
    grassSystem_->setHeightSampler(terrainHeightQuery);
    grassSystem_->init(gameConfig_.grass, renderPass_->getRenderPass(),
                       swapchain_->getExtent(), msaaSamples_,
                       textureDescriptorSetLayout_,
                       lightDescriptorSetLayout_,
                       lightManager_->getShadowDescriptorSetLayout());

    grassSystem_->setTreeQuery([this](float x, float z, float radius) {
        return treeSystem_->queryPositions(x, z, radius);
    });
    grassSystem_->setStoneQuery([this](float x, float z, float radius) {
        return stoneSystem_->queryPositions(x, z, radius);
    });
    if (auto* sunLight = lightManager_->getLightByName("sun")) {
        grassSystem_->setGlobalLightDir(sunLight->getDirection());
    }

    // 初始化体积云系统（在所有不透明渲染系统之后，GameSession之前）
    // 云层位于 Y=80~120m 高空，透明度混合叠加
    // 启用半分辨率渲染（halfRes=true），先渲染到½尺寸颜色附件，再上采样合成到主场景
    cloudSystem_ = std::make_unique<CloudSystem>(vulkanDevice_);
    cloudSystem_->init(renderPass_->getRenderPass(), swapchain_->getExtent(), msaaSamples_,
                       true, swapchain_->getImageFormat(), shaderManager_.get());

    // 初始化云合成管线资源（半分辨率上采样用）
    if (cloudSystem_ && cloudSystem_->isHalfResEnabled()) {
        createCloudCompositeResources();
    }

    // 初始化 ImGui
    imguiManager_ = std::make_unique<ImGuiManager>(vulkanDevice_, swapchain_, renderPass_, window_, vulkanInstance_->getInstance(), msaaSamples_);
    imguiManager_->init();

    // 创建游戏会话（持有 ECS/物理/碰撞/网络/动画逻辑）
    if (externalGameSession_) {
        gameSession_ = externalGameSession_;
    } else {
        ownedGameSession_ = std::make_unique<GameSession>();
        GameSessionInitParams gsParams;
        gsParams.window = window_;
        gsParams.windowWidth = windowWidth_;
        gsParams.windowHeight = windowHeight_;
        gsParams.device = vulkanDevice_;
        gsParams.textureLoader = textureLoader_;
        gsParams.terrainRenderer = terrainRenderer_;
        gsParams.treeSystem = treeSystem_.get();
        gsParams.stoneSystem = stoneSystem_.get();
        gsParams.grassSystem = grassSystem_.get();
        gsParams.descriptorPool = descriptorPool_;
        gsParams.textureDescriptorSetLayout = textureDescriptorSetLayout_;
        gsParams.lightDescriptorSetLayout = lightDescriptorSetLayout_;
        gsParams.graphicsPipelineLayout = graphicsPipeline_->getPipelineLayout();
        gsParams.terrainHeightQuery = terrainHeightQuery;
        ownedGameSession_->init(gsParams);
        gameSession_ = ownedGameSession_.get();
    }

    lastTime_ = std::chrono::high_resolution_clock::now();
}

void Renderer::mainLoop() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    // 第一帧后捕获鼠标，确保窗口已经显示
    bool firstFrame = true;
    
    // FPS 计数
    int frameCount = 0;
    float fpsTimer = 0.0f;
    
    // 绝对帧时间目标（用 sleep_until 替代 sleep_for 避免累计漂移）
    auto nextFrameTime = std::chrono::high_resolution_clock::now();
    
    while (!glfwWindowShouldClose(window_)) {
        // 记录帧开始时间
        auto frameStartTime = std::chrono::high_resolution_clock::now();
        
        // 计算delta time
        auto currentTime = frameStartTime;
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // 限制delta time以防卡顿
        if (deltaTime > ecs::MAX_DELTA_TIME) {
            deltaTime = ecs::MAX_DELTA_TIME;
        }
        
        // 累计全局时间（用于风场动画等连续效果）
        totalTime_ += deltaTime;
        
        // 检查是否有延迟的 MSAA 更改
        if (pendingMsaaChange_) {
            pendingMsaaChange_ = false;
            setMsaaSamples(pendingMsaaSamples_);
        }
        
        glfwPollEvents();

        // 第一帧后捕获鼠标
        if (firstFrame) {
            if (gameSession_ && gameSession_->getInput()) {
                gameSession_->getInput()->setCursorCaptured(true);
            }
            firstFrame = false;
            glfwPollEvents();
        }

        // === 背包打开首帧：在 NewFrame 前强制同步鼠标位置 ===
        // GLFW_CURSOR_DISABLED 模式下光标回调报告相对增量，
        // 切换到 NORMAL 后 ImGui 的 io.MousePos 可能仍有旧坐标。
        // 必须在 NewFrame() 之前设置，否则 AddMousePosEvent 会排队到下一帧。
        bool invJustOpened = gameSession_ && gameSession_->isInventoryOpen() && !prevInvOpen_;
        if (invJustOpened) {
            double mx, my;
            glfwGetCursorPos(window_, &mx, &my);
            ImGui::GetIO().AddMousePosEvent(static_cast<float>(mx), static_cast<float>(my));
        }
        prevInvOpen_ = gameSession_ ? gameSession_->isInventoryOpen() : false;

        // === ImGui 新帧 + HUD ===
        imguiManager_->newFrame();
        if (gameSession_) {
            ImGui::Begin("HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("FPS: %.1f", gameSession_->getCurrentFPS());
            ImGui::Text("Logic:%.1f Draw:%.1f Fence:%.1f ms",
                        gameSession_->getProfLogicMs(), profDrawMs_, profFenceWaitMs_);
            if (auto* ecs = gameSession_->getECSWorld()) {
                glm::vec3 pos = ecs->getPlayerPosition();
                ImGui::Text("Pos: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
            }
            ImGui::SetWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::End();
        }

        // === 采集交互提示（准星指向资源时显示） ===
        if (gameSession_ && !gameSession_->isInventoryOpen()) {
            const auto& target = gameSession_->getHarvestTarget();
            if (target.valid) {
                const char* resName = ecs::resourceTypeName(target.type);
                ImGui::SetNextWindowPos(
                    ImVec2(static_cast<float>(windowWidth_) * 0.5f - 80.0f,
                           static_cast<float>(windowHeight_) * 0.5f + 20.0f),
                    ImGuiCond_Always);
                ImGui::Begin("##harvestPrompt", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                    | ImGuiWindowFlags_NoInputs);
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.7f, 1.0f), "[F] 采集 %s", resName);
                ImGui::End();
            }
        }

        // === HUD 快捷栏（Minecraft 风格，始终显示在底部） ===
        if (gameSession_) {
            auto* ecs = gameSession_->getECSWorld();
            if (ecs) {
                auto* reg = ecs->getRegistry();
                auto player = ecs->getPlayerEntity();
                if (reg && reg->valid(player)) {
                    auto* inv = reg->try_get<ecs::InventoryComponent>(player);
                    if (inv) {
                        // 底部居中
                        ImVec2 hotbarPos(static_cast<float>(windowWidth_) * 0.5f,
                                        static_cast<float>(windowHeight_) - 50.0f);
                        ImGui::SetNextWindowPos(hotbarPos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                        ImGui::SetNextWindowBgAlpha(0.4f);
                        ImGui::Begin("##hudHotbar", nullptr,
                            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                            | ImGuiWindowFlags_NoInputs);
                        for (uint32_t i = 0; i < ecs::InventoryComponent::HOTBAR_SLOTS; i++) {
                            if (i > 0) ImGui::SameLine();
                            auto& slot = inv->slots[i];
                            ImVec4 bgColor = (i == inv->selectedHotbarIndex)
                                ? ImVec4(0.3f, 0.6f, 0.3f, 1.0f)
                                : ImVec4(0.15f, 0.15f, 0.15f, 0.8f);
                            ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
                            if (slot.isEmpty()) {
                                ImGui::Button("##hudslot", ImVec2(48, 48));
                            } else {
                                char label[32];
                                snprintf(label, sizeof(label), "%s\nx%d", slot.name(), slot.count);
                                ImGui::Button(label, ImVec2(48, 48));
                            }
                            ImGui::PopStyleColor();
                        }
                        ImGui::End();
                    }
                }
            }
        }

        // === 云调试面板（F6 切换） ===
        if (ImGui::IsKeyPressed(ImGuiKey_F6)) {
            showCloudDebug_ = !showCloudDebug_;
        }
        if (showCloudDebug_ && cloudSystem_) {
            auto cp = cloudSystem_->getDebugParams();
            ImGui::Begin("云渲染调试", &showCloudDebug_, ImGuiWindowFlags_NoCollapse);
            ImGui::Text("--- 密度参数 ---");
            ImGui::SliderFloat("覆盖率", cp.coverage, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("密度倍率", cp.densityMultiplier, 0.0f, 5.0f, "%.1f");
            ImGui::SliderFloat("步进次数", cp.stepCountFloat, 16.0f, 160.0f, "%.0f");
            ImGui::Text("--- 着色器调优参数（原硬编码#define） ---");
            ImGui::SliderFloat("采样抖动", cp.jitterAmplitude, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("花椰菜强度", cp.cauliStrength, 0.0f, 0.5f, "%.3f");
            ImGui::SliderFloat("覆盖抖动", cp.thresholdDitherAmp, 0.0f, 0.1f, "%.4f");
            ImGui::Text("--- 风动画 ---");
            ImGui::SliderFloat("风速", cp.windSpeed, 0.0f, 50.0f, "%.1f");
            ImGui::Text("--- 薄云层 ---");
            ImGui::Checkbox("启用薄云", cp.thinCloudEnabled);
            ImGui::SliderFloat("薄云高度", cp.thinCloudHeight, 120.0f, 300.0f, "%.0f");
            ImGui::SliderFloat("薄云密度", cp.thinCloudDensity, 0.0f, 1.0f, "%.2f");
            ImGui::Text("--- 光照 ---");
            ImGui::SliderFloat("太阳强度", cp.sunIntensity, 0.0f, 3.0f, "%.1f");
            ImGui::Checkbox("昼夜循环", cp.dayNightEnabled);
            ImGui::Separator();
            ImGui::Text("--- 配置持久化 ---");
            if (ImGui::Button("保存配置")) {
                cloudSystem_->saveConfig();
            }
            ImGui::SameLine();
            if (ImGui::Button("加载配置")) {
                cloudSystem_->loadConfig();
            }
            ImGui::End();
        }

        // === 背包 ImGui 窗口 ===
        if (gameSession_ && gameSession_->isInventoryOpen()) {

            // 获取背包组件
            auto* ecs = gameSession_->getECSWorld();
            if (ecs) {
                auto* reg = ecs->getRegistry();
                auto player = ecs->getPlayerEntity();
                if (reg && reg->valid(player)) {
                    auto* inv = reg->try_get<ecs::InventoryComponent>(player);
                    if (inv) {
                        const ImVec2 viewport = ImGui::GetMainViewport()->Size;
                        ImGui::SetNextWindowPos(ImVec2(viewport.x * 0.5f, viewport.y * 0.5f),
                                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                        ImGui::SetNextWindowSize(ImVec2(400, 320), ImGuiCond_FirstUseEver);
                        ImGui::Begin("背包", nullptr, ImGuiWindowFlags_NoCollapse);
                        
                        // 快捷栏（前5格）
                        ImGui::Text("快捷栏");
                        ImGui::Separator();
                        for (uint32_t i = 0; i < ecs::InventoryComponent::HOTBAR_SLOTS; i++) {
                            auto& slot = inv->slots[i];
                            ImGui::PushID(static_cast<int>(i));
                            if (i > 0) ImGui::SameLine();
                            // 绘制物品槽
                            ImVec4 bgColor = (i == inv->selectedHotbarIndex)
                                ? ImVec4(0.3f, 0.6f, 0.3f, 1.0f)  // 选中绿色
                                : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);  // 默认深灰
                            ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
                            if (slot.isEmpty()) {
                                ImGui::Button("##slot", ImVec2(48, 48));
                            } else {
                                char label[32];
                                snprintf(label, sizeof(label), "%s\nx%d", slot.name(), slot.count);
                                ImGui::Button(label, ImVec2(48, 48));
                            }
                            ImGui::PopStyleColor();
                            // 拖拽源：有物品的格子可拖出
                            if (!slot.isEmpty()) {
                                if (ImGui::BeginDragDropSource()) {
                                    ImGui::SetDragDropPayload("INVENTORY_SLOT", &i, sizeof(uint32_t));
                                    ImGui::Text("拖动 %s", slot.name());
                                    ImGui::EndDragDropSource();
                                }
                            }
                            // 拖拽目标：所有格子都可接收
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT")) {
                                    uint32_t srcIdx = *(const uint32_t*)payload->Data;
                                    if (srcIdx != i) {
                                        inv->swapSlots(srcIdx, i);
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }
                            ImGui::PopID();
                        }
                        
                        // 背包主体（6~19格，4列布局）
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Text("背包");
                        ImGui::Separator();
                        constexpr int COLS = 4;
                        for (uint32_t i = ecs::InventoryComponent::HOTBAR_SLOTS;
                             i < ecs::InventoryComponent::DEFAULT_SLOTS; i++) {
                            auto& slot = inv->slots[i];
                            ImGui::PushID(static_cast<int>(i));
                            if ((i - ecs::InventoryComponent::HOTBAR_SLOTS) % COLS != 0)
                                ImGui::SameLine();
                            ImVec4 bgColor(0.2f, 0.2f, 0.2f, 1.0f);
                            ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
                            if (slot.isEmpty()) {
                                ImGui::Button("##slot", ImVec2(48, 48));
                            } else {
                                char label[32];
                                snprintf(label, sizeof(label), "%s\nx%d", slot.name(), slot.count);
                                ImGui::Button(label, ImVec2(48, 48));
                            }
                            ImGui::PopStyleColor();
                            // 拖拽源
                            if (!slot.isEmpty()) {
                                if (ImGui::BeginDragDropSource()) {
                                    ImGui::SetDragDropPayload("INVENTORY_SLOT", &i, sizeof(uint32_t));
                                    ImGui::Text("拖动 %s", slot.name());
                                    ImGui::EndDragDropSource();
                                }
                            }
                            // 拖拽目标
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT")) {
                                    uint32_t srcIdx = *(const uint32_t*)payload->Data;
                                    if (srcIdx != i) {
                                        inv->swapSlots(srcIdx, i);
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }
                            ImGui::PopID();
                        }
                        
                        ImGui::End();
                    }
                }
            }
        } else {
            prevInvOpen_ = false;
        }

        // === 更新输入状态（previousKeys_ 快照，为 isKeyJustPressed 提供准确前一帧状态） ===
        if (gameSession_ && gameSession_->getInput()) {
            gameSession_->getInput()->update();
        }

        // === 更新游戏逻辑（委托给 GameSession） ===
        if (gameSession_) {
            float scaledDt = gameSession_->pauseGame ? 0.0f : deltaTime * gameSession_->timeScale;
            gameSession_->update(scaledDt);
        }

        // === 推进昼夜循环 ===
        dayTime_ += deltaTime;
        if (dayTime_ >= dayCyclePeriod_) {
            dayTime_ -= dayCyclePeriod_;
        }

        // === 更新体积云（使用当前太阳方向） ===
        if (cloudSystem_) {
            Camera* cam = gameSession_ ? gameSession_->getCamera() : nullptr;
            if (cam) {
                cloudSystem_->update(deltaTime, *cam, gameConfig_.renderer.sunDirection);
            }
        }

        // === 渲染帧 ===
        {
            auto t0 = std::chrono::high_resolution_clock::now();
            drawFrame();
            profDrawMs_ = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
        }

        // === FPS 统计 ===
        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 1.0f) {
            float fps = float(frameCount) / fpsTimer;
            Logger::info(std::string("[Renderer] FPS: ") + std::to_string((int)fps)
                      + " D=" + std::to_string((int)profDrawMs_)
                      + " G=" + std::to_string((int)profFenceWaitMs_) + "ms");
            frameCount = 0;
            fpsTimer = 0.0f;
            minFrameTime_ = 999.0f;
            maxFrameTime_ = 0.0f;
        }

        // === 帧率限制 ===
        if (targetFPS_ > 0.0f) {
            nextFrameTime += std::chrono::nanoseconds(static_cast<long long>((1.0 / targetFPS_) * 1e9));
        }
        auto frameEndTime = std::chrono::high_resolution_clock::now();
        frameTime_ = std::chrono::duration<float>(frameEndTime - frameStartTime).count();
        if (frameTime_ < minFrameTime_) minFrameTime_ = frameTime_;
        if (frameTime_ > maxFrameTime_) maxFrameTime_ = frameTime_;
        if (targetFPS_ > 0.0f && frameEndTime < nextFrameTime) {
            std::this_thread::sleep_until(nextFrameTime);
        } else {
            nextFrameTime = frameEndTime;
        }
    }

    vkDeviceWaitIdle(vulkanDevice_->getDevice());
}

// drawFrame 实现见 renderer_draw.cpp

void Renderer::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window_, &width, &height);
        glfwWaitEvents();
    }
    
    vkDeviceWaitIdle(vulkanDevice_->getDevice());
    
    // 保存云半分辨率状态（cleanup 后会被重置）
    bool hadCloudHalfRes = cloudSystem_ && cloudSystem_->isHalfResEnabled();
    
    // 清理MSAA颜色资源
    if (msaaSamples_ > VK_SAMPLE_COUNT_1_BIT) {
        cleanupColorResources();
    }
    
    // 清理深度资源和云合成资源
    vulkanDevice_->cleanupDepthResources();
    cleanupCloudCompositeResources();
    if (hadCloudHalfRes) {
        cloudSystem_->cleanup();  // 重新初始化时会再次 init
    }
    
    swapchain_->recreate(window_);
    
    // 重建 FSR1 管线（新尺寸）
    VkExtent2D renderExt = swapchain_->getExtent();
    if (fsr1Pass_) {
        fsr1Pass_->cleanup();
        fsr1Pass_ = std::make_unique<Fsr1Pass>(vulkanDevice_, swapchain_->getImageFormat(), swapchain_->getExtent(), fsrScale_);
        fsr1Pass_->init();
    }

    // 重新创建深度资源
    vulkanDevice_->createDepthResources(renderExt, msaaSamples_);
    
    // 更新渲染通道的MSAA样本数并重新创建
    renderPass_->setMsaaSamples(msaaSamples_);
    renderPass_->cleanup();
    renderPass_->create();
    
    // 重新创建MSAA颜色资源
    if (msaaSamples_ > VK_SAMPLE_COUNT_1_BIT) {
        createColorResources();
    }
    
    // 更新管线的MSAA样本数并重新创建
    graphicsPipeline_->setMsaaSamples(msaaSamples_);
    graphicsPipeline_->cleanup();
    graphicsPipeline_->create();
    
    skyboxPipeline_->setMsaaSamples(msaaSamples_);
    skyboxPipeline_->cleanup();
    skyboxPipeline_->create();

    // 重建草地管线（固定视口，需随交换链更新）
    if (grassSystem_) grassSystem_->rebuildPipeline();

    // 重建帧缓冲
    framebuffers_->recreate(swapchain_->getImageViews(), renderExt, colorImageView_);
    commandBuffers_->cleanup();
    commandBuffers_->create(swapchain_->getImageViews().size());
    
    // 仅重初始化 ImGui Vulkan 后端（保留 GLFW 回调，防止 ECS InputSystem 回调链丢失）
    imguiManager_->reinitVulkan();

    // 用实际渲染尺寸同步相机和 Renderer 的窗口尺寸
    // FSR 启用时视口使用 FSR1 的缩小尺寸，相机宽高比必须匹配 FSR render extent
    VkExtent2D actualExt = fsr1Pass_ ? fsr1Pass_->getRenderExtent() : renderExt;
    windowWidth_ = actualExt.width;
    windowHeight_ = actualExt.height;
    if (gameSession_ && gameSession_->getCamera()) {
        gameSession_->getCamera()->setWindowSize(windowWidth_, windowHeight_);
    }

    // 重新初始化云系统（包含半分辨率资源）
    if (hadCloudHalfRes && cloudSystem_) {
        cloudSystem_->init(renderPass_->getRenderPass(), swapchain_->getExtent(), msaaSamples_,
                           true, swapchain_->getImageFormat(), shaderManager_.get());
        // 重新创建合成管线资源
        createCloudCompositeResources();
    }
    
    // 命令缓冲在每次 drawFrame() 中动态录制，无需此处预录制
}

void Renderer::cleanup() {
    // 防重复清理：析构函数和 LifecycleManager 都可能触发
    if (cleanedUp_) return;
    cleanedUp_ = true;

    // 等待设备空闲，确保所有渲染操作完成
    if (vulkanDevice_) {
        vkDeviceWaitIdle(vulkanDevice_->getDevice());
    }
    
    // 清理游戏会话（必须在 Vulkan 资源销毁前）
    ownedGameSession_.reset();
    gameSession_ = nullptr;

    // 清理动态加载的模型
    models_.clear();
    modelDescriptorSets_.clear();
    
    // 清理 ImGui（使用 Vulkan）
    imguiManager_.reset();
    
    modelRenderer_.reset();
    skyboxRenderer_.reset();
    terrainRenderer_.reset();
    
    // 清理新系统
    lightManager_.reset();
    textureLoader_.reset();
    
    // 清理树木（由 TreeSystem 统一管理）
    treeSystem_.reset();
    // 清理石头系统
    stoneSystem_.reset();
    // 清理草丛系统
    grassSystem_.reset();
    // 清理体积云系统和合成资源
    cleanupCloudCompositeResources();
    cloudSystem_.reset();
    // 清理 FSR1 管线
    fsr1Pass_.reset();

    // 阴影映射器由 LightManager 统一管理，在 lightManager_.reset() 时自动析构清理
    // 注意清理顺序：描述符池销毁前必须确保阴影描述符集不再使用

    // 清理着色器管理器（必须在 vulkanDevice_ 销毁前，缓存了 VkShaderModule）
    shaderManager_.reset();

    // 清理描述符集资源
    if (lightUniformBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vulkanDevice_->getAllocator(), lightUniformBuffer_, lightUniformBufferAllocation_);
        lightUniformBuffer_ = VK_NULL_HANDLE;
        lightUniformBufferAllocation_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice_->getDevice(), descriptorPool_, nullptr);
    }
    if (lightDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice_->getDevice(), lightDescriptorSetLayout_, nullptr);
    }
    if (textureDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice_->getDevice(), textureDescriptorSetLayout_, nullptr);
    }
    
    syncObjects_.reset();
    commandBuffers_.reset();
    
    // 清理MSAA颜色资源
    cleanupColorResources();
    
    framebuffers_.reset();
    skyboxPipeline_.reset();
    graphicsPipeline_.reset();
    renderPass_.reset();
    swapchain_.reset();
    vulkanDevice_.reset();
    vulkanInstance_.reset();
    
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    
    glfwTerminate();
}



} // namespace owengine