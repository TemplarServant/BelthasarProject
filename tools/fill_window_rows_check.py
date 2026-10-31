#!/usr/bin/env python3
r"""The mod "Fill the bottom lines": the window rows (HDMA channel 5's iris window, and the colour window) follow the
picture -- the lowered dialogue box uncovers the picture, not the box's window, and the held origin moves the iris too.

    python tools/fill_window_rows_check.py

Two cases on the Telepod's warp (scene 8): (1) above the lowered dialogue box, no strip of the gate's BG3 swirl across the
screen -- channel 5's table gives a box's 2n lines one word ($1534 / set B $1536, field_hdma_tables.md modes 1/2), the
box's window, which takes BG1/BG2 off the whole line, and the lowered box (fillBoxBegin) leaves the top of its band to
the picture; (2) the gate's iris stays on its gate where the field holds its origin (fillHold: the map and the objects a
line lower than the SNES's, which would leave the window rows on the SNES's lines). src/field.c fieldCh5Rows /
fillWinRowsHold. Regression check: options-fill-bottom-lines-window-rows-follow-the-picture.

Two interactive runs of scene 8's gate show (CT_OVERLAY_SHOT forces the interactive presentation the mod needs; --selftest
lets CT_CALLFN start Taban's show; the state is the recipe fxf_scene8_taban_moving's), the mod on and the mod off. Printed:

    fill-win: f<n> (<what>): rows 1-<k> of the mod-on picture against the mod-off one a line lower: <p> px differ
    fill-win: f2250 (008, the lines the lowered box uncovers): rows 131-143 against f2310's, the box closed: <p> px differ

0 px: the mod-on picture is the mod-off one moved down with the held origin, the iris and the lines above the box with it;
and the lines between the SNES's box top (131) and the lowered box's (144) show the picture the room shows there once the
box has closed (f2310, nothing on those lines moves in between), not the BG3 swirl a box's window leaves.
"""
import os
import subprocess
import sys
import tempfile

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]   # tools/dis.py
import numpy as np  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
FLAGS = 'saves/field_re/telepod_entry09.flags'
# (frame, what, rows compared: 1..k of the mod-on picture against 0..k-1 of the mod-off one)
AT = [(2000, '008, the gate\'s iris', 210),
      (2250, '008, the lowered box: the lines above it', 130),
      (3500, '071, the arrival\'s iris', 210)]
UNCOVER = (2250, 2310, 131, 144)                # (the box up, the box closed, the lines the lowered box uncovers)
LAST = max(f for f, _, _ in AT)
WALK = 'i:110,' + ','.join(['a:1,i:29'] * 120)   # (field-scene8-gate-portal-draws-inside-window's drive)


def run(tmp, tag, fill):
    ini = os.path.join(tmp, tag + '.ini')
    with open(ini, 'w') as fh:
        fh.write('[video]\nwidescreen=off\nfill_bottom_lines=%d\ninterpolation=0\n' % fill)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', TB_AUTOADV='1',
               CT_CALLFN='11:1:2:100', CT_SETTINGS=ini, CT_SHOTS=os.path.join(tmp, tag),
               CT_SHOTS_AT=','.join(str(f) for f in sorted({f for f, _, _ in AT} | set(UNCOVER[:2]))), CT_SHOTS_EXIT='1',
               CT_OVERLAY_SHOT=os.path.join(tmp, tag + '_end.bmp') + '@%d:9' % (LAST + 30))
    return subprocess.Popen([FIELD, '--assets', 'ct_assets.dat', '--scene', '8', '--loadflags', FLAGS,
                             '--walkscript', WALK, '--selftest', os.path.join(tmp, tag + '_st.raw'),
                             '--frames', str(LAST + 40)], cwd=ROOT, env=env,
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
        for f, what, k in AT:
            on, off = load(tmp, 'on', f), load(tmp, 'off', f)
            n = int((on[1:k + 1] != off[0:k]).any(axis=2).sum())
            print('fill-win: f%d (%s): rows 1-%d of the mod-on picture against the mod-off one a line lower: %d px differ'
                  % (f, what, k, n))
        up, closed, y0, y1 = UNCOVER
        a, b = load(tmp, 'on', up), load(tmp, 'on', closed)
        n = int((a[y0:y1] != b[y0:y1]).any(axis=2).sum())
        print('fill-win: f%d (008, the lines the lowered box uncovers): rows %d-%d against f%d\'s, the box closed: %d px differ'
              % (up, y0, y1 - 1, closed, n))


if __name__ == '__main__':
    main()
