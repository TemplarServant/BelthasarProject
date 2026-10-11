#!/usr/bin/env python3
r"""The battle module's end tails against the rule's cartridge (disasm/notes/battle_module_tail.md).

    python tools/battle_module_tail_cmp.py

tools/battle_ab.py's synthetic fight, Magus solo (`tech 6 6`: his Black Hole kills GATO), both sides from the same WRAM,
watching what the module's end chain writes:

  xp       the block's XP +$13 ($5E40:3)  -- C1/F93E, C1/83F2: BEFORE FD/AD17's results messages
  29AD     the RNG cursor handed back     -- C1/8443
  sheet    Magus's sheet curHP ($27E3:2)  -- C1/B442's writeback, C1/83FC
  records  the staging record $29B0:5     -- CF/FC08-FC71 (C1/845E -> CF/FBE5)
  ack      $0117 -> 00                    -- the field's C0/18BD

Per quantity: the frame it changes and the value it takes, on each side. The cartridge runs the chain with no wait in
between (one frame under the rule); the port ran it all at the ack until battleModuleTail.

    module-tail: <q> cartridge f<N>=<value> port f<M>=<value> (<M-N>)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import battle_ab as B  # noqa: E402

REGS = [(0x5E40, 3), (0x29AD, 1), (0x27E3, 2), (0x29B0, 5), (0x0117, 1)]
NAMES = {0x5E40: 'xp', 0x29AD: '29AD', 0x27E3: 'sheet', 0x29B0: 'records', 0x0117: 'ack'}
TOTAL = 3200


def last_change(ch, after):
    """The first change at or after frame `after` (the fight's end), or None."""
    for f, v in ch:
        if f >= after:
            return f, v
    return None


def main():
    os.chdir(B.ROOT)
    B.regions = lambda: REGS
    B.fields = lambda: [(NAMES[a], a, 0, n) for a, n in REGS]
    regs, otl, ptl, cth, pth, plog = B.run(B.pokes_for([6]), B.drive_tech(6), TOTAL, 'module_tail')
    ok = True
    for a, n in REGS:
        q = NAMES[a]
        o = last_change(otl[q], 1000)          # (the fight's own writes end before f1000: the boot, Black Hole)
        p = last_change(ptl[q], 1000)
        if not o or not p:
            print('module-tail: %s cartridge %s port %s -> MISSING' % (q, o, p))
            ok = False
            continue
        if o[1] != p[1]:
            ok = False
        print('module-tail: %s cartridge f%d=%X port f%d=%X (%+d)' % (q, o[0], o[1], p[0], p[1], p[0] - o[0]))
    print('module-tail: values %s' % ('IDENTICAL' if ok else 'DIFFER'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
