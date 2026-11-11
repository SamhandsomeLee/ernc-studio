---
description: 只读审查当前 ERNC Studio diff
agent: ernc-reviewer
---

加载 `er-code-review` 技能并执行。

审查下面的 diff：

!`git status --short && git diff --stat && git diff`

额外范围：$ARGUMENTS

只报告问题，不修改文件，不执行 shell。
重点检查 C++/Qt 线程边界、Domain 无 Qt、协议 fail-closed、
测试真实性和是否越过 ES/QA 授权范围。
