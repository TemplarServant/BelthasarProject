#!/usr/bin/env python3
"""Honest build driver for the Chrono Trigger decomp (stdlib only).

Replaces the logic that used to live in tools/build.bat (which is now a thin
wrapper around this script). Same flow: VS 2022 BuildTools vcvars64 ->
cmake -S . -B build -G Ninja (RelWithDebInfo; CT_BUILD_TYPE overrides) -> cmake --build. On top of that it
kills the three historical FALSE-'Build OK' mechanisms:

  1. LNK1168 on a locked exe leaving the OLD exe runnable while the failure
     scrolls away  -> pre-flight FAILS if a ct*.exe from build/ is RUNNING,
     and every failure ends in a loud '=== BUILD FAILED ===' banner.
  2. The old banner named only ct.exe and printed even when the SDL2 targets
     were silently skipped or nothing was scheduled  -> per-target status
     table (BUILT / UP-TO-DATE / SKIPPED / FAILED); 'Build OK:' prints ONLY
     when every expected target verified.
  3. mtime/clock-skew: a source whose mtime did not advance past its .obj is
     silently not recompiled  -> after the build, per-target source-content
     hashes (sha1) are compared against build/.ctbuildstamp.json; a changed
     source whose exe hash did NOT change is the skew trap: auto-heal by
     os.utime(now) on the changed sources, rebuild once, re-verify, and FAIL
     loudly ('STALE: ...') if the exe still does not change.

Usage:  python tools/build.py [ct|ct_field|ct_world|ct_viewer]
        (no argument = build all configured targets, like before)

Read-only with respect to the engine: it reads CMakeLists.txt to learn the
true per-target source lists (including the file(GLOB) of snes/*.c for
ct.exe); it never edits any source content.
"""
import glob as globmod
import hashlib
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BUILD = os.path.join(ROOT, "build")
STAMP_PATH = os.path.join(BUILD, ".ctbuildstamp.json")
VCENV_BAT = os.path.join(BUILD, ".ctvcenv.bat")

VS_INSTALLER_DIR = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer"
VS_HARDCODED = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
VCVARS_REL = r"VC\Auxiliary\Build\vcvars64.bat"
CMAKE_REL = r"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
NINJA_REL = r"Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

KNOWN_TARGETS = ("ct", "ct_field", "ct_world", "ct_viewer")
SDL_H = os.path.join(ROOT, "third_party", "SDL2", "include", "SDL.h")


def fail(reason, code=1):
    print("", flush=True)
    print("=== BUILD FAILED ===", flush=True)
    print(reason, flush=True)
    print("NOTE: any exes already in build\\ are the OLD binaries - do NOT test with them.", flush=True)
    sys.exit(code)


def sha1_file(path):
    """sha1 of file content, or None if the file does not exist."""
    try:
        h = hashlib.sha1()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


def rel(path):
    return os.path.relpath(path, ROOT).replace("\\", "/")


# ---------------------------------------------------------------- CMakeLists
def parse_cmakelists():
    """Read-only parse of CMakeLists.txt -> [{name, sources[abs], sdl}]."""
    path = os.path.join(ROOT, "CMakeLists.txt")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    code = re.sub(r"#[^\n]*", "", text)  # strip comments

    # file(GLOB VAR [CONFIGURE_DEPENDS] "pattern")
    globs = {}
    for m in re.finditer(r'file\s*\(\s*GLOB\s+(\w+)\s+(?:CONFIGURE_DEPENDS\s+)?"([^"]+)"\s*\)', code):
        var, pat = m.group(1), m.group(2)
        pat = pat.replace("${CMAKE_CURRENT_SOURCE_DIR}", ROOT.replace("\\", "/"))
        globs[var] = sorted(globmod.glob(pat))

    # The SDL2 presence gate: targets declared inside it only exist when SDL.h does.
    span = None
    gate = re.search(r"if\s*\(\s*EXISTS\s+\$\{SDL2_DIR\}/include/SDL\.h\s*\)", code)
    if gate:
        endm = re.search(r"\b(?:else|endif)\s*\(", code[gate.end():])
        end = gate.end() + endm.start() if endm else len(code)
        span = (gate.start(), end)

    targets = []
    for m in re.finditer(r"add_executable\s*\(\s*(\w+)\s+([^)]+)\)", code):
        name, body = m.group(1), m.group(2)
        srcs = []
        for tok in body.split():
            vm = re.fullmatch(r"\$\{(\w+)\}", tok)
            if vm:
                srcs.extend(globs.get(vm.group(1), []))
            else:
                srcs.append(os.path.join(ROOT, tok.replace("/", os.sep)))
        sdl = bool(span and span[0] <= m.start() < span[1])
        targets.append({"name": name, "sources": [os.path.normpath(s) for s in srcs], "sdl": sdl})
    if not targets:
        fail("could not parse any add_executable() out of CMakeLists.txt")
    return targets


