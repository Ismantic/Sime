# Fcitx5 Sime 输入法使用指南

## 快速开始

### 一、安装依赖

#### Arch Linux
```bash
sudo pacman -S fcitx5 fcitx5-qt fcitx5-gtk fcitx5-configtool extra-cmake-modules
sudo pacman -S sunpinyin-data  # 词典和语言模型
```

#### Ubuntu/Debian
```bash
sudo apt install fcitx5 fcitx5-modules-dev fcitx5-config-qt \
                 extra-cmake-modules libfcitx5core-dev libfcitx5config-dev \
                 libfcitx5utils-dev
```

### 二、编译安装

```bash
# 克隆或进入 Sime 项目目录
cd Sime

# 准备数据文件
cp /usr/share/sunpinyin/pydict_sc.bin .
cp /usr/share/sunpinyin/lm_sc.t3g .

# 转换词典格式
g++ trie_conv.cc -o trie_conv
./trie_conv --input pydict_sc.bin --output pydict_sc.ime.bin

# 创建数据目录
sudo mkdir -p /usr/share/sime
sudo cp pydict_sc.ime.bin /usr/share/sime/
sudo cp lm_sc.t3g /usr/share/sime/

# 编译
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_FCITX5=ON \
      -DSIME_ENABLE_SIMD=ON \
      ..
make -j$(nproc)

# 安装
sudo make install

# 重启 fcitx5
fcitx5 -r
```

### 三、配置输入法

#### 方法 1: 图形界面
```bash
# 打开 fcitx5 配置工具
fcitx5-configtool
```

操作步骤：
1. 点击左下角的 "+" 按钮
2. 取消勾选 "Only Show Current Language"
3. 搜索 "Sime" 或浏览列表找到 "Sime"
4. 选中后点击 "Add" 添加
5. 点击 "OK" 保存

#### 方法 2: 命令行
```bash
# 编辑配置文件
vim ~/.config/fcitx5/profile

# 添加以下内容到 [Groups/0/InputMethodList] 部分：
[Groups/0/InputMethodList]
0=keyboard-us
1=sime
```

### 四、使用输入法

1. **切换到 Sime 输入法**
   - 默认快捷键: `Ctrl+Space` 或 `Super+Space`
   - 或点击系统托盘图标切换

2. **基本操作**
   - 输入拼音: 直接按字母键 (a-z)
   - 选择候选词:
     - 数字键 1-9: 选择对应候选词
     - 空格: 选择第一个候选词
   - 删除: Backspace
   - 提交拼音: Enter
   - 清空: Escape

3. **示例**
   ```
   输入: nihao
   候选词: 1.你好 2.你号 3.逆好 ...
   按 1 或 空格 → 输出"你好"
   ```

---

## 高级配置

### 自定义候选词数量

编辑配置文件：
```bash
vim ~/.config/fcitx5/conf/sime.conf
```

修改：
```ini
[Behavior]
# 候选词数量 (默认 5)
NumCandidates=5
```

### 自定义数据文件路径

如果数据文件不在默认路径，可以修改：

```bash
vim ~/.config/fcitx5/conf/sime.conf
```

```ini
[Paths]
# 词典路径
DictPath=/path/to/pydict_sc.ime.bin
# 语言模型路径
LMPath=/path/to/lm_sc.t3g
```

### 快捷键配置

在 fcitx5-configtool 的 "Global Options" 中可以配置：
- 切换输入法
- 翻页
- 候选词选择
- 等等

---

## 性能优化

### 使用 PGO 优化构建

```bash
# 第一步：生成 profile 数据
cd Sime
./scripts/pgo_generate.sh

# 第二步：使用 profile 优化构建
cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_FCITX5=ON \
      -DSIME_ENABLE_SIMD=ON \
      -DSIME_ENABLE_PGO_USE=ON \
      ..
make -j$(nproc)
sudo make install

# 重启 fcitx5
fcitx5 -r
```

### 预加载数据

在 fcitx5 启动时预加载词典和语言模型，减少首次使用延迟。

编辑 `~/.config/fcitx5/profile`:
```ini
[Groups/0]
# 默认输入法设为 Sime
DefaultIM=sime
```

---

## 故障排除

### 问题 1: 找不到 Sime 输入法

**检查安装**:
```bash
# 检查插件是否安装
ls /usr/lib/fcitx5/sime.so
# 或
ls /usr/local/lib/fcitx5/sime.so

# 检查配置文件
ls /usr/share/fcitx5/inputmethod/sime.conf
ls /usr/share/fcitx5/addon/sime-addon.conf
```

