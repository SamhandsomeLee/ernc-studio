# ernc-comm-lab

`ernc-comm-lab` 是独立的通信实验台。它复用生产代码里的会话、调度器、内存从站和串口适配器，不启动 ERNC Studio 主窗口，也不链接 Qt Widgets 或 Qwt。

当前能做两件事：

- 在内存从站上跑 `smoke`、`bulk` 和 `fault-injection`。
- 对真实串口做打开前的档案闸门。端口打不开时结果是 `BLOCKED`。端口能打开时会立刻关闭，不调用 `submit`。

串口路径不扫描本机端口，也不向设备发送 FC03 或 FC16。已批准的协议黄金向量仍然是 0 条，所以串口运行没有“读回与期望一致”的通过结果。

## 构建

工具链是 `D:/msys64/mingw64` 的 MSYS2 MINGW64。三个 Preset 都打开了 `ERNC_BUILD_COMM_LAB`。不带界面的配置用 `mingw64-comm`。

```powershell
$env:Path = "D:\msys64\mingw64\bin;" + $env:Path
cmake --preset mingw64-comm
cmake --build --preset mingw64-comm --target ernc_comm_lab
.\build\mingw64-comm\ernc-comm-lab.exe --help
```

可执行文件名是 `ernc-comm-lab.exe`，输出目录是该 Preset 的构建根目录，例如 `build/mingw64-comm/`。直接双击运行时，进程还需要能找到 MINGW64 的 `bin`，因为串口目标会带上 Qt Core 和 Qt SerialPort。

检查器测试：

```powershell
ctest --preset mingw64-comm -R comm_lab --output-on-failure
```

检查器在 `tests/comm_lab_test.cpp`。它单独启动实验台，所以实验台退出码 3 不会被 CTest 当成检查器失败。

## 目录

| 路径 | 作用 |
|---|---|
| `main.cpp` | 解析命令行，选择模拟器或串口，写一行 JSON |
| `scenarios/scenario.cpp` | `smoke`、`bulk`、`fault-injection` |
| `serial_scenarios/profile.cpp` | 读取串口档案 |
| `serial_scenarios/serial_gate.cpp` | 在 Worker 上打开端口，成功后立即关闭 |

链接关系：

| 库 | 实验台用它做什么 |
|---|---|
| `ernc_device` | `DeviceSession` 和 `CommandScheduler` |
| `ernc_simulator` | 一槽内存从站、故障序列 |
| `ernc_serial` | `SerialTransport`。只在串口档案可用时创建 |

命令行使用 C++ 标准库解析 `argv`。

## 命令

```text
ernc-comm-lab
  --help
  --transport <simulator|serial>
  --scenario <name>
  --profile <path>
  --seed <value>
  --iterations <count>
  --output <path>
  --allow-write
```

带值的选项可以写成 `--seed 305419896` 或 `--seed=305419896`。值不能为空。无法识别的选项、缺少取值，都会退出 2。

| 选项 | 缺省 | 实际行为 |
|---|---|---|
| `--transport` | `simulator` | 只接受 `simulator` 和 `serial` |
| `--scenario` | `smoke` | 模拟器接受 `smoke`、`bulk`、`fault-injection`。`soak` 和 `read-only-smoke` 在模拟器上退出 2 |
| `--profile` | 空 | 模拟器见到任何档案路径就退出 2。串口用它做闸门 |
| `--seed` | `0x12345678`（十进制 `305419896`） | 十进制或 `0x` 前缀。只影响 `fault-injection` 的序列。报告里始终写出这个数 |
| `--iterations` | `1` | 整数 `1` 到 `100`。`smoke` 仍只读一次；`bulk` 和 `fault-injection` 按这个次数重复 |
| `--output` | 标准输出 | 父目录必须已经存在。文件用覆盖方式写出 |
| `--allow-write` | 关闭 | 只改变 JSON 里的 `allowWrite`。串口路径不因此提交写入 |

