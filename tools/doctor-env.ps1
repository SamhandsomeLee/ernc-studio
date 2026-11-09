# ERNC Studio environment doctor.
# Exit 0 when the locked MINGW64 toolchain can build and run.
# Exit 1 when the compiler, PATH, Qt ABI, GDB, or a CMake cache is wrong.
# This script does not change PATH, delete caches, or select another compiler.

$ErrorActionPreference = "Continue"
$failures = New-Object System.Collections.Generic.List[string]

function Write-Pass([string]$Message) {
    Write-Host "[PASS] $Message"
}

function Write-Fail([string]$Message) {
    Write-Host "[FAIL] $Message"
    $script:failures.Add($Message) | Out-Null
}

function Normalize-PathText([string]$PathText) {
    if ([string]::IsNullOrWhiteSpace($PathText)) {
        return ""
    }
    return ($PathText -replace "\\", "/").Trim().ToLowerInvariant()
}

function Get-FirstCommand([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        return ""
    }
    return $command.Source
}

function Invoke-Captured([string]$FilePath, [string[]]$ArgumentList) {
    $id = [guid]::NewGuid().ToString("N")
    $stdout = Join-Path $env:TEMP "ernc-doctor-$id.out"
    $stderr = Join-Path $env:TEMP "ernc-doctor-$id.err"
    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $outText = ""
    $errText = ""
    if (Test-Path $stdout) { $outText = (Get-Content -Raw -Path $stdout -ErrorAction SilentlyContinue) }
    if (Test-Path $stderr) { $errText = (Get-Content -Raw -Path $stderr -ErrorAction SilentlyContinue) }
    Remove-Item $stdout, $stderr -ErrorAction SilentlyContinue
    return @{
        ExitCode = $process.ExitCode
        Output   = (($outText + "`n" + $errText).Trim())
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$presetPath = Join-Path $repoRoot "CMakePresets.json"
$environmentDoc = Join-Path $repoRoot "docs\dev\ENVIRONMENT.md"

if (-not (Test-Path $presetPath)) {
    Write-Fail "CMakePresets.json is missing: $presetPath"
    Write-Host "doctor-env: FAIL ($($failures.Count))"
    exit 1
}

$presets = Get-Content -Raw -Path $presetPath -Encoding UTF8 | ConvertFrom-Json
$base = $presets.configurePresets | Where-Object { $_.name -eq "mingw64-base" } | Select-Object -First 1
if ($null -eq $base) {
    Write-Fail "configure preset mingw64-base is missing"
    Write-Host "doctor-env: FAIL ($($failures.Count))"
    exit 1
}

$rootRaw = [string]$base.cacheVariables.ERNC_MINGW64_ROOT
$root = ($rootRaw -replace "/", "\").TrimEnd("\")
$gcc = Join-Path $root "bin\gcc.exe"
$gxx = Join-Path $root "bin\g++.exe"
$ninja = Join-Path $root "bin\ninja.exe"
$gdb = Join-Path $root "bin\gdb.exe"
$qmake = Join-Path $root "bin\qmake6.exe"

Write-Host "ERNC doctor"
Write-Host "repo=$repoRoot"
Write-Host "mingw64_root=$root"

if ((Split-Path $root -Leaf) -ne "mingw64") {
    Write-Fail "Locked root is not an MSYS2 mingw64 prefix: $root"
} else {
    Write-Pass "Locked prefix name is mingw64"
}

foreach ($pair in @(
        @{ Name = "gcc"; Path = $gcc },
        @{ Name = "g++"; Path = $gxx },
        @{ Name = "ninja"; Path = $ninja },
        @{ Name = "gdb"; Path = $gdb },
        @{ Name = "qmake6"; Path = $qmake }
    )) {
    if (Test-Path $pair.Path) {
        Write-Pass "$($pair.Name) exists at $($pair.Path)"
    } else {
        Write-Fail "$($pair.Name) is missing at $($pair.Path)"
    }
}

if (Test-Path "C:\msys64\mingw64\bin\g++.exe") {
    $designGxx = Normalize-PathText "C:\msys64\mingw64\bin\g++.exe"
    $lockedGxx = Normalize-PathText $gxx
    if ($designGxx -ne $lockedGxx) {
        Write-Fail "C:\msys64 also contains g++. The preset is locked to $gxx. Refusing to choose between the two roots."
    }
} else {
    Write-Pass "No second MINGW64 root at C:\msys64"
}

foreach ($name in @("gcc", "g++", "ninja", "gdb")) {
    $first = Get-FirstCommand $name
    $expected = Join-Path $root "bin\$name.exe"
    if ([string]::IsNullOrWhiteSpace($first)) {
        Write-Fail "$name is not on PATH. Put $root\bin first. The preset path is $expected."
        continue
    }
    if ((Normalize-PathText $first) -eq (Normalize-PathText $expected)) {
        Write-Pass "PATH selects locked $name ($first)"
    } else {
        Write-Fail "PATH selects $first before the locked $expected. This is a different compiler, Ninja, or GDB."
    }
}

$cmake = Get-FirstCommand "cmake"
if ([string]::IsNullOrWhiteSpace($cmake)) {
    Write-Fail "cmake is not on PATH"
} else {
    $cmakeRun = Invoke-Captured $cmake @("--version")
    $cmakeLine = ($cmakeRun.Output -split "`r?`n" | Select-Object -First 1)
    if ($cmakeRun.ExitCode -eq 0) {
        Write-Pass "cmake is $cmake ($cmakeLine)"
    } else {
        Write-Fail "cmake at $cmake exited $($cmakeRun.ExitCode)"
    }
}

$savedPath = $env:Path
$env:Path = "$root\bin;" + $env:Path

if ((Test-Path $gxx) -and (Test-Path $gcc)) {
    $machine = Invoke-Captured $gxx @("-dumpmachine")
    $machineText = ($machine.Output -split "`r?`n" | Select-Object -First 1).Trim()
    if ($machine.ExitCode -eq 0 -and $machineText -eq "x86_64-w64-mingw32") {
        Write-Pass "g++ target is $machineText"
    } else {
        Write-Fail "g++ target '$machineText' is not x86_64-w64-mingw32 (exit $($machine.ExitCode))"
    }
    $version = Invoke-Captured $gcc @("--version")
    $versionLine = ($version.Output -split "`r?`n" | Select-Object -First 1).Trim()
    if ($version.ExitCode -eq 0 -and $versionLine.Length -gt 0) {
        Write-Pass "gcc version: $versionLine"
    } else {
        Write-Fail "gcc --version failed with exit $($version.ExitCode)"
    }
}

$qtCore = Join-Path $root "bin\Qt6Core.dll"
$qtConfig = Join-Path $root "lib\cmake\Qt6\Qt6Config.cmake"
$qtWidgets = Join-Path $root "lib\cmake\Qt6Widgets\Qt6WidgetsConfig.cmake"
$qtSerial = Join-Path $root "lib\cmake\Qt6SerialPort\Qt6SerialPortConfig.cmake"
$catchConfig = Join-Path $root "lib\cmake\Catch2\Catch2Config.cmake"
$qwtHeader = Join-Path $root "include\qwt-qt6\qwt.h"
$qwtDll = Join-Path $root "bin\qwt-qt6.dll"
$qwindows = Join-Path $root "share\qt6\plugins\platforms\qwindows.dll"
$qoffscreen = Join-Path $root "share\qt6\plugins\platforms\qoffscreen.dll"

foreach ($required in @(
        $qtCore, $qtConfig, $qtWidgets, $qtSerial, $catchConfig, $qwtHeader, $qwtDll, $qwindows, $qoffscreen
    )) {
    if (Test-Path $required) {
        Write-Pass "SDK file present: $required"
    } else {
        Write-Fail "SDK file missing: $required"
    }
}

if (Test-Path $qtCore) {
    if (-not ("ErncDoctorNative" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class ErncDoctorNative {
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibrary(string path);
}
"@
    }
    $loaded = [ErncDoctorNative]::LoadLibrary($qtCore)
    if ($loaded -eq [IntPtr]::Zero) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Fail "Qt6Core.dll failed to load (Win32 $code). GCC runtime and Qt/ICU are not the same MINGW64 snapshot. Do not fall back to another Qt."
    } else {
        Write-Pass "Qt6Core.dll loads with the locked runtime"
    }
}

if (Test-Path $qmake) {
    $qmakeRun = Invoke-Captured $qmake @("-query", "QT_INSTALL_PREFIX")
    $prefix = ($qmakeRun.Output -split "`r?`n" | Where-Object { $_ -and $_.Trim().Length -gt 0 } | Select-Object -First 1)
    if ($qmakeRun.ExitCode -eq 0 -and (Normalize-PathText $prefix) -eq (Normalize-PathText $root)) {
        Write-Pass "qmake6 prefix is $prefix"
    } else {
        Write-Fail "qmake6 prefix '$prefix' does not match $root (exit $($qmakeRun.ExitCode))"
    }
}

if (Test-Path $gdb) {
    $gdbRun = Invoke-Captured $gdb @("--version")
    $gdbLine = ($gdbRun.Output -split "`r?`n" | Select-Object -First 1).Trim()
    if ($gdbRun.ExitCode -eq 0 -and $gdbLine -match "GNU gdb") {
        Write-Pass "gdb starts: $gdbLine"
    } else {
        Write-Fail "gdb did not start (exit $($gdbRun.ExitCode)). Output: $gdbLine"
    }
}

$env:Path = $savedPath

if (Test-Path $environmentDoc) {
    $doc = Get-Content -Raw -Path $environmentDoc -Encoding UTF8
    if ($doc.Contains($rootRaw) -or $doc.Contains($root)) {
        Write-Pass "ENVIRONMENT.md records the locked root"
    } else {
        Write-Fail "ENVIRONMENT.md does not mention the preset root $rootRaw"
    }
} else {
    Write-Fail "Missing $environmentDoc"
}

$buildRoot = Join-Path $repoRoot "build"
if (Test-Path $buildRoot) {
    $caches = Get-ChildItem -Path $buildRoot -Filter "CMakeCache.txt" -Recurse -ErrorAction SilentlyContinue
    if (-not $caches) {
        Write-Pass "No CMake cache to inspect"
    }
    foreach ($cache in $caches) {
        $text = Get-Content -Raw -Path $cache.FullName
        $cxxMatch = [regex]::Match($text, "CMAKE_CXX_COMPILER:FILEPATH=(.+)")
        $genMatch = [regex]::Match($text, "CMAKE_GENERATOR:INTERNAL=(.+)")
        $makeMatch = [regex]::Match($text, "CMAKE_MAKE_PROGRAM:FILEPATH=(.+)")
        $cxxValue = ""
        $genValue = ""
        $makeValue = ""
        if ($cxxMatch.Success) { $cxxValue = $cxxMatch.Groups[1].Value.Trim() }
        if ($genMatch.Success) { $genValue = $genMatch.Groups[1].Value.Trim() }
        if ($makeMatch.Success) { $makeValue = $makeMatch.Groups[1].Value.Trim() }
        $bad = $false
        if ($genValue -match "Visual Studio|MSBuild") {
            Write-Fail "Stale cache uses generator '$genValue': $($cache.FullName)"
            $bad = $true
        }
        if ($cxxValue -match "cl\.exe|ucrt64|clang64|msvc") {
            Write-Fail "Stale cache compiler is '$cxxValue': $($cache.FullName)"
            $bad = $true
        }
        if ($cxxValue -and (Normalize-PathText $cxxValue) -ne (Normalize-PathText $gxx)) {
            Write-Fail "Stale cache compiler '$cxxValue' is not $gxx. Delete $($cache.DirectoryName) and reconfigure."
            $bad = $true
        }
        if ($makeValue -and (Normalize-PathText $makeValue) -ne (Normalize-PathText $ninja)) {
            Write-Fail "Stale cache ninja '$makeValue' is not $ninja. Delete $($cache.DirectoryName) and reconfigure."
            $bad = $true
        }
        if (-not $bad) {
            Write-Pass "CMake cache matches the locked toolchain: $($cache.FullName)"
        }
    }
} else {
    Write-Pass "Build directory is absent"
}

if ($failures.Count -gt 0) {
    Write-Host "doctor-env: FAIL ($($failures.Count))"
    exit 1
}

Write-Host "doctor-env: PASS"
exit 0
