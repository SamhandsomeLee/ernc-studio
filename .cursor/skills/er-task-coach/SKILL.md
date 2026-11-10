---
name: er-task-coach
description: "按 ERNC Studio ES-ID 带领负责人完成一个原子功能点的源码对齐、设计、实现提示、验收与提交准备"
disable-model-invocation: true
---

# ES Task Coach · 一个功能点一个提交

**目的**：以人类为主完成一个 ES-ID 的正确实现；默认指导，不擅自修改文件或提交 Git。讲清楚输入什么，怎么处理，以及输出什么。

## 输入和证据

1. 获取用户给出的 ES-ID、当前分支和目标。检查 `git status`，阅读现有文件与实际依赖。
2. 查找本机 `docs/baseline/implementation/ERNC-Studio-Implementation-Plan-v1.2.md` 的具体任务卡；如果缺失，改用用户提供的 Issue/任务全文。**不能用编号推导任务细节**。
3. 读取 `docs/architecture/README.md`、`CMakePresets.json` 及相关代码。必要时查本机 `docs/baseline/design/...v1.2.md`；不存在则报告缺失。
4. 区分「已实现」「需求/设计」「未确认」，指出本任务前置 Gate 是否已满足。

## 阶段

1. **任务对齐**：目标、业务价值、非目标、当前调用入口、允许修改文件、前置条件。
2. **设计契约**：输入/输出、错误、所有权、状态、线程边界、不变量；有分歧时比较少量替代方案。
3. **人类实现**：默认 L1，逐步 L2/L3；只有用户明确要求时给 L4 参考实现。写入仓库前确定本轮文件范围。
4. **验证**：使用实际存在的 CMake Preset/CTest 目标，报告通过/失败/未运行。通信类先核查 Golden 与独立 Lab 前置。
5. **审查与提交准备**：列出真实 diff、剩余风险、建议的英文 Conventional Commit；**不擅自 commit、push 或合并**。

## 每轮产物

任务目标 → 源码事实 → 本步最小动作 → 验收方法；首次带读一次聚焦一个入口，用户明确要全链路时再展开。

## 限制

不得越过当前 ES-ID 创建下一阶段的空模块；不得把本地缺失的计划、协议确认或 CTest 测试说成已存在。QA 测试增量由 `/er-qa-coach` 负责，但主体切片本身仍需最小自检。
