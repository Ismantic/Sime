# Fcitx5 Sime 输入法设计文档

## 概览

将 Sime IME 集成到 fcitx5 输入法框架，使其可以在 Linux 桌面环境中使用。

## Fcitx5 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    应用程序 (App)                            │
│  (Firefox, Chrome, Terminal, etc.)                         │
└────────────────────┬────────────────────────────────────────┘
                     │ XIM/Wayland/DBus
┌────────────────────▼────────────────────────────────────────┐
│              Fcitx5 主进程 (Core)                           │
│  • 管理输入法引擎                                            │
│  • 处理输入事件                                              │
│  • 管理候选窗口                                              │
│  • 配置管理                                                  │
└────────────────────┬────────────────────────────────────────┘
                     │ Engine API
┌────────────────────▼────────────────────────────────────────┐
│          Sime Input Method Engine                          │
│  • 实现 InputMethodEngine 接口                              │
│  • 处理按键事件                                              │
│  • 管理拼音输入状态                                          │
│  • 提供候选词列表                                            │
└────────────────────┬────────────────────────────────────────┘
                     │ 调用核心库
┌────────────────────▼────────────────────────────────────────┐
│              Sime Core Library                             │
│  • Interpreter: 拼音解码                                     │
│  • Trie: 词典                                                │
│  • Scorer: 语言模型                                          │
└─────────────────────────────────────────────────────────────┘
```

## 关键组件

### 1. SimeEngine (继承自 InputMethodEngineV2)

**职责**:
- 处理按键输入
- 维护拼音缓冲区
- 调用 Sime 核心库进行解码
- 更新候选词列表
- 提交文本到应用程序

**关键方法**:
```cpp
class SimeEngine : public fcitx::InputMethodEngineV2 {
public:
    void keyEvent(const fcitx::InputMethodEntry& entry,
                  fcitx::KeyEvent& keyEvent) override;

    void reset(const fcitx::InputMethodEntry& entry,
               fcitx::InputContextEvent& event) override;

    void activate(const fcitx::InputMethodEntry& entry,
                  fcitx::InputContextEvent& event) override;

    void deactivate(const fcitx::InputMethodEntry& entry,
                    fcitx::InputContextEvent& event) override;

private:
    void updateCandidates(fcitx::InputContext* ic);
    void commitString(fcitx::InputContext* ic, const std::string& text);
    void clearPreedit(fcitx::InputContext* ic);
};
```

### 2. SimeState (每个输入上下文的状态)

**职责**:
- 保存当前拼音输入
- 缓存候选词
- 记录当前选择

**数据结构**:
```cpp
class SimeState : public fcitx::InputContextProperty {
public:
    std::string pinyinBuffer;      // 当前拼音: "nihao"
    std::vector<Candidate> candidates;  // 候选词列表
    int selectedIndex = 0;         // 当前选中的候选

    void reset();
    void appendPinyin(const std::string& pinyin);
    void deleteLast();
};
```

### 3. 候选词管理

**实现**:
```cpp
class SimeCandidateWord : public fcitx::CandidateWord {
public:
    SimeCandidateWord(const std::string& text, int index);

    void select(fcitx::InputContext* ic) const override;
    // 当候选被选中时调用
};
```

## 文件结构

```
Sime/
├── fcitx5/
│   ├── sime.h                  # SimeEngine 声明
│   ├── sime.cpp                # SimeEngine 实现
│   ├── sime-state.h            # SimeState 声明
│   ├── sime-state.cpp          # SimeState 实现
│   ├── CMakeLists.txt          # fcitx5 构建配置
│   └── data/
│       ├── sime.conf.in        # 输入法配置
│       ├── sime-addon.conf.in  # 插件元数据
│       └── icon/
│           └── sime.png        # 输入法图标
├── CMakeLists.txt              # 主构建文件（添加 fcitx5 子目录）
└── ...
```

## 按键处理流程

```
用户按键 (例如: 'n')
    ↓
SimeEngine::keyEvent()
    ↓
判断按键类型
    ├─ 字母 (a-z) → 添加到拼音缓冲区
    ├─ 数字 (1-9) → 选择对应候选词
    ├─ 空格 → 选择第一个候选词
    ├─ Backspace → 删除最后一个字符
    ├─ Enter → 提交当前拼音
    └─ Escape → 清空输入
    ↓
updateCandidates()
    ↓
调用 Sime Core: Interpreter::DecodeText()
    ↓
获取候选词列表
    ↓
更新 fcitx::InputPanel
    ├─ setPreedit() → 显示拼音
    └─ setCandidateList() → 显示候选词
    ↓
