#!/usr/bin/env python3
r"""The attract's second demo battle (loc 07D, Crono/Frog/Robo vs Hench/Vamp/Sorcerer): the colours each PC is drawn
with, against the cartridge's CGRAM.

    python tools/attract_07d_pcpal_cmp.py

The ROM has no per-character battle palette. CC/E6F8 gives seats 0/1/2 OBJ palettes 5/6/7 ($9888 = 0A/0C/0E), the
boot copies the base CGRAM page onto the live page (CC/E6EC) and writes each seat's weapon colours 13..15 from
$D1:4B00 (CC/E710-E753), and CF/E3C0 uploads the live page every NMI. The cartridge's OAM for Robo carries attribute
$3E = palette 7.

Port: the live battle path (`--scene 433`, no --selftest), CT_BATTLE_PCPALTRACE=1, the last palette printed per seat
before field frame 6900. Cartridge: saves/attract_07d_battle_f4540.state + 40 frames, CGRAM from the --peek .oam
sidecar (544 bytes OAM + high table, then 512 bytes CGRAM); OBJ palette p = CGRAM words 128 + 16p.
The state is made by a recipe (tools/fixtures/attract.json: fxa_attract_cena_inicial + pad 0:4451): attract frame
4540, loc 07D, 41 frames after the battle's `70 01`.

Regression check: attract-07d-pc-battle-palettes-vs-oracle.
"""
import os
import re
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_07d_battle_f4540.state'


def main():
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_BATTLE_PCPALTRACE='1', CT_AUDIO_PACE='7100')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env, capture_output=True,
                       text=True, errors='replace', timeout=900)
    last = {}
    for line in (r.stdout + r.stderr).splitlines():
        m = re.match(r'pcpal f(\d+) seat(\d) cid(\d+) slot(\d) ((?:[0-9A-F]{4} ?){16})', line)
        if m:
            last[int(m.group(2))] = (int(m.group(3)), int(m.group(4)), m.group(5).split())
    fb = os.path.join('build', '_pcpal07d.raw')
    subprocess.run([CT, '--peek', STATE, fb, '--frames', '40'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=900)
    try:
        side = open(fb + '.oam', 'rb').read()
    except OSError:
        sys.exit('attract-07d-pcpal: FAIL -- no oracle .oam sidecar')
    cg = side[1024 + 32:1024 + 32 + 512]
    for seat in range(3):
        if seat not in last:
            print('attract-07d-pcpal: seat%d FAIL -- the port drew no palette for this seat' % seat)
            continue
        cid, slot, words = last[seat]
        row = ['%04X' % (cg[(128 + slot * 16 + k) * 2] | (cg[(128 + slot * 16 + k) * 2 + 1] << 8)) for k in range(16)]
        # colour 0 is transparent on the SNES and never drawn; the grade is colours 1..15
        same = words[1:] == row[1:]
        print('attract-07d-pcpal: seat%d cid%d drawn with OBJ palette %d, colours 1..15 == cartridge CGRAM: %d'
              % (seat, cid, slot, 1 if same else 0))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
