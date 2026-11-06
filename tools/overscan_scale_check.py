#!/usr/bin/env python3
r"""The overscan scale (a mod, settings `scale=overscan`; docs/WIDESCREEN.md "The overscan scale"), checked without a
visible window.

    python tools/overscan_scale_check.py

Runs Belthasar's bedroom intro (scene 2: Crono's mother's dialogue window at the bottom) under SDL_VIDEODRIVER=dummy with
the widescreen at 16:9, the overscan scale and CT_OVERLAY_SHOT_SIZE (a hidden window of a screen's size), the window saved
as a BMP at frame F and the picture as presented (CT_WIDESHOTS) beside it. For each screen it restates the rule here --
k = ceil(height / 224); cut up to 13 lines at the bottom, then up to 8 at the top; else the plain integer scale -- and
compares the BMP with the picture scaled by k and placed so:

    overscan: 1920x1080: 5x, cut 8 bottom / 0 top, 7 columns a side: 0 px differ
    overscan: 1280x720: 3x, the integer scale (4x would cut 44 lines): 0 px differ

With "Fill the bottom lines" on, the dialogue window moves down only as far as the lines still shown:

    overscan: 1920x1080 with the bottom lines filled: the dialogue window 5 lines down (13 less the 8 cut)

Needs third_party/imgui (the probe is the overlay's); without it the check says so.
"""
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb   # noqa: E402

FRAME = 300
SCREENS = [(1920, 1080), (2560, 1440), (1280, 720)]
INI = ("[video]\nfullscreen=0\nscale=overscan\naspect=pixel\nfilter=nearest\nwidescreen=16:9\nsteam_margins=0\n"
       "fill_bottom_lines=%d\nscene_adjustments=0\nrender_scale=1\ninterpolation=0\nvsync=0\n")


def read_bmp(path):
    b = open(path, 'rb').read()
    off, = struct.unpack_from('<I', b, 10)
    w, h = struct.unpack_from('<ii', b, 18)
    bpp, = struct.unpack_from('<H', b, 28)
    stride = (w * bpp // 8 + 3) & ~3
    rows = []
    for y in range(abs(h)):
        sy = abs(h) - 1 - y if h > 0 else y
        r = b[off + sy * stride: off + sy * stride + w * bpp // 8]
        rows.append([(r[x * 4 + 2], r[x * 4 + 1], r[x * 4]) for x in range(w)])   # B G R -> RGB
    return w, abs(h), rows


def shoot(d, tag, fill, sw, sh):
    ini = os.path.join(d, tag + '.ini')
    open(ini, 'w').write(INI % fill)
    bmp, pre = os.path.join(d, tag + '.bmp'), os.path.join(d, tag)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOAUDIO='1', CT_SETTINGS=ini,
               CT_OVERLAY_SHOT='%s@%d:9' % (bmp, FRAME), CT_OVERLAY_SHOT_SIZE='%dx%d' % (sw, sh),
               CT_WIDESHOTS=pre, CT_WIDESHOTS_EVERY=str(FRAME), CT_WIDESHOTS_FROM=str(FRAME))
    p = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '2'],
                       env=env, capture_output=True, text=True, errors='replace', timeout=300)
    raw = '%s_f%d.raw' % (pre, FRAME)
    if not os.path.exists(bmp) or not os.path.exists(raw):
        print('overscan: %s: no shot (third_party/imgui missing?) %s' % (tag, p.stdout[-300:].strip()))
        return None
    w, h, rgb = fb.load(raw)
    pic = [[tuple(rgb[(y * w + x) * 3: (y * w + x) * 3 + 3]) for x in range(w)] for y in range(h)]
    return pic, read_bmp(bmp)


def place(sw, sh, pw):
    """The rule, restated: (k, x0, y0, cut bottom, cut top, columns cut a side) or the integer scale's."""
    k = -(-sh // 224)
    lines = sh // k
    cut = 224 - lines
    cb = min(cut, 13)
    ct = cut - cb
    cols = min(pw, sw // k)
    cols -= (pw - cols) & 1
    if ct <= 8 and cols >= 256:
        cl = (pw - cols) // 2
        return k, (sw - cols * k) // 2 - cl * k, (sh - lines * k) // 2 - ct * k, cb, ct, cl, None
    k2 = sh // 224
    while k2 > 1 and k2 * pw > sw:
        k2 -= 1
    return k2, (sw - pw * k2) // 2, (sh - 224 * k2) // 2, 0, 0, 0, cut


def compare(pic, shot, sw, sh):
    pw = len(pic[0])
    k, x0, y0, cb, ct, cl, refused = place(sw, sh, pw)
    w, h, px = shot
    if (w, h) != (sw, sh):
        return 'window %dx%d, not %dx%d' % (w, h, sw, sh)
    diff = 0
    for y in range(sh):
        sy = (y - y0) // k if y >= y0 else -1
        for x in range(sw):
            sx = (x - x0) // k if x >= x0 else -1
            want = pic[sy][sx] if 0 <= sy < 224 and 0 <= sx < pw else (0, 0, 0)
            if px[y][x] != want:
                diff += 1
    if refused is not None:
        return '%dx, the integer scale (%dx would cut %d lines): %d px differ' % (k, k + 1, refused, diff)
    return '%dx, cut %d bottom / %d top, %d columns a side: %d px differ' % (k, cb, ct, cl, diff)


def box_shift(off, on):
    """How far down the bottom dialogue window stands with the bottom lines filled: the dy that lays the mod-off
    window (lines 131-210, the middle 256 columns) on the mod-on picture."""
    pw = len(off[0])
    x0 = (pw - 256) // 2
    for dy in range(0, 14):
        if all(on[y + dy][x0:x0 + 256] == off[y][x0:x0 + 256] for y in range(131, 211)):
            return dy
    return -1


def main():
    d = tempfile.mkdtemp(prefix='ct_overscan_')
    for sw, sh in SCREENS:
        r = shoot(d, 'off_%dx%d' % (sw, sh), 0, sw, sh)
        if not r:
            return 1
        print('overscan: %dx%d: %s' % (sw, sh, compare(r[0], r[1], sw, sh)))
        if (sw, sh) == (1920, 1080):
            on = shoot(d, 'on_%dx%d' % (sw, sh), 1, sw, sh)
            if not on:
                return 1
            print('overscan: %dx%d with the bottom lines filled: %s' % (sw, sh, compare(on[0], on[1], sw, sh)))
            dy = box_shift(r[0], on[0])
            print('overscan: %dx%d with the bottom lines filled: the dialogue window %d lines down (13 less the %d cut)'
                  % (sw, sh, dy, place(sw, sh, len(r[0][0]))[3]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
