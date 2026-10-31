#!/usr/bin/env python3
r"""The mod "Fill the bottom lines" holds the picture's origin at a room's bottom in the fights as in the field: no line's
jump as a fight begins or ends.

    python tools/fill_battle_hold_check.py

With the mod on and the ROM's camera at a room's bottom the field draws the picture one line lower than the SNES (its
origin held so the room's last row stays on line 223, src/field.c fillHold). A fight composed at the SNES's origin would
move the whole picture up a line at the fight's first frame and down at the field's first frame after it. The attract's
0AD fight (the camera at the bottom of its room: mask rows 256-480, camera 256) shows both edges: f3807 (the fight
begins) and f4211 (the field again). Regression check: options-fill-bottom-lines-fight-keeps-the-held-picture.

Two interactive runs of the attract (CT_OVERLAY_SHOT forces the interactive presentation the mod needs; CT_SETTINGS
names the options): the mod on (and the 16:9 widescreen, so the fight's wide arena is composed too) and the mod off.
Printed:

    fill-hold: <edge> f<a> -> f<b>: best vertical step <+d> (rows 20-149)
    fill-hold: fight f<n>: the mod-off picture one line lower, rows 1-210: <k> px differ

The step is the dy in -3..3 that makes the two frames' rows 20-149 most alike (0: no jump). In the fight, rows 1-210 of
the mod-on picture against rows 0-209 of the mod-off one: the arena and what the fight places on the screen (the
combatants, the effects, the cursors) moved down together (0 px: nothing left at the SNES's line).
"""
import os
import subprocess
import sys
import tempfile

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]   # tools/dis.py
import numpy as np  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
EDGES = [('fight begins', 3806, 3807), ('fight ends', 4210, 4211)]
FIGHT = [3807, 3900, 4100, 4210]
AT = sorted({f for _, a, b in EDGES for f in (a, b)} | set(FIGHT))


def run(tmp, tag, fill):
    ini = os.path.join(tmp, tag + '.ini')
    with open(ini, 'w') as fh:
        fh.write('[video]\nwidescreen=%s\nfill_bottom_lines=%d\ninterpolation=0\n' % ('16:9' if fill else 'off', fill))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_SETTINGS=ini,
               CT_SHOTS=os.path.join(tmp, tag), CT_SHOTS_AT=','.join(map(str, AT)), CT_SHOTS_EXIT='1',
               CT_OVERLAY_SHOT=os.path.join(tmp, tag + '_end.bmp') + '@%d:9' % (AT[-1] + 30))
    return subprocess.Popen([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], cwd=ROOT, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def load(tmp, tag, f):
    d = open(os.path.join(tmp, '%s_f%d.raw' % (tag, f)), 'rb').read()
    w, h = (int(v) for v in np.frombuffer(d[:8], np.uint32))
    return np.frombuffer(d[8:8 + w * h * 3], np.uint8).reshape(h, w, 3).astype(int)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        procs = [run(tmp, 'on', 1), run(tmp, 'off', 0)]
        for p in procs:
            p.wait()
        for name, a, b in EDGES:
            pa, pb = load(tmp, 'on', a), load(tmp, 'on', b)
            best = min(range(-3, 4), key=lambda dy: ((np.abs(pb[20 + dy:150 + dy] - pa[20:150]).sum(axis=2) > 0).mean(), abs(dy)))
            print('fill-hold: %s f%d -> f%d: best vertical step %+d (rows 20-149)' % (name, a, b, best))
        for f in FIGHT:
            on, off = load(tmp, 'on', f), load(tmp, 'off', f)
            n = int((on[1:211] != off[0:210]).any(axis=2).sum())
            print('fill-hold: fight f%d: the mod-off picture one line lower, rows 1-210: %d px differ' % (f, n))


if __name__ == '__main__':
    main()
