#!/usr/bin/env python3
"""Decode + render a Chrono Trigger overworld (world map) from the ROM. The overworld engine is bank C2,
separate from the field-scene engine in C0:

  Location index WRAM $0100 >= 0x1F0 enters the overworld; world = ($0100 & 0x1FF) - 0x1F0 (0..7).
  Master descriptor ptr table C6:FD00 (8 u16 LOCAL offsets into bank C6) -> a 23-byte descriptor per
  world (C6:FD10 + world*0x17). Each descriptor byte indexes a bank-C6 3-byte-ptr subtable (0x80 high
  bit = none); every referenced block is LZ-decompressed (same codec as the field maps, assets/lz.py):
    desc[0x10] -> C6:FF00 -> L1 metatile/block defs   (4096 B = 512 blocks x 4 chips)
    desc[0x11] -> C6:FF20 -> L2 world block-grid       (12288 B = 96 x 128 cells, 1 byte = L1 block idx)
    desc[0x0A] -> C6:FEA0 -> palette                   (512 B = 256 BGR15 colors)
    desc[0x0C..0x0F] -> C6:FDF0 -> tile gfx list A (4bpp, DMA'd to VRAM tile 0)
    desc[0x00..0x06] -> C6:FE20 -> tile gfx list B (4bpp, DMA'd to VRAM tile 512)
  An L1 chip = u16: tile = bits0-9, palette = bits10-12, priority = bit13, xflip = bit14, yflip = bit15
  (the same SNES metatile encoding the field maps use). World pixel size = 96*16 x 128*16 = 1536 x 2048.

CLI: python assets/overworld.py <rom.sfc> <world 0-7> <out.png>"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '../tools')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lz import lz_decompress
from gfx import decode_tiles
from palettes import snes15_to_rgb, write_png_rgb

MASTER   = 0x06FD00      # C6:FD00 master descriptor ptr table (8 x u16 local offset)
SUB_FE20 = 0xC6FE20      # tile gfx list B (VRAM tile 512)
SUB_FDF0 = 0xC6FDF0      # tile gfx list A (VRAM tile 0)
SUB_FEA0 = 0xC6FEA0      # palette (+ aux)
SUB_FF00 = 0xC6FF00      # L1 metatile/block defs
SUB_FF20 = 0xC6FF20      # L2 world block-grid
GW, GH = 96, 128         # L2 grid dimensions (cells)

ERA = ["1000 AD", "600 AD", "2300 AD", "65,000,000 BC", "12,000 BC", "12,000 BC (b)",
       "12,000 BC (c)", "special"]


def _f(snes):
    return snes & 0x3FFFFF            # HiROM headerless: SNES addr -> file offset


def _ptr3(rom, tbl_snes, idx):
    b = _f(tbl_snes) + idx * 3
    return rom[b] | (rom[b + 1] << 8) | (rom[b + 2] << 16)


def decode_overworld(rom, world):
    """Return (descriptor, L1 bytes, L2 bytes, pal bytes, tiles[1024][64])."""
    desc_off = 0x060000 + (rom[MASTER + world * 2] | (rom[MASTER + world * 2 + 1] << 8))
    d = rom[desc_off:desc_off + 0x17]
    L1 = lz_decompress(rom, _f(_ptr3(rom, SUB_FF00, d[0x10])))
    L2 = lz_decompress(rom, _f(_ptr3(rom, SUB_FF20, d[0x11])))
    pal = lz_decompress(rom, _f(_ptr3(rom, SUB_FEA0, d[0x0A])))
    tiles = [[0] * 64 for _ in range(1024)]
    # the two gfx lists DMA to distinct VRAM tile bases (list A @ tile 0, list B @ tile 512), so list B
    # overwrites list A's upper half -> the 10-bit chip tile# indexes [A[0:512] | B[0:512]].
    def load_list(sub, doff, n, base):
        pos = base
        for k in range(n):
            i = d[doff + k]
            if i & 0x80:
                break
            for t in decode_tiles(lz_decompress(rom, _f(_ptr3(rom, sub, i))), 4):
                if pos < 1024:
                    tiles[pos] = t
                    pos += 1
    load_list(SUB_FDF0, 0x0C, 4, 0)      # gfx list A -> VRAM tile 0   (desc[0x0C..0x0F], bank C5)
    load_list(SUB_FE20, 0x00, 7, 512)    # gfx list B -> VRAM tile 512 (desc[0x00..0x06], bank C4)
    return d, L1, L2, pal, tiles


OCEAN = (32, 64, 112)     # base fill for water cells (BG3 ocean; exact via oracle CGRAM)
GRASS = (74, 140, 66)     # base fill for land cells (BG2 grass)


def render_overworld(rom, world):
    """Return (rgb bytes, W, H) for the full world map (1536 x 2048). The overworld composites BG1 (the
    L1/L2 metatiles -- coastlines/structures, mostly transparent) over a BG2 grass / BG3 ocean base that
    the engine builds at runtime (not in ROM). This approximates the base per cell: a cell whose block is
    mostly transparent is water (OCEAN fill), else land (GRASS fill); BG1 draws on top."""
    d, L1, L2, pal, tiles = decode_overworld(rom, world)
    W, H = GW * 16, GH * 16
    palrgb = [snes15_to_rgb(pal[c * 2] | (pal[c * 2 + 1] << 8)) for c in range(256)]
    # per-block opacity -> land/water classification for the base fill
    blk_land = bytearray(512)
    for blk in range(512):
        op = 0
        for ci in range(4):
            chip = L1[blk * 8 + ci * 2] | (L1[blk * 8 + ci * 2 + 1] << 8)
            op += sum(1 for v in tiles[chip & 0x3FF] if v)
        blk_land[blk] = 1 if op >= 96 else 0                 # >=96/256 opaque px -> land
    rgb = bytearray(W * H * 3)
    for gy in range(GH):
        for gx in range(GW):
            blk = L2[gy * GW + gx]
            base = GRASS if blk_land[blk] else OCEAN
            for y in range(16):                              # fill the 16x16 cell with the base
                o = ((gy * 16 + y) * W + gx * 16) * 3
                for x in range(16):
                    rgb[o] = base[0]; rgb[o + 1] = base[1]; rgb[o + 2] = base[2]; o += 3
            for ci in range(4):                              # block-def chip order = [TL, TR, BL, BR] (+0,+2,+4,+6)
                chip = L1[blk * 8 + ci * 2] | (L1[blk * 8 + ci * 2 + 1] << 8)
                t = tiles[chip & 0x3FF]
                pbase = ((chip >> 10) & 7) * 16
                xf, yf = (chip >> 14) & 1, (chip >> 15) & 1
                ox = gx * 16 + (8 if (ci & 1) else 0)        # TR/BR (2nd/4th) = right column
                oy = gy * 16 + (8 if (ci >= 2) else 0)       # BL/BR (3rd/4th) = bottom row
                for y in range(8):
                    ty = (7 - y) if yf else y
                    row = (oy + y) * W
                    for x in range(8):
                        v = t[ty * 8 + ((7 - x) if xf else x)]
                        if v == 0:
                            continue                         # transparent -> the cell's base fill
                        r, g, b = palrgb[pbase + v]
                        o = (row + ox + x) * 3
                        rgb[o] = r; rgb[o + 1] = g; rgb[o + 2] = b
    return bytes(rgb), W, H


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    rom = _romlib.load(sys.argv[1])
    world = int(sys.argv[2])
    out = sys.argv[3] if len(sys.argv) > 3 else 'overworld.png'
    rgb, W, H = render_overworld(rom, world)
    write_png_rgb(out, rgb, W, H)
    print("wrote %s (%dx%d) world %d (%s)" % (out, W, H, world, ERA[world]))
