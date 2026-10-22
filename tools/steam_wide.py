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

ct_wide.dat: b"CTWD", u32 version 1, u32 count, then per location: u16 location, u8 w, u8 h, 4 * w * h u16 (LE).
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


def margins(f, files):
    out = {}
    for loc in range(LOCATIONS):
        e = files.get('Game/field/mapext_bin/mapext_%d.bin' % loc)
        if not e:
            continue
        d = _block(f, *e)
        w, h = d[0], d[1]
        planes = d[2:2 + 8 * w * h]
        if len(planes) == 8 * w * h and any(planes):
            out[loc] = (w, h, planes)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('steam')
    ap.add_argument('-o', '--out', default='ct_wide.dat')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    src = os.path.join(a.steam, 'resources.bin') if os.path.isdir(a.steam) else a.steam
    with open(src, 'rb') as f:
        m = margins(f, directory(f))
    if a.list:
        for loc, (w, h, _) in sorted(m.items()):
            print('location %03X: %dx%d cells' % (loc, w, h))
    with open(a.out, 'wb') as o:
        o.write(b'CTWD' + struct.pack('<II', 1, len(m)))
        for loc, (w, h, planes) in sorted(m.items()):
            o.write(struct.pack('<HBB', loc, w, h) + planes)
    print('steam-wide: %d locations with margins -> %s' % (len(m), a.out))


if __name__ == '__main__':
    main()
