#!/usr/bin/env python3
r"""A caster-centred radius tech's victims come from its dispatch's area pass: Area Bomb, Tail Spin and Black Hole, port
against the rule's cartridge -- the select that lights them, the dispatch, and the pass that finds nobody.

    python tools/caster_radius_dispatch_cmp.py [--only <text> | --skip <text>]   (the cases whose name holds the text)

Groups 11 (Area Bomb 1F, Tail Spin 2E), 13 (40, 6A) and 1B (Black Hole 37) light every enemy around the caster at the
select (C1/224B / C1/22A4 / C1/22D3 -> C1/2701, `$960C = 80`); their area byte is 02 (CC/2B18 `01 / 00 00 10`) or 0A
(CC/2B38 `01 / 00 00 19`). C1/C1DD's PC arm ($3A = 3, C1/C518-C536) runs the area pass C1/C586: C1/C95C[0] = C1/C78D
stages `$9604 = 0`, `$9605 = $00` (participant 0's seat, C1/C74C), `$9606 = $B2AE`, `$9607 = 10 / 19`, `$9608 = 0`, and
C1/1FDD routine 1 = C1/2701 rebuilds $99C0 around the caster before C1/C600 copies it into $AD8E (battle.c
battleAreaC586), so every enemy in the radius is a victim, not only the group's own target.

The fixture: gato_wait staged with four Hetakes (tools/battle_sim.py, op 0x83 on fresh objects) at cells chosen around
the lone caster's seat 0 (cell 8,9): cmb3 at 12,8 (4^2 + 1^2 = 17), cmb4 at 6,7 (8), cmb5 at 8,3 (36), cmb6 at 10,10
(5) -- inside 0x10: cmb4, cmb6; inside 0x19: cmb3 too. Each tech is the caster's own drive (battle_ab.drive_tech): the
same keys on the same frames on both sides.

The select (C1/1F79 -> C1/224B / C1/22A4 / C1/22D3 -> C1/2701 -> C1/27E8, battle.c battleCasterRadiusBuild): every pass
rebuilds the set around the caster into $99C0 and $A62D (`$960C = 80`: no cursor, Up / Down do nothing) and the hand
lights $A62D (C1/1979 -> C1/1A7B: sprite 0 on $A62D[0], 1-3 on the $960B page; a hidden one has its $0900 X-high bit set);
the confirm's record takes `$93F1 := $A62D[0]` (C1/1703), which C1/BB85 puts in $B2AE and C1/C78D stages as $9606 -- cmb4
for Area Bomb / Tail Spin (radius^2 $10), cmb3 for Black Hole ($19). Graded: $9609 / $960C / $A62D on every frame either side
holds the select, the hand's positions (the cartridge's OAM $0700 entries shown by $0900 against the port's
CT_BATTLE_HANDTRACE sprites), and $9606 at the dispatch. 'Area Bomb + Down' presses Down in the select (the target stays
$A62D[0]); 'Area Bomb, none near' stages the four Hetakes out of radius^2 $10: C1/1FD9 `$9613 = FF` vetoes the open (C1/137C),
no select, no dispatch.

'Area Bomb, walked out': the confirm with cmb4 / cmb6 inside, then (f850, while a Hetake's attack holds the dispatch back)
both feet moved out of the radius on both sides ($1D0C / $1D23, battle_ab's timed pokes). The dispatch's pass finds nobody:
C1/C659 -> C1/C66A stages `$AE91 = $B18B`, `$AE92 = 02`, `$AE96 = 10`, `$AE93 = 7B` and plays that action (C1/AC57), then
C1/C72B `$AF23 = 1` and C1/BB8B -> C1/BC5A: C1/BDB3's reloads, no MP spent (battle.c battleDispatchScheduled, actDropTech).

    caster-radius <tech>: select ($9609 $960C $A62D) on <n> frames, first difference <f|none>
    caster-radius <tech>: the hand's sprites over the select cartridge [<x,y> ..] port [<x,y> ..]
    caster-radius <tech>: dispatch ($AE93 = <id>) cartridge f<C> port f<P>
    caster-radius <tech>: $9604 $9605 $9607 $9608 at the dispatch cartridge <hex> port <hex>
    caster-radius <tech>: $9606 (the record's target $B2AE) at the dispatch cartridge <id> port <id>
    caster-radius <tech>: victims ($AD8D / $AD8E) cartridge <n> [<ids>] port <n> [<ids>]
    caster-radius <tech>: cmb<k> took damage in the span cartridge <yes|no> port <yes|no>
    caster-radius <tech>: $AE91 $AE92 / $AE96 at the dispatch cartridge <hex> port <hex>          (the walked-out case)
    caster-radius <tech>: span ($A013) cartridge f<a>-f<b> port f<a>-f<b>
    caster-radius <tech>: after the close $AF23 / MP / $AFAB cartridge <hex> port <hex>
    caster-radius: <n>/<m> identical

Regression checks: battle-caster-radius-tech-victims-at-dispatch-vs-rule-cartridge,
battle-caster-radius-empty-pass-plays-7b-then-drops-vs-rule-cartridge, battle-target-hand-page-vs-rule-cartridge.
"""
import os
import re
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
FAR = [(0x5E, 3, 13, 9), (0x5E, 4, 3, 8), (0x5E, 5, 8, 4), (0x5E, 6, 12, 12)]   # squares 26 / 26 / 36 / 25: none <= 10
# cmb4 to cell (1,2), cmb6 to cell (14,2): squares 98 / 85 from the caster's (8,9)
WALK_OUT = (850, {0x1D10: 0x18, 0x1D27: 0x28, 0x1D12: 0xE8, 0x1D29: 0x28})
# (name, id, char, k, placed, extra keys in the select before its A, timed pokes, the $AE93 the dispatch writes)
TECHS = [('Area Bomb', 0x1F, 3, 6, PLACED, '', None, 0x1F), ('Tail Spin', 0x2E, 5, 5, PLACED, '', None, 0x2E),
         ('Black Hole', 0x37, 6, 6, PLACED, '', None, 0x37), ('Area Bomb + Down', 0x1F, 3, 6, PLACED, 'd', None, 0x1F),
         ('Area Bomb, none near', 0x1F, 3, 6, FAR, '', None, 0x1F),
         ('Area Bomb, walked out', 0x1F, 3, 6, PLACED, '', WALK_OUT, 0x7B)]
