#!/bin/bash

# OverWrite Build Script
# 前后端分离架构构建脚本
#
# 依赖管理：本脚本与 .gitmodules 同步维护，两者定义的依赖列表必须一致。
#   - .gitmodules 记录官方源 URL（供 git submodule init 使用）
#   - build.sh 提供 GitHub/Gitee 镜像切换（供无梯子环境使用）
#   修改依赖时请同步更新两个文件。
#
# Usage:
#   ./build.sh              # 默认构建 release 版本
#   ./build.sh release      # 构建 release 版本
#   ./build.sh debug        # 构建 debug 版本
#   ./build.sh clean        # 清理构建目录和着色器
#   ./build.sh run          # 构建并运行客户端
#   ./build.sh run-server   # 构建并运行服务端
#   ./build.sh all          # 构建所有目标（默认）
#   ./build.sh test         # 构建并运行单元测试（需联网下载 GTest）
#   ./build.sh --github     # 强制使用 GitHub 源
#   ./build.sh --gitee      # 强制使用 Gitee 源

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
FORCE_SOURCE=""  # 空=自动检测，github=强制 GitHub，gitee=强制 Gitee

# 依赖源配置
declare -A GITHUB_SOURCES=(
    ["entt"]="https://github.com/skypjack/entt.git"
    ["nlohmann_json"]="https://github.com/nlohmann/json.git"
    ["ixwebsocket"]="https://github.com/machinezone/IXWebSocket.git"
    ["imgui"]="https://github.com/ocornut/imgui.git"
    ["tinygltf"]="https://github.com/syoyo/tinygltf.git"
    ["tinyobjloader"]="https://github.com/tinyobjloader/tinyobjloader.git"
)

declare -A GITEE_SOURCES=(
    ["entt"]="https://gitee.com/mirrors_Esri/entt.git"
    ["nlohmann_json"]="https://gitee.com/mirrors_back/nlohmann-json.git"
    ["ixwebsocket"]="https://gitee.com/efreets/IXWebSocket.git"
    ["imgui"]="https://gitee.com/mirrors/ImGUI_old1.git"
    ["tinygltf"]="https://gitee.com/mirrors_syoyo/tinygltf_1.git"
    ["tinyobjloader"]="https://gitee.com/mirrors_syoyo/tinyobjloader-c.git"
)

declare -A DEP_VERSIONS=(
    ["entt"]="v3.13.2"
    ["nlohmann_json"]="v3.11.3"
    ["ixwebsocket"]="master"
    ["imgui"]="v1.91.8"
    ["tinygltf"]="v2.9.3"
    ["tinyobjloader"]="v2.0.0rc13"
)

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
            echo -e "${YELLOW}Cleaning external dependencies...${NC}"
            rm -rf external
            echo -e "${YELLOW}Cleaning config files...${NC}"
            rm -f imgui.ini
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
        all)
            BUILD_TARGET="all"
            ;;
        --github)
            FORCE_SOURCE="github"
            ;;
        --gitee)
            FORCE_SOURCE="gitee"
            ;;
        help|--help|-h)
            echo "OverWrite 构建脚本"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  release      构建 release 版本（默认）"
            echo "  debug        构建 debug 版本"
            echo "  clean        清理构建目录、着色器和依赖"
            echo "  run          构建并运行客户端"
            echo "  run-server   构建并运行服务端"
            echo "  client       仅构建客户端"
            echo "  all          构建所有目标（默认）"
            echo "  --github     强制使用 GitHub 源下载依赖"
            echo "  --gitee      强制使用 Gitee 源下载依赖"
            echo "  help         显示帮助信息"
            echo ""
            echo "Targets:"
            echo -e "  ${CYAN}OverWrite${NC}        - 客户端（Vulkan 渲染）"
            echo -e "  ${CYAN}overwrite-server${NC} - 服务端（WebSocket）"
            echo ""
            echo "Note: 默认自动检测网络，优先使用 GitHub，无法访问时切换到 Gitee"
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

