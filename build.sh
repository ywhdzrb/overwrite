#!/bin/bash

# Usage:
#   ./build.sh              # 默认构建 release 版本
#   ./build.sh release      # 构建 release 版本
#   ./build.sh debug        # 构建 debug 版本
#   ./build.sh clean        # 清理构建目录、着色器和依赖
#   ./build.sh run          # 构建并运行客户端
#   ./build.sh run-server   # 构建并运行服务端
#   ./build.sh all          # 构建所有目标（默认）
#   ./build.sh test         # 构建并运行单元测试（需联网下载 GTest）
#   ./build.sh package      # 打包为可分发 tar.xz
#   ./build.sh dashboard    # 生成项目综合仪表盘（Python3 + matplotlib）

# Colors for output
RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
YELLOW=$'\033[1;33m'
BLUE=$'\033[0;34m'
CYAN=$'\033[0;36m'
NC=$'\033[0m'
# =====================================================================
# 默认构建类型和目标
BUILD_TYPE="Release"
RUN_AFTER_BUILD=false
RUN_SERVER=false
BUILD_TARGET="all"
BUILD_TESTS="OFF"
MODE="build"

# 版本号
VERSION="$(cat VERSION 2>/dev/null || echo 'unknown')"
ORIG_DIR="$(pwd)"

# ====================== 打包函数 ======================

package() {
    local pkg_dir="overwrite-${VERSION}-linux-x86_64"
    local pkg_tarball="${pkg_dir}.tar.xz"

    echo -e "${GREEN}=================================="
    echo "Packaging ${pkg_dir}..."
    echo "=================================="

    rm -rf "${pkg_dir}" "${pkg_tarball}"
    mkdir -p "${pkg_dir}/bin"

    echo -e "  ${CYAN} * ${NC}拷贝二进制文件..."
    [ -f "build/OverWrite" ] && cp "build/OverWrite" "${pkg_dir}/bin/OverWrite"
    [ -f "build/overwrite-server" ] && cp "build/overwrite-server" "${pkg_dir}/bin/overwrite-server"

    echo -e "  ${CYAN} * ${NC}拷贝素材文件..."
    [ -d "assets" ] && cp -r assets "${pkg_dir}/assets"

    echo -e "  ${CYAN} * ${NC}拷贝配置文件..."
    [ -d "config" ] && cp -r config "${pkg_dir}/config"

    echo -e "  ${CYAN} * ${NC}拷贝已编译着色器..."
    if ls shaders/*.spv 1>/dev/null 2>&1; then
        mkdir -p "${pkg_dir}/shaders"
        cp shaders/*.spv "${pkg_dir}/shaders/"
    fi

    echo -e "  ${CYAN} * ${NC}拷贝版本文件..."
    [ -f "VERSION" ] && cp VERSION "${pkg_dir}/"

    echo -e "  ${CYAN} * ${NC}生成启动脚本..."
    cat > "${pkg_dir}/run.sh" << 'RUNEOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
exec ./bin/OverWrite "$@"
RUNEOF
    chmod +x "${pkg_dir}/run.sh"

    cat > "${pkg_dir}/run-server.sh" << 'RUNEOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
exec ./bin/overwrite-server "$@"
RUNEOF
    chmod +x "${pkg_dir}/run-server.sh"

    echo -e "  ${CYAN} * ${NC}压缩为 tar.xz..."
    tar -cJf "${pkg_tarball}" "${pkg_dir}"
    rm -rf "${pkg_dir}"

    echo ""
    echo -e "${GREEN}=================================="
    echo "Package created: ${pkg_tarball}"
    echo "Size: $(ls -lh "${pkg_tarball}" | awk '{print $5}')"
    echo "=================================="
    echo ""
    echo -e "使用方式:"
    echo "  tar -xf ${pkg_tarball} && cd ${pkg_dir} && ./run.sh"
}

# ====================== 参数解析 ======================
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
            rm -rf "${SCRIPT_DIR}/build"
            echo -e "${YELLOW}Cleaning compiled shaders (.spv files)...${NC}"
            find . -name "*.spv" -type f -delete
            echo -e "${YELLOW}Cleaning external dependencies (submodules)...${NC}"
            git submodule deinit --all --force 2>/dev/null || true
            echo -e "${YELLOW}Cleaning config files...${NC}"
            rm -f imgui.ini
            echo -e "${YELLOW}Cleaning CI build artifacts...${NC}"
            rm -rf "${SCRIPT_DIR}/build-ci"
            echo -e "${YELLOW}Cleaning dashboard artifacts...${NC}"
            rm -f dashboard/dashboard.png
            echo -e "${GREEN}Clean completed!${NC}"
            exit 0
            ;;
        run|Run)
            if [ -f "${ORIG_DIR}/build/OverWrite" ]; then
                echo -e "${BLUE}发现已有编译结果，直接运行...${NC}"
                cd "${ORIG_DIR}"
                unset GLFW_PLATFORM
                ./build/OverWrite
                exit $?
            fi
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
        package)
            MODE="package"
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
echo "  package      打包为可分发 tar.xz（二进制 + 运行库 + 素材 + 配置）"
echo "  all          构建所有目标（默认）"
echo "  test         构建并运行单元测试"
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

build_result=$?
if [ $build_result -ne 0 ]; then
    echo ""
    echo -e "${RED}=================================="
    echo "Build failed!"
    echo "=================================="
    exit $build_result
fi

echo ""
echo -e "${GREEN}=================================="
echo "Build completed successfully!"
echo "=================================="
echo -e "Targets built:"
echo -e "  ${CYAN}OverWrite${NC}        - ${YELLOW}./build/OverWrite${NC} (客户端)"
echo -e "  ${CYAN}overwrite-server${NC} - ${YELLOW}./build/overwrite-server${NC} (服务端)"
echo ""

# 如果指定了 package 模式，执行打包（必须在 cd build 之前调用，因为函数定义在 if 块之外）
if [ "$MODE" = "package" ]; then
    cd "${ORIG_DIR:-$OLDPWD}"
    package
    exit 0
fi

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