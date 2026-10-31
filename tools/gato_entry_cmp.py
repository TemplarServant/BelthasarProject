#!/usr/bin/env python3
r"""GATO through the battle's boot, port against the rule's cartridge: his box, every frame from the trigger.

    python tools/gato_entry_cmp.py

From op D8's trigger ($0117 = 21, f218) to the battle's first OAM (c296) the cartridge's picture keeps GATO as he
stood; the port draws him from the engine (field.c battleGatoRender) from the first frame, and this checks that his
box does not move. Regression check: battle-gato-entry-stands-still-vs-rule-cartridge.

The cartridge: saves/battle_re/gato_wait.state under the rule, A held on f0-7, `--peek` k + 2 (the alignment P + 2).
The port: the same state's WRAM (--loadwram), A held on f1-8 (its key is the oracle's + 1), CT_SHOTS. Compared:
GATO's box (rows 45-124, columns 95-149 of the 256x224 picture) on frames 212-299:
  gato-entry: GATO's box differs on <n> of 88 frames: <frames>
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import peekshots  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_gato_entry')
STATE = os.path.join(ROOT, 'saves', 'battle_re', 'gato_wait.state')
RULE = dict(os.environ, CT_ORACLE_LOGICAL='1')
FRAMES = range(212, 300)
Y0, Y1, X0, X1 = 45, 125, 95, 150


def box(rgb, w):
    return bytes(b for y in range(Y0, Y1) for b in rgb[(y * w + X0) * 3:(y * w + X1) * 3])


def oracle_all(script, ks, region):
    """Every frame k's crop from one oracle run (PEEK_SHOTS, tools/ctlib/peekshots.py: the same bytes as a --peek cut
    to k)."""
    got = peekshots.shots(STATE, script, ks, env=RULE, prefix=os.path.join(OUT, 'c'))
    out = {}
    for k in ks:
        fb = open(got[k], 'rb').read()
        out[k] = bytes(fb[((y + 8) * 2 * 512 + x * 2) * 4 + c] for y, x in region for c in (3, 2, 1))
    peekshots.clean(got)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    raw = os.path.join(OUT, '_seed.raw')
    subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT, env=RULE, capture_output=True)
    lw = os.path.join(OUT, '_seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(map(str, FRAMES)), CT_SHOTS_EXIT='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '6', '--loadwram', lw, '--walkscript',
                    '.:1,a:8,.:400'], cwd=ROOT, env=env, capture_output=True)
    shots = oracle_all('100:8,0:%d' % (max(FRAMES) + 2 - 8), [k + 2 for k in FRAMES],
                       [(y, x) for y in range(Y0, Y1) for x in range(X0, X1)])
    cart = {k: shots[k + 2] for k in FRAMES}
    bad = []
    for k in FRAMES:
        d = open(os.path.join(OUT, 'p_f%d.raw' % k), 'rb').read()
        w, h = struct.unpack('<II', d[:8])
        if box(d[8:], w) != cart[k]:
            bad.append(k)
    print("gato-entry: GATO's box differs on %d of %d frames: %s" % (len(bad), len(FRAMES), ' '.join(map(str, bad))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