# ------------------------------------------------------------------ pre-flight
def running_ct_processes():
    """[(pid, path)] of ct*.exe processes whose image lives in this repo's build/.
    Returns None if the check itself could not run."""
    ps = ("Get-Process -Name " + ",".join(KNOWN_TARGETS) +
          " -ErrorAction SilentlyContinue | ForEach-Object { \"$($_.Id)|$($_.Path)\" }")
    try:
        p = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                           capture_output=True, text=True, errors="replace", timeout=60)
    except Exception:
        return None
    hits = []
    prefix = os.path.normcase(BUILD) + os.sep
    for line in p.stdout.splitlines():
        pid, _, path = line.partition("|")
        path = path.strip()
        if path and os.path.normcase(os.path.normpath(path)).startswith(prefix):
            hits.append((pid.strip(), path))
    return hits


# ------------------------------------------------------------------- toolchain
def find_vs():
    """VS installation root: vswhere if available, else the hardcoded path."""
    vswhere = os.path.join(VS_INSTALLER_DIR, "vswhere.exe")
    if os.path.isfile(vswhere):
        try:
            p = subprocess.run(
                [vswhere, "-latest", "-products", "*",
                 "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                 "-property", "installationPath"],
                capture_output=True, text=True, errors="replace", timeout=60)
            for cand in p.stdout.splitlines():
                cand = cand.strip()
                if cand and os.path.isfile(os.path.join(cand, VCVARS_REL)):
                    return cand
        except Exception:
            pass
    if os.path.isfile(os.path.join(VS_HARDCODED, VCVARS_REL)):
        return VS_HARDCODED
    fail("could not locate VS 2022 Build Tools (vswhere found nothing and the "
         "fallback path is gone): " + VS_HARDCODED)


def vcvars_env(vs_root):
    """Environment dict after vcvars64.bat (with the VS Installer dir prepended
    to PATH first, so vcvars' internal vswhere call stops printing
    \"'vswhere.exe' is not recognized\", in the shell's Portuguese locale)."""
    vcvars = os.path.join(vs_root, VCVARS_REL)
    os.makedirs(BUILD, exist_ok=True)
    with open(VCENV_BAT, "w", encoding="ascii", errors="replace") as f:
        f.write("@echo off\r\n")
        f.write('set "PATH={};%PATH%"\r\n'.format(VS_INSTALLER_DIR))
        f.write('call "{}" >nul\r\n'.format(vcvars))
        f.write("if errorlevel 1 exit /b 1\r\n")
        f.write("set\r\n")
    p = subprocess.run(["cmd", "/d", "/c", VCENV_BAT],
                       capture_output=True, text=True, errors="replace", timeout=300)
    env = {}
    for line in p.stdout.splitlines():
        k, sep, v = line.partition("=")
        if sep and k:
            env[k] = v
    lower = {k.lower() for k in env}
    if p.returncode != 0 or ("vctoolsinstalldir" not in lower and "include" not in lower):
        sys.stderr.write(p.stderr or "")
        fail("vcvars64.bat did not produce an MSVC environment ({})".format(vcvars))
    return env


def find_tool(vs_root, env, rel_path, name):
    cand = os.path.join(vs_root, rel_path)
    if os.path.isfile(cand):
        return cand
    for d in env.get("PATH", env.get("Path", "")).split(os.pathsep):
        cand = os.path.join(d, name)
        if os.path.isfile(cand):
            return cand
    fail("could not find {} (looked in the VS layout and on the vcvars PATH)".format(name))


def run_step(cmd, env, label):
    print(">> {}".format(label), flush=True)
    try:
        return subprocess.run(cmd, env=env, cwd=ROOT).returncode
    except OSError as e:
        print("failed to launch {}: {}".format(cmd[0], e), flush=True)
        return 127


# ----------------------------------------------------------------------- stamp
def load_stamp():
    try:
        with open(STAMP_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data.get("targets", {}) if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_stamp(entries):
    tmp = STAMP_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"targets": entries}, f, indent=1, sort_keys=True)
    os.replace(tmp, STAMP_PATH)


