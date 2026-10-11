#!/usr/bin/env python3
r"""Crono's afterimage in the attract's Ice Sword (loc 087) against the cartridge.

    python tools/attract_afterimage_cmp.py [p:c,p:c,...]

The optional argument replaces the (port frame, cartridge frame) pairs -- the port's numbering moves with the attract's
clock (the rule's migration: the 087 fight -73 frames under the infinitely fast CPU), the cartridge's does not.

The user, 2026-09-27: "in the skill on zombor, crono is not leaving a 'trail'" (disasm/notes/battle_afterimage.md). The
tech's script arms the trail on Crono's track with `A4 05 03` (C1/691B); CF/ED18 records his point every 5 frames and
CF/E92C draws his current cells at the four recorded points on one frame in 3 (cartridge c7643, 7646, ... 7679). One
interactive attract run (--scene 433, CT_AUDIO_LOCKSTEP=1) shoots the port's frames, ct.exe --peek the cartridge's
(saves/attract_cena_inicial.state), port = cartridge + 1979 here; each pair's box (40,0)-(110,130) -- Crono's dash and
the copies behind him -- is compared.
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/ct_field.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial.state'
PAIRS = [(9653, 7658), (9659, 7664), (9665, 7670)]
BOX = (40, 0, 110, 130)


def main():
    global PAIRS
    if len(sys.argv) > 1:
        PAIRS = [tuple(int(v) for v in pc.split(':')) for pc in sys.argv[1].split(',')]
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    tmp = os.path.join('build', '_afterimage')
    os.makedirs(tmp, exist_ok=True)
    pre = os.path.join(tmp, 'p')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_SHOTS=pre, CT_SHOTS_AT=','.join(str(p) for p, _ in PAIRS), CT_SHOTS_EXIT='1')
    procs = [subprocess.Popen([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)]
    for _, c in PAIRS:
        procs.append(subprocess.Popen([CT, '--peek', STATE, os.path.join(tmp, 'o%d.raw' % c), '--frames', str(c)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    for pr in procs:
        pr.wait()
    x0, y0, x1, y1 = BOX
    for pf, cf in PAIRS:
        ps, os_ = '%s_f%d.raw' % (pre, pf), os.path.join(tmp, 'o%d.raw' % cf)
        if not (os.path.exists(ps) and os.path.exists(os_)):
            sys.exit('afterimage: REFUSING -- a shot is missing (port f%d / cart c%d)' % (pf, cf))
        d = open(ps, 'rb').read(); w = struct.unpack('<I', d[:4])[0]; port = d[8:]
        o = open(os_, 'rb').read()
        same = total = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                i = ((y + 8) * 2 * 512 + x * 2) * 4
                j = (y * w + x) * 3
                total += 1
                same += (o[i + 3], o[i + 2], o[i + 1]) == (port[j], port[j + 1], port[j + 2])
        print('afterimage: port f%d vs cart c%d box (%d,%d)-(%d,%d): %d / %d identical' % (pf, cf, x0, y0, x1, y1, same, total))


if __name__ == '__main__':
    main()
