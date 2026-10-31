#!/usr/bin/env python3
r"""The regression suite's state fixtures, made from recipes (no savestate is ever published).

    python tools/fixtures.py list [--domain D]
    python tools/fixtures.py make  [all | <path>...] [--domain D] [--force] [--jobs N]
    python tools/fixtures.py diff  [<path>...]          (how far a recipe-made fixture is from the file on disk)

Many checks in tools/regress_checks.json start from a state of the game (saves/...). Those files hold the game's
memory, so they cannot ship; tools/fixtures/<domain>.json says how each one is made from a recipe (tools/recipes/):

    {"path": "saves/battle_re/gato_t0.raw.wram", "recipe": "gato_press_action", "pad": "0:145", "kind": "wram",
     "doc": "the last frame before GATO's cutscene (f145)"}

  recipe  the recipe whose state is the start (python tools/recipes.py make <recipe>)
  pad     optional: a pad script run from it first (ct.exe --peek ... --btnscript, under the rule; `(a:n,b:m)*K`
          groups are expanded as in a recipe)
  env     optional: extra oracle environment for that run (e.g. PEEK_POKE)
  kind    what file to write at `path`:
            state     the oracle savestate (CTLS)
            rawstate  the same savestate without its 16-byte CTLS header (the older captures' form, which several
                      tools read the WRAM out of at offset 0x45)
            wram      the 128 KB WRAM ($7E at 0, $7F at 0x10000)
            lakewram  the same WRAM as `#lakewram\nRAM:131072:<bytes>` (Belthasar --loadwram)
            flags     the 512 story-flag bytes $7F0000-$7F01FF
            srm       the 8 KB battery RAM (the savestate's last block)
            peek      an oracle --peek dump: the 512x480 frame at `path` and its .wram/.vram/.oam sidecars
            ctfs      the port's own CtScene state (Belthasar --loadstate; F5's format): Belthasar --scene <scene> seeded
                      from the WRAM above (--loadwram), saved at frame <frames> (default 1) by CT_SAVESTATE_AT, after
                      the optional <walkscript>. A CTFS image is sizeof(CtScene)'s: remake it (--force) after a struct
                      change
`make` writes a fixture only when it is missing, unless --force: private fixtures stay in place until a
check has been moved onto its recipe-made twin (docs/ROADMAP.md 1.1). It builds the recipes the fixtures need side by
side (tools/recipes.py make_many: a recipe starts once the ones it depends on are built), then the fixtures' own runs,
--jobs at a time (default: the CPU count): a fresh clone's `make all` costs about its longest recipe chain, not the sum.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import recipes as recipelib  # noqa: E402
from recipes import expand  # noqa: E402  (a pad's `(a:n,b:m)*K` groups, as in a recipe's pad step)
TMP = os.path.join(ROOT, 'build', 'fixtures')


def load(domain=None):
    out = []
    for p in sorted(glob.glob(os.path.join(ROOT, 'tools', 'fixtures', '*.json'))):
        dom = os.path.splitext(os.path.basename(p))[0]
        if domain and dom != domain:
            continue
        for e in json.load(open(p, encoding='utf-8'))['fixtures']:
            e['_domain'] = dom
            out.append(e)
    return out


def payload(state):
    d = open(state, 'rb').read()
    return d[16:] if d[:4] == b'CTLS' else d


def build(e):
    """Make the fixture into a temporary place (its recipe already built); returns the list of (tmp, final path)."""
    os.makedirs(TMP, exist_ok=True)
    state = os.path.join(ROOT, 'build', 'recipes', e['recipe'] + '.state')
    tag = os.path.join(TMP, e['path'].replace('/', '_').replace('.', '_'))    # the whole path: fixtures build in parallel
    raw = tag + '.raw'
    if e.get('pad') or e['kind'] == 'peek':
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_ORACLE_LOGICAL='1',
                   PEEK_SAVE=tag + '.state', **e.get('env', {}))
        rom = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
        pad = expand(e.get('pad') or '0:0')
        p = subprocess.run([CT, rom, '--peek', state, raw, '--btnscript', pad], cwd=ROOT, env=env,
                           capture_output=True, text=True, errors='replace')
        if not os.path.exists(tag + '.state'):
            raise SystemExit('fixtures: %s: the pad run wrote no state\n%s' % (e['path'], p.stdout[-800:]))
        state = tag + '.state'
    pl = payload(state)
    wram = pl[69:69 + 0x20000]
    k, dst = e['kind'], os.path.join(ROOT, e['path'])
    out = tag + '.out'
    if k == 'state':
        shutil.copyfile(state, out)
    elif k == 'rawstate':
        open(out, 'wb').write(pl)
    elif k == 'wram':
        open(out, 'wb').write(wram)
    elif k == 'lakewram':
        open(out, 'wb').write(b'#lakewram\nRAM:131072:' + wram)
    elif k == 'flags':
        open(out, 'wb').write(wram[0x10000:0x10200])
    elif k == 'srm':
        open(out, 'wb').write(pl[-8192:])
    elif k == 'peek':
        pairs = [(raw, dst)] + [(raw + x, dst + x) for x in ('.wram', '.vram', '.oam') if os.path.exists(raw + x)]
        return pairs
    elif k == 'ctfs':
        lw = tag + '.lakewram'
        open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + wram)
        at = int(e.get('frames', 1))
        if os.path.exists(out):
            os.remove(out)
        args = [os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', str(e['scene']),
                '--loadwram', lw, '--selftest', '--frames', str(at + 2)]
        if e.get('walkscript'):
            args += ['--walkscript', e['walkscript']]
        p = subprocess.run(args, cwd=ROOT, env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                                                     CT_NOPACE='1', CT_SAVESTATE_AT=str(at), CT_SAVESTATE=out),
                           capture_output=True, text=True, errors='replace')
        if not os.path.exists(out) or open(out, 'rb').read(4) != b'CTFS':
            raise SystemExit('fixtures: %s: Belthasar wrote no CTFS state\n%s' % (e['path'], p.stderr[-800:]))
    else:
        raise SystemExit('fixtures: %s: unknown kind %s' % (e['path'], k))
    return [(out, dst)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('cmd', choices=['list', 'make', 'diff'])
    ap.add_argument('paths', nargs='*')
    ap.add_argument('--domain')
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    a = ap.parse_args()
    fx = load(a.domain)
    if a.paths and a.paths != ['all']:
        fx = [e for e in fx if e['path'] in a.paths]
    if a.cmd == 'list':
        for e in fx:
            print('%-52s %-9s %-26s %s' % (e['path'], e['kind'], e['recipe'] + (' +pad' if e.get('pad') else ''),
                                          'present' if os.path.exists(os.path.join(ROOT, e['path'])) else 'missing'))
        return 0
    todo = []
    for e in fx:
        if a.cmd == 'make' and os.path.exists(os.path.join(ROOT, e['path'])) and not a.force:
            print('fixture %s: present (kept)' % e['path'])
        else:
            todo.append(e)
    if not todo:
        return 0
    # the recipes first, side by side (each chain in order), then the fixtures' own runs
    allr = recipelib.load_all()
    rom_path = os.path.abspath(os.environ.get('CT_ROM') or recipelib.romlib.find_default())
    rom_sha = recipelib.hashlib.sha1(recipelib.romlib.load(rom_path)).hexdigest()
    recipelib.make_many(allr, sorted({e['recipe'] for e in todo}), rom_path, rom_sha, recipelib.oracle_tag(), a.jobs)
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max(1, a.jobs)) as ex:
        for e, pairs in zip(todo, ex.map(build, todo)):
            if a.cmd == 'diff':
                for tmp, fin in pairs:
                    x = open(tmp, 'rb').read()
                    y = open(fin, 'rb').read() if os.path.exists(fin) else b''
                    n = sum(1 for i in range(min(len(x), len(y))) if x[i] != y[i]) + abs(len(x) - len(y))
                    print('fixture %s: %s' % (os.path.relpath(fin, ROOT), 'identical' if n == 0 else '%d bytes differ' % n))
                continue
            for tmp, fin in pairs:
                os.makedirs(os.path.dirname(fin), exist_ok=True)
                shutil.copyfile(tmp, fin)
            print('fixture %s: made from %s' % (e['path'], e['recipe']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