`--help` 放在无法识别的参数之前时，程序先打印用法并退出 0。

## 退出码

| 码 | 含义 | 报告 |
|---|---|---|
| 0 | 模拟器场景按自己的规则完成 | `status` 为 `PASS` |
| 1 | 模拟器读数或写入结果与字面量不符 | `status` 为 `FAIL` |
| 2 | 参数、模拟器档案、报告写失败，或串口打开后被立即关闭 | 串口打开后又关闭时只向标准错误打印 `serial port was closed without a byte exchange`，不写 JSON |
| 3 | 串口没有可用档案，或 Worker 上的 `open` 失败 | `status` 为 `BLOCKED` |

`fault-injection` 跑完就退出 0。报告里的 `failed` 可以大于 0，这些次数是预期中的故障分类，程序仍写 `PASS`。

## JSON 报告

成功、失败和 `BLOCKED` 都是一行 JSON，末尾有换行。字段顺序固定：

```json
{"transport":"simulator","scenario":"smoke","allowWrite":false,"status":"PASS","reads":1,"writes":0,"data":"78563412","seed":305419896,"passed":1,"failed":0,"none":0,"drop":0,"delay":0,"badFrame":0,"badCrc":0,"disconnect":0}
```

| 字段 | 含义 |
|---|---|
| `transport` | 本次传入的传输名 |
| `scenario` | 本次传入的场景名。串口被阻断时也会原样写出，即使该场景只在模拟器实现 |
| `allowWrite` | 命令行是否带了 `--allow-write` |
| `status` | `PASS`、`FAIL` 或 `BLOCKED` |
| `reads` | 成功读到数据区的次数 |
| `writes` | 内存从站接受写入的次数。串口提交不计入 |
| `data` | 最近一次成功读到的 4 字节，小写十六进制。没有成功读时为空字符串 |
| `seed` | 本次种子 |
| `passed` | 与字面量一致，或故障响应被按规则接受的次数 |
| `failed` | 数据不符，或该故障不能当成成功读的次数 |
| `none` `drop` `delay` `badFrame` `badCrc` `disconnect` | 只在 `fault-injection` 里增加 |

`BLOCKED` 报告的读写次数和故障计数都是 0，`data` 为空，`seed` 仍是本次解析到的种子。

## 模拟器场景

三个场景都使用同一份内置镜像，从站地址 `0x01`，slot 0 的初始 4 字节是 `78 56 34 12`。1 个 slot 等于 4 字节，也就是 2 个寄存器。这些字节写在源码字面量里。

内存传输在调用线程上同步完成。场景不睡眠，不打开 `QSerialPort`。

### smoke

读 slot 0 一次，期望 `78563412`。`reads` 为 1，`writes` 为 0。`--iterations` 只要在 1 到 100 内，重复次数不改变这次读取。

```powershell
.\build\mingw64-comm\ernc-comm-lab.exe --transport simulator --scenario smoke --output build\mingw64-comm\comm-smoke.json
```

### bulk

每一轮都从初始镜像重新开始，做两段独立的会话：

1. 读 slot 0，期望仍是 `78 56 34 12`。
2. 在新的初始镜像上写入 `11 22 33 44`，调度器的 `readBack` 保持关闭；写入被从站接受后，同一会话再读 slot 0，期望变为 `11223344`。

一轮因此增加 2 次读、1 次写。任一字节不符时该轮记入 `failed`，全部轮次都成功才退出 0。最后一次成功读的 `data` 是 `11223344`。

这个写入只发生在内存从站里。命令行没有 `--allow-write` 时，`bulk` 仍会写这块内存。

```powershell
.\build\mingw64-comm\ernc-comm-lab.exe --transport simulator --scenario bulk --iterations 1 --output build\mingw64-comm\comm-bulk.json
```

### fault-injection

对同一条读 slot 0 的请求重复取故障。请求字节是字面量：

