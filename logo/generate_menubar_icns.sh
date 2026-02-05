#!/bin/bash
#
# 生成 macOS 菜单栏专用的 .icns 文件（长方形 logo）
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

ICONSET_DIR="menubar.iconset"
OUTPUT_FILE="sime_menubar.icns"

echo "Creating macOS menubar icon (.icns) from rectangular logos..."

# 清理旧文件
rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

# macOS .icns 文件需要特定的命名规则
# 对于长方形图标，我们使用最接近的尺寸，系统会自动缩放

# 复制并重命名文件到 iconset
# 注意：macOS 期望的是正方形图标，但长方形也可以工作
# 我们使用高度作为尺寸参考

# 16x16 和 16x16@2x (使用 17 高度的版本，接近 16)
cp sime_minimal_macos_22x17.png "$ICONSET_DIR/icon_16x16.png"
cp sime_minimal_macos_26x20.png "$ICONSET_DIR/icon_16x16@2x.png"

# 32x32 和 32x32@2x (使用 25 高度的版本，接近 32，以及 40 高度的 @2x)
cp sime_minimal_macos_32x25.png "$ICONSET_DIR/icon_32x32.png"
cp sime_minimal_macos_52x40.png "$ICONSET_DIR/icon_32x32@2x.png"

echo "Created iconset directory: $ICONSET_DIR"
ls -lh "$ICONSET_DIR"

# 使用 iconutil 生成 .icns 文件
if command -v iconutil &> /dev/null; then
    iconutil -c icns "$ICONSET_DIR" -o "$OUTPUT_FILE"
    echo "✓ Generated: $OUTPUT_FILE"
    ls -lh "$OUTPUT_FILE"
else
    echo "WARNING: iconutil not found (only available on macOS)"
    echo "You can run this script on macOS to generate the .icns file"
fi

# 清理 iconset 目录
rm -rf "$ICONSET_DIR"

echo ""
echo "Done! Use $OUTPUT_FILE for macOS menubar icon."
