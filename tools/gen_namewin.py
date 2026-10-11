#!/usr/bin/env python3
"""Regenerate saves/ct_namewin.bin -- the name-entry-screen WINDOW BACKGROUND, byte-exact.

The naming-screen windows (frame + diagonal weave + per-scanline HDMA gradient) are SNES BG2,
composited by the PPU's colour-math pipeline -- not reproducible from the final framebuffer alone
(the weave is a diagonal tile texture and the gradient is per-scanline). So we capture BG2 in
ISOLATION from the oracle: ct.exe renders one frame with only layer 1 (BG2) on the main screen
(PEEK_ISOBG=1, the g_ppuIsoBg hook in snes/ppu.c), keeping its real gradient/colour-math but
dropping the content layer (BG1 = portrait/keyboard/name) and sprites. The result is the exact
window background, character-independent; field.c blits it and draws the content on top.

Verified byte-identical: the full Lucca naming screen matches the oracle fb 57344/57344 px (max
channel diff 0). Run from the repo root; needs the ROM + saves/luccaname.state.
Output: saves/ct_namewin.bin = [w u16 LE][h u16 LE] + w*h*3 RGB (256x224)."""
import os, struct, subprocess, sys

ROM = sys.argv[1] if len(sys.argv) > 1 else 'Chrono_Trigger_USA_SNES.sfc'
STATE = 'saves/luccaname.state'        # a captured naming-screen savestate (Lucca; the bg is the same for all)
FB = 'build/winbg.fb'
OUT = 'saves/ct_namewin.bin'

subprocess.run(['build/ct.exe', ROM, '--peek', STATE, FB, '--frames', '1'],
               env={**os.environ, 'PEEK_ISOBG': '1'}, check=True)

d = open(FB, 'rb').read()                # oracle fb = 512x480, [pad,B,G,R] per px (2x); downscale to 256x224
W = 512
out = bytearray(struct.pack('<HH', 256, 224))
for y in range(224):
    for x in range(256):
        o = (2 * y * W + 2 * x) * 4
        out += bytes((d[o + 3], d[o + 2], d[o + 1]))   # R,G,B
open(OUT, 'wb').write(out)
print('wrote %s (%d bytes)' % (OUT, len(out)))
