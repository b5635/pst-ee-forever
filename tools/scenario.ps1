# SPDX-License-Identifier: GPL-2.0-or-later
# Runs one scenario of docs/SCENARIO_TEST_PLAN.md: tests\ingame\scenarios\lib.lua + the scenario file, in its save,
# then scans the run's log lines for trouble (script state lost, faults, crashes) and prints a verdict line.
# A scenario file starts with a header line:   -- scenario: save=000700162 env=PSTF_TBC_AUTOPARTY=1 timeout=300
#   tools\scenario.ps1 -Id S23            (file tests\ingame\scenarios\S23_*.lua)
#   tools\scenario.ps1 -Id S00 -Save 000700083   (lists every creature of that save's area: ids, names, EA, hp)
#   tools\scenario.ps1 -Id S23 -Shots     (also PSTF_SHOT_DIR = logs\scen_S23, a half-size shot every 3 s)
# Exit 0 = ran to "ok", 1 = ISSUE lines (read them), 2 = TEST FAIL / no result. Saves: tools\install_walk_saves.ps1.
param(
  [Parameter(Mandatory = $true)][string]$Id,
  [switch]$Shots,
  [string]$Save = '',          # override the header's save (S00 = list the actors of any save)
  [string]$Game = 'C:\GOG Games\Planescape Torment - Enhanced Edition'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$file = Get-ChildItem (Join-Path $root 'tests\ingame\scenarios') -Filter "$($Id)_*.lua" | Select-Object -First 1
if (-not $file) { Write-Host "scenario: no tests\ingame\scenarios\$($Id)_*.lua"; exit 2 }
$head = (Get-Content $file.FullName -TotalCount 1)
if ($head -notmatch '^-- scenario:') { Write-Host "scenario: $($file.Name) has no '-- scenario:' header"; exit 2 }
$opt = @{ save = ''; timeout = '300'; env = '' }
foreach ($kv in ($head -replace '^-- scenario:\s*', '') -split '\s+') {
  if ($kv -match '^(\w+)=(.*)$') { $opt[$Matches[1]] = $Matches[2] }
}
if ($Save) { $opt.save = $Save }
$envs = @{ PSTF_TEST_TICK = '1' }
foreach ($e in ($opt.env -split ',')) { if ($e -match '^(\w+)=(.*)$') { $envs[$Matches[1]] = $Matches[2] } }
if ($Shots) {
  $dir = Join-Path $Game "pst_forever\logs\scen_$Id"
  New-Item -ItemType Directory -Force $dir | Out-Null
  $envs.PSTF_SHOT_DIR = $dir; $envs.PSTF_SHOT_EVERY_MS = '3000'
}
$out = Join-Path $root "build\scen"
New-Item -ItemType Directory -Force $out | Out-Null
$lua = Join-Path $out "$Id.lua"
$text = "S = {id = '$Id'}`n" + (Get-Content -Raw (Join-Path $root 'tests\ingame\scenarios\lib.lua')) + "`n" + (Get-Content -Raw $file.FullName)
Set-Content -Path $lua -Value $text -Encoding utf8
$log = Join-Path $Game 'pst_forever\logs\pst_forever.log'
$res = & (Join-Path $PSScriptRoot 'launch.ps1') -Save $opt.save -Lua $lua -Env $envs -Until "scenario $($Id): (ok|ISSUE summary)|TEST FAIL" -Close -Timeout ([int]$opt.timeout) *>&1
$res | Where-Object { $_ -match "scenario $Id|TEST FAIL|launch:" } | ForEach-Object { Write-Host $_ }
# Trouble the engine side logged during the run (the log is rewritten per launch)
$trouble = Select-String -Path $log -Pattern 'script state lost|faulted|CRASH [(]|test tick error|lua error' -ErrorAction SilentlyContinue |
  Select-Object -First 25
foreach ($t in $trouble) { Write-Host "  log: $($t.Line.Trim())" }
$all = $res -join "`n"
if ($all -match 'TEST FAIL' -or $all -notmatch "scenario $($Id): (ok|ISSUE summary)") { Write-Host "scenario ${Id}: FAIL"; exit 2 }
if ($all -match "scenario $($Id): ISSUE" -or $trouble) { Write-Host "scenario ${Id}: ISSUES (see above, combat.log)"; exit 1 }
Write-Host "scenario ${Id}: ok"; exit 0
