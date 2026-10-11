"""Many oracle frames from ONE `ct.exe --peek` run (src/main.c PEEK_SHOTS).

A frame of `--peek <state> <out> --btnscript S` is a pure function of the state and the buttons up to it, so the dump
after n frames is the same whether the run stops at n or goes on. `shots(state, script, frames)` runs the script once,
cut to the last frame wanted, and returns {n: path of the dump after n frames} -- byte-identical to what
`--peek <state> <out> --btnscript <S cut to n frames>` writes to <out> (graded by tools/peekshots_selftest.py).

    from ctlib import peekshots
    got = peekshots.shots('saves/x.state', '0:4,200:2,0:100', [10, 11, 12], env=dict(os.environ, CT_ORACLE_LOGICAL='1'))
    raw = open(got[10], 'rb').read()      # 512x480 [pad,B,G,R]; sidecars=True adds <path>.wram/.vram/.oam
"""
import itertools
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CT = os.path.join(ROOT, 'build', 'ct.exe')
_seq = itertools.count()


def cut(script, n):
    """The button script `hexbtn:frames,...` cut (or padded with no buttons) to exactly n frames."""
    out, left = [], n
    for tok in script.split(','):
        if left <= 0:
            break
        m, k = tok.split(':')
        k = min(int(k), left)
        out.append('%s:%d' % (m, k))
        left -= k
    if left > 0:
        out.append('0:%d' % left)
    return ','.join(out)


def shots(state, script, frames, env=None, prefix=None, sidecars=False, cwd=ROOT):
    """Run the oracle once from `state` through `script` (cut to max(frames)); {n: dump path after n frames}."""
    frames = sorted(set(int(f) for f in frames))
    if prefix is None:
        prefix = os.path.join(ROOT, 'build', '_peekshots', '%d_%d' % (os.getpid(), next(_seq)))
    os.makedirs(os.path.dirname(prefix), exist_ok=True)
    e = dict(env if env is not None else os.environ)
    e['PEEK_SHOTS'] = prefix
    e['PEEK_SHOTS_AT'] = ','.join(map(str, frames))
    if sidecars:
        e['PEEK_SHOTS_SIDECARS'] = '1'
    else:
        e.pop('PEEK_SHOTS_SIDECARS', None)
    subprocess.run([CT, '--peek', state, prefix + '_end.raw', '--btnscript', cut(script, frames[-1])], cwd=cwd, env=e,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for ext in ('', '.wram', '.vram', '.oam'):
        try:
            os.remove(prefix + '_end.raw' + ext)
        except OSError:
            pass
    return {f: '%s_f%d.raw' % (prefix, f) for f in frames}


def clean(paths):
    for p in (paths.values() if isinstance(paths, dict) else paths):
        for ext in ('', '.wram', '.vram', '.oam'):
            try:
                os.remove(p + ext)
            except OSError:
                pass
