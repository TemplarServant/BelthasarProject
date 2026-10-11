#!/usr/bin/env python3
"""src/data/menu_tech.h -- the Tech screen's three Single/Dual/Triple tabs, from the oracle's VRAM and CGRAM.

The tabs are BG1 tiles 0x164..0x16F (char base 0, 4bpp: tile N at VRAM byte N*32), four per tab {TL,TR,BL,BR}; the
selected tab takes CGRAM row 4, the others row 5 (colours as the PPU expands them, (v<<3)|(v>>2)). The capture: the X
menu open in front of GATO (recipe menu_open_gato2, standing in for saves/menu_open_gato2.state), Right x2 (the Tech
icon) and A (the screen), 40 frames on.

    python tools/gen/menu_tech.py [--state S] [--norule] [--out src/data/menu_tech.h]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, recipe, peek, scratch, cgram_of  # noqa: E402
from menugfx import tile4, rgb  # noqa: E402

DRIVE = '80:2,0:4,80:2,0:4,100:2,0:40'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a state with the X menu open instead of the recipe (saves/menu_open_gato2.state)')
    ap.add_argument('--norule', action='store_true', help='run the oracle without the rule (the old captures)')
    ap.add_argument('--out', default=os.path.join('src', 'data', 'menu_tech.h'))
    a = ap.parse_args()
    out = peek(a.state or recipe('menu_open_gato2'), scratch('menu_tech'), DRIVE, rule=not a.norule)
    vram = open(out + '.raw.vram', 'rb').read()
    cg = cgram_of(out + '.raw')
    L = ["// gen_tech.h - AUTO-EXTRACTED from oracle VRAM (tech_enter state: menu_open_gato2 + Right x2 + A).",
         "// The three 'Single/Dual/Triple' category TABS at the tech screen's top-right.",
         "// Each tab = 2x2 = 16x16 px, four 8x8 4bpp tiles in order {TL,TR,BL,BR} (VRAM tiles 0x164..0x16F).",
         "// Palette: SELECTED tab -> techTabPal[0] (CGRAM row4), UNSELECTED -> techTabPal[1] (CGRAM row5).",
         "// Pixel index 0 = transparent (shows panel bg). Indices 1..15 -> techTabPal[sel][idx].",
         "#ifndef GEN_TECH_H", "#define GEN_TECH_H", "#include <stdint.h>",
         "// 12 tiles x 64 nibble-indices (row-major 8x8). Groups of 4 = tab 1,2,3.",
         "static const uint8_t techTabTiles[12][64] = {"]
    for n in range(0x164, 0x170):
        L.append('  {%s}, // tile 0x%03X' % (','.join(str(v) for v in tile4(vram, 0, n)), n))
    L += ["};", "// [0]=selected(CGRAM row4), [1]=unselected(row5); 16 colors RGB888 (idx0 transparent).",
          "static const uint8_t techTabPal[2][16][3] = {"]
    for row in (4, 5):
        L.append('  { %s },' % ', '.join('{%d,%d,%d}' % rgb(cg, row * 16 + i) for i in range(16)))
    L += ["};", "// Screen placement (OUR 256x224 coords): tab i top-left = (144 + i*32, 24), size 16x16.",
          "static const uint16_t techTabX[3] = {144,176,208};", "#define TECH_TAB_Y 24", "#endif"]
    path = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    open(path, 'w', newline='\n').write('\n'.join(L))
    print('wrote %s' % a.out)


if __name__ == '__main__':
    main()
