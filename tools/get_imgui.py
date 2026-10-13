#!/usr/bin/env python3
"""Fetch the pinned MIT-licensed options UI dependency on any platform."""
import io
from pathlib import Path
import urllib.request
import zipfile

VERSION = "1.91.5"  # Keep aligned with get_imgui.ps1.
ROOT = Path(__file__).resolve().parent.parent


def main():
    dest = ROOT / "third_party" / "imgui"
    url = f"https://github.com/ocornut/imgui/archive/refs/tags/v{VERSION}.zip"
    print(f"Fetching Dear ImGui {VERSION}: {url}", flush=True)
    with urllib.request.urlopen(url, timeout=60) as response:
        archive = zipfile.ZipFile(io.BytesIO(response.read()))
    prefix = f"imgui-{VERSION}/"
    backends = ("imgui_impl_sdl2", "imgui_impl_sdlrenderer2",
                "imgui_impl_opengl3", "imgui_impl_vulkan")
    for name in archive.namelist():
        if not name.startswith(prefix):
            continue
        relative = Path(name[len(prefix):])
        if len(relative.parts) == 1:
            keep = relative.suffix in (".cpp", ".h") or relative.name == "LICENSE.txt"
        else:
            keep = (len(relative.parts) == 2 and relative.parts[0] == "backends"
                    and relative.name.startswith(backends)
                    and relative.suffix in (".cpp", ".h"))
        if keep:
            output = dest / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(archive.read(name))
    print(f"Dear ImGui installed in {dest}")


if __name__ == "__main__":
    main()
