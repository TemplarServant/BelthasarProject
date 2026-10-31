#!/usr/bin/env python3
r"""The Roly Rider's death dissolve, port against the rule's cartridge (scene 119's ambush, a layout-type-1 enemy).

    python tools/roly_dissolve_cmp.py

saves/battle_re/roly_rider_s119.state (recipe twin tools/recipes/fxr_roly_s119.json, from the playthrough's Guardia
Forest): Crono in scene 119 above obj30's trigger tile; Down 40 walks into it and the ambush stages three Roly Riders
(id 13, type 1, slots 0/4/6). roly_rider_battle.state is the same run 240 frames on. Crono attacks (A, A, every 144
frames); his third blow kills slot 0 (C1:ED12; cartridge f708 of the battle state, the port's field f949). The port
replays the drive from the s119 WRAM (roly_rider_s119.lakewram) with its keys one frame after the cartridge's
(`.:1,d:40`, the frame alignment in docs/TESTING.md); its fight script starts at its battle frame 120.

Per frame from the kill, the dying Roly's box (around its foot at the kill) is compared with the same box once it is
gone (the cartridge's +60): the count of pixels that still differ is how much of the corpse is drawn. A dissolve holds
the count, then thins it to 0; a corpse that vanishes on the blow reads 0 from the first frame. The port's +k is set
against the cartridge's +k+1: inside a fight the pictures align at P+1 (docs/TESTING.md). CD/0043's 43 frames:
disasm/notes/battle_death_dissolve_order.md.
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
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_roly')
CSTATE = 'saves/battle_re/roly_rider_battle.state'
PSEED = 'saves/battle_re/roly_rider_s119.lakewram'
CKILL, PKILL = 708, 949            # the kill frame on each side (cartridge: the battle state's; port: the field's)
OFFS = [0, 2, 3, 10, 12, 20, 30, 40, 42, 43]
PICT = 1                      # the cartridge's picture of the port's frame P is its --peek P+1 (in a fight)
# the dying Roly's box in the port's picture (the oracle's is 8 lines lower): around its foot (slot 0 = combatant 3,
# $1D0F / $1D26 on the cartridge at the kill), -17..+17 across and -26..+10 down
BOX_OFF = (-17, -26, 17, 10)
DRIVE = ','.join(['100:2,0:10,100:2,0:120'] * 6)


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
    scr = 'n:120,' + ','.join(['a:2,n:10,a:2,n:120'] * 6)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_SCRIPT_ABS='1',
               CT_BATTLE_SCRIPT=scr, CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(map(str, pf)),
               CT_SHOTS_EXIT='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '119', '--loadwram', PSEED, '--walkscript', '.:1,d:40'],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def port_frame(f):
        d = open(os.path.join(OUT, 'p_f%d.raw' % f), 'rb').read()
        w, h = np.frombuffer(d[:8], np.uint32)
        return np.frombuffer(d[8:8 + w * h * 3], np.uint8).reshape(h, w, 3).astype(np.int16)

    cart_frame(CKILL)
    wr = open(os.path.join(OUT, 'c%d.raw.wram' % CKILL), 'rb').read()
    fx, fy = wr[0x1D0C + 3], wr[0x1D23 + 3]
    x0, y0, x1, y1 = fx + BOX_OFF[0], fy + BOX_OFF[1], fx + BOX_OFF[2], fy + BOX_OFF[3]
    cend = cart_frame(CKILL + 60)[y0 + 8:y1 + 8, x0:x1]          # (the oracle's picture starts 8 lines higher)
    pend = port_frame(PKILL + 60)[y0:y1, x0:x1]
    for k in OFFS:
        c = cart_frame(CKILL + k + PICT)[y0 + 8:y1 + 8, x0:x1]
        p = port_frame(PKILL + k)[y0:y1, x0:x1]
        cn = int((np.abs(c - cend).max(axis=2) > 24).sum())
        pn = int((np.abs(p - pend).max(axis=2) > 24).sum())
        print('roly-dissolve: +%d cartridge %d port %d' % (k, cn, pn))
    return 0


if __name__ == '__main__':
    sys.exit(main())
