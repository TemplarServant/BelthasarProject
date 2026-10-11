#!/usr/bin/env python3
r"""Oracle states rebuilt from power-on: the recipes that replace captured savestates.

    python tools/recipes.py list
    python tools/recipes.py make <name>... | all      [--rom ROM] [--force]
    python tools/recipes.py show <name>                (the chain of steps, flattened)

A savestate holds the console's whole memory -- the game's graphics, code and text -- so none may be published. What
can be published is how to get there: a RECIPE is a JSON file in tools/recipes/ that names a parent recipe (or the
power-on) and the steps the oracle (build/ct.exe, the cartridge under the rule) runs from it:

    {"name": "leene_square", "from": "bedroom", "doc": "...",
     "steps": [["pad", "10:32,0:8,100:6,0:60"]],
     "expect": {"loc": "0x1B2"}}

Steps:
  ["power_on", N]        the cartridge from power-on for N frames (the root of every chain)
  ["boot_to_bedroom"]    power-on through the new game's opening to Crono's bedroom (ct.exe --script; the drive in
                         src/harness_drive.c)
  ["pad", "btn:frames,..."]  run a pad script (ct.exe --btnscript: LakeSnes serial-order bits, R=80 L=40 D=20 U=10
                         Start=08 Select=04 Y=02 B=01, A=100 X=200, L=400 R=800; `0:N` waits N frames;
                         `(100:2,0:40)*5` repeats a group -- expanded here, before the oracle sees it)
  ["poke", "ADDR=HH,..."]  set WRAM bytes on the current state (PEEK_POKE on a 0-frame --peek): for the bytes a
                         recipe cannot replay (a capture's play clock, a party's sheets the owner's own fights left)
                         or that a capture's own provenance poked (a keeper's stores, a minigame's points)
  ["navworld"]           ct.exe --navworld: the scripted walk from Crono's bedroom to the overworld
  ["teleport", scene, tx, ty, facing, frames(, method)]  ct.exe --teleport: poke the location request ($0100-$0104)
                         and let the ROM's own loader run it (method jmp = enter C0:0066, the default; ow; req), then
                         run `frames` frames with no pad -- how several captures were made from an earlier state
  ["sram", "<recipe>"]   the battery RAM of another recipe's state at power-on (CT_ORACLE_SRAM): only valid before
                         power_on / boot_to_bedroom
"expect" asserts the result: "loc" ($0100) and "wram" {"0xADDR": "0xVALUE", ...}; a recipe whose result does not
match fails loudly rather than handing a wrong state to a generator or a check.

Every state is built under the rule (CT_ORACLE_LOGICAL=1, CLAUDE.md) and cached in build/recipes/<name>.state (+ .wram,
.vram, .oam sidecars of the last step); the cache key is the whole chain's steps, the ROM and the oracle's own sources,
so a recipe is rebuilt only when one of them changes.
"""
import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RECIPES = os.path.join(ROOT, 'tools', 'recipes')
OUT = os.path.join(ROOT, 'build', 'recipes')
CT = os.path.join(ROOT, 'build', 'ct.exe')
ORACLE_SOURCES = ['snes/*.c', 'snes/*.h', 'src/ct_cpu_infra.c', 'src/ct_lake_state.c', 'src/harness_drive.c',
                  'src/rom_verify.c']

sys.path.insert(0, os.path.join(ROOT, 'tools'))
from ctlib import rom as romlib  # noqa: E402


def load_all():
    out = {}
    for p in sorted(glob.glob(os.path.join(RECIPES, '*.json'))):
        r = json.load(open(p, encoding='utf-8'))
        r['_file'] = os.path.relpath(p, ROOT)
        if r['name'] in out:
            sys.exit('recipes: %s is defined twice' % r['name'])
        out[r['name']] = r
    return out


