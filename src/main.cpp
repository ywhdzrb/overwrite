// OverWrite 游戏主程序入口
#include <iostream>
#include <stdexcept>

#include "core/renderer.h"
#include "utils/logger.h"

int main() {
    try {
        owengine::Logger::info("Starting OverWrite...");

        constexpr int WINDOW_WIDTH = 800;
        constexpr int WINDOW_HEIGHT = 600;
        const std::string WINDOW_TITLE = "OverWrite";

        owengine::Renderer renderer(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
        renderer.run();

        owengine::Logger::info("Application closed successfully");
    } catch (const std::exception& e) {
        owengine::Logger::error(std::string("Error: ") + e.what());
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
