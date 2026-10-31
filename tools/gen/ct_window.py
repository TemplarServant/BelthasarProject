#!/usr/bin/env python3
"""saves/ct_window.bin -- the dialogue window bitmap (ct_assets.dat T_WINDOW, CT_ASSET_WINDOW).

The field's dialogue box (disasm/notes/textbox.md) is BG1 (the static window tilemap at VRAM $1C00/$1D00: the metal
frame and the weave fill, scheme 0 = grey) switched in per band by indirect HDMA, with the vertical ramp computed by
HDMA on CGADSUB/COLDATA; the text is BG3. So the bitmap is the oracle's BG1 rendered alone (PEEK_ISOBG=0, the
g_ppuIsoBg hook in snes/ppu.c) on a frame where a box is fully open at the top (screen y 8..87 = rows 16..95 of the
240-row peek picture): the frame, the weave and the ramp, and no glyph.

The capture: recipe name_mom_box (Mom's first box on the ground floor of Crono's house, the typewriter on its first
character -- BG3 is dropped, so the frame on which the text stands does not matter).

The 41 pixels. By default the output keeps the earlier lifted bitmap's bytes, which the port is graded against: that
lift repaired the text region from the fill's 32-pixel period, out[x][y] = out[x + 32][y], starting at x 5 (inside the
left frame tile, which is not periodic) on rows 8..32, so x 5..7 of those rows carry the weave of x 37..39 -- 41
pixels differ from the cartridge's (disasm/notes/textbox.md, "The window from the ROM"). This tool re-applies that
repair; --cartridge writes the plain isolated BG1. The field box itself is built from the ROM (field.c
fieldWindowFromRom); this asset feeds the other windows (drawCtWindowRect, the battle message band, the menu's
drawWin, src/data/dlg_cursor.h).

    python tools/gen/ct_window.py [--state S] [--frames N] [--cartridge] [--out saves/ct_window.bin]

Output: [h u8] + 256*h*3 RGB, h = 80.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import recipe, peek, scratch, rgb_of_fb  # noqa: E402

H = 80
TOP = 16                    # the top box's first line (screen y 8) on the 240-row peek picture
LIFT_X, LIFT_Y = (5, 8), (8, 33)   # the lift's repair box over the left frame tile: x 5..7, rows 8..32
PERIOD = 32


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a state with a dialogue box fully open at the top (default: recipe name_mom_box)')
    ap.add_argument('--frames', type=int, default=1)
    ap.add_argument('--cartridge', action='store_true', help='the isolated BG1 as is (without the lift\'s 41 pixels)')
    ap.add_argument('--out', default='saves/ct_window.bin')
    a = ap.parse_args()
    st = a.state or recipe('name_mom_box')
    rgb = rgb_of_fb(peek(st, scratch('nm_window'), None, env={'PEEK_ISOBG': '0'}, frames=a.frames) + '.raw')
    win = bytearray(rgb[TOP * 768:(TOP + H) * 768])
    if not a.cartridge:
        for y in range(*LIFT_Y):
            for x in range(*LIFT_X):
                o, s = (y * 256 + x) * 3, (y * 256 + x + PERIOD) * 3
                win[o:o + 3] = win[s:s + 3]
    os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True)
    open(a.out, 'wb').write(bytes([H]) + bytes(win))
    print('wrote %s (%d bytes)' % (a.out, 1 + len(win)))


if __name__ == '__main__':
    main()
