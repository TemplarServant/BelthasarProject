"""Framebuffer/screenshot decode + dependency-free PNG writer.

Three capture formats exist in this repo (all observed in the writers):
  1. oracle fb (ct.exe --peek .fb / <peek>.bin): 512x480, 4 bytes per pixel
     [pad, B, G, R] (read R at +3!). It is a 2x pixel-doubled image, so the
     visible frame = EVERY OTHER column (2*x) and row (2*y) -> 256x240 RGB.
     Exactly 983040 bytes. (Reading column x = the left 256 cols showed only
     the left HALF stretched -- it hid the whole right side of the screen.)
  2. ct_field shot (--shot / CT_OWBOAT_SHOT / .pan.raw, src/field.c):
     [u32le w][u32le h][w*h*3 RGB rows]  (e.g. SHOT.raw = 256x224).
  3. ct_world WORLD_SHOT (src/ct_world.c): [u16le w][u16le h][w*h*3 RGB rows].

API:
  load(path)              -> (w, h, rgb bytes), format auto-sniffed by size
  png_bytes(w, h, rgb)    -> minimal zlib PNG file bytes
  save_png(out, w, h, rgb)
  fb_to_png(fb_path, out=None) -> out path written
"""
import struct
import zlib

FB_W, FB_H = 512, 480          # oracle raw dump dimensions
ORACLE_W, ORACLE_H = 256, 240  # visible image after 2x downsample
ORACLE_SIZE = FB_W * FB_H * 4  # 983040


def _load_oracle(d):
    """512x480 [pad,B,G,R] -> (256, 240, RGB). The fb is a 2x pixel-doubled
    image (SNES 256-wide rendered into a 512-wide buffer), so the true frame is
    every OTHER column (2*x) and row (2*y). Reading column x took only the left
    256 columns = the left HALF of the screen, stretched."""
    W, H = ORACLE_W, ORACLE_H
    img = bytearray(W * H * 3)
    for y in range(H):
        for x in range(W):
            o = ((2 * y) * FB_W + 2 * x) * 4
            i = (y * W + x) * 3
            img[i + 0] = d[o + 3]      # R at +3
            img[i + 1] = d[o + 2]      # G
            img[i + 2] = d[o + 1]      # B
    return W, H, bytes(img)


def load(path):
    """Decode a framebuffer/screenshot dump -> (w, h, RGB bytes)."""
    with open(path, 'rb') as fh:
        d = fh.read()
    if len(d) == ORACLE_SIZE:                       # oracle 512x480 fb
        return _load_oracle(d)
    if len(d) >= 8:                                 # [u32 w][u32 h][RGB]
        w, h = struct.unpack_from('<II', d, 0)
        if 0 < w <= 1024 and 0 < h <= 1024 and len(d) == 8 + w * h * 3:
            return w, h, d[8:]
    if len(d) >= 4:                                 # [u16 w][u16 h][RGB]
        w, h = struct.unpack_from('<HH', d, 0)
        if 0 < w <= 1024 and 0 < h <= 1024 and len(d) == 4 + w * h * 3:
            return w, h, d[4:]
    raise ValueError(
        "%s (%d bytes): not an oracle fb (512x480x4 = %d bytes) and not a "
        "[w][h][RGB] shot (u32 or u16 header)" % (path, len(d), ORACLE_SIZE))


def png_bytes(w, h, rgb):
    """Minimal 8-bit RGB PNG (no deps), from tools/_fbpng.py."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)                                  # filter: None
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(t, data):
        c = t + data
        return (struct.pack(">I", len(data)) + c +
                struct.pack(">I", zlib.crc32(c) & 0xffffffff))

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return (sig + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def save_png(out, w, h, rgb):
    with open(out, 'wb') as fh:
        fh.write(png_bytes(w, h, rgb))
    return out


def fb_to_png(fb_path, out=None):
    """Convenience: decode a framebuffer dump and write <fb_path>.png."""
    if out is None:
        out = fb_path + '.png'
    w, h, rgb = load(fb_path)
    return save_png(out, w, h, rgb)
