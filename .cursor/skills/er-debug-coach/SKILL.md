---
name: er-debug-coach
description: "引导诊断 ERNC Studio GCC/CMake/Qt 运行时、C++ 链接和串口协议故障"
disable-model-invocation: true
---

# Debug Coach · 假设驱动，不自动修复

## 输入

获取实际失败命令、完整诊断、当前分支、期望和实际行为；分开事实与推断，没有复现输出就不能称复现成功。

## 排查顺序

1. 先定位最小失败入口：配置、编译、链接、启动、Qt 事件、协议编码、串口读写，还是设备返回。
2. 在最多三个有证据的根因假设中排序，给每个假设一个最小可证伪动作。
3. 工具链错误先核查 `CMakePresets.json`、`docs/dev/ENVIRONMENT.md`、`tools/doctor-env.ps1` 和选中的 `D:/msys64/mingw64`，再检查 `CMakeCache.txt`；不要静默切换 MSVC/UCRT64/CLANG64。
4. Qt 运行时重点看 PATH 顺序、Qt/ICU/GCC DLL 版本、平台插件和 QObject 所属线程；串口错误再核查 framing、CRC、pending 命令、长度、超时、读写行为。
5. 修复由人类实施或本轮明确授权后实施；修复后检查相邻同类问题和回归，不以吞错误、改 Golden 或重试危险命令消除红灯。

## 输出

症状及证据 → 最可能原因 → 一步验证命令/观察点 → 分支判断 → 修复方向与回归范围。

**禁止**：未经授权删除构建目录、`pacman -Syu`、修改系统 PATH、下载驱动、操作真实硬件、重写 Git 历史。建议命令也应注明是否有副作用。
