#!/bin/bash

# OverWrite Build Script
# 前后端分离架构构建脚本
#
# Usage:
#   ./build.sh              # 默认构建 release 版本
#   ./build.sh release      # 构建 release 版本
#   ./build.sh debug        # 构建 debug 版本
#   ./build.sh clean        # 清理构建目录和着色器
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
            echo -e "${YELLOW}Cleaning compiled shaders (.spv files)...${NC}"
            find . -name "*.spv" -type f -delete
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
            echo "  clean        清理构建目录和着色器"
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

# 检查并切换 submodule 到指定版本（只在需要时执行）
checkout_if_needed() {
    local dir=$1
    local version=$2
    # 获取目标版本的 commit hash
    local target_commit=$(cd "$dir" 2>/dev/null && git rev-parse "$version" 2>/dev/null)
    # 获取当前 HEAD 的 commit hash
    local current_commit=$(cd "$dir" 2>/dev/null && git rev-parse HEAD 2>/dev/null)
    
    if [ "$target_commit" != "$current_commit" ]; then
        echo "  $dir: switching to $version"
        cd "$dir" && git checkout "$version" && cd - > /dev/null
    fi
}

# 初始化 git submodules（仅在需要时）
if [ -f ".gitmodules" ]; then
    # 检查是否有未初始化的 submodule（检查是否存在有效的 git 引用）
    NEED_INIT=0
    for submodule in external/entt external/nlohmann_json external/ixwebsocket external/imgui external/tinygltf external/tinyobjloader; do
        if [ ! -e "$submodule/.git" ] || [ ! -d "$submodule" ]; then
            NEED_INIT=1
            break
        fi
    done
    
    if [ $NEED_INIT -eq 1 ]; then
        echo -e "${GREEN}Initializing git submodules...${NC}"
        git submodule update --init --recursive
    fi
    
    # 检查并切换 submodules 到指定版本
    echo -e "${GREEN}Checking submodule versions...${NC}"
    checkout_if_needed "external/entt" "v3.13.2"
    checkout_if_needed "external/nlohmann_json" "v3.11.3"
    checkout_if_needed "external/ixwebsocket" "origin/master"
    checkout_if_needed "external/imgui" "v1.91.8"
    checkout_if_needed "external/tinygltf" "v2.9.3"
    checkout_if_needed "external/tinyobjloader" "v2.0.0rc13"
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