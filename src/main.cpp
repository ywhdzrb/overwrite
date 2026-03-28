// OverWrite 游戏主程序入口
// 包含渲染器和日志系统的头文件
#include "renderer.h"
#include "logger.h"
#include <iostream>
#include <stdexcept>

namespace vgame {

// 主函数：程序的入口点
int main() {
    try {
        // 记录程序启动日志
        Logger::info("Starting OverWrite...");
        
        // 设置窗口参数
        constexpr int WINDOW_WIDTH = 800;   // 窗口宽度
        constexpr int WINDOW_HEIGHT = 600;  // 窗口高度
        const std::string WINDOW_TITLE = "OverWrite";  // 窗口标题
        
        // 创建渲染器并传入窗口参数
        Renderer renderer(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
        
        // 运行游戏主循环
        renderer.run();
        
        // 记录程序正常关闭日志
        Logger::info("Application closed successfully");
    } catch (const std::exception& e) {
        // 捕获异常并记录错误日志
        Logger::error(std::string("Error: ") + e.what());
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;  // 返回失败状态码
    }
    
    return EXIT_SUCCESS;  // 返回成功状态码
}

} // namespace vgame

// 全局主函数，调用命名空间中的main函数
int main() {
    return vgame::main();
}