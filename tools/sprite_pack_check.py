#!/usr/bin/env python3
r"""Every sprite frame's tiles are in its pack: the chip word's tile index is ELEVEN bits.

    python tools/sprite_pack_check.py

A sprite frame is 4 chips a unit, each a word of the assembly record; the anim drain's two copiers take the tile with
`AND #07FF` (C0/E687, C0/E534) from $CF:($CD + tile*32). The extractor read ten bits and capped a character's pack at
1024 tiles. Ayla's pack is $8C00 bytes (1120 tiles) and 29 of her frames use tiles $400-$45F, so those frames were
drawn from the tiles 1024 below (playthrough leg 126's end: her frames 1D / 1E; disasm/notes/sprite_chip_tile_index.md).

Two lines, both from the user's ROM / pack, nothing pinned but counts:

    spritepack: extractor: sprite 5 has <t> tiles, <f> frames, <n> chips past them; <k> frames on tiles 400-<max>
    spritepack: ct_assets.dat: the seven characters' <f> frames, <n> chips past their packs (tiles <t0> <t1> ...)

The first runs the extractor's own code on the ROM (assets/extract_assets.py pack_sprites, sprite 5); the second reads
the seven characters' blobs of ct_assets.dat as the port's compositor does (scene.c CtComposeSpriteHalves: a chip past
the pack is not drawn) -- a pack extracted before the fix reads `133 chips past` and 1024 tiles for sprite 5. (Only the
characters: an NPC's blob carries up to 255 frame records and the ones past its real assembly are whatever follows.)
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from ctlib import rom as _romlib  # noqa: E402

UNITS = (4, 8, 12, 24)
T_SPRITE = 15


def frames_of(blob):
    """(ntiles, [chip words of each frame]) of a sprite blob."""
    size, nframes, ntiles = blob[0] & 3, blob[1], blob[2] | blob[3] << 8
    units = UNITS[size]
    base = 4 + 48 + ntiles * 64
    out = []
    for f in range(nframes):
        fr = blob[base + f * units * 10:base + f * units * 10 + units * 8]
        out.append([fr[k * 2] | fr[k * 2 + 1] << 8 for k in range(units * 4)])
    return ntiles, out


def past(ntiles, frames):
    return sum(1 for fr in frames for c in fr if c and (c & 0x7FF) >= ntiles)


def main():
    os.chdir(ROOT)
    from assets import extract_assets
    rom = _romlib.load('Chrono_Trigger_USA_SNES.sfc')
    blob = None
    for t, idn, b in extract_assets.pack_sprites(rom, {'used_sprites': [5], 'static_max': {}}):
        if idn == 5:
            blob = bytes(b)
    if blob is None:
        print('spritepack: extractor: no blob for sprite 5')
        return 1
    nt, fr = frames_of(blob)
    high = [f for f in fr if any((c & 0x7FF) >= 0x400 for c in f)]
    top = max([c & 0x7FF for f in fr for c in f] or [0])
    print('spritepack: extractor: sprite 5 has %d tiles, %d frames, %d chips past them; %d frames on tiles 400-%03X' % (
        nt, len(fr), past(nt, fr), len(high), top))
    d = open('ct_assets.dat', 'rb').read()
    n = struct.unpack('<I', d[8:12])[0]
    nf = np = 0
    tiles = {}
    for i in range(n):
        t, idn, off, ln = struct.unpack('<IIII', d[12 + i * 16:28 + i * 16])
        if t != T_SPRITE or idn > 6:
            continue
        nt, fr = frames_of(d[off:off + ln])
        nf += len(fr); np += past(nt, fr); tiles[idn] = nt
    print("spritepack: ct_assets.dat: the seven characters' %d frames, %d chips past their packs (tiles %s)" % (
        nf, np, ' '.join(str(tiles.get(k, 0)) for k in range(7))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