TOTAL = 1300
CMBS = range(3, 7)


def main():
    os.chdir(ROOT)
    os.environ['CT_BATTLE_HANDTRACE'] = '1'                # the port's hand sprites (src/field.c battleTargetCursorRender)
    regs = [(0xAE93, 1), (0xA013, 1), (0x9604, 5), (0x99C0, 3), (0xAD8D, 5), (0x9609, 1), (0x960C, 1), (0xA62D, 4),
            (0x0700, 16), (0x0900, 1), (0xAE91, 2), (0xAE96, 1), (0xAF23, 1), (0x5E34, 2), (0xAFAB, 1)] + \
        [(bs.cmb_base(c) + 3, 2) for c in CMBS]
    ok = n = 0

    def grade(line, same):
        nonlocal ok, n
        n += 1; ok += bool(same)
        print('caster-radius ' + line)

    def ids(h):
        return ' '.join(h[i:i + 2] for i in range(0, len(h or ''), 2) if h[i:i + 2] != 'FF') or '-'

    flt = sys.argv[1:3] if len(sys.argv) >= 3 and sys.argv[1] in ('--only', '--skip') else None
    for i, (name, tid, ch, k, placed, keys, timed, act) in enumerate(TECHS):
        if flt and (flt[1] in name) != (flt[0] == '--only'):
            continue
        pk = ab.pokes_for([ch], extra=bs.staging_pokes(placed))
        drive = ab.drive_tech(k)
        if keys:                                           # the extra keys between the target select's open and its A
            at = drive[-1][0]
            drive = drive[:-1] + [(at + 12 * j, b, 2) for j, b in enumerate(keys)] + [(at + 12 * len(keys), 'a', 2)]
        _, rc, rp, _, _, plog = ab.run(pk, drive, TOTAL, 'caster_radius_%02X_%d' % (tid, i), regs=regs, raw=True,
                                       timed=timed)
        sides = {}
        for side, rows in (('cartridge', rc), ('port', rp)):
            ae93 = bs.series(rows, 0xAE93, TOTAL)
            a013 = bs.series(rows, 0xA013, TOTAL)
            d = next((f for f in range(TOTAL + 1) if ae93[f] == '%02X' % act), None)
            op = None if d is None else next((f for f in range(d, TOTAL + 1) if a013[f] == '01'), None)
            end = None if d is None else next((f for f in range(d, TOTAL + 1)
                                               if a013[f] == '00' and a013[f - 1] == '01'), None)
            hp = {c: bs.series(rows, bs.cmb_base(c) + 3, TOTAL) for c in CMBS}
            sr = [bs.series(rows, a, TOTAL) for a in (0x9609, 0x960C, 0xA62D)]
            sel = {f: tuple(x[f] for x in sr) for f in range(TOTAL + 1)}
            sides[side] = dict(d=d, op=op, end=end, st=bs.state_at(rows, d) if d is not None else {}, hp=hp, sel=sel,
                               after=bs.state_at(rows, end) if end is not None else {})
        c, p = sides['cartridge'], sides['port']
        held = [f for f in range(TOTAL + 1) if c['sel'][f][0] == '01' or p['sel'][f][0] == '01']
        bad = next((f for f in held if c['sel'][f] != p['sel'][f]), None)
        grade('%s: select ($9609 $960C $A62D) on %d frames, first difference %s' % (
            name, len(held), 'none' if bad is None else 'f%d cartridge %s port %s' % (bad, c['sel'][bad], p['sel'][bad])),
            bad is None)
        # the hand: the cartridge's shown OAM entries 0-3 over the frames it holds the select; the port's drawn sprites
        hc = set()
        o700, o900 = bs.series(rc, 0x0700, TOTAL), bs.series(rc, 0x0900, TOTAL)
        for f in held:
            if c['sel'][f][0] != '01' or not o700[f] or not o900[f]:
                continue
            hi = int(o900[f], 16)
            for e in range(4):
                if not (hi >> (2 * e)) & 1:
                    hc.add((int(o700[f][e * 8:e * 8 + 2], 16), int(o700[f][e * 8 + 2:e * 8 + 4], 16)))
        hp_ = set((int(m.group(1), 16), int(m.group(2), 16))
                  for m in re.finditer(r'battlehand: sprite\d cmb=\d+ oam=([0-9A-F]{2}),([0-9A-F]{2})', plog))
        fmt = lambda s: ' '.join('%02X,%02X' % xy for xy in sorted(s)) or '-'
        grade('%s: the hand\'s sprites over the select cartridge [%s] port [%s]' % (name, fmt(hc), fmt(hp_)), hc == hp_)
        grade('%s: dispatch ($AE93 = %02X) cartridge f%s port f%s' % (name, act, c['d'], p['d']), c['d'] == p['d'])
        if c['d'] is None or p['d'] is None:
            continue
        a, b = c['st'].get(0x9604, ''), p['st'].get(0x9604, '')
        cut = lambda h: h[:4] + h[6:]                    # $9606 aside: C1/2701 never reads it
        grade('%s: $9604 $9605 $9607 $9608 at the dispatch cartridge %s port %s' % (name, cut(a), cut(b)),
              cut(a) == cut(b))
        grade('%s: $9606 (the record\'s target $B2AE) at the dispatch cartridge %s port %s' % (name, a[4:6], b[4:6]),
              a[4:6] == b[4:6])
        vc, vp = c['st'].get(0xAD8D, ''), p['st'].get(0xAD8D, '')
        grade('%s: victims ($AD8D / $AD8E) cartridge %s [%s] port %s [%s]' % (name, vc[:2], ids(vc[2:]), vp[:2],
                                                                            ids(vp[2:])), vc == vp)
        for cm in CMBS:
            took = []
            for s in (c, p):
                h = s['hp'][cm]
                took.append('yes' if s['end'] is not None and h[s['end']] != h[s['d']] else 'no')
            grade('%s: cmb%d took damage in the span cartridge %s port %s' % (name, cm, took[0], took[1]),
                  took[0] == took[1])
        if act == tid:
            continue
        hdr = [s['st'].get(0xAE91, '') + ' / ' + s['st'].get(0xAE96, '') for s in (c, p)]
        grade('%s: $AE91 $AE92 / $AE96 at the dispatch cartridge %s port %s' % (name, hdr[0], hdr[1]), hdr[0] == hdr[1])
        grade('%s: span ($A013) cartridge f%s-f%s port f%s-f%s' % (name, c['op'], c['end'], p['op'], p['end']),
              (c['op'], c['end']) == (p['op'], p['end']))
        aft = [' '.join(s['after'].get(r, '') for r in (0xAF23, 0x5E34, 0xAFAB)) for s in (c, p)]
        grade('%s: after the close $AF23 / MP / $AFAB cartridge %s port %s' % (name, aft[0], aft[1]), aft[0] == aft[1])
    print('caster-radius: %d/%d identical' % (ok, n))
    return 0 if ok == n else 1


if __name__ == '__main__':
    sys.exit(main())
