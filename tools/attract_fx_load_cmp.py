#!/usr/bin/env python3
r"""Compare X-Strike's effect load in the port against the cartridge, byte for byte.

    python tools/attract_fx_load_cmp.py [<port_frame> <oracle_frame>]     (default 290 111)

The port runs the attract from its own boot (`--scene 433`) and, at battle tick <port_frame>, writes its $7E page and
its effect VRAM window with CT_BATTLE_FXDUMP (64 KB WRAM, then 0x800 bytes = VRAM $F800.., then 0x1000 = VRAM $A000..).
The oracle runs from saves/attract_demo_battle_f1850.state (the attract at its demo battle) for <oracle_frame> frames
and dumps .wram/.vram with --peek. Both frames sit after the load (port f214, oracle f22-f34) and before the program's
first block upload is replaced, so what is compared is what CD/0EBD built:
  blocks  WRAM $7E:D000-FFFF  -- CD/1052's 00EE fill + CD/107D's tile blocks (D1:A575 layout E6)
  sheets  VRAM $A000-$AFFF    -- the four CD/11E4 sheets (CF:8000 + X*64, X = 15 17 19 1B) and the i==3 zeroing
  map     VRAM $F800-$FFFF    -- CD/0FFD's DMA of the fill plus CD/0607's upload of block 0 at $7C00
  pal     WRAM $2032/$203A x3 and $2232/$223A x3 -- CGRAM 25-27 / 29-31 from D1:6002[$CA95 / $CA96] (base page; the
          live page is also the arena fade's, which the port reaches one frame apart, so only the base is compared)

Regression check: attract-demo-battle-xstrike-fx-load-vs-oracle.
"""
import os
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_demo_battle_f1850.state'


def main():
    pf, of = (int(sys.argv[1]), int(sys.argv[2])) if len(sys.argv) >= 3 else (290, 111)
    tmp = os.path.join('build', '_fxloadcmp')
    dump = tmp + '_port.bin'
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_BATTLE_FXDUMP='%s,%d' % (dump, pf))
    if os.path.exists(dump):
        os.remove(dump)
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433', '--frames', '3832', '--selftest'],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(dump):
        sys.exit('attract-fx-load-cmp: FAIL -- the port wrote no dump at battle tick %d' % pf)
    orc = tmp + '_orc.raw'
    subprocess.run([CT, '--peek', STATE, orc, '--frames', str(of)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p = open(dump, 'rb').read()
    ow = open(orc + '.wram', 'rb').read()
    ov = open(orc + '.vram', 'rb').read()
    pw, pmap, pchr = p[:0x10000], p[0x10000:0x10800], p[0x10800:0x11800]
    rows = [('blocks WRAM D000-FFFF', ow[0xD000:0x10000], pw[0xD000:0x10000]),
            ('sheets VRAM A000-AFFF', ov[0xA000:0xB000], pchr),
            ('map VRAM F800-FFFF', ov[0xF800:0x10000], pmap),
            ('pal base CGRAM 25-27 29-31', ow[0x2032:0x2038] + ow[0x203A:0x2040], pw[0x2032:0x2038] + pw[0x203A:0x2040])]
    for name, a, b in rows:
        same = sum(1 for i in range(len(a)) if a[i] == b[i])
        live = sum(1 for v in a if v)
        print('attract-fx-load-cmp: port f%d vs oracle f%d %s -> %d / %d identical (%d non-zero)' % (pf, of, name, same, len(a), live))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
