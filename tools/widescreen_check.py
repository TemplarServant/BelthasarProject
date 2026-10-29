#!/usr/bin/env python3
r"""The widescreen field against the SNES's 256 columns: the view in its middle must be the same picture.

    python tools/widescreen_check.py [scene ...]

For each scene (default 5 6 112) runs Belthasar twice with a settings file (CT_SETTINGS) -- widescreen 16:9 and off, the
window at x1 with the picture at integer scale, so the window's pixels are the picture's -- and saves the frame at f150
without the overlay (CT_OVERLAY_SHOT ...:9, under the dummy video driver). Then:

    widescreen: scene 5 398x224: the middle 256 == the 256 picture (0 px differ); sides 31808 px, 0 black
    widescreen: scene 6 398x224: the middle 256 == the 256 picture (0 px differ); sides 31808 px, 29962 black

The sides' black pixels are the room's end (the scroll mask): a room no wider than the screen shows nothing beside it.

Two more cases (in the default set) need a seed and a frame of their own: `roll`, the staff roll's caption plane (scene
05B's roll from tools/caption_plane_cmp.py's seed), f450; and `line`, the tutorial's colour window (scene 254's line from
tools/tutorial_cmp.py's seed), f100. Both wrote their rows with the 256 picture's stride into the wide one (2026-10-10).
And `forest`, a live battle (scene 19 from saves/battle_re/forest_pre_wrapped.lakewram, A mashed), f300: the arena is
composed wide round the battle's own 256 view. A room as wide as its map ends at its edge like any other (the option
that wrapped the map there, wide_room_wrap, was removed 2026-10-23): black past it but for a background layer. The widescreen's
camera is the Steam release's clamped one (2026-10-28): at a room's edge the room's edge stands at the wide screen's
edge and the view slides off the middle (`5-edge`, the fair with Crono at its left edge: the view at x 0; the forest's
fight at f300). When the view stands off the middle the line says where (the screen-fixed boxes stay at the middle:
what still differs there).
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from backends_compare import read_bmp


def case(scene):
    """(argv after --assets, frame, extra env) for a scene number or a named case."""
    if scene == 'roll':
        import caption_plane_cmp as C
        return (['--scene', '91', '--loadwram', C.ensure_seed(), '--walkscript', '.:1000'], 450,
                {'CT_AUDIO_SEEDSTATE': C.STATE})
    if scene == 'line':
        import tutorial_cmp as T
        _, lw = T.ensure_seed('line')
        phase = T.SEGMENTS['line'][0] % (T.PER + T.PRESS)
        return (['--scene', '254', '--loadwram', lw, '--walkscript', T.walk(140, phase)], 100, {})
    if scene == 'forest':                          # a live battle (the arena wide: the field map, BG3 and its math)
        return (['--scene', '19', '--loadwram', 'saves/battle_re/forest_pre_wrapped.lakewram'], 300,
                {'CT_BATTLE_MASHA': '1', 'CT_NOPACE': '1'})
    if scene == '5-edge':                          # the fair (768 wide) with Crono at its left edge: the camera stops
        return (['--scene', '5', '--at', '40', '500'], 120, {})
    return (['--scene', str(scene)], 150, {})


def shot(scene, wide, d):
    ini = os.path.join(d, 'w%s_%s.ini' % (wide, scene)); bmp = os.path.join(d, 'w%s_%s.bmp' % (wide, scene))
    open(ini, 'w').write('[video]\nwindow_scale=1\nscale=integer\naspect=pixel\nfilter=nearest\nwidescreen=%s\n' % wide)
    args, at, extra = case(scene)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOAUDIO='1', CT_SETTINGS=ini,
               CT_OVERLAY_SHOT=bmp + '@%d:9' % at, **extra)
    subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat'] + args,
                   env=env, capture_output=True, text=True, errors='replace', timeout=300)
    return read_bmp(bmp) if os.path.exists(bmp) else None


def main():
    scenes = sys.argv[1:] or ['5', '5-edge', '6', '112', 'roll', 'line', 'forest']
    d = tempfile.mkdtemp(prefix='ct_wide_')
    for sc in scenes:
        a, b = shot(sc, '16:9', d), shot(sc, 'off', d)
        if not a or not b:
            print('widescreen: scene %s: a shot is missing' % sc); continue
        (w, h, pw), (nw, nh, pn) = a, b
        x0 = (w - nw) // 2

        def differ(v0):
            return sum(1 for y in range(h) for x in range(nw)
                       if pw[(y * w + v0 + x) * 3:(y * w + v0 + x) * 3 + 3] != pn[(y * nw + x) * 3:(y * nw + x) * 3 + 3])
        diff = differ(x0)
        where = ''
        if diff:                                   # the camera stopped at a room's edge: where the world's view stands
            best = min(range(0, w - nw + 1), key=differ)
            if best != x0:
                where = "; the view stands at x %d (the camera at the room's edge), %d px differ there" % (best, differ(best))
        side = [(y, x) for y in range(h) for x in list(range(x0)) + list(range(x0 + nw, w))]
        black = sum(1 for y, x in side if y < 211 and pw[(y * w + x) * 3:(y * w + x) * 3 + 3] == b'\0\0\0')
        print('widescreen: scene %s %dx%d: the middle %d %s the %d picture (%d px differ); sides %d px, %d black%s'
              % (sc, w, h, nw, '==' if diff == 0 else '!=', nw, diff, len(side), black, where))
    return 0


if __name__ == '__main__':
    sys.exit(main())
