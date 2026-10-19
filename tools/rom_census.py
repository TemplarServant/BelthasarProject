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


# The harness's switches: a run that sets one runs the oracle or a reference beside the game (the ROM by design).
HARNESS_ENV = ('CT_AUDIO_SEEDSTATE=', 'CT_FIELDLOCK=', 'CT_AUDIO_SPC=ref', 'CT_AUDIO_SPC=lockstep')


def harness_env(line):
    e = line[len('# env '):]
    return '_FROM_ROM=' in e or e.startswith(HARNESS_ENV)


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
    ap.add_argument('--lzmiss', action='store_true', help='the files are CT_LZ_MISSLOG outputs: streams by caller')
    a = ap.parse_args()
    paths = [p for f in a.files for p in (glob.glob(f) or [f]) if os.path.exists(p)]
    syms, base = load_map(a.map)
    addrs = [s[0] for s in syms]
    if a.lzmiss:                                      # `OFFSET CALLER kind` lines (src/lz.c CT_LZ_MISSLOG)
        by = collections.defaultdict(set)
        for p in paths:
            for line in open(p):
                f = line.split()
                if len(f) >= 4 and not f[3].lower().startswith('belthasar'):
                    continue                          # another executable's callers: Belthasar's map cannot name them
                if len(f) >= 3:
                    fn, src = resolve(syms, addrs, base + int(f[1], 16)) if syms else ('?', '?')
                    by['%s %s %s' % (fn, src, f[2])].add(int(f[0], 16))
        total = set().union(*by.values()) if by else set()
        print('lz-miss: %d streams read from the ROM, by caller:' % len(total))
        for k, offs in sorted(by.items(), key=lambda kv: -len(kv[1])):
            print('lz-miss: %4d  %s  (banks %s)' % (len(offs), k, ' '.join(sorted({'%02X' % (0xC0 + (o >> 16)) for o in offs}))))
        return 0
    pages = {}                                        # page -> (function, source) of a first read seen
    harness = []                                      # runs that read the whole image (an oracle / a reference inside) or ask for the ROM (CT_*_FROM_ROM)
    for p in paths:
        lines = open(p).read().splitlines()
        if sum(1 for l in lines if l and not l.startswith('#')) > 900 or any(harness_env(l) for l in lines if l.startswith('# env')):
            harness.append(next((l[2:] for l in lines if l.startswith('# env CT_')), next((l[2:] for l in lines if l.startswith('#')), p)))
            continue
        for line in lines:
            f = line.split()
            if len(f) < 3 or line.startswith('#'):
                continue
            if len(f) >= 4 and not f[3].lower().startswith('belthasar'):
                continue                              # another executable's reads (ct.exe's tools): not the game's
            pg = int(f[0], 16)
            if pg not in pages:
                pages[pg] = resolve(syms, addrs, base + int(f[2], 16)) if syms else ('?', '?')
    print('rom-census: %d of 1024 pages read (%d KB) over %d runs' % (len(pages), len(pages) * 4, len(paths) - len(harness)))
    if harness:
        print('rom-census: (%d run(s) set apart: they read the whole image -- a harness with the oracle or a reference: %s)'
              % (len(harness), '; '.join(sorted(set(h[:60] for h in harness)))[:400]))
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
