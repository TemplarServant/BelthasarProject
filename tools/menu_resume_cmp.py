#!/usr/bin/env python3
r"""The field's fade-in after a menu (C0/2824) against the cartridge, pixel by pixel -- user defect 2026-09-26 #21:
a menu opened mid-walk made the screen "jump" on closing.

    python tools/menu_resume_cmp.py

C0/2824 fades in the RELOADED field (C0/01A5 rebuilt the map, C0/7506/7553 the camera from the arrival tile, C0/286C
redrew the objects), with the scheduler and the controller frozen. The port used to fade in the frame the menu froze
at its OPEN, then cut to the reloaded field: on a walk the camera jumped at the fade's end. Two drives on party3_field
(the logical oracle, `--peek` F+2 = the port's frame F, menu_shell_cmp.py's alignment): X standing still, and X right
after a walk to the right. For every frame from the reload's last black one to the fade's end the tool counts the
pixels that differ in the 256x224 picture and prints the largest; what is left is sprite-sized (C0/B127 resets the drawn
objects' frames to their sequences' first -- `$1681 = 0` -- for the fade, the port keeps its own), not a picture:

    menu-resume: walk f121-f137: max 329 px differ (the black f121, the fade f122-f136, the lit f137)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menu_shell_cmp as M  # noqa: E402
from ctlib import fb, peekshots  # noqa: E402

ROOT = M.ROOT
DRIVES = [
    ('stand', '0:4,200:2,0:54,1:2,0:90', 'n:5,m:1,n:55,b:1,n:90', 108, 124),
    ('walk', '0:4,80:16,200:2,0:52,1:2,0:110', 'n:5,r:16,m:1,n:53,b:1,n:110', 121, 137),
]


def main():
    for tag, obtn, pwalk, a, b in DRIVES:
        frames = list(range(a, b + 1))
        # the cartridge's display of frame F = the dump after F + 2 frames, every frame from one run (PEEK_SHOTS; this read
        # menu_shell_cmp's per-frame `_mshell_o_*_<F>.raw` files, which that tool no longer leaves behind)
        got = peekshots.shots('saves/battle_re/party3_field.state', obtn, [f + 2 for f in frames],
                              env=dict(os.environ, CT_ORACLE_LOGICAL='1'), prefix=os.path.join(ROOT, 'build', '_mresume_o_' + tag))
        M.port_frames('saves/battle_re/party3_field.lakewram', 130, pwalk, frames, 'rs' + tag)
        worst = 0
        for f in frames:
            ow, oh, ob = fb.load(got[f + 2])
            pw, ph, pb = fb.load(os.path.join(ROOT, 'build', '_mshell_p_rs%s_f%d.raw' % (tag, f)))
            n = 0
            for y in range(224):
                orow, prow = (y + 8) * ow * 3, y * pw * 3
                if ob[orow:orow + 768] != pb[prow:prow + 768]:
                    n += sum(1 for x in range(256) if ob[orow + 3 * x:orow + 3 * x + 3] != pb[prow + 3 * x:prow + 3 * x + 3])
            worst = max(worst, n)
        peekshots.clean(got)
        print('menu-resume: %s f%d-f%d: max %d px differ%s' % (tag, a, b, worst,
                                                                 '' if worst < 1000 else '  <== the picture, not sprites'))


if __name__ == '__main__':
    main()
