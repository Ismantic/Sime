# 隐私政策 / Privacy Policy

**生效日期 / Effective date**: 2026-04-25

---

## 中文

### 概述

是语输入法（以下简称"本应用"）是一款离线运行的中文拼音输入法，包含 Android 与 Linux（Fcitx5 插件）两个发行版本。本政策说明本应用在使用过程中如何处理用户数据。

### 我们不收集任何数据

本应用**完全在用户设备本地运行**，不存在以下任何行为：

- 不收集、不记录、不上传用户的任何输入内容（拼音、汉字、英文等）；
- 不收集设备标识、位置、联系人、通话记录、相册等任何隐私信息；
- 不进行任何形式的网络请求；本应用的 Android 包未声明 `INTERNET` 权限，Linux 插件不主动建立网络连接。

### 本机存储

为了实现输入法功能，本应用会在用户设备上保存以下数据，仅用于本地使用，不会离开设备：

- 词库与语言模型文件（随安装包发布的静态资源）；
- 用户在应用内主动添加的"常用语"；
- 用户授权剪贴板功能时，最近若干条剪贴板内容（仅在剪贴板面板内可见，可随时清空）；
- 输入法设置项（候选页大小、繁简切换、声音/振动等）。

以上数据保存在 Android 的 `SharedPreferences` 或 Linux 的 `~/.config` 目录下，删除或卸载本应用时随之清除。

### 隐私字段保护

当用户聚焦的输入框被识别为密码、验证码或其他敏感字段时，本应用会自动：

- 暂停剪贴板监听，避免捕获敏感内容；
- 关闭联想功能，避免泄漏上下文。

### 第三方组件

本应用不接入任何第三方 SDK、广告、分析或崩溃上报服务。

### 联系方式

如对本政策有任何疑问，请发送邮件至 **ismantic@163.com**。

### 政策更新

本政策若有调整，更新版本将与新发布版本一同发布到本仓库。

---

## English

### Overview

Sime IME ("the App") is an offline Chinese pinyin input method, distributed as an Android app and a Linux Fcitx5 plugin. This policy describes how the App handles user data.

### We collect no data

The App runs **entirely on the user's device**. None of the following occur:

- No keystrokes, pinyin, characters, or other input is collected, logged, or uploaded.
- No device identifiers, location, contacts, call logs, photos, or other personal information are accessed.
- No network requests are made. The Android package does not declare the `INTERNET` permission; the Linux plugin opens no network connections.

### Local storage

To deliver IME functionality, the App stores the following on the user's device only — none of it leaves the device:

- Dictionary and language-model files (shipped statically with the install package).
- User-added "quick phrases".
- When clipboard support is enabled by the user, a small history of recent clipboard entries — visible only inside the clipboard panel and can be cleared at any time.
- IME preferences (candidate page size, simplified/traditional switch, sound/vibration, etc.).

This data lives in Android `SharedPreferences` or under the Linux `~/.config` directory, and is removed when the App is uninstalled.

### Sensitive-field protection

When the focused text field is detected to be a password, OTP, or another sensitive input, the App automatically:

- Pauses clipboard monitoring to avoid capturing sensitive content.
- Disables prediction to avoid leaking context.

### Third-party components

The App does not integrate any third-party SDK, advertising, analytics, or crash-reporting service.

### Contact

For questions about this policy, email **ismantic@163.com**.

### Updates

If this policy changes, the updated version will be published in this repository alongside a new release.
