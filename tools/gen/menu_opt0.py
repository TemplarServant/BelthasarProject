#!/usr/bin/env python3
"""src/data/menu_opt0.h -- the Equip screen's equip-type icons, from the oracle's VRAM and CGRAM.

BG1 tiles 0x20..0x29 (char base 0, 4bpp: tile N at VRAM byte N*32; the icons use colours 0-3) with CGRAM row 0 as the
PPU expands it, (v<<3)|(v>>2) -- the icon the ROM draws left of an item's name is byte 0 of its $CC:0B5E record. The
capture: the X menu open in front of GATO (recipe menu_open_gato2, standing in for saves/menu_open_gato2.state), A (the
Equip screen), 40 frames on.

    python tools/gen/menu_opt0.py [--state S] [--norule] [--out src/data/menu_opt0.h]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, recipe, peek, scratch, cgram_of  # noqa: E402
from menugfx import tile4, rgb  # noqa: E402

DRIVE = '100:2,0:40'
SINGLE = [('weapon', 0x20), ('helm', 0x27), ('armor', 0x28), ('accessory', 0x29)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a state with the X menu open instead of the recipe (saves/menu_open_gato2.state)')
    ap.add_argument('--norule', action='store_true', help='run the oracle without the rule (the old captures)')
    ap.add_argument('--out', default=os.path.join('src', 'data', 'menu_opt0.h'))
    a = ap.parse_args()
    out = peek(a.state or recipe('menu_open_gato2'), scratch('menu_opt0'), DRIVE, rule=not a.norule)
    vram = open(out + '.raw.vram', 'rb').read()
    cg = cgram_of(out + '.raw')
    tiles = {n: tile4(vram, 0, n) for n in range(0x20, 0x2A)}
    pal = []
    for i in range(16):
        r, g, b = rgb(cg, i)
        pal.append(0 if i == 0 else 0xFF000000 | b << 16 | g << 8 | r)
    L = ['// gen_opt0.h  -- opt0 (Equip/Status) DETAIL graphics extracted from the oracle.',
         '// (The portraits and element plaques are decoded from the ROM itself: menu.c mDetailArt, C2/EC38 + C2/A273.)',
         '// Format: RGBA8888 rows. Color index 0 -> (0,0,0,0) transparent. Palettes resolved from oracle CGRAM.',
         '#ifndef GEN_OPT0_H', '#define GEN_OPT0_H', '#include <stdint.h>', '// RGBA packed little-endian as 0xAABBGGRR',
         '', '// Equip-type icons (BG1 char base0, PALETTE 0), drawn at col17/x136 left of each equip name.']
    for name, n in SINGLE:
        L.append('static const uint8_t gen_opt0_icon_%s[8*8] = { %s };  // tile 0x%02X'
                 % (name, ','.join(str(v) for v in tiles[n]), n))
    L.append('static const uint32_t gen_opt0_pal0[16] = { %s };  // 0xAABBGGRR' % ', '.join('0x%08X' % c for c in pal))
    L += ['', "// The 10 equip-TYPE icon tiles 0x20-0x29 (BG1 pal0), indexed by (tile-0x20): the ROM draws an item's",
          '// icon = BYTE 0 of its $CC:0B5E name record (0x20 sword/0x21 bow/0x22 gun/0x23 arm/0x24 katana/0x25 fist/',
          '// 0x26 scythe/0x27 helm/0x28 armor/0x29 accessory). Extracted from the oracle VRAM (0x20/27/28/29 verified',
          '// byte-identical to the prior single icons).',
          'static const uint8_t gen_opt0_icon_tiles[10][8*8] = {']
    for n in range(0x20, 0x2A):
        L.append('  { %s }, // 0x%02X' % (','.join(str(v) for v in tiles[n]), n))
    L += ['};', '#endif', '']
    path = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    open(path, 'w', newline='\n').write('\n'.join(L))
    print('wrote %s' % a.out)


if __name__ == '__main__':
    main()
