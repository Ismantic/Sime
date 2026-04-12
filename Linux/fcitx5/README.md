# Fcitx5 Sime 插件

将 Sime 拼音引擎集成到 Fcitx5 输入法框架。

## 打包安装 (Arch Linux)

```bash
cd Linux/package
makepkg -si
```

## 手动编译安装

```bash
cd Sime
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSIME_ENABLE_FCITX5=ON
cmake --build build
sudo cmake --install build
fcitx5 -r  # 重启 fcitx5
```

## 编译依赖

- fcitx5 >= 5.0
- extra-cmake-modules

## 使用

1. 在 fcitx5-configtool 中添加 "Sime" 输入法
2. 切换到 Sime，输入拼音，数字键或空格选词


