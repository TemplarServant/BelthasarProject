#!/usr/bin/env python3
"""src/data/menu_itemdetail.h -- the Item screen's selection hand, from the oracle's OAM, VRAM and CGRAM.

OBJ sprite 28 on the Item screen (OAM x=24 y=87 tile 0x020 pal 0 prio 3, 16x16): OBJ char base VRAM byte 0x8000, the
16x16 = tiles 0x20, 0x21, 0x30, 0x31; OBJ palette 0 = CGRAM 128..143 as the PPU expands it, (v<<3)|(v>>2). The capture:
the X menu open in front of GATO (recipe menu_open_gato2, standing in for saves/menu_open_gato2.state), Right (the Item
icon) and A, 12 frames on.

    python tools/gen/menu_itemdetail.py [--state S] [--norule] [--out src/data/menu_itemdetail.h]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, recipe, peek, scratch, cgram_of  # noqa: E402
from menugfx import rgb, block16  # noqa: E402

DRIVE = '80:2,0:6,100:2,0:12'
OBJ = 0x8000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a state with the X menu open instead of the recipe (saves/menu_open_gato2.state)')
    ap.add_argument('--norule', action='store_true', help='run the oracle without the rule (the old captures)')
    ap.add_argument('--out', default=os.path.join('src', 'data', 'menu_itemdetail.h'))
    a = ap.parse_args()
    out = peek(a.state or recipe('menu_open_gato2'), scratch('menu_itemdetail'), DRIVE, rule=not a.norule)
    vram = open(out + '.raw.vram', 'rb').read()
    oam = open(out + '.raw.oam', 'rb').read()
    cg = cgram_of(out + '.raw')
    s = oam[28 * 4:28 * 4 + 4]
    hi = (oam[512 + 7] >> 0) & 3
    if (s[0], s[1], s[2], s[3] & 0x0F, hi) != (24, 87, 0x20, 0, 2):
        sys.exit('menu_itemdetail: OBJ 28 is not the hand (x=24 y=87 tile 20, 16x16): %s %d' % (s.hex(), hi))
    t = s[2] | (s[3] & 1) << 8
    pal = (s[3] >> 1) & 7
    idx = block16(vram, OBJ, t, t + 1, t + 16, t + 17)
    cols = [rgb(cg, 128 + pal * 16 + i) for i in range(16)]
    used = sorted(set(idx))
    L = ['// gen_itemdetail.h - CT item-menu SELECTION CURSOR (hand), extracted from the oracle.',
         '// Source: saves/menu_open_gato2.state + btnscript "80:2,0:6,100:2,0:12" (opt1 ITEM, A).',
         '// OBJ sprite 28: OAM x=%d y=%d tile=0x%03X pal=%d prio=%d size=1 (16x16). VRAM OBJ char base'
         % (s[0], s[1], t, pal, (s[3] >> 4) & 3),
         '// byte 0x8000 (objTileAdr1=0x4000 word); 16x16 = tiles 0x20,0x21,0x30,0x31. Palette = OBJ',
         "// pal0 = CGRAM entries 128..143 (BGR555 in the state's .oam bytes 1056+). idx0 = transparent.",
         '// This is the MENU CURSOR (points at the highlighted item), NOT a per-item type marker.',
         '// Format: RGBA8888, 16x16, row-major. Alpha 0 where palette index == 0 (transparent).',
         '#ifndef GEN_ITEMDETAIL_H', '#define GEN_ITEMDETAIL_H', '#define ITEMCURSOR_W 16', '#define ITEMCURSOR_H 16',
         '// OBJ pal0 colors used (RGB): ' + ', '.join('%d=(%d,%d,%d)' % ((i,) + cols[i]) for i in used),
         "// (colour 14 = BGR555 (13,12,12) through the framebuffer's (v<<3)|(v>>2); it was baked as 106,98,98 = v*255/31,",
         "//  one off on every channel -- measured 2026-09-24 on the oracle's own Item and Shop frames: 107,99,99.)",
         'static const unsigned char itemCursorRGBA[16*16*4] = {']
    rows = []
    for y in range(16):
        rows.append('  ' + ','.join('%d,%d,%d,%d' % ((0, 0, 0, 0) if i == 0 else cols[i] + (255,))
                                     for i in idx[y * 16:y * 16 + 16]))
    L += [',\n'.join(rows), '};', 'static const unsigned char itemCursorIdx[16*16] = {']
    L += [',\n'.join('  ' + ','.join(str(i) for i in idx[y * 16:y * 16 + 16]) for y in range(16)), '};']
    L += ['// OBJ pal0 (16 BGR555->RGB888 triples); index 0 is the transparent color key.',
          'static const unsigned char itemCursorPal[16*3] = {',
          '  ' + ','.join('%d,%d,%d' % c for c in cols), '};',
          '// (REMOVED) itemScrollArrow -- an 8x8 crop of the list scrollbar\'s arrow, cut out of an oracle frame',
          '// at x235/y100. It was too small: the ROM\'s widget is the WHOLE 16x16 OBJ tile 0x22 (the same graphic',
          '// the ribbon cursor uses, menu_arrow.h / ROM FF/9D70), and the crop sliced the tile\'s dark OUTLINE',
          '// away -- it even kept the outline\'s colours, with alpha 0. The Item screen now blits ctMenuArrow at',
          '// the arrow\'s own OAM position; see the scrollbar block in CtMenuRenderItem.', '#endif', '']
    path = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    open(path, 'w', newline='\n').write('\n'.join(L))
    print('wrote %s' % a.out)


if __name__ == '__main__':
    main()
