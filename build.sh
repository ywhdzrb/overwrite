#!/bin/bash

# OverWrite Build Script
# For commercial production environments
#
# Usage:
#   ./build.sh          # 默认构建 release 版本
#   ./build.sh release  # 构建 release 版本
#   ./build.sh debug    # 构建 debug 版本
#   ./build.sh clean    # 清理构建目录
#   ./build.sh run      # 构建并运行

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 默认构建类型
BUILD_TYPE="Release"
RUN_AFTER_BUILD=false

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
        help|--help|-h)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  release  构建 release 版本（默认）"
            echo "  debug    构建 debug 版本"
            echo "  clean    清理构建目录"
            echo "  run      构建并运行"
            echo "  help     显示帮助信息"
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
cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE}

# Build the project
echo -e "${GREEN}Building project...${NC}"
cmake --build . -j$(nproc)

# Check if build was successful
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}=================================="
    echo "Build completed successfully!"
    echo "=================================="
    echo -e "Run the game with: ${YELLOW}./OverWrite${NC}"
    echo ""
    
    # 如果指定了 run 参数，则运行程序
    if [ "$RUN_AFTER_BUILD" = true ]; then
        echo -e "${BLUE}Running the game...${NC}"
        cd ..
        ./run.sh
    fi
else
    echo ""
    echo -e "${RED}=================================="
    echo "Build failed!"
    echo "=================================="
    exit 1
fi