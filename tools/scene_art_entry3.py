#!/usr/bin/env python3
"""The scene adjustments' new art for sequence entry 3 (a mod; src/scene_art.h, docs/WIDESCREEN.md "Scene adjustments").

The machine rising over the planet under the red moon (`FF 23` after the Blackbird, `FF 63` in the attract) is drawn for 256
columns: its BG1 is the field's machinery as one screen. With the widescreen the sides need more of it. This script is the
art's source: it lays STAMPS (pieces of the machine's own layer, by place: the runtime cuts them from the player's VRAM) and
PAINTS new pixels over them, in the machine's own palettes and resolution, after the machine's design as the overworld shows
it (the Black Omen's wings sweeping out and down, a big gold-rimmed pod on each, spikes along their edges). Only the left
side is drawn; the right one is its mirror, as the machine itself is.

It writes src/art/entry3_sides.h: the stamps, and the new pixels as runs (CGRAM indices). None of the ROM's pixels is in it --
a run holds only a painted colour or a see-through pixel (checked).

  python tools/scene_art_entry3.py --pic <state.pic> [--write] [--preview <wide.raw> <out.png>] [--zoom <out.png>]

<state.pic>: the compositor's state at a frame of the stage (ct.exe --racepiccmp ... with RACEPICCMP_SHOT=<k> writes
build/racepic_native.pic; e.g. saves/blackbird_re/ff23_handoff_f25.state 0:3000 0 23 501 with SHOT=500). <wide.raw>: the
same frame with RACEPICCMP_WIDE=<side> RACEPICCMP_ART=1 (build/racepic_wide.raw).
"""
import argparse, os, struct, sys

W, H = 144, 256          # the art: the field's columns u = -144..-1, rows 0..255 of the layer
PRI = 0

# ---------------------------------------------------------------------------------------------------------------- the scene
def load_pic(path):
    d = open(path, 'rb').read()
    vram = struct.unpack_from('<32768H', d, 0)
    cg = struct.unpack_from('<256H', d, 65536)
    r = d[65536 + 512 + 512 + 32: 65536 + 512 + 512 + 32 + 64]
    return vram, cg, r

def orig_layer(vram, r):
    """The machine as the field laid it out (src/scene_art.c entry3Build): 256 x 256 CGRAM indices."""
    mp = (r[0x07] & 0xFC) << 8; chr_ = (r[0x0B] & 0x0F) << 12
    out = [[0] * 256 for _ in range(256)]
    for ty in range(32):
        for tx in range(32):
            e = vram[(mp + ty * 32 + tx + 1) & 0x7FFF] if tx < 31 else vram[(mp + ((ty + 1) & 31) * 32) & 0x7FFF]
            base = chr_ + (e & 0x3FF) * 16
            for y in range(8):
                row = 7 - y if e & 0x8000 else y
                a = vram[(base + row) & 0x7FFF]; b = vram[(base + 8 + row) & 0x7FFF]
                for x in range(8):
                    col = x if e & 0x4000 else 7 - x
                    c = ((a >> col) & 1) | (((a >> (8 + col)) & 1) << 1) | (((b >> col) & 1) << 2) | (((b >> (8 + col)) & 1) << 3)
                    out[ty * 8 + y][tx * 8 + x] = ((e >> 10) & 7) * 16 + c if c else 0
    return out

