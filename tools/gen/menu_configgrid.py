#!/usr/bin/env python3
"""src/data/menu_configgrid.h -- the Config screen's icon graphics, from the oracle's VRAM and CGRAM.

BG char base 0, 4bpp (tile N at VRAM byte N*32); each icon is a 2x2 block (TL = base, TR = base+1, BL = base+2,
BR = base+3), the faces a 2x1 pair; palette-resolved RGBA8888 with CGRAM rows 5 and 6 as the PPU expands them,
(v<<3)|(v>>2); colour 0 transparent. The capture: the X menu open in front of GATO (recipe menu_open_gato2, standing
in for saves/menu_open_gato2.state), Right x3 (the Config icon) and A, 12 frames on.

    python tools/gen/menu_configgrid.py [--state S] [--norule] [--out src/data/menu_configgrid.h]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, recipe, peek, scratch, cgram_of  # noqa: E402
from menugfx import tile4, rgba, block16  # noqa: E402

DRIVE = '80:2,0:4,80:2,0:4,80:2,0:4,0:6,100:2,0:12'
# (name, first tile, palette row, 16x16 block or 16x8 pair)
ICONS = [('CFG_ICON_STEREO', 0x13C, 5, 1), ('CFG_ICON_BATTLEMODE', 0x144, 5, 1), ('CFG_ICON_SAVEMENU', 0x158, 5, 1),
         ('CFG_ICON_SAVEBATTLE', 0x15C, 5, 1), ('CFG_ICON_SAVESKILL', 0x160, 6, 1), ('CFG_ICON_BATTLESPEED', 0x138, 5, 1),
         ('CFG_ICON_WINDOWCOLOR', 0x148, 5, 1), ('CFG_ICON_BATTLEGAUGE', 0x154, 5, 1),
         ('CFG_ICON_CONTROLPAD', 0x14C, 5, 1), ('CFG_ICON_SKILLINFO', 0x140, 5, 1), ('CFG_CHECKBOX_EMPTY', 0x130, 5, 1),
         ('CFG_CHECKBOX_CHECKED', 0x134, 5, 1), ('CFG_FACE_LEFT', 0x152, 5, 0), ('CFG_FACE_RIGHT', 0x150, 5, 0)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a state with the X menu open instead of the recipe (saves/menu_open_gato2.state)')
    ap.add_argument('--norule', action='store_true', help='run the oracle without the rule (the old captures)')
    ap.add_argument('--out', default=os.path.join('src', 'data', 'menu_configgrid.h'))
    a = ap.parse_args()
    out = peek(a.state or recipe('menu_open_gato2'), scratch('menu_configgrid'), DRIVE, rule=not a.norule)
    vram = open(out + '.raw.vram', 'rb').read()
    cg = cgram_of(out + '.raw')
    L = ['// gen_configgrid.h -- Config-screen ICON GRAPHICS extracted from the oracle VRAM.',
         '// SOURCE: saves/menu_open_gato2.state --btnscript "80:2,0:4,80:2,0:4,80:2,0:4,0:6,100:2,0:12" (enter Config).',
         '//   VRAM BG char base 0, 4bpp; tile N at VRAM byte N*32. CGRAM palettes (BGR555, re-expanded 2026-09-24 as the'
         ' PPU does, (v<<3)|(v>>2); the capture had v*255/31): pal5 rows 80..95,',
         '//   pal6 rows 96..111. Each icon = a 2x2 tile block (TL=base, TR=base+1, BL=base+2, BR=base+3) -> 16x16.',
         '//   Format: palette-RESOLVED RGBA8888, row-major, 16 rows x 16 px x 4 bytes. Color index 0 = transparent',
         '//   (A=0); the red check in CFG_CHECKBOX_CHECKED is pal5 color index 14 = (222,24,16).',
         '// Faces (CFG_FACE_*) are 8 rows x 16 px (a 2x1 horizontal tile pair) flanking the Battle-Gauge-Speed',
         '//   number row. All derived byte-for-byte from VRAM; nothing invented.',
         '#ifndef GEN_CONFIGGRID_H', '#define GEN_CONFIGGRID_H', '#include <stdint.h>', '']
    for name, base, pal, big in ICONS:
        if big:
            idx, rows = block16(vram, 0, base, base + 1, base + 2, base + 3), 16
            L.append('// tile 0x%03X-0x%03X, palette %d' % (base, base + 3, pal))
            L.append('static const uint8_t %s[16*16*4] = {' % name)
        else:
            t0, t1 = tile4(vram, 0, base), tile4(vram, 0, base + 1)
            idx = [(t0 if x < 8 else t1)[y * 8 + x % 8] for y in range(8) for x in range(16)]
            rows = 8
            L.append('// tile 0x%03X-0x%03X, palette %d (16x8)' % (base, base + 1, pal))
            L.append('static const uint8_t %s[8*16*4] = {' % name)
        for y in range(rows):
            L.append('  ' + ''.join('%d,%d,%d,%d,' % rgba(cg, pal * 16 + i) for i in idx[y * 16:y * 16 + 16]))
        L += ['};', '']
    L.append('#endif // GEN_CONFIGGRID_H')
    path = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    open(path, 'w', newline='\n').write('\n'.join(L) + '\n')
    print('wrote %s' % a.out)


if __name__ == '__main__':
    main()
