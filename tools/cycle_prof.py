#!/usr/bin/env python3
r"""Where a span of ROM code spends its cycles, from a beam trace: per-PC and per-routine totals.

    python tools/cycle_prof.py <beam trace> <from PC> <to PC> [--nth N] [--top K]

<beam trace>: `ct.exe --cputrace <state> <frames> <out>` with CPUTRACE_BEAM=1. The span runs from the N-th occurrence
(default 1) of <from PC> to the next <to PC>. Each instruction is charged the c= delta to the next traced instruction,
less the refreshes between them (tools/ctlib/beam.py). Prints the top K PCs (count, cycles) and the
totals per 16-byte-aligned code block, the quickest way to see which loop owns a routine's cost.
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import beam  # noqa: E402


def main():
    args = sys.argv[1:]
    nth, top = 1, 30
    if '--nth' in args:
        k = args.index('--nth'); nth = int(args[k + 1]); del args[k:k + 2]
    if '--top' in args:
        k = args.index('--top'); top = int(args[k + 1]); del args[k:k + 2]
    path, a, b = args[0], args[1], args[2]
    rows = beam.load(path)
    i, seen = 0, 0
    while i < len(rows):
        if rows[i][0] == a:
            seen += 1
            if seen == nth:
                break
        i += 1
    j = i + 1
    while j < len(rows) and rows[j][0] != b:
        j += 1
    per, cnt = collections.Counter(), collections.Counter()
    for k in range(i, j):
        p, q = rows[k], rows[k + 1]
        per[p[0]] += beam.nonrefresh(p, q)
        cnt[p[0]] += 1
    total = sum(per.values())
    print('span %s (#%d) -> %s: %d instructions, %d cycles' % (a, nth, b, sum(cnt.values()), total))
    for pc, c in per.most_common(top):
        print('  %s x%-6d %9d  %5.1f%%' % (pc, cnt[pc], c, 100.0 * c / max(1, total)))
    blk = collections.Counter()
    for pc, c in per.items():
        bank, adr = pc.split(':')
        blk['%s:%03Xx' % (bank, int(adr, 16) >> 4)] += c
    print('by 16-byte block:')
    for k2, c in blk.most_common(12):
        print('  %s %9d  %5.1f%%' % (k2, c, 100.0 * c / max(1, total)))


if __name__ == '__main__':
    main()
