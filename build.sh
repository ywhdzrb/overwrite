#!/bin/bash

# OverWrite Build Script
# 前后端分离架构构建脚本
#
# 依赖管理：依赖通过 Git Submodule 管理，首次构建前执行 git submodule update --init --recursive。
#
# Usage:
#   ./build.sh              # 默认构建 release 版本
#   ./build.sh release      # 构建 release 版本
#   ./build.sh debug        # 构建 debug 版本
#   ./build.sh clean        # 清理构建目录、着色器和依赖
#   ./build.sh run          # 构建并运行客户端
#   ./build.sh run-server   # 构建并运行服务端
#   ./build.sh all          # 构建所有目标（默认）
#   ./build.sh test         # 构建并运行单元测试（需联网下载 GTest）
#   ./build.sh dashboard    # 生成项目综合仪表盘（Python3 + matplotlib）

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 默认构建类型和目标
BUILD_TYPE="Release"
RUN_AFTER_BUILD=false
RUN_SERVER=false
BUILD_TARGET="all"
BUILD_TESTS="OFF"

# 解析参数
for arg in "$@"; do
    case $arg in
        release|Release)
            BUILD_TYPE="Release"
            ;;
        debug|Debug)
            BUILD_TYPE="Debug"
            ;;
        clean|Clean)
            echo -e "${YELLOW}Cleaning build directory...${NC}"
            rm -rf build
            echo -e "${YELLOW}Cleaning compiled shaders (.spv files)...${NC}"
            find . -name "*.spv" -type f -delete
            echo -e "${YELLOW}Cleaning external dependencies (submodules)...${NC}"
            git submodule deinit --all --force 2>/dev/null || true
            echo -e "${YELLOW}Cleaning config files...${NC}"
            rm -f imgui.ini
            echo -e "${YELLOW}Cleaning CI build artifacts...${NC}"
            rm -rf build-ci
            echo -e "${YELLOW}Cleaning dashboard artifacts...${NC}"
            rm -f dashboard/dashboard.png
            echo -e "${GREEN}Clean completed!${NC}"
            exit 0
            ;;
        run|Run)
            RUN_AFTER_BUILD=true
            ;;
        run-server|server)
            RUN_SERVER=true
            RUN_AFTER_BUILD=true
            ;;
        client)
            BUILD_TARGET="OverWrite"
            ;;
        test|tests)
            BUILD_TESTS="ON"
            ;;
        dashboard)
            echo -e "${GREEN}Generating project dashboard...${NC}"
            python3 dashboard/gen_dashboard.py
            echo -e "${GREEN}Dashboard generated: dashboard/dashboard.png${NC}"
            exit 0
            ;;
        all)
            BUILD_TARGET="all"
            ;;
        help|--help|-h)
            echo "OverWrite 构建脚本"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  release      构建 release 版本（默认）"
            echo "  debug        构建 debug 版本"
            echo "  clean        清理构建目录、着色器、依赖和仪表盘"
            echo "  dashboard    生成项目综合仪表盘 (需 Python3 + matplotlib)"
            echo "  run          构建并运行客户端"
            echo "  run-server   构建并运行服务端"
            echo "  client       仅构建客户端"
            echo "  all          构建所有目标（默认）"
            echo "  help         显示帮助信息"
            echo ""
            echo "Targets:"
            echo -e "  ${CYAN}OverWrite${NC}        - 客户端（Vulkan 渲染）"
            echo -e "  ${CYAN}overwrite-server${NC} - 服务端（WebSocket）"
            echo ""
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $arg${NC}"
            echo "Use '$0 help' for usage information"
            exit 1
            ;;
    esac
done

echo "=================================="
echo "OverWrite Build Script"
echo "=================================="
echo -e "Build type: ${BLUE}${BUILD_TYPE}${NC}"
echo -e "Target:     ${CYAN}${BUILD_TARGET}${NC}"
echo ""

# 初始化 Git Submodule 依赖
if [ -f ".gitmodules" ]; then
    echo -e "${GREEN}Initializing Git submodules...${NC}"
    git submodule update --init --recursive
fi

# Create build directory
echo -e "${GREEN}Creating build directory...${NC}"
mkdir -p build
cd build

# Configure with CMake
echo -e "${GREEN}Configuring project with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DBUILD_TESTS=${BUILD_TESTS}

# Build the project
echo -e "${GREEN}Building project...${NC}"
if [ "$BUILD_TARGET" = "all" ]; then
    cmake --build . -j$(nproc)
else
    cmake --build . -j$(nproc) --target ${BUILD_TARGET}
fi

# Check if build was successful
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}=================================="
    echo "Build completed successfully!"
    echo "=================================="
    echo -e "Targets built:"
    echo -e "  ${CYAN}OverWrite${NC}        - ${YELLOW}./build/OverWrite${NC} (客户端)"
    echo -e "  ${CYAN}overwrite-server${NC} - ${YELLOW}./build/overwrite-server${NC} (服务端)"
    echo ""
    
    # 如果启用测试，运行 CTest
    if [ "$BUILD_TESTS" = "ON" ]; then
        echo -e "${GREEN}Running unit tests...${NC}"
        ctest --output-on-failure
        echo ""
    fi
    
    # 如果指定了 run 参数，则运行程序
    if [ "$RUN_AFTER_BUILD" = true ]; then
        cd ..
        if [ "$RUN_SERVER" = true ]; then
            echo -e "${BLUE}Starting server on port 9002...${NC}"
            ./build/overwrite-server
        else
            echo -e "${BLUE}Running the game client...${NC}"
            # 使用默认平台（X11，Wayland 键盘输入可能不工作）
            unset GLFW_PLATFORM
            ./build/OverWrite
        fi
    fi
else
    echo ""
    echo -e "${RED}=================================="
    echo "Build failed!"
    echo "=================================="
    exit 1
fi
