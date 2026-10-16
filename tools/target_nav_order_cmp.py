#!/usr/bin/env python3
r"""The enemy target cursor's walk on Up / Down, port against the rule's cartridge: which enemy each key moves to and on
which frame -- $9614 (the cursor's index into $99C0) and $A62D (the lit targets) on every frame the select is open.

    python tools/target_nav_order_cmp.py [--only <text> | --skip <text>]   (the cases whose name holds the text)

C1/1561 runs the group handler (C1/1F79 -> C1/1FF8[$960D & 7F]) BEFORE the pass's own keys, and some handlers take the
pad themselves (dis.py 12169 0xE2 1 0, 122F5 0x34 1 0):
  0B / 0C / 0D (C1/2169, C1/2203, C1/21AF)  `$EF & 09` (Up|Right) -> C1/27FA, `$EF & 06` (Down|Left) -> C1/27FA: every
                                            direction steps to the NEXT entry;
  0F / 12 / 1A (C1/22F5, C1/225F)           `$EF & 09` -> C1/27FA (next), `$EF & 06` -> C1/2814 (previous): Down walks
                                            BACKWARDS through $99C0.
and leave `$960C = 80` (C1/216F / C1/27E8), so C1/15F4 `LDA $960C / BMI` skips the pass's plain walk C1/15F9 (`$EF & 05`
Down|Right -> C1/176C next, `$EF & 0A` Up|Left -> C1/1786 previous), which every other group takes. C1/27FA / 2814 step
$9614 through C1/20A9's list (the living enemies, X = 3..0A in order) with the same wrap as C1/176C / 1786, after C1/282D
finds an entry at all.

The fixture is tools/caster_radius_dispatch_cmp.py's: gato_wait staged with four Hetakes (tools/battle_sim.py, op 0x83)
at cells cmb3 12,8 / cmb4 6,7 / cmb5 8,3 / cmb6 10,10, Crono alone. Cyclone (tech 01, group 12): on the rule's cartridge
the four Downs move $9614 0 -> 3 -> 2 -> 1 -> 0 and the Ups 0 -> 1 -> 2 -> 3 -> 0; $A62D = the radius set around
$99C0[$9614] (C1/225F -> C1/2701 -> C1/27E8). Slash (tech 02, group 0B, the band): Down and Up both step forward. The Att
(group 07, C1/20A9 + the pass's own walk): Down forward, Up back, `$A62D` the cursor's enemy alone. Each case is
battle_ab.drive_tech(k) (or the Att's A) with the keys held 2 frames, 60 apart, between the select's open and its A.
The port: battle.c battleEnemyGroupStep / battleEnemyTargetPass / battleEnemyTargetWalk.

    target-nav <case>: select held cartridge f<a>-f<b> port f<a>-f<b>
    target-nav <case>: $9614 walk cartridge [<i> ..] port [<i> ..]
    target-nav <case>: $9614 $A62D on <n> frames, first difference <none | f<n> cartridge <..> port <..>>
    target-nav: <n>/<m> identical
"""
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path = [p for p in sys.path if os.path.abspath(p or '.') != TOOLS]
sys.path.insert(0, TOOLS)
import battle_ab as ab  # noqa: E402
sys.path.insert(0, TOOLS)
import battle_sim as bs  # noqa: E402

ROOT = os.path.dirname(TOOLS)
# (id, op2, tile x, tile y): op 0x83's `8B x y` puts the foot at (x * 16 + 8, y * 16 - 1) -- the cell (x, y - 1)
PLACED = [(0x5E, 3, 12, 9), (0x5E, 4, 6, 8), (0x5E, 5, 8, 4), (0x5E, 6, 10, 11)]
GAP = 60
# (name, Crono's tech row k for battle_ab.drive_tech -- None: the Att, the keys between the select's open and its A)
CASES = [('Cyclone, Down x4', 0, 'dddd'), ('Cyclone, Up x4', 0, 'uuuu'), ('Cyclone, Right / Left', 0, 'rrll'),
         ('Slash, Down / Up', 1, 'dduu'), ('Att, Down / Up', None, 'ddduuu')]


def main():
    os.chdir(ROOT)
    regs = [(0x9609, 1), (0x960C, 1), (0x9614, 1), (0xA62D, 4)]
    ok = n = 0

    def grade(line, same):
        nonlocal ok, n
        n += 1; ok += bool(same)
        print('target-nav ' + line)

    flt = sys.argv[1:3] if len(sys.argv) >= 3 and sys.argv[1] in ('--only', '--skip') else None
    for i, (name, k, keys) in enumerate(CASES):
        if flt and (flt[1] in name) != (flt[0] == '--only'):
            continue
        pk = ab.pokes_for([0], extra=bs.staging_pokes(PLACED))
        drive = ab.drive_tech(k) if k is not None else [(ab.MENU_AT, 'a', 2), (ab.MENU_AT + 12, 'a', 2)]
        at = drive[-1][0]                                  # the select's A: the keys first, GAP frames apart
        drive = drive[:-1] + [(at + GAP * j, b, 2) for j, b in enumerate(keys)] + [(at + GAP * len(keys), 'a', 2)]
        total = at + GAP * len(keys) + 30
        _, rc, rp, _, _, _ = ab.run(pk, drive, total, 'target_nav_%d' % i, regs=regs, raw=True)
        sides = {}
        for side, rows in (('cartridge', rc), ('port', rp)):
            sr = [bs.series(rows, a, total) for a in (0x9609, 0x9614, 0xA62D)]
            held = [f for f in range(total + 1) if sr[0][f] == '01']
            walk = []
            for f in held:
                if not walk or walk[-1] != sr[1][f]:
                    walk.append(sr[1][f])
            sides[side] = dict(held=held, walk=walk, sel={f: (sr[1][f], sr[2][f]) for f in held})
        c, p = sides['cartridge'], sides['port']
        span = lambda s: 'f%d-f%d' % (s['held'][0], s['held'][-1]) if s['held'] else '-'
        grade('%s: select held cartridge %s port %s' % (name, span(c), span(p)), span(c) == span(p))
        fmt = lambda w: ' '.join(str(int(x, 16)) if x else '?' for x in w)
        grade('%s: $9614 walk cartridge [%s] port [%s]' % (name, fmt(c['walk']), fmt(p['walk'])), c['walk'] == p['walk'])
        frames = sorted(set(c['held']) | set(p['held']))
        bad = next((f for f in frames if c['sel'].get(f) != p['sel'].get(f)), None)
        grade('%s: $9614 $A62D on %d frames, first difference %s' % (
            name, len(frames), 'none' if bad is None else 'f%d cartridge %s port %s' % (
                bad, ' '.join(c['sel'].get(bad) or ('-',)), ' '.join(p['sel'].get(bad) or ('-',)))), bad is None)
    print('target-nav: %d/%d identical' % (ok, n))
    return 0 if ok == n else 1


if __name__ == '__main__':
    sys.exit(main())
