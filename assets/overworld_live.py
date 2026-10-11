#!/usr/bin/env python3
"""Render the CT overworld from a live engine snapshot (the map is assembled at runtime in WRAM).
Inputs (build/): ow_wram.bin (128KB; grids BG1 $7E:4000/defs $7E:3000, BG2 $7E:5800/defs $7E:3800, palette
$7F:8C00), ow_vram.bin (64KB char). Palette = CGRAM ($7F:8C00), 4bpp/16 colors per sub-palette; sub-palette
use (user-confirmed): 0=grass/sand/cliff, 2=sea, 3=forest, 4/5=village, 6=castle, 7=rock/mountain.
Composite: BG2 (base, opaque) then BG1 (land/structures, transparent over it). Chip order [TL,BL,TR,BR].
CLI: python assets/overworld_live.py [out.png]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from palettes import write_png_rgb

GW, GH = 96, 64
CORNERS = ((0, 0), (8, 0), (0, 8), (8, 8))   # block-def chip order [TL, TR, BL, BR] (+0,+2,+4,+6)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'build/ow_live.png'
    wram = open('build/ow_wram.bin', 'rb').read()
    vram = open('build/ow_vram.bin', 'rb').read()
    cg = wram[0x18C00:0x18C00 + 512]                       # $7F:8C00 = overworld CGRAM

    def conv(w):                                            # BGR15 -> RGB888, proper 5->8 bit fill
        f = lambda c: (c << 3) | (c >> 2)
        return (f(w & 31), f((w >> 5) & 31), f((w >> 10) & 31))
    palrgb = [conv(cg[c*2] | (cg[c*2+1] << 8)) for c in range(256)]

    tc = {}
    def tile(T):
        if T in tc: return tc[T]
        b = ((T & 0x3FF) + 512) * 32; px = bytearray(64)   # BG char base = VRAM tile 512 (no wrap; FE20 @ 512-1407)
        for y in range(8):
            b0, b1, b2, b3 = vram[b+y*2], vram[b+y*2+1], vram[b+16+y*2], vram[b+16+y*2+1]
            for x in range(8):
                k = 7-x
                px[y*8+x] = ((b0>>k)&1)|(((b1>>k)&1)<<1)|(((b2>>k)&1)<<2)|(((b3>>k)&1)<<3)
        tc[T] = px; return px

    W, H = GW*16, GH*16
    rgb = bytearray(W*H*3)
    def layer(defs, grid, transp):
        for gy in range(GH):
            for gx in range(GW):
                blk = wram[grid + gy*GW + gx]; d = defs + blk*8
                for ci in range(4):
                    chip = wram[d+ci*2] | (wram[d+ci*2+1] << 8)
                    t = tile(chip & 0x3FF); pb = ((chip >> 10) & 7) * 16
                    xf, yf = (chip >> 14) & 1, (chip >> 15) & 1
                    ox, oy = gx*16 + CORNERS[ci][0], gy*16 + CORNERS[ci][1]
                    for y in range(8):
                        ty = (7-y) if yf else y; row = (oy+y)*W
                        for x in range(8):
                            v = t[ty*8 + ((7-x) if xf else x)]
                            if transp and v == 0: continue
                            r, g, b = palrgb[pb+v]; o = (row+ox+x)*3
                            rgb[o] = r; rgb[o+1] = g; rgb[o+2] = b
    layer(0x3800, 0x5800, False)   # BG2 base, opaque
    layer(0x3000, 0x4000, True)    # BG1 over, transparent
    write_png_rgb(out, bytes(rgb), W, H)
    print('wrote %s (%dx%d)' % (out, W, H))


if __name__ == '__main__':
    main()
