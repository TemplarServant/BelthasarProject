#!/usr/bin/env python3
r"""The port without the dump against the port with it, on a playthrough leg's own frames.

    python tools/rom_free_picture_cmp.py <name> [--png]

The ROM-free runtime reads every ROM byte it needs from ct_assets.dat's tables (assets/rom_closure.py); a byte no table
holds reads as 0 when no dump is present, and with a dump in the tree the port quietly reads the ROM instead -- so every
check that runs with the dump present cannot see a missing table. This tool plays a leg's pad on the port twice from the
leg's start state, once as usual and once with CT_NO_ROM=1, and compares the pictures at the frames a case names: any
pixel apart is a read the pack does not cover.

    romfree: <name> leg <leg> f<a>,f<b>..: <d> px differ (the port without the dump vs with it)

--png writes build/_romfree/<name>.png: per frame, with the dump, without, the pixels apart.
Regression checks: `rom-free-picture-*` (docs/TESTING.md).
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.environ.get('ROMFREE_TMP') or os.path.join(ROOT, 'build', '_romfree')

CASES = {
    # The Factory's fight (playthrough leg 51): the results name the PCs -- "Robo learns Laser Spin!", "Lucca's level
    # up!". The VWF's name codes (11, 13-19, 1B+; C2/5A26 / 5B15 / 5B70) read the name's address from C2:5FD8 + 2 x id;
    # the pack's VWF table began at C2:5FE6, so without the dump the pointer read 0 and the name came from $7E:0000 --
    # a stray glyph and the line pushed right and wrapped (GitHub issue #11, v0.26.0).
    'reward-names': dict(leg='51_security', at=[10100, 10300]),
    # The Tech screen's target overlay (playthrough leg 155): HDMA channel 2's TM lines are the record FF/BBDA (the pack's
    # 'tech screen TM lines'); without it no line would hide the caster's top rows above line 16.
    'tech-screen-tm-lines': dict(leg='155_omen_circuit_k', at=[28800]),
}


def shots(lg, i, legs, at, tag, nodump):
    P.TMP = os.path.join(WORK, tag)
    os.makedirs(P.TMP, exist_ok=True)
    end = lg['frames']
    P.SHOTS = [f - end for f in at]
    old = os.environ.get('CT_NO_ROM')
    if nodump:
        os.environ['CT_NO_ROM'] = '1'
    else:
        os.environ.pop('CT_NO_ROM', None)
    try:
        _, pics = P.port_timeline(P.start_of(legs, i), lg['pad'], P.last_key(legs, i))
    finally:
        if old is None:
            os.environ.pop('CT_NO_ROM', None)
        else:
            os.environ['CT_NO_ROM'] = old
    return {f: pics.get(f - end) for f in at}


def run(name, png):
    c = CASES[name]
    legs = P.route()
    i = P.find(legs, c['leg'])
    if i > 0:
        P.ensure_states(legs, i - 1)
    lg = legs[i]
    a = shots(lg, i, legs, c['at'], name + '_dump', False)
    b = shots(lg, i, legs, c['at'], name + '_nodump', True)
    n, rows = 0, []
    for f in c['at']:
        if a[f] is None or b[f] is None:
            print('romfree: %s leg %s f%d: no picture (%s)' % (name, lg['name'].split('_')[0], f,
                                                               'with the dump' if a[f] is None else 'without'))
            return 1
        for y in range(224):
            ra, rb = a[f][y * 768:(y + 1) * 768], b[f][y * 768:(y + 1) * 768]
            d = bytearray()
            for x in range(0, 768, 3):
                if ra[x:x + 3] != rb[x:x + 3]:
                    n += 1
                    d += b'\xff\x00\x00'
                else:
                    d += bytes(v // 3 for v in ra[x:x + 3])
            rows.append(b'\0' + ra + rb + bytes(d))
    print('romfree: %s leg %s %s: %d px differ (the port without the dump vs with it)'
          % (name, lg['name'].split('_')[0], ','.join('f%d' % f for f in c['at']), n))
    if png:
        def chunk(t, d):
            return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
        out = os.path.join(WORK, name + '.png')
        open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 768, len(rows), 8, 2, 0, 0, 0))
                              + chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))
        print('  -> %s' % out)
    return 0 if n == 0 else 1


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    names = args or sorted(CASES)
    rc = 0
    for name in names:
        if name not in CASES:
            sys.exit('rom_free_picture_cmp: no case %s (%s)' % (name, ', '.join(sorted(CASES))))
        rc |= run(name, '--png' in sys.argv)
    return rc


if __name__ == '__main__':
    sys.exit(main())