**解决方案**:
```bash
# 确认安装路径
sudo make install

# 检查 fcitx5 日志
fcitx5 -r -d
journalctl --user -f | grep fcitx
```

### 问题 2: 没有候选词显示

**检查数据文件**:
```bash
# 确认数据文件存在
ls -lh /usr/share/sime/pydict_sc.ime.bin
ls -lh /usr/share/sime/lm_sc.t3g
```

**查看日志**:
```bash
# 启用详细日志
export FCITX_VERBOSE=sime=10
fcitx5 -r

# 查看错误信息
journalctl --user -f | grep -i sime
```

**解决方案**:
- 确保数据文件已转换为 .ime.bin 格式
- 检查文件权限 (应该可读)
- 验证配置文件中的路径

### 问题 3: fcitx5 崩溃

**获取调试信息**:
```bash
# 使用 gdb 运行
gdb fcitx5
(gdb) run
# ... 触发崩溃 ...
(gdb) bt  # 查看堆栈
```

**常见原因**:
- Fcitx5 版本不兼容 (需要 >= 5.0)
- 缺少依赖库
- 数据文件损坏

**解决方案**:
```bash
# 更新 fcitx5
sudo pacman -Syu fcitx5

# 重新构建插件
cd build
make clean
cmake .. && make -j$(nproc)
sudo make install
```

### 问题 4: 输入延迟

**性能诊断**:
```bash
# 检查是否使用了优化构建
file /usr/lib/fcitx5/sime.so | grep stripped

# 检查 CPU 使用率
top -p $(pgrep fcitx5)
```

**优化建议**:
1. 使用 Release 构建（已启用 -O3）
2. 启用 SIMD 优化
3. 使用 PGO 优化
4. 减少候选词数量
5. 使用 SSD 存储数据文件

---

## 卸载

```bash
# 删除插件
sudo rm /usr/lib/fcitx5/sime.so
sudo rm /usr/local/lib/fcitx5/sime.so

# 删除配置文件
sudo rm /usr/share/fcitx5/inputmethod/sime.conf
sudo rm /usr/share/fcitx5/addon/sime-addon.conf

# 删除数据文件（可选）
sudo rm -r /usr/share/sime

# 删除用户配置（可选）
rm ~/.config/fcitx5/conf/sime.conf

# 重启 fcitx5
fcitx5 -r
```

---

## 开发和调试

### 启用调试日志

```bash
# 设置环境变量
export FCITX_VERBOSE=sime=10,pinyin=5

# 启动 fcitx5
fcitx5 -r -d

# 查看日志
journalctl --user -f | grep -E 'fcitx|sime'
```

### 热重载插件

修改代码后：
```bash
# 重新编译
cd build && make -j$(nproc)

# 安装
sudo make install

# 重启 fcitx5
fcitx5 -r
```

### 内存和性能分析

```bash
# Valgrind 内存检查
valgrind --leak-check=full fcitx5

# Perf 性能分析
perf record -g fcitx5
perf report
```

---

## 贡献和反馈

### 报告问题

请提供以下信息：
1. 操作系统和版本
2. Fcitx5 版本: `fcitx5 --version`
3. Sime 版本/commit hash
4. 详细的错误日志
5. 复现步骤

### 功能建议

欢迎在 GitHub Issues 中提出建议：
- 双拼支持
- 云输入
- 用户词库
- 表情符号
- 快捷短语
- 其他功能

---

## 常见问题 (FAQ)

### Q: Sime 和其他中文输入法有什么区别？

A: Sime 的特点：
- 基于 Sunpinyin 词典和语言模型
- 高度优化的解码算法（SIMD + PGO）
- 轻量级，内存占用小
- 开源，可定制

### Q: 支持哪些拼音方案？

A: 当前仅支持全拼。双拼支持在开发计划中。

### Q: 可以导入其他输入法的词库吗？

A: 当前不支持。未来版本计划支持用户词库和自定义词典。

### Q: 是否支持云输入？

A: 当前不支持。这是一个计划中的功能。

### Q: 性能如何？

A: 通过 SIMD 和 PGO 优化，Sime 的解码速度非常快。在现代 CPU 上，延迟小于 1ms。

### Q: 支持哪些平台？

A: 当前支持 Linux + fcitx5。未来可能支持：
- Windows (通过 TSF)
- macOS (通过 InputMethodKit)
- Android

---

## 参考链接

- [Fcitx5 官方文档](https://fcitx-im.org/wiki/)
- [Fcitx5 配置指南](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland)
- [Sime 项目主页](https://github.com/your-repo/Sime)
- [问题反馈](https://github.com/your-repo/Sime/issues)
