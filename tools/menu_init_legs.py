#!/usr/bin/env python3
r"""Every leg of the menu open's NMI-off stretch (C0/0B5D .. C2/8087), side by side over several beam traces.

    python tools/menu_init_legs.py NAME=trace.txt [NAME=trace.txt ...]

The traces are `ct.exe --cputrace` with CPUTRACE_BEAM=1 over a menu open (any caller: the X menu's C0/1958, op C8's
C0/0CEA). Each leg is the non-refresh cycles between the first occurrences of its two markers (tools/ctlib/beam.py).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import beam  # noqa: E402

LEGS = [('C0:0B5D', 'C2:8000', 'C0/0B4E rest + C0/011B + JSL'),
        ('C2:8000', 'C2:8068', 'C2/8000 prologue'),
        ('C2:8068', 'C2:80EE', 'C2/8096 head + 5 sound posts'),
        ('C2:80EE', 'C2:80F3', 'C2/940D PPU/VRAM/WRAM'),
        ('C2:80F3', 'C2:8124', 'fills'),
        ('C2:8124', 'C2:8127', 'C2/968D graphics'),
        ('C2:8127', 'C2:812A', 'C2/D156 slot scan'),
        ('C2:812A', 'C2:812D', 'C2/984A'),
        ('C2:812D', 'C2:8130', 'C2/8663 queue drain'),
        ('C2:8130', 'C2:8133', 'C2/816A roster'),
        ('C2:8133', 'C2:8136', 'C2/92F4 config'),
        ('C2:8136', 'C2:8139', 'C2/F3CA windows'),
        ('C2:8139', 'C2:813C', 'C2/9875 items/stats'),
        ('C2:813C', 'C2:806B', 'HP-bar scale + RTS'),
        ('C2:806B', 'C2:8087', 'C2/806B .. NMI on')]


def run(path):
    t = beam.load(path)
    idx = {}
    for i, r in enumerate(t):
        idx.setdefault(r[0], i)
    out = []
    for a, b, _ in LEGS:
        ia = idx.get(a)
        ib = None
        if ia is not None:
            ib = next((i for i in range(ia, len(t)) if t[i][0] == b), None)
        out.append(None if ia is None or ib is None else beam.nonrefresh(t[ia], t[ib]))
    ia, ib = idx.get('C0:0B5D'), idx.get('C2:8087')
    total = beam.nonrefresh(t[ia], t[ib]) if ia is not None and ib is not None else None
    return out, total


def main():
    cols = []
    for a in sys.argv[1:]:
        name, path = a.split('=', 1)
        cols.append((name, run(path)))
    print('%-32s' % 'leg' + ''.join('%14s' % n for n, _ in cols))
    for k, (_, _, label) in enumerate(LEGS):
        print('%-32s' % label + ''.join('%14s' % (c[1][0][k] if c[1][0][k] is not None else '-') for c in cols))
    print('%-32s' % 'TOTAL (F)' + ''.join('%14s' % ('%d' % c[1][1] if c[1][1] else '-') for c in cols))
    print('%-32s' % '' + ''.join('%14s' % ('%.3f' % (c[1][1] / beam.F) if c[1][1] else '-') for c in cols))


if __name__ == '__main__':
    main()
