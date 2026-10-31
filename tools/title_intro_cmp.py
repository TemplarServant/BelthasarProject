#!/usr/bin/env python3
r"""Diff the engine's title-intro swing frame against the oracle's own framebuffer, pixel for pixel.

    python tools/title_intro_cmp.py <swing frame> [<swing frame> ...]

The engine's swing frame k is the oracle's boot frame k + CT_INTRO_BASE (141: the frame the driver task
$7E:5BCF first runs). The frames graded span the copyright block (until frame ~109), the swing and the
mode-2 logo phase (from frame ~589); disasm/notes/title_intro.md.

Note: the oracle's 512x480 dump is not a plain 2x image: ppu_putPixels writes scanline y to buffer row
y*2 + 16, so the 224 visible lines are image rows 8..231 of the 256x240 that ctlib/fb.py decodes.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb as FB

ROM = 'Chrono_Trigger_USA_SNES.sfc'
CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/Belthasar.exe')
CT_INTRO_BASE = 141


# Per-process temp names: two checks run this script (title-intro-swing-vs-oracle and
# title-intro-logo-vs-oracle) and regress.py runs checks in parallel. `compare` deletes the shot
# before each frame so a stale file cannot be graded; with shared paths the sibling's delete would
# land on the file this run just wrote.
_TMP = 'build/_introcmp_%d' % os.getpid()

def compare(k):
    shot = _TMP + '_ours.raw'
    # Note: the engine writes the shot only if it reaches that frame of the sequence; without this
    # delete the comparison would grade the previous run's file.
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
