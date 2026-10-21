#!/usr/bin/env python3
r"""The widescreen (a mod) against the vanilla picture for a sequence stage's curtain: the race's opening and closing.

    python tools/wide_curtain_cmp.py [frame,frame,...]

Runs the attract (--scene 433, lockstep) twice to the race's end -- once at 16:9, once with the widescreen off -- with
CT_WIDESHOTS every frame from f8140, and for each listed frame reads the lit span on row 112 (the curtain opens and
closes from the left, so the span is [0, edge]). The widescreen's curtain is the vanilla one scaled to the picture: the
vanilla edge e (0..255) stands at round(e * (w - 1) / 255) of the w-wide picture (snes/ppu.c ppu_winTest's curtain arm,
the same scaling as field.c wideWipeEdge). Prints one line per frame and a verdict:

    wide-curtain: f<N> vanilla edge <e> -> widescreen edge <E> (scaled <S>)
    wide-curtain: <k> of <n> frame(s) off the scaled edge

The user, 2026-10-13 (translated from Portuguese): "in widescreen the race's curtain in the demo does not work as it
should" -- the left side was lit from the curtain's first frame and the right one snapped dark (or lit) at once, since
the window test only moved the edge across the 256 columns.
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
# (f8170 is the opening's last frame before its edge comes within 16 columns of the screen's -- from there both window
#  rules, snes/ppu.c's and seq_pic.c's, read the band left as the road's hidden column and light the side: the mod's known
#  limit; the curtain is the native stage's since 2026-10-18, on the cartridge's frames)
FRAMES = (8151, 8155, 8159, 8163, 8167, 8170, 8487, 8491, 8495, 8499, 8503, 8507)


def run(ini, out):
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, CT_SETTINGS=ini, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_AUDIO_LOCKSTEP='1', CT_WIDESHOTS=os.path.join(out, 'w'), CT_WIDESHOTS_EVERY='1',
               CT_WIDESHOTS_FROM='8140', CT_OVERLAY_SHOT='%s@8530:9' % os.path.join(out, 'end.bmp'))
    # (the overlay probe makes the host interactive, the only kind that reads the settings' mods; it ends the run at 8530)
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env, capture_output=True, text=True,
                   errors='replace')


def edge(path):
    b = open(path, 'rb').read()
    w, h = struct.unpack('<II', b[:8])
    px = b[8:]
    lit = [x for x in range(w) if sum(px[(112 * w + x) * 3:(112 * w + x) * 3 + 3]) > 0]
    return w, (max(lit) if lit else -1)


def main():
    frames = tuple(int(v) for v in sys.argv[1].split(',')) if len(sys.argv) > 1 else FRAMES
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    base = ('[video]\nbackend=sdl\nfullscreen=0\nwindow_scale=1\nscale=fit\naspect=pixel\nfilter=nearest\n'
            'wide_room_wrap=1\nrender_scale=1\ninterpolation=0\nvsync=0\n')
    for tag, wide in (('wide', '16:9'), ('vanilla', 'off')):
        ini = os.path.abspath('build/_widecurtain_%s.ini' % tag)
        open(ini, 'w').write(base + 'widescreen=%s\n' % wide)
        run(ini, os.path.abspath('build/_widecurtain_%s' % tag))
    off = 0
    for f in frames:
        vw, ve = edge('build/_widecurtain_vanilla/w_f%d.raw' % f)
        ww, we = edge('build/_widecurtain_wide/w_f%d.raw' % f)
        s = -1 if ve < 0 else (ww - 1 if ve >= vw - 1 else (ve * (ww - 1) + 127) // 255)
        # (a pixel of slack: the vanilla edge is a column, the scaled one the nearest of the picture's)
        if abs(we - s) > 1:
            off += 1
        print('wide-curtain: f%d vanilla edge %d -> widescreen edge %d (scaled %d)' % (f, ve, we, s))
    print('wide-curtain: %d of %d frame(s) off the scaled edge' % (off, len(frames)))


if __name__ == '__main__':
    main()
