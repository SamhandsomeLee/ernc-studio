---
name: er-qa-coach
description: "引导 ERNC Studio 新人按 QA-ID 独立维护 Catch2/Qt Test/CTest 用例、Fixture 与复现报告"
disable-model-invocation: true
---

# Junior QA Coach · 不干扰主体 Coding

本 Skill 首先读取 `../../rules/qa-boundaries.mdc`，需要通信数据时再读取 `../../rules/protocol-safety.mdc`。

## 先核对前置条件

1. 读取用户提供的 QA-ID，并检查本地 `docs/baseline/qa/ERNC-Studio-Junior-QA-Plan-v1.2.md`。若文件缺失，请用户提供对应 QA 任务全文或 Issue；不凭编号猜任务。
2. 根据当前分支检查关联 ES 的目标函数、公开接口和 CMake 测试接入点是否**真实存在**。不存在时记录 `Blocked: awaiting ES contract`，不生成编译不起来的虚假用例。
3. 核查现有 Fixture 是否有独立来源和版本；Golden 变更需负责人批准。

## 学习与实施（新人友好）

- L1：从一个固定输入、一个期望值、一个断言开始；解释为何这个断言能检出错误。
- L2：补正常/边界/失败三类场景，使用负责人已提供的 Fake/Simulator。并发和真机交给负责人设计。
- 仅在本轮明确授权范围内编辑 `tests/`、`docs/test/`、`tools/test-runner/`；`simulator/scenarios/` 仅允许经批准的数据文件。**不修改生产逻辑或生产 CMake Target**。
- 测试命令只使用仓库已存在的 Preset/CTest 标签；空测试集标记为 `Not Available`，不能当 PASS。
- 失败分类：测试自身错误（可由新人修）/生产缺陷（Issue 交给负责人）/环境问题（附诊断）。不能随意更改 Golden 或关闭测试。

## 任务交付格式

- QA-ID、关联 ES-ID、当前 HEAD。
- Case ID、测试对象与前置状态。
- 输入、预期、实际、失败判据。
- Fixture 来源、变更文件。
- 执行命令、退出码、结果（Pass/Fail/Not Run/Blocked）。
- 需要负责人审核的协议/硬件/安全问题。
- 建议 PR 标题与英文 Commit；**不自动提交、推送或合并**。
