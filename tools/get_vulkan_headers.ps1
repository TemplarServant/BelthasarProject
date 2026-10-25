# Download the Khronos Vulkan headers (github.com/KhronosGroup/Vulkan-Headers, Apache-2.0 / MIT) into
# third_party/vulkan for Belthasar's Vulkan backend (src/present_vk.c). Only include/ is kept; git-ignored like SDL2.
# No SDK is needed: the functions are loaded at run time from the driver's vulkan-1 through SDL. Without the headers
# Belthasar builds without the Vulkan backend (choosing it falls back to SDL_Renderer). Run once:
#   powershell -ExecutionPolicy Bypass -File tools\get_vulkan_headers.ps1
$ErrorActionPreference = 'Stop'
$ver  = '1.3.296'
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root 'third_party\vulkan'
if (Test-Path (Join-Path $dest 'include\vulkan\vulkan.h')) { Write-Output "Vulkan headers already present at $dest"; exit 0 }
$work = Join-Path $env:TEMP "vkhx_$(Get-Random)"            # a fresh, empty directory for the untrusted archive
New-Item -ItemType Directory -Force -Path $work | Out-Null
$zip = Join-Path $work "Vulkan-Headers-$ver.zip"
$url = "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v$ver.zip"
Write-Output "Downloading $url"
curl.exe -sL $url -o $zip
Expand-Archive -Path $zip -DestinationPath $work -Force
$src = Join-Path $work "Vulkan-Headers-$ver"
if (-not (Test-Path (Join-Path $src 'include\vulkan\vulkan.h'))) { throw "unexpected archive layout in $work" }
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item (Join-Path $src 'include') (Join-Path $dest 'include') -Recurse -Force
Copy-Item (Join-Path $src 'LICENSE.md') (Join-Path $dest 'LICENSE.md') -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $work
Write-Output "Vulkan headers $ver installed to $dest"
