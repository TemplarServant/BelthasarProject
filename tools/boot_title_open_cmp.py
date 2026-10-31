#!/usr/bin/env python3
r"""The title's opening at power-on, port against the cartridge: nothing lit before it, and its frames the cartridge's.

    python tools/boot_title_open_cmp.py [frames]

At power-on loc 1B1's `FF 02` runs op 0xFF's own fade inside the field's program (C0/3FBB-3FD1) and only then jumps to
C3:0000, whose title setup ($7E:5978) holds the screen black for its song load and spin before the pendulum's opening.
Both sides run from power-on: the cartridge under the rule (build/ct.exe --peek boot, CT_ORACLE_LOGICAL=1
CT_ORACLE_VIRTUAL_BEAM=1, PEEK_THUMBS every frame) and the port (build/Belthasar.exe --boot, CT_SHOTS_THUMBS every frame).
Each side is aligned on its own first lit frame (the boot's own length is another question), and the next <frames>
(default 360) are compared as 128x112 thumbnails (a pixel differs when a channel differs by more than 24):

    boot-title: first lit port f<P> cartridge c<C>; <n> frames from there: <k> differ [first +<i>]

A port that shows anything of the title before the real opening (e.g. the machine ticking under the op's fade) lights
early, and its first lit frame then lines up with the cartridge's opening while the next ones are black: those frames
differ.

Regression check: boot-title-opening-no-false-start-vs-rule-cartridge.
"""
import os
import struct
import subprocess
import sys

# tools/dis.py would shadow the standard library's `dis`, which numpy's import chain needs (as tools/attract_divergence.py).
sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_boottitle')
REC = 6 + 128 * 112 * 3


def load(path):
    d = open(path, 'rb').read()
    out = {}
    for i in range(len(d) // REC):
        f, _loc = struct.unpack('<IH', d[i * REC:i * REC + 6])
        out[f] = np.frombuffer(d[i * REC + 6:(i + 1) * REC], np.uint8).reshape(112, 128, 3).astype(np.int16)
    return out


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 360
    os.makedirs(OUT, exist_ok=True)
    ct, pt = os.path.join(OUT, 'cart.bin'), os.path.join(OUT, 'port.bin')
    for p in (ct, pt):
        if os.path.exists(p):
            os.remove(p)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1', PEEK_THUMBS=ct, PEEK_THUMBS_EVERY='1')
    subprocess.run([CT, '--peek', 'boot', os.path.join(OUT, 'c.raw'), '--frames', str(n + 200)], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_SHOTS_THUMBS=pt,
               CT_SHOTS_THUMBS_EVERY='1', CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=str(n + 150), CT_SHOTS_EXIT='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--boot'], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    C, P = load(ct), load(pt)
    lit = lambda D: min((f for f in D if D[f].mean() > 0.5), default=None)
    fc, fp = lit(C), lit(P)
    if fc is None or fp is None:
        sys.exit('boot-title: nothing lit (cartridge %s, port %s)' % (fc, fp))
    bad, cmp = [], 0
    for k in range(n):
        if fc + k in C and fp + k in P:
            cmp += 1
            if (np.abs(C[fc + k] - P[fp + k]).max(axis=2) > 24).any():
                bad.append(k)
    print('boot-title: first lit port f%d cartridge c%d; %d frames from there: %d differ%s'
          % (fp, fc, cmp, len(bad), (' [first +%d]' % bad[0]) if bad else ''))


if __name__ == '__main__':
    main()
