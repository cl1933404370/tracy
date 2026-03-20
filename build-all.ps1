# Tracy build-all.ps1
# Builds TracyClient + tracy-capture (TracyServer) + tracy-test + tracy-profiler in one pass.
#
# How to run:
#   Option A (recommended): In Visual Studio -> Tools -> Command Line -> Developer PowerShell
#                           .\build-all.ps1
#   Option B: In any PowerShell - the script locates MSVC x64 automatically.
#
# Usage:
#   .\build-all.ps1                     # Release: Client + Capture + Test + Profiler
#   .\build-all.ps1 -Config Debug       # Debug build
#   .\build-all.ps1 -Clean              # wipe binary dirs before configure
#   .\build-all.ps1 -Profiler           # legacy switch (no longer needed)

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config   = "Release",
    [switch]$Profiler,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root    = $PSScriptRoot
$suffix  = $Config.ToLower()

if ($Profiler) {
    Write-Host "-Profiler is now implicit in all-* presets; building profiler from root build." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Locate vcvarsall.bat if not already in a VS Dev shell
# ---------------------------------------------------------------------------
$vcvarsall = $null
if (-not $env:VSCMD_ARG_TGT_ARCH) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    }

    $vsRoot = $null
    if (Test-Path $vswhere) {
        $vsRoot = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null | Select-Object -First 1)
    }
    if (-not $vsRoot) {
        foreach ($c in @(
            "D:\Program Files\Microsoft Visual Studio\18\Enterprise",
            "C:\Program Files\Microsoft Visual Studio\18\Enterprise",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
            "C:\Program Files\Microsoft Visual Studio\2022\Community",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional"
        )) { if (Test-Path "$c\VC\Auxiliary\Build\vcvarsall.bat") { $vsRoot = $c; break } }
    }

    if (-not $vsRoot) {
        Write-Error "Cannot find Visual Studio. Run from VS Developer PowerShell or install VS with C++ workload."
    }
    $vcvarsall = "$vsRoot\VC\Auxiliary\Build\vcvarsall.bat"
    Write-Host "Using vcvarsall: $vcvarsall" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Helper: run one cmake configure + build step (possibly through vcvarsall)
# ---------------------------------------------------------------------------
function Invoke-CmakeBuild {
    param(
        [string]$SourceDir,   # directory that contains CMakePresets.json
        [string]$Preset,      # configure preset name
        [string]$BinDir       # binary dir to clean (optional)
    )

    if ($Clean -and $BinDir -and (Test-Path $BinDir)) {
        Write-Host "  Cleaning $BinDir ..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BinDir
    }

    $cfg  = "cmake --preset $Preset"
    $bld  = "cmake --build --preset $($Preset)-build"

    if ($vcvarsall) {
        $cmd = "`"$vcvarsall`" x64 && cd /d `"$SourceDir`" && $cfg && $bld"
        cmd /c $cmd
    } else {
        Push-Location $SourceDir
        Invoke-Expression $cfg
        if ($LASTEXITCODE -eq 0) { Invoke-Expression $bld }
        Pop-Location
    }

    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED in $SourceDir (preset=$Preset)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# ---------------------------------------------------------------------------
# 1. Client + Capture + Test + Profiler  (single cmake from root)
# ---------------------------------------------------------------------------
Write-Host "`n===  Client + Capture + Test + Profiler  ===" -ForegroundColor Cyan
Invoke-CmakeBuild `
    -SourceDir $root `
    -Preset    "all-$suffix" `
    -BinDir    "$root\out\all-$suffix"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
$binAll      = "$root\out\all-$suffix"

Write-Host "`nAll done!" -ForegroundColor Green
Write-Host "  TracyClient.lib            ->  $binAll\TracyClient.lib"
Write-Host "  TracyServer.lib            ->  $binAll\capture\TracyServer.lib"
Write-Host "  tracy-capture.exe          ->  $binAll\capture\tracy-capture.exe"
Write-Host "  tracy-capture-daemon.exe   ->  $binAll\capture\tracy-capture-daemon.exe"
Write-Host "  tracy-test.exe             ->  $binAll\test\tracy-test.exe"
Write-Host "  tracy-profiler.exe (GUI)   ->  $root\out\profiler-$suffix\tracy-profiler.exe" -ForegroundColor Green
