---
name: er-codebase-tour
description: "由入口到调用链分步阅读 ERNC Studio C++20、Qt Widgets、CMake 和未来设备模块"
disable-model-invocation: true
---

# Codebase Tour · 从实际入口读项目

## 目的

训练阅读大型 C++/Qt 工程时从入口、依赖、对象所有权和数据流建立心智模型，而不是逐行翻译。

## 当前起始路径（只表示快照）

- `app/main.cpp`：QApplication、QMainWindow 和 `--exit-after-show` 的最小运行链。
- `app/CMakeLists.txt`：`ernc_app` 及 Qt6::Widgets 关联。
- `CMakeLists.txt`：全局 C++20、构建选项和按需添加子目录。
- `CMakePresets.json` / `cmake/ToolchainGate.cmake`：锁定 MINGW64 GCC 与 Ninja。
- `tools/doctor-env.ps1`：开发机环境诊断。
- `docs/architecture/README.md`：未来模块依赖和线程契约。

读取当前分支后再确认这些路径仍存在。**Domain、Protocol、Device、Region 和分析模块目前只有规划，不得捏造调用链。**

## 带读协议

1. 先说本轮要理解的行为，用 3～5 个节点画出浅层流程。
2. 给一个核实过的入口函数：职责、输入、输出、调用方或生命周期。
3. 下一轮按用户选择深入一层，重点分析 C++ 所有权、Qt 线程归属、错误和状态变化。
4. 用现有测试、编译配置或明确的实验说明如何验证；无法运行标明未运行。
5. 需要总览时，汇总全链路、不变量、设计取舍和实际证据。

默认每轮只聚焦一个逻辑单元；用户明确要求完整架构时可一次给全图。**没有证据不推定当前实现。**
