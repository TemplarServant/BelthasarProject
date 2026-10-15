#!/usr/bin/env python3
r"""The target hand's page ($960B), port against the rule's cartridge, frame by frame over a held select.

    python tools/target_hand_page_cmp.py [--only <text>] [-v]

C1/17DD -> C1/1979: with a second actor lit (`$A62E` not FF) the multi arm C1/1A7B draws sprite 0 on $A62D[0] and
sprites 1-3 on the page of $A62D starting at `$960B` (C1/1AA2-1AF8), then stores the next page: 3 after page 0, 0 after
page 3. Only that rebuild writes $960B, once per menu pass (the $993A request, every 4th frame), and nothing resets it.
The port keeps the byte in WRAM (src/battle.c battleMenuPassTail) and draws the page the pass started at, as the NMI
shipped it (h->handPageShown, src/field.c battleTargetCursorRender).

The fixture is tools/caster_radius_dispatch_cmp.py's: gato_wait staged with four Hetakes (tools/battle_sim.py, op 0x83)
at cells cmb3 12,8 / cmb4 6,7 / cmb5 8,3 / cmb6 10,10 around seat 0's 8,9, each tech the caster's own drive
(battle_ab.drive_tech) with the select HELD (its A pressed late) so the page turns many times:
  Area Bomb (Robo, group 11 -> C1/224B, radius 0x10 around the caster): $A62D = 04 06, the multi arm on every pass.
  Cyclone (Crono, group 12 -> C1/225F, radius 9 around the cursor's enemy cmb3): $A62D = 03 06, the multi arm.
  Cyclone, none near (the Hetakes at cmb3 13,8 / cmb4 3,7 / cmb5 8,3 / cmb6 12,11: cmb6's square is 10): $A62D = 03, the
  single arm C1/19B4 on every pass -- the page never turns and stays what the fight left it.
(Cyclone's Downs are not driven: the cartridge's next enemy from cmb3 is cmb6, then cmb5, cmb4 -- the port's walks the
slots 4, 5, 6, a divergence of the cursor's order C1/176C, not of the page.)

Graded on every frame either side holds the select ($9609 = 01): `$960B` (WRAM, the same frame on both sides), and the
hand's shown sprites -- the cartridge's OAM entries 0-3 whose $0900 X-high bit is clear, the port's drawn sprites
(CT_BATTLE_HANDTRACE) -- at the lag the picture runs (the port's frame f against the cartridge's shadow at f - 1), from
the select's second frame on (the open's own frame is the hand's visibility, not its page: the port draws the hand on the
frame $9609 rises, the cartridge's picture one frame later).

    hand-page <case>: $960B over <n> frames, first difference <f|none>
    hand-page <case>: $960B turns cartridge f<..> port f<..>
    hand-page <case>: shown sprites over <n> frames (port f = cartridge shadow f-1), first difference <f|none>
    hand-page: <n>/<m> identical
"""
import os
import re
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path = [p for p in sys.path if os.path.abspath(p or '.') != TOOLS]
sys.path.insert(0, TOOLS)
import battle_ab as ab  # noqa: E402
sys.path.insert(0, TOOLS)                                 # (battle_ab drops it: tools/dis.py shadows dis)
import battle_sim as bs  # noqa: E402

ROOT = os.path.dirname(TOOLS)
PLACED = [(0x5E, 3, 12, 9), (0x5E, 4, 6, 8), (0x5E, 5, 8, 4), (0x5E, 6, 10, 11)]
FAR = [(0x5E, 3, 13, 9), (0x5E, 4, 3, 8), (0x5E, 5, 8, 4), (0x5E, 6, 12, 12)]
# The port's frame f draws the shadow the cartridge's main loop left at f - 1: the cartridge's picture N shows the
# shadow of N - 2 (the NMI at N - 1's end ships it), and inside a fight the port's picture P is the cartridge's P + 1
# (docs/TESTING.md, THE ALIGNMENT). Measured (BATTLE_AB_KEEP=1, the thumbnails): the secondary hand's cell turns on and
# off on the same frames at P + 1; -v prints the sprite match at every lag.
LAG = -1
# (name, char, k, placed, frames the select is held)
CASES = [('Area Bomb', 3, 6, PLACED, 60), ('Cyclone', 0, 0, PLACED, 120), ('Cyclone, none near', 0, 0, FAR, 60)]
TOTAL = 1000


def drive(k, hold):
    d = ab.drive_tech(k)
    return d[:-1] + [(d[-1][0] + hold, 'a', 2)]


