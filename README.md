<p align="center">
  <img src="assets/icon.png" width="120" alt="Sime icon">
</p>

<h1 align="center">是语 Sime</h1>

<p align="center">
  开源中文输入法
</p>

<p align="center">
  <a href="https://github.com/Ismantic/Sime/releases/latest">
    <img src="https://img.shields.io/github/v/release/Ismantic/Sime?style=flat-square" alt="Release">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/github/license/Ismantic/Sime?style=flat-square" alt="License">
  </a>
</p>

---

## 特性

- **语言模型驱动**，支持百亿字数语料训练
- **多平台应用**，当前支持 Linux 与 Android（TODO: macOS 与 iOS）
- **多输入方式**，全键盘与九宫格，全拼与简拼，中文与英文
- **联想 / 繁简 / 表情 / 符号** 多维度支持

---

## 下载

最新版本见 [Releases](https://github.com/Ismantic/Sime/releases/latest)。

### Android

下载 `app-release.apk` 后传到手机，用文件管理器安装；或：

```bash
adb install app-release.apk
```

安装后到「设置 → 通用 → 输入法」中启用是语并切换为默认。

### Linux (Arch)

```bash
sudo pacman -U fcitx5-sime-*.pkg.tar.zst
fcitx5 -r
```

然后在 fcitx5 配置中添加「Sime」输入法。

---

## 隐私

所有输入与剪贴板内容仅保存于本机，不存在任何上传、收集、统计行为。完整说明见 [PRIVACY.md](PRIVACY.md)。

---

## For Developers

### 构建

```bash
# C++ 引擎 + CLI 工具
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 含 Fcitx5 插件
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSIME_ENABLE_FCITX5=ON
cmake --build build
```

Android 用自己的 Gradle/NDK 构建（见 `Android/BUILD.md`），不走根 CMake。

### 交互式 CLI

拼音 / T9 / 联想：

```bash
./build/sime --dict pipeline/output/sime.dict --cnt pipeline/output/sime.cnt -s -n 1
```

```
# 全拼整句
> cengjingcanghainanweishui
  [0] 曾经沧海难为水 [ceng'jing'cang'hai'nan'wei'shui] (score -16.361, matched 25/25, ids: 13654 91952 118630)

# 简拼
> h'n'w's
  [0] 湖南卫视 [h'n'w's] (score -24.936, matched 7/7, ids: 52706)

# 简拼 + 全拼混合
> r'm'wansu
  [0] 人民万岁 [r'm'wan'su] (score -28.815, matched 9/9, ids: 105839 130766)

# T9（数字键映射）
> 94'26
  [0] 西安 [xi'an] (score -9.913, matched 5/5, ids: 137451)

# 拼音 + T9 混合
> shanxi9426
  [0] 陕西西安 [shan'xi'xi'an] (score -13.427, matched 10/10, ids: 110186)

# T9 中英混输
> 7464486474663
  [0] 苹果iPhone [ping'guo'iPhone] (score -16.218, matched 13/13, ids: 98042 177860)

# 上下文联想（选定首条后自动出预测）
> bairi
  decoded: 百日 [bai'ri] ids: 3505
  context: 3505
  [0] 攻坚 (score -1.463, ids: 43229)
  [1] 行动 (score -1.780, ids: 48871)
```

英文前缀补全 / 联想：

```bash
./build/sime --dict pipeline/output/sime.dict --cnt pipeline/output/sime.cnt --en
```

```
# 前缀补全
> DeepS
  [0] DeepSeek (score -16.185, id: 213931)
  [1] DeepState (score -17.088, id: 237049)

# 联想
> Love
  decoded: Love [Love] ids: 179338
  context: 179338
  [0] You (score -3.037, ids: 177918)
  [1] Live (score -3.526, ids: 179469)
  [2] is (score -3.777, ids: 177655)
  [3] Me (score -3.948, ids: 180313)
```

### 训练 Pipeline

训练流程在 `pipeline/` 目录下。前置准备：`sentences.cut.txt`（切词后语料，空格分隔）+ `units.txt`（拼音表）。

```bash
cd pipeline
make chars       # 1. 统计语料词频
make dict        # 2. 生成词典
make count       # 3. N-gram 统计
make construct   # 4. 构建语言模型
make convert     # 5. 编译拼音 Trie
```

产出：`pipeline/output/sime.dict` 和 `pipeline/output/sime.raw.cnt`。

`pipeline/en/` 与 `pipeline/nine/` 是英文模型与 T9 二元模型的并行管道，结构相同。

---

## License

[Apache-2.0](LICENSE)
