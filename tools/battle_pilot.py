#!/usr/bin/env python3
r"""A battle pilot for route authoring: plays a fight on the rule's cartridge from a state, deciding each command from the
WRAM when a PC's command window opens, and prints the pad it pressed (the playthrough's grammar).

    python tools/battle_pilot.py <start.state> <out.state> [--heal PCT] [--max FRAMES]

Each step reads the cartridge's WRAM and, when a PC's window is open and idle -- `$95D5` the seat (0..2; 80 off the
fight), `$95DB` = 0 (the command row, not a submenu), `$9609` = 0 (no target select) -- picks:
  * an ITEM (a Tonic, or a Mid Tonic for a deficit over 100 or when no Tonic is left) on the living PC with the lowest
    HP ratio, when that ratio is under --heal (default 35%) and the bag holds one: the command row `$95DC + seat`
    walked to row 2 (Item), A; then, on the next steps, the menus' own cursors: the list's (`$95E6 + $95E5` over the
    records `$1580`, five bytes each -- remembered between turns when the Config's `$A0D5` says so: the prison's guards
    met it at row 2) walked to the item, A, and the ally select's `$9614` (an index into `$99C0`) walked to that PC, A;
  * otherwise ATTACK: row 0, A, A (the default target) -- or, with PILOT_TARGETS (enemy ids in hex, in order), row 0, A
    and the target select walked to the first living one; PILOT_TECH (seats, `0,1`) takes the seat's first tech instead
    of the attack while its MP holds 2; PILOT_DUAL (`a:b:row:mpA:mpB`, comma-separated) takes a double tech from seat
    a's window when seat b is ready (gauge 0, no command given) and both MPs hold -- Tech, the list `row` rows down -- and
    idles the window (at most 400 frames) while b's gauge still runs; PILOT_HEAL (percent) moves the heal
    threshold; PILOT_RUN holds L+R the whole fight (the party runs away) and picks nothing.
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
MIDTONIC = 0xBE
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
        self.spentMid, self.goal, self.pending = 0, None, {}
        self.prefer = [int(x, 16) for x in os.environ.get('PILOT_TARGETS', '').split(',') if x]
        self.techs = [int(x) for x in os.environ.get('PILOT_TECH', '').split(',') if x]
        self.duals = [[int(x) for x in d.split(':')] for d in os.environ.get('PILOT_DUAL', '').split(',') if d]
        self.waited = 0                                           # frames a window has idled for its partner's gauge
        self.busy = set()                                         # seats given a command whose gauge has not reloaded yet
        if os.environ.get('PILOT_HEAL'):                          # (route_walk's pilot takes the default threshold)
            self.heal = float(os.environ['PILOT_HEAL']) / 100

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
            self.goal = (REVIVE, dead[0])
            return 'revive->%d' % dead[0], press(walk(row, 2) + [0x100])
        if lives and w[0x2980 + seat] == FROG and rd16(w, 0x5E2D + seat * 0x80 + 7) >= 2:
            ratio, who = min(lives)
            if ratio < self.heal and who in order:
                # Frog's first tech, Slurp (one ally): row 1 (Tech), A, the list's row 0, A, the ally, A
                return 'slurp->%d' % who, press(walk(row, 1) + [0x100]) + [(0, 6)] + press([0x100]) + [(0, 6)] + \
                    press(walk(0, order.index(who)) + [0x100])
        # a heal already chosen for a PC whose HP has not moved since is still on its way (the queue's other seat): not twice
        for s in list(self.pending):
            if rd16(w, 0x5E2D + s * 0x80 + 3) != self.pending[s]:
                del self.pending[s]
        lives = [(r, s) for r, s in lives if s not in self.pending]
        if lives:
            ratio, who = min(lives)
            mids = sum(n for iid, n in bag if iid == MIDTONIC) - self.spentMid
            if ratio < self.heal and (tonics > 0 or mids > 0):
                b = 0x5E2D + who * 0x80
                big = rd16(w, b + 5) - rd16(w, b + 3) > 100          # a Mid Tonic (200) for a deficit a Tonic (50) leaves
                use = MIDTONIC if (mids > 0 and (big or tonics <= 0)) else TONIC
                if use == TONIC: self.spent += 1
                else: self.spentMid += 1
                self.goal = (use, who)
                self.pending[who] = rd16(w, 0x5E2D + who * 0x80 + 3)
                return '%s->%d' % ('midtonic' if use == MIDTONIC else 'tonic', who), press(walk(row, 2) + [0x100])
        tgt = self.target(w)
        for d in self.duals:
            # PILOT_DUAL (`a:b:row:mpA:mpB`, comma-separated): seat a's window with seat b alive and both MPs held -- b READY
            # (its gauge `$AFAB + b` at 0) and given no command yet: row 1 (Tech), A, the list's cursor down `row` rows (past
            # a's single techs onto the double), A, the target select next as an attack's; b still charging: the window
            # idles (the battle runs on, Active), at most 400 frames
            if seat != d[0]:
                continue
            b, drow, ma, mb = d[1], d[2], d[3], d[4]
            if b not in [s for _, s in lives] or rd16(w, 0x5E2D + seat * 0x80 + 7) < ma or rd16(w, 0x5E2D + b * 0x80 + 7) < mb:
                continue
            if w[0xAFAB + b] == 0 and b not in self.busy:
                self.waited = 0
                self.goal = ('attack', tgt) if tgt is not None else None
                pad = press(walk(row, 1) + [0x100]) + [(0, 6)] + press([0x20] * drow + [0x100])
                return 'dual->%s' % tgt, pad + ([] if tgt is not None else [(0, 6)] + press([0x100]))
            if w[0xAFAB + b] != 0 and self.waited < 400:
                self.waited += 4
                return 'wait', [(0, 4)]
        self.waited = 0
        if tgt is not None and seat in self.techs and rd16(w, 0x5E2D + seat * 0x80 + 7) >= 2:
            # PILOT_TECH (seats): the seat's first tech on the chosen enemy -- row 1 (Tech), A, the list's row 0, A; the
            # target select comes next, as an attack's
            self.goal = ('attack', tgt)
            return 'tech->%d' % tgt, press(walk(row, 1) + [0x100]) + [(0, 6)] + press([0x100])
        if tgt is not None:                                       # a chosen enemy: the target select comes next
            self.goal = ('attack', tgt)
            return 'attack->%d' % tgt, press(walk(row, 0) + [0x100])
        return 'attack', press(walk(row, 0) + [0x100]) + [(0, 6)] + press([0x100])

    def target(self, w):
        """The first living enemy of the preference order (PILOT_TARGETS: enemy ids in hex, `AD,94`), or None."""
        for want in self.prefer:
            for c in range(3, 11):
                b = 0x5E2D + c * 0x80
                if w[0xAEFF + c] == want and rd16(w, b + 3) > 0:
                    return c
        return None

    def step(self, w):
        """The pad for the next stretch of a fight (and a note when it decided something)."""
        if os.environ.get('PILOT_RUN'):                           # PILOT_RUN: hold L+R (the run away) and press nothing
            return [(0xC00, 8)], None
        self.stuck = self.stuck + 1 if w[0x95D5] < 3 and (w[0x95DB] or w[0x9609]) else 0
        if self.stuck > 12:
            self.stuck = 0
            return press([0x01]), 'B (a menu left open)'
        if w[0x95D5] < 3 and self.goal and w[0x95DB] == 2 and w[0x9609] == 0:
            # the Item list: the battle's records $1580 (five bytes, the bag's usable items in order), its cursor the row
            # $95E5 in the three-row window over $95E6 -- remembered between turns when the Config says so ($A0D5)
            ids = [w[0x1580 + i * 5] for i in range(0x40)]
            want = ids.index(self.goal[0]) if self.goal[0] in ids else 0
            self.stuck = 0
            return press(walk(w[0x95E6] + w[0x95E5], want) + [0x100]), None
        if w[0x95D5] < 3 and self.goal and w[0x9609]:
            # the ally select: $9614 indexes the compacted list $99C0 (C1/20A9)
            lst = []
            for i in range(11):
                if w[0x99C0 + i] == 0xFF: break
                lst.append(w[0x99C0 + i])
            want = lst.index(self.goal[1]) if self.goal[1] in lst else w[0x9614]
            self.goal, self.stuck = None, 0
            return press(walk(w[0x9614], want) + [0x100]), None
        self.busy = {s for s in self.busy if w[0xAFAB + s] == 0}    # a reloaded gauge: that seat's command has run
        if w[0x95D5] < 3 and w[0x95DB] == 0 and w[0x9609] == 0:
            self.goal = None
            what, pad = self.decide(w)
            if what == 'wait':
                return pad, None
            self.busy.add(w[0x95D5])
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