def chain(recipes, name):
    """The recipes from the root to `name`."""
    seq, seen = [], set()
    while name:
        if name in seen:
            sys.exit('recipes: a cycle through %s' % name)
        if name not in recipes:
            sys.exit('recipes: no recipe %s' % name)
        seen.add(name)
        seq.append(recipes[name])
        name = recipes[name].get('from')
    return seq[::-1]


def oracle_tag():
    h = hashlib.sha1()
    for pat in ORACLE_SOURCES:
        for p in sorted(glob.glob(os.path.join(ROOT, pat))):
            h.update(open(p, 'rb').read().replace(b'\r\n', b'\n'))
    return h.hexdigest()


def key_of(recipes, name, rom_sha, tag):
    body = [(r['name'], r.get('steps'), r.get('expect')) for r in chain(recipes, name)]
    # an sram step depends on another chain too
    for r in chain(recipes, name):
        for st in r.get('steps', []):
            if st[0] == 'sram':
                body.append(('sram', key_of(recipes, st[1], rom_sha, tag)))
    return hashlib.sha1(json.dumps([body, rom_sha, tag]).encode()).hexdigest()


def expand(script):
    """`(a:n,b:m)*K` -> the group K times (nestable)."""
    while '(' in script:
        m = re.search(r'\(([^()]*)\)\*(\d+)', script)
        if not m:
            sys.exit('recipes: a bad group in the pad script %r' % script)
        script = script[:m.start()] + ','.join([m.group(1)] * int(m.group(2))) + script[m.end():]
    return script.replace(' ', '')


