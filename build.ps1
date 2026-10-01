<#
.SYNOPSIS
Repatch build script (Windows, x64).

.DESCRIPTION
Windows counterpart of build.sh.

  1. checks the toolchain (MSVC v143, cmake)
  2. builds PCL's static libraries from ..\PCL once (scripts\build_pcl.ps1)
  3. configures and builds with CMake (core, tests, CLI, module)
  4. runs the unit tests and a CLI smoke run on a synthetic image
  5. verifies bin\windows\x64\Repatch-pxm.dll

Signing is a separate step: scripts\dev_sign_module.ps1

.PARAMETER PclPath
PCL root. Defaults to $env:PCLDIR, else ..\PCL relative to this script.

.PARAMETER NoTests
Skip ctest and the CLI smoke run.

.PARAMETER NoModule
Build core, tests and CLI only; no PCL checkout needed.

.PARAMETER Debug
Build the Debug configuration instead of Release.

.PARAMETER Clean
Remove the build directory first.
#>
[CmdletBinding()]
param(
    [string] $PclPath,
    [switch] $NoTests,
    [switch] $NoModule,
    [switch] $DebugBuild,
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$config = if ($DebugBuild) { 'Debug' } else { 'Release' }

function Info    { param($m) Write-Host "[INFO] $m"  -ForegroundColor Blue }
function Ok      { param($m) Write-Host "[OK] $m"    -ForegroundColor Green }
function Fail    { param($m) Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

# --- toolchain -------------------------------------------------------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Fail "required tool not found: cmake"
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found; install Visual Studio 2022" }
$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $vsPath) {
    Fail "no Visual Studio install with the C++ toolset was found. Install the 'Desktop development with C++' workload."
}
Info "visual studio: $vsPath"
Info "cmake: $((cmake --version | Select-Object -First 1))"

# --- PCL -------------------------------------------------------------------
$cmakeArgs = @("-DCMAKE_BUILD_TYPE=$config")
if (-not $NoModule) {
    if (-not $PclPath) { $PclPath = $env:PCLDIR }
    if (-not $PclPath) { $PclPath = Join-Path (Split-Path -Parent $root) 'PCL' }
    if (-not (Test-Path (Join-Path $PclPath 'include\pcl'))) {
        Fail "PCL not found at '$PclPath'; pass -PclPath <dir>"
    }
    $PclPath = (Resolve-Path $PclPath).Path
    Info "PCL: $PclPath"
    & (Join-Path $root 'scripts\build_pcl.ps1') -PclDir $PclPath -Configuration Release
    if ($LASTEXITCODE -ne 0) { Fail "PCL build failed" }
    $cmakeArgs += @("-DREPATCH_BUILD_MODULE=ON", "-DPCL_DIR=$PclPath")
} else {
    $cmakeArgs += "-DREPATCH_BUILD_MODULE=OFF"
}

# --- configure and build ---------------------------------------------------
$buildDir = Join-Path $root 'build'
if ($Clean -and (Test-Path $buildDir)) {
    Info "removing $buildDir"
    Remove-Item $buildDir -Recurse -Force
}

Info "configuring ($config)"
# The Visual Studio generator is always available with the C++ workload; it is
# multi-config, so the configuration is selected at build and test time too.
cmake -S $root -B $buildDir -G "Visual Studio 17 2022" -A x64 @cmakeArgs
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }

Info "building"
cmake --build $buildDir --config $config --parallel
if ($LASTEXITCODE -ne 0) { Fail "build failed" }

$binDir = Join-Path $buildDir $config

# --- tests -----------------------------------------------------------------
if (-not $NoTests) {
    Info "running unit tests"
    Push-Location $buildDir
    try {
        ctest --output-on-failure --build-config $config
        if ($LASTEXITCODE -ne 0) { Fail "unit tests failed" }
    } finally { Pop-Location }

    Info "CLI smoke run"
    $cli = Join-Path $binDir 'repatch-cli.exe'
    & $cli --synth 256 256 (Join-Path $buildDir 'smoke_in.fits') (Join-Path $buildDir 'smoke_mask.fits')
    if ($LASTEXITCODE -ne 0) { Fail "CLI synth failed" }
    & $cli (Join-Path $buildDir 'smoke_in.fits') (Join-Path $buildDir 'smoke_mask.fits') (Join-Path $buildDir 'smoke_out.fits') --seed 1
    if ($LASTEXITCODE -ne 0) { Fail "CLI fill failed" }
    Ok "tests and smoke run passed"
}

# --- module ----------------------------------------------------------------
if (-not $NoModule) {
    $out = Join-Path $root 'bin\windows\x64\Repatch-pxm.dll'
    if (-not (Test-Path $out)) { Fail "module not found at $out" }

    # dumpbin lives in the MSVC toolset; locate it through the VS install.
    $dumpbin = Get-ChildItem (Join-Path $vsPath 'VC\Tools\MSVC') -Filter dumpbin.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
    if ($dumpbin) {
        $headers = & $dumpbin.FullName /headers $out
        if (-not ($headers | Select-String -Quiet 'x64 \(unknown\)|machine \(x64\)')) {
            Fail "module is not an x64 binary"
        }
        $exports = & $dumpbin.FullName /exports $out
        if (-not ($exports | Select-String -Quiet 'InstallPixInsightModule')) {
            Fail "module does not export InstallPixInsightModule"
        }
    } else {
        Info "dumpbin not found; skipping export verification"
    }
    $kb = [math]::Round((Get-Item $out).Length / 1KB)
    Ok "module: $out ($kb KB)"

    Write-Host ""
    Write-Host "Next steps:"
    Write-Host "  1. scripts\dev_sign_module.ps1            (prompts for the .xssk password)"
    Write-Host "  2. Once per machine: PixInsight > Edit > Local Signing Identity..., select the"
    Write-Host "     .xssk, enter its password, tick 'Make the local signing identity persistent'"
    Write-Host "     (otherwise install fails with 'Unknown code signing identity')"
    Write-Host "  3. PixInsight > Process > Modules > Install Modules..., browse to"
    Write-Host "     $root\bin\windows\x64  and click Search, then Install"
}