```text
08 01 03 00 00 00 02 C4 0B
```

种子使用线性同余序列，同一种子和同样的次数得到同一串故障。种类顺序是 `none`、`drop`、`delay`、`badFrame`、`badCrc`、`disconnect`。

| 种类 | 这一轮怎么计 |
|---|---|
| `none` | 从站正常处理。数据区仍是 `78 56 34 12` 时记入 `reads` 和 `passed` |
| `drop` | 请求不到达从站，镜像不变。记入 `failed` |
| `delay` | 从站已处理，响应晚一拍才交出。晚一拍后的数据区仍是初始 4 字节时记入 `passed` |
| `badFrame` | 交出短坏帧。记入 `failed` |
| `badCrc` | 交出原响应，但最后一字节被翻转。记入 `failed` |
| `disconnect` | 请求不到达从站。记入 `failed`。后面的轮次继续，因为这里直接调用故障函数，不经过会保持关闭的传输对象 |

```powershell
.\build\mingw64-comm\ernc-comm-lab.exe --transport simulator --scenario fault-injection --seed 0x12345678 --iterations 8 --output build\mingw64-comm\comm-fault.json
```

## 串口闸门

`--transport serial` 先于场景名处理。没有档案、档案不可用、或 Worker 上 `open` 失败时，写 `BLOCKED` 并退出 3。`smoke`、`bulk` 或其他场景名都不会在这条路径上执行。

档案可用时，程序创建 `QCoreApplication`，在 `SerialTransport` 的 Worker 线程上调用 `open`。调用线程直接 `open` 会被适配器拒绝，因此实验台不在主线程上打开 `QSerialPort`。

`open` 成功后，同一 Worker 上调用 `close`，然后退出 2。这条路径不调用 `submit`，所以 `--allow-write` 和 `writeSlots` 都不会产生线字节。当前也没有独立的读期望，白名单只决定档案能不能进入打开步骤。

不存在的端口名 `ERNCTEST_NO_SUCH_PORT` 用于自动化。它不是本机枚举结果。

### 档案

档案是一个 JSON 对象，体积不超过 64 KiB，不能带 UTF-8 BOM。程序只接受下面这些键，重复键和未知键都会使档案不可用。字符串不支持反斜杠转义，整数只接受十进制。

| 键 | 要求 |
|---|---|
| `port` | 必填，非空，可打印 ASCII |
| `baudRate` | 必填，大于 0 的整数。程序不填默认波特率 |
| `slave` | 必填，1 到 247。这是地址字节范围，不是已经确认的 6MIC 从站清单 |
| `readSlots` | 必填，1 到 62 个互不相同的非负 slot。空数组不可用 |
| `writeSlots` | 可省略。省略或空数组表示没有可写 slot。有内容时不超过 61 个，且互不相同、非负 |

62 和 61 是一次 FC03、一次 FC16 的现有单包上限。档案只被用来判断能否打开端口，实验台不会按这个列表发送请求。

```json
{"port":"ERNCTEST_NO_SUCH_PORT","baudRate":9600,"slave":1,"readSlots":[0],"writeSlots":[]}
```

示例中的 `9600` 只说明 `baudRate` 要由档案给出一个正整数。把它换成别的正整数同样可以被解析；程序内部没有一份设备波特率。

```powershell
.\build\mingw64-comm\ernc-comm-lab.exe --transport serial --profile .\serial-profile.json --output build\mingw64-comm\comm-serial.json
```

档案文件不存在时退出 3。模拟器命令即使带着同一个 `--profile`，仍然退出 2，并打印 `external profile is not available`。

## 还没有的行为

这些名字出现在设计说明里，当前实验台不会把它们跑成通过：

- `soak`、`--duration`、`--trace`
- 串口上的只读烟雾和按白名单真正收发
- 把打开成功的串口记成 `PASS`
- 外部设备档案格式的冻结文本。上表只描述实验台现在能读的对象
