#!/usr/bin/env python3
"""Fetch the pinned MIT-licensed options UI dependency on any platform.

The release zip is fetched first; where it cannot be (a cloud session's proxy refuses github.com's archive links),
a shallow `git clone` of the same tag gives the same files.
"""
import io
from pathlib import Path
import subprocess
import tempfile
import urllib.error
import urllib.request
import zipfile

VERSION = "1.91.5"  # Keep aligned with get_imgui.ps1.
ROOT = Path(__file__).resolve().parent.parent
REPO = "https://github.com/ocornut/imgui"
BACKENDS = ("imgui_impl_sdl2", "imgui_impl_sdlrenderer2",
            "imgui_impl_opengl3", "imgui_impl_vulkan")


def keep(relative):
    if len(relative.parts) == 1:
        return relative.suffix in (".cpp", ".h") or relative.name == "LICENSE.txt"
    return (len(relative.parts) == 2 and relative.parts[0] == "backends"
            and relative.name.startswith(BACKENDS)
            and relative.suffix in (".cpp", ".h"))


def from_zip():
    """(relative path, bytes) of every file in the tag's archive."""
    url = f"{REPO}/archive/refs/tags/v{VERSION}.zip"
    print(f"Fetching Dear ImGui {VERSION}: {url}", flush=True)
    with urllib.request.urlopen(url, timeout=60) as response:
        archive = zipfile.ZipFile(io.BytesIO(response.read()))
    prefix = f"imgui-{VERSION}/"
    return [(Path(n[len(prefix):]), archive.read(n)) for n in archive.namelist()
            if n.startswith(prefix) and not n.endswith("/")]


def from_git():
    """(relative path, bytes) of every file in a shallow clone of the tag."""
    print(f"Fetching Dear ImGui {VERSION} with git: {REPO}.git", flush=True)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["git", "clone", "-q", "--depth", "1", "-c", "advice.detachedHead=false",
                        "--branch", f"v{VERSION}", f"{REPO}.git", tmp], check=True)
        base = Path(tmp)
        return [(p.relative_to(base), p.read_bytes()) for p in base.rglob("*")
                if p.is_file() and ".git" not in p.relative_to(base).parts]


def main():
    dest = ROOT / "third_party" / "imgui"
    try:
        files = from_zip()
    except (urllib.error.URLError, OSError) as e:
        print(f"The archive could not be fetched ({e}); trying git", flush=True)
        files = from_git()
    for relative, data in files:
        if keep(relative):
            output = dest / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
    print(f"Dear ImGui installed in {dest}")


if __name__ == "__main__":
    main()
