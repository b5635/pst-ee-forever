# SPDX-License-Identifier: GPL-2.0-or-later
# Copies version.dll, pst_forever.dll and pst_forever\ (ini) into the game folder. Closes the game first.
# -Remove takes them out again (back to vanilla). An existing pst_forever.ini is kept unless -ResetIni.
param(
  [string]$Game = 'C:\GOG Games\Planescape Torment - Enhanced Edition',
  [switch]$Remove,
  [switch]$ResetIni
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\Release'

Get-Process -Name Torment -ErrorAction SilentlyContinue | ForEach-Object {
  Write-Host "closing Torment (pid $($_.Id))"
  Stop-Process -Id $_.Id -Confirm:$false
  $_.WaitForExit(10000) | Out-Null
}

if ($Remove) {
  foreach ($f in 'version.dll', 'pst_forever.dll') {
    $p = Join-Path $Game $f
    if (Test-Path $p) { Remove-Item $p -Confirm:$false; Write-Host "removed $f" }
  }
  Write-Host 'deploy: removed (pst_forever\ folder with ini/logs kept)'
  exit 0
}

foreach ($f in 'version.dll', 'pst_forever.dll') {
  if (-not (Test-Path (Join-Path $bin $f))) { Write-Host "missing $bin\${f}: run tools\check.ps1"; exit 1 }
  Copy-Item (Join-Path $bin $f) $Game -Force
}
$data = Join-Path $Game 'pst_forever'
New-Item -ItemType Directory -Force $data | Out-Null
$ini = Join-Path $data 'pst_forever.ini'
if ($ResetIni -or -not (Test-Path $ini)) { Copy-Item (Join-Path $root 'data\pst_forever.ini') $ini -Force }
Write-Host "deploy: ok -> $Game"