def rgb(c):
    return tuple(((c >> s) & 31) * 255 // 31 for s in (0, 5, 10))

# --------------------------------------------------------------------------------------------------------------- the canvas
class Canvas:
    """The art in the field's columns (u < 0). Stamps are replayed alone into `S` too: the new pixels are where the two differ."""
    def __init__(self, orig):
        self.orig = orig
        self.F = [[0] * W for _ in range(H)]; self.S = [[0] * W for _ in range(H)]
        self.painted = [[0] * W for _ in range(H)]
        self.stamps = []

    def _put(self, img, u, y, v):
        x = u + W
        if 0 <= x < W and 0 <= y < H: img[y][x] = v

    def get(self, u, y):
        x = u + W
        if 0 <= x < W and 0 <= y < H: return self.F[y][x]
        if 0 <= u < 256 and 0 <= y < 256: return self.orig[y][u]
        return 0

    def stamp(self, du, dy, su, sy, w, h, flip=0):
        self.stamps.append((du, dy, su, sy, w, h, flip))
        for j in range(h):
            for i in range(w):
                sx = su + (w - 1 - i if flip else i); syy = sy + j
                if not (0 <= sx < 256 and 0 <= syy < 256): continue
                v = self.orig[syy][sx]
                if v:
                    self._put(self.F, du + i, dy + j, v); self._put(self.S, du + i, dy + j, v)
                    x = du + i + W
                    if 0 <= x < W and 0 <= dy + j < H: self.painted[dy + j][x] = 0

    def stamp_mask(self, du, dy, su, sy, w, h, inside, flip=0):
        """A stamp whose pixels outside `inside(u, y)` (the art's place) are taken off again (see-through, painted)."""
        self.stamp(du, dy, su, sy, w, h, flip)
        for j in range(h):
            for i in range(w):
                if not inside(du + i, dy + j) and self.get(du + i, dy + j): self.px(du + i, dy + j, 0)

    def paint_on(self, pts_or_fn, v, base):
        """New pixels over the pixels of value `base` only (a seam drawn on the plating, not over the stamped pieces)."""
        for (u, y) in pts_or_fn:
            if self.get(u, y) == base and -W <= u < 0: self.px(u, y, v)

    def px(self, u, y, v):
        """A new pixel: a CGRAM index, or 0 (see-through)."""
        x = u + W
        if 0 <= x < W and 0 <= y < H:
            self.F[y][x] = v; self.painted[y][x] = 1

    def hline(self, u0, u1, y, v):
        for u in range(min(u0, u1), max(u0, u1) + 1): self.px(u, y, v)

    def line(self, u0, y0, u1, y1, v):
        n = max(abs(u1 - u0), abs(y1 - y0), 1)
        for k in range(n + 1):
            self.px(round(u0 + (u1 - u0) * k / n), round(y0 + (y1 - y0) * k / n), v)

    def poly_fill(self, pts, v, only_empty=False):
        ys = [p[1] for p in pts]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            xs = []
            for i in range(len(pts)):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
                if (y0 <= y < y1) or (y1 <= y < y0):
                    xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for u in range(int(round(a)), int(round(b))):
                    if not only_empty or not self.get(u, y): self.px(u, y, v)

    def clip_outside(self, pts):
        """Everything outside the polygon becomes see-through."""
        inside = set()
        ys = [p[1] for p in pts]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            xs = []
            for i in range(len(pts)):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
                if (y0 <= y < y1) or (y1 <= y < y0):
                    xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for u in range(int(round(a)), int(round(b))): inside.add((u, y))
        for y in range(H):
            for x in range(W):
                if self.F[y][x] and (x - W, y) not in inside: self.px(x - W, y, 0)

    def recolor(self, pts, mapping):
        """Inside the polygon, every pixel whose index is a key takes the value (shading a stamped piece)."""
        ys = [p[1] for p in pts]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            xs = []
            for i in range(len(pts)):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
                if (y0 <= y < y1) or (y1 <= y < y0):
                    xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for u in range(int(round(a)), int(round(b))):
                    v = self.get(u, y)
                    if v in mapping and -W <= u < 0: self.px(u, y, mapping[v])

    def runs(self):
        """The new pixels as (count, value) runs: 0 = none, a multiple of 16 = see-through, else a CGRAM index."""
        flat = []
        for y in range(H):
            for x in range(W):
                f, s = self.F[y][x], self.S[y][x]
                if f == s: flat.append(0)
                else:
                    assert f == 0 or self.painted[y][x], 'a new pixel that was not painted (%d, %d)' % (x - W, y)
                    flat.append(f if f else 0x10)
        out = []; i = 0
        while i < len(flat):
            j = i
            while j < len(flat) and flat[j] == flat[i] and j - i < 255: j += 1
            out += [j - i, flat[i]]; i = j
        return out

# ------------------------------------------------------------------------------------------------------------------ the art
# CGRAM indices (palette * 16 + colour): the machine's own four palettes.
def P(pal, c): return pal * 16 + c
# The ramp shared by palettes 2, 5 and 6 (1 FFFFEE, 2 CDCDAC, 3 9C9494, 4 626273, 5 394A62, 6 293141, 7 182939, 8 181829,
# 9 081029, 10 the darkest); palette 3's blues (1 A4FFFF, 2 6AE6E6, 3 4ABDCD, 4 318BC5, 5 2952B4, 6 292983, 7 181852);
# palette 5's gold (11 DEA420, 12 8B5A18, 13 6A4118, 14 392910); palette 6's pinks (11 B48BA4 .. 15 DEB4CD).
GREY = lambda c: P(5, c)
BLUE = lambda c: P(3, c)
GOLD = lambda c: P(5, c)

def draw(cv):
    """The left side (u < 0): none drawn yet -- the machine's own art past its edge is still to make (after the overworld's
    Black Omen, its wings sweeping out and down to their points). With the player's Steam copy the Steam release's own art
    for the scene is drawn instead (src/scene_art.c, the Steam margins of location 06B)."""
    pass

# --------------------------------------------------------------------------------------------------------------- the output
HEADER = """// GENERATED by tools/scene_art_entry3.py -- do not edit by hand. The scene adjustments' new art for sequence entry 3 (a mod,
// src/scene_art.h): stamps of the machine's own layer by place (cut at run time from the player's VRAM) and the new pixels
// as runs. No pixel of the ROM is here.
#define E3_ART_W %d
#define E3_ART_H %d
#define E3_ART_PRI %d
"""

def write_header(cv, path):
    st = cv.stamps or [(0, 0, 0, 0, 0, 0, 0)]
    runs = cv.runs()
    with open(path, 'w', newline='\n') as f:
        f.write(HEADER % (W, H, PRI))
        f.write('static const CtArtStamp kE3Stamps[] = {\n')
        for s in st: f.write('  { %d, %d, %d, %d, %d, %d, %d },\n' % s)
        f.write('};\nstatic const uint8_t kE3Px[] = {\n')
        for i in range(0, len(runs), 24): f.write('  ' + ', '.join(str(v) for v in runs[i:i + 24]) + ',\n')
        f.write('};\n')
    print('wrote %s: %d stamps, %d runs' % (path, len(cv.stamps), len(runs) // 2))

def preview(cv, cg, wide_raw, out):
    from PIL import Image
    d = open(wide_raw, 'rb').read(); ww, hh = struct.unpack('<II', d[:8]); side = (ww - 256) // 2
    im = Image.frombytes('RGB', (ww, hh), d[8:8 + ww * hh * 3]); px = im.load()
    for line in range(hh):
        y = line                                   # (the stage's BG1 V scroll is -1: line l shows the layer's row l - 1 + 1)
        for x in range(-side, 256 + side):
            if 8 <= x < 256: continue
            u = x - 8
            if u >= 128: u = 255 - u
            v = cv.get(u, y)
            if v: px[x + side, line] = rgb(cg[v])
    im.resize((ww * 3, hh * 3), Image.NEAREST).save(out)
    print('preview', out)

def zoom(cv, cg, out, z=5):
    from PIL import Image, ImageDraw
    u0, u1, y0, y1 = -W, 64, 80, 248
    im = Image.new('RGB', ((u1 - u0) * z + 40, (y1 - y0) * z + 20), (0, 0, 0)); d = ImageDraw.Draw(im)
    for y in range(y0, y1):
        for u in range(u0, u1):
            v = cv.get(u, y)
            c = rgb(cg[v]) if v else (52, 0, 52)
            d.rectangle([40 + (u - u0) * z, 20 + (y - y0) * z, 40 + (u - u0 + 1) * z - 1, 20 + (y - y0 + 1) * z - 1], fill=c)
    for u in range(u0, u1 + 1, 8):
        X = 40 + (u - u0) * z; d.line([(X, 20), (X, im.height)], fill=(80, 80, 80)); d.text((X + 1, 2), str(u), fill=(255, 255, 0))
    for y in range(y0, y1 + 1, 8):
        Y = 20 + (y - y0) * z; d.line([(40, Y), (im.width, Y)], fill=(80, 80, 80)); d.text((2, Y + 1), str(y), fill=(255, 255, 0))
    im.save(out); print('zoom', out)

def export_layer(cv, cg, out, key=(0, 255, 0)):
    """The layer alone, u = -144..111, rows 0..223, the see-through as a flat key colour (an image model's canvas)."""
    from PIL import Image
    im = Image.new('RGB', (256, 224), key); px = im.load()
    for y in range(224):
        for x in range(256):
            v = cv.get(x - W, y)
            if v: px[x, y] = rgb(cg[v])
    im.save(out); print('layer', out)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--pic', required=True)
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--preview', nargs=2)
    ap.add_argument('--zoom')
    ap.add_argument('--export-layer')
    ap.add_argument('--blank', action='store_true', help='no art (the layer as the ROM has it)')
    a = ap.parse_args()
    vram, cg, r = load_pic(a.pic)
    cv = Canvas(orig_layer(vram, r))
    if not a.blank: draw(cv)
    if a.write:
        write_header(cv, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'art', 'entry3_sides.h'))
    if a.preview: preview(cv, cg, a.preview[0], a.preview[1])
    if a.zoom: zoom(cv, cg, a.zoom)
    if a.export_layer: export_layer(cv, cg, a.export_layer)

if __name__ == '__main__':
    main()