显示在候选窗口
```

## 配置文件

### sime.conf.in
```ini
[InputMethod]
Name=Sime
Icon=sime
Label=是
LangCode=zh_CN
```

### sime-addon.conf.in
```ini
[Addon]
Name=sime
Category=InputMethod
Enabled=True
Library=sime
Type=SharedLibrary
OnDemand=True

[Addon/Dependencies]
0=pinyin
```

## 构建集成

### fcitx5/CMakeLists.txt
```cmake
find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Module REQUIRED)

add_library(sime MODULE
    sime.cpp
    sime-state.cpp
)

target_link_libraries(sime
    Fcitx5::Core
    Fcitx5::Module::Pinyin  # 可选：复用拼音解析
    sime_core  # Sime 核心库
)

install(TARGETS sime DESTINATION ${FCITX5_ADDON_INSTALL_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sime.conf
        DESTINATION ${FCITX5_ADDON_CONFIG_INSTALL_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sime-addon.conf
        DESTINATION ${FCITX5_ADDON_INSTALL_DIR})
```

## 数据文件部署

### 选项 1: 系统路径
```
/usr/share/sime/
├── pydict_sc.ime.bin
└── lm_sc.t3g
```

### 选项 2: 用户配置路径
```
~/.local/share/fcitx5/sime/
├── pydict_sc.ime.bin
└── lm_sc.t3g
```

## 依赖

### 编译时依赖
```bash
# Arch Linux
sudo pacman -S fcitx5 fcitx5-qt fcitx5-gtk extra-cmake-modules

# Ubuntu/Debian
sudo apt install fcitx5 fcitx5-modules-dev extra-cmake-modules libfcitx5core-dev
```

### 运行时依赖
```bash
# 数据文件
sudo pacman -S sunpinyin-data

# 或手动提供
cp pydict_sc.ime.bin /usr/share/sime/
cp lm_sc.t3g /usr/share/sime/
```

## 功能特性

### 基本功能
- [x] 拼音输入
- [x] 候选词显示（默认 5 个）
- [x] 数字键选词 (1-9)
- [x] 空格选择首选词
- [x] Backspace 删除
- [x] Enter 提交拼音
- [x] Escape 清空

### 高级功能
- [ ] 用户词库
- [ ] 云输入
- [ ] 表情符号
- [ ] 快捷短语
- [ ] 全拼/双拼切换

## 性能优化

### 1. 延迟初始化
```cpp
// 首次使用时才加载词典和语言模型
void SimeEngine::activate(...) {
    if (!interpreter_.Ready()) {
        loadResources();
    }
}
```

### 2. 候选词缓存
```cpp
// 相同拼音不重复解码
if (state->pinyinBuffer == state->cachedPinyin) {
    return state->cachedCandidates;
}
```

### 3. 异步解码（可选）
```cpp
// 长输入异步解码，避免阻塞 UI
std::async(std::launch::async, [this, pinyin]() {
    return interpreter_.DecodeText(pinyin, options);
});
```

## 调试

### 启用 fcitx5 日志
```bash
# 设置环境变量
export FCITX_VERBOSE=pinyin=10,sime=10

# 启动 fcitx5
fcitx5 -r
```

### 查看日志
```bash
journalctl --user -f | grep fcitx
```

## 安装和使用

### 编译安装
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_FCITX5=ON \
      ..
make -j$(nproc)
sudo make install
```

### 启用输入法
```bash
# 重启 fcitx5
fcitx5 -r

# 在 fcitx5 配置工具中添加 "Sime"
fcitx5-configtool
```

### 测试
```bash
# 在任意文本编辑器中
# 1. 切换到 Sime 输入法
# 2. 输入 "nihao"
# 3. 应该看到 "你好" 等候选词
```

## 已知限制

1. **初始版本仅支持全拼**
   - 未来可添加双拼支持

2. **候选词数量固定**
   - 默认显示 5 个
   - 可通过配置调整

3. **无用户词库**
   - 当前使用静态词典
   - 未来可添加动态学习

4. **性能依赖数据文件**
   - 大语言模型可能占用内存较多
   - 考虑使用量化或压缩

## 未来改进

1. **双拼支持**
   - 添加双拼方案配置
   - 扩展 UnitParser

2. **云输入**
   - 集成在线 API
   - 提高长句准确率

3. **个性化**
   - 用户词频统计
   - 自动学习

4. **性能优化**
   - 内存映射词典
   - GPU 加速（如果适用）

## 参考资料

- [Fcitx5 开发文档](https://fcitx-im.org/wiki/Development)
- [Fcitx5 API 参考](https://fcitx.github.io/fcitx5/)
- [Fcitx5 输入法插件示例](https://github.com/fcitx/fcitx5-chinese-addons)
