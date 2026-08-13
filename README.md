# 是语输入法引擎

Sime 是一个使用 C++20 编写的本地中文输入法引擎。本仓库包含解码器、词典和
语言模型实现、命令行工具及模型训练流水线。Android、macOS 和 Fcitx5 前端位于
独立的 [SimeApp](https://github.com/Ismantic/SimeApp) 仓库。

## 目标
不用云端功能的前提下，能做到输入体验媲美商业闭源输入法（比如微信/豆包输入法）。

## 功能

- 全拼、简拼、混拼和九宫格输入；
- 中文、英文及中英混合整句解码；
- trigram 语言模型、上下文联想和用户句子学习；
- 繁简转换、表情、符号及词典扩展；
- mmap 模型加载和跨平台 C++ API。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

只把引擎作为其他 CMake 项目的依赖时，可关闭 CLI：

```bash
cmake -S . -B build/core -DSIME_BUILD_TOOLS=OFF
cmake --build build/core
```

## 交互式 CLI

准备 `pipeline/output/sime.dict` 和 `pipeline/output/sime.cnt` 后运行：

```bash
./build/sime \
  --dict pipeline/output/sime.dict \
  --cnt pipeline/output/sime.cnt -s -n 1
```

CLI 支持普通拼音、数字 T9 和英文模式。输入 `94'26` 可测试“西安”的九宫格
解码。

## 训练模型

训练流程位于 `pipeline/`。准备切词语料和拼音词典后：

```bash
cd pipeline
make chars
make dict
make count
make construct
make convert
```

主要产物为 `pipeline/output/sime.dict` 和 `pipeline/output/sime.cnt`。
大规模语料、训练中间文件和生成模型不应提交到 Git。

## 应用集成

公开头文件位于 `include/`，核心 CMake target 为 `sime_core`。应用仓可通过
`add_subdirectory()` 引入本仓库，并设置 `SIME_BUILD_TOOLS=OFF`。平台桥接代码
应保留在 SimeApp，避免 JNI、Swift 或 Fcitx5 依赖进入引擎。

同一个 `Sime` 实例的公开解码入口以及 `ResetCaches()` 会在内部串行化，多个
线程可以安全地共享实例。不同实例之间不会共享这把锁，确实需要并行解码时应为
每个工作线程创建独立实例。配置、用户句子加载/学习以及通过
`MutableUserSentence()` 直接修改状态时，调用方仍应避免与解码并发。

解码结果转为 UTF-8 时可能因损坏的词典数据抛出 `std::runtime_error`。C、Swift、
Rust 等跨语言适配层必须在 C++ 边界内捕获异常并转换为本语言的错误值，不能让
C++ 异常穿越 ABI。

分隔符搜索缓存会自动每 5000 次解码软清理一次。常驻输入法进程也可以在系统发出
内存压力通知时调用 `ResetCaches()` 立即释放缓存；该调用已与解码使用同一把内部
互斥锁，无需调用方另行串行化。

## License

[Apache-2.0](LICENSE)
