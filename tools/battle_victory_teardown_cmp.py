#!/usr/bin/env python3
r"""The victory's teardown C1/356D, port against the rule's cartridge, frame by frame.

    python tools/battle_victory_teardown_cmp.py            # both parties below
    python tools/battle_victory_teardown_cmp.py trio       # or one: solo / trio

GATO's fight made into a Dark Matter kill (tools/battle_ab.py's harness: saves/battle_re/gato_wait.state, the field WRAM
poked on both sides, Magus's eighth tech at the first menu). C1/356D runs in the victory's frame V (`$A0FE` 00 -> 01):
`STZ $A1 / JSR C1/1BAA / JSR C1/007E` and `INC $A1` twice more -- seat 0's queue removal in V, seat 1's in V+1, seat 2's
in V+2 -- then in V+3 `$95D5 = FF`, the C1/0C2D repaint (`$A6DF` takes the FE the removals left) and the pose ladder's
arm, whose copier publishes the first victory pose (`$9907`) in V+5 (disasm/notes/battle_end_ui_teardown.md sec. 9).

Both sides' bytes from V-2 to V+40 (V found on each side): the queue and the chooser ($95D5, the pending list $95D6..,
$A6D9-$A6DB, $A6DD-$A6DF), the menu ($9609, $95DB, $99E0, $A862), the stepper's $E5, the published poses $9907 and,
from V, the seat counter $A1 (before V the port's $A1 has other writers than the cartridge's: not this check's subject).
Not compared: the ladder's own bytes $5DF1 / $5DC9 / $5DBD / $5DC1 -- the port keeps them in its host (pcLadder*).

Prints per party `victory-teardown: <party> V c<frame> p<frame>, <frames> frames x <fields> fields, <n> differing`, then
the cartridge's frames (relative to V) of the three removals, the arm and the first publish.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import battle_ab as B  # noqa: E402

REGS = [(0x95D5, 1), (0x95D6, 5), (0xA6D9, 3), (0xA6DD, 3), (0xA0FE, 1), (0x9907, 3), (0x9609, 1), (0x95DB, 1),
        (0x99E0, 1), (0xA862, 1), (0x00E5, 1), (0x00A1, 1)]
PARTIES = {'solo': [6], 'trio': [6, 0, 1]}
LO, HI = -2, 40


def carried(rows):
    """{frame: {addr: hex}} with every value carried forward."""
    st, out = {}, {}
    for f in sorted(rows):
        st.update(rows[f])
        out[f] = dict(st)
    return out


def at(rows, keys, f):
    best = None
    for k in keys:
        if k > f:
            break
        best = k
    return rows[best] if best is not None else {}


def victory_frame(rows):
    """V: the frame `$A0FE` goes 00 -> 01 inside the fight (C1/3571)."""
    last = None
    for f in sorted(rows):
        v = rows[f].get(0xA0FE)
        if v is None:
            continue
        if last == '00' and v == '01' and f > 400:
            return f
        last = v
    return None


def one(name):
    party = PARTIES[name]
    os.environ['BATTLE_AB_TAG'] = '_vtd%d' % os.getpid()
    regs, orows, prows, _, _, _ = B.run(B.pokes_for(party), B.drive_tech(7), 1500, 'victory_' + name, REGS, raw=True)
    o, p = carried(orows), carried(prows)
    vo, vp = victory_frame(orows), victory_frame(prows)
    if vo is None or vp is None:
        print('victory-teardown: %s NO VICTORY (cartridge %s, port %s)' % (name, vo, vp))
        return 1
    ok_, pk_ = sorted(o), sorted(p)
    diff, frames = [], 0
    for r in range(LO, HI + 1):
        a, b = at(o, ok_, vo + r), at(p, pk_, vp + r)
        frames += 1
        for addr, _ in REGS:
            if addr == 0x00A1 and r < 0:
                continue
            if a.get(addr) != b.get(addr):
                diff.append('V%+d %04X c=%s p=%s' % (r, addr, a.get(addr), b.get(addr)))
    print('victory-teardown: %s V c%d p%d, %d frames x %d fields, %d differing' % (name, vo, vp, frames, len(REGS),
                                                                                  len(diff)))
    for d in diff[:12]:
        print('  ' + d)

    def first(addr, pred):
        for r in range(0, HI + 1):
            v = at(o, ok_, vo + r).get(addr)
            if v is not None and pred(v, r):
                return 'V+%d' % r
        return '-'
    a1 = [first(0x00A1, lambda v, r, s=s: v == '%02X' % s) for s in range(3)]
    print('victory-teardown: %s cartridge $A1 0/1/2 at %s, $95D5 FF at %s, $A6DF FF at %s, first publish at %s' % (
        name, '/'.join(a1), first(0x95D5, lambda v, r: v == 'FF'), first(0xA6DD, lambda v, r: v[4:6] == 'FF'),
        first(0x9907, lambda v, r: v != at(o, ok_, vo).get(0x9907))))
    return 1 if diff else 0


def main():
    names = [a for a in sys.argv[1:] if a in PARTIES] or list(PARTIES)
    bad = sum(one(n) for n in names)
    print('victory-teardown: %d/%d parties equal' % (len(names) - bad, len(names)))


if __name__ == '__main__':
    main()
