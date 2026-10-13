#!/usr/bin/env python3
"""Package a local macOS app; copy the ROM/assets only when requested."""
import argparse
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"


def app_version(value):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", value):
        raise argparse.ArgumentTypeError("use three numeric components, e.g. 1.2.3")
    return value


def build_number(value):
    if not re.fullmatch(r"[1-9][0-9]*", value):
        raise argparse.ArgumentTypeError("use a positive integer, e.g. 2")
    return value


def source_revision():
    """Identify the packaging checkout; source archives may have no Git metadata."""
    try:
        checkout = subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "--show-toplevel"],
            text=True, stderr=subprocess.DEVNULL).strip()
        if Path(checkout).resolve() != ROOT:
            return None, None
        revision = subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
            text=True, stderr=subprocess.DEVNULL).strip()
        modified = bool(subprocess.check_output(
            ["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"],
            text=True, stderr=subprocess.DEVNULL).strip())
        return revision, modified
    except (OSError, subprocess.CalledProcessError):
        return None, None


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def dependencies(binary):
    return [line.strip().split(" (compatibility", 1)[0]
            for line in run("otool", "-L", binary).splitlines()[1:]]


def rpaths(binary):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset",
                      run("otool", "-l", binary))


def system_library(name):
    return name.startswith(("/System/Library/", "/usr/lib/"))


def resolve(name, loader):
    def expand(path):
        return Path(path.replace("@loader_path", str(loader.parent))
                    .replace("@executable_path", str(BUILD)))
    candidates = ([expand(p) / name[len("@rpath/"):]
                   for p in rpaths(loader) + rpaths(BUILD / "ct_field.exe")]
                  if name.startswith("@rpath/") else [expand(name)])
    if name.startswith("@rpath/"):
        candidates.append(loader.parent / name[len("@rpath/"):])
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(f"Cannot resolve {name} loaded by {loader}")


