# SPDX-License-Identifier: GPL-2.0-or-later
# Records the screen (Desktop Duplication, ffmpeg ddagrab) while tools\launch.ps1 runs a scenario.
#   tools\record.ps1 -Out build\rec\fight.mp4 -Launch @{Save='000000126'; Lua='tests\ingame\x.lua'; Until='...'; Env=@{...}}
# The game runs full screen on the primary display. Its mode switch at start-up cuts Desktop Duplication
# (DXGI_ERROR_ACCESS_LOST), so ffmpeg starts once the log says the world is loaded (-StartAt regex).
# Give the scenario a short delay (PSTF_TEST_TICK) so nothing happens before the recording runs.
# The video is scaled to -Width at -Fps; recording stops when launch.ps1 returns (-Close is always passed).
param(
  [Parameter(Mandatory)] [string]$Out,
  [Parameter(Mandatory)] [hashtable]$Launch,
  [int]$Fps = 30,
  [int]$Width = 1280,
  [int]$Output = 0,
  [string]$StartAt = 'autoload: world ready',
  [string]$Game = 'C:\GOG Games\Planescape Torment - Enhanced Edition',
  [string]$Ffmpeg = 'ffmpeg'
)
$ErrorActionPreference = 'Stop'
$Out = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
$log = Join-Path $Game 'pst_forever\logs\pst_forever.log'
$t0 = Get-Date

$Launch['Close'] = $true
$job = Start-Job -ScriptBlock {
  param($script, $a)
  & $script @a *>&1 | ForEach-Object { "$_" }
  "launch-exit $LASTEXITCODE"
} -ArgumentList (Join-Path $PSScriptRoot 'launch.ps1'), $Launch

# Wait for the loaded world in a log written after we started.
$ff = $null
while ($job.State -eq 'Running') {
  if ((Test-Path $log) -and (Get-Item $log).LastWriteTime -gt $t0 -and
      (Select-String -Path $log -Pattern $StartAt -Quiet)) { break }
  Start-Sleep -Milliseconds 100
}
if ($job.State -eq 'Running') {
  Start-Sleep -Milliseconds 300
  $filter = "ddagrab=output_idx=${Output}:framerate=${Fps}:draw_mouse=0,hwdownload,format=bgra,scale=${Width}:-2:flags=lanczos,format=yuv420p"
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName = (Get-Command $Ffmpeg).Source
  $psi.Arguments = "-hide_banner -loglevel error -y -f lavfi -i `"$filter`" -c:v libx264 -preset veryfast -crf 16 -r $Fps `"$Out`""
  $psi.UseShellExecute = $false
  $psi.RedirectStandardInput = $true
  $ff = [Diagnostics.Process]::Start($psi)
  Write-Host "record: ffmpeg pid $($ff.Id) -> $Out"
}
$lines = Receive-Job $job -Wait
Remove-Job $job
if ($ff) {
  if (-not $ff.HasExited) { $ff.StandardInput.Write('q'); $ff.StandardInput.Close() }
  if (-not $ff.WaitForExit(30000)) { $ff.Kill() }
}
$code = 1
foreach ($l in $lines) { if ($l -match '^launch-exit (\d+)') { $code = [int]$Matches[1] } }
$lines | Where-Object { $_ -match 'launch:|TEST FAIL|error' } | ForEach-Object { Write-Host $_ }
if (Test-Path $Out) { Write-Host ("record: {0:N1} MB, launch exit {1}" -f ((Get-Item $Out).Length / 1MB), $code) }
else { Write-Host "record: no video" }
exit $code
