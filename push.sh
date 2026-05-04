#!/bin/bash
#
# OverWrite 推送脚本
# 普通模式：git add → commit → push
# 发布模式：构建 → 打包上传 Release
#
# Usage:
#   ./push.sh                          # 交互式 git push
#   ./push.sh -m "说明"                # 直接指定 message
#   ./push.sh --push-only              # 仅 push（跳过 add/commit）
#   ./push.sh --release                # 发布打包
#   ./push.sh --release --download     # 发布打包 + 从 Release 下载 assets
#
# 输出（发布模式）: overwrite-<version>-linux.tar.xz

set -e

# ==================== 配置 ====================
VERSION="0.1.0-alpha"
RELEASE_TAG="v${VERSION}"
REPO="ywhdzrb/overwrite"
OUTPUT_DIR="/tmp/overwrite-package-$$"
ASSETS_URL="https://github.com/${REPO}/releases/download/${RELEASE_TAG}/overwrite-assets-${VERSION}.tar.xz"
DOWNLOAD_ASSETS=false
RELEASE_MODE=false
COMMIT_MSG=""
PUSH_ONLY=false
BUNDLE_LIBS=false
BUILD_DIR="$(cd "$(dirname "$0")" && pwd)"

# ==================== 解析参数 ====================
for arg in "$@"; do
    case $arg in
        --release)
            RELEASE_MODE=true
            ;;
        --download)
            DOWNLOAD_ASSETS=true
            ;;
        --bundle)
            BUNDLE_LIBS=true
            ;;
        -m=*)
            COMMIT_MSG="${arg#*=}"
            ;;
        --push-only)
            PUSH_ONLY=true
            ;;
        --version=*)
            VERSION="${arg#*=}"
            RELEASE_TAG="v${VERSION}"
            ASSETS_URL="https://github.com/${REPO}/releases/download/${RELEASE_TAG}/overwrite-assets-${VERSION}.tar.xz"
            ;;
        --help|-h)
            echo "OverWrite 推送脚本"
            echo ""
            echo "Usage: ./push.sh [options]"
            echo ""
            echo "普通模式："
            echo "  ./push.sh              交互式输入 commit message"
            echo "  ./push.sh -m \"说明\"     直接 push"
            echo "  ./push.sh --push-only   仅 git push"
            echo ""
            echo "发布模式："
            echo "  ./push.sh --release                       构建+打包（assets 不存在时自动下载）"
            echo "  ./push.sh --release --download            强制重新下载 assets 资源包"
            echo "  ./push.sh --release --bundle              捆绑运行时 .so 库（推荐）"
            echo "  ./push.sh --release --download --bundle   全部一起"
            echo ""
            echo "版本："
            echo "  --version=VERSION   指定版本（默认: ${VERSION}）"
            exit 0
            ;;
    esac
done

