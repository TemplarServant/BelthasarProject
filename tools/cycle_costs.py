#!/usr/bin/env python3
r"""Each instruction's cost, per execution, from beam traces: the distribution of master cycles per PC.

    python tools/cycle_costs.py <beam trace> [<beam trace> ...] --range C2:5DC4-C2:5F06 [--range ...]

An instruction is charged the c= delta to the next traced instruction. A DRAM refresh (40 master cycles, once a line)
lands inside whichever instruction straddles it, so a PC's distribution is its cost plus, now and then, 40; a branch
has two costs (taken / not taken). Prints `PC  cost:count cost:count ...` with the costs ascending -- the base cost is
the smallest value that is not a refresh straddle of another.
"""
import collections
import re
import sys

rx = re.compile(r'^([0-9A-F]{2}):([0-9A-F]{4}) ([0-9A-F]{2})[0-9A-F]* .* P=(\S+) v=\s*(\d+) h=\s*(\d+) c=(\d+)')


def main():
    args = sys.argv[1:]
    ranges = []
    while '--range' in args:
        k = args.index('--range')
        a, b = args[k + 1].split('-')
        ranges.append((int(a.replace(':', ''), 16), int(b.replace(':', ''), 16)))
        del args[k:k + 2]
    dist = collections.defaultdict(collections.Counter)
    ops = {}
    for path in args:
        prev = None
        for line in open(path):
            if line.startswith('-- frame'):
                prev = None
                continue
            m = rx.match(line)
            if not m:
                continue
            pc = int(m.group(1) + m.group(2), 16)
            c = int(m.group(7))
            if prev is not None:
                ppc, pc0, flags = prev
                if any(lo <= ppc <= hi for lo, hi in ranges):
                    dist[ppc][c - pc0] += 1
                    ops[ppc] = flags
            prev = (pc, c, m.group(3) + ' ' + m.group(4))
    for pc in sorted(dist):
        d = dist[pc]
        print('%02X:%04X  op %-14s %s' % (pc >> 16, pc & 0xFFFF, ops[pc],
                                         ' '.join('%d:%d' % (k, d[k]) for k in sorted(d))))


if __name__ == '__main__':
    main()
