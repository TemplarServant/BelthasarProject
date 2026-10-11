#!/usr/bin/env python3
r"""PEEK_SHOTS against a `--peek` per frame: the one-run dumps must be the same bytes (tools/ctlib/peekshots.py).

    python tools/peekshots_selftest.py

Three drives -- the field X menu on the logical oracle, GATO's fight under the rule, the attract from power-on -- each
sampled at a few frames; every frame's buffer and its .wram/.vram/.oam sidecars are compared with the dump of a separate
`--peek` whose script is cut to that frame:
  peekshots: <drive> <n> frames, <k> differ
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import peekshots  # noqa: E402

ROOT = peekshots.ROOT
OUT = os.path.join(ROOT, 'build', '_peekshots_selftest')
LOGICAL = dict(os.environ, CT_ORACLE_LOGICAL='1')
DRIVES = [
    ('x-menu', 'saves/battle_re/party3_field.state', '0:4,200:2,0:54,1:2,0:100', [3, 20, 55, 69, 90], LOGICAL),
    ('gato', 'saves/battle_re/gato_wait.state', '100:8,0:400', [1, 9, 218, 296, 300], LOGICAL),
    ('boot', 'boot', '0:700', [1, 250, 699], dict(os.environ)),
]


def one(state, script, n, env, tag):
    out = os.path.join(OUT, '%s_%d.raw' % (tag, n))
    subprocess.run([peekshots.CT, '--peek', state, out, '--btnscript', peekshots.cut(script, n)], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    bad_total = 0
    for tag, state, script, frames, env in DRIVES:
        got = peekshots.shots(state, script, frames, env=env, sidecars=True)
        with ThreadPoolExecutor(8) as ex:
            ref = dict(zip(frames, ex.map(lambda n: one(state, script, n, env, tag), frames)))
        bad = []
        for n in frames:
            for ext in ('', '.wram', '.vram', '.oam'):
                a, b = got[n] + ext, ref[n] + ext
                if not (os.path.exists(a) and os.path.exists(b)) or open(a, 'rb').read() != open(b, 'rb').read():
                    bad.append('f%d%s' % (n, ext))
        peekshots.clean(got)
        peekshots.clean(ref)
        bad_total += len(bad)
        print('peekshots: %s %d frames, %d differ%s' % (tag, len(frames), len(bad), (' ' + ' '.join(bad)) if bad else ''))
    return 1 if bad_total else 0


if __name__ == '__main__':
    sys.exit(main())
