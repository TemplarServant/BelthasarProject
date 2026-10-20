#!/usr/bin/env python3
r"""A battle pilot for route authoring: plays a fight on the rule's cartridge from a state, deciding each command from the
WRAM when a PC's command window opens, and prints the pad it pressed (the playthrough's grammar).

    python tools/battle_pilot.py <start.state> <out.state> [--heal PCT] [--max FRAMES]

Each step reads the cartridge's WRAM and, when a PC's window is open and idle -- `$95D5` the seat (0..2; 80 off the
fight), `$95DB` = 0 (the command row, not a submenu), `$9609` = 0 (no target select) -- picks:
  * an ITEM (a Tonic) on the living PC with the lowest HP ratio, when that ratio is under --heal (default 35%) and the
    bag holds one: the command row `$95DC + seat` walked to row 2 (Item), A, the Tonic's row in the list, A, the ally
    index `$9614` walked to that PC, A. The ally list is the user first, then the others in seat order (measured on the
    sanctum's Diablos fight: Lucca's index 0 heals Lucca, 1 Crono, 2 Frog);
  * otherwise ATTACK: row 0, A, A (the default target).
Presses are 2 frames with 10-frame gaps (the menu reads one key per 4-frame pass). A submenu or target select still open
after 12 steps (a press that missed its pass) is backed out with B. Between decisions it idles 4 frames; it stops when
control is back on the field ($011F != 0) or after --max frames. Combatant blocks: `$5E2D + c * 0x80`, the current HP at
+3, the max at +5. The bag: `$2400` ids / `$2500` counts; the battle's list shows the usable items in bag order (the
Pendant is not one).

This is an AUTHORING tool: the route it writes is the cartridge's own play, compared with the port like any other leg.
playthrough.py's route_walk uses Pilot for the fights it meets.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

TONIC = 0xBD
REVIVE = 0xC7
FROG = 4                                 # the character id at $2980 + seat
USABLE = set(range(0xBD, 0xD0))           # the battle's item list: the consumables (Tonic BD .. ), not the Pendant D6


def rd16(w, a):
    return w[a] | (w[a + 1] << 8)


def press(seq):
    out = []
    for k in seq:
        out += [(k, 2), (0, 10)]
    return out


def walk(cur, want):
    return [0x20] * (want - cur) if want > cur else [0x10] * (cur - want)


def in_fight(w):
    return w[0x95D5] != 0x80


class Pilot:
    def __init__(self, heal=0.35):
        self.heal, self.spent, self.stuck, self.revived = heal, 0, 0, 0

    def decide(self, w):
        seat = w[0x95D5]
        pcs = [s for s in range(3) if w[0x2980 + s] < 0x80]
        lives = []
        for s in pcs:
            b = 0x5E2D + s * 0x80
            hp, mx = rd16(w, b + 3), rd16(w, b + 5)
            if hp > 0 and mx > 0:
                lives.append((hp / mx, s))
        bag = [(w[0x2400 + i], w[0x2500 + i]) for i in range(0x100) if w[0x2400 + i] in USABLE and w[0x2500 + i]]
        row = w[0x95DC + seat]
        tonics = sum(n for iid, n in bag if iid == TONIC) - self.spent      # chosen ones the bag may not show yet
        order = [seat] + [s for _, s in sorted(lives, key=lambda t: t[1]) if s != seat]
        dead = [s for s in pcs if rd16(w, 0x5E2D + s * 0x80 + 3) == 0 and s != seat]
        revives = [i for i, (iid, _) in enumerate(bag) if iid == REVIVE]
        if dead and revives and self.revived < sum(n for iid, n in bag if iid == REVIVE):
            # a fallen ally: a Revive (the ally list holds every party member, the user first, then seat order)
            allord = [seat] + [s for s in pcs if s != seat]
            self.revived += 1
            return 'revive->%d' % dead[0], press(walk(row, 2) + [0x100]) + [(0, 6)] + \
                press(walk(0, revives[0]) + [0x100]) + [(0, 6)] + press(walk(0, allord.index(dead[0])) + [0x100])
        if lives and w[0x2980 + seat] == FROG and rd16(w, 0x5E2D + seat * 0x80 + 7) >= 2:
            ratio, who = min(lives)
            if ratio < self.heal and who in order:
                # Frog's first tech, Slurp (one ally): row 1 (Tech), A, the list's row 0, A, the ally, A
                return 'slurp->%d' % who, press(walk(row, 1) + [0x100]) + [(0, 6)] + press([0x100]) + [(0, 6)] + \
                    press(walk(0, order.index(who)) + [0x100])
        if lives and tonics > 0:
            ratio, who = min(lives)
            rows = [i for i, (iid, _) in enumerate(bag) if iid == TONIC]
            if ratio < self.heal and rows:
                tgt = order.index(who) if who in order else 0
                self.spent += 1
                return 'tonic->%d' % who, press(walk(row, 2) + [0x100]) + [(0, 6)] + \
                    press(walk(0, rows[0]) + [0x100]) + [(0, 6)] + press(walk(0, tgt) + [0x100])
        return 'attack', press(walk(row, 0) + [0x100]) + [(0, 6)] + press([0x100])

    def step(self, w):
        """The pad for the next stretch of a fight (and a note when it decided something)."""
        self.stuck = self.stuck + 1 if w[0x95D5] < 3 and (w[0x95DB] or w[0x9609]) else 0
        if self.stuck > 12:
            self.stuck = 0
            return press([0x01]), 'B (a menu left open)'
        if w[0x95D5] < 3 and w[0x95DB] == 0 and w[0x9609] == 0:
            what, pad = self.decide(w)
            hp = ' '.join('%d/%d' % (rd16(w, 0x5E30 + s * 0x80), rd16(w, 0x5E32 + s * 0x80)) for s in range(3))
            return pad, 'seat %d %s (hp %s)' % (w[0x95D5], what, hp)
        return [(0, 4)], None


def main():
    import playthrough as P
    a = sys.argv[1:]
    if len(a) < 2:
        sys.exit(__doc__)
    pilot = Pilot(float(a[a.index('--heal') + 1]) / 100 if '--heal' in a else 0.35)
    cap = int(a[a.index('--max') + 1]) if '--max' in a else 12000
    cur = a[1]
    P.oracle_play(a[0], [(0, 1)], cur)
    pad, t = [(0, 1)], 1
    while t < cap:
        w = P.wram_of(cur)
        if w[0x011F]:
            print('pilot: control back at +%d' % t)
            break
        step, note = pilot.step(w)
        if note:
            print('pilot: +%d %s' % (t, note))
        nxt = cur + '.n'
        P.oracle_play(cur, step, nxt)
        os.replace(nxt, cur)
        pad += step
        t += sum(n for _, n in step)
    merged = []
    for mk, n in pad:
        if merged and merged[-1][0] == mk and mk == 0:
            merged[-1][1] += n
        else:
            merged.append([mk, n])
    print('pad: %s' % P.btnscript([tuple(x) for x in merged]))


if __name__ == '__main__':
    main()
