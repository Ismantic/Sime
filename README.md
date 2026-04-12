# Sime 是语输入法

开源中文拼音输入法，语言模型后端 + Linux/Android输入前端。

## 特性
- 语言模型支持完全自训练，仅需提供切词语料与汉字-拼音词典；
- Linux 与 Android 双端支持，全键盘与九宫格两种输入方式支持；

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
./build/sime-interpreter --trie data/sime.trie --cnt data/sime.cnt -s

# 九宫格模式
./build/sime-interpreter --trie data/sime.trie --cnt data/sime.cnt --num -s
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
- `units.txt` — 拼音词典

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
