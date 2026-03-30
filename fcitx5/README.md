# Fcitx5 Sime 输入法插件

将 Sime IME 集成到 fcitx5 输入法框架。

## 文件结构

```
fcitx5/
├── sime.h              # SimeEngine 引擎头文件
├── sime.cpp            # SimeEngine 引擎实现
├── sime-state.h        # SimeState 状态管理头文件
├── sime-state.cpp      # SimeState 状态管理实现
├── CMakeLists.txt      # 构建配置
├── data/
│   ├── sime.conf.in    # 输入法配置模板
│   └── sime-addon.conf.in  # 插件元数据模板
└── README.md           # 本文件
```

## 快速开始

### 编译

```bash
cd Sime
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_FCITX5=ON \
      ..
make -j$(nproc)
```

### 安装

```bash
sudo make install
fcitx5 -r  # 重启 fcitx5
```

### 使用

1. 打开 fcitx5-configtool
2. 添加 "Sime" 输入法
3. 切换到 Sime 输入法
4. 输入拼音，例如 "nihao"
5. 选择候选词（数字键或空格）

## 依赖

### 编译时
- fcitx5 >= 5.0
- fcitx5-qt / fcitx5-gtk
- extra-cmake-modules

### 运行时
- 词典: /usr/share/sime/pydict_sc.ime.bin
- 语言模型: /usr/share/sime/lm_sc.t3g

## 配置

### 系统配置
- 输入法: /usr/share/fcitx5/inputmethod/sime.conf
- 插件: /usr/share/fcitx5/addon/sime-addon.conf

### 用户配置
- ~/.config/fcitx5/conf/sime.conf

## 功能特性

- ✅ 拼音输入
- ✅ 智能候选词排序
- ✅ 数字键选词 (1-9)
- ✅ 空格选择首选词
- ✅ Backspace 删除
- ✅ 高性能解码（SIMD + PGO）
- ⏳ 双拼支持（计划中）
- ⏳ 用户词库（计划中）
- ⏳ 云输入（计划中）

## 故障排除

### 找不到输入法

检查插件是否安装：
```bash
ls /usr/lib/fcitx5/sime.so
ls /usr/share/fcitx5/inputmethod/sime.conf
```

### 没有候选词

检查数据文件：
```bash
ls /usr/share/sime/pydict_sc.ime.bin
ls /usr/share/sime/lm_sc.t3g
```

### 查看日志

```bash
export FCITX_VERBOSE=sime=10
fcitx5 -r
journalctl --user -f | grep sime
```

## 详细文档

参见 [FCITX5_GUIDE.md](../FCITX5_GUIDE.md) 获取完整的安装、配置和使用指南。

## 开发

### 构建调试版本

```bash
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DSIME_ENABLE_FCITX5=ON \
      ..
make -j$(nproc)
```

### 热重载

```bash
# 修改代码后
make -j$(nproc)
sudo make install
fcitx5 -r
```

## 许可证

与主项目相同。