# 处理 -m 后面跟的 message
if [[ "$*" == *"-m"* ]] && [ -z "$COMMIT_MSG" ]; then
    for i in $(seq 1 $#); do
        if [ "${!i}" = "-m" ]; then
            next=$((i + 1))
            COMMIT_MSG="${!next}"
            break
        fi
    done
fi

# ==================== 发布模式 ====================
if [ "$RELEASE_MODE" = true ]; then
    TOTAL_STEPS=4
    [ "$BUNDLE_LIBS" = true ] && TOTAL_STEPS=5

    echo "=================================="
    echo "OverWrite 发布打包"
    echo "=================================="
    echo "版本: ${VERSION}"
    echo "构建目录: ${BUILD_DIR}"
    echo ""

    # 第1步：构建
    echo "[1/${TOTAL_STEPS}] 构建项目..."
    cd "${BUILD_DIR}"
    ./build.sh release
    echo ""

    # 第2步：资源包
    echo "[2/${TOTAL_STEPS}] 准备资源包..."

    # 先检查本地 assets/ 是否存在
    if [ "$DOWNLOAD_ASSETS" = false ] && [ -d "assets" ]; then
        echo "  使用本地 assets/ 目录"
        mkdir -p "${OUTPUT_DIR}"
        cp -r "assets" "${OUTPUT_DIR}/assets"
    else
        if [ "$DOWNLOAD_ASSETS" = true ]; then
            echo "  强制从 Release 下载（--download）..."
        else
            echo "  本地 assets/ 不存在，自动从 Release 下载..."
        fi
        mkdir -p "${OUTPUT_DIR}/assets"
        cd "${OUTPUT_DIR}"
        echo "  下载: ${ASSETS_URL}"
        curl -L -o "assets.tar.xz" "${ASSETS_URL}"
        tar -xJf "assets.tar.xz" -C "${OUTPUT_DIR}"
        rm "assets.tar.xz"
        cd "${BUILD_DIR}"
    fi
    echo ""

    # 第3步：收集文件
    echo "[3/${TOTAL_STEPS}] 收集可执行文件..."
    mkdir -p "${OUTPUT_DIR}/bin"
    cp "build/OverWrite" "${OUTPUT_DIR}/bin/"
    cp "build/overwrite-server" "${OUTPUT_DIR}/bin/"
    [ -d "shaders" ] && cp -r "shaders" "${OUTPUT_DIR}/shaders"
    [ -d "config" ] && cp -r "config" "${OUTPUT_DIR}/config"
    echo ""

    # 第4步：捆绑运行库（可选）
    if [ "$BUNDLE_LIBS" = true ]; then
        echo "[4/${TOTAL_STEPS}] 捆绑运行库..."
        mkdir -p "${OUTPUT_DIR}/lib"

        declare -A _seen
        for _bin in OverWrite overwrite-server; do
            while IFS=$' \t' read -r _name _arrow _libpath _; do
                [[ "$_arrow" != "=>" ]] && continue
                [ -z "$_libpath" ] && continue
                [[ "$_libpath" != /* ]] && continue

                _libname=$(basename "$_libpath")

                # 跳过系统核心库（所有发行版标配）
                case "$_libname" in
                    libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1)
                        continue ;;
                    ld-linux-x86-64.so.2|ld-linux.so.2|ld-linux-aarch64.so.1)
                        continue ;;
                    libutil.so.1|libresolv.so.2)
                        continue ;;
                    libnss_*)
                        continue ;;
                esac

                [[ -z "${_seen[$_libname]}" ]] || continue
                _seen[$_libname]=1
                cp -L "$_libpath" "${OUTPUT_DIR}/lib/"
                echo "  + ${_libname}"
            done < <(ldd "${OUTPUT_DIR}/bin/${_bin}")
        done
        unset _seen _bin _name _arrow _libpath _libname

        # 设置 RPATH，让二进制优先在 lib/ 找 so
        if command -v patchelf &>/dev/null; then
            patchelf --set-rpath '$ORIGIN/../lib' "${OUTPUT_DIR}/bin/OverWrite"
            patchelf --set-rpath '$ORIGIN/../lib' "${OUTPUT_DIR}/bin/overwrite-server"
            echo "  RPATH 已设置 (\$ORIGIN/../lib)"
        fi
        echo "  已捆绑 $(ls "${OUTPUT_DIR}/lib" | wc -l) 个动态库"
        echo ""
    fi

    # 生成启动脚本（需在捆绑步骤之后，以获取 LD_LIBRARY_PATH）
    cat > "${OUTPUT_DIR}/run.sh" << RUNEOF
#!/bin/bash
SCRIPT_DIR="\$(cd "\$(dirname "\$0")" && pwd)"
cd "\${SCRIPT_DIR}"

# 优先使用捆绑的运行库
if [ -d "\${SCRIPT_DIR}/lib" ]; then
    export LD_LIBRARY_PATH="\${SCRIPT_DIR}/lib\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
fi

if [ "\$1" = "server" ]; then
    exec ./bin/overwrite-server "\$@"
else
    exec ./bin/OverWrite "\$@"
fi
RUNEOF
    chmod +x "${OUTPUT_DIR}/run.sh"

    # 第5步（或第4步）：打包
    if [ "$BUNDLE_LIBS" = true ]; then
        echo "[5/${TOTAL_STEPS}] 打包..."
    else
        echo "[4/${TOTAL_STEPS}] 打包..."
    fi
    PACKAGE_NAME="overwrite-${VERSION}-linux"
    cd /tmp
    mv "${OUTPUT_DIR}" "${PACKAGE_NAME}"
    export XZ_OPT="-9e -T0"
    tar -cJf "${BUILD_DIR}/${PACKAGE_NAME}.tar.xz" "${PACKAGE_NAME}"
    rm -rf "${PACKAGE_NAME}"

    echo ""
    echo "打包完成: ${BUILD_DIR}/${PACKAGE_NAME}.tar.xz"
    echo "大小: $(du -h "${BUILD_DIR}/${PACKAGE_NAME}.tar.xz" | cut -f1)"
    exit 0
fi

# ==================== 普通模式：git push ====================
cd "${BUILD_DIR}"

# 检查是否真的需要 push
HAS_CHANGES=false
git diff --quiet || HAS_CHANGES=true
git diff --cached --quiet || HAS_CHANGES=true
[ -n "$(git ls-files --others --exclude-standard)" ] && HAS_CHANGES=true

if [ "$HAS_CHANGES" = false ] && [ "$PUSH_ONLY" = false ]; then
    echo "没有待提交的变更。"
    exit 0
fi

# --push-only
if [ "$PUSH_ONLY" = true ]; then
    echo "git push..."
    git push
    echo "推送完成。"
    exit 0
fi

# add
echo "git add -A"
git add -A
echo ""
echo "===== 变更 ====="
git status --short
echo "================"
echo ""

# commit
if [ -n "$COMMIT_MSG" ]; then
    git commit -m "$COMMIT_MSG"
else
    echo "输入 commit message（回车取消）："
    read -r MSG
    [ -z "$MSG" ] && echo "已取消。" && exit 0
    git commit -m "$MSG"
fi

# push
echo ""
echo "git push..."
git push
echo "推送完成。"
