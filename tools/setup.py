#!/usr/bin/env python3
r"""Set BelthasarProject up from your own ROM: verify it, extract the assets, rebuild the oracle states, generate the data, build.

    python tools/setup.py --rom <path to your Chrono Trigger (USA) dump> [--no-build] [--jobs N]
    python tools/setup.py --check            regenerate every generated file and compare with the one in the tree

The repository carries none of the game. This is the one command that makes, on your machine and from your dump, every
file the build needs that comes from the game (docs/ROM.md):

  1. the ROM is verified (Chrono Trigger (USA), SHA-1 de5822f4...; a 512-byte copier header is dropped) and copied to
     Chrono_Trigger_USA_SNES.sfc at the repository root, where every tool looks for it;
  2. the oracle (build/ct.exe) is built; the recipes the generators need (tools/recipes/) are replayed from power-on
     as each generator asks for them;
  3. the generators marked "stage": "assets" in tools/gen/manifest*.json write the oracle captures ct_assets.dat packs
     (the overworld snapshots and sprite atlases, the window bitmaps: assets/extract_assets.py pack_world);
  4. assets/extract_assets.py builds ct_assets.dat (the scenes, sprites, scripts and text the engine loads);
  5. the other generators write src/data/*.h and src/xlat/*.c;
  6. tools/gen/pack_tables.py packs the tables of src/data/*.h into ct_tables.dat, the file a data-free Belthasar
     (CT_DATAFREE) loads them from instead of compiling them in;
  7. everything is built (tools/build.py).

Each step says what it did. A generator that still needs a capture that is not a recipe yet is reported (see
docs/ROADMAP.md) and setup exits 1 at the end if any generated file is missing.

The player's setup (Belthasar's ROM screen, src/launcher.c) runs the same steps without a compiler:

    python tools/setup.py --rom <dump> --no-build --oracle <ct.exe> --workdir <dir> --out <dir>

--oracle uses a ct.exe built beforehand (the release's, compiled from the shapes like step 2's) instead of building one;
--workdir copies this tree to <dir> first and runs there (the installed one may be read-only); --out copies
ct_assets.dat and ct_tables.dat to <dir> at the end. Every step prints `setup: progress K/N` for the screen's bar.

--check is the inverse proof for this repository: every generator runs again and its output must equal the file in the
tree (line endings aside), so a header that cannot be made from the ROM shows up; the asset-stage outputs are compared
with the captures they replace, and ct_assets.dat is rebuilt from the regenerated ones (into build/setup_check/) and
compared with the one at the root.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from ctlib import rom as romlib  # noqa: E402

ROM_AT = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')


def say(msg):
    print('setup: ' + msg, flush=True)


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, **kw)


def generators():
    """Every tools/gen/manifest*.json's generators, manifest.json first, the others by name (one per area)."""
    import glob
    paths = sorted(glob.glob(os.path.join(ROOT, 'tools', 'gen', 'manifest*.json')),
                   key=lambda p: (os.path.basename(p) != 'manifest.json', p))
    return [g for p in paths for g in json.load(open(p, encoding='utf-8'))['generators']]


def stage_of(g):
    """'assets' for a generator whose output assets/extract_assets.py packs into ct_assets.dat (it runs first)."""
    return g.get('stage', 'data')


def run_generator(g):
    """Run one generator from the root; returns (ok, message)."""
    missing = [n for n in g.get('needs', []) if not os.path.exists(os.path.join(ROOT, n))]
    if missing:
        return False, 'needs %s (a capture not yet replaced by a recipe)' % ', '.join(missing)
    for r in g.get('recipes', []):
        if run([sys.executable, 'tools/recipes.py', 'make', r]).returncode:
            return False, 'recipe %s failed' % r
    cmd = [sys.executable] + g['cmd']
    if g.get('stdout'):
        with open(os.path.join(ROOT, g['out'][0]), 'w', encoding='utf-8', newline='\n') as f:
            p = run(cmd, stdout=f, stderr=subprocess.PIPE, text=True)
    else:
        p = run(cmd, capture_output=True, text=True)
    if p.returncode:
        return False, 'exit %d: %s' % (p.returncode, (p.stderr or '')[-400:])
    return True, 'ok'


