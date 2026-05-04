#!/bin/bash
#
# OverWrite 发布打包脚本
# 编译项目 → 下载 assets 资源包 → 打包可执行文件 + 运行库 + 素材 → tar.xz
#
# Usage:
#   ./scripts/package_release.sh              # 使用本地构建 + 本地 assets
#   ./scripts/package_release.sh --download   # 从 GitHub Release 下载 assets 包
#   ./scripts/package_release.sh --version v0.1.0-alpha  # 指定版本
#
# 输出: overwrite-<version>-linux.tar.xz

set -e

# ==================== 配置 ====================
VERSION="0.1.0-alpha"
RELEASE_TAG="v${VERSION}"
REPO="ywhdzrb/overwrite"
OUTPUT_DIR="/tmp/overwrite-package-$$"
ASSETS_URL="https://github.com/${REPO}/releases/download/${RELEASE_TAG}/overwrite-assets-${VERSION}.tar.xz"
DOWNLOAD_ASSETS=false
BUILD_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# ==================== 解析参数 ====================
for arg in "$@"; do
    case $arg in
        --download)
            DOWNLOAD_ASSETS=true
            ;;
        --version=*)
            VERSION="${arg#*=}"
            RELEASE_TAG="v${VERSION}"
            ASSETS_URL="https://github.com/${REPO}/releases/download/${RELEASE_TAG}/overwrite-assets-${VERSION}.tar.xz"
            ;;
        --help|-h)
            echo "OverWrite 发布打包脚本"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --download          从 GitHub Release 下载 assets 资源包"
            echo "  --version=VERSION   指定版本号（默认: ${VERSION}）"
            echo "  --help              显示帮助信息"
            exit 0
            ;;
    esac
done

echo "=================================="
echo "OverWrite 发布打包"
echo "=================================="
echo "版本: ${VERSION}"
echo "构建目录: ${BUILD_DIR}"
echo "输出目录: ${OUTPUT_DIR}"
echo ""

# ==================== 第1步：构建项目 ====================
echo -e "[1/4] 构建项目..."
cd "${BUILD_DIR}"
./build.sh release
echo ""

# ==================== 第2步：准备资源包 ====================
echo -e "[2/4] 准备资源包..."

if [ "$DOWNLOAD_ASSETS" = true ]; then
    # 从 GitHub Release 下载 assets
    echo "  从 Release 下载 assets 包..."
    mkdir -p "${OUTPUT_DIR}/assets"
    cd "${OUTPUT_DIR}"
    echo "  下载: ${ASSETS_URL}"
    curl -L -o "assets.tar.xz" "${ASSETS_URL}"
    tar -xJf "assets.tar.xz" -C "${OUTPUT_DIR}"
    rm "assets.tar.xz"
    cd "${BUILD_DIR}"
else
    # 使用本地 assets
    if [ -d "assets" ]; then
        echo "  使用本地 assets/ 目录"
        mkdir -p "${OUTPUT_DIR}"
        cp -r "assets" "${OUTPUT_DIR}/assets"
    else
        echo "  ERROR: assets/ 目录不存在！"
        echo "  请先下载 assets 资源包："
        echo "    curl -L -o /tmp/assets.tar.xz ${ASSETS_URL}"
        echo "    tar -xJf /tmp/assets.tar.xz"
        exit 1
    fi
fi
echo ""

# ==================== 第3步：收集可执行文件和运行库 ====================
echo -e "[3/4] 收集可执行文件和运行库..."

mkdir -p "${OUTPUT_DIR}/bin"

# 复制可执行文件
cp "build/OverWrite" "${OUTPUT_DIR}/bin/"
cp "build/overwrite-server" "${OUTPUT_DIR}/bin/"

# 复制编译好的着色器（运行时需要）
if [ -d "shaders" ]; then
    cp -r "shaders" "${OUTPUT_DIR}/shaders"
fi

# 复制配置文件
if [ -d "config" ]; then
    cp -r "config" "${OUTPUT_DIR}/config"
fi

# 复制启动脚本
cat > "${OUTPUT_DIR}/run.sh" << 'RUNEOF'
#!/bin/bash
# OverWrite 启动脚本
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}"

# 运行客户端
if [ "$1" = "server" ]; then
    exec ./bin/overwrite-server "$@"
else
    exec ./bin/OverWrite "$@"
fi
RUNEOF
chmod +x "${OUTPUT_DIR}/run.sh"

echo ""

# ==================== 第4步：打包 ====================
echo -e "[4/4] 打包发行版..."
PACKAGE_NAME="overwrite-${VERSION}-linux"
cd /tmp
mv "${OUTPUT_DIR}" "${PACKAGE_NAME}"
export XZ_OPT="-9e -T0"
tar -cJf "${BUILD_DIR}/${PACKAGE_NAME}.tar.xz" "${PACKAGE_NAME}"
rm -rf "${PACKAGE_NAME}"

echo ""
echo "=================================="
echo "打包完成！"
echo "=================================="
echo "输出文件: ${BUILD_DIR}/${PACKAGE_NAME}.tar.xz"
echo ""
echo "文件大小: $(du -h "${BUILD_DIR}/${PACKAGE_NAME}.tar.xz" | cut -f1)"
echo ""
echo "使用方法："
echo "  tar -xJf ${PACKAGE_NAME}.tar.xz"
echo "  cd ${PACKAGE_NAME}"
echo "  ./run.sh          # 运行客户端"
echo "  ./run.sh server   # 运行服务端"
