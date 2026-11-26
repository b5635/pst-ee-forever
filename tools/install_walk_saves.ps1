# SPDX-License-Identifier: GPL-2.0-or-later
# Copies the full-playthrough saves (Documents\...\all saves\save, one per walkthrough step) into the game's save
# folder under numbers 700000 + the original slot, so they never collide with the test saves (000000126 = the Hive
# test save, 000700126 = "112 Spoke with the Wizard"). tools\launch.ps1 -Save 000700126 then loads it.
# Scenario plan: docs/SCENARIO_TEST_PLAN.md. Quick/auto saves are skipped. Re-running replaces the copies.
#   tools\install_walk_saves.ps1 [-List]
param(
  [string]$Docs = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Planescape Torment - Enhanced Edition'),
  [switch]$List
)
$ErrorActionPreference = 'Stop'
$src = Join-Path $Docs 'all saves\save'
if (-not (Test-Path $src)) { Write-Host "install_walk_saves: no $src"; exit 2 }
$n = 0
foreach ($s in Get-ChildItem $src -Directory) {
  if ($s.Name -notmatch '^(\d{9})-(.+)$') { continue }
  $slot = [int]$Matches[1]
  if ($slot -lt 10) { continue }  # auto/quick saves
  $name = '{0:D9}-{1}' -f (700000 + $slot), $Matches[2]
  if ($List) { Write-Host $name; continue }
  $dst = Join-Path $Docs "save\$name"
  if (Test-Path $dst) { Remove-Item -Recurse -Force $dst -Confirm:$false }
  Copy-Item -Recurse $s.FullName $dst
  $n++
}
if (-not $List) { Write-Host "install_walk_saves: $n saves as 000700NNN-*" }
