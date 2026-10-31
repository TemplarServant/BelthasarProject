#!/usr/bin/env python3
r"""The demo battles' colours against the cartridge: the entry frame and the frames around a tech's effect.

    python tools/attract_battle_colour_cmp.py [port_frames [cart_frames]]

The optional comma lists replace the port's (and the cartridge's) frames, in PAIRS order; the port's numbering moves
with the attract's clock. One interactive attract run (--scene 433, CT_AUDIO_LOCKSTEP=1) shoots the port's frames;
ct.exe --peek shoots the cartridge's under the rule (CT_ORACLE_LOGICAL / VIRTUAL_BEAM, from
saves/attract_cena_inicial_rule.state: the attract from power-on + 2159, made by the recipe
tools/fixtures/attract_rule.json), and each pair's box is compared (disasm/notes/battle_arena_cgram.md). The pairs
sit on the fights' picture diagonal: port = cartridge + 2066 (P+1), + 2067 in 087's, whose visit is a frame longer in
the port.

  0AD entry     the battle's first frame, the PCs' box (Crono, (112,128)-(128,194)): the first compose must not read
                h->fxCgram before a battle NMI has filled it (black PCs for one frame).
  07D mid-tech  the whole screen, through CD/0E79's fade of CGRAM $10-$1F down at the effect load.
  087 darken    the Ice Sword's own records $0580/$058C (mode 50 over CGRAM $10-$7F) darkening the arena's BG1/BG2 --
                what the arena baked from the battle's page draws.
  07D after     CD/0453's CD/0E61 fades $10-$1F back up -- only those colours.
  07D glow      before the tech the candles' BG3 glow is added (CGWSEL 02 CGADSUB 03); during the tech CGWSEL 00, no
                sub screen added (the glow gone, not green).
  087 after     no record at all ($2C66 = 13, C0/6DCF's $01BB).

The regression check pins the counts exactly, so a change in either direction is seen.

Regression check: attract-battle-colours-entry-and-tech-vs-oracle.
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial_rule.state'
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}
PAIRS = [   # (label, port frame, cart frame, box)
    ('0AD entry, the PCs', 3807, 1741, (112, 128, 128, 194)),
    ('07D before the tech', 6594, 4528, (0, 0, 256, 224)),
    ('07D mid-tech', 6670, 4604, (0, 0, 256, 224)),
    ('07D mid-tech', 6754, 4688, (0, 0, 256, 224)),
    ('07D effect load, faded', 6608, 4542, (0, 0, 256, 224)),
    ('087 Ice Sword darken', 9411, 7344, (0, 0, 256, 224)),
    ('07D after the tech', 7092, 5026, (0, 0, 256, 224)),
    ('07D after the tech', 7100, 5034, (0, 0, 256, 224)),
    ('087 after the tech', 9779, 7712, (0, 0, 256, 224)),
    ('087 after the tech', 9787, 7720, (0, 0, 256, 224)),
]


def main():
    global PAIRS
    # an optional comma list replaces the port frames, in PAIRS order (the port's numbering moves with the attract's
    # clock); a second one the cartridge's
    if len(sys.argv) > 1:
        pfs = [int(v) for v in sys.argv[1].split(',')]
        PAIRS = [(l, pf, c, b) for (l, _, c, b), pf in zip(PAIRS, pfs)]
    if len(sys.argv) > 2:
        cfs = [int(v) for v in sys.argv[2].split(',')]
        PAIRS = [(l, p, cf, b) for (l, p, _, b), cf in zip(PAIRS, cfs)]
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
                                      env=dict(os.environ, **RULE), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
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
