#!/usr/bin/env python3
r"""Build a player's release of Belthasar from the public tree: no byte of the game inside, checked.

    python tools/release/package.py [--out build/release] [--rom ROM] [--python DIR | --no-python] [--keep]
                                    [--version V] [--no-scan] [--android]

What a player downloads (docs/RELEASE.md) is a folder -- zipped on Windows, a .tar.gz on Linux:

    Belthasar-<version>-<os>-<arch>/
        Belthasar[.exe]           the game, built data-free (CT_DATAFREE=1: no generated table compiled in)
        SDL2.dll                  (Windows) the SDL2 runtime the build links
        setup/ct[.exe]            the oracle the setup runs, built from the shapes (tools/gen/shapes.py: no data)
        setup/kit/                the public tree itself (git archive HEAD): tools/setup.py and what it runs
        setup/python/             a Python runtime (the one running this script, pruned), unless --no-python
        LICENSE, CREDITS.md, README.txt
        ct_mapedits.dat           the owner's painted world-map cells (the scene adjustments mod reads them)

The player opens Belthasar, chooses their own dump on its setup screen (src/launcher_ui.cpp), and the setup makes
ct_assets.dat (the tables inside) in their data folder; from then on the game reads only that (docs/ROM.md, the ROM-free
runtime).

The steps, each checked:
  1. the tree: `git archive HEAD` into <out>/tree (the tracked files: what the public repository holds), plus the
     third-party libraries the build takes from third_party/ (git-ignored: SDL2, Dear ImGui, the Vulkan headers);
  2. the shapes: `tools/gen/shapes.py install` puts the generated files' shapes (declarations, every array zero) where
     src/data/*.h and src/xlat/*.c would be;
  3. the build: `CT_DATAFREE=1 tools/build.py ct` and `... Belthasar` in that tree (its own build/);
  4. the leak scan: tools/release/scan_rom_leaks.py --binaries over the release folder against the ROM (and, when
     present, ct_assets.dat and the developer's src/data tables), so no executable, library or file carries the game;
  5. the archive.
The version is `git describe --tags --always --dirty`, or --version's.

--android packages the Android app instead (docs/ANDROID.md): steps 1-2 as above (third_party/SDL2-src too), then the
APK built by Gradle in that tree (assembleRelease: always data-free, android/CMakeLists.cmake; signed when the release
key is configured, android/app/build.gradle), copied to <out>/Belthasar-<version>-android.apk and scanned unpacked
(libSDL2.so listed as an upstream runtime).

--no-scan skips step 4's scan (a CI runner has no ROM): the package is not checked until someone with the ROM runs
`python tools/release/scan_rom_leaks.py <the unpacked package> --rom ROM --binaries --corpus ct_assets.dat ...` on it
(docs/RELEASE.md, "The release workflow").
"""
import argparse
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WIN = os.name == 'nt'
EXE = '.exe' if WIN else ''
THIRD = ('SDL2', 'SDL2-src', 'imgui', 'vulkan')           # what CMakeLists.txt takes from third_party/
# A bundled Python: the standard library the setup needs, without what it never imports.
PY_SKIP = {'test', 'tests', 'idlelib', 'tkinter', 'turtledemo', 'ensurepip', 'site-packages', '__pycache__',
           'lib2to3', 'pydoc_data', 'unittest', 'venv', 'distutils', 'msilib', 'curses', 'dbm', 'sqlite3', 'xmlrpc'}
PY_SKIP_FILES = ('_tkinter.pyd', 'tcl86t.dll', 'tk86t.dll', '_sqlite3.pyd', 'sqlite3.dll', '_testcapi.pyd',
                 '_testinternalcapi.pyd', '_testbuffer.pyd', '_testimportmultiple.pyd', '_testmultiphase.pyd',
                 '_ctypes_test.pyd', 'pythonw.exe')


def say(*a):
    print('package:', *a, flush=True)


def run(cmd, cwd, env=None, quiet=False):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=quiet, text=True)
    if p.returncode:
        if quiet:
            sys.stdout.write((p.stdout or '')[-4000:] + (p.stderr or '')[-4000:])
        raise SystemExit('package: FAILED: %s (exit %d)' % (' '.join(cmd), p.returncode))
    return p


def version():
    p = subprocess.run(['git', 'describe', '--tags', '--always', '--dirty'], cwd=ROOT, capture_output=True, text=True)
    return (p.stdout.strip() or 'dev').replace('/', '-')


def archive_tree(dst):
    """git archive HEAD -> dst (the tracked files, as the public repository holds them)."""
    os.makedirs(dst)
    tar = os.path.join(os.path.dirname(dst), 'tree.tar')
    run(['git', 'archive', '--format=tar', '-o', tar, 'HEAD'], ROOT)
    with tarfile.open(tar) as t:
        t.extractall(dst)
    os.remove(tar)


