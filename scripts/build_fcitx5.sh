#!/bin/bash
# Fcitx5 Sime 输入法构建脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-fcitx5"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║          Fcitx5 Sime 输入法构建脚本                         ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo

# 检查依赖
echo "📦 检查依赖..."
MISSING_DEPS=()

if ! pkg-config --exists Fcitx5Core; then
    MISSING_DEPS+=("Fcitx5Core")
fi

if ! pkg-config --exists Fcitx5Utils; then
    MISSING_DEPS+=("Fcitx5Utils")
fi

if [[ ${#MISSING_DEPS[@]} -gt 0 ]]; then
    echo "❌ 缺少依赖: ${MISSING_DEPS[*]}"
    echo
    echo "在 Arch Linux 上安装:"
    echo "  sudo pacman -S fcitx5 fcitx5-qt fcitx5-gtk extra-cmake-modules"
    echo
    echo "在 Ubuntu/Debian 上安装:"
    echo "  sudo apt install fcitx5 fcitx5-modules-dev extra-cmake-modules"
    exit 1
fi

echo "✓ 依赖检查通过"
echo

# 检查数据文件
echo "📁 检查数据文件..."
DICT_FILE="$PROJECT_ROOT/pydict_sc.ime.bin"
LM_FILE="$PROJECT_ROOT/lm_sc.t3g"

if [[ ! -f "$DICT_FILE" ]]; then
    echo "❌ 未找到词典文件: $DICT_FILE"
    echo "   请先运行 trie_conv 转换词典"
    exit 1
fi

if [[ ! -f "$LM_FILE" ]]; then
    echo "❌ 未找到语言模型: $LM_FILE"
    echo "   请从 sunpinyin-data 包中复制"
    exit 1
fi

echo "✓ 数据文件检查通过"
echo

# 创建构建目录
echo "📁 创建构建目录: $BUILD_DIR"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置
echo
echo "⚙️  配置项目..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_FCITX5=ON \
      -DSIME_ENABLE_SIMD=ON \
      "$PROJECT_ROOT"

# 构建
echo
echo "🔨 构建项目..."
make -j$(nproc)

# 检查构建产物
if [[ ! -f "fcitx5/sime.so" ]]; then
    echo
    echo "❌ 构建失败: fcitx5/sime.so 未生成"
    exit 1
fi

echo
echo "✅ 构建成功！"
echo
echo "插件位置: $BUILD_DIR/fcitx5/sime.so"
echo "大小: $(du -h "$BUILD_DIR/fcitx5/sime.so" | cut -f1)"
echo

# 询问是否安装
read -p "是否安装到系统？(需要 sudo) [y/N]: " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo
    echo "📦 安装..."

    # 安装数据文件
    echo "安装数据文件到 /usr/share/sime/"
    sudo mkdir -p /usr/share/sime
    sudo cp "$DICT_FILE" /usr/share/sime/
    sudo cp "$LM_FILE" /usr/share/sime/

    # 安装插件
    echo "安装插件..."
    sudo make install

    echo
    echo "✅ 安装完成！"
    echo
    echo "下一步:"
    echo "  1. 重启 fcitx5:"
    echo "     fcitx5 -r"
    echo
    echo "  2. 打开配置工具添加 Sime 输入法:"
    echo "     fcitx5-configtool"
    echo
    echo "  3. 在任意文本编辑器中测试输入"
else
    echo
    echo "⏭️  跳过安装"
    echo
    echo "手动安装步骤:"
    echo "  cd $BUILD_DIR"
    echo "  sudo make install"
fi
