#!/usr/bin/env python3
r"""Which ROM pages Belthasar still reads at run time, and which code reads them -- the ROM-free runtime's meter.

    python tools/rom_census.py 'build/_census/*.txt' [--map build/Belthasar.map] [--functions] [--pages]

The files are CT_ROM_CENSUS outputs (src/rom_census.h: `PAGE IMAGE CODEADDR` lines, one per 4 KB page a run read first).
The union is printed per bank (C0..FF), per source file (the code address resolved through the MSVC link map) and per
work package of the plan (memory rom-free-runtime-goal):

    rom-census: <n> of 1024 pages read (<KB> KB) over <k> runs
    rom-census: bank C0 <n> pages ...
    rom-census: <source>.c <n> pages (package)

--functions lists the function behind each page's first read; --pages lists every page. A run with no ROM read leaves
an empty file: that is the goal.
"""
import argparse
import bisect
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKAGE = {                                   # the plan's work packages, by the source that reads (rom-free-runtime-goal)
    'battle.c': 'G battle', 'move.c': 'D sprites', 'oam.c': 'D sprites', 'objslot.c': 'P1 small tables',
    'ct_vm.c': 'D sprites / P1', 'lz.c': 'A scene load', 'field_cost.c': 'A scene load', 'field.c': 'field (A/E/P1)',
    'menu.c': 'E text+menus', 'menu_cost.c': 'E text+menus', 'vwf.c': 'E text+menus', 'world.c': 'F overworld',
    'tileanim.c': 'P1 small tables', 'hdma_table.c': 'P1 small tables', 'wipe.c': 'P1 small tables',
    'audio.c': 'sound', 'romview.c': 'a table read no CT_ASSET_TABLE covers (the reader is its caller)', 'title_intro.c': 'B sequences', 'scene.c': 'A scene load', 'ct_shadow.c': 'debug',
}


def load_map(path):
    """[(address, name, object)] of the code symbols, sorted; and the preferred base."""
    syms, base = [], 0x140000000
    if not os.path.exists(path):
        return syms, base
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'\s*Preferred load address is ([0-9A-Fa-f]+)', line)
        if m:
            base = int(m.group(1), 16)
        m = re.match(r'\s*0001:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$', line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1), m.group(3)))
    syms.sort()
    return syms, base


def resolve(syms, addrs, rva):
    i = bisect.bisect_right(addrs, rva) - 1
    if i < 0:
        return '?', '?'
    _, name, obj = syms[i]
    src = obj.split(':')[-1].replace('.c.obj', '.c').replace('.cpp.obj', '.cpp')
    return name, src


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('files', nargs='+')
    ap.add_argument('--map', default=os.path.join(ROOT, 'build', 'Belthasar.map'))
    ap.add_argument('--functions', action='store_true')
    ap.add_argument('--pages', action='store_true')
    a = ap.parse_args()
    paths = [p for f in a.files for p in (glob.glob(f) or [f]) if os.path.exists(p)]
    syms, base = load_map(a.map)
    addrs = [s[0] for s in syms]
    pages = {}                                        # page -> (function, source) of a first read seen
    for p in paths:
        for line in open(p):
            f = line.split()
            if len(f) < 3:
                continue
            pg = int(f[0], 16)
            if pg not in pages:
                pages[pg] = resolve(syms, addrs, base + int(f[2], 16)) if syms else ('?', '?')
    print('rom-census: %d of 1024 pages read (%d KB) over %d runs' % (len(pages), len(pages) * 4, len(paths)))
    banks = collections.Counter(0xC0 + (pg >> 16) for pg in pages)
    print('rom-census: banks ' + ' '.join('%02X:%d' % (b, banks[b]) for b in sorted(banks)))
    bysrc = collections.Counter(src for _, src in pages.values())
    for src, n in bysrc.most_common():
        print('rom-census: %-16s %4d pages  (%s)' % (src, n, PACKAGE.get(src, '?')))
    if a.functions:
        byfn = collections.Counter('%s %s' % v for v in pages.values())
        for fn, n in byfn.most_common():
            print('rom-census:   %4d  %s' % (n, fn))
    if a.pages:
        for pg in sorted(pages):
            print('rom-census: page %02X:%04X  %s (%s)' % (0xC0 + (pg >> 16), pg & 0xFFFF, pages[pg][0], pages[pg][1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
