# SPDX-License-Identifier: GPL-2.0-or-later
# Default loop: build (Release x64), L0 unit tests, static hook verification against the on-disk exe.
# Prints only errors and the summary; full logs in build\logs\.
param([string]$Exe = 'C:\GOG Games\Planescape Torment - Enhanced Edition\Torment.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$logs = Join-Path $build 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null

# The user PATH has a stray quote (Nsight entry); use a clean PATH. The VS generator finds MSBuild itself.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0;C:\Program Files\CMake\bin"

$fail = 0
function Step($name, [scriptblock]$cmd) {
  $log = Join-Path $logs "$name.log"
  $ErrorActionPreference = 'Continue'  # PS 5.1 turns native stderr into errors
  & $cmd *> $log
  if ($LASTEXITCODE -ne 0) {
    Write-Host "FAIL $name (exit $LASTEXITCODE), log: $log"
    Get-Content $log | Select-String -Pattern 'error|FAIL' | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
    $script:fail++
    return $false
  }
  return $true
}

$ok = Step 'configure' { cmake -S $root -B $build -G 'Visual Studio 16 2019' -A x64 }
if ($ok) { $ok = Step 'build' { cmake --build $build --config Release -- /m /nologo /v:m } }
if ($ok) {
  Step 'unit' { & "$build\Release\pstf_tests.exe" } | Out-Null
  Step 'verify' { & "$build\Release\pstf_verify.exe" $Exe } | Out-Null
  Get-Content "$logs\unit.log", "$logs\verify.log" | Select-String -Pattern '^(unit|verify):' | ForEach-Object { Write-Host $_ }
}
if ($fail) { Write-Host "check: $fail step(s) failed"; exit 1 }
Write-Host 'check: ok'
