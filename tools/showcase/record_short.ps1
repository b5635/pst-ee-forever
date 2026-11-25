# SPDX-License-Identifier: GPL-2.0-or-later
# Records the YouTube Short's clips, one MP4 per clip (build\rec\short\<name>.mp4), so a section can be redone alone:
#   tools\showcase\record_short.ps1                 all clips
#   tools\showcase\record_short.ps1 -Only voice,spells
# Game music is off while recording (Baldur.lua 'Volume Music' 0, put back afterwards): the Short has its own music.
# Then python -I tools/showcase/make_short.py cuts them into build\rec\short\short.mp4.
param(
  [string[]]$Only = @(),
  [int]$Width = 1920,
  [int]$Fps = 60,
  [string]$Game = 'C:\GOG Games\Planescape Torment - Enhanced Edition',
  [string]$Docs = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Planescape Torment - Enhanced Edition')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root 'build\rec\short'
New-Item -ItemType Directory -Force $out | Out-Null
$sc = 'tools\showcase'
$tmp = Join-Path $root 'build\tmp'
New-Item -ItemType Directory -Force $tmp | Out-Null

# Spiritual Hammer at night: lit, then the engine's own look (off), then lit again (the before/after).
Set-Content -Encoding utf8 (Join-Path $tmp 'short_hammer.lua') ("pstfHammerRuns = {{'night','lit'},{'night','off'},{'night','lit'}}`n" +
  (Get-Content -Raw (Join-Path $root 'tests\ingame\lighting_hammer.lua')))

$clips = [ordered]@{
  hammer = @{Save = '000000990'; Lua = 'build\tmp\short_hammer.lua'; Env = @{PSTF_TEST_TICK = '1'; PSTF_TBC_NOFIGHT = '1'}
             Until = 'lighting_hammer: ok|TEST FAIL'; Timeout = 300}
  spells = @{Save = '000000990'; Lua = "$sc\short_spells.lua"; Env = @{PSTF_TEST_TICK = '1'; PSTF_FEEDBACK_LOG = '1'}
             Until = 'short_spells: done|TEST FAIL'; Timeout = 400}
  moves  = @{Save = '000000126'; Lua = "$sc\short_moves.lua"; Env = @{PSTF_TEST_TICK = '1'}
             Until = 'short_moves: done|TEST FAIL'; Timeout = 400}
  blood  = @{Save = '000000126'; Lua = "$sc\showcase_blood.lua"; Env = @{PSTF_TEST_TICK = '1'}
             Until = 'blood: done'; Timeout = 200; Ini = @{start_radius = '0'; join_radius = '0'}}
  dice   = @{Save = '000000126'; Lua = 'tests\ingame\dlgroll.lua'; Env = @{PSTF_DLG_TEST = '1'; PSTF_TBC_NOFIGHT = '1'}
             Until = 'dlgroll: ok|TEST FAIL'; Timeout = 200}
  voice  = @{Save = '000000200'; Lua = "$sc\short_voice.lua"; Env = @{PSTF_DLG_TEST = '1'; PSTF_TBC_NOFIGHT = '1'}
             Until = 'short_voice: done|TEST FAIL'; Timeout = 240; Mouse = $true}
  party8 = @{Save = '000000200'; Lua = "$sc\showcase_party8.lua"; Env = @{PSTF_DLG_TEST = '1'; PSTF_TBC_NOFIGHT = '1'}
             Until = 'showcase_party8: done'; Timeout = 200}
}

$baldur = Join-Path $Docs 'Baldur.lua'
$ini = Join-Path $Game 'pst_forever\pst_forever.ini'
function Set-Value([string]$file, [string]$pattern, [string]$value) {
  # returns the old value; $pattern has one group before the value and one after
  $txt = [IO.File]::ReadAllText($file)
  $m = [regex]::Match($txt, $pattern, 'Multiline')
  if (-not $m.Success) { throw "no match for $pattern in $file" }
  $new = $txt.Substring(0, $m.Groups[2].Index) + $value + $txt.Substring($m.Groups[2].Index + $m.Groups[2].Length)
  [IO.File]::WriteAllText($file, $new)
  return $m.Groups[2].Value
}
$musicPat = "(SetPrivateProfileString\('Program Options','Volume Music',')(\d+)('\))"
$music = $null
$failed = @()
try {
  $music = Set-Value $baldur $musicPat '0'
  Write-Host "short: game music off (was $music)"
  foreach ($name in $clips.Keys) {
    if ($Only.Count -and $Only -notcontains $name) { continue }
    $c = $clips[$name]
    $old = @{}
    if ($c.Ini) { foreach ($k in $c.Ini.Keys) { $old[$k] = Set-Value $ini "^($k\s*=\s*)(\S+)()" $c.Ini[$k] } }
    try {
      $launch = @{Save = $c.Save; Lua = (Join-Path $root $c.Lua); Env = $c.Env; Until = $c.Until; Timeout = $c.Timeout}
      $extra = @{}
      if ($c.Mouse) { $extra = @{DrawMouse = $true; Clicks = $true} }
      Write-Host "short: recording $name"
      & (Join-Path $root 'tools\record.ps1') -Out (Join-Path $out "$name.mp4") -Width $Width -Fps $Fps -Launch $launch @extra
      if ($LASTEXITCODE -ne 0) { $failed += $name }
      # the music must stay off: the engine writes Baldur.lua back on exit
      Set-Value $baldur $musicPat '0' | Out-Null
    } finally {
      foreach ($k in $old.Keys) { Set-Value $ini "^($k\s*=\s*)(\S+)()" $old[$k] | Out-Null }
    }
  }
} finally {
  if ($null -ne $music) { Set-Value $baldur $musicPat $music | Out-Null; Write-Host "short: game music back to $music" }
}
if ($failed.Count) { Write-Host "short: FAILED $($failed -join ', ')"; exit 1 }
Write-Host "short: clips in $out"
