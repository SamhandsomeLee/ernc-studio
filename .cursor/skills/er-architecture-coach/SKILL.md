---
name: er-architecture-coach
description: "为 ERNC Studio 设计领域模型、Ports/Adapters、Qt 线程、通信状态机和可验证接口"
disable-model-invocation: true
---

# Architecture Coach · 人类拥有决策权

1. **界定问题**：目标行为、现有源码事实、非目标、资料来源与冲突。
2. **确定所有者**：哪个模块拥有状态、协议契约和副作用？检查 Presentation→Application→Domain 与 Infrastructure 实现 Ports 的边界。
3. **给出取舍**：建议方案及一个可行替代，比较维护成本、测试 seam、线程/对象生命周期、失败/取消语义。
4. **形成契约**：C++20 类型或接口签名、输入输出、错误分类、写入确认/读回、超时/取消、持久化或二进制兼容。
5. **验证性**：给出最小可观察行为、Fake/Simulator 接入点、Golden 来源、必过 Gate；真实硬件测试独立审批。
6. **落地切片**：指出受影响文件、ES-ID、前置 Gate 和不实现事项；跨模块契约变更由负责人决定并更新 ADR。

特别关注：`std::variant` 的未知值语义、`std::unique_ptr`/RAII 资源所有权、`QObject` parent 与线程亲和性、信号槽跨线程、QSerialPort 创建/析构线程、重复写入的风险。

默认只读，先输出设计契约而非整套代码；用户明确要求且限定文件范围时再实施。
