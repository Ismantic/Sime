# SimeIME - macOS Input Method

macOS 下的中文拼音输入法，基于 Sime 引擎实现。

## 功能特性

- 拼音到汉字的智能转换
- 基于统计语言模型的候选词排序
- 支持数字键（1-9）快速选择候选词
- 方向键选择候选词
- 简洁的候选窗口界面
- 与 macOS Input Method Kit 深度集成

## 系统要求

- macOS 10.15 (Catalina) 或更高版本
- x86_64 或 Apple Silicon (ARM64)

## 依赖项

- Sunpinyin 数据文件：
  - `pydict_sc.ime.bin` - 拼音字典（转换后的格式）
  - `lm_sc.t3g` - 语言模型文件

## 构建

### 使用 CMake

```bash
cd ~/workspace/Sime/macos
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

### 使用 Xcode

```bash
cd ~/workspace/Sime/macos
mkdir -p build && cd build
cmake .. -G Xcode
open SimeIME.xcodeproj
```

然后在 Xcode 中构建项目。

## 安装

### 自动安装

```bash
# 构建完成后安装到 ~/Library/Input Methods
cd ~/workspace/Sime/macos/build
make install
```

### 手动安装

1. 复制 `SimeIME.app` 到 `~/Library/Input Methods/`
2. 复制数据文件到合适位置：
   - `pydict_sc.ime.bin`
   - `lm_sc.t3g`

```bash
cp -r SimeIME.app ~/Library/Input\ Methods/

# 复制数据文件（从 sunpinyin-data 获取）
cp pydict_sc.ime.bin ~/Library/Input\ Methods/SimeIME.app/Contents/Resources/
cp lm_sc.t3g ~/Library/Input\ Methods/SimeIME.app/Contents/Resources/
```

### 获取 Sunpinyin 数据文件

如果你有 sunpinyin-data 安装：

```bash
# macOS (通过 Homebrew)
brew install sunpinyin

# 或者从 Linux 系统复制
scp user@linux-host:/usr/share/sunpinyin/pydict_sc.bin .
scp user@linux-host:/usr/share/sunpinyin/lm_sc.t3g .

# 转换字典格式
cd ~/workspace/Sime
g++ trie_conv.cc -o trie_conv
./trie_conv --input pydict_sc.bin --output pydict_sc.ime.bin
```

## 启用输入法

1. 安装完成后，注销并重新登录（或重启）
2. 打开 "系统设置" -> "键盘" -> "输入法"
3. 点击 "+" 添加输入法
4. 找到 "简体中文" -> "SimeIME"
5. 添加后可以通过菜单栏或快捷键切换

或者使用命令行启用：

```bash
# 注册输入法
sudo touch /Library/Input\ Methods/SimeIME.app

# 刷新输入法列表
killall InputMethodKit 2>/dev/null; true
```

## 使用方法

### 基本输入

1. 切换到 SimeIME 输入法
2. 输入拼音，如 `zhongwen`
3. 候选窗口会显示对应的汉字选项
4. 使用数字键 `1-9` 选择候选词
5. 或使用方向键 `↑↓` 选择，`Enter` 确认

### 快捷键

| 按键 | 功能 |
|------|------|
| `1-9` | 直接选择对应候选词 |
| `↑` / `↓` | 选择上一个/下一个候选词 |
| `←` / `→` | 同上下 |
| `Space` | 选择当前候选词 |
| `Enter` / `Return` | 确认输入 |
| `Esc` | 取消输入 |
| `Backspace` / `Delete` | 删除拼音字符 |
| `Tab` | 切换到下一个候选词 |

## 项目结构

```
macos/
├── CMakeLists.txt              # CMake 构建配置
├── README.md                   # 本文档
└── SimeIME/
    └── SimeIME/
        ├── main.m              # 入口点
        ├── Info.plist          # 输入法配置
        ├── SimeEngine.h/.mm    # C++ 引擎封装
        ├── SimeInputController.h/.mm  # 输入法控制器
        └── SimeCandidatesWindow.h/.mm # 候选窗口
```

## 架构说明

### 核心组件

1. **SimeEngine** - 封装 C++ Sime 引擎
   - 加载字典和语言模型
   - 将拼音转换为候选词列表
   - 单例模式管理引擎实例

2. **SimeInputController** - 输入法控制器
   - 继承自 `IMKInputController`
   - 处理键盘输入事件
   - 管理输入状态
   - 协调引擎和 UI

3. **SimeCandidatesWindow** - 候选窗口
   - 继承自 `NSPanel`
   - 显示候选词列表
   - 高亮当前选择
   - 跟随光标位置

### 输入处理流程

```
键盘输入 → IMKInputController → SimeEngine → 候选词
                                    ↓
                              字典查询
                              语言模型评分
                              候选排序
                                    ↓
                            SimeCandidatesWindow
```

## 故障排除

### 输入法不显示

1. 确认 `SimeIME.app` 在 `~/Library/Input Methods/` 中
2. 注销并重新登录
3. 检查系统设置中是否已添加

### 引擎加载失败

1. 检查数据文件是否存在：
   ```bash
   ls -la ~/Library/Input\ Methods/SimeIME.app/Contents/Resources/
   ```
2. 确认文件路径在 `SimeEngine.mm` 的搜索路径中
3. 查看 Console.app 中的日志输出

### 候选窗口不显示

1. 检查输入法是否已激活（activateServer 被调用）
2. 确认窗口层级设置正确
3. 查看光标位置计算是否正确

## 开发计划

- [ ] 词组联想
- [ ] 用户词库
- [ ] 个性化学习
- [ ] 双拼支持
- [ ] 皮肤主题
- [ ] 设置面板

## 许可证

与 Sime 项目相同。

## 参考

- [InputMethodKit Documentation](https://developer.apple.com/documentation/inputmethodkit)
- [Sime Project](../CLAUDE.md)
- [Sunpinyin Project](https://github.com/sunpinyin/sunpinyin)
