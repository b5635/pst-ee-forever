# SPDX-License-Identifier: GPL-2.0-or-later
# Launch PST:EE straight into a save and (optionally) run a Lua test file once the world is up.
#   tools\launch.ps1                                  newest save, no script, game stays open
#   tools\launch.ps1 -Save 000000013 -Lua tests\ingame\smoke.lua -Close
# How: sets Baldur.lua 'Game Options','Last Save PST' to the save, starts Torment.exe with
# PSTF_AUTOLOAD=1 (+ PSTF_RUN_LUA), then waits on pst_forever.log for "dev: autoload: done".
# Needs the DLL deployed (tools\deploy.ps1). Prints the dev:/lua: log lines. Exit 0 = loaded, script ok.
param(
  [string]$Save = '',                 # save folder name or a prefix of it; default: newest
  [string]$Lua = '',                  # Lua file to run in the engine state when the world is ready
  [int]$Timeout = 120,
  [switch]$Close,                     # close the game afterwards (by PID)
  [string]$Game = 'C:\GOG Games\Planescape Torment - Enhanced Edition',
  [string]$Docs = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Planescape Torment - Enhanced Edition')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

Get-Process -Name Torment -ErrorAction SilentlyContinue | ForEach-Object {
  Write-Host "closing Torment (pid $($_.Id))"
  Stop-Process -Id $_.Id -Confirm:$false
  $_.WaitForExit(10000) | Out-Null
}

$saves = Get-ChildItem (Join-Path $Docs 'save') -Directory | Sort-Object LastWriteTime -Descending
$pick = if ($Save) { $saves | Where-Object { $_.Name -like "$Save*" } | Select-Object -First 1 } else { $saves | Select-Object -First 1 }
if (-not $pick) { Write-Host "launch: no save matching '$Save' in $Docs\save"; exit 2 }

# The engine reads Baldur.lua at start and rewrites it on exit; one line per key.
$ini = Join-Path $Docs 'Baldur.lua'
$line = "SetPrivateProfileString('Game Options','Last Save PST','$($pick.Name -replace "'", "\'")')"
$lines = @(Get-Content $ini | Where-Object { $_ -notmatch "^SetPrivateProfileString\('Game Options','Last Save PST'," })
$text = (($lines + $line) -join "`r`n") + "`r`n"
[IO.File]::WriteAllText($ini, $text, (New-Object Text.UTF8Encoding $false))

$log = Join-Path $Game 'pst_forever\logs\pst_forever.log'
$env:PSTF_AUTOLOAD = '1'
$env:PSTF_RUN_LUA = ''
if ($Lua) {
  $cand = if ([IO.Path]::IsPathRooted($Lua)) { $Lua } else { Join-Path $root $Lua }
  if (-not (Test-Path $cand)) { Write-Host "launch: no Lua file $cand"; exit 2 }
  $env:PSTF_RUN_LUA = (Resolve-Path $cand).Path
}
$p = Start-Process -FilePath (Join-Path $Game 'Torment.exe') -WorkingDirectory $Game -PassThru
Remove-Item Env:PSTF_AUTOLOAD, Env:PSTF_RUN_LUA -ErrorAction SilentlyContinue
Write-Host "launch: pid $($p.Id), save '$($pick.Name)'"

$deadline = (Get-Date).AddSeconds($Timeout)
$state = 'timeout'
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 500
  if ($p.HasExited) { $state = 'exited'; break }
  if (-not (Test-Path $log)) { continue }
  $t = Get-Content $log -Raw -ErrorAction SilentlyContinue
  if ($t -match 'dev: autoload: FAILED') { $state = 'failed'; break }
  if ($t -match 'dev: autoload: done') { $state = 'done'; break }
}
Get-Content $log | Select-String -Pattern ' (dev|lua|tick|manifest|patch): ' | ForEach-Object { Write-Host "  $($_.Line)" }
$scriptOk = -not (Select-String -Path $log -Pattern 'dev: autorun .*: lua error' -Quiet)
if ($Close -and -not $p.HasExited) { Stop-Process -Id $p.Id -Confirm:$false; Write-Host "launch: closed pid $($p.Id)" }
Write-Host "launch: $state"
if ($state -eq 'done' -and $scriptOk) { exit 0 } else { exit 1 }
