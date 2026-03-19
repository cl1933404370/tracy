# Tracy build-all.ps1
# Builds TracyClient + tracy-capture (TracyServer) + tracy-test in one pass.
#
# How to run:
#   Option A (recommended): In Visual Studio -> Tools -> Command Line -> Developer PowerShell
#                           .\build-all.ps1
#   Option B: In any PowerShell - the script locates MSVC x64 automatically.
#
# Usage:
#   .\build-all.ps1              # Release (default)
#   .\build-all.ps1 -Config Debug
#   .\build-all.ps1 -Clean       # wipe binary dir before configure

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$preset = "all-$($Config.ToLower())"
$buildPreset = "$preset-build"
$binDir = "$root\out\all-$($Config.ToLower())"

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
        $vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
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
# Optionally clean binary directory
# ---------------------------------------------------------------------------
if ($Clean -and (Test-Path $binDir)) {
    Write-Host "Cleaning $binDir ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $binDir
}

# ---------------------------------------------------------------------------
# Build cmake commands as a single cmd string so vcvarsall x64 is inherited
# ---------------------------------------------------------------------------
$cmakeConfigure = "cmake --preset $preset"
$cmakeBuild     = "cmake --build --preset $buildPreset"

if ($vcvarsall) {
    # Run both cmake steps inside one cmd session so env from vcvarsall is shared
    $cmdLine = "`"$vcvarsall`" x64 && cd /d `"$root`" && $cmakeConfigure && $cmakeBuild"
    Write-Host "`n==> Running via cmd (MSVC x64 + cmake)..." -ForegroundColor Cyan
    cmd /c $cmdLine
    $exitCode = $LASTEXITCODE
} else {
    # Already in VS Dev shell - run cmake directly
    Push-Location $root
    Write-Host "`n==> $cmakeConfigure" -ForegroundColor Cyan
    Invoke-Expression $cmakeConfigure; $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) {
        Write-Host "`n==> $cmakeBuild" -ForegroundColor Cyan
        Invoke-Expression $cmakeBuild; $exitCode = $LASTEXITCODE
    }
    Pop-Location
}

if ($exitCode -ne 0) {
    Write-Host "`nBuild FAILED (exit $exitCode)" -ForegroundColor Red
    exit $exitCode
}

Write-Host "`nAll done!  $binDir\" -ForegroundColor Green
Write-Host "  TracyClient        (lib)  ->  $binDir\TracyClient.lib"
Write-Host "  TracyServer        (lib)  ->  $binDir\capture\TracyServer.lib"
Write-Host "  tracy-capture      (exe)  ->  $binDir\capture\tracy-capture.exe"
Write-Host "  tracy-capture-daemon(exe) ->  $binDir\capture\tracy-capture-daemon.exe"
Write-Host "  tracy-test         (exe)  ->  $binDir\test\tracy-test.exe"
