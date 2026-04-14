# Sime 是语输入法

开源中文拼音输入法，语言模型后端 + Linux/Android输入前端。

## 特性
- 完全自定义训练：提供语言模型训练Pipeline，同时支持中英文语料的训练；
- 多前端与多输入方式：Linux 与 Android 前端实现，全键盘与九宫格两种输入方式支持；
- 混合输入：中文模式下支持输入英文，英文模式下支持补全，以及支持联想；

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
./build/sime --trie data/sime.trie --cnt data/sime.cnt -s

# 九宫格模式
./build/sime --trie data/sime.trie --cnt data/sime.cnt --num -s
```

```
> nihao
  [0] 你好 [ni'hao] (score -18.962)
> zhonghuarenmingongheguo
  [0] 中华人民共和国 [zhong'hua'ren'min'gong'he'guo] (score -24.935)
```

## 训练

训练流程在 `pipeline/` 目录下。

**前置准备**
- `sentences.cut.txt` — 切词后的语料（空格分隔）
- `units.txt` — 拼音词典

**步骤**

```bash
cd pipeline
make chars       # 1. 统计语料词频
make dict        # 2. 生成拼音词典
make count       # 3. N-gram 统计
make construct   # 4. 构建语言模型
make convert     # 5. 编译拼音 Trie
make compact     # 6. 量化压缩
```

产出文件：`pipeline/output/sime.trie` 和 `pipeline/output/sime.cnt` 。


## License

Apache-2.0
