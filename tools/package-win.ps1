#Requires -Version 5.1
<#
.SYNOPSIS
  Build ERNC Studio Release, stage runtime + data, run windeployqt, produce ZIP (and NSIS setup when available).

.EXAMPLE
  powershell -NoProfile -File tools/package-win.ps1
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$mingwBin = 'D:\msys64\mingw64\bin'
if (-not (Test-Path $mingwBin)) {
    Write-Error "MINGW64 bin not found at $mingwBin"
}

$env:Path = "$mingwBin;$env:Path"

$preset = 'mingw64-release'
$buildDir = Join-Path $repoRoot "build\$preset"

Write-Host "[package] configure $preset"
& cmake --preset $preset | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[package] build ernc_app"
& cmake --build $buildDir --target ernc_app | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $buildDir 'ernc-studio.exe'
if (-not (Test-Path $exe)) {
    Write-Error "Missing $exe"
}

$gitShort = 'unknown'
$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) {
    Push-Location $repoRoot
    $gitShort = (& git rev-parse --short=6 HEAD).Trim()
    Pop-Location
}

$packageRoot = Join-Path $buildDir 'package'
$stageName = "ERNC-Studio-$gitShort-win64"
$stageDir = Join-Path $packageRoot $stageName

if (Test-Path $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

Copy-Item -LiteralPath $exe -Destination $stageDir

$qwtDll = Join-Path $mingwBin 'qwt-qt6.dll'
if (Test-Path $qwtDll) {
    Copy-Item -LiteralPath $qwtDll -Destination $stageDir
} else {
    Write-Warning "qwt-qt6.dll not found; chart features may fail at runtime."
}

$dataPairs = @(
    @{ Src = Join-Path $repoRoot 'catalog'; Dst = 'catalog' },
    @{ Src = Join-Path $repoRoot 'firmware'; Dst = 'firmware' },
    @{ Src = Join-Path $repoRoot 'resources\keymap'; Dst = 'resources\keymap' }
)
foreach ($pair in $dataPairs) {
    if (-not (Test-Path $pair.Src)) {
        Write-Error "Missing data path: $($pair.Src)"
    }
    $target = Join-Path $stageDir $pair.Dst
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath $pair.Src -Destination $target -Recurse -Force
}

$readme = @"
ERNC Studio ($gitShort) — Windows x64 便携目录包

1. 解压到任意路径（路径中避免非 ASCII 更稳妥）。
2. 运行 ernc-studio.exe。
3. 日志目录：<本目录>\logs\

需已安装 Visual C++/MinGW 运行库（windeployqt 已带入 Qt 与 GCC 运行时 DLL）。
"@
Set-Content -LiteralPath (Join-Path $stageDir 'README.txt') -Value $readme -Encoding UTF8

Write-Host "[package] windeployqt"
$windeployqt = Join-Path $mingwBin 'windeployqt.exe'
if (-not (Test-Path $windeployqt)) {
    Write-Error "windeployqt.exe not found"
}
& $windeployqt --compiler-runtime --no-translations --no-system-d3d-compiler `
    (Join-Path $stageDir 'ernc-studio.exe') | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
$zipPath = Join-Path $packageRoot "$stageName.zip"
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Write-Host "[package] zip -> $zipPath"
Compress-Archive -LiteralPath $stageDir -DestinationPath $zipPath -CompressionLevel Optimal

$nsisScript = Join-Path $repoRoot 'tools\installer\ernc-studio.nsi'
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if ($makensis -and (Test-Path $nsisScript)) {
    $setupExe = Join-Path $packageRoot "ERNC-Studio-$gitShort-setup.exe"
    Write-Host "[package] NSIS -> $setupExe"
    & makensis /DStageDir="$stageDir" /DOutFile="$setupExe" $nsisScript | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "NSIS build failed; ZIP package is still available."
    }
} else {
    Write-Host "[package] makensis or tools/installer/ernc-studio.nsi not found; skipped setup.exe"
}

Write-Host "[package] done"
Write-Host "  Staged: $stageDir"
Write-Host "  Zip:    $zipPath"