# 检测网络并选择源
detect_and_select_source() {
    if [ -n "$FORCE_SOURCE" ]; then
        echo -e "${GREEN}Using forced source: ${CYAN}${FORCE_SOURCE}${NC}"
        SELECTED_SOURCE="$FORCE_SOURCE"
        return
    fi
    
    echo -e "${GREEN}Detecting network connectivity...${NC}"
    
    # 尝试连接 GitHub（超时 3 秒）
    if timeout 3 git ls-remote https://github.com/skypjack/entt.git HEAD &>/dev/null; then
        echo -e "  ${GREEN}GitHub is accessible${NC}"
        SELECTED_SOURCE="github"
    else
        echo -e "  ${YELLOW}GitHub is not accessible, using Gitee mirror${NC}"
        SELECTED_SOURCE="gitee"
    fi
}

# 检查并切换依赖到指定版本（只在需要时执行）
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

# 克隆单个依赖
clone_dependency() {
    local name=$1
    local version=$2
    local dir="external/$name"
    
    if [ ! -d "$dir" ]; then
        # 根据选择的源获取 URL
        if [ "$SELECTED_SOURCE" = "gitee" ]; then
            local url="${GITEE_SOURCES[$name]}"
        else
            local url="${GITHUB_SOURCES[$name]}"
        fi
        
        echo "  Cloning $name ($version) from $SELECTED_SOURCE..."
        
        # 尝试克隆指定版本（临时禁用 set -e）
        set +e
        if [ "$version" = "master" ]; then
            git clone --depth 1 "$url" "$dir" 2>&1
            local result=$?
        else
            git clone --depth 1 --branch "$version" "$url" "$dir" 2>&1
            local result=$?
        fi
        set -e
        
        # 如果失败，尝试备用源
        if [ $result -ne 0 ]; then
            echo -e "  ${YELLOW}Failed to clone from $SELECTED_SOURCE, trying alternate source...${NC}"
            if [ "$SELECTED_SOURCE" = "gitee" ]; then
                local alt_url="${GITHUB_SOURCES[$name]}"
            else
                local alt_url="${GITEE_SOURCES[$name]}"
            fi
            
            set +e
            if [ "$version" = "master" ]; then
                git clone --depth 1 "$alt_url" "$dir" 2>&1
                result=$?
            else
                git clone --depth 1 --branch "$version" "$alt_url" "$dir" 2>&1
                result=$?
            fi
            set -e
            
            if [ $result -ne 0 ]; then
                echo -e "  ${RED}Failed to clone $name from both sources!${NC}"
                return 1
            fi
        fi
    fi
    return 0
}

# 初始化依赖
if [ -f ".gitmodules" ]; then
    # 检查是否有未初始化的依赖
    NEED_INIT=0
    
    # 首先检查 external 目录是否存在
    if [ ! -d "external" ]; then
        NEED_INIT=1
    else
        # 检查各个依赖是否已存在
        for dep in external/entt external/nlohmann_json external/ixwebsocket external/imgui external/tinygltf external/tinyobjloader; do
            if [ ! -d "$dep" ]; then
                NEED_INIT=1
                break
            fi
        done
    fi
    
    if [ $NEED_INIT -eq 1 ]; then
        # 检测网络并选择源
        detect_and_select_source
        
        echo -e "${GREEN}Initializing dependencies...${NC}"
        
        # 创建 external 目录
        mkdir -p external
        
        # 克隆所有依赖
        FAILED_COUNT=0
        for name in entt nlohmann_json ixwebsocket imgui tinygltf tinyobjloader; do
            if ! clone_dependency "$name" "${DEP_VERSIONS[$name]}"; then
                FAILED_COUNT=$((FAILED_COUNT + 1))
            fi
        done
        
        if [ $FAILED_COUNT -gt 0 ]; then
            echo -e "${RED}Failed to clone $FAILED_COUNT dependencies!${NC}"
            echo -e "${YELLOW}Please check your network connection and try again.${NC}"
            exit 1
        fi
    fi
    
    # 检查并切换依赖到指定版本
    echo -e "${GREEN}Checking dependency versions...${NC}"
    checkout_if_needed "external/entt" "v3.13.2"
    checkout_if_needed "external/nlohmann_json" "v3.11.3"
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
