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

function Resolve-InnoCompiler {
    foreach ($candidate in @(
            (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
            'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
            'C:\Program Files\Inno Setup 6\ISCC.exe',
            (Join-Path $repoRoot 'tools\.cache\inno\ISCC.exe')
        )) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    $cacheRoot = Join-Path $repoRoot 'tools\.cache\inno'
    New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
    $installer = Join-Path $cacheRoot 'innosetup-6.3.3.exe'
    $url = 'https://files.jrsoftware.org/is/6/innosetup-6.3.3.exe'
    Write-Host "[package] downloading Inno Setup 6.3.3 (first time only) ..."
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing
    $proc = Start-Process -FilePath $installer -ArgumentList @("/DIR=$cacheRoot", '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        throw "Inno Setup installer exited with code $($proc.ExitCode)"
    }
    $iscc = Join-Path $cacheRoot 'ISCC.exe'
    if (-not (Test-Path $iscc)) {
        throw 'ISCC.exe not found after Inno Setup install'
    }
    return $iscc
}

function Get-PeImportedDllNames {
    param(
        [Parameter(Mandatory = $true)][string]$BinaryPath,
        [Parameter(Mandatory = $true)][string]$ObjdumpPath
    )

    $output = & $ObjdumpPath -p $BinaryPath 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed for $BinaryPath with code $LASTEXITCODE"
    }
    foreach ($line in $output) {
        if ($line -match '^\s*DLL Name:\s*(.+?)\s*$') {
            $Matches[1]
        }
    }
}

function Test-WindowsSystemDll {
    param(
        [Parameter(Mandatory = $true)][string]$DllName
    )

    if ($DllName -match '^(api-ms-|ext-ms-)') {
        return $true
    }
    foreach ($systemDirectory in @(
            (Join-Path $env:SystemRoot 'System32'),
            (Join-Path $env:SystemRoot 'SysWOW64')
        )) {
        if (Test-Path -LiteralPath (Join-Path $systemDirectory $DllName)) {
            return $true
        }
    }
    return $false
}

function Complete-MingwRuntimeClosure {
    param(
        [Parameter(Mandatory = $true)][string]$StageDir,
        [Parameter(Mandatory = $true)][string]$MingwBin
    )

    $objdump = Join-Path $MingwBin 'objdump.exe'
    if (-not (Test-Path -LiteralPath $objdump)) {
        throw "objdump.exe not found at $objdump"
    }

    $stagedByName = @{}
    $queue = New-Object System.Collections.Queue
    foreach ($binary in Get-ChildItem -LiteralPath $StageDir -Recurse -File) {
        if ($binary.Extension -notin @('.exe', '.dll')) {
            continue
        }
        if (-not $stagedByName.ContainsKey($binary.Name)) {
            $stagedByName[$binary.Name] = $binary.FullName
        }
        $queue.Enqueue($binary.FullName)
    }

    $processed = @{}
    $copied = New-Object System.Collections.Generic.List[string]
    while ($queue.Count -gt 0) {
        $binaryPath = [string]$queue.Dequeue()
        if ($processed.ContainsKey($binaryPath)) {
            continue
        }
        $processed[$binaryPath] = $true

        foreach ($importName in Get-PeImportedDllNames -BinaryPath $binaryPath -ObjdumpPath $objdump) {
            if ($stagedByName.ContainsKey($importName)) {
                $dependencyPath = [string]$stagedByName[$importName]
                if (-not $processed.ContainsKey($dependencyPath)) {
                    $queue.Enqueue($dependencyPath)
                }
                continue
            }

            $toolchainDll = Join-Path $MingwBin $importName
            if (-not (Test-Path -LiteralPath $toolchainDll)) {
                if (Test-WindowsSystemDll -DllName $importName) {
                    continue
                }
                $relativeBinary = [System.IO.Path]::GetFileName($binaryPath)
                throw "Dependency $importName imported by $relativeBinary is neither staged, in MINGW64 bin, nor a Windows system DLL"
            }

            $destination = Join-Path $StageDir $importName
            Copy-Item -LiteralPath $toolchainDll -Destination $destination -Force
            $stagedByName[$importName] = $destination
            $copied.Add($importName)
            $queue.Enqueue($destination)
            Write-Host "[package] runtime -> $importName"
        }
    }

    $requiredCompilerRuntimes = @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')
    foreach ($runtimeName in $requiredCompilerRuntimes) {
        if (-not $stagedByName.ContainsKey($runtimeName)) {
            throw "Required MINGW64 runtime was not staged: $runtimeName"
        }
    }

    $remaining = New-Object System.Collections.Generic.List[string]
    foreach ($binary in Get-ChildItem -LiteralPath $StageDir -Recurse -File) {
        if ($binary.Extension -notin @('.exe', '.dll')) {
            continue
        }
        foreach ($importName in Get-PeImportedDllNames -BinaryPath $binary.FullName -ObjdumpPath $objdump) {
            $toolchainDll = Join-Path $MingwBin $importName
            if ((Test-Path -LiteralPath $toolchainDll) -and -not $stagedByName.ContainsKey($importName)) {
                $remaining.Add("$($binary.Name) -> $importName")
            }
        }
    }
    if ($remaining.Count -gt 0) {
        throw "Unresolved MINGW64 runtime dependencies:`n$($remaining -join [Environment]::NewLine)"
    }

    return @($copied | Sort-Object -Unique)
}

