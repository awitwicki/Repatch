<#
.SYNOPSIS
Builds PCL's third-party static libraries and PCL-pxi.lib for Windows x64.

.DESCRIPTION
Windows counterpart of scripts/build_pcl.sh. Libraries whose .lib is already
present are skipped, so re-runs are cheap.

Two mechanisms, because PCL's repository is not uniform on Windows:

  * The six third-party libraries ship their own MSBuild projects under
    src/3rdparty/<lib>/windows/vc17, which this script drives directly --
    their flags and defines come from PCL itself.

  * The main library has no such project: src/pcl/windows/ does not exist in
    PCL's git repository (an installed PixInsight has one, but for whatever
    PCL version that core was built from). It is built instead through
    scripts/pcl/CMakeLists.txt, which transcribes the Release|x64 settings of
    PCL's own PCL.vcxproj.

The PCL tree must be writable: the MSBuild projects put object files in
src/.../windows/vc17/x64/Release, and the CMake build directory is created
inside the tree as well.

.PARAMETER PclDir
PCL root (contains include\ and src\). Defaults to $env:PCLDIR, else ..\PCL
relative to the repository root.

.PARAMETER Configuration
Release (default) or Debug.
#>
[CmdletBinding()]
param(
    [string] $PclDir,
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $PclDir) { $PclDir = $env:PCLDIR }
if (-not $PclDir) { $PclDir = Join-Path (Split-Path -Parent $repoRoot) 'PCL' }
if (-not (Test-Path (Join-Path $PclDir 'include\pcl'))) {
    throw "build_pcl.ps1: PCL not found at '$PclDir' (no include\pcl). Pass -PclDir <path>."
}
$PclDir = (Resolve-Path $PclDir).Path

# PCL README conventions. PCLLIBDIR64 is where every project writes its .lib.
$env:PCLDIR      = $PclDir
$env:PCLINCDIR   = Join-Path $PclDir 'include'
$env:PCLSRCDIR   = Join-Path $PclDir 'src'
$env:PCLLIBDIR64 = Join-Path $PclDir 'lib\windows\x64'
$env:PCLLIBDIR   = $env:PCLLIBDIR64
$env:PCLBINDIR64 = Join-Path $PclDir 'bin'
$env:PCLBINDIR   = $env:PCLBINDIR64
New-Item -ItemType Directory -Force -Path $env:PCLLIBDIR64, $env:PCLBINDIR64 | Out-Null

# MSBuild from the newest VS install that actually has the C++ toolset.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "build_pcl.ps1: vswhere.exe not found; is Visual Studio installed?" }
$msbuild = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) {
    throw "build_pcl.ps1: no Visual Studio install with the C++ toolset was found. Install the component Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ('MSVC v143 - VS 2022 C++ x64/x86 build tools')."
}
Write-Host "[pcl] msbuild: $msbuild"
Write-Host "[pcl] PCLDIR:  $PclDir"

# --- third-party libraries: PCL's own MSBuild projects ---------------------
foreach ($lib in @('cminpack', 'lcms', 'lz4', 'RFC6234', 'zlib', 'zstd')) {
    $out = Join-Path $env:PCLLIBDIR64 "$lib-pxi.lib"
    if (Test-Path $out) {
        Write-Host "[pcl] $lib-pxi.lib present"
        continue
    }
    $proj = Join-Path $PclDir "src\3rdparty\$lib\windows\vc17\$lib.vcxproj"
    if (-not (Test-Path $proj)) { throw "build_pcl.ps1: missing project $proj" }
    Write-Host "[pcl] building $lib-pxi.lib"
    & $msbuild $proj /nologo /m /p:Configuration=$Configuration /p:Platform=x64 /v:minimal /clp:Summary
    if ($LASTEXITCODE -ne 0) { throw "build_pcl.ps1: MSBuild failed for $lib (exit $LASTEXITCODE)" }
}

# --- PCL itself ------------------------------------------------------------
# Prefer PCL's own project when the tree has one (a tree taken from an
# installed PixInsight does); fall back to our CMake transcription for a tree
# from PCL's git repository, which does not carry src/pcl/windows.
$pclLib = Join-Path $env:PCLLIBDIR64 'PCL-pxi.lib'
$pclProj = Join-Path $PclDir 'src\pcl\windows\vc17\PCL.vcxproj'
if (Test-Path $pclLib) {
    Write-Host "[pcl] PCL-pxi.lib present"
} elseif (Test-Path $pclProj) {
    Write-Host "[pcl] building PCL-pxi.lib from PCL's own project (this takes several minutes)"
    & $msbuild $pclProj /nologo /m /p:Configuration=$Configuration /p:Platform=x64 /v:minimal /clp:Summary
    if ($LASTEXITCODE -ne 0) { throw "build_pcl.ps1: MSBuild failed for PCL (exit $LASTEXITCODE)" }
} else {
    Write-Host "[pcl] no src\pcl\windows\vc17\PCL.vcxproj in this tree"
    Write-Host "[pcl] building PCL-pxi.lib via scripts\pcl (this takes several minutes)"
    $src = Join-Path $PSScriptRoot 'pcl'
    # Kept inside the PCL tree so that cleaning Repatch's build directory does
    # not throw away a long PCL build.
    $bld = Join-Path $PclDir 'build-windows-x64'
    cmake -S $src -B $bld -G "Visual Studio 17 2022" -A x64 `
        "-DPCL_DIR=$PclDir" "-DPCL_LIB_OUT=$($env:PCLLIBDIR64)"
    if ($LASTEXITCODE -ne 0) { throw "build_pcl.ps1: cmake configure failed for PCL" }
    cmake --build $bld --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "build_pcl.ps1: cmake build failed for PCL" }
}
if (-not (Test-Path $pclLib)) { throw "build_pcl.ps1: build finished but $pclLib is missing" }

Write-Host "[pcl] libraries in $($env:PCLLIBDIR64):"
# Out-String renders here rather than emitting format objects, which would
# corrupt the display if a caller pipes this script's output anywhere.
Get-ChildItem $env:PCLLIBDIR64 |
    Format-Table Name, Length, LastWriteTime -AutoSize | Out-String | Write-Host
