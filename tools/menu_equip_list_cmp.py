#!/usr/bin/env python3
r"""The Equip screen's candidate list, frame by frame, against the cartridge (menu2_equip_change.md; user report
2026-09-24: "when opening to equip weapons, the box does not 'rise' with the canonical animation").

    python tools/menu_equip_list_cmp.py

saves/battle_re/party3_field.state with an Iron Blade poked into bag slot 1 (the oracle, LOGICAL frames) and the same
WRAM with the same poke (the port, saves/battle_re/party3_field.lakewram), one drive on both, each press on the frame
the cartridge reads it: X; A (Equip); A (Crono); A on the Weapon row (C2/A41A -> C2/A4AF, the 8-frame climb); B (C2/A64D
-> C2/A697, the way back down); A on the Weapon row again; A (commit the Iron Blade); A on the Weapon row (now the Wood
Sword -- a LOWER Attack); B. Graded every frame f138-f330, three ways:

  edges   the rows of the window rules crossing x 24-100 (the column's top and bottom, the bar's top): the geometry
  bar     the pixels of the description bar's box right of the column (x 112-239, y 176-207)
  attack  the pixels of the picked member's Attack digits (x 40-65, y 46-57): the live preview C2/A5C3 / C2/A5E7 --
          the NEW value, palette 2 when the candidate raises it, palette 7 when it lowers it

(The screen's static art is graded nowhere yet and differs: the marble's colour conversion, the portrait and plaque,
the icons, the hand -- those regions are outside the three above.)

    equip-list: <what> f138-f330: <N> frame(s) differ [...]
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb, peekshots  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
POKE = {0x2401: 0x02, 0x2501: 0x01}          # an Iron Blade in bag slot 1
# presses: (oracle btnscript mask, oracle press frame); the cartridge reads each one frame later (its pad latch)
PRESSES = [(0x200, 4), (0x100, 60), (0x100, 100), (0x100, 140), (0x1, 180), (0x100, 220), (0x100, 260), (0x100, 300),
           (0x1, 330)]
KEY = {0x200: 'm', 0x100: 'a', 0x1: 'b'}
F0, F1 = 138, 330


def obtn(total):
    parts, t = [], 0
    for m, f in PRESSES:
        if f >= total:
            break
        if f > t:
            parts.append('0:%d' % (f - t))
        n = min(2, total - f)
        parts.append('%X:%d' % (m, n))
        t = f + n
    if total > t:
        parts.append('0:%d' % (total - t))
    return ','.join(parts)


def oracle(frames):
    """Every frame's dump from ONE logical-oracle run (PEEK_SHOTS: the same bytes as a --peek cut to f + 2)."""
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_POKE=','.join('%04X=%02X' % kv for kv in POKE.items()))
    got = peekshots.shots('saves/battle_re/party3_field.state', obtn(max(frames) + 2), [f + 2 for f in frames], env=env,
                          prefix=os.path.join(ROOT, 'build', '_eqlist_o'))
    return {f: got[f + 2] for f in frames}


def port(frames):
    b = bytearray(open(os.path.join(ROOT, 'saves', 'battle_re', 'party3_field.lakewram'), 'rb').read())
    o = b.index(b'RAM:131072:') + 11
    for a, v in POKE.items():
        b[o + a] = v
    seed = os.path.join(ROOT, 'build', '_eqlist_seed.lakewram')
    open(seed, 'wb').write(bytes(b))
    walk, t = [], 0
    for m, f in PRESSES:
        pf = f + 1                                   # the frame the cartridge reads it
        if pf > t:
            walk.append('n:%d' % (pf - t))
        walk.append('%s:1' % KEY[m])
        t = pf + 1
    walk.append('n:%d' % (F1 + 4 - t))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=os.path.join(ROOT, 'build', '_eqlist_p'), CT_SHOTS_AT=','.join(str(f) for f in frames))
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '130', '--loadwram', seed, '--selftest',
                    os.path.join(ROOT, 'build', '_eqlist.raw'), '--frames', str(F1 + 4), '--walkscript', ','.join(walk)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {f: os.path.join(ROOT, 'build', '_eqlist_p_f%d.raw' % f) for f in frames}


def px(img, x, y, oracle):
    w, h, p = img
    i = ((y + (8 if oracle else 0)) * w + x) * 3
    return p[i:i + 3]


def edges(img, oracle):
    r = []
    for y in range(224):
        n = 0
        for x in range(24, 100):
            c = px(img, x, y, oracle)
            if c[0] > 200 and c[1] > 200 and c[2] > 200:
                n += 1
        if n > 60:
            r.append(y)
    return r


def region(o, p, x0, x1, y0, y1):
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if px(o, x, y, True) != px(p, x, y, False))


def main():
    frames = list(range(F0, F1 + 1))
    pfs = port(frames)
    ofs = oracle(frames)
    bad = {'edges': [], 'bar': [], 'attack': []}
    for f in frames:
        o = fb.load(ofs[f])
        p = fb.load(pfs[f])
        if edges(o, True) != edges(p, False):
            bad['edges'].append(f)
        if region(o, p, 112, 240, 176, 208):
            bad['bar'].append(f)
        if region(o, p, 40, 66, 46, 58):
            bad['attack'].append(f)
    for k in ('edges', 'bar', 'attack'):
        v = bad[k]
        print('equip-list: %s f%d-f%d: %d frame(s) differ%s' % (k, F0, F1, len(v), (' ' + str(v[:10])) if v else ''))
    peekshots.clean(ofs)


if __name__ == '__main__':
    main()
