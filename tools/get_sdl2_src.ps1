# Download the SDL2 source (zlib licence, github.com/libsdl-org/SDL) into third_party/SDL2-src for the Android build
# (docs/ANDROID.md): the APK builds libSDL2.so from it with the NDK, and takes SDL's Java glue (org.libsdl.app) from it.
# The same version as the Windows dev libraries in third_party/SDL2 (tools/get_sdl2.ps1). Git-ignored. Run once:
#   powershell -ExecutionPolicy Bypass -File tools\get_sdl2_src.ps1
$ErrorActionPreference = 'Stop'
$ver  = '2.32.10'
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root 'third_party\SDL2-src'
if (Test-Path (Join-Path $dest 'include\SDL.h')) { Write-Output "SDL2 source already present at $dest"; exit 0 }
$work = Join-Path $env:TEMP "sdl2src_$(Get-Random)"         # a fresh, empty directory for the untrusted archive
New-Item -ItemType Directory -Force -Path $work | Out-Null
$zip = Join-Path $work "SDL2-$ver.zip"
$url = "https://github.com/libsdl-org/SDL/releases/download/release-$ver/SDL2-$ver.zip"
Write-Output "Downloading $url"
curl.exe -sL $url -o $zip
Expand-Archive -Path $zip -DestinationPath $work -Force
$src = Join-Path $work "SDL2-$ver"
if (-not (Test-Path (Join-Path $src 'include\SDL.h')) -or
    -not (Test-Path (Join-Path $src 'android-project\app\src\main\java\org\libsdl\app\SDLActivity.java'))) {
  throw "unexpected archive layout in $work"
}
Move-Item $src $dest
Remove-Item -Recurse -Force $work
Write-Output "SDL2 $ver source installed to $dest"
