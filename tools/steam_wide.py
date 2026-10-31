#!/usr/bin/env python3
r"""The Steam release's widescreen margins, read from the player's own copy into a local file (a mod's data; never shipped).

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
After them an optional u16 flag and a fifth plane (the loader 0x4DAE30).

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
x (u16 chip, u8 palette, u8 flags: H 1, V 2, priority 4), u8 pages, per page: u8 page, u16 sheet. Then b"SPCL", u32
images, per image: u8 name length, the name, u16 w, u16 h, w * h RGBA -- the special scenes' extension art (SPECIAL),
halved to the SNES's pixels. Then b"PALS", u32 palettes, per palette: u16 location, 128 LE BGR555 -- the Steam release's
colours of the location's BG half, for the locations with added sheets: never drawn, a key to their indices
(steam_palette).
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


def steam_palette(f, files, loc):
    """The Steam release's palette of a location, its BG half (CGRAM 0-127, 128 LE BGR555): `palette_bin/plt<id>.bin`
    (u16 0x100, then 256 colours), id = the location's ROM header byte 3 (`Mapinfo` u16 5, equal to it in 511 of 512) -- or None. The Steam release
    recoloured its rooms; the port keeps the ROM's colours, so this is never drawn:
    the added sheets were painted with its indices, and where two of them are one colour in it but not in the ROM's (06B's
    0x2A and 0x5A, both 080808: the sky's 000010 and the machine's body 000000 in the ROM's) it tells the port which of
    the room's own colours a sheet's pixel stands for (scene.c CtFieldSceneBakeExt)."""
    info = files.get('Game/field/Mapinfo/mapinfo_%d.dat' % loc)
    if not info:
        return None
    e = files.get('Game/field/palette_bin/plt%d.bin' % struct.unpack('<11H', _block(f, *info)[:22])[5])
    if not e:
        return None
    d = _block(f, *e)
    return d[2:2 + 256] if len(d) >= 2 + 512 and struct.unpack('<H', d[:2])[0] == 0x100 else None


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


STEAM_APP = 613830                                       # Chrono Trigger on Steam


def _steam_roots():
    """The Steam installations this machine may have: the registry's on Windows, the usual folders elsewhere."""
    roots = []
    if sys.platform == 'win32':
        try:
            import winreg
            for hive, key, name in ((winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath'),
                                    (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath'),
                                    (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Valve\Steam', 'InstallPath')):
                try:
                    with winreg.OpenKey(hive, key) as k:
                        roots.append(winreg.QueryValueEx(k, name)[0])
                except OSError:
                    pass
        except ImportError:
            pass
        roots += [r'C:\Program Files (x86)\Steam', r'C:\Program Files\Steam']
    else:
        home = os.path.expanduser('~')
        roots += [os.path.join(home, p) for p in ('.steam/steam', '.local/share/Steam',
                                                  '.var/app/com.valvesoftware.Steam/.local/share/Steam',
                                                  'Library/Application Support/Steam')]
    return roots


def find_steam():
    """The folder of an installed Steam copy of Chrono Trigger (its resources.bin an ARC1 archive), or None.
    CT_STEAM_DIR names one directly. Every library of every Steam installation found is looked in (libraryfolders.vdf),
    then the usual library folders on each drive (Windows)."""
    env = os.environ.get('CT_STEAM_DIR')
    cands = [env] if env else []
    libs = []
    for root in _steam_roots():
        root = os.path.normpath(root)
        if not os.path.isdir(root):
            continue
        libs.append(root)
        vdf = os.path.join(root, 'steamapps', 'libraryfolders.vdf')
        try:
            import re
            libs += [p.replace('\\\\', '\\') for p in re.findall(r'"path"\s+"([^"]+)"', open(vdf, encoding='utf-8', errors='replace').read())]
        except OSError:
            pass
    if sys.platform == 'win32':
        libs += ['%s:\\SteamLibrary' % d for d in 'CDEFGHIJ']
    for lib in libs:
        sa = os.path.join(lib, 'steamapps')
        inst = 'Chrono Trigger'
        try:
            import re
            m = re.search(r'"installdir"\s+"([^"]+)"', open(os.path.join(sa, 'appmanifest_%d.acf' % STEAM_APP), encoding='utf-8', errors='replace').read())
            if m:
                inst = m.group(1)
        except OSError:
            pass
        cands.append(os.path.join(sa, 'common', inst))
    seen = set()
    for c in cands:
        if not c or os.path.normcase(c) in seen:
            continue
        seen.add(os.path.normcase(c))
        rb = os.path.join(c, 'resources.bin') if os.path.isdir(c) else c
        try:
            with open(rb, 'rb') as f:
                if _keystream_xor(f.read(16), 0)[:4] == b'ARC1':
                    return c
        except OSError:
            pass
    return None


def _png_rgba(data):
    """A PNG (8-bit: palette with tRNS, grey, RGB or RGBA; no interlace) as (w, h, RGBA bytes) -- the stdlib only."""
    import zlib
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    o, idat, plte, trns = 8, b'', b'', b''
    w = h = depth = ctype = 0
    while o + 8 <= len(data):
        n, typ = struct.unpack('>I4s', data[o:o + 8]); body = data[o + 8:o + 8 + n]; o += 12 + n
        if typ == b'IHDR': w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif typ == b'PLTE': plte = body
        elif typ == b'tRNS': trns = body
        elif typ == b'IDAT': idat += body
    ch = {0: 1, 2: 3, 3: 1, 6: 4}.get(ctype)
    if depth != 8 or not ch:
        return None
    raw = zlib.decompress(idat); stride = w * ch; rows = []; prev = bytearray(stride); p = 0
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0; b = prev[i]; c = prev[i - ch] if i >= ch else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line); prev = line
    out = bytearray()
    for line in rows:
        for x in range(w):
            if ctype == 3:
                k = line[x]
                out += plte[3 * k:3 * k + 3] + bytes([trns[k] if k < len(trns) else 255])
            elif ctype == 0: out += bytes([line[x]] * 3 + [255])
            elif ctype == 2: out += line[3 * x:3 * x + 3] + b'\xff'
            else: out += line[4 * x:4 * x + 4]
    return w, h, bytes(out)


def _halve(w, h, px):
    """The Steam release's special scenes are drawn at twice the SNES's resolution: back to its pixels, each 2 x 2 block's
    most common colour (see-through when most of the block is)."""
    out = bytearray()
    for y in range(0, h - 1, 2):
        for x in range(0, w - 1, 2):
            q = [px[4 * ((y + j) * w + x + i):4 * ((y + j) * w + x + i) + 4] for j in (0, 1) for i in (0, 1)]
            op = [c for c in q if c[3] >= 128]
            if len(op) < 2:
                out += b'\0\0\0\0'
            else:
                out += max(op, key=op.count)[:3] + b'\xff'
    return w // 2, h // 2, bytes(out)


# The special scenes' extension art the port draws (src/scene_art.c), halved to the SNES's pixels: the Epoch's cockpit
# walls beside the dashboard, past the 256 (sequence entry 6, the Epoch into Lavos).
SPECIAL = ['Game/special/into_lavos/40_kokscr_ext.png']


def specials(f, files):
    out = []
    for name in SPECIAL:
        if name in files:
            img = _png_rgba(_block(f, *files[name]))
            if img:
                out.append((name.rsplit('/', 1)[1],) + _halve(*img))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('steam', nargs='?', help="the Steam release's Chrono Trigger folder or its resources.bin (default: look for it)")
    ap.add_argument('-o', '--out', default='ct_wide.dat')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--find', action='store_true', help='only say where the Steam copy is')
    a = ap.parse_args()
    if not a.steam or a.find:
        found = find_steam()
        if a.find:
            print('steam-wide: %s' % (found or 'no Steam copy of Chrono Trigger found'))
            return 0 if found else 1
        if not found:
            sys.exit('steam-wide: no Steam copy of Chrono Trigger found (name its folder, or CT_STEAM_DIR)')
        a.steam = found
        print('steam-wide: the Steam copy at %s' % found)
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
        spc = specials(f, files)
        pals = {loc: p for loc in m if m[loc][5] for p in [steam_palette(f, files, loc)] if p}
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
        o.write(b'SPCL' + struct.pack('<I', len(spc)))   # (after the locations: a version-3 reader stops before it)
        for name, w, h, px in spc:
            o.write(struct.pack('<B', len(name)) + name.encode() + struct.pack('<HH', w, h) + px)
        o.write(b'PALS' + struct.pack('<I', len(pals)))  # (after the images: an older reader stops ahead of it)
        for loc, pal in sorted(pals.items()):
            o.write(struct.pack('<H', loc) + pal)
    print('steam-wide: %d locations with margins, %d chip sets, %d sheets, %d special images, %d palettes -> %s'
          % (len(m), len(sets), len(sheets), len(spc), len(pals), a.out))


if __name__ == '__main__':
    sys.exit(main())
