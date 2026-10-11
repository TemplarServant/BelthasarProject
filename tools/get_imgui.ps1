# Download Dear ImGui (MIT, github.com/ocornut/imgui) into third_party/imgui for ct_field's options overlay (F1).
# Only the core sources and the SDL2 / SDL_Renderer / OpenGL3 / Vulkan backends are kept. Git-ignored like SDL2.
# Without it ct_field still builds -- the overlay is compiled out (CT_HAVE_IMGUI undefined). Run once:
#   powershell -ExecutionPolicy Bypass -File tools\get_imgui.ps1
$ErrorActionPreference = 'Stop'
$ver  = '1.91.5'
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root 'third_party\imgui'
if (Test-Path (Join-Path $dest 'imgui.h')) { Write-Output "Dear ImGui already present at $dest"; exit 0 }
$work = Join-Path $env:TEMP "imguix_$(Get-Random)"          # a fresh, empty directory for the untrusted archive
New-Item -ItemType Directory -Force -Path $work | Out-Null
$zip = Join-Path $work "imgui-$ver.zip"
$url = "https://github.com/ocornut/imgui/archive/refs/tags/v$ver.zip"
Write-Output "Downloading $url"
curl.exe -sL $url -o $zip
Expand-Archive -Path $zip -DestinationPath $work -Force
$src = Join-Path $work "imgui-$ver"
if (-not (Test-Path (Join-Path $src 'imgui.h'))) { throw "unexpected archive layout in $work" }
New-Item -ItemType Directory -Force -Path (Join-Path $dest 'backends') | Out-Null
foreach ($f in 'imgui.cpp','imgui.h','imgui_demo.cpp','imgui_draw.cpp','imgui_internal.h','imgui_tables.cpp',
               'imgui_widgets.cpp','imconfig.h','imstb_rectpack.h','imstb_textedit.h','imstb_truetype.h','LICENSE.txt') {
  Copy-Item (Join-Path $src $f) (Join-Path $dest $f) -Force
}
foreach ($f in 'imgui_impl_sdl2.cpp','imgui_impl_sdl2.h','imgui_impl_sdlrenderer2.cpp','imgui_impl_sdlrenderer2.h',
               'imgui_impl_opengl3.cpp','imgui_impl_opengl3.h','imgui_impl_opengl3_loader.h',
               'imgui_impl_vulkan.cpp','imgui_impl_vulkan.h') {
  Copy-Item (Join-Path $src "backends\$f") (Join-Path $dest "backends\$f") -Force
}
Remove-Item -Recurse -Force $work
Write-Output "Dear ImGui $ver installed to $dest"
