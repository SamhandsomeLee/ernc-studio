# ERNC Studio · OpenCode Harness

本文件记录从 `.cursor/` 迁移到 OpenCode 后的项目 harness。事实源是 `AGENTS.md` 和 `.agents/skills/`；`.cursor/` 在过渡期继续保留给 Cursor。

## 结构

```text
AGENTS.md                         始终生效的项目规则
opencode.jsonc                    权限、watcher、默认 agent
tests/AGENTS.md                   读取 tests/ 时懒加载的 QA 边界
.opencode/commands/er-*.md        斜杠命令入口
.opencode/agents/ernc-reviewer.md 只读审查子代理
.agents/skills/                   项目技能
docs/agent/HARNESS.md             本文件
```

## 入口

| 意图 | OpenCode 命令 | 使用的技能 |
|---|---|---|
| 按 ES-ID 开发 | `/er-task-coach ES-079` | `er-task-coach` |
| 新人 QA 任务 | `/er-qa-coach QA-008` | `er-qa-coach` |
| 阅读源码调用链 | `/er-codebase-tour ToolchainGate.cmake` | `er-codebase-tour` |
| 讨论架构与边界 | `/er-architecture-coach` | `er-architecture-coach` |
| 学习 C++/Qt | `/er-cpp-mentor` | `er-cpp-mentor` |
| 诊断构建/运行故障 | `/er-debug-coach` | `er-debug-coach` |
| 只读审查 diff | `/er-code-review` | `er-code-review` + `ernc-reviewer` |
| 通信实验 | `/er-comm-lab` | `er-comm-lab` |

技能也可以直接用 `@er-task-coach` 或 `skill` 工具按 ID 加载。

## 规则位置

| 原 Cursor 规则 | 现在位置 |
|---|---|
| `mentor-mode` | `AGENTS.md` |
| `response-clarity` | `AGENTS.md` |
| `architecture-boundaries` | `AGENTS.md` + `docs/architecture/README.md` |
| `development-scope` | `AGENTS.md` |
| `git-workflow` | `AGENTS.md` + `opencode.jsonc` 的 `permissions` |
| `comments` | `.agents/skills/er-comment-contract/SKILL.md` |
| `protocol-safety` | `.agents/skills/er-protocol-safety/SKILL.md` |
| `qa-boundaries` | `tests/AGENTS.md` |

## 权限

`opencode.jsonc` 里的 `permissions` 是协作护栏，不是安全沙箱：

- 只读 Git 命令、`cmake --preset/build`、`ctest` 自动放行。
- `git add`、`git commit` 每次询问。
- `git push`、`git reset --hard`、`git clean -fd` 直接拒绝。
- `src/`、`include/`、`app/` 的编辑需要确认；`tests/`、`docs/` 可直接编辑。
- 项目外目录访问需要确认。

规则按最后匹配优先。需要临时放宽或收紧时，改 `opencode.jsonc` 后保存即可，OpenCode 会自动重载。

## 过渡期约定

- `.cursor/` 保留，但不作为 OpenCode 的事实源；不要在两处同时修改规则。
- 新增或修改规则先改 `AGENTS.md`、`tests/AGENTS.md` 或对应 Skill。
- 验证完成后，如不再使用 Cursor，可以删除 `.cursor/rules/`、`.cursor/skills/`、`.cursor/README.md` 和 `.cursorignore`。

## 新增技能或命令

新增技能：

1. 建 `.agents/skills/<id>/SKILL.md`。
2. frontmatter 至少写 `name` 和 `description`；ID 由目录名决定。
3. 需要显式调用时，加 `metadata.opencode/autoinvoke: false` 或 `disable-model-invocation: true`。

新增命令：

1. 建 `.opencode/commands/<name>.md`。
2. frontmatter 可写 `description`、`agent`、`model`、`subagent`。
3. 正文用 `$ARGUMENTS` 或 `$1`、`$2` 接收参数。

## 验证

```text
/er-task-coach ES-079
/er-code-review
```

预期：

- 命令能展开并加载对应技能。
- 审查子代理不能编辑文件或执行 shell。
- `git status` 自动放行，`git push` 被拒绝。
- 读取 `tests/CMakeLists.txt` 时 `tests/AGENTS.md` 生效。
- 编辑 C/C++ 时使用 `er-comment-contract`，涉及协议时使用 `er-protocol-safety`。
