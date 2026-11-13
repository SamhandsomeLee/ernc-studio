# Windows 安装包 / 便携包

## 前提

- 已按 [ENVIRONMENT.md](ENVIRONMENT.md) 配置 MSYS2 MINGW64。
- 在仓库根目录执行打包脚本。

## 一键打包

```powershell
$env:Path = "D:\msys64\mingw64\bin;" + $env:Path
powershell -NoProfile -File .\tools\package-win.ps1
```

产物目录：`build/mingw64-release/package/`

| 产物 | 说明 |
|------|------|
| `ERNC-Studio-<git6>-win64/` | 完整便携目录（exe、Qt 运行时、catalog、firmware、keymap） |
| `ERNC-Studio-<git6>-win64.zip` | 上述目录的压缩包 |
| `ERNC-Studio-<git6>-setup.exe` | Inno Setup 安装向导（首次打包会自动下载 Inno 6 到 `tools/.cache/inno`） |

Release 构建会使用 `resolveBundledResourcePath`：优先加载 exe 同级的 `catalog/`、`firmware/` 等；开发树 Debug 构建仍回退到 CMake 源码绝对路径。

## 安装包（setup.exe）

- 默认安装到 `C:\Program Files\ERNC Studio`，包含 exe、Qt 运行时、`catalog/`、`firmware/`、`resources/keymap/`。
- 创建桌面与开始菜单快捷方式；可在「应用和功能」中卸载。
- 需管理员确认（UAC）。

## 手动验证

1. 运行 `ERNC-Studio-*-setup.exe` 完成安装，或解压 zip 到空目录后运行 `ernc-studio.exe`。
2. 项目树应能加载参数目录（非“无法加载”）。
3. 曲线分析、区域目录依赖 `firmware/layout_manifest.json`。

## 未包含

- 安装包不写入注册表开机启动；卸载程序仅删除安装目录与桌面快捷方式。
- 不含测试目标与 comm-lab 工具。

备选：`tools/installer/ernc-studio.nsi` 为 NSIS 脚本（需本机已安装 NSIS），日常以 Inno 产物为准。
