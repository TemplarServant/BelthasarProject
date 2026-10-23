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
CTExplore). A margins map: [w, h] (16-px cells) then four planes of w*h little-endian u16 cells: 0 = nothing, v = the
location's BG1/BG2 metatile v - 1 (the Steam executable's drawer, 0x54FD5C: page (v - 1) >> 8, tile (v - 1) & 255 of a
16 x 16 sheet; pages 2 and up are tiles the Steam release added, below). It spans the room's columns -x .. right+x and
rows 0 .. bottom, where x = (w - (right + 1)) / 2 (6 for a room as narrow as the screen); the planes are drawn 3, 2, 1, 0.
After them an optional u16 flag and a fifth plane (the loader 0x4DAE30). Checked against the owner's Steam 16:9
screenshots of 0x014 (scroll top and bottom): the strips they show, 99.9% of their pixels.

The margins are drawn with the Steam release's own tiles: it redefined metatiles the margins use (Guardia Forest's 268,
284, 300, 316: a closed canopy where the SNES's were partly empty) and loads chip sets the SNES's tileset leaves empty
(set 233 in slot 5 of tileset 3). So each location also carries its tileset's chip sets for slots 0-5 (Steam
`BGSetTable/bgsettable_<tileset>` -> `map_bin/cg<set>.bin`, kept once per set) and the Steam definition of every
metatile its margins use (`ChipTable/ChipTable_<assembly>`, 3 bytes a corner: chip 0x3FF, H 0x400, V 0x800, palette
>> 12; priority byte 2 bit 0). The room itself keeps the ROM's.

The planes are the Steam release's map layers (its texture builder 0x4DE2F0 draws them into its layer textures through
0x4DFA30): plane 3 with BG2's low-priority tiles, plane 2 with BG1's, planes 1 and 0 with the priority-1 tiles (the
layer above the sprites), and the fifth plane -- after a u16 flag, 15 locations, cells in 2 of them -- drawn opaque
(colour 0 too: 0x550260) with BG3. Cells of page 2 and up ((v - 1) >> 8 >= 2) are tiles the Steam release added: pixel
sheets of 256 x 256 (u32 w, u32 h, one CGRAM index a pixel, 0 = nothing), `map_bin/cgext<location in decimal>.bin` as
page 2, or `cgext<location>_<k>.bin` as page 2 + k for 107, 165, 475 and 592 (the loader 0x4D9870, called from
0x559AEE-0x559D2E); 0x1C and 0x15B take `cg_bg.bin` as page 1 and 0x15A-0x15E and 0x1C8-0x1CB `cgext_lavos.bin` as
page 2, both after the room's own pages are built.

ct_wide.dat (all LE): b"CTWD", u32 version 3, u32 locations, u32 sets, u32 sheets; per set: u8 set, 128 x 64 chip pixels
(0..15); per sheet: 256 x 256 CGRAM indices; per location: u16 location, u8 w, u8 h, 5 * w * h u16 (planes 0-4; the
fifth zero where the Steam release has none), u8 slot set[6] (0xFF none), u16 metatiles, per metatile: u16 id, 4 corners
x (u16 chip, u8 palette, u8 flags: H 1, V 2, priority 4), u8 pages, per page: u8 page, u16 sheet.
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


def sheet(f, files, name):
    """A Steam pixel sheet (cgext*, cg_bg): u32 w, u32 h, then one CGRAM index a pixel; 256 x 256 or None."""
    e = files.get('Game/field/map_bin/' + name)
    if not e:
        return None
    d = _block(f, *e)
    w, h = struct.unpack('<II', d[:8])
    return d[8:8 + 65536] if (w, h) == (256, 256) and len(d) >= 8 + 65536 else None


def sheet_pages(loc):
    """The pages past the room's own the Steam release loads for a location (0x559AEE-0x559D2E): [(page, sheet name)],
    later entries replacing earlier ones."""
    multi = {0x6B: 3, 0xA5: 2, 0x1DB: 2}
    pages = ([(2 + k, 'cgext%d_%d.bin' % (loc, k)) for k in range(multi[loc])] if loc in multi
             else [(2, 'cgext%d.bin' % loc)])
    if loc in (0x1C, 0x15B):
        pages.append((1, 'cg_bg.bin'))
    if 0x15A <= loc <= 0x15E or 0x1C8 <= loc <= 0x1CB:
        pages.append((2, 'cgext_lavos.bin'))
    return pages


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
        o = 2 + 8 * w * h                                  # the flag and the fifth plane (0x4DB0C4)
        fifth = d[o + 2:o + 2 + 2 * w * h] if len(d) >= o + 2 + 2 * w * h and d[o] | d[o + 1] else b''
        planes += fifth if len(fifth) == 2 * w * h else bytes(2 * w * h)
        hd = struct.unpack('<11H', _block(f, *info)[:22])
        tileset, assembly = hd[1], hd[2]
        bs = files.get('Game/field/BGSetTable/bgsettable_%d.dat' % tileset)
        slots = list(_block(f, *bs)[:6]) if bs else [0xFF] * 6
        ce = files.get('Game/field/ChipTable/ChipTable_%04d.dat' % assembly)
        asm = _block(f, *ce) if ce else b''
        ids = sorted(set(v - 1 for v in struct.unpack('<%dH' % (5 * w * h), planes) if 0 < v <= 512))
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
        out[loc] = (w, h, planes, slots, metas, sheet_pages(loc))
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
        for loc, (_, _, _, slots, _, _) in m.items():
            for s in slots:
                if s != 0xFF and s not in sets:
                    sets[s] = chip_set(f, files, s)
        sets = {s: px for s, px in sets.items() if px}
        sheets, index = [], {}                             # each sheet once; a location names its pages by index
        for loc in m:
            pages = []
            for pg, name in m[loc][5]:
                if name not in index:
                    px = sheet(f, files, name)
                    index[name] = len(sheets) if px else None
                    if px:
                        sheets.append(px)
                if index[name] is not None:
                    pages = [p for p in pages if p[0] != pg] + [(pg, index[name])]
            m[loc] = m[loc][:5] + (pages,)
    if a.list:
        for loc, (w, h, _, _, metas, pages) in sorted(m.items()):
            print('location %03X: %dx%d cells, %d metatiles, pages %s' % (loc, w, h, len(metas), [p for p, _ in pages]))
    with open(a.out, 'wb') as o:
        o.write(b'CTWD' + struct.pack('<IIII', 3, len(m), len(sets), len(sheets)))
        for s, px in sorted(sets.items()):
            o.write(struct.pack('<B', s) + px)
        for px in sheets:
            o.write(px)
        for loc, (w, h, planes, slots, metas, pages) in sorted(m.items()):
            o.write(struct.pack('<HBB', loc, w, h) + planes)
            o.write(bytes(s if s in sets else 0xFF for s in slots))
            o.write(struct.pack('<H', len(metas)))
            for t, corners in metas:
                o.write(struct.pack('<H', t) + b''.join(struct.pack('<HBB', c, p, fl) for c, p, fl in corners))
            o.write(struct.pack('<B', len(pages)) + b''.join(struct.pack('<BH', pg, s) for pg, s in pages))
    print('steam-wide: %d locations with margins, %d chip sets, %d sheets -> %s' % (len(m), len(sets), len(sheets), a.out))


if __name__ == '__main__':
    main()
