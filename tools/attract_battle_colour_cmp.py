#!/usr/bin/env python3
r"""The demo battles' colours against the cartridge: the entry frame and the frames around a tech's effect.

    python tools/attract_battle_colour_cmp.py

The user, 2026-09-27: "when the battle with flea starts in the demo, the characters vanish for 1 or more frames" and "after
the frog's skill ends in Magus's castle, and the ice skill on zombor, the screen is darkening for a moment"
(disasm/notes/battle_arena_cgram.md). One interactive attract run (--scene 433, CT_AUDIO_LOCKSTEP=1) shoots the port's
frames; ct.exe --peek shoots the cartridge's (saves/attract_cena_inicial.state), and each pair's box is compared:

  0AD entry     port f3810 = the battle's first frame, cart c1744: the PCs' box (Crono, (112,128)-(128,194)). The
                first compose read h->fxCgram before any battle NMI filled it -- zeros: black PCs for one frame.
  07D mid-tech  port f6708 / cart c4700, the whole screen, and port f6659 / cart c4648: CD/0E79's fade of CGRAM $10-$1F
                down at the effect load (armed port f6631 / cart c4633; c4648 differs from the lit c4630 by 1062 px).
  087 darken    port f9503 / cart c7505: the Ice Sword's own records $0580/$058C (mode 50 over CGRAM $10-$7F, cartridge
                c7493-c7523) darkening the ARENA's BG1/BG2 -- what the arena baked from the battle's page draws.
  07D after     port f7171 / c5145 and f7179 / c5153: CD/0453's CD/0E61 fades $10-$1F back up -- only those colours.
  07D glow      port f6664 / c4630 (before the tech: the candles' BG3 glow added, CGWSEL 02 CGADSUB 03) and f6731 / c4700,
                f6831 / c4800 (the tech: CGWSEL 00, no sub screen added -- the glow gone, not green; the user's item #6).
  087 after     port f9900 / c7889 and f9908 / c7897: no record at all ($2C66 = 13, C0/6DCF's $01BB).

The counts are the port's residual against the cartridge at those frames (the 07D's candle glow and sprites, the 087's
~1300 px of Zombor's area) -- pinned exactly, so a change in either direction is seen.
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/ct_field.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial.state'
PAIRS = [   # (label, port frame, cart frame, box)
    ('0AD entry, the PCs', 3810, 1744, (112, 128, 128, 194)),
    ('07D before the tech', 6639, 4630, (0, 0, 256, 224)),
    ('07D mid-tech', 6708, 4700, (0, 0, 256, 224)),
    ('07D mid-tech', 6808, 4800, (0, 0, 256, 224)),
    ('07D effect load, faded', 6659, 4648, (0, 0, 256, 224)),
    ('087 Ice Sword darken', 9503, 7505, (0, 0, 256, 224)),
    ('07D after the tech', 7148, 5145, (0, 0, 256, 224)),
    ('07D after the tech', 7156, 5153, (0, 0, 256, 224)),
    ('087 after the tech', 9878, 7889, (0, 0, 256, 224)),
    ('087 after the tech', 9886, 7897, (0, 0, 256, 224)),
]


def main():
    global PAIRS
    # an optional comma list replaces the port frames, in PAIRS order: the port's numbering moves with the attract's clock
    # (the rule's migration: the fights' boots and returns under the infinitely fast CPU), the cartridge's does not
    if len(sys.argv) > 1:
        pfs = [int(v) for v in sys.argv[1].split(',')]
        PAIRS = [(l, pf, c, b) for (l, _, c, b), pf in zip(PAIRS, pfs)]
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    tmp = os.path.join('build', '_battlecolour')
    os.makedirs(tmp, exist_ok=True)
    pre = os.path.join(tmp, 'p')
    frames = sorted({p for _, p, _, _ in PAIRS})
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_SHOTS=pre, CT_SHOTS_AT=','.join(map(str, frames)), CT_SHOTS_EXIT='1')
    procs = [subprocess.Popen([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)]
    for _, _, c, _ in PAIRS:
        procs.append(subprocess.Popen([CT, '--peek', STATE, os.path.join(tmp, 'o%d.raw' % c), '--frames', str(c)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    for pr in procs:
        pr.wait()
    for label, pf, cf, (x0, y0, x1, y1) in PAIRS:
        ps, os_ = '%s_f%d.raw' % (pre, pf), os.path.join(tmp, 'o%d.raw' % cf)
        if not (os.path.exists(ps) and os.path.exists(os_)):
            sys.exit('battle-colours: REFUSING -- a shot is missing (port f%d / cart c%d)' % (pf, cf))
        d = open(ps, 'rb').read(); w = struct.unpack('<I', d[:4])[0]; port = d[8:]
        o = open(os_, 'rb').read()
        same = total = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                i = ((y + 8) * 2 * 512 + x * 2) * 4
                j = (y * w + x) * 3
                total += 1
                same += (o[i + 3], o[i + 2], o[i + 1]) == (port[j], port[j + 1], port[j + 2])
        print('battle-colours: %s, port f%d vs cart c%d box (%d,%d)-(%d,%d): %d / %d identical'
              % (label, pf, cf, x0, y0, x1, y1, same, total))


if __name__ == '__main__':
    main()
