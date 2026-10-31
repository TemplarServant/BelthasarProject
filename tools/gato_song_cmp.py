#!/usr/bin/env python3
r"""GATO's song (the tech $9877 = 10) against the rule's cartridge: the whole picture through the spotlight.

    python tools/gato_song_cmp.py

On the cartridge the cone is the arena's own colour math: the song program's BG3 beam on the sub screen (TS = 04)
added to BG1/BG2 (CGADSUB = 03, no half) over lines 0-160, the room's palettes faded down under it -- the floor shows
through the beam. Regression check: battle-gato-song-spotlight-is-the-arena-colour-math-vs-rule-cartridge.

Both sides from saves/battle_re/gato_wait.state with A held on f0-7 (the port's key one frame later) and nothing else:
the fight runs on its own and GATO sings at f1006-1305. Port frame k against the cartridge's `--peek` k + 2 (the
alignment P + 2), every 10th frame of the lit window f1060-1240:
  gato-song: f<k> <n> px differ
  gato-song: lit window max <n> px of 57344
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
OUT = os.path.join(ROOT, 'build', '_gato_song')
STATE = os.path.join(ROOT, 'saves', 'battle_re', 'gato_wait.state')
RULE = dict(os.environ, CT_ORACLE_LOGICAL='1')
FRAMES = range(1060, 1241, 10)


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
                    '.:1,a:8,.:1500'], cwd=ROOT, env=env, capture_output=True)
    full = [(y, x) for y in range(224) for x in range(256)]
    shots = oracle_all('100:8,0:%d' % (max(FRAMES) + 2 - 8), [k + 2 for k in FRAMES], full)
    cart = {k: shots[k + 2] for k in FRAMES}
    worst = 0
    for k in FRAMES:
        d = open(os.path.join(OUT, 'p_f%d.raw' % k), 'rb').read()
        p, c = d[8:], cart[k]
        n = sum(1 for i in range(0, 256 * 224 * 3, 3) if p[i:i + 3] != c[i:i + 3])
        worst = max(worst, n)
        print('gato-song: f%d %d px differ' % (k, n))
    print('gato-song: lit window max %d px of 57344' % worst)
    return 0


if __name__ == '__main__':
    sys.exit(main())
