#!/usr/bin/env python3
r"""The three video backends (SDL_Renderer, OpenGL 3.3, Vulkan) against each other, on this machine's GPU.

    python tools/backends_compare.py [scene]

Not a suite check: it needs a real video driver (the suite runs headless). For each backend and filter it runs
ct_field with a settings file (CT_SETTINGS) choosing that backend, window x3, fit, square pixels, and CT_OVERLAY_SHOT
(the overlay's Sync tab at f40; the window, hidden, saved as a BMP), then compares the pictures:

    backends: nearest sdl vs opengl 768x672 -> 0 pixels differ (max 0)        (expected: identical)
    backends: linear  sdl vs opengl 768x672 -> N pixels differ (max 2)         (GL's interpolation rounds differently)

and which backend actually ran (a backend that cannot start falls back to SDL_Renderer -- the [video] line).
"""
import os
import struct
import subprocess
import sys
import tempfile


def read_bmp(path):
    b = open(path, 'rb').read()
    off, = struct.unpack_from('<I', b, 10)
    w, h = struct.unpack_from('<ii', b, 18)
    bpp, = struct.unpack_from('<H', b, 28)
    stride = (w * bpp // 8 + 3) & ~3
    px = bytearray()
    for y in range(abs(h)):
        sy = abs(h) - 1 - y if h > 0 else y
        row = b[off + sy * stride: off + sy * stride + w * bpp // 8]
        for x in range(w):
            px += row[x * 4: x * 4 + 3]
    return w, abs(h), bytes(px)


def main():
    scene = sys.argv[1] if len(sys.argv) > 1 else '6'
    d = tempfile.mkdtemp(prefix='ct_backends_')
    shots = {}
    for flt in ('nearest', 'linear'):
        for bk in ('sdl', 'opengl', 'vulkan'):
            ini = os.path.join(d, '%s_%s.ini' % (bk, flt)); bmp = os.path.join(d, '%s_%s.bmp' % (bk, flt))
            open(ini, 'w').write('[video]\nbackend=%s\nscale=fit\naspect=pixel\nwindow_scale=3\nfilter=%s\n' % (bk, flt))
            env = dict(os.environ, SDL_AUDIODRIVER='dummy', CT_NOAUDIO='1', CT_SETTINGS=ini, CT_OVERLAY_SHOT=bmp + '@40:3')
            env.pop('SDL_VIDEODRIVER', None)
            p = subprocess.run([os.path.abspath('build/ct_field.exe'), '--assets', 'ct_assets.dat', '--scene', scene],
                               env=env, capture_output=True, text=True, errors='replace', timeout=300)
            ran = next((l for l in (p.stdout + p.stderr).splitlines() if l.startswith('[video]')), '[video] ?')
            if flt == 'nearest': print('backends: %-6s -> %s' % (bk, ran[8:]))
            if os.path.exists(bmp): shots[(bk, flt)] = read_bmp(bmp)
    for flt in ('nearest', 'linear'):
        for a, b in (('sdl', 'opengl'), ('sdl', 'vulkan'), ('opengl', 'vulkan')):
            if (a, flt) not in shots or (b, flt) not in shots:
                print('backends: %-7s %s vs %s: a shot is missing' % (flt, a, b)); continue
            (w, h, pa), (_, _, pb) = shots[(a, flt)], shots[(b, flt)]
            n = mx = 0
            for i in range(0, min(len(pa), len(pb)), 3):
                m = max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2]))
                if m: n += 1; mx = max(mx, m)
            print('backends: %-7s %s vs %s %dx%d -> %d pixels differ (max %d)' % (flt, a, b, w, h, n, mx))
    return 0


if __name__ == '__main__':
    sys.exit(main())
