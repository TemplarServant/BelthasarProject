#!/usr/bin/env python3
r"""WRAM cells over a stretch of a playthrough leg, the port against the cartridge, frame by frame.

    python tools/leg_cells_cmp.py [name ...]

Both sides play the leg from its start (the cartridge's state at the leg's start, made from the route by
tools/playthrough.py; the port seeded from that state's WRAM with the leg's own pad) and the named cells are read on
every frame of [a, b]: the cartridge's by `ct.exe --monitor`, the port's by CT_FIELD_MEMMON. A frame differs when any
cell does. For state that no picture shows -- a byte a later scene reads back -- where tools/leg_window_cmp.py's
change-list steps cannot see a cell that stands on one side and moves on the other.

    legcells: <name> leg <n> f<a>-f<b>: <d> of <k> frames differ

Regression checks: the `leg-cells-*` family (docs/TESTING.md).
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.environ.get('LEGCELLS_TMP') or os.path.join(ROOT, 'build', '_legcells')

# name: leg, the frames [a, b] graded, the cells (address in $7E, length).
CASES = {
    # The world's entry, C2/0471: only the CLASS WORD of each of the 64 object blocks ($0B30 + $40 k) is zeroed; the rest of
    # the field's page stands until an allocation writes it (C2/048A). Leg 84 enters 1F0 on f1209 and leaves on f2699:
    # `$1608` / `$160A` -- two field objects' facing, inside blocks 43 and 44 -- keep their 01 through the visit, and the
    # field reads them again when it comes back (the port zeroed the whole area: 00 from f1210 on).
    # Loc 12F's load (leg 100, f3556): its chest list is an ALIAS -- the first word of the list is 0000 and the second names
    # loc 06E, whose list C0/A712 then walks (C0/A75D-A767). The load's cost walk priced it and the load is the cartridge's
    # 41 black frames: `$0119` lights on f3597 on both sides (the walk had refused the alias and estimated a frame long).
    # A sound JSL an init script makes INSIDE a load (the init runner C0/5709, NMI off): the cartridge's CPU reaches it after
    # the walk's work before it, the driver answers on its tick, and the load's black counts the JSL's own wall time.
    # Leg 78, loc 11C (f241): `10 38` enters C7 on line 127-129 of f268 and leaves on line 210; the load lights on f279.
    # Leg 34, loc 01C (f684): the JSL enters 6.43M cycles in (the later objects' runner heads come after it); lit f718.
    # Leg 47, loc 0DF (f3175): `10 27` uploads a song for 29 frames + 104 lines with no NMI, IRQ or HDMA; lit f3241.
    # Leg 63, loc 087 (f504, from the world): `10 45` uploads for 41 frames + 198 lines; lit f587.
    'load-post-counts-its-own-wall-time': dict(leg='78_lair_holes', a=236, b=300, cells=[('0119', 1), ('0100', 2)]),
    'load-post-follows-the-runners-heads': dict(leg='34_to_bridge', a=680, b=740, cells=[('0119', 1), ('0100', 2)]),
    'load-upload-runs-on-the-quiet-background': dict(leg='47_race', a=3170, b=3260, cells=[('0119', 1), ('0100', 2)]),
    'world-exit-load-upload-runs-on-the-quiet-background': dict(leg='63_zombor', a=500, b=610, cells=[('0119', 1), ('0100', 2)]),
    # A load out of the world (leg 64, 1F1 -> 08A, f367): the NMI goes off at C2/246D, 8,956 master before C0/0B5D.
    'world-exit-load-starts-at-the-worlds-nmi-off': dict(leg='64_to_denadoro', a=360, b=430, cells=[('0119', 1), ('0100', 2)]),
    # A song the field uploads with the NMI on (leg 84, the Gate's warp `10 37`, f335): the field stands 19 frames, the
    # HDMA the JSL meets being the room's live channels (21,300 master a frame there); the arrival lights on f702.
    'field-upload-meets-the-rooms-hdma': dict(leg='84_masamune_reforged', a=330, b=720, cells=[('0119', 1), ('0100', 2)]),
    'chest-alias-load-is-walked': dict(leg='100_lair_grind', a=3550, b=3620, cells=[('0119', 1), ('0100', 2)]),
    'world-entry-zeroes-class-words-only': dict(leg='84_masamune_reforged', a=1260, b=2760, cells=[('1608', 1), ('160A', 1)]),
}


def run(name):
    c = CASES[name]
    legs = P.route(); i = P.find(legs, c['leg']); lg = legs[i]
    P.TMP = os.path.join(WORK, 'run%d' % os.getpid()); os.makedirs(P.TMP, exist_ok=True)
    n = c['b'] + 2; pad = []
    for b, q in lg['pad']:
        if n <= 0:
            break
        pad.append((b, min(q, n))); n -= q
    st = P.start_of(legs, i)
    P.MON = [('c%d' % k, '7E' + a, a, ln) for k, (a, ln) in enumerate(c['cells'])]
    pt, _ = P.port_timeline(st, pad, P.last_key(legs, i)); ot = P.oracle_timeline(st, pad)

    def rows(tl):                                        # (the timelines list the frames a cell changed on)
        out = {}; cur = {}; k = 0
        for f in range(c['b'] + 1):
            while k < len(tl) and tl[k][0] <= f:
                cur.update(tl[k][1]); k += 1
            out[f] = tuple(cur.get(m[0]) for m in P.MON)
        return out

    o, p = rows(ot), rows(pt)
    fr = list(range(c['a'], c['b'] + 1))
    bad = [f for f in fr if o[f] != p[f]]
    print('legcells: %s leg %s f%d-f%d: %d of %d frames differ' % (name, c['leg'].split('_')[0], c['a'], c['b'], len(bad), len(fr)))
    if bad:
        f = bad[0]
        print('  first: f%d cartridge %s, port %s' % (f, ' '.join(o[f]), ' '.join(p[f])))
    return 0


def main():
    names = sys.argv[1:] or sorted(CASES)
    os.chdir(ROOT)
    for nm in names:
        if nm not in CASES:
            print('legcells: no case %s' % nm); return 1
        run(nm)
    return 0


if __name__ == '__main__':
    sys.exit(main())