def copy_python(src, dst):
    """The Python installation at `src` without the parts the setup never imports (PY_SKIP)."""
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in PY_SKIP and not d.startswith(('Scripts', 'Tools', 'Doc', 'include', 'libs', 'tcl', 'share'))]
        rel = os.path.relpath(root, src)
        out = os.path.join(dst, rel)
        os.makedirs(out, exist_ok=True)
        for f in files:
            if f in PY_SKIP_FILES or f.endswith(('.pyc', '.chm', '.lib', '.pdb')):
                continue
            shutil.copy2(os.path.join(root, f), os.path.join(out, f))


def scan(a, dist, third):
    """Step 4's leak scan of the folder `dist` against the ROM and ct_assets.dat; `third` = --third-party args."""
    if a.no_scan:
        say('   the leak scan: SKIPPED (--no-scan) -- this package is not checked yet')
        return
    corpora = []                                         # the game's bytes: the ROM and what it decompresses to
    for c in (os.path.join(ROOT, 'ct_assets.dat'),):
        if os.path.exists(c):
            corpora += ['--corpus', c]
    say('   the leak scan (the ROM%s)' % (' + ' + ', '.join(os.path.basename(c) for c in corpora[1::2]) if corpora else ''))
    p = run([sys.executable, os.path.join(ROOT, 'tools', 'release', 'scan_rom_leaks.py'), dist, '--rom', a.rom,
             '--binaries'] + corpora + third, ROOT, quiet=True)
    sys.stdout.write('   ' + p.stdout.strip().splitlines()[-1] + '\n')


def git_count():
    p = subprocess.run(['git', 'rev-list', '--count', 'HEAD'], cwd=ROOT, capture_output=True, text=True)
    return int(p.stdout.strip() or '1')


