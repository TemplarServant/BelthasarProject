#!/usr/bin/env python3
r"""The widescreen (a mod) against the vanilla picture for a screen-fixed overlay: the dialogue box.

    python tools/wide_box_cmp.py [state] [scene] [frame]

Loads a port scene snapshot (F5's CtSceneSaveState; default saves/field_re/wide_box_07a.state, loc 07A = scene 122, made
by tools/fixtures/rest.json from tools/recipes/fxr_chamber_touch.json; its top box is open at f100),
presses A every 30 frames so the cutscene's next box opens, and captures the PRESENTED frame (CT_OVERLAY_SHOT: the
picture the player sees, the widescreen's assembly included) twice -- once with the settings at 16:9, once with the
widescreen off -- at the same frame. The box is screen-fixed: in the wide picture it stands in the middle, and its 256
columns must be the vanilla box's, pixel for pixel. Prints

    wide-box: f<N> box rows <y0>-<y1>, widescreen (<w> wide) vs vanilla: <differing> differing pixel(s)

The user, 2026-10-13 (translated from Portuguese): "remnants of the background over the dialogue box" in widescreen --
loc 07A's camera stops at the room's edge, so fieldWideAssemble moves the screen-fixed pixels to the middle, and it told
them from the world by colour: the box's darkest pixels ((0,0,8), BG3 colour 0 on the backdrop) equal to the black floor
under them stayed with the world, and the middle showed the room past the edge through them.
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')


def bmp(path):
    b = open(path, 'rb').read()
    off = struct.unpack('<I', b[10:14])[0]
    w, h = struct.unpack('<ii', b[18:26])
    bpp = struct.unpack('<H', b[28:30])[0] // 8
    row = (w * bpp + 3) & ~3
    img = []
    for y in range(abs(h)):
        sy = (abs(h) - 1 - y) if h > 0 else y
        r = b[off + sy * row: off + sy * row + w * bpp]
        img.append([r[x * bpp: x * bpp + 3] for x in range(w)])
    return w, img


def shot(ini, state, scene, frame, tag):
    out = os.path.abspath('build/_widebox_%s.bmp' % tag)
    ws = 'n:20' + ''.join(',a:2,n:28' for _ in range(frame // 30 + 2))
    env = dict(os.environ, CT_SETTINGS=ini, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_OVERLAY_SHOT='%s@%d:9' % (out, frame), CT_SHOTS=os.path.abspath('build/_widebox_s'),
               CT_SHOTS_AT=str(frame + 3), CT_SHOTS_EXIT='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(scene), '--loadstate', state, '--walkscript', ws],
                   env=env, capture_output=True, text=True, errors='replace')
    return bmp(out)


def main():
    state = sys.argv[1] if len(sys.argv) > 1 else 'saves/field_re/wide_box_07a.state'
    scene = int(sys.argv[2]) if len(sys.argv) > 2 else 122
    frame = int(sys.argv[3]) if len(sys.argv) > 3 else 100
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    base = ('[video]\nbackend=sdl\nfullscreen=0\nwindow_scale=1\nscale=fit\naspect=pixel\nfilter=nearest\n'
            'wide_room_wrap=1\nrender_scale=1\ninterpolation=0\nvsync=0\n')
    inis = {}
    for tag, wide in (('wide', '16:9'), ('vanilla', 'off')):
        p = os.path.abspath('build/_widebox_%s.ini' % tag)
        open(p, 'w').write(base + 'widescreen=%s\n' % wide)
        inis[tag] = p
    ww, wi = shot(inis['wide'], state, scene, frame, 'wide')
    vw, vi = shot(inis['vanilla'], state, scene, frame, 'vanilla')
    if vw != 256 or ww <= 256:
        sys.exit('wide-box: unexpected widths (wide %d, vanilla %d)' % (ww, vw))
    x0 = (ww - 256) // 2
    # the box: from its top border to its bottom one -- the rows that are one light colour across (r,g,b all > 150)
    rows = [y for y in range(len(vi)) if min(vi[y][128]) > 150 and all(vi[y][x] == vi[y][128] for x in range(16, 240))]
    if not rows:
        sys.exit('wide-box: no box on the vanilla frame %d' % frame)
    y0 = rows[0]; y1 = min(len(vi), y0 + 80) - 1     # the field box is TB_H = 80 rows from its top border (field.c)
    diff = sum(1 for y in range(y0, y1 + 1) for x in range(256) if wi[y][x0 + x] != vi[y][x])
    print('wide-box: f%d box rows %d-%d, widescreen (%d wide) vs vanilla: %d differing pixel(s)' % (frame, y0, y1, ww, diff))


if __name__ == '__main__':
    main()
