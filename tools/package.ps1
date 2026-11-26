# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the release zip: its layout mirrors the game folder, so a player only unzips it into
# "Planescape Torment - Enhanced Edition" (next to Torment.exe). Same files as tools\deploy.ps1.
#   version.dll, pst_forever.dll, pst_forever_config.exe
#   pst_forever\pst_forever.ini, sounds\, assets\{vfx,lighting,dice,gore}\
#   override\PF*.BAM / PF*.WAV / PORTRT*.BMP
# -Bin    folder with the built binaries (default build\Release; run tools\check.ps1 first)
# -Out    output folder (default build\package)
# -Version name in the zip file (default: git describe)
# -Assets optional pst_forever\assets folder whose lighting\ and sprites\ (GemRB maps, sprite depth;
#         not in git, ~1.4 GB) go into a separate pst_forever-<ver>-lighting.zip
# Used by .github/workflows/release.yml. Exit 0 = zip(s) written; prints their paths.
param(
  [string]$Bin = '',
  [string]$Out = '',
  [string]$Version = '',
  [string]$Assets = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Bin) { $Bin = Join-Path $root 'build\Release' }
if (-not $Out) { $Out = Join-Path $root 'build\package' }
if (-not $Version) {
  $Version = (& git -C $root describe --tags --always --dirty 2>$null)
  if (-not $Version) { $Version = 'dev' }
}
$Version = $Version.Trim()

$stage = Join-Path $Out 'stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force $stage | Out-Null

foreach ($f in 'version.dll', 'pst_forever.dll', 'pst_forever_config.exe') {
  $p = Join-Path $Bin $f
  if (-not (Test-Path $p)) { Write-Host "package: missing $p (build first: tools\check.ps1)"; exit 1 }
  Copy-Item $p $stage
}

function Put($src, $dstDir) {
  $s = Join-Path $root $src
  if (-not (Test-Path $s)) { Write-Host "package: missing $src"; exit 1 }
  $d = Join-Path $stage $dstDir
  New-Item -ItemType Directory -Force $d | Out-Null
  Copy-Item $s $d
}
Put 'data\pst_forever.ini' 'pst_forever'
Put 'data\vfxlight.tsv'    'pst_forever\assets\vfx'
Put 'data\vfxglow.tsv'     'pst_forever\assets\vfx'
Put 'data\arealights.tsv'  'pst_forever\assets\lighting'
Put 'data\d20_numbers.bin' 'pst_forever\assets\dice'
Put 'data\hitmat.2da'      'pst_forever\assets\gore'
Put 'data\hitarmor.tsv'    'pst_forever\assets\gore'
Put 'data\statusfx_atlas.bin' 'pst_forever\assets\statusfx'
$sounds = Join-Path $stage 'pst_forever\sounds'
New-Item -ItemType Directory -Force $sounds | Out-Null
Get-ChildItem (Join-Path $root 'sounds') -Filter *.wav | ForEach-Object { Copy-Item $_.FullName $sounds }
$ovr = Join-Path $stage 'override'
New-Item -ItemType Directory -Force $ovr | Out-Null
Get-ChildItem (Join-Path $root 'data\override') -File | ForEach-Object { Copy-Item $_.FullName $ovr }
Copy-Item (Join-Path $root 'COPYING') (Join-Path $stage 'pst_forever\COPYING')
@"
pst_forever $Version for Planescape: Torment: Enhanced Edition (GOG 3.2.2.0, 64-bit)

Install: unzip everything into the game folder, the one with Torment.exe
(e.g. C:\GOG Games\Planescape Torment - Enhanced Edition). Start the game as usual.
Options: run pst_forever_config.exe in the game folder.
Updating: unzip over the old version. pst_forever\pst_forever.ini is replaced with the
defaults; keep a copy first if you changed settings, or re-set them in pst_forever_config.exe.
Remove: delete version.dll, pst_forever.dll, pst_forever_config.exe, the pst_forever folder
and override\PF*.* (and PORTRT6.BMP, PORTRT7.BMP) from the game folder.
Log: pst_forever\logs\pst_forever.log.
"@ | Set-Content -Encoding utf8 (Join-Path $stage 'pst_forever\README.txt')

# Entries written one by one with "/" names: PS 5.1's Compress-Archive and .NET Framework's
# ZipFile.CreateFromDirectory both write "\" (breaks other unzippers); Compress-Archive also fails on big archives.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
function Write-Zip($dir, $zipPath) {
  $base = (Resolve-Path $dir).Path.TrimEnd('\') + '\'
  $z = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
  try {
    Get-ChildItem $dir -Recurse -File | Sort-Object FullName | ForEach-Object {
      $name = $_.FullName.Substring($base.Length).Replace('\', '/')
      [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, $_.FullName, $name,
        [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
  } finally { $z.Dispose() }
}
$zip = Join-Path $Out "pst_forever-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force -Confirm:$false }
Write-Zip $stage $zip
Write-Host "package: $zip"

if ($Assets) {
  $lstage = Join-Path $Out 'stage_lighting'
  if (Test-Path $lstage) { Remove-Item $lstage -Recurse -Force -Confirm:$false }
  $dst = Join-Path $lstage 'pst_forever\assets'
  New-Item -ItemType Directory -Force $dst | Out-Null
  foreach ($d in 'lighting', 'sprites') {
    $s = Join-Path $Assets $d
    if (-not (Test-Path $s)) { Write-Host "package: missing $s"; exit 1 }
    Copy-Item $s $dst -Recurse
  }
  $lzip = Join-Path $Out "pst_forever-$Version-lighting.zip"
  if (Test-Path $lzip) { Remove-Item $lzip -Force -Confirm:$false }
  Write-Zip $lstage $lzip
  Write-Host "package: $lzip"
}
exit 0
