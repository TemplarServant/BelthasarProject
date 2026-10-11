#!/usr/bin/env python3
r"""The CPU cost of a ROM routine's LEGS, measured on the oracle: non-refresh master cycles between marker PCs.

    python tools/cycle_legs.py <beam trace> <PC> <PC> ... [--first-after PC]

<beam trace> is `ct.exe --cputrace <state> <frames> <out>` run with CPUTRACE_BEAM=1 (every instruction, its v/h beam
position and the master-cycle counter c=). Each PC is a 24-bit address `BB:AAAA` (C2:80B2). The legs are the spans
between the FIRST occurrence of each marker, in the order given, after `--first-after` (default: the first marker).
Refresh is excluded the way the logical oracle excludes it: 40 master cycles each time the beam crosses h = 536
(tools/ctlib/beam.py; a phase hold does not move the beam, so it adds none). Prints one line per leg:

    leg C2:80B2 -> C2:80B6: 12345 cycles (0.036 F)      F = 346,886 = one frame's non-refresh cycles
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import beam  # noqa: E402

F = beam.F


def legs(trace, marks, after=None):
    i = 0
    if after:
        while i < len(trace) and trace[i][0] != after:
            i += 1
    stamps = []
    for mk in marks:
        while i < len(trace) and trace[i][0] != mk:
            i += 1
        stamps.append(trace[i] if i < len(trace) else None)
    res = [None if (a is None or b is None) else beam.nonrefresh(a, b) for a, b in zip(stamps, stamps[1:])]
    return stamps, res


if __name__ == '__main__':
    args = sys.argv[1:]
    after = None
    if '--first-after' in args:
        k = args.index('--first-after')
        after = args[k + 1]
        del args[k:k + 2]
    tr = beam.load(args[0])
    marks = args[1:]
    stamps, res = legs(tr, marks, after or marks[0])
    for (a, b), c in zip(zip(marks, marks[1:]), res):
        print('leg %s -> %s: %s cycles%s' % (a, b, c if c is not None else 'MISSING',
                                           '' if c is None else ' (%.3f F)' % (c / F)))
    tot = [c for c in res if c is not None]
    print('total %s -> %s: %d cycles (%.3f F)' % (marks[0], marks[-1], sum(tot), sum(tot) / F))
