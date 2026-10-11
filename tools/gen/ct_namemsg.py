#!/usr/bin/env python3
"""saves/ct_namemsg.bin -- the "Enter a name, then push the Start Button." prompt box (ct_assets.dat T_NAMEMSG).

The FIRST naming screen (new game, Crono) shows this help message ($FF:CF49, the index-0 string of the help table
$FF:CF3B) in a textbox over the keyboard: BG2 (a third window) + BG3 (the dialogue text), composited by the PPU. The
box is cropped byte-exact from the oracle: the new game's Crono naming screen advanced 20 frames to full brightness,
the rect (16,104)-(239,151).

The capture: recipe name_crono (atb_vram's next A takes Active and opens Crono's naming screen). Port of
tools/gen_namemsg.py (which read the owner's saves/crononame.state).

    python tools/gen/ct_namemsg.py [--state S] [--out saves/ct_namemsg.bin]

Output: [x0 u16][y0 u16][w u16][h u16] + w*h*3 RGB (224x48 at 16,104).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import recipe, peek, scratch  # noqa: E402

X0, Y0, W, H = 16, 104, 224, 48            # the box rect (measured: diff vs the plain Crono naming)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a Crono naming-screen state instead of the recipe (e.g. saves/crononame.state)')
    ap.add_argument('--out', default='saves/ct_namemsg.bin')
    a = ap.parse_args()
    st = a.state or recipe('name_crono')
    fb = peek(st, scratch("nm_namemsg"), None, frames=20) + '.raw'
    d = open(fb, 'rb').read()               # 512x480 [pad,B,G,R] (2x)
    out = bytearray(struct.pack('<HHHH', X0, Y0, W, H))
    for j in range(H):
        for i in range(W):
            o = (2 * (Y0 + j) * 512 + 2 * (X0 + i)) * 4
            out += bytes((d[o + 3], d[o + 2], d[o + 1]))
    os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True)
    open(a.out, 'wb').write(out)
    print('wrote %s (%d bytes), box %dx%d at (%d,%d)' % (a.out, len(out), W, H, X0, Y0))


if __name__ == '__main__':
    main()