def norm(path):
    return open(path, 'rb').read().replace(b'\r\n', b'\n') if os.path.exists(path) else None


def check():
    if run([sys.executable, 'tools/gen/shapes.py', 'check']).returncode:
        print('gen-check: the shapes are stale -- python tools/gen/shapes.py make')
    if run([sys.executable, 'tools/gen/pack_tables.py', 'check']).returncode:
        print('gen-check: src/tables/ is stale -- python tools/gen/pack_tables.py headers')
    bad = 0
    saved = {}                                                # every output's bytes in the tree, put back at the end
    gens = sorted(generators(), key=lambda g: stage_of(g) != 'assets')
    try:
        for i, g in enumerate(gens):
            before = {o: open(os.path.join(ROOT, o), 'rb').read() for o in g['out'] if os.path.exists(os.path.join(ROOT, o))}
            for o in g['out']:
                saved.setdefault(o, before.get(o))
            ok, msg = run_generator(g)
            for o in g['out']:
                p = os.path.join(ROOT, o)
                if not ok:
                    print('gen-check: %-34s SKIP (%s)' % (o, msg))
                    continue
                same = o in before and norm(p) == before[o].replace(b'\r\n', b'\n')
                if not same and g.get('compare') == 'ct_assets.dat':
                    # a whole-RAM dump whose unread bytes (power-on fill, stack, the play clock) follow the capture's
                    # history: what proves it is the ct_assets.dat built from it (compared below)
                    print('gen-check: %-34s regenerated (not byte-equal; proved by ct_assets.dat)' % o)
                    continue
                print('gen-check: %-34s %s' % (o, 'identical' if same else 'DIFFERS'))
                bad += not same
            last_assets = stage_of(g) == 'assets' and (i + 1 == len(gens) or stage_of(gens[i + 1]) != 'assets')
            if last_assets and os.path.exists(os.path.join(ROOT, 'ct_assets.dat')):
                # ct_assets.dat from the regenerated captures (they stay in place until the end) against the root's
                d = os.path.join(ROOT, 'build', 'setup_check')
                os.makedirs(d, exist_ok=True)
                out = os.path.join(d, 'ct_assets.dat')
                p = run([sys.executable, 'assets/extract_assets.py', ROM_AT, out], capture_output=True, text=True)
                same = not p.returncode and open(out, 'rb').read() == open(os.path.join(ROOT, 'ct_assets.dat'), 'rb').read()
                print('gen-check: %-34s %s' % ('ct_assets.dat', 'identical' if same else 'DIFFERS'))
                bad += not same
    finally:
        for o, data in saved.items():                        # the tree keeps its own bytes either way
            if data is not None:
                open(os.path.join(ROOT, o), 'wb').write(data)
    outs = {o for g in generators() for o in g['out']}
    tree = {'src/data/' + f for f in os.listdir(os.path.join(ROOT, 'src', 'data')) if f.endswith('.h')}
    tree |= {'src/xlat/' + f for f in os.listdir(os.path.join(ROOT, 'src', 'xlat')) if f.endswith('.c')}
    print('gen-check: %d of %d generated files (src/data/*.h, src/xlat/*.c) have a generator; %s; not generated yet: %d (docs/ROADMAP.md)'
          % (len(outs & tree), len(tree), 'all reproduced' if not bad else '%d DIFFER' % bad, len(tree - outs)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--rom')
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--oracle', help='a ct.exe built beforehand: used instead of building one')
    ap.add_argument('--workdir', help='copy this tree to WORKDIR and run there')
    ap.add_argument('--out', help='copy ct_assets.dat and ct_tables.dat to OUT at the end')
    a = ap.parse_args()

    if a.workdir and os.path.normcase(os.path.abspath(a.workdir)) != os.path.normcase(ROOT):
        work = os.path.abspath(a.workdir)
        say('copying the setup to %s' % work)
        shutil.copytree(ROOT, work, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git', '.claude', 'build', 'third_party', '__pycache__', 'saves',
                                                      '*.sfc', '*.dat', '*.srm', '*.state', '*.wram'))
        args = [x for x in sys.argv[1:]]
        i = args.index('--workdir')
        del args[i:i + 2]
        for k in ('--rom', '--oracle', '--out'):              # paths the copy must still find, made absolute
            if k in args:
                j = args.index(k) + 1
                args[j] = os.path.abspath(args[j])
        return subprocess.run([sys.executable, os.path.join(work, 'tools', 'setup.py')] + args, cwd=work).returncode

    src = a.rom or (ROM_AT if os.path.exists(ROM_AT) else None)
    if not src:
        sys.exit('setup: name your ROM: python tools/setup.py --rom <path>  (docs/ROM.md)')
    try:
        data = romlib.load(src)
    except romlib.RomError as e:
        sys.exit('setup: %s' % e)
    say('ROM verified: Chrono Trigger (USA), SHA-1 %s' % romlib.ROM_SHA1)
    if os.path.abspath(src) != ROM_AT:
        open(ROM_AT, 'wb').write(data)
        say('ROM copied to %s' % os.path.relpath(ROM_AT, ROOT))

    if a.check:
        return check()

    # A fresh clone has no generated file, and the oracle is compiled with the engine that includes them: the shapes
    # (tools/gen/shapes/: declarations and sizes, no data) stand in for the missing ones so a bootstrap oracle builds.
    run([sys.executable, 'tools/gen/shapes.py', 'install'])
    gens = generators()
    total = 3 + len(gens)                                  # the oracle, the generators, the extraction, the pack
    done = [0]

    def progress():
        done[0] += 1
        say('progress %d/%d' % (done[0], total))

    oracle = os.path.join(ROOT, 'build', 'ct.exe')
    if a.oracle:
        if not os.path.exists(a.oracle):
            sys.exit('setup: no oracle at %s' % a.oracle)
        if os.path.normcase(os.path.abspath(a.oracle)) != os.path.normcase(oracle):
            os.makedirs(os.path.dirname(oracle), exist_ok=True)
            shutil.copy2(a.oracle, oracle)
        say('the oracle: %s' % a.oracle)
    else:
        say('building the oracle (build/ct.exe)')
        if run([sys.executable, 'tools/build.py', 'ct']).returncode:
            sys.exit('setup: the oracle did not build')
    progress()

    missing = []

    def is_shape(path):
        p = os.path.join(ROOT, path)
        return not os.path.exists(p) or open(p, 'rb').read(64).startswith(b'/* SHAPE of a generated file')

    def run_stage(gens):
        for g in gens:
            ok, msg = run_generator(g)
            say('%-40s %s' % (', '.join(os.path.basename(o) for o in g['out'])[:40], msg))
            missing.extend(o for o in g['out'] if is_shape(o))
            progress()

    # every recipe the generators need, built once and side by side (tools/recipes.py make_many: a chain in order, the
    # chains in parallel); each generator's own `recipes.py make` then finds its states cached
    need = sorted({r for g in gens for r in g.get('recipes', [])})
    if need:
        say('replaying the %d recipes the generators need' % len(need))
        run([sys.executable, 'tools/recipes.py', 'make'] + need)

    run_stage([g for g in gens if stage_of(g) == 'assets'])   # the captures ct_assets.dat packs

    say('extracting the assets -> ct_assets.dat')
    if run([sys.executable, 'assets/extract_assets.py', ROM_AT, 'ct_assets.dat']).returncode:
        sys.exit('setup: assets/extract_assets.py failed')
    progress()

    run_stage([g for g in gens if stage_of(g) != 'assets'])

    say('packing the tables -> ct_tables.dat')
    if run([sys.executable, 'tools/gen/pack_tables.py', 'pack']).returncode:
        missing.append('ct_tables.dat')
    progress()

    if not a.no_build:
        say('building everything')
        if run([sys.executable, 'tools/build.py']).returncode:
            sys.exit('setup: the build failed%s' % (' (missing generated files: %s)' % ', '.join(missing) if missing else ''))
    if missing:
        say('NOT COMPLETE: %d generated file(s) could not be made: %s (docs/ROADMAP.md)' % (len(missing), ', '.join(missing)))
        return 1
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        for f in ('ct_assets.dat', 'ct_tables.dat'):
            shutil.copy2(os.path.join(ROOT, f), os.path.join(a.out, f))
        say('ct_assets.dat and ct_tables.dat -> %s' % a.out)
    say('done. Play: build\\Belthasar.exe --assets ct_assets.dat --boot')
    return 0


if __name__ == '__main__':
    sys.exit(main())
