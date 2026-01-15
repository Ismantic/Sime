是语输入法

# 说明
本项目是一个输入法实验工程，包含词典与语言模型的准备流程，以及构建与测试步骤。

# 使用方式

## 1. 数据准备

安装依赖并准备数据文件：

```bash
sudo pacman -S sunpinyin-data
cp /usr/share/sunpinyin/pydict_sc.bin .
cp /usr/share/sunpinyin/lm_sc.t3g .
```

转换词典：

```bash
g++ trie_conv.cc -o trie_conv
./trie_conv --input pydict_sc.bin --output pydict_sc.ime.bin
```

## 2. 构建与测试

```bash
mkdir build
cd build
cmake ..
make
./ime_interpreter --pydict ../pydict_sc.ime.bin --lm ../lm_sc.t3g
```

## TODO

- 调整词表
- 调整语料
