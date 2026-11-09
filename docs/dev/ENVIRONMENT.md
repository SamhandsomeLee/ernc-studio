# 开发环境

P0a 的构建和调试只走 MSYS2 MINGW64。诊断脚本是 `tools/doctor-env.ps1`。它发现错误环境、过期 CMake 缓存或缺失 SDK 时以退出码 1 结束，并打印 `[FAIL]`。它不会改写 PATH，也不会改用 MSVC。

## 锁定位置

| 项 | 锁定值 |
|---|---|
| MINGW64 根目录 | `D:/msys64/mingw64` |
| C 编译器 | `D:/msys64/mingw64/bin/gcc.exe` |
| C++ 编译器 | `D:/msys64/mingw64/bin/g++.exe` |
| Ninja | `D:/msys64/mingw64/bin/ninja.exe` |
| GDB | `D:/msys64/mingw64/bin/gdb.exe` |
| Qt 前缀 | `D:/msys64/mingw64` |
| CMake Preset | `mingw64-dev`（日常），`mingw64-comm`（无界面），`mingw64-release` |
| 调试配置 | `.vscode/launch.json` 的 `ERNC Studio (MINGW64 GDB)` |

设计草案写过 `C:/msys64/mingw64`。这台机器上该目录不存在，MSYS2 安装在 `D:\msys64`。根目录以 `CMakePresets.json` 的 `ERNC_MINGW64_ROOT` 为准。

2026-11-09 为了让已安装的 Qt 6.12 / ICU 78 能够加载，MINGW64 GCC 运行库从 15.2.0-8 对齐到仓库中的 16.2.0-6。后续 `pacman -Syu` 之后要重新运行诊断，并把下表改成实际版本。

| 组件 | 记录版本 |
|---|---|
| GCC | 16.2.0，MSYS2 Rev6 |
| 目标三元组 | `x86_64-w64-mingw32` |
| CMake | 登录 PATH 上是 4.1.2（`E:\CMake\bin\cmake.exe`）。把 MINGW64 `bin` 放在最前时会选到 4.4.5。两套都能配置，编译器路径由 Preset 锁定 |
| Ninja | 1.13.2（Preset 固定使用 MINGW64 的 ninja） |
| Qt | 6.12.0，`mingw-w64-x86_64-qt6-base` |
| Qwt | 6.3.0，头文件 `include/qwt-qt6/qwt.h`，库 `bin/qwt-qt6.dll` |
| Catch2 | 3.16.1 |
| GDB | 18.1 |

## 日常命令

```powershell
$env:Path = "D:\msys64\mingw64\bin;" + $env:Path
powershell -NoProfile -File .\tools\doctor-env.ps1

cmake --preset mingw64-dev
cmake --build --preset mingw64-dev
.\build\mingw64-dev\ernc-studio.exe --exit-after-show
```

`--exit-after-show` 只用于证明窗口进程能启动并正常返回。日常使用不要加这个参数。

运行前把 `D:\msys64\mingw64\bin` 放在 PATH 最前面。否则可能加载到其他目录的 `libstdc++-6.dll`、Python 自带的 `ninja.exe`，或 `D:\Qt` 里另一套 Qt DLL。

## 诊断失败时怎么处理

- 第一条 `gcc.exe` / `g++.exe` / `ninja.exe` 不在锁定根目录：调整 PATH。不要让 CMake 自己挑选编译器。
- `C:\msys64`、`ucrt64`、`clang64` 或 ARM GCC 出现在编译器路径里：这是另一个环境，停止配置。
- `Qt6Core.dll` 加载返回 Win32 127：GCC 运行库和 Qt/ICU 不是同一套 MSYS2 快照。在 MINGW64 里重装匹配的 `mingw-w64-x86_64-gcc-libs`，然后删除对应的 `build/<preset>` 目录。
- `build/<preset>/CMakeCache.txt` 里的编译器或生成器与 Preset 不一致：删除该构建目录后重新 `cmake --preset`。不要继续用旧缓存。
- GDB 或 `qmake6` 无法启动：同样先确认 PATH 和 GCC 运行库，再看诊断输出里的退出码。

## 调试

Cursor / VS Code 的 CMake 预设选择 `mingw64-dev`。调试配置使用 `D:/msys64/mingw64/bin/gdb.exe`，并把 MINGW64 的 `bin` 放在该调试会话 PATH 的最前面。不使用 Visual Studio 调试器。
