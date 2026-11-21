# SPDX-License-Identifier: GPL-2.0-or-later
# Reads the engine's newest crash dump (or -Dump FILE) with cdb: exception context, registers, the
# faulting code and the stack, with pst_forever.pdb symbols from build\Release.
# cdb comes with WinDbg (winget install Microsoft.WinDbg). Torment.exe has no PDB: map its addresses
# with `python -P tools/re.py find`/`addr`, or run `python -I tools/crash.py` for named frames.
param([string]$Dump = '', [string]$Extra = '')
$cdb = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\cdbX64.exe'
if (-not (Test-Path $cdb)) { Write-Host 'dbg: cdb not found: winget install Microsoft.WinDbg'; exit 2 }
if (-not $Dump) {
  $Dump = (Get-ChildItem "$env:USERPROFILE\Documents\Infinity Engine - Enhanced Edition\crash\*.dmp" |
           Sort-Object LastWriteTime | Select-Object -Last 1).FullName
}
$sym = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release'
Write-Host "dbg: $Dump"
& $cdb -z $Dump -y "$sym" -c ".ecxr; r; u @rip-20 L14; kn 25; $Extra; q" 2>&1 |
  Select-String -NotMatch '^\s*$|NatVis|Symbol search|Deferred|Microsoft \(R\)|Copyright|\*\*\*|Files\WindowsApps|lizers\' |
  ForEach-Object { $_.Line }
