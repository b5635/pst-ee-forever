# SPDX-License-Identifier: GPL-2.0-or-later
# Runs tests\ingame\tbc_spells_all.lua on the all-spells save (000000990) and writes tests\unit\spell_results.h.
#   tools\spell_suite.ps1                   every spell (~15 min)
#   tools\spell_suite.ps1 -Only SPWI119     just these (comma list); spell_results.h is not rewritten then
param([string]$Only = '', [int]$Timeout = 2400)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tmp = Join-Path $root 'build\tmp_tbc_spells_all.lua'
New-Item -ItemType Directory -Force (Split-Path $tmp) | Out-Null
$pre = if ($Only) { "pstfSpellsOnly = '$Only'`n" } else { '' }
$pre + (Get-Content (Join-Path $root 'tests\ingame\spell_list.lua') -Raw) + "`n" + (Get-Content (Join-Path $root 'tests\ingame\tbc_spells_all.lua') -Raw) |
  Set-Content -Path $tmp -Encoding utf8
$envs = @{PSTF_TEST_TICK = '1'}
& (Join-Path $PSScriptRoot 'launch.ps1') -Save 000000990 -Lua $tmp -Env $envs -Until 'tbc_spells_all: ok|TEST FAIL' -Close -Timeout $Timeout
$rc = $LASTEXITCODE
$log = 'C:\GOG Games\Planescape Torment - Enhanced Edition\pst_forever\logs\pst_forever.log'
Select-String -Path $log -Pattern 'tbc_spells_all:|TEST FAIL' | ForEach-Object { $_.Line }
if (-not $Only) { python -I (Join-Path $PSScriptRoot 'spell_results.py') $log }
exit $rc
