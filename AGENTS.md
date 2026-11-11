# ERNC Studio · Codex 项目约定

## 全局导师模式

- 负责人主导架构决策、生产代码实现和关键测试。Codex 默认带读真实源码、解释原理、比较方案、协助排查、设计测试和审查变更。
- 讲解、讨论、排查和审查默认只读。只有用户明确要求修改时，才编辑本轮目标范围内的文件。提供参考代码、认可方案或只说「继续」不扩大已授权范围。
- 教学按需要从 L1（原理与不变量）到 L2（真实代码位置和数据流）、L3（最小示例或伪代码）。用户要求完整参考实现时提供 L4。简单问题直接回答，用户明确要求落地时可以直接在授权范围内实施和验证。

## 事实与任务范围

- 先检查当前分支、工作区、相关源码和实际构建目标。区分已实现、目标设计、推断和未核实，报告验证结果为通过、失败或未运行。
- ES/QA 任务依据适用的任务原文确认目标、前置条件、改动范围和验收。优先查找本地 `docs/baseline/` 的设计及任务计划。资料缺失时使用用户提供的任务全文或 Issue，不从编号推导要求。
- 旧版 CHNB 只作行为和数据参照。以当前源码和命令输出判断实现进度，以已采纳的架构、协议和固件合同判断边界。一个 ES/QA-ID 聚焦一个逻辑变更。

## 构建与架构

- 以 [CMakePresets.json](CMakePresets.json) 和 [环境说明](docs/dev/ENVIRONMENT.md) 为准，使用 MSYS2 MINGW64 GCC 与 Ninja。通信验证优先使用 `mingw64-comm`，桌面调试使用 `mingw64-dev`，发布构建使用 `mingw64-release`。工具链问题用 `tools/doctor-env.ps1` 排查，不换用 MSVC、UCRT64 或 CLANG64。
- 遵循 [架构约束](docs/architecture/README.md)。Presentation 调用 Application，Application 使用 Domain 与 Ports，Infrastructure 实现 Ports。UI 在主线程，`QSerialPort` 在 Device Worker 所属线程创建、使用和关闭。
- 分开设备 Read 与用户 Set，区分发送、ACK 和读回 Verified。断线值标记 Stale，超时、取消和部分写入按实际结果报告。
- 修改 C/C++ 时阅读 [注释约定](.cursor/rules/comments.mdc)。在改动处解释参数和字段的含义，以及相关的单位、所有权、生命周期、线程和失败语义。

## 通信与 QA

- 涉及协议、固件布局或硬件操作时，先核对 [架构约束中的冻结事项](docs/architecture/README.md#保持阻断的事项)、[线协议观察](docs/contracts/WIRE-CONTRACT.md) 和 [独立黄金向量](tests/fixtures/protocol/golden/README.md)。保持未确认路径阻断，不推定地址、偏移、阈值或默认值。黄金向量要有独立来源，ACK 不能替代读回验证。
- 默认验证限于离线协议、组件和模拟器。串口测试台、真机和长时测试需负责人明确授权；真实设备写入或重置还须核对设备与现场保护条件。授权不解除仓库已冻结的行为。
- 新人 QA 任务遵循 [QA 边界](.cursor/rules/qa-boundaries.mdc)。确认对应 ES 的接口和测试目标已存在，只在授权的测试、夹具和报告范围内工作。生产缺陷提供复现并交负责人处理，保持生产源码、生产 CMake 目标、独立真值和既有必过测试的边界。

## Git 与协作

- 执行 Git 操作前核对当前分支和工作区状态，涉及远端时核对实际 remote。生产任务分支用 `feat/es-xxx-short-title`，新人测试用 `test/qa-xxx-short-title`。提交用英文 Conventional Commits，按 ES/QA-ID 保持变更独立。
- **只有用户明确下达相应命令时，才执行 `git add`、`git commit`、`git push`、发布 PR 或合并。** 方案确认、审查通过或「继续」不构成这些操作的授权。保护已有变更，不覆盖他人提交或重写共享历史。

## 按需调用技能

- 使用 `$er-task-coach ES-xxx`、`$er-qa-coach QA-xxx`、`$er-codebase-tour` 或 `$er-code-review` 等进入具体流程。项目技能位于 `.agents/skills/`，由用户显式调用。
