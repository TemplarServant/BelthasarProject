#!/usr/bin/env python3
r"""The Steam release's widescreen margins, read from the player's OWN copy into a local file (a mod's data; never shipped).

    python tools/steam_wide.py "<Steam's Chrono Trigger folder or its resources.bin>" [-o ct_wide.dat]
    python tools/steam_wide.py <folder> --list            (the locations that carry margins, and their sizes)

The Steam release (2018-2023) draws its rooms wider than the SNES: for each location it keeps a map of the strip past the
room's scroll mask, `Game/field/mapext_bin/mapext_<location>.bin`, inside its `resources.bin`. This copies those maps,
for the locations the port has (0..511), into `ct_wide.dat`, which Belthasar reads when the option "Steam margins" is on
(settings.h wideSteamMargins, the F1 overlay's video tab; ROADMAP X4). The file holds Square Enix's data from the player's
own purchase, so it is written next to the player's ct_assets.dat and is never committed or published (.gitignore *.dat).

The archive ("ARC1"): each block XORed with an LCG keystream seeded by the block's file offset, then a big-endian length
and gzip -- the format as GitExl/CTViewer reads it (src/filesystem/resourcesbin.rs, MIT; credited to RiverNyxx's
CTExplore). A margins map: [w, h] (16-px cells) then four planes of w*h little-endian u16 metatile ids into the
location's own BG1/BG2 tile assembly (0 = nothing); it spans the room's columns -x .. right+x and rows 0 .. bottom, where
x = (w - (right + 1)) / 2 (6 for a room as narrow as the screen). Ids of 512 and up are tiles the Steam release added
(`map_bin/cgext*.bin`): not read here, the port draws nothing for them.

The margins are drawn with the Steam release's own tiles: it redefined metatiles the margins use (Guardia Forest's 268,
284, 300, 316: a closed canopy where the SNES's were partly empty) and loads chip sets the SNES's tileset leaves empty
(set 233 in slot 5 of tileset 3). So each location also carries its tileset's chip sets for slots 0-5 (Steam
`BGSetTable/bgsettable_<tileset>` -> `map_bin/cg<set>.bin`, kept once per set) and the Steam definition of every
metatile its margins use (`ChipTable/ChipTable_<assembly>`, 3 bytes a corner: chip 0x3FF, H 0x400, V 0x800, palette
>> 12; priority byte 2 bit 0). The room itself keeps the ROM's.

ct_wide.dat (all LE): b"CTWD", u32 version 2, u32 locations, u32 sets; per set: u8 set, 128 x 64 chip pixels (0..15);
per location: u16 location, u8 w, u8 h, 4 * w * h u16, u8 slot set[6] (0xFF none), u16 metatiles, per metatile: u16 id,
4 corners x (u16 chip, u8 palette, u8 flags: H 1, V 2, priority 4).
"""
import argparse
import gzip
import os
import struct
import sys

LOCATIONS = 0x200


def _keystream_xor(data, seed):
    out = bytearray(len(data))
    n = (0x19000000 + seed) & 0xFFFFFFFF
    for i, b in enumerate(data):
        n = (n * 0x41C64E6D + 12345) & 0xFFFFFFFF
        out[i] = b ^ (n >> 24)
    return bytes(out)


def _block(f, off, size):
    f.seek(off)
    d = _keystream_xor(f.read(size), off)
    return gzip.decompress(d[4:])[:struct.unpack('>I', d[:4])[0]]


def directory(f):
    f.seek(0)
    h = _keystream_xor(f.read(16), 0)
    if h[:4] != b'ARC1':
        sys.exit('steam-wide: not the Steam release\'s resources.bin (no ARC1 header)')
    _, doff, dlen = struct.unpack('<III', h[4:16])
    d = _block(f, doff, dlen)
    files = {}
    for k in range(struct.unpack('<I', d[:4])[0]):
        po, off, sz = struct.unpack('<III', d[4 + 12 * k:16 + 12 * k])
        files[d[po:d.index(b'\0', po)].decode('utf-8', 'replace')] = (off, sz)
    return files


