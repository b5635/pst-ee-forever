# SPDX-License-Identifier: GPL-2.0-or-later
# Reports what this machine has for developing pst_forever (docs\DEVELOPING.md). Read-only: changes nothing.
#   tools\setup_check.ps1 [-Game DIR]
# Exit 0 = everything needed to build and verify is there (the other groups are optional).
param(
  [string]$Game = 'C:\GOG Games\Planescape Torment - Enhanced Edition',
  [string]$Docs = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Planescape Torment - Enhanced Edition')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$winSha = '99ae15860cfdbcdaad0f48e005eee21444046b21de848c1deab9367e8246a3c5'
$linSha = 'e615763edb57427e3efded67296c357e2563ee40ebc2aef929e1f180e1b5846e'
$script:missing = 0

function Report($ok, $name, $hint, [switch]$Required) {
  if ($ok) { Write-Host "  ok       $name"; return }
  $tag = 'absent '
  if ($Required) { $tag = 'MISSING'; $script:missing++ }
  Write-Host "  $tag  $name -- $hint"
}
function Test-Sha($path, $sha) { (Test-Path $path) -and ((Get-FileHash $path -Algorithm SHA256).Hash -eq $sha) }
function Test-PyModule($name) {
  if (-not (Get-Command python -ErrorAction SilentlyContinue)) { return $false }
  $ErrorActionPreference = 'Continue'
  & python -I -c "import importlib.util,sys; sys.exit(0 if importlib.util.find_spec('$name') else 1)" *> $null
  return $LASTEXITCODE -eq 0
}

Write-Host 'Build and verify (required)'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = $null
if (Test-Path $vswhere) {
  $vs = & $vswhere -products * -version '[16.0,17.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
Report $vs 'Visual Studio 2019 C++ x64 tools' 'install VS 2019 Build Tools, workload "Desktop development with C++"' -Required
Report (Test-Path 'C:\Program Files\CMake\bin\cmake.exe') 'CMake in C:\Program Files\CMake' 'install CMake to its default folder (tools\check.ps1 looks there)' -Required
$exe = Join-Path $Game 'Torment.exe'
Report (Test-Path $exe) "game at $Game" 'install the GOG release, or pass -Game' -Required
if (Test-Path $exe) {
  Report (Test-Sha $exe $winSha) 'Torment.exe is GOG 3.2.2.0' 'only this exact build is supported' -Required
}

Write-Host 'In-game tests'
Report (Test-Path (Join-Path $Docs 'Baldur.lua')) 'Baldur.lua' 'start the game once (looked in Documents; see -Docs)'
$lua = Join-Path $Docs 'Baldur.lua'
$movies = (Test-Path $lua) -and (Select-String -Path $lua -Pattern "'Disable Movies'\s*,\s*'1'" -Quiet)
Report $movies "Baldur.lua 'Disable Movies' = 1" 'needed by tests that set PSTF_TEST_TICK=1'
foreach ($s in Get-ChildItem (Join-Path $root 'tests\saves') -Directory) {
  Report (Test-Path (Join-Path $Docs "save\$($s.Name)")) "test save $($s.Name)" 'run tools\install_test_saves.ps1'
}
Report (Test-Path (Join-Path $Docs 'save\000000126*')) 'Hive save 000000126*' 'not in the repo; see docs\DEVELOPING.md'
Report (Test-Path (Join-Path $Game 'pst_forever.dll')) 'mod deployed to the game' 'run tools\deploy.ps1'

Write-Host 'Python tools'
Report (Get-Command python -ErrorAction SilentlyContinue) 'python on PATH' 'install Python 3'
foreach ($m in @(@('pytest', 'pytest'), @('capstone', 'capstone'), @('PIL', 'Pillow'), @('numpy', 'numpy'), @('scipy', 'scipy'))) {
  Report (Test-PyModule $m[0]) $m[1] "python -m pip install $($m[1])"
}

Write-Host 'Reverse engineering (only for new hooks or offsets)'
Report (Test-Path 'C:\Program Files\IDA Professional 9.1\idat.exe') 'IDA Professional 9.1' 'commercial, with the x64 Hex-Rays decompiler'
Report (Test-Path (Join-Path $root 'resources\ext\diaphora\diaphora.py')) 'Diaphora' 'clone into resources\ext\diaphora'
Report (Test-Sha (Join-Path $root 'resources\re\win\Torment.exe') $winSha) 'resources\re\win\Torment.exe' 'copy it from your game folder'
Report (Test-Sha (Join-Path $root 'resources\re\lin\Torment') $linSha) 'resources\re\lin\Torment' 'copy the Linux binary of the same GOG release'
Report (Test-Path (Join-Path $root 'resources\re\index.sqlite')) 'dump index (tools\re.py)' 'run bash tools/re_pipeline.sh fresh'

if ($script:missing) { Write-Host "setup_check: $($script:missing) required item(s) missing"; exit 1 }
Write-Host 'setup_check: ready to build'