function Test-IsolatedPackageLaunch {
    param(
        [Parameter(Mandatory = $true)][string]$StageDir
    )

    $stageExe = Join-Path $StageDir 'ernc-studio.exe'
    $originalPath = $env:Path
    try {
        $env:Path = "$(Join-Path $env:SystemRoot 'System32');$env:SystemRoot"
        $process = Start-Process -FilePath $stageExe -ArgumentList '--exit-after-show' `
            -WorkingDirectory $StageDir -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(15000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'Packaged application did not exit within 15 seconds'
        }
        if ($process.ExitCode -ne 0) {
            throw "Packaged application smoke test exited with code $($process.ExitCode)"
        }
    } finally {
        $env:Path = $originalPath
        $logsPath = Join-Path $StageDir 'logs'
        if (Test-Path -LiteralPath $logsPath) {
            $stagePrefix = [System.IO.Path]::GetFullPath($StageDir).TrimEnd('\') + '\'
            $resolvedLogs = [System.IO.Path]::GetFullPath($logsPath)
            if (-not $resolvedLogs.StartsWith($stagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to remove smoke-test logs outside stage directory: $resolvedLogs"
            }
            Remove-Item -LiteralPath $resolvedLogs -Recurse -Force
        }
    }
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

Qt、Qwt、MINGW64 GCC 与 MSYS2 动态运行库均已包含在本目录中，无需另装开发环境。
"@
Set-Content -LiteralPath (Join-Path $stageDir 'README.txt') -Value $readme -Encoding UTF8

Write-Host "[package] windeployqt"
$windeployqt = Join-Path $mingwBin 'windeployqt.exe'
if (-not (Test-Path $windeployqt)) {
    Write-Error "windeployqt.exe not found"
}
& $windeployqt --compiler-runtime --no-translations --no-system-d3d-compiler `
    --exclude-plugins qglib,qsqlibase,qsqlmysql,qsqlodbc,qsqlpsql `
    (Join-Path $stageDir 'ernc-studio.exe') | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[package] complete MINGW64 runtime dependency closure"
$runtimeDlls = @(Complete-MingwRuntimeClosure -StageDir $stageDir -MingwBin $mingwBin)
Write-Host "[package] staged $($runtimeDlls.Count) additional MINGW64 runtime DLLs"

Write-Host "[package] isolated launch smoke test"
Test-IsolatedPackageLaunch -StageDir $stageDir

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
$zipPath = Join-Path $packageRoot "$stageName.zip"
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Write-Host "[package] zip -> $zipPath"
Compress-Archive -LiteralPath $stageDir -DestinationPath $zipPath -CompressionLevel Optimal

$innoScript = Join-Path $repoRoot 'tools\installer\ernc-studio.iss'
$setupExe = Join-Path $packageRoot "ERNC-Studio-$gitShort-setup.exe"
if (-not (Test-Path $innoScript)) {
    Write-Warning "Missing $innoScript; skipped setup.exe"
} else {
    try {
        $isccPath = Resolve-InnoCompiler
        Write-Host "[package] Inno Setup ($isccPath) -> $packageRoot"
        & $isccPath "/DStageDir=$stageDir" "/DGitShort=$gitShort" "/O$packageRoot" $innoScript | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "ISCC exited with code $LASTEXITCODE"
        }
        if (-not (Test-Path $setupExe)) {
            $built = Get-ChildItem -Path $packageRoot -Filter "ERNC-Studio-$gitShort-setup.exe" | Select-Object -First 1
            if (-not $built) {
                throw 'Setup executable was not produced'
            }
        }
    } catch {
        Write-Warning "Installer build failed: $($_.Exception.Message). ZIP package is still available."
    }
}

Write-Host "[package] done"
Write-Host "  Staged: $stageDir"
Write-Host "  Zip:    $zipPath"
if (Test-Path $setupExe) {
    Write-Host "  Setup:  $setupExe"
}
