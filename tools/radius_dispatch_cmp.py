#!/usr/bin/env python3
r"""A radius tech's victims are rebuilt at its dispatch: Fire Whirl after an ally's target select, port against the rule's
cartridge.

    python tools/radius_dispatch_cmp.py

The victims must not be the $99C0 the radius tech's target select left: a PC-side select in between (Slurp, group 80:
C1/203A lists the caster first) rewrites $99C0 to `02 00 01`, and Fire Whirl would strike Frog, Crono and Lucca.
The cartridge's dispatch runs the area pass C1/C586 (area 03 = CC/2B1C `01 / 01 01 09`): C1/C7A7
centres the radius on the action's target ($9605 = $06, $9607 = 09) and C1/1FDD routine 1 = C1/2701 rebuilds $99C0
before C1/C600 copies it into $AD8E (battle.c battleAreaC586 / battleAreaRadius2701).

The fixture: gato_wait staged with Yakra (tools/battle_sim.py, op 0x83 on a fresh object), the party Crono / Lucca /
Frog with every tech learned and Crono + Lucca's duals ($283F = E0), ACTIVE mode ($2990 = C4). Crono's Tech list,
Fire Whirl (3C, group 12) on Yakra, confirmed while Yakra's attack runs; then Frog's Tech, Slurp (21) on himself
before Fire Whirl dispatches. Both sides take the same keys on the same frames (battle_ab.run).

    radius-dispatch: Fire Whirl dispatch ($AE93 = 3C) cartridge f<C> port f<P>
    radius-dispatch: $99C0 at the dispatch cartridge <ids> port <ids>
    radius-dispatch: victims ($AD8D / $AD8E) cartridge <n> [<ids>] port <n> [<ids>]
    radius-dispatch: cmb<k> took damage in the span cartridge <yes|no> port <yes|no>
    radius-dispatch: <n>/<m> identical
    radius-dispatch-info: cmb<k> HP cartridge <a>-><b> port <c>-><d>     (not graded: the damage's RNG)
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
FIRE_WHIRL, SLURP = 0x3C, 0x21
# Crono (head): Tech, the list, Down x8 to Fire Whirl (singles 01-08, then the duals), A, A on Yakra -- the confirm
# lands during Yakra's attack (f756). Frog (the next head): Tech, the list, Slurp (row 0), A on himself.
DRIVE = [(650, 'd'), (662, 'a')] + [(674 + 12 * i, 'd') for i in range(8)] + \
        [(770, 'a'), (782, 'a'), (794, 'd'), (806, 'a'), (818, 'a'), (830, 'a')]
TOTAL = 1500
PCS = 3 + 1                                   # Crono, Lucca, Frog, Yakra (slot 0)


def main():
    os.chdir(ROOT)
    cat = bs.catalog()
    enemies, _ = bs.parse_enemies('90', cat)
    pk = ab.pokes_for([0, 2, 4], extra=bs.staging_pokes(bs.layout(enemies)))
    pk[0x283F] = 0xE0                         # Crono + Lucca: Fire Whirl, Fire Sword, Fire Sword 2 (C1/CA1A)
    pk[0x2990] = 0xC4                         # bit 6: ACTIVE (battle_atbmode.md sec. 1.4)
    regs = [(0xAE93, 1), (0xA013, 1), (0x99C0, 3), (0xAD8D, 4)] + [(bs.cmb_base(c) + 3, 2) for c in range(PCS)]
    _, rc, rp, _, _, _ = ab.run(pk, [(f, k, 2) for f, k in DRIVE], TOTAL, 'radius_dispatch', regs=regs, raw=True)
    sides = {}
    for name, rows in (('cartridge', rc), ('port', rp)):
        ae93 = bs.series(rows, 0xAE93, TOTAL)
        a013 = bs.series(rows, 0xA013, TOTAL)
        d = next((f for f in range(TOTAL + 1) if ae93[f] == '%02X' % FIRE_WHIRL), None)
        end = None if d is None else next((f for f in range(d, TOTAL + 1) if a013[f] == '00' and a013[f - 1] == '01'), None)
        st = bs.state_at(rows, d) if d is not None else {}
        hp = {c: bs.series(rows, bs.cmb_base(c) + 3, TOTAL) for c in range(PCS)}
        sides[name] = dict(d=d, end=end, st=st, hp=hp)
    c, p = sides['cartridge'], sides['port']
    ok = n = 0

    def grade(line, same):
        nonlocal ok, n
        n += 1; ok += bool(same)
        print('radius-dispatch: ' + line)

    grade('Fire Whirl dispatch ($AE93 = %02X) cartridge f%s port f%s' % (FIRE_WHIRL, c['d'], p['d']), c['d'] == p['d'])
    if c['d'] is None or p['d'] is None:
        print('radius-dispatch: %d/%d identical' % (ok, n)); return 1

    def ids(h):
        return ' '.join(h[i:i + 2] for i in range(0, len(h or ''), 2) if h[i:i + 2] != 'FF') or '-'
    grade('$99C0 at the dispatch cartridge %s port %s' % (ids(c['st'].get(0x99C0)), ids(p['st'].get(0x99C0))),
          ids(c['st'].get(0x99C0)) == ids(p['st'].get(0x99C0)))
    vc, vp = c['st'].get(0xAD8D, ''), p['st'].get(0xAD8D, '')
    grade('victims ($AD8D / $AD8E) cartridge %s [%s] port %s [%s]' % (vc[:2], ids(vc[2:]), vp[:2], ids(vp[2:])), vc == vp)
    for k in range(PCS):
        took = []
        for s in (c, p):
            h = s['hp'][k]
            took.append('yes' if s['end'] is not None and h[s['end']] != h[s['d']] else 'no')
        grade('cmb%d took damage in the span cartridge %s port %s' % (k, took[0], took[1]), took[0] == took[1])
        print('radius-dispatch-info: cmb%d HP cartridge %s->%s port %s->%s' % (
            k, c['hp'][k][c['d']], c['hp'][k][c['end']] if c['end'] else '?',
            p['hp'][k][p['d']], p['hp'][k][p['end']] if p['end'] else '?'))
    print('radius-dispatch: %d/%d identical' % (ok, n))
    return 0 if ok == n else 1


if __name__ == '__main__':
    sys.exit(main())