def hash_sources(target):
    out = {}
    for s in target["sources"]:
        h = sha1_file(s)
        if h is None:
            print("WARNING: source listed in CMakeLists.txt not found: {}".format(rel(s)), flush=True)
        else:
            out[rel(s)] = h
    return out


# ------------------------------------------------------------------------ main
def main(argv):
    want = None
    args = [a for a in argv if a not in ("-h", "--help")]
    if len(args) != len(argv):
        print(__doc__)
        return 0
    if len(args) > 1:
        fail("usage: build.py [{}]".format("|".join(KNOWN_TARGETS)), 2)
    if args:
        want = args[0]
        if want not in KNOWN_TARGETS:
            fail("unknown target '{}' (know: {})".format(want, ", ".join(KNOWN_TARGETS)), 2)

    targets = parse_cmakelists()
    byname = {t["name"]: t for t in targets}
    sdl_present = os.path.isfile(SDL_H)

    if want is not None and want not in byname:
        fail("target '{}' is not declared in CMakeLists.txt".format(want), 2)
    if want is not None and byname[want]["sdl"] and not sdl_present:
        fail("target '{}' needs SDL2 but third_party/SDL2/include/SDL.h is missing "
             "- run tools/get_sdl2.ps1 first".format(want), 2)

    if want is not None:
        requested = [want]
    else:
        requested = [t["name"] for t in targets if (not t["sdl"]) or sdl_present]

    # ---- pre-flight: refuse to build over a RUNNING exe (the LNK1168 class).
    procs = running_ct_processes()
    if procs is None:
        print("WARNING: could not check for running ct*.exe processes (powershell "
              "unavailable?) - a locked exe will still fail the link loudly.", flush=True)
    elif procs:
        for pid, path in procs:
            print("RUNNING: PID {} -> {}".format(pid, path), flush=True)
        fail("{} process(es) from this repo's build\\ are RUNNING (listed above). "
             "Close them and re-run; refusing to link over a locked exe. "
             "(Nothing was killed.)".format(len(procs)), 2)

    exe_path = {t["name"]: os.path.join(BUILD, t["name"] + ".exe") for t in targets}
    pre_hash = {n: sha1_file(p) for n, p in exe_path.items()}
    stamp = load_stamp()

    # ---- toolchain + the exact same configure/build flow as before.
    vs_root = find_vs()
    print("VS: {}".format(vs_root), flush=True)
    env = vcvars_env(vs_root)
    cmake = find_tool(vs_root, env, CMAKE_REL, "cmake.exe")
    ninja = find_tool(vs_root, env, NINJA_REL, "ninja.exe")

    # BUILD TYPE. The tree built Debug (/Od) for years, which is why a suite of emulator runs took
    # minutes: every check is an oracle or engine run and none of it was optimised. RelWithDebInfo
    # (/O2 /Zi) keeps full symbols and line info for a crash while letting the ROM/engine loops run at
    # speed. CT_BUILD_TYPE overrides (Debug for a stepping session); the configure is re-run when the
    # type changes, since the cache remembers it.
    btype = os.environ.get("CT_BUILD_TYPE", "RelWithDebInfo")
    cfg = [cmake, "-S", ROOT, "-B", BUILD, "-G", "Ninja",
           "-DCMAKE_MAKE_PROGRAM=" + ninja, "-DCMAKE_BUILD_TYPE=" + btype]
    if run_step(cfg, env, "cmake configure (Ninja, " + btype + ")") != 0:
        fail("cmake configure step failed (see errors above)")

    bld = [cmake, "--build", BUILD]
    if want is not None:
        bld += ["--target", want]
    label = "cmake --build" + (" --target " + want if want else " (all targets)")
    if run_step(bld, env, label) != 0:
        fail("cmake --build failed (see the compiler/linker errors above; a "
             "LNK1168 means an exe in build\\ is open in another process)")

    # ---- freshness verification (kills the mtime/clock-skew false OK).
    status = {}     # name -> (STATUS, detail)
    cur_src = {}    # name -> {relpath: sha1}
    skewed = {}     # name -> [changed relpaths]
    for n in requested:
        cur_src[n] = hash_sources(byname[n])
        post = sha1_file(exe_path[n])
        if post is None:
            status[n] = ("FAILED", "exe missing after a 'successful' build")
            continue
        st = stamp.get(n)
        if st:
            changed = [p for p, h in cur_src[n].items() if st.get("sources", {}).get(p) != h]
            if changed and post == st.get("exe"):
                skewed[n] = changed   # edit on disk, exe provably the old build

    if skewed:
        print("", flush=True)
        for n, files in skewed.items():
            print("CLOCK-SKEW TRAP on {}: source content changed since the last certified "
                  "build but the exe was NOT relinked (ninja saw a stale mtime):".format(n), flush=True)
            for p in files:
                print("    changed: {}".format(p), flush=True)
        heal = sorted({p for files in skewed.values() for p in files})
        print("auto-heal: bumping mtime to now on {} file(s) and rebuilding once...".format(len(heal)), flush=True)
        now = time.time()
        for p in heal:
            os.utime(os.path.join(ROOT, p.replace("/", os.sep)), (now, now))
        if run_step(bld, env, label + " [skew re-run]") != 0:
            fail("re-build after the clock-skew auto-heal failed (see errors above)")
        for n, files in skewed.items():
            post = sha1_file(exe_path[n])
            if post == stamp.get(n, {}).get("exe"):
                status[n] = ("STALE", files)
            # else: healed; final status decided below from pre/post hashes

    # ---- final statuses + stamp update for every target that verified.
    for n in requested:
        if n in status:
            continue
        post = sha1_file(exe_path[n])
        status[n] = ("BUILT", None) if post != pre_hash[n] else ("UP-TO-DATE", None)
        stamp[n] = {"exe": post, "sources": cur_src[n],
                    "when": time.strftime("%Y-%m-%d %H:%M:%S")}
    save_stamp(stamp)

    # ---- honest per-target banner.
    print("", flush=True)
    print("--- build status " + "-" * 45, flush=True)
    ok = True
    for t in targets:
        n = t["name"]
        if n in status:
            s, detail = status[n]
            if s == "BUILT":
                line = "BUILT        ({} updated)".format(rel(exe_path[n]))
            elif s == "UP-TO-DATE":
                line = "UP-TO-DATE"
            elif s == "STALE":
                ok = False
                line = "STALE        (exe did NOT change after rebuilding)"
            else:
                ok = False
                line = "FAILED       ({})".format(detail)
        elif t["sdl"] and not sdl_present:
            line = "SKIPPED      (SDL2 missing: third_party/SDL2 not found - run tools/get_sdl2.ps1)"
        else:
            line = "not requested"
        print("  {:<11} {}".format(n, line), flush=True)
    print("-" * 62, flush=True)

    if not ok:
        for n, (s, detail) in status.items():
            if s == "STALE":
                for p in detail:
                    print("STALE: {} does not contain your edit to {}".format(rel(exe_path[n]), p), flush=True)
        fail("one or more targets failed verification (see the table above)")

    print("Build OK: {}".format(BUILD), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
