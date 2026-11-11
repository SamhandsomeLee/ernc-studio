---
name: er-comm-lab
description: "指导 ERNC Studio 纯 C++ 通信协议、调度、模拟器、串口与真机分层测试"
---

# Communication Lab · 协议先于 UI

先按[项目约定](../../../AGENTS.md#通信与-qa)核对协议、固件证据和冻结事项，再读[通信实验台说明](../../../tools/ernc-comm-lab/README.md)及当前实现，确认场景、串口闸门和支持的验证层级。默认采用离线验证。

## 测试分层

- **T0 纯 C++**：CRC、length prefix、Modbus FC03/FC16、typed payload、字节序及边界 Golden。
- **T1 组件**：命令队列、单 in-flight、超时、重试、ACK/Verified、取消和断开清理。
- **T2 模拟器**：完整正常/延迟/CRC 错误/异常/半帧/粘连/丢包端到端。
- **T3 串口测试台**：USB-UART、驱动、线材及物理串口，需负责人批准。
- **T4 真机 HIL**：真实读写与固件布局对照，单独授权和风险审批。
- **T5 长时 Soak**：稳定性、内存/延迟趋势与错误统计，单独审批。

## 合同与工具链

- 先读当前 `CMakePresets.json`，优先使用不带 UI 的 `mingw64-comm`，核对所需目标和场景是否存在。
- 验证命令：`cmake --preset mingw64-comm`、`cmake --build --preset mingw64-comm`、`ctest --preset mingw64-comm --output-on-failure`。**CTest 0 条用例或目录缺失不是测试通过。**
- 黄金向量必须与被测实现独立，包含来源、版本、十六进制、输入和期望。不能以同一编码器生成并验证自家答案。
- 每一用例记录：Case ID、给定状态、输入、预期完整帧/状态、错误判据、来源、实测结果。
- 真实设备的写入、重置/`RNC_refresh`、故障注入和长时测试必须取得明确授权、设备白名单和现场保护，绝不自动触发。

## 输出

先指出当前测试层是否存在 → 给 3～6 个高价值用例和边界 → 负责人的待确认合同 → 可运行命令及结果等级。新人需要增加测试时，建议使用 `$er-qa-coach`。
