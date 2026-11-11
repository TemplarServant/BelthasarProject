#!/usr/bin/env python3
r"""A field picture, the port against the cartridge from one cartridge state.

    python tools/field_picture_cmp.py [name ...] [--png]

The playthrough's legs grade their last picture, but a leg ends wherever the route cut it: after a walk, a load or a
fight whose length is the timing class, so a picture that differs there says little about how the room is drawn. This
tool takes the cartridge's state at a leg's START (made from the route by tools/playthrough.py, keyed on the route and
the oracle), leaves the pad alone for a few frames on both sides, and compares the pictures: nothing moved but what the
room itself animates, and both sides began on the same WRAM.

    fieldpicture: <name> leg <leg> +<n> idle frames: <d> px differ at port <lag>

`<lag>` is the port frame that fits best in -6..+6 around the cartridge's (the port's picture P is the cartridge's P+2).
--png writes build/_fieldpic/<name>.png: the cartridge, the port, the pixels apart.

Regression checks: the `field-picture-*` family (docs/TESTING.md).
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.environ.get('FIELDPIC_TMP') or os.path.join(ROOT, 'build', '_fieldpic')
P.TMP = WORK                                                  # (its own scratch: not the playthrough's)

# name: the leg whose START state seeds both sides (its name in tools/playthrough/route.txt) and the idle frames.
PICTURES = {
    # Death Peak, loc 0F6 (the start of leg 135): the snow is BG3 with a speed of its own, two pixels up and one left a
    # frame, under the window wave ($1DF9): HDMA channel 3 hands BG3HOFS out a line at a time, +-5 px over 16 lines.
    # TM 17, TS 13, CGWSEL 02, CGADSUB 44, BGMODE 09: the snow's priority-1 pixels are the main screen's top, above the
    # sprites, and are half-added to the sub screen -- the room, and the party and the Poyozos where they stand. Three
    # things make the picture: the sprite as the SUB pixel under a BG pixel that beats it (scene.c BG_OBJ_LVL and
    # CtFieldSceneObjLevels, field.c g_objSubMask), the line's own scroll (field.c: picture row r shows transfer r - 1
    # of channel 3's table), and the wave's phase riding BG3's vertical steps (ct_vm.c: C0/9721 / C0/975F's
    # `INC|DEC $FB` x4 a pixel).
    'death-peak-snow': dict(leg='135_death_peak_0f6', frames=40),
    # The End of Time, loc 1D0 (the start of leg 107): the lead stands in a pillar of light. TM 17, TS 12, the math adds:
    # the pillar is a BG priority-1 pixel over the lead's OBJ, so the main pixel is the pillar's and the sub pixel the
    # lead -- the character shows through the light, where the foreground's re-draw used to cover it. The sky around
    # the platform is BG3 under the same wave as the snow.
    'end-of-time-pillar': dict(leg='107_keepers_dome', frames=40),
}


def png(path, c, p):
    rows = []
    for y in range(224):
        cr, pr = c[y * 768:(y + 1) * 768], p[y * 768:(y + 1) * 768]
        dr = bytearray()
        for x in range(0, 768, 3):
            dr += b'\xff\x00\x00' if cr[x:x + 3] != pr[x:x + 3] else bytes(v // 3 for v in cr[x:x + 3])
        rows.append(b'\0' + cr + pr + bytes(dr))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 768, 224, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))


def run(name, want_png):
    d = PICTURES[name]
    legs = P.route()
    i = P.find(legs, d['leg'])
    if i > 0:
        P.ensure_states(legs, i - 1)
    start = P.start_of(legs, i)
    pad = [(0, d['frames'])]
    _, pics = P.port_timeline(start, pad)
    c = P.oracle_picture(start, pad)
    if not pics:
        print('fieldpicture: %s leg %s: the port drew no picture' % (name, legs[i]['name'].split('_')[0]))
        return 1
    k, n = P.picture_grade(c, pics)
    print('fieldpicture: %s leg %s +%d idle frames: %d px differ at port %+d' % (
        name, legs[i]['name'].split('_')[0], d['frames'], n, k))
    if want_png:
        out = os.path.join(WORK, name + '.png')
        png(out, c, pics[k])
        print('  -> %s' % out)
    return 0


def main():
    os.makedirs(WORK, exist_ok=True)
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    bad = [a for a in args if a not in PICTURES]
    if bad:
        print('fieldpicture: no picture named %s (known: %s)' % (', '.join(bad), ', '.join(sorted(PICTURES))))
        return 1
    rc = 0
    for name in args or sorted(PICTURES):
        rc |= run(name, '--png' in sys.argv)
    return rc


if __name__ == '__main__':
    sys.exit(main())
