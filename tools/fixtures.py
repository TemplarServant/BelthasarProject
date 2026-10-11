#!/usr/bin/env python3
r"""The regression suite's state fixtures, made from recipes (no savestate is ever published).

    python tools/fixtures.py list [--domain D]
    python tools/fixtures.py make  [all | <path>...] [--domain D] [--force]
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
            lakewram  the same WRAM as `#lakewram\nRAM:131072:<bytes>` (ct_field --loadwram)
            flags     the 512 story-flag bytes $7F0000-$7F01FF
            srm       the 8 KB battery RAM (the savestate's last block)
            peek      an oracle --peek dump: the 512x480 frame at `path` and its .wram/.vram/.oam sidecars
`make` writes a fixture only when it is missing, unless --force: the owner's private fixtures stay in place until a
check has been moved onto its recipe-made twin (docs/ROADMAP.md 1.1).
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
    """Make the fixture into a temporary place; returns the list of (tmp, final path)."""
    os.makedirs(TMP, exist_ok=True)
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'recipes.py'), 'make', e['recipe']], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode:
        raise SystemExit('fixtures: recipe %s failed:\n%s' % (e['recipe'], r.stdout[-800:] + r.stderr[-800:]))
    state = os.path.join(ROOT, 'build', 'recipes', e['recipe'] + '.state')
    tag = os.path.join(TMP, os.path.basename(e['path']).replace('.', '_'))
    raw = tag + '.raw'
    if e.get('pad') or e['kind'] == 'peek':
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_ORACLE_LOGICAL='1',
                   PEEK_SAVE=tag + '.state', **e.get('env', {}))
        rom = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from recipes import expand
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
    else:
        raise SystemExit('fixtures: %s: unknown kind %s' % (e['path'], k))
    return [(out, dst)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('cmd', choices=['list', 'make', 'diff'])
    ap.add_argument('paths', nargs='*')
    ap.add_argument('--domain')
    ap.add_argument('--force', action='store_true')
    a = ap.parse_args()
    fx = load(a.domain)
    if a.paths and a.paths != ['all']:
        fx = [e for e in fx if e['path'] in a.paths]
    if a.cmd == 'list':
        for e in fx:
            print('%-52s %-9s %-26s %s' % (e['path'], e['kind'], e['recipe'] + (' +pad' if e.get('pad') else ''),
                                          'present' if os.path.exists(os.path.join(ROOT, e['path'])) else 'missing'))
        return 0
    for e in fx:
        dst = os.path.join(ROOT, e['path'])
        if a.cmd == 'make' and os.path.exists(dst) and not a.force:
            print('fixture %s: present (kept)' % e['path'])
            continue
        pairs = build(e)
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
