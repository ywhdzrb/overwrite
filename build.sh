#!/bin/bash

# OverWrite Build Script
# 前后端分离架构构建脚本
#
# Usage:
#   ./build.sh              # 默认构建 release 版本
#   ./build.sh release      # 构建 release 版本
#   ./build.sh debug        # 构建 debug 版本
#   ./build.sh clean        # 清理构建目录
#   ./build.sh run          # 构建并运行客户端
#   ./build.sh run-server   # 构建并运行服务端
#   ./build.sh all          # 构建所有目标（默认）

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
            echo -e "${GREEN}Build directory cleaned!${NC}"
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
            echo "  clean        清理构建目录"
            echo "  run          构建并运行客户端"
            echo "  run-server   构建并运行服务端"
            echo "  client       仅构建客户端"
            echo "  all          构建所有目标（默认）"
            echo "  help         显示帮助信息"
            echo ""
            echo "Targets:"
            echo "  ${CYAN}OverWrite${NC}        - 客户端（Vulkan 渲染）"
            echo "  ${CYAN}overwrite-server${NC} - 服务端（WebSocket）"
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

# 检查 build 目录是否存在
if [ -d "build" ] && [ "$BUILD_TYPE" != "Debug" ] && [ "$BUILD_TYPE" != "Release" ]; then
    echo -e "${YELLOW}Build directory exists, cleaning...${NC}"
    rm -rf build
fi

# Create build directory
echo -e "${GREEN}Creating build directory...${NC}"
mkdir -p build
cd build

# Configure with CMake
echo -e "${GREEN}Configuring project with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCMAKE_POLICY_VERSION_MINIMUM=3.5

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
    
    # 如果指定了 run 参数，则运行程序
    if [ "$RUN_AFTER_BUILD" = true ]; then
        cd ..
        if [ "$RUN_SERVER" = true ]; then
            echo -e "${BLUE}Starting server on port 9002...${NC}"
            ./build/overwrite-server
        else
            echo -e "${BLUE}Running the game client...${NC}"
            ./run.sh
        fi
    fi
else
    echo ""
    echo -e "${RED}=================================="
    echo "Build failed!"
    echo "=================================="
    exit 1
fi
