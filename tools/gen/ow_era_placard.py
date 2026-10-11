#!/usr/bin/env python3
"""src/data/ow_era_placard.h -- the overworld ERA PLACARD's pixels, per era id, from the oracle's isolated OBJ layer.

    python tools/gen/ow_era_placard.py [--capture] [--out PATH]

The placard is an ordinary overworld ambient-VM object (class $0F63, attr $31) parked at (8,192); object-VM op 0x30
(C2/160F -> C2/1CDA) points it at entry [id] of $C3:E000, a one-op script `04 <descriptor> 00`. Ids $75..$7B are the
era placards (disasm/notes/ow_era_placard.md). Which placard, where and when are ported as code; only the PIXELS are
baked, because the port keeps no overworld OBJ VRAM. Per descriptor and not per tile name: the OBJ tile names are
per-world VRAM slots (tile $1AC is piece 0 of five descriptors and its art differs between worlds).

Each source state runs 240 frames under `ct.exe --peek ... --btnscript 0:240` with PEEK_ISOBG=4 (the OBJ layer alone,
the plain oracle as the original bake ran); the pieces are the OAM rows at Y 192 with attribute $31 and the strip is
cut from the picture, the isolated layer's backdrop (its modal colour) as the transparent key.

Sources (the names in the header's comments are the moments, as the original bake named them):
  saves/ow_w0.state         $75  1000 A.D.        default: recipe ow_nav_w0
  saves/com_jogo_salvo.state $76  600 A.D.         default: recipe com_jogo_salvo ($7E:1BAD bit0 set)
  saves/ow_w3.live          $78  world 3          default: `ct.exe --owforce <ow_nav_w0> 3`
  saves/ow_w4.live          $79  world 4                   ... 4
  saves/ow_w7.live          $7A  world 7                   ... 7
  saves/ow_w1.live          $7B  world 1 ("?")             ... 1
$77 (world 2) is not baked: its script gate is $7E:1BA6 >= 0x36 (the storyline) and no source reaches it.
--capture reads the original states instead (saves/ under the root).
"""
import argparse
import os
import subprocess
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, CT, ROM, recipe, scratch, rgb_of_fb  # noqa: E402
import ow_world  # noqa: E402

OUT = os.path.join(ROOT, 'src', 'data', 'ow_era_placard.h')
SOURCES = [('saves/ow_w0.state', 0x75, None), ('saves/com_jogo_salvo.state', 0x76, None),
           ('saves/ow_w3.live', 0x78, 3), ('saves/ow_w4.live', 0x79, 4),
           ('saves/ow_w7.live', 0x7A, 7), ('saves/ow_w1.live', 0x7B, 1)]
PLACARD_Y = 192          # the object's own Y, and every shipped piece has dY = 0
ROW0 = PLACARD_Y + 8     # the oracle fb puts SNES scanline 0 at image row 8


