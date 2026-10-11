#!/usr/bin/env python3
"""src/data/menu_icons.h -- the X menu's six ribbon icons, from the oracle's VRAM and CGRAM.

OBJ tiles (char base VRAM byte 0x8000; a 16x16 icon = t, t+1, t+16, t+17): 04..0E are the grey unselected icons,
24..2E the selected ones. CT loads a different OBJ palette 0 per selection (each icon's own colours), so each
highlight is decoded with OBJ palette 0 of the frame where THAT icon is selected; the greys with the first one's (the
greys' colours are the same in all six). Colours as the PPU expands them, (v<<3)|(v>>2). disasm/notes/menu_phase2.md.
The captures: the X menu open in front of GATO (recipe menu_open_gato2, standing in for saves/menu_open_gato2.state;
icon 0 selected), then Right x i for icon i.

    python tools/gen/menu_icons.py [--state S] [--norule] [--out src/data/menu_icons.h]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, recipe, peek, scratch, cgram_of  # noqa: E402
from menugfx import rgb, block16  # noqa: E402


def icon(vram, cg, t):
    out = []
    for i in block16(vram, 0x8000, t, t + 1, t + 16, t + 17):
        out += (0, 0, 0, 0) if i == 0 else rgb(cg, 128 + i) + (255,)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a state with the X menu open instead of the recipe (saves/menu_open_gato2.state)')
    ap.add_argument('--norule', action='store_true', help='run the oracle without the rule (the old captures)')
    ap.add_argument('--out', default=os.path.join('src', 'data', 'menu_icons.h'))
    a = ap.parse_args()
    st = a.state or recipe('menu_open_gato2')
    base, sel = None, []
    for i in range(6):
        out = peek(st, scratch('menu_icons%d' % i), '(80:2,0:8)*%d,0:12' % i if i else '0:2', rule=not a.norule)
        vram = open(out + '.raw.vram', 'rb').read()
        oam = open(out + '.raw.oam', 'rb').read()
        cg = cgram_of(out + '.raw')
        if oam[i * 4 + 2] != 0x24 + 2 * i:
            sys.exit('menu_icons: icon %d is not the selected one after Right x%d (OAM %d tile %02X)'
                     % (i, i, i, oam[i * 4 + 2]))
        if base is None:
            base = [icon(vram, cg, 4 + 2 * k) for k in range(6)]
        sel.append(icon(vram, cg, 0x24 + 2 * i))
    L = ['// Menu ribbon icons from the oracle OBJ VRAM (chr $4000/byte $8000, OBJ pal 0). base 004..00E =',
         '// GREY unselected; highlight 024..02E = the SELECTED icon\'s REAL colors -- CT loads a DIFFERENT OBJ',
         '// pal0 per selection (each icon\'s own colours), so each highlight tile is decoded with the palette',
         '// from the state where THAT icon is selected (sel$i.state). See disasm/notes/menu_phase2.md.',
         '#ifndef CT_MENU_ICONS_H', '#define CT_MENU_ICONS_H', '#include <stdint.h>', '',
         'static const uint8_t ctMenuIconBase[6][16*16*4] = {']
    for k in range(6):
        L.append('  { /* tile %02X */ %s },' % (4 + 2 * k, ','.join(str(v) for v in base[k])))
    L += ['};', '', 'static const uint8_t ctMenuIconSel[6][16*16*4] = {']
    for k in range(6):
        L.append('  { /* tile %02X */ %s },' % (0x24 + 2 * k, ','.join(str(v) for v in sel[k])))
    L += ['};', '', '#endif', '']
    path = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    open(path, 'w', newline='\n').write('\n'.join(L))
    print('wrote %s' % a.out)


if __name__ == '__main__':
    main()
