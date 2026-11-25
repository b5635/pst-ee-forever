# SPDX-License-Identifier: GPL-2.0-or-later
# Records the screen (Desktop Duplication, ffmpeg ddagrab) while tools\launch.ps1 runs a scenario.
#   tools\record.ps1 -Out build\rec\fight.mp4 -Launch @{Save='000000126'; Lua='tests\ingame\x.lua'; Until='...'; Env=@{...}}
# The game runs full screen on the primary display. Its mode switch at start-up cuts Desktop Duplication
# (DXGI_ERROR_ACCESS_LOST), so ffmpeg starts once the log says the world is loaded (-StartAt regex).
# Give the scenario a short delay (PSTF_TEST_TICK) so nothing happens before the recording runs.
# The video is scaled to -Width at -Fps; recording stops when launch.ps1 returns (-Close is always passed).
# Sound: tools\audio_loop.py records the speakers (WASAPI loopback) beside ffmpeg; both log the wall clock
# of their first sample/frame (ffmpeg via RTCTIME + showinfo in an FFREPORT log), and the mux lines them up.
# -NoAudio = video only. Needs PyAudioWPatch (python -m pip install --user PyAudioWPatch).
param(
  [Parameter(Mandatory)] [string]$Out,
  [Parameter(Mandatory)] [hashtable]$Launch,
  [int]$Fps = 30,
  [int]$Width = 1280,
  [int]$Output = 0,
  [string]$StartAt = 'autoload: world ready',
  [string]$Game = 'C:\GOG Games\Planescape Torment - Enhanced Edition',
  [string]$Ffmpeg = 'ffmpeg',
  [switch]$NoAudio,
  [switch]$DrawMouse,   # show the mouse pointer in the video
  [switch]$Clicks       # a log line 'showcase_click: DX,DY' (pixels from the screen centre) glides the real mouse there and clicks
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
$ffExe = (Get-Command $Ffmpeg).Source
$dir = Split-Path -Parent $Out
$stem = [IO.Path]::GetFileNameWithoutExtension($Out)
$video = if ($NoAudio) { $Out } else { Join-Path $dir "$stem.video.mp4" }
$wav = Join-Path $dir "$stem.audio.wav"
$stop = Join-Path $dir "$stem.audio.stop"
$report = Join-Path $dir "$stem.ffreport.log"
$au = $null
if ($job.State -eq 'Running') {
  Start-Sleep -Milliseconds 300
  if (-not $NoAudio) {
    Remove-Item $stop, $wav -ErrorAction SilentlyContinue
    $apsi = New-Object Diagnostics.ProcessStartInfo
    $apsi.FileName = (Get-Command python).Source
    $apsi.Arguments = "-P `"$(Join-Path $PSScriptRoot 'audio_loop.py')`" `"$wav`" `"$stop`""
    $apsi.UseShellExecute = $false
    $apsi.RedirectStandardOutput = $true
    $au = [Diagnostics.Process]::Start($apsi)
  }
  # Branch 2 of the graph logs the first frame's wall clock (RTCTIME, us since 1970) for the audio sync.
  $dm = if ($DrawMouse) { 1 } else { 0 }
  $filter = "ddagrab=output_idx=${Output}:framerate=${Fps}:draw_mouse=${dm},hwdownload,format=bgra,split[v][s];" +
            "[v]scale=${Width}:-2:flags=lanczos,format=yuv420p[out0];" +
            "[s]select=eq(n\,0),setpts=RTCTIME/1000000/TB,showinfo,nullsink"
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName = $ffExe
  $psi.Arguments = "-hide_banner -loglevel error -y -f lavfi -i `"$filter`" -c:v libx264 -preset veryfast -crf 16 -r $Fps `"$video`""
  $psi.UseShellExecute = $false
  $psi.RedirectStandardInput = $true
  $psi.WorkingDirectory = $dir
  $psi.EnvironmentVariables['FFREPORT'] = "file=$stem.ffreport.log:level=32"
  $ff = [Diagnostics.Process]::Start($psi)
  Write-Host "record: ffmpeg pid $($ff.Id) -> $video"
}
if ($Clicks) {
  Add-Type -Namespace Pstf -Name Mouse -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern bool GetCursorPos(out System.Drawing.Point p);
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, System.UIntPtr e);
'@ -ReferencedAssemblies System.Drawing
  [Pstf.Mouse]::SetProcessDPIAware() | Out-Null
  $sw, $sh = [Pstf.Mouse]::GetSystemMetrics(0), [Pstf.Mouse]::GetSystemMetrics(1)
  $seen = @(Select-String -Path $log -Pattern 'showcase_click: ').Count   # lines of earlier runs
  while ($job.State -eq 'Running') {
    $hits = @(Select-String -Path $log -Pattern 'showcase_click: (-?[0-9.]+),(-?[0-9.]+)')
    for ($i = $seen; $i -lt $hits.Count; $i++) {
      $tx = [int]($sw / 2 + [double]$hits[$i].Matches[0].Groups[1].Value)
      $ty = [int]($sh / 2 + [double]$hits[$i].Matches[0].Groups[2].Value)
      $p = New-Object System.Drawing.Point
      [Pstf.Mouse]::GetCursorPos([ref]$p) | Out-Null
      for ($k = 1; $k -le 30; $k++) {   # ~0.5 s glide, eased
        $e = 1 - [Math]::Pow(1 - $k / 30, 3)
        [Pstf.Mouse]::SetCursorPos([int]($p.X + ($tx - $p.X) * $e), [int]($p.Y + ($ty - $p.Y) * $e)) | Out-Null
        Start-Sleep -Milliseconds 16
      }
      Start-Sleep -Milliseconds 350
      [Pstf.Mouse]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)   # left down
      Start-Sleep -Milliseconds 60
      [Pstf.Mouse]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)   # left up
      Write-Host "record: click $tx,$ty"
    }
    $seen = $hits.Count
    Start-Sleep -Milliseconds 50
  }
}
$lines = Receive-Job $job -Wait
Remove-Job $job
if ($ff) {
  if (-not $ff.HasExited) { $ff.StandardInput.Write('q'); $ff.StandardInput.Close() }
  if (-not $ff.WaitForExit(30000)) { $ff.Kill() }
}
if ($au) {
  New-Item -ItemType File -Force $stop | Out-Null
  $aline = $au.StandardOutput.ReadLine()
  if (-not $au.WaitForExit(10000)) { $au.Kill() }
  Remove-Item $stop -ErrorAction SilentlyContinue
  $v0 = $null
  if (Test-Path $report) {
    $m = Select-String -Path $report -Pattern 'Parsed_showinfo.*pts_time:([0-9.]+)' | Select-Object -First 1
    if ($m) { $v0 = [double]$m.Matches[0].Groups[1].Value }
  }
  if ($v0) {
    # <stem>.events.txt: the run's lua notes and voice lines at their second in the video (for cutting)
    $first = [DateTimeOffset]::FromUnixTimeMilliseconds([long]($v0 * 1000)).LocalDateTime
    $ev = foreach ($l in (Get-Content $log)) {
      if ($l -match '^(\d\d:\d\d:\d\d\.\d{3}) (lua note: .*|voice: .*|tbc: .* turn.*)$') {
        $at = [DateTime]::ParseExact("$($first.ToString('yyyy-MM-dd')) $($Matches[1])", 'yyyy-MM-dd HH:mm:ss.fff', $null)
        $s = ($at - $first).TotalSeconds
        if ($s -ge -5) { '{0,8:F2}  {1}' -f $s, $Matches[2] }
      }
    }
    Set-Content -Encoding utf8 (Join-Path $dir "$stem.events.txt") $ev
  }
  if ($aline -match '^audio: start ([0-9.]+)' -and $v0 -and (Test-Path $video) -and (Test-Path $wav)) {
    $a0 = [double]$Matches[1]
    $off = $a0 - $v0   # < 0: the audio began before the first frame
    $ain = if ($off -lt 0) { "-ss {0:F4} -i `"$wav`"" -f (-$off) } else { "-itsoffset {0:F4} -i `"$wav`"" -f $off }
    $mux = "-hide_banner -loglevel error -y -i `"$video`" $ain -map 0:v -map 1:a -c:v copy " +
           "-af aresample=48000 -ac 2 -c:a aac -b:a 192k -shortest -movflags +faststart `"$Out`""
    $p = Start-Process -FilePath $ffExe -ArgumentList $mux -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -eq 0) {
      Write-Host ("record: audio offset {0:N0} ms" -f ($off * 1000))
      Remove-Item $video, $wav, $report -ErrorAction SilentlyContinue
    } else { Write-Host "record: mux failed, kept $video and $wav" }
  } else {
    Write-Host "record: no audio sync ($aline; first frame $v0), kept $video"
    if (Test-Path $video) { Move-Item -Force $video $Out }
  }
}
$code = 1
foreach ($l in $lines) { if ($l -match '^launch-exit (\d+)') { $code = [int]$Matches[1] } }
$lines | Where-Object { $_ -match 'launch:|TEST FAIL|error' } | ForEach-Object { Write-Host $_ }
if (Test-Path $Out) { Write-Host ("record: {0:N1} MB, launch exit {1}" -f ((Get-Item $Out).Length / 1MB), $code) }
else { Write-Host "record: no video" }
exit $code
