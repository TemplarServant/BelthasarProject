#!/usr/bin/env python3
"""saves/ct_namewin.bin -- the name-entry screen's WINDOW BACKGROUND (ct_assets.dat T_NAMEWIN), byte-exact.

The naming screen's windows (frame + diagonal weave + per-scanline HDMA gradient) are SNES BG2, composited by the
PPU's colour math -- not recoverable from the final picture (the weave is a diagonal tile texture, the gradient is
per-scanline). So BG2 is rendered in ISOLATION by the oracle: one frame with only layer 1 (BG2) on the main screen
(PEEK_ISOBG=1, the g_ppuIsoBg hook in snes/ppu.c), keeping its real gradient and colour math but dropping the content
layer (BG1 = portrait/keyboard/name) and the sprites. The result is character-independent; field.c blits it and draws
the content on top.

The capture: Lucca's naming screen (recipe name_lucca: home_downstairs's walk down to Mom, stopped on the naming
screen before its Start). Port of tools/gen_namewin.py (which read the owner's saves/luccaname.state).

    python tools/gen/ct_namewin.py [--state S] [--out saves/ct_namewin.bin]

Output: [w u16 LE][h u16 LE] + w*h*3 RGB (256x224).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import recipe, peek, scratch  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a naming-screen state instead of the recipe (e.g. saves/luccaname.state)')
    ap.add_argument('--out', default='saves/ct_namewin.bin')
    a = ap.parse_args()
    st = a.state or recipe('name_lucca')
    fb = peek(st, scratch("nm_namewin"), None, env={'PEEK_ISOBG': '1'}, frames=1) + '.raw'
    d = open(fb, 'rb').read()               # 512x480 [pad,B,G,R] (2x) -> 256x224 RGB
    out = bytearray(struct.pack('<HH', 256, 224))
    for y in range(224):
        for x in range(256):
            o = (2 * y * 512 + 2 * x) * 4
            out += bytes((d[o + 3], d[o + 2], d[o + 1]))
    os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True)
    open(a.out, 'wb').write(out)
    print('wrote %s (%d bytes)' % (a.out, len(out)))


if __name__ == '__main__':
    main()
