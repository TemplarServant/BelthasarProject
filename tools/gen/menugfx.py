"""Shared decoding for the menu generators that read the oracle's VRAM and CGRAM (tools/gen/menu_*.py).

tile4(vram, base, n)  -> the 64 colour indices (row-major 8x8) of the 4bpp tile n at VRAM byte base + n*32
rgb(cgram, i)         -> CGRAM colour i as the PPU expands it to 8 bits, (v<<3)|(v>>2) per channel
rgba(cgram, i)        -> rgb + 255, or (0,0,0,0) for i % 16 == 0 (colour 0 of a row is transparent)
"""


def tile4(vram, base, n):
    o = base + n * 32
    out = []
    for y in range(8):
        p0, p1, p2, p3 = vram[o + y * 2], vram[o + y * 2 + 1], vram[o + 16 + y * 2], vram[o + 16 + y * 2 + 1]
        for x in range(8):
            b = 7 - x
            out.append(((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) | (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3))
    return out


def x5(v):
    return (v << 3) | (v >> 2)


def rgb(cg, i):
    w = cg[i * 2] | cg[i * 2 + 1] << 8
    return (x5(w & 31), x5((w >> 5) & 31), x5((w >> 10) & 31))


def rgba(cg, i):
    return (0, 0, 0, 0) if i % 16 == 0 else rgb(cg, i) + (255,)


def block16(vram, base, tl, tr, bl, br):
    """A 16x16 block of four 8x8 4bpp tiles -> 256 indices, row-major."""
    t = [tile4(vram, base, n) for n in (tl, tr, bl, br)]
    out = []
    for y in range(16):
        for x in range(16):
            out.append(t[(y // 8) * 2 + x // 8][(y % 8) * 8 + x % 8])
    return out
