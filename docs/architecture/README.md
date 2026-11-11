# ERNC Studio 架构约束

本文件是仓库内的依赖和边界说明。下面记录依赖方向、线程归属和必须保持阻断的事项。

## 产品边界

一个桌面进程，模块化单体。当前不拆服务，不改设备线缆协议，不改嵌入式算法。旧版 `CHNB-NEW-6MIC` @ `fbecac9f54d378d212794fd937e950d9d2acd58d` 只作参照。

## 依赖方向

```text
Presentation  ->  Application  ->  Domain
Application   ->  Ports
Infrastructure 实现 Ports，可使用 Domain 类型
```

允许的依赖只有上面三条。下面这些依赖禁止出现：

- Domain 使用 Qt Widgets、QSerialPort、SQLite 或 Qwt
- 参数 Model 组装 Modbus 报文
- QWidget 持有串口对象
- 协议代码直接更新 QWidget
- 编解码代码直接写数据库

Presentation 只把参数 ID 和用户动作交给 Application。设备当前值（Read）和用户目标值（Set）分开。已发送、设备 ACK、读回 Verified 是三种状态。断线后的值标记为 Stale。

## 线程

| 线程 | 负责 | 不负责 |
|---|---|---|
| UI 主线程 | QWidget、Model/View、轻量状态合并 | 阻塞串口、解码大块数据、长文件 I/O |
| Device Worker | QSerialPort、拆帧、命令调度、超时 | 访问 QWidget |
| Analysis Pool | 大数组解码和绘图前处理 | 更新 Qwt 或 QWidget |
| Storage Worker | SQLite 事务和文件 I/O | 更新 UI |

`QSerialPort` 在 Device Worker 所属线程创建和使用。

## 后续目标

这些 CMake Target 会在第一次真正使用时创建。P0a 不建立它们的源码目录：

| Target | 职责 | 首次使用 |
|---|---|---|
| `ernc_domain` | 参数、规则、布局描述 | Domain 阶段 |
| `ernc_protocol` | 帧、Modbus、CRC、typed codec | 协议阶段 |
| `ernc_device` | 串口、Session、Scheduler | 设备会话阶段 |
| `ernc_application` | 用例和工作流 | 参数服务阶段 |
| `ernc_analysis` | Region 解码和曲线数据 | 分析阶段 |
| `ernc_storage` | SQLite 和文件仓储 | 工程存储阶段 |
| `ernc_ui_models` | Qt Model/View | 参数表阶段 |
| `ernc_ui` | 页面、控件、主题 | 工作台阶段 |
| `ernc_app` | 程序入口 | P0a 已建立最小窗口 |
| `ernc_simulator` | 虚拟设备 | 通信实验阶段 |
| `ernc_comm_lab` | 独立通信验收 CLI | 通信实验阶段 |
| `ernc_tests` | 自动化测试 | 对应接口出现之后 |

`ERNC_BUILD_TESTS` 只在 `tests/CMakeLists.txt` 已经存在时加入该目录。`ERNC_BUILD_COMM_LAB` 只在 `tools/ernc-comm-lab/CMakeLists.txt` 已经存在时加入该目录。

## 工具链

正式构建只用 `CMakePresets.json` 里的 `mingw64-comm`、`mingw64-dev`、`mingw64-release`。编译器、Ninja 和 `CMAKE_PREFIX_PATH` 锁定到同一个 MINGW64 根目录。配置阶段拒绝 MSVC、Visual Studio 生成器、UCRT64、CLANG64，以及目标三元组不是 `x86_64-w64-mingw32` 的编译器。

本机不存在设计草案中的 `C:/msys64`。锁定根目录是 `D:/msys64/mingw64`。换机器时修改 Preset 中的根目录并重新跑环境诊断，不从 PATH 上的其他 GCC 静默回退。

## 保持阻断的事项

在取得固件或黄金报文证据之前，实现必须失败关闭，不能补默认值：

- CAN 36 个名称与 `Can_Msg[72]` 的槽位映射
- `RNC_refresh` 在车速映射确认前的真实写入。2026-11-11 起，ES-035 瞬时命令调度闸门和 ES-095 的 `1→0` 脉冲一并冻结。当前只走普通读和普通写，不新增 `Blocked` 执行器，也不自动重复脉冲。
- 顶层 `Step` 的写入口
- Order 4/5 未确认控制开关
- 未确认的阈值公式
- 没有 `sizeof` / `offsetof` 证据的固件偏移

时间参考换算另行冻结，不因已有 Excel 备注而实现。`running_time`、`max_time` 在解除冻结前只使用固件原值：不显示参考秒数，也不把秒数反算成写入值。

## 提交规则

一个 ES-ID 对应一个功能切片。主体分支使用 `feat/es-xxx-short-title`。测试分支使用 `test/qa-xxx-short-title`。测试提交不修改生产源码和生产 CMake Target。黄金报文不由被测编码器自己生成。
