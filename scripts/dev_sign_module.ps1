<#
.SYNOPSIS
Signs the Repatch module with a PixInsight developer key (.xssk).

.DESCRIPTION
Windows counterpart of scripts/dev_sign_module.sh. The key password is read
from the console with echo disabled and held as a SecureString; it is handed
only to the PixInsight signing command and is never written to disk or
printed. Close PixInsight before running this script.

Note that PixInsight takes the password as a command-line argument, so it is
visible to other processes on this machine for the duration of the call --
that is a property of the PixInsight CLI, not of this script.

.PARAMETER XsskFile
Developer key. Defaults to pikey.xssk in the repository root.

.PARAMETER ModuleFile
Module to sign. Defaults to bin\windows\x64\Repatch-pxm.dll.

.PARAMETER PixInsight
PixInsight executable. Defaults to the standard install location.
#>
[CmdletBinding()]
param(
    [string] $XsskFile,
    [string] $ModuleFile,
    [string] $PixInsight = "$env:ProgramFiles\PixInsight\bin\PixInsight.exe"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $XsskFile)   { $XsskFile   = Join-Path $root 'pikey.xssk' }
if (-not $ModuleFile) { $ModuleFile = Join-Path $root 'bin\windows\x64\Repatch-pxm.dll' }

function Fail { param($m) Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $XsskFile))   { Fail "signing key not found: $XsskFile (use -XsskFile)" }
if (-not (Test-Path $ModuleFile)) { Fail "module not found: $ModuleFile (run build.ps1 first)" }
if (-not (Test-Path $PixInsight)) { Fail "PixInsight executable not found: $PixInsight (use -PixInsight)" }

$XsskFile   = (Resolve-Path $XsskFile).Path
$ModuleFile = (Resolve-Path $ModuleFile).Path
$xsgn = [IO.Path]::ChangeExtension($ModuleFile, '.xsgn')

if (Get-Process -Name PixInsight -ErrorAction SilentlyContinue) {
    Fail "PixInsight is running; close it before signing"
}

# An existing signature is deliberately left in place: if this run fails, the
# previous one is still there rather than destroyed. Success is judged by the
# file being newer than this moment, not by its mere existence.
$startedAt = Get-Date

$secure = Read-Host -Prompt "Password for $(Split-Path -Leaf $XsskFile)" -AsSecureString
$plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
try {
    if (-not $plain) { Fail "empty password" }
    Write-Host "Signing $ModuleFile"
    # PixInsight.exe is a Windows GUI-subsystem binary. PowerShell's call
    # operator does NOT wait for such a process, so invoking it with & would
    # let the signature check below run before PixInsight has written the file.
    # Start-Process -Wait waits on the process handle and does wait.
    $proc = Start-Process -FilePath $PixInsight -Wait -PassThru -ArgumentList @(
        "--sign-module-file=$ModuleFile",
        "--xssk-file=$XsskFile",
        "--xssk-password=$plain",
        "--no-splash")
    $code = $proc.ExitCode
} finally {
    $plain = $null
    [GC]::Collect()
}

# Being a GUI binary, PixInsight writes no diagnostics to the console and
# returns a nonzero exit code even when signing succeeds, so the signature file
# is the only reliable indicator (the macOS script relies on it too). It can
# also land a moment after the process exits, hence the short poll.
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    $f = Get-Item $xsgn -ErrorAction SilentlyContinue
    if ($f -and $f.LastWriteTime -ge $startedAt) { break }
    Start-Sleep -Milliseconds 500
}
$f = Get-Item $xsgn -ErrorAction SilentlyContinue
if (-not $f -or $f.LastWriteTime -lt $startedAt) {
    Fail ("signing did not produce a new $xsgn (PixInsight exit code $code). " +
          "Check that the .xssk password is correct and that this key's identity is registered: " +
          "Edit > Local Signing Identity...")
}
if ($code -ne 0) { Write-Host "(PixInsight returned exit code $code; the signature was written, which is what counts)" }
Write-Host "Signature written: $xsgn"
Write-Host "If PixInsight has not yet registered this key's identity (once per machine):"
Write-Host "  Edit > Local Signing Identity..., select $(Split-Path -Leaf $XsskFile), enter its password,"
Write-Host "  tick 'Make the local signing identity persistent', OK."
Write-Host "Install from PixInsight: Process > Modules > Install Modules..., directory $(Split-Path -Parent $ModuleFile)"
