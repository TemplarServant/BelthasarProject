#!/usr/bin/env python3
"""Decode Chrono Trigger graphics and render them to PNG (dependency-free).

Tile source is either an LZ-compressed ROM block or a raw VRAM dump. Colors come
from a CGRAM dump sub-palette (matching what the oracle loaded, so they reflect
any runtime palette animation); without one, a grayscale ramp is used.

CLI:
  python assets/gfx.py <rom.sfc> <hex-off> <out.png> [bpp] [cgram.bin] [sub]
  python assets/gfx.py --vram <vram.bin> <out.png> [bpp] [cgram.bin] [sub]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lz import lz_decompress
from palettes import snes15_to_rgb, write_png_rgb

def decode_tiles(data, bpp):
    """Yield 8x8 tiles as lists of 64 palette indices. bpp in {2,4}."""
    tile_bytes = 8 * bpp
    for t in range(len(data) // tile_bytes):
        base = t * tile_bytes
        px = [0] * 64
        for y in range(8):
            planes = []
            for pair in range(bpp // 2):
                lo = data[base + pair * 16 + y * 2]
                hi = data[base + pair * 16 + y * 2 + 1]
                planes.append((lo, 2 * pair))
                planes.append((hi, 2 * pair + 1))
            for x in range(8):
                bit = 7 - x
                v = 0
                for (b, shift) in planes:
                    v |= ((b >> bit) & 1) << shift
                px[y * 8 + x] = v
        yield px

def sub_palette(cgram, sub, ncolors):
    """Return `ncolors` RGB tuples from CGRAM sub-palette `sub`."""
    base = sub * ncolors
    return [snes15_to_rgb(cgram[(base + i) * 2] | (cgram[(base + i) * 2 + 1] << 8))
            for i in range(ncolors)]

def render_rgb(tiles, cols, bpp, palette):
    tiles = list(tiles)
    rows = (len(tiles) + cols - 1) // cols
    w, h = cols * 8, rows * 8
    maxv = (1 << bpp) - 1
    scale = 255 // maxv
    px = bytearray(w * h * 3)
    for i, t in enumerate(tiles):
        tx, ty = (i % cols) * 8, (i // cols) * 8
        for y in range(8):
            for x in range(8):
                idx = t[y * 8 + x]
                if palette:
                    r, g, b = palette[idx]
                else:
                    r = g = b = idx * scale
                o = ((ty + y) * w + (tx + x)) * 3
                px[o], px[o + 1], px[o + 2] = r, g, b
    return px, w, h

if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__); sys.exit(1)
    if a[0] == '--vram':
        data = open(a[1], 'rb').read()
        rest = a[2:]
    else:
        rom = open(a[0], 'rb').read()
        data = lz_decompress(rom, int(a[1], 16))
        rest = a[2:]
    out = rest[0]
    bpp = int(rest[1]) if len(rest) > 1 else 4
    palette = None
    if len(rest) > 2:
        cgram = open(rest[2], 'rb').read()
        sub = int(rest[3]) if len(rest) > 3 else 0
        palette = sub_palette(cgram, sub, 1 << bpp)
    ntiles = len(data) // (8 * bpp)
    cols = 16
    px, w, h = render_rgb(decode_tiles(data, bpp), cols, bpp, palette)
    write_png_rgb(out, px, w, h)
    print("%d bytes -> %d %dbpp tiles -> %dx%d '%s'%s"
          % (len(data), ntiles, bpp, w, h, out, " (colorized)" if palette else " (grayscale)"))
