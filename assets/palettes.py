#!/usr/bin/env python3
"""Chrono Trigger palette handling: SNES 15-bit BGR -> RGB, using the exact
formula the PPU uses ((c<<3)|(c>>2) per 5-bit channel), so colors match what the
oracle renders. Renders a CGRAM dump as a 16x16 swatch, and converts a raw
framebuffer dump to PNG. Dependency-free (zlib from stdlib).

CLI:
  python assets/palettes.py swatch <cgram.bin> <out.png>     # 256-color 16x16 grid
  python assets/palettes.py fb <fb.raw> <out.png> [w h]      # fb dump: 4 B/px [pad,B,G,R]
"""
import sys, struct, zlib, binascii

def snes15_to_rgb(word):
    r = word & 0x1F
    g = (word >> 5) & 0x1F
    b = (word >> 10) & 0x1F
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))

def write_png_rgb(path, px, w, h):
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter 0
        raw += px[(y * w) * 3:(y * w + w) * 3]
    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', binascii.crc32(c) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))  # 24-bit RGB
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))

def cgram_swatch(cgram, cell=12):
    cols = rows = 16
    w, h = cols * cell, rows * cell
    px = bytearray(w * h * 3)
    for i in range(min(256, len(cgram) // 2)):
        word = cgram[i * 2] | (cgram[i * 2 + 1] << 8)
        r, g, b = snes15_to_rgb(word)
        cx, cy = (i % 16) * cell, (i // 16) * cell
        for y in range(cell):
            for x in range(cell):
                o = ((cy + y) * w + (cx + x)) * 3
                px[o], px[o + 1], px[o + 2] = r, g, b
    return px, w, h

def timeline(files, out, cell=4):
    """Render a (frames x 256) palette image and report animated CGRAM indices."""
    cgrams = [open(f, 'rb').read() for f in files]
    n = len(cgrams)
    w, h = 256 * cell, n * cell
    px = bytearray(w * h * 3)
    for r, cg in enumerate(cgrams):
        for i in range(256):
            r8, g8, b8 = snes15_to_rgb(cg[i * 2] | (cg[i * 2 + 1] << 8))
            for y in range(cell):
                for x in range(cell):
                    o = ((r * cell + y) * w + (i * cell + x)) * 3
                    px[o], px[o + 1], px[o + 2] = r8, g8, b8
    write_png_rgb(out, px, w, h)
    changed = [i for i in range(256)
               if any(cgrams[k][i * 2:i * 2 + 2] != cgrams[0][i * 2:i * 2 + 2] for k in range(1, n))]
    return changed, n

def fb_bgrx_to_rgb(fb, w, h):
    # LakeSnes pixelOutputFormat = BGRX (=1) writes each pixel as bytes [pad, B, G, R]
    # (snes/ppu.c:334-339: +1=B, +2=G, +3=R; byte +0 is NEVER written). Reading from byte 0
    # drops the red channel and yellow-tints everything -- the old bug here.
    px = bytearray(w * h * 3)
    for i in range(w * h):
        px[i * 3 + 0] = fb[i * 4 + 3]  # R
        px[i * 3 + 1] = fb[i * 4 + 2]  # G
        px[i * 3 + 2] = fb[i * 4 + 1]  # B
    return px, w, h

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print(__doc__); sys.exit(1)
    mode = sys.argv[1]
    if mode == 'timeline':
        # palettes.py timeline <out.png> <cgram0.bin> <cgram1.bin> ...
        out = sys.argv[2]
        files = sys.argv[3:]
        changed, n = timeline(files, out)
        print("timeline of %d frames -> '%s'; %d animated CGRAM indices: %s"
              % (n, out, len(changed), ' '.join('%d' % i for i in changed[:48])))
    else:
        src, out = sys.argv[2], sys.argv[3]
        data = open(src, 'rb').read()
        if mode == 'swatch':
            px, w, h = cgram_swatch(data)
            write_png_rgb(out, px, w, h)
            print("CGRAM -> %dx%d swatch '%s'" % (w, h, out))
        elif mode == 'fb':
            w = int(sys.argv[4]) if len(sys.argv) > 4 else 512
            h = int(sys.argv[5]) if len(sys.argv) > 5 else 480
            px, w, h = fb_bgrx_to_rgb(data, w, h)
            write_png_rgb(out, px, w, h)
            print("framebuffer -> %dx%d PNG '%s'" % (w, h, out))
        elif mode == 'raw':
            # C-engine raw dump: [W u32 LE][H u32 LE][W*H*3 RGB] -> PNG
            w = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24)
            h = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
            write_png_rgb(out, bytearray(data[8:8 + w * h * 3]), w, h)
            print("raw %dx%d -> '%s'" % (w, h, out))
        else:
            print("unknown mode:", mode); sys.exit(1)
