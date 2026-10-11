#!/usr/bin/env python3
"""Regenerate saves/ct_namemsg.bin -- the "Enter a name, then push the Start Button." prompt box.

The FIRST naming screen (new game, Crono) shows this help message ($FF:CF49, the index-0 string of
the help table $FF:CF3B) in a textbox over the keyboard. It is BG2 (a third window) + BG3 (the
dialogue text), composited by the PPU. We capture the byte-exact box from the oracle: a saved
new-game naming state (saves/crononame.state -- made with the --script driver, DRIVE_CAP=450 +
PEEK_SAVE) advanced ~20 frames to full brightness, then crop the box rect (16,104)-(239,151).

Output: saves/ct_namemsg.bin = [x0 u16][y0 u16][w u16][h u16] + w*h*3 RGB (224x48 at 16,104).
The engine overlays it on the Crono naming until the player presses A/Start (see field.c)."""
import os, struct, subprocess, sys

ROM = sys.argv[1] if len(sys.argv) > 1 else 'Chrono_Trigger_USA_SNES.sfc'
STATE = 'saves/crononame.state'
FB = 'build/cnmsg.fb'
OUT = 'saves/ct_namemsg.bin'
X0, Y0, W, H = 16, 104, 224, 48              # the box rect (measured: diff vs the plain Crono naming)

subprocess.run(['build/ct.exe', ROM, '--peek', STATE, FB, '--frames', '20'], check=True)  # -> full bright

d = open(FB, 'rb').read()                     # 512x480 [pad,B,G,R] 2x -> 256x224 RGB
SW = 512
out = bytearray(struct.pack('<HHHH', X0, Y0, W, H))
for j in range(H):
    for i in range(W):
        o = (2 * (Y0 + j) * SW + 2 * (X0 + i)) * 4
        out += bytes((d[o + 3], d[o + 2], d[o + 1]))
open(OUT, 'wb').write(out)
print('wrote %s (%d bytes), box %dx%d at (%d,%d)' % (OUT, len(out), W, H, X0, Y0))
