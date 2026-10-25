#!/usr/bin/env python3
r"""The dialogue window's TEXT against the cartridge, frame by frame (disasm/notes/textbox.md, "four lines, the line
buffer and the NMI's upload").

    python tools/textbox_window_cmp.py

Two drives, the same on both sides, ALIGNED BY THE RULE (ct-frame-alignment-p2): the logical oracle, the port's key one
frame after the cartridge's, the port's frame F beside the cartridge's --peek F+2. (Until 2026-10-10 this ran the real-time
oracle with a peek at F+1, which carried the rule's one-frame compensation: the NMI's text upload had been modelled at the
head of the NEXT frame to match it. Under the rule the upload is the frame's own -- see tools/textbox_rule_cmp.py.)

  * the tent keeper (saves/field_re/tent_keeper_lake.state / tent_keeper.lakewram, A at f11): a FOUR-line box typed
    without a press -- the typewriter (one character an iteration, a break's own iteration, the end iteration), the
    NMI's upload of the line buffer into window line $0131 (a character typed in frame f is on screen from f+1), the
    text row (window top + 10) and the closing quote (0xE2, not 0xE1);
  * Marle (saves/field_re/s439_marle_choice_lake.state / s439_marle_choice.lakewram, A at f101): a page turn -- the
    old page's lines leave one per frame as the ramp walks $0131 0..3 with a cleared buffer (1FB4 / 1FC0), and the
    new page types its first character on the ramp's last frame (1FC5 -> C0/2126).

Only the text rows are compared (x 8..247 of the window's 80 rows): the window's own texture and its open ramp are
graded elsewhere. One line per moment:

    textbox-window: <drive> f<F> -> <N> px differ
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
CT = os.path.join(ROOT, 'build', 'ct.exe')

DRIVES = [
    # name, oracle state, port seed, scene, press frame, window rows, frames
    ('tent-keeper', 'saves/field_re/tent_keeper_lake.state', 'saves/field_re/tent_keeper.lakewram', 5, 11, (131, 211),
     [27, 30, 45, 46, 47, 48, 98, 99, 100, 126, 127, 140]),
    ('marle-page', 'saves/field_re/s439_marle_choice_lake.state', 'saves/field_re/s439_marle_choice.lakewram', 439, 101,
     (8, 88), [100, 102, 103, 104, 105, 106, 107, 108, 110]),
]


def oracle(state, press, frame, tag):
    out = os.path.join(ROOT, 'build', '_tbwin_o_%s_%d.raw' % (tag, frame))
    script = '0:%d,100:1,0:%d' % (press - 1, max(1, frame - press))   # a peek after `frame` frames in all
    subprocess.run([CT, '--peek', state, out, '--btnscript', script], cwd=ROOT,
                   env=dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1'),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


def port(seed, scene, press, frames, tag):
    last = max(frames) + 2
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=os.path.join(ROOT, 'build', '_tbwin_p_%s' % tag), CT_SHOTS_AT=','.join(str(f) for f in frames))
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(scene), '--loadwram', seed, '--selftest',
                    os.path.join(ROOT, 'build', '_tbwin.raw'), '--frames', str(last + 1),
                    '--walkscript', 'n:%d,a:1,n:%d' % (press, last - press)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {f: os.path.join(ROOT, 'build', '_tbwin_p_%s_f%d.raw' % (tag, f)) for f in frames}


def diff(of, pf, rows):
    ow, oh, o = fb.load(of)
    pw, ph, p = fb.load(pf)
    n = 0
    for y in range(rows[0], rows[1]):
        for x in range(0, 256):                       # the whole row: the frame's edge tiles too
            i, j = ((y + 8) * ow + x) * 3, (y * pw + x) * 3
            if o[i:i + 3] != p[j:j + 3]:
                n += 1
    return n


def main():
    for name, ostate, seed, scene, press, rows, frames in DRIVES:
        pfs = port(seed, scene, press, frames, name)
        for f in frames:
            of = oracle(ostate, press, f + 2, name)
            if not os.path.exists(of) or not os.path.exists(pfs[f]):
                print('textbox-window: %s f%d -- a frame was not produced' % (name, f))
                continue
            print('textbox-window: %s f%d -> %d px differ' % (name, f, diff(of, pfs[f], rows)))


if __name__ == '__main__':
    main()
