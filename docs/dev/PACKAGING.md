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
| `ERNC-Studio-<git6>-setup.exe` | 仅当本机已安装 NSIS 且 `makensis` 在 PATH 时生成 |

Release 构建会使用 `resolveBundledResourcePath`：优先加载 exe 同级的 `catalog/`、`firmware/` 等；开发树 Debug 构建仍回退到 CMake 源码绝对路径。

## 手动验证

1. 解压 zip 到空目录，运行 `ernc-studio.exe`。
2. 项目树应能加载参数目录（非“无法加载”）。
3. 曲线分析、区域目录依赖 `firmware/layout_manifest.json`。

## 未包含

- 安装包不写入注册表开机启动；卸载程序仅删除安装目录与桌面快捷方式。
- 不含测试目标与 comm-lab 工具。
