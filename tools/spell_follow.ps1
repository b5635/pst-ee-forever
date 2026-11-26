# SPDX-License-Identifier: GPL-2.0-or-later
# Follows one spell turn by turn (tests\ingame\spell_follow.lua) and prints its lines: effects, stats, state, icons
# of the creature it changed at the end of the cast turn and at each of its turn starts.
#   tools\spell_follow.ps1 -Caster Grace -Cast 'SpellNoDec(Myself,1302)' -Owner self -Stats 14,84 [-Turns 4] [-Level 14]
#   -Pre 'ChangeStat(Myself,ARMORCLASS,20,SET)': an action on the caster before the fight
#   -FoePre 'ChangeStat(Myself,THAC0,0,SET)': an action on the foe as it is rigged (every swing hits)
#   -FoeAttacks Player1: the foe attacks that party member on its turns (reflected damage, Black-Barbed Shield)
#   -Owner self | foe | hit (the creature whose HP changed in the cast turn)   -Saves pass: the foe always saves
param([string]$Caster = 'Grace', [Parameter(Mandatory)][string]$Cast, [string]$Owner = 'self', [int[]]$Stats = @(),
      [int]$Turns = 4, [int]$Level = 0, [string]$Saves = 'fail', [int]$Timeout = 400,
      [string]$FoeAttacks = '', [string]$Pre = '', [string]$FoePre = '', [int]$OwnerHp = 0, [switch]$Lists, [switch]$CasterAttacks, [switch]$During, [int]$FoeHp = 0)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tmp = Join-Path $root 'build\tmp_spell_follow.lua'
New-Item -ItemType Directory -Force (Split-Path $tmp) | Out-Null
$lvl = if ($Level -gt 0) { ", level = $Level" } else { '' }
$pre = "pstfFollow = {caster = '$Caster', cast = '$Cast', owner = '$Owner', turns = $Turns, stats = {$($Stats -join ',')}, saves = '$Saves'$lvl$(if ($FoeAttacks) { ", foeAttacks = '$FoeAttacks'" })$(if ($Pre) { ", pre = '$Pre'" })$(if ($FoePre) { ", foePre = '$FoePre'" })$(if ($OwnerHp) { ", ownerHp = $OwnerHp" })$(if ($Lists) { ", lists = true" })$(if ($CasterAttacks) { ", casterAttacks = true" })$(if ($During) { ", during = true" })$(if ($FoeHp) { ", foeHp = $FoeHp" })}`n"
$pre + (Get-Content (Join-Path $root 'tests\ingame\spell_follow.lua') -Raw) | Set-Content -Path $tmp -Encoding utf8
& (Join-Path $PSScriptRoot 'launch.ps1') -Save 000000990 -Lua $tmp -Env @{PSTF_TEST_TICK = '1'} -Until 'spell_follow: ok|TEST FAIL' -Close -Timeout $Timeout *> $null
$rc = $LASTEXITCODE
$log = 'C:\GOG Games\Planescape Torment - Enhanced Edition\pst_forever\logs\pst_forever.log'
$lines = Get-Content $log
for ($i = 0; $i -lt $lines.Count; $i++) {
  $l = $lines[$i]
  if ($l -match 'spell_follow:|TEST FAIL|WARNING|faulted') { $l.Substring(13) }
  elseif ($l -match 'PSTF_Effects|PSTF_Status' -and $i + 1 -lt $lines.Count -and ($lines[$i + 1] -match 'spell_follow:' -or ($i + 2 -lt $lines.Count -and $lines[$i + 2] -match 'spell_follow:'))) { '   ' + $l.Substring(13) }
}
exit $rc
