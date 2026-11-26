# SPDX-License-Identifier: GPL-2.0-or-later
# Runs tests\ingame\tbc_spells_all.lua on the all-spells save (000000990) and writes tests\unit\spell_results.h.
#   tools\spell_suite.ps1                   every spell (~15 min)
#   tools\spell_suite.ps1 -Only SPWI119     just these (comma list); their rows in spell_results.h are refreshed (merge)
#   -Env @{NAME='1'}                        extra game environment
#   -Saves pass                             the foe always saves (default fail: it never does); spell_results.h untouched
#   -Verbose                                print the whole game log (default: spell lines only)
#   -Trace                                  log the caster's action and animation while a cast has not started
param([string[]]$Only = @(), [int]$Timeout = 2400, [hashtable]$Env = @{}, [switch]$Trace, [switch]$Verbose, [string]$Saves = 'fail')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tmp = Join-Path $root 'build\tmp_tbc_spells_all.lua'
New-Item -ItemType Directory -Force (Split-Path $tmp) | Out-Null
$Only = ($Only -join ',') -replace '\s', ''  # -Only A,B arrives as an array
$pre = if ($Only) { "pstfSpellsOnly = '$Only'`n" } else { '' }
if ($Trace) { $pre += "pstfSpellsTrace = true`n" }
$pre += "pstfSpellsSaves = '$Saves'`n"
$pre + (Get-Content (Join-Path $root 'tests\ingame\spell_list.lua') -Raw) + "`n" + (Get-Content (Join-Path $root 'tests\ingame\tbc_spells_all.lua') -Raw) |
  Set-Content -Path $tmp -Encoding utf8
$envs = @{PSTF_TEST_TICK = '1'; PSTF_FEEDBACK_LOG = '1'}  # feedback: PSTF_Casts, the proof of a cast
foreach ($k in $Env.Keys) { $envs[$k] = $Env[$k] }
$launch = { & (Join-Path $PSScriptRoot 'launch.ps1') -Save 000000990 -Lua $tmp -Env $envs -Until 'tbc_spells_all: ok|TEST FAIL' -Close -Timeout $Timeout }
if ($Verbose) { & $launch } else { & $launch *> $null }  # launch.ps1 prints the whole game log
$rc = $LASTEXITCODE
$log = 'C:\GOG Games\Planescape Torment - Enhanced Edition\pst_forever\logs\pst_forever.log'
Select-String -Path $log -Pattern 'tbc_spells_all: (SP\w+ (ok|fizzle|stall)|\d+ spells|ok$)|TEST FAIL|spell hold cap|faulted|CRASH' | ForEach-Object { $_.Line }
# a turn that ended on the safety cap cut its spell short: the spell did not finish in its turn
if ($rc -eq 0 -and (Select-String -Path $log -Pattern 'WARNING spell hold cap' -Quiet)) { Write-Output 'spell_suite: FAIL, a spell hit the turn hold cap'; $rc = 1 }
if ($rc -eq 0 -and $Saves -eq 'fail') {
  if ($Only) { python -I (Join-Path $PSScriptRoot 'spell_results.py') --merge $log }
  else { python -I (Join-Path $PSScriptRoot 'spell_results.py') $log }
}
exit $rc
