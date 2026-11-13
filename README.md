# ERNC Studio

ERNC Studio 是六通道 ERNC/RNC 设备的 Windows 桌面工程工作站。本仓库是新建的 C++20 / Qt 6 Widgets 程序，旧版 Python 工程只作为行为、协议和数据解码参照。

## 基线

- 产品名：ERNC Studio
- 根命名空间：`ernc`
- 工程后缀：`.erncproj`
- 源码参照：`SamhandsomeLee/CHNB`，分支 `CHNB-NEW-6MIC`，提交 `fbecac9f54d378d212794fd937e950d9d2acd58d`
- 不修改旧仓库

## 架构

分层、依赖方向和线程归属见 [docs/architecture/README.md](docs/architecture/README.md)。应用为模块化单体（Presentation / Application / Domain / Infrastructure），UI 不直接持有串口或协议编解码。

## 构建

工具链锁定为 `D:/msys64/mingw64` 的 MSYS2 MINGW64 GCC。配置入口只有 CMake Presets。说明和诊断命令见 [docs/dev/ENVIRONMENT.md](docs/dev/ENVIRONMENT.md)。

```powershell
$env:Path = "D:\msys64\mingw64\bin;" + $env:Path
powershell -NoProfile -File .\tools\doctor-env.ps1

cmake --preset mingw64-comm
cmake --build --preset mingw64-comm

cmake --preset mingw64-dev
cmake --build --preset mingw64-dev
.\build\mingw64-dev\ernc-studio.exe --exit-after-show

cmake --preset mingw64-release
cmake --build --preset mingw64-release
```

`mingw64-comm` 不构建 Qt 界面。`mingw64-dev` 和 `mingw64-release` 构建桌面主程序 `ernc-studio`。MSVC、UCRT64 和 CLANG64 会在配置阶段失败。

## Windows 发行（安装包 / 便携包）

在已配置 MINGW64 的前提下，仓库根目录执行：

```powershell
$env:Path = "D:\msys64\mingw64\bin;" + $env:Path
powershell -NoProfile -File .\tools\package-win.ps1
```

产物位于 `build/mingw64-release/package/`：

| 产物 | 用途 |
|------|------|
| `ERNC-Studio-<git6>-setup.exe` | Inno Setup 安装向导；默认装到 `Program Files\ERNC Studio`，含 exe、Qt 运行时、`catalog/`、`firmware/`、`resources/keymap/` |
| `ERNC-Studio-<git6>-win64.zip` | 便携压缩包，解压即可运行 |
| `ERNC-Studio-<git6>-win64/` | 与 zip 相同的展开目录 |

Release 运行时通过 `resolveBundledResourcePath` 优先读取 exe 同级 bundled 目录；开发构建仍可回退 CMake 源码路径。步骤、验证清单与边界说明见 [docs/dev/PACKAGING.md](docs/dev/PACKAGING.md)。

## 文档

| 文档 | 内容 |
|------|------|
| [docs/architecture/README.md](docs/architecture/README.md) | 分层与运行时边界 |
| [docs/dev/ENVIRONMENT.md](docs/dev/ENVIRONMENT.md) | 工具链与 Preset |
| [docs/dev/PACKAGING.md](docs/dev/PACKAGING.md) | Windows 打包与安装 |
| [AGENTS.md](AGENTS.md) | 协作与 Agent 约定 |

## 分支与提交

- 主分支：`main`
- 主体任务：`feat/es-xxx-short-title`，一个 ES-ID 一个逻辑提交
- 新人测试：`test/qa-xxx-short-title`，不修改 `src/`、`include/ernc/` 和生产 CMake Target
- 未确认的协议、固件偏移和阈值保持阻断，不写隐式默认值
