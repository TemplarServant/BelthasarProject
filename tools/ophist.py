"""Field event-VM opcode USAGE HISTOGRAM + byte-exact runner coverage.

This is the tool that picks the next porting target. The project's rule is to choose by
MEASUREMENT rather than by guess ("port the two script JUMPs -- picked by measuring opcode
usage, not by guessing"), and for three sessions the measurement was re-derived ad hoc and
then lost, which is how the denominator drifted from 114848 to a mis-remembered 113011.

    python tools/ophist.py [rom]

TWO THINGS IT DOES NOT GUESS AT:

1. THE WALK FOLLOWS FN POINTERS, it does not scan the body linearly. A scene's script body
   holds data as well as code, and a linear walk desyncs into it and invents sites -- 155966
   instead of 114848, a 36% overcount, with the surplus landing on whatever byte values the
   data happens to contain. Each object's 16-entry fn-pointer table is the only honest set of
   entry points, and a body offset already walked is counted ONCE, so functions that several
   slots share do not inflate their opcodes.

2. THE PORTED SET IS READ OUT OF src/move.c, not written down here. A hand-kept list of "what
   the runner can execute" has drifted twice in this project's history (--portrun's claim list,
   twice), and both times the actors sitting on the missing entries were invisible rather than
   broken -- which does not look like a bug, it looks like a clean run.
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.ctlib import script as S


def histogram(rom):
    """-> (Counter of opcode -> static reachable sites, number of scenes parsed)."""
    assets = S._assets_script()
    hist = collections.Counter()
    scenes = 0
    for sc in range(512):
        try:
            objs, body, _ = S.load(rom, sc)
        except Exception:
            continue                      # unused/!parseable header
        if not objs:
            continue
        scenes += 1
        seen = {}                         # body offset -> opcode; shared code counts once
        for pts in objs:
            for start in set(pts):
                if start < 0 or start >= len(body):
                    continue
                try:
                    for o, op, _ln in assets.iter_ops(body, start, limit=100000):
                        if o in seen:
                            break         # this path has already been walked
                        seen[o] = op
                except Exception:
                    pass                  # a truncated tail is not a reason to drop the scene
        for op in seen.values():
            hist[op] += 1
    return hist, scenes


def ported_opcodes(path='src/move.c'):
    """The runner's REAL dispatch set, read from the switch in CtFieldRunnerOne."""
    src = open(path, encoding='utf-8', errors='replace').read()
    body = src[src.index('int CtFieldRunnerOne'):]
    return set(int(x, 16) for x in re.findall(r'case 0x([0-9A-Fa-f]{2}):', body))


def main():
    rom_path = sys.argv[1] if len(sys.argv) > 1 else 'Chrono_Trigger_USA_SNES.sfc'
    rom = _romlib.load(rom_path)
    hist, scenes = histogram(rom)
    ported = ported_opcodes()
    total = sum(hist.values())
    covered = sum(c for op, c in hist.items() if op in ported)
    print('scenes parsed: %d   static fn-reachable op sites: %d   distinct opcodes: %d'
          % (scenes, total, len(hist)))
    print('byte-exact runner: %d opcodes ported, coverage %d/%d = %.1f%%'
          % (len(ported), covered, total, 100.0 * covered / total))
    print('\nTOP UNPORTED BY STATIC USAGE (the next targets):')
    shown = 0
    for op, c in hist.most_common():
        if op in ported:
            continue
        print('  0x%02X  %6d  %.2f%%' % (op, c, 100.0 * c / total))
        shown += 1
        if shown >= 20:
            break


if __name__ == '__main__':
    main()
