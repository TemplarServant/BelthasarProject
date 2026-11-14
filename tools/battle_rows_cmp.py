#!/usr/bin/env python3
r"""A fight's WRAM row by row, the port against the cartridge from one recipe state.

    python tools/battle_rows_cmp.py [name ...] [-v]

tools/battle_picture_seq_cmp.py grades a fight's pictures frame by frame; this grades the bytes under them. It plays a
recipe's state under its own pad on both sides (the port seeded with the state's WRAM, the same pad a frame later, as
the playthrough does) and compares the named cells on EVERY frame of a span -- the cartridge's `--monitor` row of a
frame against the port's row of the same frame:

    battlerows: <name> <state> f<a>-f<b> <cells>: <n> of <m> rows are the cartridge's; first: f<x> cartridge <v> port <w>

A row is the WRAM a frame leaves. `-v` prints both sides' rows on change.

Regression checks: the `battle-rows-*` family (docs/TESTING.md).
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.environ.get('BATTLEROWS_TMP') or os.path.join(ROOT, 'build', '_battlerows')
SCRATCH = os.path.join(WORK, 'run%d' % os.getpid())           # (one run's own files: the suite runs checks side by side)

# name: the recipe's state, the pad from it (button word, frames), the rows [a, b] and the cells (bank $7E address, bytes).
ROWS = {
    # GATO's fight won (the fair's recipe; A mashed four frames on, four off): the victory's exit. CF/FBE5's waits
    # (CF/FCFB) make no menu pass, and the frame service goes on raising the rebuild request every fourth frame: `$993A`
    # 01 on row f2149, 02 on f2153. CF/FCE9 `JSL CD/0006` -> CD/03B4 -> CD/03D3 `JSR CD/039E`: the fade's post and
    # `LDA #0F / JMP CD/3E3B` -- fifteen waits that are CD/3E75's, each the NMI and then `JSL C1/0012` = C1/10E3, the
    # menu pass (`STZ $993A / STZ $E5`). 00 on row f2156 and on every row to f2172 (the requests of f2157 / 61 / 65 / 69
    # consumed on their own frames); then CD/0516's waits are CD/3E7D's: 01 on f2173 .. 04 on f2185, and INIDISP's
    # shadow `$45` on f2186. The cells: the request, the idle tick's `$E5`, the group counter `$A4`, `$45`.
    'gato-won-exit': dict(state='saves/battle_re/gato_wait.state', pad=[(0x100, 4), (0, 4)] * 290, a=2100, b=2300,
                          cells=[(0x993A, 1), (0x00E5, 1), (0x00A4, 1), (0x0045, 1)]),
}


def per_frame(tl, keys, n):
    """A change list [(frame, {key: hex})] as one value per frame 0..n (None before the first row)."""
    out, cur, k = [], None, 0
    for f in range(n + 1):
        while k < len(tl) and tl[k][0] <= f:
            cur = ' '.join(tl[k][1][key] for key in keys); k += 1
        out.append(cur)
    return out


def run(name, verbose):
    d = ROWS[name]
    os.makedirs(SCRATCH, exist_ok=True)
    P.TMP = SCRATCH                                    # (P.wram_of's and the port's files)
    st = os.path.join(ROOT, d['state'])
    a, b = d['a'], d['b']
    n = b + 4
    pad, left = [], n
    for btn, q in d['pad']:
        q = min(q, left)
        if q > 0:
            pad.append((btn, q)); left -= q
    if left > 0:
        pad.append((0, left))
    mon = [('%04X' % ad, '7E%04X' % ad, '%04X' % ad, ln) for ad, ln in d['cells']]
    keys = [m[0] for m in mon]
    P.MON = mon
    o = P.oracle_timeline(st, pad, mon)
    p, _ = P.port_timeline(st, pad)
    ro, rp = per_frame(o, keys, n), per_frame(p, keys, n)
    same, first = 0, None
    for f in range(a, b + 1):
        if ro[f] is not None and ro[f] == rp[f]:
            same += 1
        elif first is None:
            first = f
    if verbose:
        lo = lp = None
        for f in range(a, b + 1):
            if ro[f] != lo or rp[f] != lp:
                print('  f%-5d cartridge %-24s port %s' % (f, ro[f], rp[f]))
                lo, lp = ro[f], rp[f]
    tail = '; first: f%d cartridge %s port %s' % (first, ro[first], rp[first]) if first is not None else ''
    print("battlerows: %s %s f%d-f%d %s: %d of %d rows are the cartridge's%s" % (
        name, os.path.basename(d['state']), a, b, ' '.join('$%s' % k for k in keys), same, b - a + 1, tail))
    return 0 if first is None else 1


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('-')]
    verbose = '-v' in sys.argv
    bad = [x for x in args if x not in ROWS]
    if bad:
        print('battlerows: no such case: %s (cases: %s)' % (' '.join(bad), ' '.join(ROWS)))
        return 2
    rc = 0
    for name in (args or list(ROWS)):
        rc |= run(name, verbose)
    return rc


if __name__ == '__main__':
    sys.exit(main())
