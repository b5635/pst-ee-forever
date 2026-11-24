# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the all-spells test save and stores it in the repository (tests\saves\000000990-PSTF-AllSpells) and in
# the game's save folder (same name: tools\launch.ps1 -Save 000000990 loads it).
#   tools\make_spell_save.ps1            from the Hive save 000000126 (tests\ingame\make_spell_save.lua)
# The game writes a quick save on the way (its own rotating 000000001-Quick-Save* folders).
param(
  [string]$From = '000000126',
  [string]$Name = '000000990-PSTF-AllSpells',
  [string]$Docs = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Planescape Torment - Enhanced Edition')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tmp = Join-Path $root 'build\tmp_make_spell_save.lua'
New-Item -ItemType Directory -Force (Split-Path $tmp) | Out-Null
(Get-Content (Join-Path $root 'tests\ingame\spell_list.lua') -Raw) + "`n" + (Get-Content (Join-Path $root 'tests\ingame\make_spell_save.lua') -Raw) |
  Set-Content -Path $tmp -Encoding utf8
$start = Get-Date
& (Join-Path $PSScriptRoot 'launch.ps1') -Save $From -Lua $tmp -Env @{PSTF_TEST_TICK = '1'; PSTF_TBC_NOFIGHT = '1'} `
  -Until 'make_spell_save: ok|TEST FAIL' -Close -Timeout 300
if ($LASTEXITCODE -ne 0) { Write-Host 'make_spell_save: the game run failed'; exit 1 }
# The engine rotates its quick-save folders (000000001-Quick-Save, -2, -3 ...): take the one it just wrote.
$quick = Get-ChildItem (Join-Path $Docs 'save') -Directory -Filter '000000001-Quick-Save*' |
  Where-Object { $_.LastWriteTime -ge $start } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $quick) { Write-Host 'make_spell_save: no new quick save'; exit 1 }
$quick = $quick.FullName
foreach ($dst in @((Join-Path $root "tests\saves\$Name"), (Join-Path $Docs "save\$Name"))) {
  if (Test-Path $dst) { Remove-Item -Recurse -Force $dst -Confirm:$false }
  Copy-Item -Recurse $quick $dst
  Write-Host "make_spell_save: saved $dst"
}
