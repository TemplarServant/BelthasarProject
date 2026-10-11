#!/usr/bin/env python3
"""src/data/menu_arrow.h -- the ribbon's selection arrow, from the oracle's OAM, VRAM and CGRAM.

OBJ tile 0x22 (16x16: 0x22, 0x23, 0x32, 0x33 at OBJ char base VRAM byte 0x8000) with OBJ palette 7 (CGRAM 240..255
as the PPU expands it, (v<<3)|(v>>2)) -- the two sprites the ribbon puts under and (V-flipped) over the highlighted
icon, OAM 6 at Y=128 and OAM 7 at Y=96. The capture: the X menu open in front of GATO (recipe menu_open_gato2,
standing in for saves/menu_open_gato2.state), two frames on.

    python tools/gen/menu_arrow.py [--state S] [--norule] [--out src/data/menu_arrow.h]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, recipe, peek, scratch, cgram_of  # noqa: E402
from menugfx import rgb, block16  # noqa: E402

DRIVE = '0:2'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a state with the X menu open instead of the recipe (saves/menu_open_gato2.state)')
    ap.add_argument('--norule', action='store_true', help='run the oracle without the rule (the old captures)')
    ap.add_argument('--out', default=os.path.join('src', 'data', 'menu_arrow.h'))
    a = ap.parse_args()
    out = peek(a.state or recipe('menu_open_gato2'), scratch('menu_arrow'), DRIVE, rule=not a.norule)
    vram = open(out + '.raw.vram', 'rb').read()
    oam = open(out + '.raw.oam', 'rb').read()
    cg = cgram_of(out + '.raw')
    s = oam[6 * 4:6 * 4 + 4]
    if s[1] != 128 or s[2] != 0x22 or (s[3] & 0x0F) != 0x0E:
        sys.exit('menu_arrow: OBJ 6 is not the ribbon arrow (Y=128, tile 22, pal 7): %s' % s.hex())
    idx = block16(vram, 0x8000, 0x22, 0x23, 0x32, 0x33)
    L = ['// menu_arrow.h - the ribbon SELECTION cursor arrow (the ▼/▲ straddling the highlighted icon).',
         '// The oracle OBJ sprite tile 0x22 (16x16, OBJ pal7), extracted from VRAM. It is a filled up-pointing',
         '// triangle (▲) occupying rows 1-12 of the 16x16 cell (rows 0,13-15 transparent) -- so blitting the',
         '// whole cell at the OAM Y auto-positions the tip next to the icon. Bottom arrow = as-is at OAM Y=128;',
         '// top arrow = vertically flipped (▼) at OAM Y=96; both at X=81+opt*16. RGBA8888, A=0 transparent.',
         "// The mid grey is BGR555 (13,12,12) through the framebuffer's (v<<3)|(v>>2) = 107,99,99 (it was baked as",
         "// 106,98,98 = v*255/31); measured 2026-09-24 on the oracle's ribbon and shop frames.",
         '#ifndef CT_MENU_ARROW_H', '#define CT_MENU_ARROW_H', '#include <stdint.h>',
         'static const uint8_t ctMenuArrow[16*16*4] = {']
    for y in range(16):
        L.append('  ' + ''.join('%d,%d,%d,%d,' % ((0, 0, 0, 0) if i == 0 else rgb(cg, 128 + 7 * 16 + i) + (255,))
                                for i in idx[y * 16:y * 16 + 16]))
    L += ['};', '#endif', '']
    path = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    open(path, 'w', newline='\n', encoding='utf-8').write('\n'.join(L))
    print('wrote %s' % a.out)


if __name__ == '__main__':
    main()