def peek_iso(state, tag):
    out = scratch('placard_' + tag)
    env = dict(os.environ, PEEK_ISOBG='4', SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    for ext in ('', '.oam'):
        if os.path.exists(out + ext):
            os.remove(out + ext)
    subprocess.run([CT, ROM, '--peek', state, out, '--btnscript', '0:240'], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    img = rgb_of_fb(out)
    oam = open(out + '.oam', 'rb').read()
    pieces = [(oam[i * 4], oam[i * 4 + 2] | ((oam[i * 4 + 3] & 1) << 8))
              for i in range(128) if oam[i * 4 + 1] == PLACARD_Y and oam[i * 4 + 3] == 0x31]
    # the transparent key is the isolated layer's backdrop (CGRAM colour 0, a mid brown on the overworld): the modal
    # colour of the whole frame
    key = Counter(bytes(img[i:i + 3]) for i in range(0, len(img), 3)).most_common(1)[0][0]
    return img, pieces, key


def strip(img, pieces, key):
    """The composed placard straight off the screen. An OAM x >= 128 is the PPU's wrap of a negative x (tile $1AC at
    dX -12 from base 8 = -4); those columns are off the left edge and are not shown."""
    xs = [(x - 256 if x >= 128 else x) for x, _ in pieces]
    lo, hi = min(xs), max(xs) + 16
    x0 = max(0, lo)
    w = hi - x0
    px = bytearray(w * 16 * 4)
    for ty in range(16):
        for tx in range(w):
            sx = x0 + tx
            if sx < 0 or sx >= 256:
                continue
            o = ((ROW0 + ty) * 256 + sx) * 3
            r, g, b = img[o], img[o + 1], img[o + 2]
            i = (ty * w + tx) * 4
            px[i], px[i + 1], px[i + 2] = r, g, b
            px[i + 3] = 0 if bytes((r, g, b)) == key else 255
    return x0, w, bytes(px)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--capture', action='store_true', help='read the original saves/ states')
    ap.add_argument('--out', default=OUT)
    a = ap.parse_args()
    os.environ.pop('CT_ORACLE_LOGICAL', None)          # the plain oracle, as the original bake
    if not a.capture:
        w0 = recipe('ow_nav_w0')
        states = {0x75: w0, 0x76: recipe('com_jogo_salvo')}
        for _, era, world in SOURCES:
            if world is not None:
                states[era] = ow_world.owforce(w0, world, scratch('placard_w%d' % world))
    eras, seen_in, shape = {}, {}, {}
    for name, era, _ in SOURCES:
        state = os.path.join(ROOT, name) if a.capture else states[era]
        img, pieces, key = peek_iso(state, '%02X' % era)
        if not pieces:
            sys.exit('ow_era_placard: no placard on screen in %s' % state)
        eras[era] = strip(img, pieces, key)
        seen_in[era] = name
        shape[era] = ' '.join('%03X@%d' % (t, x) for x, t in pieces)
        print('  %-28s id $%02X  x%-3d w%-3d  %s' % (name, era, eras[era][0], eras[era][1], shape[era]))

    ids = sorted(eras)
    maxw = max(w for _, w, _ in eras.values())
    with open(a.out, 'w', newline='\n') as f:
        f.write('// ow_era_placard.h -- GENERATED by tools/_gen_ow_placard.py, do not hand-edit.\n'
                '// The overworld ERA PLACARD, captured from the oracle isolated OBJ layer (PEEK_ISOBG=4).\n'
                '// WHICH placard is drawn, WHERE and WHEN all come from the ROM at run time (op 0x30 ->\n'
                '// $C3:E000[id] -> `04 <descriptor>` -> the piece list, gated on $7E:1BF7 >= 0x78); only\n'
                '// the pixels are baked, because the port keeps no overworld OBJ VRAM.\n'
                '// Keyed by ERA ID and not by tile name: the tile names are per-world VRAM slots, so the\n'
                '// same name is different art in different worlds.  See disasm/notes/ow_era_placard.md.\n'
                '#pragma once\n')
        f.write('#define CT_PLACARD_N %d\n' % len(ids))
        f.write('#define CT_PLACARD_MAXW %d\n' % maxw)
        f.write('#define CT_PLACARD_H 16\n')
        f.write('// { era id, screen x of the strip, width }.  y is the object\'s own Y ($C0 = 192) and\n'
                '// every shipped piece has dY = 0.\n')
        f.write('static const unsigned short ctPlacardEra[CT_PLACARD_N][3] = {\n')
        for t in ids:
            x0, w, _ = eras[t]
            f.write('  { 0x%02X, %3d, %3d },   /* %-28s %s */\n' % (t, x0, w, seen_in[t], shape[t]))
        f.write('};\n')
        f.write('// RGBA, 16 rows of CT_PLACARD_MAXW (only each row\'s own width is meaningful);\n'
                '// alpha 0 = the OBJ backdrop (transparent).\n')
        f.write('static const unsigned char ctPlacardPx[CT_PLACARD_N][16 * CT_PLACARD_MAXW * 4] = {\n')
        for t in ids:
            x0, w, b = eras[t]
            pad = bytearray()
            for ty in range(16):
                pad += b[ty * w * 4:(ty + 1) * w * 4]
                pad += bytes((maxw - w) * 4)
            f.write('  { /* $%02X */\n    ' % t)
            for i in range(0, len(pad), 32):
                f.write(','.join(str(v) for v in pad[i:i + 32]))
                f.write(',\n    ' if i + 32 < len(pad) else '\n')
            f.write('  },\n')
        f.write('};\n')
    print('ow_era_placard: %d era placards (%s) -> %s' % (len(ids), ' '.join('$%02X' % t for t in ids), a.out))


if __name__ == '__main__':
    main()
