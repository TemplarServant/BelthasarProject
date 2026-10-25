#!/usr/bin/env python3
r"""Sleeping at an inn, port against the cartridge under the rule: the way back out of loc 10F and the wake-up walk.

    python tools/inn_sleep_cmp.py [frames]

THE ROM. The keeper (loc 00C's obj8, `C0 4A 06` -> "Check in?") charges, heals, raises $7F0152 bit 0 (`65 80 52`), leaves
the way back in the story page (`56 0C 66 01 / 56 38 68 01 / 56 08 6A 01 / 56 01 6C 01` = $7F0166/68/6A/6C: loc 00C,
tile 38,08, facing 1) and sends the party to loc 10F with `E0 0F 03 08 08`. Loc 10F's obj0 counts its local 9 to 10
(`AD 05 / 71 09 / 11 0A`), copies the four bytes into locals 0A-0D (`53 68 01 0A` ..) and leaves with `E2 0C 0A 0B 0D`
(C0/3B9E: op 0xE0's fade-change with every operand a LOCAL -- until 2026-09-30 the port stepped over it and the screen
stayed black for good). Back in loc 00C the PCs' fn0s pick their beds from their own party slot (`5C / 48 04 11 7E 0A /
19 0A / 1A 00 .. 1A 01 .. 1A 02 ..`: $1100,X read back through a local into the result word) and obj0 walks them out.

  fixture:   saves/field_re/inn_sleep_10f.state (the cartridge, under the rule) and .lakewram (its WRAM, for the port):
             saves/base_loc6.state -> `--teleport 12 51 21 0 --frames 120` (the counter) -> PEEK_POKE="7F0152=01,
             7F0166=0C,7F0168=38,7F016A=08,7F016C=01" (the keeper's five stores) `--teleport 271 8 8 1 --frames 40` (his
             E0's destination), CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1. At f40 loc 10F's obj0 is in its count.
  cartridge: build/ct.exe --monitor from the .state, the rule
  port:      build/Belthasar.exe --scene 271 --loadwram the .lakewram (CT_FIELD_MEMMON)

Printed:
    inn-sleep: back to loc <L> tile (<x>,<y>) facing <f> at cartridge f<a> / port f<b>
    inn-sleep: beds cartridge f<a> obj1 (<x>,<y>) obj2 (<x>,<y>) / port f<b> ...
    inn-sleep: wake walk from the first step (cartridge f<a>, port f<b>): <k>/<n> frames equal [first differ c<f>: ..]
The walk is $1800/$1880 (x/y words) and $1600 (facing) of obj1 and obj2, aligned on each side's first step (the reload
of loc 00C prices its own frames -- the load clock, an open item of its own -- so the two walks start apart).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
STATE = os.path.join(ROOT, 'saves', 'field_re', 'inn_sleep_10f.state')
SEED = os.path.join(ROOT, 'saves', 'field_re', 'inn_sleep_10f.lakewram')
REGIONS = [('0100', 2), ('0102', 3), ('1802', 4), ('1882', 4), ('1602', 1), ('1604', 1)]


def rows(text, pat):
    out = {}
    for line in text.splitlines():
        m = re.match(pat, line)
        if m:
            vals = re.findall(r'=([0-9A-F]+)\*?', line)
            if len(vals) >= len(REGIONS):
                out[int(m.group(1))] = tuple(vals[-len(REGIONS):])
    return out


def filled(d, last):
    out, cur = {}, None
    for f in range(0, last + 1):
        if f in d:
            cur = d[f]
        out[f] = cur
    return out


def word(h, k):                       # the k-th little-endian word of a hex byte run
    return int(h[k * 4 + 2:k * 4 + 4] + h[k * 4:k * 4 + 2], 16)


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 560
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1')
    spec = ','.join('7E%s:%d' % (a, n) for a, n in REGIONS)
    o = subprocess.run([CT, '--monitor', spec, '--from', STATE, '--frames', str(frames)], cwd=ROOT, env=env,
                       capture_output=True, text=True, errors='replace')
    ot = rows(o.stdout, r'\s*f(\d+)\s')
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                CT_FIELD_MEMMON=','.join('%s:%X' % (a, n) for a, n in REGIONS))
    penv.pop('CT_ORACLE_LOGICAL', None); penv.pop('CT_ORACLE_VIRTUAL_BEAM', None)
    p = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '271', '--loadwram', SEED, '--selftest',
                        os.path.join(ROOT, 'build', '_inn_sleep.raw'), '--frames', str(frames + 80)],
                       cwd=ROOT, env=penv, capture_output=True, text=True, errors='replace')
    pt = filled(rows(p.stdout + p.stderr, r'fmemmon f(\d+) '), frames + 80)
    if not ot or not any(pt.values()):
        print('inn-sleep: FAIL no rows (cartridge %d, port %d)' % (len(ot), sum(1 for v in pt.values() if v)))
        return

    def back(d):                      # the first frame the location reads 00C, and where the staged change put the lead
        for f in sorted(d):
            if d[f] and d[f][0] == '0C00':
                return f, d[f][1]
        return None, None
    ob, oxy = back(ot); pb, pxy = back(pt)
    fmt = lambda f, xy: ('f%d -> loc 00C tile (%d,%d) facing %d' % (f, int(xy[0:2], 16), int(xy[2:4], 16), int(xy[4:6], 16))
                         if f is not None else 'never')
    print('inn-sleep: back to loc 00C: cartridge %s / port %s' % (fmt(ob, oxy), fmt(pb, pxy)))
    if ob is None or pb is None:
        return

    def beds(d, start):               # the first frame after the return whose two PC words both change (the fn0 placement)
        f0 = d[start]
        for f in sorted(k for k in d if k > start):
            if d[f] and d[f][2] != f0[2]:
                return f, d[f]
        return None, None
    obf, obv = beds(ot, ob); pbf, pbv = beds(pt, pb)
    pos = lambda v: ('obj1 (%d,%d) obj2 (%d,%d)' % (word(v[2], 0) >> 4, word(v[3], 0) >> 4, word(v[2], 1) >> 4, word(v[3], 1) >> 4)
                     if v else '-')
    print('inn-sleep: beds cartridge f%s %s / port f%s %s' % (obf, pos(obv), pbf, pos(pbv)))
    if obf is None or pbf is None:
        return

    def first_step(d, start):
        s0 = d[start][2]
        for f in sorted(k for k in d if k > start):
            if d[f] and d[f][2] != s0:
                return f
        return None
    oa, pa = first_step(ot, obf), first_step(pt, pbf)
    if oa is None or pa is None:
        print('inn-sleep: wake walk: no step (cartridge %s, port %s)' % (oa, pa))
        return
    n = k = 0; bad = None
    for f in sorted(x for x in ot if x >= oa):
        q = pt.get(f - oa + pa)
        if q is None:
            break
        n += 1
        if q[2:] == ot[f][2:]:
            k += 1
        elif bad is None:
            bad = (f, ot[f][2:], q[2:])
    tail = '' if bad is None else ' [first differ c%d: cartridge %s / port %s]' % (bad[0], ' '.join(bad[1]), ' '.join(bad[2]))
    print('inn-sleep: wake walk from the first step (cartridge f%d, port f%d): %d/%d frames equal%s' % (oa, pa, k, n, tail))


if __name__ == '__main__':
    main()
