#!/usr/bin/env python3
r"""The F1 options overlay and ct_settings.ini, checked without a visible window.

    python tools/options_overlay_check.py

Writes a settings file (4:3, window x3, integer scale, the A button on Space, sync free) to a temp folder, runs
Belthasar under SDL_VIDEODRIVER=dummy with CT_SETTINGS pointing at it and CT_OVERLAY_SHOT (the overlay drawn at frame
40 on the Controls tab, the window saved as a BMP), then reads the BMP:

    options: window 896x672 (4:3 at x3)            -- the file's aspect and window size took effect
    options: picture 896 px wide from x 0 ...       -- integer scale fills the 4:3 window (3 x 224 lines)
    options: overlay drawn (N px of its panel colour)

Needs third_party/imgui (tools/get_imgui.ps1); without it Belthasar has no overlay and the check says so.
"""
import os
import struct
import subprocess
import sys
import tempfile

INI = ("[video]\nscale=integer\naspect=4:3\nwindow_scale=3\n[sound]\nvolume=40\n"
       "[controls]\nkey_a=Space\n[timing]\nsync=free\n")


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
        rows.append([tuple(r[x * 4: x * 4 + 3]) for x in range(w)])   # B G R
    return w, abs(h), rows


def main():
    d = tempfile.mkdtemp(prefix='ct_opts_')
    ini, bmp = os.path.join(d, 'ct_settings.ini'), os.path.join(d, 'shot.bmp')
    open(ini, 'w').write(INI)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOAUDIO='1', CT_SETTINGS=ini,
               CT_OVERLAY_SHOT=bmp + '@40:2')
    p = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '6'],
                       env=env, capture_output=True, text=True, errors='replace', timeout=300)
    if not os.path.exists(bmp):
        print('options: no overlay (third_party/imgui missing?)', p.stdout[-300:])
        return 1
    w, h, px = read_bmp(bmp)
    print('options: window %dx%d (%s)' % (w, h, '4:3 at x3' if (w, h) == (896, 672) else 'UNEXPECTED'))
    # the picture: the box of non-black pixels on the outer rows/columns (the overlay sits inside it)
    def black(c): return c[0] < 8 and c[1] < 8 and c[2] < 8
    cols = [x for x in range(w) if not black(px[h // 2][x])]
    print('options: picture %d px wide from x %d on the middle line' % (cols[-1] - cols[0] + 1 if cols else 0,
                                                                     cols[0] if cols else -1))
    # the overlay's window background (ImGui dark: ~ (15,15,15) at 94% over the picture) -> count dark-blue title/tab
    tabs = sum(1 for y in range(h) for x in range(0, w, 4) if px[y][x][0] > 150 and px[y][x][2] < 90 and px[y][x][1] < 120)
    print('options: overlay drawn' if tabs > 200 else 'options: overlay NOT drawn')
    return 0


if __name__ == '__main__':
    sys.exit(main())
