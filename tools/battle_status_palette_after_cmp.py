#!/usr/bin/env python3
r"""A status colour cycle after the fight: the PCs' OBJ palette rows on the field, port against the rule's cartridge.

    python tools/battle_status_palette_after_cmp.py

The Naga-ettes' fight (saves/field_re/m81_nuns_lake: scene 129, A mashed every 12 frames). Their kiss slows Crono and
Marle, and Slow is a colour cycle, not an icon: CF/E7D7 walks colours 1 and 12 of the seat's OBJ palette row ($993C,Y
<< 4 over the live page $2300) along CE/0100's ramp while $A46D,Y is set. The victory's stager C1/356D ends it for the
three seats (C1/357D-3587 `JSR C1/30B6`: $A46D cleared, the row's 26 bytes put back from the base page $2100), and the
field keeps the page after the ack. The port skipped those three calls until 2026-10-04, and a Slowed Crono walked the
field outlined in the ramp's last colour.

  cartridge  build/ct.exe --peek from saves/field_re/m81_nuns_lake.state (the rule), A every 12 frames, 10300 frames: the
             field after the ack (Crono and Marle slowed from c3911 / c5673 until the last enemy falls, c9733)
  port       build/Belthasar.exe --loadwram saves/field_re/m81_nuns_lake.lakewram, CT_BATTLE_MASHA, CT_DUMPWRAM at field
             f9000 (Crono slowed from the kiss to the victory)

    status-palette: OBJ<r> cartridge <16 words> | port <16 words> -> SAME | DIFFER
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_status_pal')
ROWS = (5, 6)          # Crono's and Marle's battle rows ($993C = 0A / 0C)


def row(w, r):
    return ' '.join('%04X' % (w[0x2300 + r * 32 + 2 * i] | (w[0x2301 + r * 32 + 2 * i] << 8)) for i in range(16))


def main():
    os.makedirs(OUT, exist_ok=True)
    frames = 10300
    seq, t = [], 0
    while t < frames:
        n = min(12, frames - t)
        seq.append('100:2,0:%d' % (n - 2) if n > 2 else '0:%d' % n)
        t += n
    craw = os.path.join(OUT, 'c.raw')
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1')
    subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', 'saves/field_re/m81_nuns_lake.state', craw,
                    '--btnscript', ','.join(seq)], cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pw = os.path.join(OUT, 'p.wram')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_MASHA='1',
               CT_BATTLE_DEBUGEND='20000', CT_DUMPWRAM=pw, CT_DUMPWRAM_AT='9000')
    subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '129',
                    '--loadwram', 'saves/field_re/m81_nuns_lake.lakewram', '--selftest', os.path.join(OUT, 'p.raw'),
                    '--frames', '9002'], cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(craw + '.wram') or not os.path.exists(pw):
        print('status-palette: FAILED -- a side wrote no WRAM')
        return 1
    cw, pw_ = open(craw + '.wram', 'rb').read(), open(pw, 'rb').read()
    print('status-palette: cartridge loc %02X%02X, port loc %02X%02X' % (cw[0x101], cw[0x100], pw_[0x101], pw_[0x100]))
    for r in ROWS:
        a, b = row(cw, r), row(pw_, r)
        print('status-palette: OBJ%d cartridge %s | port %s -> %s' % (r, a, b, 'SAME' if a == b else 'DIFFER'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
