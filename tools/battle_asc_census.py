#!/usr/bin/env python3
r"""Which opcodes of the PC battle anim scripts the port's interpreter executes -- the static census.

    python tools/battle_asc_census.py [--scripts]

Runs `build/ct_field.exe --asccensus` (battle.c CtBattleAscCensus: every tech's, consumable's and physical selector's
bank-CE script, walked track by track with the interpreter's own opcode lengths, `1E ss` supers descended) and
classifies every opcode against battleAscTick's `switch (op)`, whose case labels this tool reads out of src/battle.c:

  run    the op has a case (or is a terminator / 1F / a class the tick handles before the switch);
  inert  its length is known but no case exists -- the track passes over it doing nothing;
  STOP   the interpreter cannot size it -- the track stops there (battleAscTick's `STOP unknown op`).

Prints, per op not run, the scripts that carry it; then the totals. `--scripts` also lists every script's
inert ops. An inert op is not necessarily a defect (some are visual only, some free counters), but every one is
behaviour the cartridge has and the port does not.
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def handled_ops():
    src = open(os.path.join(ROOT, 'src', 'battle.c'), encoding='utf-8', errors='replace').read().splitlines()
    start = next(i for i, l in enumerate(src) if l.startswith('static void battleAscTick('))
    sw = next(i for i in range(start, len(src)) if 'switch (op) {' in src[i])
    end = next(i for i in range(sw, len(src)) if src[i].strip().startswith('default:') and
               'facing/counter: inert' in src[i])
    ops = set()
    for l in src[sw:end]:
        for m in re.finditer(r'case 0x([0-9A-Fa-f]{2})\s*:', l):
            ops.add(int(m.group(1), 16))
    return ops


def terminators():
    """Every op whose handler (C1/7A6B) is C1/4CF0 (the track's end) or C1/4CF7 (the action's end): battleAnimOpClass
    sorts them before the switch, so they run with no case of their own (01, 2F, 4B, 56-5C, 7E, 86-97, ...)."""
    rom = open(os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'), 'rb').read()
    return {op for op in range(0xDB) if (rom[0x17A6B + op * 2] | rom[0x17A6C + op * 2] << 8) in (0x4CF0, 0x4CF7)}


def main():
    os.chdir(ROOT)
    out = subprocess.run([os.path.abspath('build/ct_field.exe'), '--asccensus'], capture_output=True, text=True).stdout
    run = handled_ops() | terminators() | {0x1F}     # terminators end the track before the switch; 1F is sized as 1
    scripts = []
    for l in out.splitlines():
        m = re.match(r'asccensus (\w+) (\w+) sel=(\w+) CE/(\w+) tracks=(\d+) ops=([\w,]*) stop=(\S+)', l)
        if m:
            scripts.append((m.group(1), m.group(2), m.group(4), [int(x, 16) for x in m.group(6).split(',') if x],
                            m.group(7)))
    inert = collections.defaultdict(list)
    stops = collections.defaultdict(list)
    for kind, sid, ptr, ops, stop in scripts:
        for op in ops:
            if op not in run:
                inert[op].append('%s %s' % (kind, sid))
        if stop != '-':
            stops[stop.split('@')[0]].append('%s %s (%s)' % (kind, sid, stop))
    print('asc-census: %d distinct scripts (%d techs, %d items, %d physicals); the interpreter has %d cases'
          % (len(scripts), sum(1 for s in scripts if s[0] == 'tech'), sum(1 for s in scripts if s[0] == 'item'),
             sum(1 for s in scripts if s[0] == 'phys'), len(run)))
    for op in sorted(stops, key=lambda k: -len(stops[k])):
        print('  STOP  op %s in %d script(s): %s' % (op, len(stops[op]), ', '.join(stops[op][:12])))
    for op in sorted(inert, key=lambda k: -len(inert[k])):
        print('  inert op %02X in %d script(s): %s%s' % (op, len(inert[op]), ', '.join(inert[op][:10]),
                                                           ' ...' if len(inert[op]) > 10 else ''))
    clean = [s for s in scripts if s[4] == '-' and all(op in run for op in s[3])]
    print('asc-census: %d / %d scripts use only executed ops; %d distinct inert ops, %d stop ops'
          % (len(clean), len(scripts), len(inert), len(stops)))
    if '--scripts' in sys.argv:
        for kind, sid, ptr, ops, stop in scripts:
            bad = [op for op in ops if op not in run]
            print('  %s %s CE/%s inert=%s stop=%s' % (kind, sid, ptr, ','.join('%02X' % o for o in bad) or '-', stop))


if __name__ == '__main__':
    main()
