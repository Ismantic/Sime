# Sime (是语输入法) Logo 设计

本目录包含为 Sime 输入法设计的多种风格的 Logo，适用于状态栏、应用图标等场景。

## 设计方案

### 1. "是"字设计 (`sime_shi_*`)
- **风格**: 圆角方形 + "是"字
- **配色**: 蓝色渐变
- **适用**: 突出中文输入法特性
- **文件**: `sime_shi_*.png`, `sime_shi.svg`

### 2. 字母 "S" 设计 (`sime_s_*`)
- **风格**: 圆角方形 + 字母 S
- **配色**: 靛蓝色
- **适用**: 国际化、现代简约风格
- **文件**: `sime_s_*.png`, `sime_s.svg`

### 3. 抽象 "语" 设计 (`sime_yu_*`)
- **风格**: 圆形 + 抽象表情(两点一横)
- **配色**: 紫蓝渐变
- **适用**: 可爱、亲和风格
- **文件**: `sime_yu_*.png`, `sime_yu.svg`

### 4. 极简设计 (`sime_minimal_*`)
- **风格**: 圆角方形 + 三条横线(代表输入)
- **配色**: 青绿色
- **适用**: 极简主义、现代风格
- **文件**: `sime_minimal_*.png`, `sime_minimal.svg`

#### macOS 版本 (`sime_minimal_macos_*`)
专门为 macOS 状态栏优化的版本，比例调整为 **1.3:1**（横向稍宽）：
- `sime_minimal_macos_22x17.png` - 标准状态栏尺寸 (~1.29:1)
- `sime_minimal_macos_29x22.png` - 精确 1.3:1 比例
- `sime_minimal_macos_44x34.png` - Retina @2x 尺寸 (~1.29:1)
- `sime_minimal_macos_52x40.png` - 大图标 Retina (1.3:1)

### 5. "语"字设计 (`sime_yu_char_*`)
- **风格**: 圆形 + "语"字
- **配色**: 深色背景
- **适用**: 强调"语言"概念
- **文件**: `sime_yu_char_*.png`

## 尺寸规格

| 尺寸 | 用途 |
|------|------|
| 16x16 | 最小状态栏图标 |
| 22x22 | Linux/Windows 状态栏 |
| 24x24 | macOS 状态栏 |
| 32x32 | 应用小图标 |
| 48x48 | 应用图标 |
| 64x64 | 应用图标/Retina |
| 128x128 | 高清应用图标 |
| 256x256 | 超高清单应用图标 |

## 使用建议

### Fcitx5 状态栏
- **图标文件**: `sime_minimal_48x48.png`
- **配置位置**: `fcitx5/data/icon/sime.png`
- **配置文件**: `sime.conf.in` 中设置 `Icon=sime`

### macOS 菜单栏和应用图标

#### 应用图标（系统偏好设置、输入法选择器）
- **图标文件**: `sime_minimal.icns`（正方形）
- **配置**: Info.plist 中 `CFBundleIconFile=sime.icns`

#### 菜单栏图标（屏幕顶部状态栏）
- **图标文件**: `sime_menubar.icns` 或 `sime_menubar.png`（长方形 1.3:1）
- **配置**: Info.plist 中 `tsInputMethodIconFileKey=sime_menubar`
- **生成方法**: 运行 `./generate_menubar_icns.sh` 生成 .icns 文件
- **源文件**:
  - `sime_minimal_macos_29x22.png` - 标准分辨率
  - `sime_minimal_macos_44x34.png` - Retina @2x

### 应用图标
推荐使用 **64x64** 或更大尺寸，或直接使用 SVG 矢量文件。

## SVG 文件

提供了 SVG 矢量版本，可以无限缩放：
- `sime_shi.svg` - "是"字设计
- `sime_s.svg` - 字母 S 设计
- `sime_yu.svg` - 抽象"语"设计
- `sime_minimal.svg` - 极简设计

## 技术说明

### Logo 生成
- **通用 Logo**: `generate_logo.py` - 生成各种风格的正方形 Logo
- **macOS 长方形**: `generate_macos_logo.py` - 生成 1.3:1 比例的菜单栏 Logo
- **菜单栏 .icns**: `generate_menubar_icns.sh` - 将 PNG 打包成 .icns 文件（需要 macOS）

### 构建集成
- **Fcitx5**: CMakeLists.txt 自动复制图标到安装目录
- **macOS**: CMakeLists.txt 在构建时自动生成并复制图标到 bundle（需要 macOS 环境）

## 推荐选择

### 当前使用（统一为 sime_minimal）
1. **Fcitx5**: `sime_minimal` - 极简设计，48x48 正方形
2. **macOS 应用**: `sime_minimal` - 极简设计，正方形 .icns
3. **macOS 菜单栏**: `sime_minimal_macos` - 极简设计，长方形 1.3:1

### 其他可选风格
1. **"是"字设计** (`sime_shi`) - 最能代表"是语输入法"品牌
2. **字母 S 设计** (`sime_s`) - 国际化且简洁
3. **抽象"语"设计** (`sime_yu`) - 可爱、亲和