def chip_set(f, files, s):
    """A Steam chip set: 4-byte header, then 4bpp nibbles (high first) of a 128-px-wide sheet, 16 chips a row."""
    e = files.get('Game/field/map_bin/cg%d.bin' % s)
    if not e:
        return None
    raw = _block(f, *e)[4:]
    sheet = bytearray(len(raw) * 2)
    sheet[0::2] = bytes(b >> 4 for b in raw)
    sheet[1::2] = bytes(b & 15 for b in raw)
    out = bytearray(128 * 64)
    for c in range(128):
        x, y = (c % 16) * 8, (c // 16) * 8
        for r in range(8):
            o = (y + r) * 128 + x
            out[c * 64 + r * 8:c * 64 + r * 8 + 8] = sheet[o:o + 8] if o + 8 <= len(sheet) else bytes(8)
    return bytes(out)


def margins(f, files):
    out = {}
    for loc in range(LOCATIONS):
        e = files.get('Game/field/mapext_bin/mapext_%d.bin' % loc)
        info = files.get('Game/field/Mapinfo/mapinfo_%d.dat' % loc)
        if not e or not info:
            continue
        d = _block(f, *e)
        w, h = d[0], d[1]
        planes = d[2:2 + 8 * w * h]
        if len(planes) != 8 * w * h or not any(planes):
            continue
        hd = struct.unpack('<11H', _block(f, *info)[:22])
        tileset, assembly = hd[1], hd[2]
        bs = files.get('Game/field/BGSetTable/bgsettable_%d.dat' % tileset)
        slots = list(_block(f, *bs)[:6]) if bs else [0xFF] * 6
        ce = files.get('Game/field/ChipTable/ChipTable_%04d.dat' % assembly)
        asm = _block(f, *ce) if ce else b''
        ids = sorted(set(v for v in struct.unpack('<%dH' % (4 * w * h), planes) if 0 < v < 512))
        metas = []
        for t in ids:
            if (t + 1) * 12 > len(asm):
                continue
            corners = []
            for k in range(4):
                o = (t * 4 + k) * 3
                v = asm[o] | asm[o + 1] << 8
                corners.append((v & 0x3FF, (v >> 12) & 15, (1 if v & 0x400 else 0) | (2 if v & 0x800 else 0) | (4 if asm[o + 2] & 1 else 0)))
            metas.append((t, corners))
        out[loc] = (w, h, planes, slots, metas)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('steam')
    ap.add_argument('-o', '--out', default='ct_wide.dat')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    src = os.path.join(a.steam, 'resources.bin') if os.path.isdir(a.steam) else a.steam
    with open(src, 'rb') as f:
        files = directory(f)
        m = margins(f, files)
        sets = {}
        for loc, (_, _, _, slots, _) in m.items():
            for s in slots:
                if s != 0xFF and s not in sets:
                    sets[s] = chip_set(f, files, s)
        sets = {s: px for s, px in sets.items() if px}
    if a.list:
        for loc, (w, h, _, _, metas) in sorted(m.items()):
            print('location %03X: %dx%d cells, %d metatiles' % (loc, w, h, len(metas)))
    with open(a.out, 'wb') as o:
        o.write(b'CTWD' + struct.pack('<III', 2, len(m), len(sets)))
        for s, px in sorted(sets.items()):
            o.write(struct.pack('<B', s) + px)
        for loc, (w, h, planes, slots, metas) in sorted(m.items()):
            o.write(struct.pack('<HBB', loc, w, h) + planes)
            o.write(bytes(s if s in sets else 0xFF for s in slots))
            o.write(struct.pack('<H', len(metas)))
            for t, corners in metas:
                o.write(struct.pack('<H', t) + b''.join(struct.pack('<HBB', c, p, fl) for c, p, fl in corners))
    print('steam-wide: %d locations with margins, %d chip sets -> %s' % (len(m), len(sets), a.out))


if __name__ == '__main__':
    main()
