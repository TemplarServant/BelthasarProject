#!/usr/bin/env python3
r"""Crono attacking an enemy whose counter takes no action: the RNG cursor and the enemy's HP, port against the rule's cartridge.

    python tools/pc_hit_eval_cmp.py [ids]          (hex enemy ids, default 48,1E: Diablos, Naga-ette)

A PC command whose first victim is an enemy arms the counter (C1/BB2D / BC4A / C011: `$B2C0 = 1`); at the action's close
C1/864B runs the counter loop C1/8671 (block 2 of each victim) and then C1/881C -> C1/8C09, the eval of every enemy whose
`$B2B6` is 0, with its draws (C1/AF22) -- also when the counter took no action. The port ran the eval only after a counter
that acted, so from a PC's first hit on such an enemy its RNG cursor `$26` ran a draw behind the cartridge's and every
roll after it differed (battle_ai_interp.md sec. 25.3, enemy C1).

GATO's arena (tools/battle_ab.py: saves/battle_re/gato_wait.state, the enemy id poked into `$1111`), Crono alone, Attack +
target (A, A) at f640 and every 400 frames after, three times; the cartridge under the rule (`--monitor`), the port seeded
with the same WRAM and SPC. Per enemy:

    pc-hit-eval: enemy <id> $26 identical (<n> changes)      (or: differs from f<F>: cartridge .. port ..)
    pc-hit-eval: enemy <id> HP identical (<n> changes)
"""
import os
import sys

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import battle_ab as B  # noqa: E402

EXTRA = [(0x0026, 1)]


def first_diff(c, p):
    k = 0
    while k < min(len(c), len(p)) and c[k] == p[k]:
        k += 1
    cf = c[k] if k < len(c) else None
    pf = p[k] if k < len(p) else None
    return min(x[0] for x in (cf, pf) if x), cf, pf


def main():
    ids = [int(x, 16) for x in (sys.argv[1] if len(sys.argv) > 1 else '48,1E').split(',')]
    _r, _f = B.regions, B.fields
    B.regions = lambda: _r() + EXTRA
    B.fields = lambda: _f() + [('$%04X' % a, a, 0, 1) for a, n in EXTRA]
    for eid in ids:
        f, drive = B.MENU_AT, []
        for _ in range(3):
            drive += [(f, 'a', 2), (f + 12, 'a', 2)]
            f += 400
        regs, otl, ptl, cth, pth, plog = B.run(B.pokes_for([0], extra={0x1111: eid}), drive, f + 200, 'pchit_%02X' % eid)
        for label, key in (('$26', '$0026'), ('HP', 'c6.HP')):
            # (the HP's first entry is the staging's: the cartridge's boot writes the block a few frames after the port's)
            c = [x for x in otl[key] if x[0] >= 300] if key != '$0026' else otl[key]
            p = [x for x in ptl[key] if x[0] >= 300] if key != '$0026' else ptl[key]
            if c == p:
                print('pc-hit-eval: enemy %02X %s identical (%d changes)' % (eid, label, len(c)))
            else:
                fr, cf, pf = first_diff(c, p)
                print('pc-hit-eval: enemy %02X %s differs from f%d: cartridge %s port %s' % (eid, label, fr, cf, pf))
    return 0


if __name__ == '__main__':
    sys.exit(main())
