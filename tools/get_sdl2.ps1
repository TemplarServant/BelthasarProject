# Download the SDL2 (VC) dev libs into third_party/SDL2 so CMake builds ct_viewer.
# The headless 'ct' target does not need this; only the interactive SDL viewer does.
# SDL2 is git-ignored (third-party binaries / not distributed). Run once:
#   powershell -ExecutionPolicy Bypass -File tools\get_sdl2.ps1
$ErrorActionPreference = 'Stop'
$ver  = '2.32.10'
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root 'third_party\SDL2'
if (Test-Path (Join-Path $dest 'include\SDL.h')) { Write-Output "SDL2 already present at $dest"; exit 0 }
$zip = Join-Path $env:TEMP "SDL2-devel-$ver-VC.zip"
if (-not (Test-Path $zip)) {
  $url = "https://github.com/libsdl-org/SDL/releases/download/release-$ver/SDL2-devel-$ver-VC.zip"
  Write-Output "Downloading $url"
  curl.exe -sL $url -o $zip
}
$tmp = Join-Path $env:TEMP "sdl2x_$(Get-Random)"
Expand-Archive -Path $zip -DestinationPath $tmp -Force
$src = (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item (Join-Path $src 'include') (Join-Path $dest 'include') -Recurse -Force
Copy-Item (Join-Path $src 'lib')     (Join-Path $dest 'lib')     -Recurse -Force
Copy-Item (Join-Path $src 'lib\x64\SDL2.dll') (Join-Path $dest 'SDL2.dll') -Force
Write-Output "SDL2 $ver installed to $dest"