def port_shown(plog):
    """{field frame: set of (x, y)} -- the battlehand lines a frame printed come before its `fmemmon f<N>` line."""
    out, cur = {}, set()
    for line in plog.splitlines():
        m = re.match(r'battlehand: sprite\d cmb=\d+ oam=([0-9A-F]{2}),([0-9A-F]{2})', line)
        if m:
            cur.add((int(m.group(1), 16), int(m.group(2), 16)))
            continue
        m = re.match(r'fmemmon f(\d+) ', line)
        if m:
            out[int(m.group(1))] = cur
            cur = set()
    return out


def cart_shown(rows):
    o700, o900 = bs.series(rows, 0x0700, TOTAL), bs.series(rows, 0x0900, TOTAL)
    out = {}
    for f in range(TOTAL + 1):
        s = set()
        if o700[f] and o900[f]:
            hi = int(o900[f], 16)
            for e in range(4):
                if not (hi >> (2 * e)) & 1:
                    s.add((int(o700[f][e * 8:e * 8 + 2], 16), int(o700[f][e * 8 + 2:e * 8 + 4], 16)))
        out[f] = s
    return out


def main():
    os.chdir(ROOT)
    os.environ['CT_BATTLE_HANDTRACE'] = '1'
    verbose = '-v' in sys.argv
    flt = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    # $9931 counts every battle frame (CF/E73B): the port prints an fmemmon line per frame to hang the hand lines on
    regs = [(0x9609, 1), (0x960B, 1), (0x960D, 1), (0x9614, 1), (0xA62D, 4), (0x9931, 1), (0x0700, 16), (0x0900, 1)]
    ok = n = 0

    def grade(line, same):
        nonlocal ok, n
        n += 1; ok += bool(same)
        print('hand-page ' + line)

    for i, (name, ch, k, placed, hold) in enumerate(CASES):
        if flt and flt not in name:
            continue
        pk = ab.pokes_for([ch], extra=bs.staging_pokes(placed))
        _, rc, rp, _, _, plog = ab.run(pk, drive(k, hold), TOTAL, 'hand_page_%d' % i, regs=regs, raw=True)
        c9609, p9609 = bs.series(rc, 0x9609, TOTAL), bs.series(rp, 0x9609, TOTAL)
        held = [f for f in range(TOTAL + 1) if c9609[f] == '01' or p9609[f] == '01']
        cb, pb = bs.series(rc, 0x960B, TOTAL), bs.series(rp, 0x960B, TOTAL)
        bad = next((f for f in held if cb[f] != pb[f]), None)
        grade('%s: $960B over %d frames, first difference %s' % (
            name, len(held), 'none' if bad is None else 'f%d cartridge %s port %s' % (bad, cb[bad], pb[bad])),
            bad is None and held)
        turns = [[f for f in held if s[f] != s[f - 1]] for s in (cb, pb)]
        fmt = lambda t: ' '.join('f%d' % f for f in t) or '-'
        grade('%s: $960B turns cartridge %s port %s' % (name, fmt(turns[0]), fmt(turns[1])), turns[0] == turns[1])
        cs, ps = cart_shown(rc), port_shown(plog)
        fr = [f for f in held if c9609[f] == '01' and p9609[f] == '01' and c9609.get(f + LAG) == '01']
        bad = next((f for f in fr if ps.get(f, set()) != cs[f + LAG]), None)
        grade('%s: shown sprites over %d frames (port f = cartridge shadow f%+d), first difference %s' % (
            name, len(fr), LAG, 'none' if bad is None else 'f%d cartridge %s port %s' % (
                bad, sorted(cs[bad + LAG]), sorted(ps.get(bad, set())))), bad is None and fr)
        if verbose:
            for lag in range(-2, 4):
                m = sum(ps.get(f, set()) == cs.get(f + lag, set()) for f in fr)
                print('  lag %+d: %d/%d frames equal' % (lag, m, len(fr)))
            ca, c900 = bs.series(rc, 0xA62D, TOTAL), bs.series(rc, 0x0900, TOTAL)
            for f in held:
                print('  f%d  cartridge 9609=%s 960B=%s a62d=%s 0900=%s %s | port 9609=%s 960B=%s %s' % (
                    f, c9609[f], cb[f], ca[f], c900[f], sorted(cs[f]), p9609[f], pb[f], sorted(ps.get(f, set()))))
    print('hand-page: %d/%d identical' % (ok, n))
    return 0 if ok == n else 1


if __name__ == '__main__':
    sys.exit(main())