def android(a, ver, out, tree):
    """--android: the APK from the shapes tree (steps 3-4 for Android)."""
    say('3/4 the APK (Gradle assembleRelease: data-free, android/CMakeLists.cmake)')
    gw = os.path.join(tree, 'android', 'gradlew.bat' if WIN else 'gradlew')
    if not WIN:
        os.chmod(gw, 0o755)
    run([gw, '--no-daemon', '-q', 'assembleRelease', '-PctVersionCode=%d' % git_count(), '-PctVersionName=%s' % ver],
        os.path.join(tree, 'android'), quiet=True)
    apks = os.path.join(tree, 'android', 'app', 'build', 'outputs', 'apk', 'release')
    signed = os.path.join(apks, 'app-release.apk')
    src = signed if os.path.exists(signed) else os.path.join(apks, 'app-release-unsigned.apk')
    if not os.path.exists(src):
        raise SystemExit('package: FAILED: no APK in %s' % apks)
    apk = os.path.join(out, 'Belthasar-%s-android.apk' % ver)
    shutil.copy2(src, apk)
    say('4/4 %s (%s)' % (apk, 'signed' if src == signed else 'UNSIGNED: no release key configured'))
    unpacked = os.path.join(out, 'apk-unpacked')
    if os.path.exists(unpacked):
        shutil.rmtree(unpacked)
    with zipfile.ZipFile(apk) as z:
        z.extractall(unpacked)
    third = []
    for abi in os.listdir(os.path.join(unpacked, 'lib')):
        third += ['--third-party', 'lib/%s/libSDL2.so' % abi]
    scan(a, unpacked, third)
    if not a.keep:
        shutil.rmtree(unpacked, ignore_errors=True)
        shutil.rmtree(tree, ignore_errors=True)
    say('done: %s (%.1f MB)' % (apk, os.path.getsize(apk) / 2**20))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'release'))
    ap.add_argument('--rom', default=os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'), help='the leak scan\'s corpus')
    ap.add_argument('--python', help='the Python installation to bundle (default: the one running this, on Windows)')
    ap.add_argument('--no-python', action='store_true', help='bundle none: the player\'s python3 runs the setup')
    ap.add_argument('--keep', action='store_true', help='keep <out>/tree (the build tree) afterwards')
    ap.add_argument('--version', help='the package\'s version (default: git describe)')
    ap.add_argument('--no-scan', action='store_true', help='skip the leak scan (no ROM here; scan the package later)')
    ap.add_argument('--android', action='store_true', help='the Android app (an APK) instead of the desktop package')
    a = ap.parse_args()

    ver = (a.version or version()).replace('/', '-')
    arch = {'amd64': 'x64', 'x86_64': 'x64', 'arm64': 'arm64', 'aarch64': 'arm64'}.get(platform.machine().lower(), platform.machine())
    osname = 'windows' if WIN else 'macos' if sys.platform == 'darwin' else 'linux'
    name = 'Belthasar-%s-%s-%s' % (ver, osname, arch)
    out = os.path.abspath(a.out)
    tree, dist = os.path.join(out, 'tree'), os.path.join(out, name)
    for d in (tree, dist):
        if os.path.exists(d):
            shutil.rmtree(d)
    os.makedirs(out, exist_ok=True)

    say('1/5 the public tree (git archive HEAD) ->', tree)
    archive_tree(tree)
    for t in THIRD:
        s = os.path.join(ROOT, 'third_party', t)
        if os.path.isdir(s):
            shutil.copytree(s, os.path.join(tree, 'third_party', t))
    say('2/5 the shapes in place of the generated files')
    run([sys.executable, 'tools/gen/shapes.py', 'install'], tree, quiet=True)
    if a.android:
        return android(a, ver, out, tree)

    say('3/5 the build: the oracle from the shapes, Belthasar data-free')
    env = dict(os.environ, CT_DATAFREE='1')
    env.pop('CT_BUILD_DIR', None)
    for target in ('ct', 'Belthasar'):                    # (tools/build.py builds one target a call)
        run([sys.executable, 'tools/build.py', target], tree, env=env, quiet=True)
    b = os.path.join(tree, 'build')

    say('4/5 the release folder ->', dist)
    os.makedirs(os.path.join(dist, 'setup'))
    shutil.copy2(os.path.join(b, 'Belthasar' + EXE), dist)
    shutil.copy2(os.path.join(b, 'ct' + EXE), os.path.join(dist, 'setup'))
    if WIN:
        for f in os.listdir(b):
            if f.lower().endswith('.dll'):
                shutil.copy2(os.path.join(b, f), dist)
                shutil.copy2(os.path.join(b, f), os.path.join(dist, 'setup'))
    archive_tree(os.path.join(dist, 'setup', 'kit'))
    if not a.no_python and (a.python or WIN):
        src = a.python or sys.base_prefix
        copy_python(src, os.path.join(dist, 'setup', 'python'))
        say('   bundled Python %s from %s' % (platform.python_version() if not a.python else '?', src))
    for f in ('LICENSE', 'CREDITS.md', 'ct_mapedits.dat'):     # (the map editor's cells: the scene adjustments mod)
        if os.path.exists(os.path.join(ROOT, f)):
            shutil.copy2(os.path.join(ROOT, f), dist)
    with open(os.path.join(dist, 'README.txt'), 'w', encoding='utf-8', newline='\r\n' if WIN else '\n') as fh:
        fh.write(README % {'ver': ver, 'exe': 'Belthasar' + EXE})

    third = []                                           # upstream runtimes shipped as they are: listed, not searched
    for f in sorted(os.listdir(dist)):
        if f.lower().endswith('.dll'):
            third += ['--third-party', f, '--third-party', 'setup/' + f]
    if os.path.isdir(os.path.join(dist, 'setup', 'python')):
        third += ['--third-party', 'setup/python']
    scan(a, dist, third)

    say('5/5 the archive')
    if WIN:
        arc = dist + '.zip'
        with zipfile.ZipFile(arc, 'w', zipfile.ZIP_DEFLATED) as z:
            for root, _, files in os.walk(dist):
                for f in files:
                    p = os.path.join(root, f)
                    z.write(p, os.path.relpath(p, out))
    else:
        arc = dist + '.tar.gz'
        with tarfile.open(arc, 'w:gz') as t:
            t.add(dist, arcname=name)
    if not a.keep:
        shutil.rmtree(tree, ignore_errors=True)
    say('done: %s (%.1f MB)' % (arc, os.path.getsize(arc) / 2**20))
    return 0


README = """Belthasar %(ver)s
==================

Belthasar is a native reimplementation of a SNES game's engine. It ships NONE of the game: you need your own
dump of Chrono Trigger (USA), made from your own cartridge.

First run
---------
1. Open %(exe)s.
2. Click Browse... (or drag your .sfc file onto the window) and choose your dump. It is verified.
3. Click "Make the game data". The setup makes the game's data from your dump on this computer, once
   (about 15-20 minutes; nothing is downloaded).
4. Click Play. From then on Belthasar starts straight into the game; it reads only its own data, not the ROM.

Belthasar keeps everything in its own folder, beside %(exe)s: the made data (ct_assets.dat), your save
(ct_sram.srm) and your options (ct_settings.ini). Unpack it somewhere you can write to (not Program Files); to move or
back it up, move or copy the whole folder. Coming from 0.5.0 or older: on its first start it copies your save, options
and dump from the old folder (%%APPDATA%%\\BelthasarProject\\Belthasar\\ or ~/.local/share/BelthasarProject/Belthasar/).

F1 opens the options. The source code and the documentation: https://github.com/TemplarServant/BelthasarProject

Android
-------
The Android app (Belthasar-<version>-android.apk, the same release) does not make the game's data: copy ct_assets.dat
from the desktop Belthasar's folder -- and ct_wide.dat (the Steam margins) and ct_sram.srm (your save) if you want them -- to the
phone, open the app and choose them.
"""

if __name__ == '__main__':
    sys.exit(main())
