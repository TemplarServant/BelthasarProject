"""What the game can reach from the ROM's own tables -- the closures the ROM-free runtime packs (memory
rom-free-runtime-goal). Never a trace of what some run touched: a run reaches only part of the game.

The location load (C0/00F4 and its packets; src/lz.c ct_lz_load_cost / ct_lz_return_cost, disasm/notes/field_load_cost.md):
every one of the 512 location records ($F6:0000, 14 bytes) names
    +1  its tileset (FF none): the 8-entry list $F6:1C00 + t*8 (each FF or an index into $F6:2220) and $F6:2100 + t*3
    +2  its BG3 tileset (FF none): $F6:2220 + b*3 and $F6:21C0 + b*3
    +4  its map (word): $F6:1E00 + m*3
    +8  its script (word): $FC:F9F0 + s*3
"""

LOC_RECORDS = 0x360000
TILESET_LISTS = 0x361C00
MAP_PTRS = 0x361E00
ASM_PTRS = 0x362100
ASM3_PTRS = 0x3621C0
TS_PTRS = 0x362220
SCRIPT_PTRS = 0x3CF9F0          # $FC:F9F0
SCENE_PALETTES = 0x3624C0       # $F6:24C0 + rec[3] * 0xD2 (field.c fieldPaletteRows: 7 rows of 0x1E)
EXITS = 0x250000                # $E5:0000: 512 + 1 word pointers (bank-local), then 7-byte records (fieldStageExit)
TREASURE = 0x35F000             # $F5:F000: 512 + 1 word pointers (bank-local), then 4-byte records (fieldTreasureList)


def ptr24(rom, at):
    """A 3-byte HiROM pointer -> its file offset, or None outside the ROM."""
    bank = rom[at + 2]
    if bank < 0xC0:
        return None
    return (bank - 0xC0) << 16 | rom[at] | rom[at + 1] << 8


def location_indices(rom):
    """The indices every location reaches: tilesets, BG3 tilesets, tileset-list entries, maps, scripts."""
    ts, b3s, idxs, maps, scripts = set(), set(), set(), set(), set()
    for loc in range(512):
        r = LOC_RECORDS + loc * 14
        t, b3 = rom[r + 1], rom[r + 2]
        if t != 0xFF:
            ts.add(t)
            for i in range(8):
                ix = rom[TILESET_LISTS + t * 8 + i]
                if ix != 0xFF:
                    idxs.add(ix)
        if b3 != 0xFF:
            b3s.add(b3)
        maps.add(rom[r + 4] | rom[r + 5] << 8)
        scripts.add(rom[r + 8] | rom[r + 9] << 8)
    return ts, b3s, idxs, maps, scripts


def colour_slot_tables(rom):
    """(start, end) of bank F6's colour tables FD/E292's records name (objslot.c objslotTableCopy / mode 8), over every
    location: a 1x record copies count words at base + frame * (count*2 & FF) for frame 0..(mode & F); an 8x record takes
    its frame from its frame list."""
    lo, hi = None, None
    for loc in range(512):
        ti = (rom[LOC_RECORDS + loc * 14 + 3] << 1) & 0xFF
        x = rom[0x3DF9C7 + ti] | rom[0x3DF9C8 + ti] << 8
        d = lambda i: rom[0x3DFA77 + i]
        for _ in range(6):
            m = d(x) & 0xF0
            if m == 0:
                x += 1
                continue
            r0, cnt = d(x), d(x + 2)
            x += 4
            if m not in (0x10, 0x80):
                continue
            base = d(x) | d(x + 1) << 8
            x += 3
            frames = (r0 & 0x0F) + 1
            if m == 0x10:
                sels = range(frames)
            else:
                sels = [d(x + k) for k in range(frames)]
                x += frames
            step = (cnt * 2) & 0xFF
            a = base + min(sels) * step
            b = base + max(sels) * step + 2 * (cnt or 0x10000)
            lo = a if lo is None else min(lo, a)
            hi = b if hi is None else max(hi, b)
    return 0x360000 + lo, 0x360000 + hi


