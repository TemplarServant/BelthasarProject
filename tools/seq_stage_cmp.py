#!/usr/bin/env python3
r"""Diff one frame of a $C3 SEQUENCE-TABLE entry against the ORACLE's own framebuffer, pixel for
pixel.

    python tools/seq_stage_cmp.py <entry> <page> <frame> [<frame> ...]

`<entry>` is the index into $7E:3043 (disasm/notes/sequence_table.md); `<page>` is how many times
the setup has run, which for entry 1 is the credits page ($0382 is not cleared by the common init,
so the attract's five visits show pages 1..5). `<frame>` counts NMIs since the setup returned.

⚠ The oracle is not driven to the stage -- the stage is reached only from deep inside the attract
demo, five thousand frames in. So the reference is a PLAIN BOOT run to a frame that happens to be
inside the page, and the alignment is not counted by hand: the probe run reads the overlay's own
16-bit frame counter $7E:0008 out of the .wram sidecar, and `setup = probe - $0008`. That makes the
comparison self-locating -- if the boot's timing ever moves, the probe moves with it -- and it is
checked, not assumed: the probe must land on this entry ($0384) and this page ($0382).

⚠ The oracle's 512x480 dump is not a plain 2x image: ppu_putPixels writes scanline y to buffer row
y*2 + 16, so the 224 visible lines are IMAGE rows 8..231 of the 256x240 ctlib/fb.py decodes.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb as FB

ROM = 'Chrono_Trigger_USA_SNES.sfc'
CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/ct_field.exe')

# A boot frame that lands somewhere inside each page of entry 1. Only a SEED for the probe -- the
# real alignment comes out of $7E:0008 below, so these only have to be inside the right page.
PROBE = {(1, 1): 2840, (1, 2): 4540, (1, 3): 5565, (1, 4): 7549, (1, 5): 8813}


def oracle(frame, tag):
    out = 'build/_seqcmp_%s_%d.raw' % (tag, os.getpid())   # per process: two checks run this tool at once
    for p in (out, out + '.wram'):
        if os.path.exists(p):
            os.remove(p)
    subprocess.run([CT, ROM, '--fb', out, '--frames', str(frame)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    if not os.path.exists(out):
        sys.exit('seq-stage-cmp: the oracle wrote no framebuffer at boot frame %d' % frame)
    return out


def locate(entry, page):
    """Find the boot frame the stage's setup returned on, from the overlay's own counter."""
    probe = PROBE.get((entry, page))
    if probe is None:
        sys.exit('seq-stage-cmp: no probe frame known for entry %d page %d' % (entry, page))
    w = open(oracle(probe, 'probe') + '.wram', 'rb').read()
    got, pg, ctr = w[0x384], w[0x382], w[8] | (w[9] << 8)
    if got != 0x60 + entry:
        sys.exit('seq-stage-cmp: boot f%d is in $0384=%02X, not entry %d' % (probe, got, entry))
    if pg != page:
        sys.exit('seq-stage-cmp: boot f%d is on page %d, not %d' % (probe, pg, page))
    if not 0 < ctr < 105:
        sys.exit('seq-stage-cmp: boot f%d has $0008=%d -- not inside the stage' % (probe, ctr))
    return probe - ctr


def compare(entry, page, setup, k):
    shot = 'build/_seqcmp_ours_%d.raw' % os.getpid()
    if os.path.exists(shot):
        os.remove(shot)
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--seqstage', str(entry), str(k), str(page)],
                   env=dict(os.environ, CT_SEQSHOT=shot),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    if not os.path.exists(shot):
        sys.exit('seq-stage-cmp: entry %d frame %d was NEVER DRAWN' % (entry, k))
    ow, oh, ours = FB.load(shot)
    rw, rh, ref = FB.load(oracle(setup + k, 'orc'))
    bad = 0
    for y in range(oh):
        for x in range(ow):
            o = (y * ow + x) * 3
            r = ((y + 8) * rw + x) * 3
            if ours[o:o + 3] != ref[r:r + 3]:
                bad += 1
    print('seq-stage-cmp: entry %d page %d frame %d vs oracle boot f%d -> %d mismatching px of %d'
          % (entry, page, k, setup + k, bad, ow * oh))
    return bad


if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    e, p = int(sys.argv[1]), int(sys.argv[2])
    base = locate(e, p)
    worst = max(compare(e, p, base, int(a)) for a in sys.argv[3:])
    for tag in ('probe', 'orc', 'ours'):
        for x in ('', '.wram', '.vram', '.oam'):
            f = 'build/_seqcmp_%s_%d.raw%s' % (tag, os.getpid(), x)
            if os.path.exists(f):
                os.remove(f)
    sys.exit(0 if worst == 0 else 1)
