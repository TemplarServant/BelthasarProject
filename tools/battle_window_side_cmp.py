#!/usr/bin/env python
"""Y in a battle moves the battle windows to the other side of the screen: the port against the rule's cartridge.

    python tools/battle_window_side_cmp.py

The battle NMI's CD/092F-0972 (GitHub issue #7's follow-up): on even `$7C`, with no layout open, the held remapped pad
`$00F8` bit 0 (Y) toggles `$CA21` and `$EC` once per press; D1/FE4C copies `$CA21` into `$CA22` as it installs the other
HDMA preset (CE/EE6E), and the picture follows: the strip and the windows at the top, the arena below.

tools/battle_ab.py's synthetic battle (GATO, saves/battle_re/gato_wait.state, the same WRAM and SPC on both sides), Y at
f700 for 4 frames and again at f800 (back to the bottom): on the cartridge in the drive, in the port through CT_HOLDBTN a
frame later (on this fixture the port's field frame f is the cartridge's frame f, and CT_HOLDBTN at f + 1 puts the press
in the port's `$00F8` on the frame the cartridge's drive puts it there: f + 1, both). Graded:
  - the layout bytes' timeline (`--monitor` / CT_FIELD_MEMMON of `$EC`, `$CA21`, `$CA22`) from the press;
  - the pictures, every frame from the press to 40 frames after the second: the port's thumbnail P against the
    cartridge's P + L, L the lag with the fewest differing pixels over the window (a pixel differs when a channel
    differs by more than 24), and the frames that differ at it -- against the same count with no press (the fight's own
    residual: the selection bracket's pulse runs from the band's arm, which the port reaches early, battle_menu_pass.md).
Prints `windowside: timeline <same|DIFFERENT> ...` and `windowside: pictures ...`. Regression check:
battle-y-moves-the-windows-to-the-top-vs-oracle.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import battle_ab as ab  # noqa: E402

PRESSES = (700, 800)
HOLD = 4
TOTAL = 880
REGS = [(0x00F8, 2), (0x00EC, 1), (0xCA21, 2)]


def main():
    os.chdir(ab.ROOT)
    ab.BTN['y'] = 0x02                                      # the cartridge's Y (the controller word's bit 1)
    drive = [(f, 'y', HOLD) for f in PRESSES]
    keep = ab.port_script
    ab.port_script = lambda d, start, total: keep([e for e in d if e[1] != 'y'], start, total)   # (no Y token there)
    os.environ['CT_HOLDBTN'] = ','.join('04@%d:%d' % (f + 1, HOLD) for f in PRESSES)
    regs, orows, prows, cth, pth, _ = ab.run({}, drive, TOTAL, 'windowside', regs=REGS, raw=True)
    os.environ['CT_HOLDBTN'] = ''
    _, _, _, cth0, pth0, _ = ab.run({}, [], TOTAL, 'windowside_nopress', regs=REGS, raw=True)

    def tl(rows):
        out, last = [], None
        for f in sorted(rows):
            v = (rows[f].get(0x00F8), rows[f].get(0x00EC), rows[f].get(0xCA21))
            if None in v: continue
            v = (v[0], v[1], v[2][0:2], v[2][2:4])
            if v != last and f >= PRESSES[0] - 2:
                out.append((f - PRESSES[0], v))
            last = v
        return out
    o, p = tl(orows), tl(prows)
    fmt = lambda e: ' '.join('+%d:%s' % (d, '/'.join(v)) for d, v in e)
    print('windowside: timeline %s -- `$00F8/$EC/$CA21/$CA22` from the first press (f%d): the cartridge %s; the port %s' % (
        'same' if o == p and o else 'DIFFERENT', PRESSES[0], fmt(o), fmt(p)))

    def grade(cth, pth):
        frames = [f for f in range(PRESSES[0], PRESSES[-1] + 40) if f in pth]
        best = None
        for lag in range(-8, 9):
            if not all(f + lag in cth for f in frames): continue
            bad = [f for f in frames if (np.abs(pth[f] - cth[f + lag]).max(axis=2) > 24).sum()]
            if best is None or len(bad) < len(best[1]): best = (lag, bad)
        return frames, best[0], best[1]
    frames, lag, bad = grade(cth, pth)
    _, lag0, bad0 = grade(cth0, pth0)
    worst = max(((np.abs(pth[f] - cth[f + lag]).max(axis=2) > 24).sum(), f) for f in bad) if bad else (0, None)
    print('windowside: pictures f%d-f%d at lag %+d: %d of %d frames differ%s; with no press %d at lag %+d' % (
        frames[0], frames[-1], lag, len(bad), len(frames),
        (' (the worst %d px at f%d)' % worst) if bad else '', len(bad0), lag0))
    extra = sorted(set(bad) - set(bad0))
    print('windowside: frames that differ only with the press: %s' % (','.join('f%d' % f for f in extra) or 'none'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