def install_app(stage, output):
    """Copy to the destination volume before replacing an existing app."""
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".chrono-trigger-", dir=output.parent))
    replacement = temporary / "next.app"
    backup = temporary / "previous.app"
    try:
        shutil.copytree(stage, replacement, symlinks=True)
        if output.exists():
            output.rename(backup)
        try:
            replacement.rename(output)
        except OSError as error:
            if backup.exists():
                try:
                    backup.rename(output)
                except OSError as restore_error:
                    raise RuntimeError(f"Previous app kept at {backup}: {restore_error}") from error
            raise
    finally:
        # If restoring fails, leave the previous app available for recovery.
        if not backup.exists() or output.exists():
            shutil.rmtree(temporary)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--with-game-data", action="store_true",
                    help="Include your local ROM/assets for personal use only")
    ap.add_argument("--identity", default="-", help="codesign identity; default: local ad-hoc")
    ap.add_argument("--version", type=app_version,
                    help="upstream source version (X.Y.Z); default: 0.0.0 development build")
    ap.add_argument("--build-number", type=build_number,
                    help="numeric bundle build number for local rebuilds; default: 1")
    ap.add_argument("--output", type=Path, default=BUILD / "macos" / "Chrono Trigger.app",
                    help="destination .app path")
    args = ap.parse_args()
    if platform.system() != "Darwin":
        ap.error("run this on macOS after tools/setup.py")
    output = args.output.resolve()
    staging_dir = (BUILD / "macos-staging").resolve()
    if (output.suffix != ".app" or output == staging_dir
            or staging_dir in output.parents):
        ap.error("choose a destination ending in .app outside build/macos-staging")
    if output.exists():
        existing = output / "Contents" / "Info.plist"
        if (not existing.is_file() or plistlib.loads(existing.read_bytes()).get("CFBundleIdentifier")
                != "io.github.TemplarServant.BelthasarProject"):
            ap.error("refusing to replace an unrelated existing app")
    engine = BUILD / "ct_field.exe"
    if not engine.is_file():
        ap.error("build the project first: python3 tools/build.py")

    # Stage separately: a failed package never overwrites a working app.
    stage = BUILD / "macos-staging" / "Chrono Trigger.app"
    if stage.parent.exists():
        shutil.rmtree(stage.parent)
    (stage / "Contents").mkdir(parents=True)
    shutil.copy2(ROOT / "platform" / "macos" / "Info.plist", stage / "Contents" / "Info.plist")
    macos = stage / "Contents" / "MacOS"
    frameworks = stage / "Contents" / "Frameworks"
    resources = stage / "Contents" / "Resources"
    macos.mkdir(exist_ok=True)
    frameworks.mkdir(exist_ok=True)
    resources.mkdir(exist_ok=True)
    shutil.copy2(engine, macos / "ChronoTrigger")

    libraries = {}  # original resolved file -> bundled file
    origins = {macos / "ChronoTrigger": engine}

    def collect(original, filename=None):
        original = original.resolve()
        if original in libraries:
            return libraries[original]
        destination = frameworks / (filename or original.name)
        if destination.exists():
            raise RuntimeError(f"Library filename collision: {destination.name}")
        shutil.copy2(original, destination)
        libraries[original] = destination
        origins[destination] = original
        for dep in dependencies(original):
            if system_library(dep):
                continue
            resolved = resolve(dep, original)
            if resolved != original:  # LC_ID_DYLIB is also listed by otool.
                collect(resolved)
        return destination

    for dep in dependencies(engine):
        if not system_library(dep):
            collect(resolve(dep, engine))

    # sdl2-compat loads SDL3 with dlopen, so it is absent from otool -L.
    if any(b"libSDL3.dylib" in p.read_bytes() for p in libraries):
        libdir = Path(run("pkg-config", "--variable=libdir", "sdl3"))
        collect(libdir / "libSDL3.dylib", "libSDL3.dylib")

    for binary, original in origins.items():
        binary.chmod(binary.stat().st_mode | 0o200)
        for dep in dependencies(original):
            if system_library(dep):
                continue
            resolved = resolve(dep, original)
            if resolved == original:
                continue
            bundled = libraries[resolved]
            prefix = "@loader_path/" if binary.parent == frameworks else "@executable_path/../Frameworks/"
            run("install_name_tool", "-change", dep, prefix + bundled.name, binary)
        if binary.parent == frameworks:
            run("install_name_tool", "-id", "@rpath/" + binary.name, binary)
        for path in rpaths(binary):
            if path.startswith("/") and not system_library(path):
                run("install_name_tool", "-delete_rpath", path, binary)
        unresolved = [d for d in dependencies(binary)
                      if not system_library(d) and not d.startswith(("@loader_path/", "@executable_path/", "@rpath/"))]
        if unresolved:
            raise RuntimeError(f"Unbundled dependency in {binary.name}: {unresolved}")

    licenses = resources / "Licenses"
    licenses.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "LICENSE", licenses / "BelthasarProject.txt")
    shutil.copy2(ROOT / "snes" / "LICENSE.LakeSnes.txt", licenses / "LakeSnes.txt")
    imgui_license = ROOT / "third_party" / "imgui" / "LICENSE.txt"
    if imgui_license.is_file():
        shutil.copy2(imgui_license, licenses / "DearImGui.txt")
    for original in libraries:
        # Homebrew and source installs commonly put LICENSE.txt at the prefix.
        for parent in original.parents:
            license_file = parent / "LICENSE.txt"
            if license_file.is_file():
                shutil.copy2(license_file, licenses / (original.stem + ".txt"))
                break
        else:
            raise RuntimeError(f"Missing SDL license for {original}; install its LICENSE.txt in its prefix")

    if args.with_game_data:
        data = resources / "GameData"
        data.mkdir()
        for filename in ("Chrono_Trigger_USA_SNES.sfc", "ct_assets.dat"):
            shutil.copy2(ROOT / filename, data / filename)
    # Do not advertise compatibility older than the compiled engine/libraries.
    versions = []
    for binary in list(origins):
        commands = run("otool", "-l", binary)
        match = re.search(r"\bminos ([\d.]+)", commands) or re.search(
            r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)", commands)
        if match:
            versions.append(tuple(int(v) for v in match.group(1).split(".")))
    plist_path = stage / "Contents" / "Info.plist"
    metadata = plistlib.loads(plist_path.read_bytes())
    if args.version is not None:
        metadata["CFBundleShortVersionString"] = args.version
    if args.build_number is not None:
        metadata["CFBundleVersion"] = args.build_number
    metadata["BelthasarBuildChannel"] = "versioned" if args.version is not None else "development"
    revision, modified = source_revision()
    if revision is not None:
        metadata["BelthasarSourceRevision"] = revision
        metadata["BelthasarSourceModified"] = modified
    if versions:
        metadata["LSMinimumSystemVersion"] = ".".join(str(v) for v in max(versions))
    plist_path.write_bytes(plistlib.dumps(metadata))
    sign_args = ["--timestamp=none"] if args.identity == "-" else ["--options", "runtime", "--timestamp"]
    for binary in list(libraries.values()) + [stage]:
        run("codesign", "--force", "--sign", args.identity, *sign_args, binary)
    run("codesign", "--verify", "--deep", "--strict", stage)

    install_app(stage, output)
    print(f"Packaged {output}")
    print(f"Version: {metadata['CFBundleShortVersionString']} "
          f"(build {metadata['CFBundleVersion']}, {metadata['BelthasarBuildChannel']})")
    print("Source: " + ((revision + (" (modified)" if modified else "")) if revision else "unknown"))
    print("Bundled ROM/assets: " + ("included (local use only)" if args.with_game_data else "excluded"))
    print("An engine compiled from ROM-derived data must remain local; see CONTRIBUTING.md.")
    print("Signing: " + ("local ad-hoc; not notarized" if args.identity == "-" else args.identity))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(f"macOS packaging failed: {error}")
