// OverWrite 游戏主程序入口
#include <iostream>
#include <stdexcept>

#include "core/renderer.hpp"
#include "core/lifecycle_manager.hpp"
#include "core/config_manager.hpp"
#include "core/event_bus.hpp"
#include "core/task_manager.hpp"
#include "core/audio_manager.hpp"
#include "utils/logger.hpp"
#include "utils/asset_paths.hpp"
#include "utils/error_handler.hpp"

/**
 * @brief 主函数
 *
 * 启动流程：
 *   1. 安装崩溃信号处理器
 *   2. 注册所有子系统到 LifecycleManager（依赖拓扑排序）
 *   3. 按依赖顺序初始化
 *   4. 进入主循环
 *   5. 按逆序关闭所有服务
 */
int main() {
    owengine::installCrashHandlers("OverWrite");

    try {
        constexpr int WINDOW_WIDTH = 800;
        constexpr int WINDOW_HEIGHT = 600;
        const std::string WINDOW_TITLE = "OverWrite";

        owengine::Logger::info("Starting OverWrite...");

        // ---- 创建核心对象 ----
        owengine::LifecycleManager lifecycle;
        owengine::Renderer renderer(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
        owengine::ConfigManager configManager;
        owengine::EventBus eventBus;
        owengine::TaskManager taskManager;
        owengine::AudioManager audioManager;

        // ---- 注册服务（依赖空 = 无依赖，拓扑排序决定顺序） ----

        // 1. 配置管理器（最先加载，供其他服务读取配置）
        lifecycle.registerService("ConfigManager", {},
            [&]() -> bool {
                if (!configManager.loadFile(owengine::AssetPaths::GAME_CONFIG)) {
                    owengine::Logger::warning("游戏配置文件加载失败，使用默认配置");
                }
                if (!configManager.loadFile(owengine::AssetPaths::SCENE_CONFIG)) {
                    owengine::Logger::warning("场景配置文件加载失败，使用默认配置");
                }
                return true;
            },
            [&]() { configManager.clear(); }
        );

        // 2. 事件总线（纯内存结构，初始化无操作）
        lifecycle.registerService("EventBus", {},
            [&]() -> bool { return true; },
            []() { /* EventBus 无资源需要释放 */ }
        );

        // 3. 异步任务调度器
        lifecycle.registerService("TaskManager", {},
            [&]() -> bool {
                owengine::Logger::info("[TaskManager] 已启动 " +
                    std::to_string(std::thread::hardware_concurrency()) + " 个工作线程");
                return true;
            },
            [&]() { taskManager.stop(); }
        );

        // 4. 窗口（GLFW）
        lifecycle.registerService("Window", {},
            [&]() -> bool {
                renderer.initWindow();
                return true;
            },
            []() { /* 窗口清理由 Renderer::cleanup 处理 */ }
        );

        // 5. 音频引擎（初始化音频设备）
        lifecycle.registerService("AudioManager", {},
            [&]() -> bool { return audioManager.init(); },
            [&]() { audioManager.cleanup(); }
        );

        // 6. Vulkan 渲染引擎（依赖窗口）
        lifecycle.registerService("VulkanEngine", {"Window"},
            [&]() -> bool {
                renderer.initVulkan();
                return true;
            },
            [&]() { renderer.cleanup(); }
        );

        // ---- 按依赖拓扑顺序初始化 ----
        if (!lifecycle.initialize()) {
            owengine::Logger::error("引擎启动失败");
            return EXIT_FAILURE;
        }

        // ---- 主循环 ----
        renderer.run(true);  // skipInit=true

        // ---- 关闭（按逆序自动清理） ----
        lifecycle.shutdown();

        owengine::Logger::info("Application closed successfully");
    } catch (const std::exception& e) {
        owengine::Logger::error(std::string("Error: ") + e.what());
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
