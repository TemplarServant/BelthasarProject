#!/usr/bin/env python3
r"""The SHOP screen ($0C) against the cartridge, picture for picture (disasm/notes/shop_screen.md).

    python tools/shop_screen_cmp.py

Drives the SAME shop on both sides -- the fair merchant (loc 5 obj16, `C8 90` = shop $10) from
saves/field_re/shop90_lake.state (oracle) and saves/field_re/shop90.lakewram (the port, its WRAM) -- through
Buy / a Karate Gi / confirm / B / Sell / the Karate Gi / confirm, and compares whole 256x224 frames at aligned
moments. Prints one line per moment:

    shop-screen-cmp: <name> oracle f<O> port f<P> -> <N> px differ

and, for shop $11 (the oracle's script byte poked to `C8 91`, the port's CT_SHOPNO=11), the buy list with its
PROPORTIONAL description line ("Restores 50 HP", C2/F2F3 -> C2/FA49).

THE ALIGNMENT (docs/TESTING.md, "THE ALIGNMENT (2026-09-24), for every menu tool"; memory ct-frame-alignment-p2):
  * the cartridge side is the LOGICAL oracle (CLAUDE.md grades a lag frame against that mode; the real-time one has a
    lag frame inside C2/DACA);
  * the cartridge displays frame P's state during P+1 -- the NMI at the end of P uploads its tiles, sprites and
    brightness together -- so the port's frame P is the oracle's --peek after P+2 frames;
  * the cartridge reads a press a frame after the button goes down, so the port presses one frame after the oracle's
    drive (the talk A is a field press on the same frame on both sides).
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
CT = os.path.join(ROOT, 'build', 'ct.exe')
OSTATE = 'saves/field_re/shop90_lake.state'
PWRAM = 'saves/field_re/shop90.lakewram'
SHIFT = 2                     # the cartridge displays frame P's state during P+1 = the peek after P+2 frames
PRESS = -1                    # oracle press frame - PRESS = the port's walkscript press frame (the cartridge reads it a frame later)

# (button mask, oracle frame) -- the oracle drive, in LakeSnes serial masks (A=100, B=1, Right=80).
DRIVE = [(0x100, 10), (0x100, 162), (0x100, 204), (0x100, 246), (0x1, 282), (0x80, 312), (0x100, 342),
         (0x100, 384), (0x100, 426)]
KEY = {0x100: 'a', 0x1: 'b', 0x80: 'r'}
MOMENTS = [('main', 104), ('main-arrow', 112), ('buy-list-marle', 181), ('buy-list', 190), ('buy-list-pose', 200),
           ('buy-qty', 240),
           ('bought', 275), ('sell-list', 370), ('sell-qty', 410), ('sold', 450)]


def oracle_frame(frame, env=None):
    parts, t = [], 0
    for mask, f in DRIVE:
        if f + 2 > frame:
            break
        if f > t:
            parts.append('0:%d' % (f - t))
        parts.append('%X:2' % mask)
        t = f + 2
    parts.append('0:%d' % (frame - t))
    out = os.path.join(ROOT, 'build', '_shopcmp_o%d.raw' % frame)
    subprocess.run([CT, '--peek', OSTATE, out, '--btnscript', ','.join(parts)], cwd=ROOT,
                   env=dict(env or os.environ, CT_ORACLE_LOGICAL='1'),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


def port_frames(frames, extra_env=None):
    walk, t = [], 0
    for mask, f in DRIVE:
        pf = f if f == 10 else f - PRESS                 # the talk A is a FIELD press: same frame on both sides
        if pf > t:
            walk.append('n:%d' % (pf - t))
        walk.append('%s:1' % KEY[mask])
        t = pf + 1
    last = max(frames) + 2
    walk.append('n:%d' % max(1, last - t))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=os.path.join(ROOT, 'build', '_shopcmp_p'), CT_SHOTS_AT=','.join(str(f) for f in frames))
    if extra_env:
        env.update(extra_env)
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '5', '--loadwram', PWRAM, '--selftest',
                    os.path.join(ROOT, 'build', '_shopcmp.raw'), '--frames', str(last + 2), '--walkscript', ','.join(walk)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {f: os.path.join(ROOT, 'build', '_shopcmp_p_f%d.raw' % f) for f in frames}


def diff(of, pf):
    ow, oh, o = fb.load(of)
    pw, ph, p = fb.load(pf)
    n = 0
    for y in range(224):
        for x in range(256):
            i, j = ((y + 8) * ow + x) * 3, (y * pw + x) * 3
            if o[i:i + 3] != p[j:j + 3]:
                n += 1
    return n


def main():
    pfs = port_frames([o - SHIFT for _, o in MOMENTS])
    for name, o in MOMENTS:
        of = oracle_frame(o)
        pf = pfs[o - SHIFT]
        if not os.path.exists(pf) or not os.path.exists(of):
            print('shop-screen-cmp: %s -- a frame was not produced' % name)
            continue
        print('shop-screen-cmp: %s oracle f%d port f%d -> %d px differ' % (name, o, o - SHIFT, diff(of, pf)))
    # shop $11: the description line
    o = 190
    of = oracle_frame(o, env=dict(os.environ, PEEK_POKE='7F2866=91'))
    pf = port_frames([o - SHIFT], extra_env={'CT_SHOPNO': '11'})[o - SHIFT]
    print('shop-screen-cmp: shop11-description oracle f%d port f%d -> %d px differ' % (o, o - SHIFT, diff(of, pf)))


if __name__ == '__main__':
    main()