def run(args, env_extra, log):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_ORACLE_LOGICAL='1', **env_extra)
    p = subprocess.run([CT] + args, cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    log.write('$ ct.exe %s  [%s]\n%s%s\n' % (' '.join(args), ' '.join('%s=%s' % kv for kv in env_extra.items()),
                                             p.stdout[-4000:], p.stderr[-2000:]))
    return p


def wram_of(state_path):
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    d = open(state_path, 'rb').read()
    pl = d[16:] if d[:4] == b'CTLS' else d
    return pl[69:69 + 0x20000]


def check_expect(r, path):
    exp = r.get('expect') or {}
    w = wram_of(path)
    bad = []
    if 'loc' in exp:
        loc = (w[0x100] | w[0x101] << 8) & 0x1FF
        if loc != int(exp['loc'], 16):
            bad.append('loc %03X, expected %s' % (loc, exp['loc']))
    for a, v in (exp.get('wram') or {}).items():
        a, v = int(a, 16), int(v, 16)
        if w[a] != v:
            bad.append('$%04X = %02X, expected %02X' % (a, w[a], v))
    return bad


def make(recipes, name, rom_path, rom_sha, tag, force=False, depth=0):
    r = recipes[name]
    os.makedirs(OUT, exist_ok=True)
    st = os.path.join(OUT, name + '.state')
    meta = os.path.join(OUT, name + '.key')
    key = key_of(recipes, name, rom_sha, tag)
    if not force and os.path.exists(st) and os.path.exists(meta) and open(meta).read() == key:
        print('%srecipe %s: cached' % ('  ' * depth, name))
        return st
    parent = r.get('from')
    src = make(recipes, parent, rom_path, rom_sha, tag, False, depth + 1) if parent else None
    print('%srecipe %s: building' % ('  ' * depth, name), flush=True)
    log = open(os.path.join(OUT, name + '.log'), 'w', encoding='utf-8')
    env, cur = {}, src
    for i, step in enumerate(r.get('steps', [])):
        tmp = os.path.join(OUT, '%s.step%d' % (name, i))
        op = step[0]
        if op == 'sram':
            srm = make(recipes, step[1], rom_path, rom_sha, tag, False, depth + 1)
            env['CT_ORACLE_SRAM'] = srm + '.srm'
            continue
        if op == 'power_on':
            p = run([rom_path, '--fb', tmp + '.raw', '--frames', str(step[1])], dict(env, PEEK_SAVE=tmp + '.state'), log)
        elif op == 'boot_to_bedroom':
            p = run([rom_path, '--fb', tmp + '.raw', '--script', '--frames', '1'], dict(env, PEEK_SAVE=tmp + '.state'), log)
        elif op == 'pad':
            p = run([rom_path, '--peek', cur, tmp + '.raw', '--btnscript', expand(step[1])],
                    dict(env, PEEK_SAVE=tmp + '.state'), log)
        elif op == 'poke':
            p = run([rom_path, '--peek', cur, tmp + '.raw', '--btnscript', '0:0'],
                    dict(env, PEEK_SAVE=tmp + '.state', PEEK_POKE=step[1]), log)
        elif op == 'navworld':
            p = run([rom_path, '--navworld', tmp + '.state', '--from', cur], env, log)
        elif op == 'teleport':
            sc, tx, ty, face, frames = step[1:6]
            meth = step[6] if len(step) > 6 else 'jmp'
            p = run([rom_path, '--teleport', str(sc), str(tx), str(ty), str(face), '--from', cur, '--method', meth,
                     '--frames', str(frames), '--out', tmp + '.state'], env, log)
        else:
            sys.exit('recipe %s: unknown step %r' % (name, op))
        if not os.path.exists(tmp + '.state'):
            sys.exit('recipe %s: step %d (%s) wrote no state; see %s' % (name, i, op, log.name))
        cur = tmp + '.state'
    if cur is None:
        sys.exit('recipe %s: no steps and no parent' % name)
    shutil.copyfile(cur, st)
    last = os.path.splitext(cur)[0] + '.raw'
    for ext in ('.wram', '.vram', '.oam'):
        if os.path.exists(last + ext):
            shutil.copyfile(last + ext, st[:-6] + '.raw' + ext)
    bad = check_expect(r, st)
    if bad:
        os.remove(st)
        sys.exit('recipe %s: the result is not what the recipe expects: %s (log %s)' % (name, '; '.join(bad), log.name))
    open(meta, 'w').write(key)
    for f in glob.glob(os.path.join(OUT, name + '.step*')):
        os.remove(f)
    return st


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('cmd', choices=['list', 'make', 'show', 'try'])
    ap.add_argument('names', nargs='*')
    ap.add_argument('--rom')
    ap.add_argument('--force', action='store_true')
    a = ap.parse_args()
    recipes = load_all()
    if a.cmd == 'list':
        for n, r in recipes.items():
            print('%-28s from %-22s %s' % (n, r.get('from') or '(power-on)', r.get('doc', '')[:80]))
        return 0
    if a.cmd == 'show':
        for n in a.names:
            for r in chain(recipes, n):
                print('%s: %s' % (r['name'], json.dumps(r.get('steps'))))
        return 0
    rom_path = a.rom or os.environ.get('CT_ROM') or romlib.find_default()
    rom_sha = hashlib.sha1(romlib.load(rom_path)).hexdigest()       # verified: only the supported dump runs
    rom_path = os.path.abspath(rom_path)
    tag = oracle_tag()
    if a.cmd == 'try':                     # authoring: <recipe or .state> <pad script> -> build/_author/try.state + .png
        start = a.names[0] if a.names[0].endswith('.state') else make(recipes, a.names[0], rom_path, rom_sha, tag)
        d = os.path.join(ROOT, 'build', '_author')
        os.makedirs(d, exist_ok=True)
        out = os.path.join(d, 'try')
        p = run([rom_path, '--peek', start, out + '.raw', '--btnscript', expand(a.names[1])],
                {'PEEK_SAVE': out + '.state'}, sys.stdout)
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'ct.py'), 'fb2png', out + '.raw', out + '.png'],
                       cwd=ROOT, capture_output=True)
        return 0
    names = list(recipes) if a.names == ['all'] else a.names
    for n in names:
        if n not in recipes:
            sys.exit('recipes: no recipe %s' % n)
        print('%s -> %s' % (n, os.path.relpath(make(recipes, n, rom_path, rom_sha, tag, a.force), ROOT)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
