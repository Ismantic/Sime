# Sime 是语输入法

开源中文拼音输入法，纯 C++ 引擎 + Android / Linux 双平台前端。

## 特性

**引擎**
- Modified Kneser-Ney N-gram 语言模型 + Entropy pruning
- 量化压缩（16-bit 概率 / 14-bit backoff / 18-bit token）
- 拼音 Trie 前缀匹配，支持多音字与不完整拼音
- Viterbi beam search 解码，两层输出（Layer 1 全句 + Layer 2 单字）
- T9 九宫格数字到拼音的解码，支持分隔符 `'` 和 tail expansion

**Android**
- 九宫格 / 全键盘 / 数字 / 符号 / 设置 五种键盘，自建 UI 框架（KeyDef → KeyboardLayout → KeyboardContainer → KeyView）
- 三块结构键盘（左 strip + 中网格 + 右列，换行键插到底）
- 候选展开面板：左拼音 strip + 中 4 列汉字网格 + 右控制列（返回 / 上翻 / 下翻 / 删除）
- Preedit 拼音独立行 + 候选汉字行，互不挤压
- 长按退格连删、长按空格切语言、长按字母出数字/符号
- 逗号/句号双态键（中英自动切换）、T9 "1 键" 标点选择器
- 主题系统：浅色/深色自动跟随系统，圆角阴影按键

**Linux**
- Fcitx5 输入法插件

## 构建

```bash
# C++ 引擎 + CLI 工具
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 含 Fcitx5 插件
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSIME_ENABLE_FCITX5=ON
cmake --build build
```

## 使用

```bash
# 全键盘拼音（交互式）
./build/sime-interpreter --trie sime/output/sime.trie --cnt sime/output/sime.cnt -s

# 九宫格模式
./build/sime-interpreter --trie sime/output/sime.trie --cnt sime/output/sime.cnt --num -s
```

```
> nihao
  [0] 你好 [ni'hao] (score -18.962)
> zhonghuarenmingongheguo
  [0] 中华人民共和国 [zhong'hua'ren'min'gong'he'guo] (score -24.935)
```

## 训练

训练流程在 `sime/` 目录下。

**前置准备**
- `sentences.cut.txt` — 切词后的语料（空格分隔）
- `chinese_units.txt` — 拼音词典

**步骤**

```bash
cd sime
make chars       # 1. 统计语料词频
make dict        # 2. 生成拼音词典
make count       # 3. N-gram 统计
make construct   # 4. 构建语言模型
make convert     # 5. 编译拼音 Trie
make compact     # 6. 量化压缩
```

产出文件：`sime/output/sime.trie`（拼音 Trie）和 `sime/output/sime.cnt`（压缩语言模型）。

## 项目结构

```
include/         C++ 引擎头文件
src/             C++ 引擎实现
app/             CLI 工具入口
Android/         Android 输入法应用
  app/src/main/java/com/isma/sime/
    ime/keyboard/framework/   键盘 UI 框架
    ime/keyboard/layouts/     各键盘布局数据
    ime/keyboard/             键盘 View 子类
    ime/candidates/           候选条 + 展开面板
    ime/                      InputKernel / InputState 等
Linux/fcitx5/    Fcitx5 插件
sime/            训练 pipeline + Makefile
data/            预编译模型文件
```

## License

Apache-2.0
