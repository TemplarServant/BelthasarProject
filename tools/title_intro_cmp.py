#!/usr/bin/env python3
r"""Diff the ENGINE's title-intro swing frame against the ORACLE's own framebuffer, pixel for pixel.

    python tools/title_intro_cmp.py <swing frame> [<swing frame> ...]

The engine's swing frame k is the oracle's boot frame k + CT_INTRO_BASE (141, measured: it is the
frame the driver task $7E:5BCF first runs, and the alignment was found by sweeping it until the diff
went to zero rather than by counting frames in the boot).

⚠ The oracle's 512x480 dump is not a plain 2x image: ppu_putPixels writes scanline y to buffer row
y*2 + 16, so the 224 visible lines are IMAGE rows 8..231 of the 256x240 that ctlib/fb.py decodes.
Comparing row 0 to row 0 shows ~6000 "mismatches" that look exactly like a rendering defect.

The swing is byte-exact against the oracle for frames ~110..588. OUTSIDE that window two layers the
port does not draw yet are on screen and account for every differing pixel: the COPYRIGHT block until
frame ~109 (668 px) and the mode-2 LOGO phase from frame ~589 (1489 px). Both are named as open in
disasm/notes/title_intro.md §5.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb as FB

ROM = 'Chrono_Trigger_USA_SNES.sfc'
CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/ct_field.exe')
CT_INTRO_BASE = 141


# ⚠ PER-PROCESS temp names. TWO checks run this script -- title-intro-swing-vs-oracle and
# title-intro-logo-vs-oracle -- and regress.py schedules checks in PARALLEL (--jobs defaults to
# min(12, CPUs-1)). With one fixed pair of paths the two races: `compare` DELETES the shot before
# each frame precisely so a stale file cannot be graded, and the sibling's delete lands on the file
# this run just wrote. Observed as an intermittent 0.1s 'signature missing' on whichever lost, while
# running the same argv alone passes 8/8 at 0 px in 99s -- a red that is not a regression is worse
# than useless, because it trains you to discount the suite.
_TMP = 'build/_introcmp_%d' % os.getpid()

def compare(k):
    shot = _TMP + '_ours.raw'
    # ⚠ The engine only writes the shot if it REACHES that frame of the sequence; without this the
    # comparison would silently grade the previous run's file, which is the worst kind of green.
    if os.path.exists(shot):
        os.remove(shot)
    env = dict(os.environ, CT_SRAM_FRESH='1', CT_NAMEAS='Crono',
               CT_INTROSHOT=shot, CT_INTROSHOT_AT=str(k))
    n = k + 400                                    # the boot needs ~330 frames to reach sequence 2
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--boot', '--selftest',
                    '--frames', str(n), '--walkscript', 'n:%d' % (n - 1)], env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    if not os.path.exists(shot):
        sys.exit('title-intro-cmp: swing frame %d was NEVER REACHED in %d engine frames' % (k, n))
    ow, oh, ours = FB.load(shot)
    subprocess.run([CT, ROM, '--fb', _TMP + '_orc.raw', '--frames', str(CT_INTRO_BASE + k)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    rw, rh, ref = FB.load(_TMP + '_orc.raw')
    bad = 0
    for y in range(oh):
        for x in range(ow):
            o = (y * ow + x) * 3
            r = ((y + 8) * rw + x) * 3
            if ours[o:o + 3] != ref[r:r + 3]:
                bad += 1
    print('title-intro-cmp: swing frame %d vs oracle f%d -> %d mismatching pixels of %d'
          % (k, CT_INTRO_BASE + k, bad, ow * oh))
    return bad


if __name__ == '__main__':
    ks = [int(a) for a in sys.argv[1:]] or [150, 300, 500]
    worst = max(compare(k) for k in ks)
    sys.exit(0 if worst == 0 else 1)
