#!/usr/bin/env python3
r"""The Roly Rider's death dissolve, port against the rule's cartridge (scene 119's ambush, a layout-type-1 enemy).

    python tools/roly_dissolve_cmp.py

saves/battle_re/roly_rider_s119.state: the cartridge `--teleport 119 8 10 2` from saves/jogo_trava_lake.state (above
obj30's trigger tile); Down 40 walks into it and the ambush stages three Roly Riders (id 13, type 1, slots 0/4/6).
roly_rider_battle.state is the same run 240 frames on. Crono attacks (A, A); his first blow kills slot 0 (C1:ED12,
cartridge f324 of the battle state = f564 of the s119 state).
The s119 state was rebuilt on 2026-10-12 by the same teleport (the first one was lost; tools/regress_inputs.json):
the cartridge's side reproduces the original numbers exactly, and the port's kill lands one frame earlier on it (f564). The port replays the same drive from the s119 WRAM
(roly_rider_s119.lakewram).

Per frame from the kill, the dying Roly's box (84,26)-(118,62) is compared with the same box once it is gone (the
cartridge's +60): the count of pixels that still differ = how much of the corpse is drawn. A dissolve shows the
count holding, then thinning to 0; a corpse that vanishes on the blow reads 0 from the first frame.

    roly-dissolve: +<k> cartridge <n> port <m>
"""
import os
import subprocess
import sys

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
OUT = os.path.join(ROOT, 'build', '_roly')
CSTATE = 'saves/battle_re/roly_rider_battle.state'
PSEED = 'saves/battle_re/roly_rider_s119.lakewram'
CKILL, PKILL = 324, 564            # the kill frame on each side (cartridge: the battle state's; port: the field's)
OFFS = [0, 2, 10, 20, 30, 40]
BOX = (84, 26, 118, 62)       # the port's picture; the oracle's PNG is 8 lines lower
DRIVE = ','.join(['100:2,0:10,100:2,0:120'] * 3)


def cart_input(f):
    out, t = [], 0
    for part in DRIVE.split(','):
        b, n = part.split(':'); n = int(n)
        if t + n >= f:
            out.append('%s:%d' % (b, f - t)); t = f
            break
        out.append(part); t += n
    if t < f:
        out.append('0:%d' % (f - t))
    return ','.join(out)


def cart_frame(f):
    raw = os.path.join(OUT, 'c%d.raw' % f)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1')
    subprocess.run([CT, '--peek', CSTATE, raw, '--btnscript', cart_input(f)], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w, h, rgb = fb.load(raw)                                                   # ctlib/fb: the 2x-downsampled 256x240
    return np.frombuffer(bytes(rgb), np.uint8).reshape(h, w, 3).astype(np.int16)


def main():
    os.makedirs(OUT, exist_ok=True)
    pf = [PKILL + k for k in OFFS] + [PKILL + 60]
    scr = 'n:116,' + ','.join(['a:2,n:10,a:2,n:120'] * 3)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_SCRIPT_ABS='1',
               CT_BATTLE_SCRIPT=scr, CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(map(str, pf)),
               CT_SHOTS_EXIT='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '119', '--loadwram', PSEED, '--walkscript', 'd:40'],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def port_frame(f):
        d = open(os.path.join(OUT, 'p_f%d.raw' % f), 'rb').read()
        w, h = np.frombuffer(d[:8], np.uint32)
        return np.frombuffer(d[8:8 + w * h * 3], np.uint8).reshape(h, w, 3).astype(np.int16)

    x0, y0, x1, y1 = BOX
    cend = cart_frame(CKILL + 60)[y0 + 8:y1 + 8, x0:x1]          # (the oracle's picture starts 8 lines higher)
    pend = port_frame(PKILL + 60)[y0:y1, x0:x1]
    for k in OFFS:
        c = cart_frame(CKILL + k)[y0 + 8:y1 + 8, x0:x1]
        p = port_frame(PKILL + k)[y0:y1, x0:x1]
        cn = int((np.abs(c - cend).max(axis=2) > 24).sum())
        pn = int((np.abs(p - pend).max(axis=2) > 24).sum())
        print('roly-dissolve: +%d cartridge %d port %d' % (k, cn, pn))
    return 0


if __name__ == '__main__':
    sys.exit(main())
