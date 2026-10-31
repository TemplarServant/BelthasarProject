#!/usr/bin/env python3
r"""Compare Flea's entrance pose in the attract's demo battle (loc 0AD) with the cartridge, pixel for pixel.

    python tools/attract_flea_cmp.py [<port_frame> <oracle_frame>]      (default 3716 50)

In this pose her OBJ tiles overlap: her face (tiles 0-3) is drawn over her white cape (tiles 4-7). On the SNES a
lower OAM slot wins, and the ROM copies a frame's tiles into ascending slots in tile order (oam.md: tile# =
$0D00 + t*2), so tile 0 is in front -- the oracle's OAM at this pose has Flea at slots 40..47 = tiles 080..08E.
The port composes a frame into one bitmap (src/scene.c CtComposeSprite); if a later unit overwrites an earlier
one, the face disappears under the cape.

The port runs the attract from its own boot (`--scene 433 --selftest`) and dumps the composed field frame at
<port_frame> with CT_FIELDSHOT. The oracle runs the cartridge under the rule (CT_ORACLE_LOGICAL / VIRTUAL_BEAM) from
saves/attract_loc0AD_flea_rule.state (the attract from power-on + 3758, at loc 0AD before Flea's entrance, made by a
recipe: tools/fixtures/attract_rule.json) for <oracle_frame> frames and dumps its framebuffer with --peek. Oracle
frame rows sit 8 lines below the port's (the 240-line framebuffer), which the throne -- a static BG feature above
her -- confirms. Compared: the box around Flea, x 104..139, port y 62..107.

Regression check: attract-flea-entrance-vs-oracle.
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_loc0AD_flea_rule.state'
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}
X0, X1, Y0, Y1, DY = 104, 140, 62, 108, 8


def main():
    pf, of = (int(sys.argv[1]), int(sys.argv[2])) if len(sys.argv) >= 3 else (3716, 50)
    shot = os.path.join('build', '_fleacmp_port_%d.shot' % os.getpid())
    orc = os.path.join('build', '_fleacmp_orc_%d.raw' % os.getpid())
    for p in (shot, orc):
        if os.path.exists(p):
            os.remove(p)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_FIELDSHOT=shot, CT_FIELDSHOT_AT=str(pf))
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433', '--frames', str(pf + 10), '--selftest'],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([CT, '--peek', STATE, orc, '--frames', str(of)], env=dict(os.environ, **RULE),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(shot) or not os.path.exists(orc):
        sys.exit('attract-flea-cmp: FAIL -- missing dump (port %s, oracle %s)' % (os.path.exists(shot), os.path.exists(orc)))
    p = open(shot, 'rb').read()
    w, h = struct.unpack('<II', p[:8])
    pb = p[8:]
    o = open(orc, 'rb').read()
    diff = total = opaque = 0
    for y in range(Y0, Y1):
        for x in range(X0, X1):
            k = ((2 * (y + DY)) * 512 + 2 * x) * 4
            a = (o[k + 3], o[k + 2], o[k + 1])
            kk = (y * w + x) * 3
            b = tuple(pb[kk:kk + 3])
            total += 1
            diff += a != b
    # a reference that is not the sprite: the throne just above her must agree too (x 112..139, y 50..61 -- the
    # starfield around it drifts in phase against the cartridge, and one star shows through at (111,59), so it is
    # deliberately outside both boxes)
    tdiff = 0
    for y in range(50, 62):
        for x in range(112, 140):
            k = ((2 * (y + DY)) * 512 + 2 * x) * 4
            kk = (y * w + x) * 3
            tdiff += (o[k + 3], o[k + 2], o[k + 1]) != tuple(pb[kk:kk + 3])
    print('attract-flea-cmp: port f%d vs cartridge f%d -> Flea box %d / %d px differ' % (pf, of, diff, total))
    print('attract-flea-cmp: throne box %d px differ' % tdiff)


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