def location_tables(rom):
    """(name, start, end) of the location load's tables, each to the last index or byte a location reaches."""
    ts, b3s, idxs, maps, scripts = location_indices(rom)
    pals = {rom[LOC_RECORDS + loc * 14 + 3] for loc in range(512)}
    w = lambda o: rom[o] | rom[o + 1] << 8
    exits_end = max(w(EXITS + 2 * i) for i in range(513))
    treasure_end = max(w(TREASURE + 2 * i) for i in range(513))
    cs_lo, cs_hi = colour_slot_tables(rom)
    return [
        ('scene palettes ($F6:24C0, 0xD2 each)', SCENE_PALETTES, SCENE_PALETTES + 0xD2 * (max(pals) + 1)),
        ('colour-slot colour tables (bank F6, their closure)', cs_lo, cs_hi),
        ('exits ($E5:0000: pointers, then records)', EXITS, EXITS + exits_end),
        ('treasure ($F5:F000: pointers, then records)', TREASURE, 0x350000 + treasure_end),
        ('location tileset lists ($F6:1C00, 8 per tileset)', TILESET_LISTS, TILESET_LISTS + 8 * (max(ts) + 1)),
        ('location map pointers ($F6:1E00)', MAP_PTRS, MAP_PTRS + 3 * (max(maps) + 1)),
        ('location assembly pointers ($F6:2100)', ASM_PTRS, ASM_PTRS + 3 * (max(ts) + 1)),
        ('location BG3 assembly pointers ($F6:21C0)', ASM3_PTRS, ASM3_PTRS + 3 * (max(b3s) + 1)),
        ('location tileset pointers ($F6:2220)', TS_PTRS, TS_PTRS + 3 * (max(idxs | b3s) + 1)),
        ('location script pointers ($FC:F9F0)', SCRIPT_PTRS, SCRIPT_PTRS + 3 * (max(scripts) + 1)),
    ]


def location_streams(rom):
    """{what: set of stream file offsets} every location's load decompresses (or prices)."""
    ts, b3s, idxs, maps, scripts = location_indices(rom)
    def ps(table, ids):
        return {o for o in (ptr24(rom, table + 3 * i) for i in ids) if o is not None}
    return {
        'location maps': ps(MAP_PTRS, maps),
        'location tilesets': ps(TS_PTRS, idxs | b3s),
        'location assemblies': ps(ASM_PTRS, ts),
        'location BG3 assemblies': ps(ASM3_PTRS, b3s),
        'location scripts': ps(SCRIPT_PTRS, scripts),
    }


# The $C3 sequence stages (src/seq_*.c, disasm/notes/sequence_table.md, seq_picture.md): the overlay's packet table
# $FE:0000 (3-byte pointers, X = its index; C3:0000 takes the overlay at X = 3, the stages' $7E:3691 / $7E:36AA the rest).
# It ends where the first block it points to in its own bank begins.
SEQ_TABLE = 0x3E0000


def sequence_table_entries(rom):
    """[(x, file offset)] of the $FE:0000 table's pointers into the ROM's banks."""
    first = min(rom[SEQ_TABLE + x] | rom[SEQ_TABLE + x + 1] << 8
                for x in range(0, 0x100, 3) if rom[SEQ_TABLE + x + 2] == 0xFE)
    out = []
    for x in range(0, first - 2, 3):
        o = ptr24(rom, SEQ_TABLE + x)
        if o is not None:
            out.append((x, o))
    return out, first


def sequence_streams(rom):
    """{what: offsets} the stages decompress through the table ($7E:3691 / C3:0000): every entry (lz_pack skips a raw one)."""
    ents, _ = sequence_table_entries(rom)
    return {'sequence packets ($FE:0000 table)': {o for _, o in ents}}


def sequence_tables(rom, raw_offsets):
    """(name, start, end): the table itself, the raw blocks $7E:36AA copies (the table's bank-FE targets that are not LZ
    streams -- `raw_offsets`, from tools/gen/lz_pack.py -- each to the next target), $D2:0000's zero run, and the gate
    warp's FD tables (src/seq_gwarp.c: the index ranges its loops and masks give)."""
    ents, first = sequence_table_entries(rom)
    fe = sorted({o for _, o in ents if o >> 16 == 0x3E})
    out = [('sequence packet table ($FE:0000)', SEQ_TABLE, SEQ_TABLE + first)]
    for i, o in enumerate(fe):
        if o in raw_offsets:
            end = fe[i + 1] if i + 1 < len(fe) else SEQ_TABLE + 0x10000
            out.append(('sequence raw block ($FE:%04X)' % (o & 0xFFFF), o, end))
    out += [
        ('$D2:0000 (32 bytes the stages copy)', 0x120000, 0x120020),
        ('gate warp: radii, sprites, brightness ($FD:DD96-DE14)', 0x3DDD96, 0x3DDE15),
        ('gate warp: the plot bit masks ($FD:ED79, 16)', 0x3DED79, 0x3DED89),
        ('gate warp: the colours ($FD:F001-F120)', 0x3DF001, 0x3DF121),
        ('gate warp: the colour scales ($FD:F181, 64)', 0x3DF181, 0x3DF1C1),
    ]
    return out
