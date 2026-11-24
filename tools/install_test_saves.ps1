# SPDX-License-Identifier: GPL-2.0-or-later
# Copies the versioned test saves (tests\saves\*) into the game's save folder, so tools\launch.ps1 -Save <prefix>
# finds them on a fresh machine. Existing saves of the same name are replaced; other saves are left alone.
#   tools\install_test_saves.ps1
param(
  [string]$Docs = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Planescape Torment - Enhanced Edition')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
foreach ($s in Get-ChildItem (Join-Path $root 'tests\saves') -Directory) {
  $dst = Join-Path $Docs "save\$($s.Name)"
  if (Test-Path $dst) { Remove-Item -Recurse -Force $dst -Confirm:$false }
  Copy-Item -Recurse $s.FullName $dst
  Write-Host "install_test_saves: $($s.Name)"
}
