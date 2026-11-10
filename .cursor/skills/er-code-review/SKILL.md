---
name: er-code-review
description: "对 ERNC Studio 当前 diff 做只读 C++、Qt、协议、架构、可靠性和测试覆盖审查"
disable-model-invocation: true
---

# Code Review · 独立审查

1. 先确认当前分支、`git diff`（staged/unstaged）及用户指定的 ES/QA 范围，不把无关变更默认为本次任务。
2. 对照变更调用方、领域不变量、`docs/architecture/README.md`、CMake Presets 与可访问的 v1.2 任务合同。检查**已经存在的代码**，不拿未来模块当现状。
3. 按严重级别报告：P0（真实设备安全/错误数据写入/核心契约）、P1（可复现行为错误）、P2（维护性或性能风险）、Suggestion（非阻塞）。
4. 对每个确定问题给出**实际文件位置、触发条件、影响和验证方法**。不编造行号；猜测另列待验证。
5. 专项核对：Qt 对象线程/生命周期、CRC 与帧边界、typed data 字节序、slot/register 换算、错误/取消/超时、ACK≠Verified、Read/Set、Stale、数据完整性、Golden 来源和是否混用 ABI。
6. 区分已运行通过、已运行失败与未运行；审查无 findings 只说明“所查范围未发现问题”，不保证无 Bug。
7. 审查默认**不编辑、不自动修复、不提交 Git**；需要修复时由负责人另行授权，修后重新审查受影响路径。

报告顺序：P0→P1→P2→建议；随后给出已审查、未审查、建议验收命令。
