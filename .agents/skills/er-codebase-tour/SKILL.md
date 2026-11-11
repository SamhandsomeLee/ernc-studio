---
name: er-codebase-tour
description: "由入口到调用链分步阅读 ERNC Studio 的 C++20、Qt Widgets、CMake 和设备通信代码"
---

# Codebase Tour · 从实际入口读项目

## 目的

训练阅读大型 C++/Qt 工程时从入口、依赖、对象所有权和数据流建立心智模型，而不是逐行翻译。

## 按主题选择起点

- `app/main.cpp` 与 `app/CMakeLists.txt`：桌面启动、参数目录加载、主窗口和 `ernc_app` 的依赖。
- `src/application/device/` 与 `tools/ernc-comm-lab/`：设备会话、调度和独立通信实验台。
- `CMakeLists.txt`：全局 C++20、构建选项和按需添加子目录。
- `CMakePresets.json` / `cmake/ToolchainGate.cmake`：锁定 MINGW64 GCC 与 Ninja。
- `tools/doctor-env.ps1`：开发机环境诊断。
- `docs/architecture/README.md`：模块依赖、线程契约和冻结事项。

根据当前分支的实际文件、CMake 目标和调用方核对各层实现及集成情况。设计文档用于理解目标，调用链以当前源码为准。

## 带读协议

1. 先说本轮要理解的行为，用 3～5 个节点画出浅层流程。
2. 给一个核实过的入口函数：职责、输入、输出、调用方或生命周期。
3. 下一轮按用户选择深入一层，重点分析 C++ 所有权、Qt 线程归属、错误和状态变化。
4. 用现有测试、编译配置或明确的实验说明如何验证；无法运行标明未运行。
5. 需要总览时，汇总全链路、不变量、设计取舍和实际证据。

默认每轮只聚焦一个逻辑单元；用户明确要求完整架构时可一次给全图。**没有证据不推定当前实现。**
