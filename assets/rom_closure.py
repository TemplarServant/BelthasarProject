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


# The sprites (src/ct_vm.c loadGfxMirror, src/move.c's loaders and anim drain, src/oam.c): every sprite record -- the
# PCs' and NPCs' at $E4:F000 (5 bytes: gfx, assembly, palette, anim, class; sprite s at F000 + 5s) and the enemies' at
# $E4:F600 (10 bytes) -- through the meta at $E4:FFE0 (the gfx, assembly, frame-table and duration-table pointer tables).
# ALL records, not the ones some script names: the closure is the tables'.
E4 = 0x240000
SPRITE_RECS = ((E4 + 0xF000, 5, (0xF600 - 0xF000) // 5), (E4 + 0xF600, 10, (0xFFE0 - 0xF600) // 10))


def _w(rom, o):
    return rom[o] | rom[o + 1] << 8


def sprite_fields(rom):
    """The sets of gfx, assembly, palette and anim ids any sprite record names."""
    g, a, p, n = set(), set(), set(), set()
    for base, stride, count in SPRITE_RECS:
        for i in range(count):
            r = base + stride * i
            g.add(rom[r]); a.add(rom[r + 1]); p.add(rom[r + 2]); n.add(rom[r + 3])
    return g, a, p, n


def sprite_meta(rom):
    return tuple(E4 + _w(rom, E4 + 0xFFE0 + 2 * k) for k in range(4))   # gfx, asm, frames, durations


def sprite_pointers(rom):
    """({gfx id: file offset}, {asm id: file offset}) of every id the records name."""
    gfx_t, asm_t, _, _ = sprite_meta(rom)
    g, a, _, _ = sprite_fields(rom)
    gp = {i: ptr24(rom, gfx_t + 3 * i) for i in g}
    ap = {i: ptr24(rom, asm_t + 3 * i) for i in a}
    return {k: v for k, v in gp.items() if v is not None}, {k: v for k, v in ap.items() if v is not None}


def sprite_streams(rom):
    """Every gfx pointer: the LZ sheets (lz_pack skips a raw one -- those are blocks, sprite_tables)."""
    gp, _ = sprite_pointers(rom)
    return {'sprite sheets ($E4:FFE0 gfx table)': set(gp.values())}


def blocks(offsets, ends=()):
    """[(start, end)] each offset to the next one in its own 64 KB bank (or to the bank's end, or to an `ends` bound
    that comes first): the raw data a pointer table names, cut at the next datum it names."""
    out = []
    srt = sorted(set(offsets))
    for i, o in enumerate(srt):
        bank_end = (o & ~0xFFFF) + 0x10000
        nxt = srt[i + 1] if i + 1 < len(srt) and srt[i + 1] < bank_end else bank_end
        for e in ends:
            if o < e < nxt:
                nxt = e
        out.append((o, nxt))
    return out


def sprite_tables(rom, raw_offsets):
    """(name, start, end) of the sprite system's tables: the records, the meta, the four pointer tables to the last id
    the records name, the palettes, the frame and duration tables every anim's pointers span, the raw graphics (the gfx
    pointers that are not LZ streams -- `raw_offsets` from lz_pack) and the assemblies, each a block to the next datum."""
    gfx_t, asm_t, fp_t, dp_t = sprite_meta(rom)
    g, a, p, n = sprite_fields(rom)
    fp = [_w(rom, fp_t + 2 * i) for i in range(max(n) + 2)]
    dp = [_w(rom, dp_t + 2 * i) for i in range(max(n) + 2)]
    out = [
        ('sprite records: PCs and NPCs ($E4:F000, 5 each)', E4 + 0xF000, E4 + 0xF000 + 5 * SPRITE_RECS[0][2]),
        ('sprite records: enemies ($E4:F600, 10 each, the 8-bit id: to $E5:0000)', E4 + 0xF600, E4 + 0xF600 + 10 * 256),
        ('sprite meta ($E4:FFE0)', E4 + 0xFFE0, E4 + 0xFFE8),
        ('sprite palettes ($E4:0000, 24 each)', E4, E4 + 24 * (max(p) + 1)),
        ('sprite gfx pointers', gfx_t, gfx_t + 3 * (max(g) + 1)),
        ('sprite assembly pointers', asm_t, asm_t + 3 * (max(a) + 1)),
        ('sprite frame-table pointers', fp_t, fp_t + 2 * (max(n) + 2)),
        ('sprite duration-table pointers', dp_t, dp_t + 2 * (max(n) + 2)),
        ('sprite frame tables (bank E4)', E4 + min(fp), E4 + max(fp)),
        ('sprite duration tables (bank E4)', E4 + min(dp), E4 + max(dp)),
    ]
    gp, ap = sprite_pointers(rom)
    raw = [o for o in gp.values() if o in raw_offsets]
    for s0, e0 in blocks(raw):
        out.append(('sprite raw tiles ($%02X:%04X)' % (0xC0 + (s0 >> 16), s0 & 0xFFFF), s0, e0))
    for s0, e0 in blocks(ap.values()):
        out.append(('sprite assembly ($%02X:%04X)' % (0xC0 + (s0 >> 16), s0 & 0xFFFF), s0, e0))
    return out


def party_tables(rom):
    """(name, start, end) of the tables the field's party code reads (src/move.c): the walk-anim dispatcher C0/9ECD's
    jump table and the handler bodies whose operands it decodes, the triple-tech tables FF/F958 reads, and the weapon
    palettes the epilogue C2/834D copies -- each extent the reading code's own index range."""
    cd6cec = 0x0D6CEC
    return [
        ('walk-anim jump table ($C0:9FF7, X & 7E)', 0x009FF7, 0x009FF7 + 0x80),
        ('walk-anim handlers ($C0:A079-A171, their operands)', 0x00A079, 0x00A171),
        ('triple-tech members ($CC:2953, 5 x 3)', 0x0C2953, 0x0C2953 + 15),
        ('triple-tech masks ($CC:2983, 5)', 0x0C2983, 0x0C2983 + 5),
        ('PC bit table ($FF:F9BB, 8-bit index)', 0x3FF9BB, 0x3FF9BB + 0x100),
        ('weapon palette index ($CD:6CEC, 8-bit index)', cd6cec, cd6cec + 0x100),
        ('weapon palettes ($D1:4B00, 6 each)', 0x114B00, 0x114B00 + 6 * (max(rom[cd6cec:cd6cec + 0x100]) + 1)),
    ]


FF, C2, CC, DE = 0x3F0000, 0x020000, 0x0C0000, 0x1E0000


def word_table(rom, at, bank):
    """A table of 16-bit pointers into `bank` that ends where its first target begins: (entries, end)."""
    ts, o = [], at
    while o < bank + min(ts + [0x10000]) and o + 1 < len(rom):
        ts.append(_w(rom, o))
        o += 2
    return ts, o


# C2/EF7E's codes 01-0F and the operand bytes their handlers (C2:EFD4) step over: 02 05 06 07 two, 03 0B four, 04 three
# (a long pointer to another string, which C2/EF7E walks), 08 09 0A one, 0C three; 0D-0F are an RTS.
MENU_STRING_OPERANDS = {0x01: 0, 0x02: 2, 0x03: 4, 0x04: 3, 0x05: 2, 0x06: 2, 0x07: 2, 0x08: 1, 0x09: 1, 0x0A: 1,
                        0x0B: 4, 0x0C: 3, 0x0D: 0, 0x0E: 0, 0x0F: 0}


def menu_string(rom, a, seen, out, depth=0):
    """C2/EF7E's string from `a` to its 00 (read), its includes walked too; returns the 0C codes' cap indices."""
    s0, caps = a, []
    while a < len(rom) and rom[a]:
        c = rom[a]
        a += 1
        if c >= 0x10:
            continue
        if c == 0x04 and depth < 4 and rom[a + 2] >= 0xC0:      # (a string in WRAM is not the ROM's)
            inc = ((rom[a + 2] - 0xC0) << 16) | _w(rom, a)
            if inc not in seen:
                seen.add(inc)
                caps += menu_string(rom, inc, seen, out, depth + 1)
        if c == 0x0C:
            caps.append(rom[a])
        a += MENU_STRING_OPERANDS[c]
    out.append(('menu string ($%02X:%04X, C2/EF7E)' % (0xC0 + (s0 >> 16), s0 & 0xFFFF), s0, a + 1))
    return caps


def window_program(rom, a):
    """C2/ED31's program from `a`: [op][FF:BD18[op] + 1 operand bytes] until a byte with bit 7 (the end, read)."""
    while not rom[a] & 0x80:
        a += 1 + rom[FF + 0xBD18 + rom[a]] + 1
    return a + 1


def until0(rom, a):
    while rom[a]:
        a += 1
    return a + 1


def hdma_runs(rom, a, width):
    """An HDMA-style table read as [n, `width` bytes] entries until n = 0 (that byte read too)."""
    while rom[a]:
        a += 1 + width
    return a + 1


def menu_tables(rom):
    """(name, start, end) of the tables the menus and their text read (src/menu.c, menu_cost.c, vwf.c and field.c's
    menu and treasure text), each extent the reading code's own index range or the closure of a program it walks."""
    out = []
    t = lambda name, a, b: out.append((name, a, b))
    # the menu strings: $FF:C457's table (to its first string), every string by C2/EF7E's codes, its includes
    ss, end = word_table(rom, FF + 0xC457, FF)
    t('menu string pointers ($FF:C457)', FF + 0xC457, end)
    seen, caps = set(), []
    for p in ss:
        if FF + p not in seen:
            seen.add(FF + p)
            caps += menu_string(rom, FF + p, seen, out)
    t('stat caps ($FF:CF33, the 0C codes\' cap index, LV and the stats)', FF + 0xCF33, FF + 0xCF33 + max(caps + [7]) + 1)
    t('item classes: record bases, strides ($FF:CF0E, CF18)', FF + 0xCF0E, FF + 0xCF18 + 5)
    t('Tab stat offsets and caps ($FF:CF2B, CF32, X 1-6)', FF + 0xCF2B, FF + 0xCF32 + 7)
    # the VWF's strings (C2/57DF): the exchange prompt's $FF:CF3B and the save slots' $FF:D3FE, each to its 00
    for at in (0xCF3B, 0xD03E, 0xD3FE):                  # (+ the save slots' chapter titles, $FF:D03E)
        ps, end = word_table(rom, FF + at, FF)
        t('VWF string pointers ($FF:%04X)' % at, FF + at, end)
        for p in ps:
            t('VWF string ($FF:%04X)' % p, FF + p, until0(rom, FF + p))
    t('save-slot chapter thresholds ($FF:D024, X < 1A)', FF + 0xD024, FF + 0xD024 + 0x1A)
    t('save-slot header shifts ($C2:D329, slot < 3)', C2 + 0xD329, C2 + 0xD329 + 3)
    # the VWF's font and tables: FF:2060 + code x 24 (left eight) and FF:3860 + (code >> 1) x 24 (right four), the
    # code 8-bit; C2:5FE6 / C2:6066 column offsets (x = 2 * (pen >> 3) + 4 at most), C2:60E6 advances (code - A0)
    t('VWF font ($FF:2060, $FF:3860)', FF + 0x2060, FF + 0x3860 + 0x80 * 24)
    t('VWF column offsets and advances ($C2:5FE6-61E5)', C2 + 0x5FE6, C2 + 0x60E6 + 0x100)
    # the portraits and element plaques (C2/EC38, C2/A273), for the eight ids
    tiles = [rom[FF + 0xCE70 + i] for i in range(8)]
    pals = [_w(rom, FF + 0xCE78 + 2 * i) for i in range(8)]
    xs = [rom[C2 + 0xA2C6 + i] for i in range(8)]
    t('portrait tiles, palettes ($FF:CE70, CE78)', FF + 0xCE70, FF + 0xCE88)
    t('plaque index ($C2:A2C6)', C2 + 0xA2C6, C2 + 0xA2CE)
    t('plaque words, palettes ($FF:CE88, CE92 + x)', FF + 0xCE88, FF + 0xCE92 + max(xs) + 2)
    for i in range(8):
        t('portrait %d ($FF:0000 + 32 (tile - 1), 36 tiles)' % i, FF + 32 * (tiles[i] - 1), FF + 32 * (tiles[i] - 1) + 36 * 32)
        t('portrait %d palette' % i, FF + pals[i], FF + pals[i] + 32)
    for x in set(xs):
        wd, ep = _w(rom, FF + 0xCE88 + x), _w(rom, FF + 0xCE92 + x)
        if (wd & 0x3FF) >= 0x100:
            a = FF + 0xD5E4 + 32 * ((wd & 0x3FF) - 0x100)
            t('plaque ($FF:D5E4, 12 tiles)', a, a + 12 * 32)
            t('plaque palette', FF + ep, FF + ep + 32)
    # the cursor graphs (a length byte, then 6-byte cells) the menus load
    for g in (0xB63D, 0xB6B0, 0xB731, 0xB77A, 0xB7A5):
        t('cursor graph ($FF:%04X)' % g, FF + g, FF + g + 1 + rom[FF + g])
    # the window programs (C2/ED31) and their operand lengths ($FF:BD18, ops 00-14: C2:ED59's eleven handlers)
    t('window program operand lengths ($FF:BD18)', FF + 0xBD18, FF + 0xBD18 + 0x15)
    t('save prompt window program ($FF:C16F)', FF + 0xC16F, window_program(rom, FF + 0xC16F))
    t('window cell pattern ($C2:EEF5, 4 x 4)', C2 + 0xEEF5, C2 + 0xEEF5 + 16)
    # the menus' HDMA tables: the ribbon's window ($FF:BAAA, 5-byte groups; $FF:BAE3, 2-byte) and the save screen's
    # register record ($FF:BCE6, $2123-$212F); the shop's scroll and mode tables (a count byte, then [n, value])
    t('ribbon window ($FF:BAAA)', FF + 0xBAAA, hdma_runs(rom, FF + 0xBAAA, 4))
    t('save prompt window ($FF:BAE3)', FF + 0xBAE3, hdma_runs(rom, FF + 0xBAE3, 1))
    t('save screen registers ($FF:BCE6)', FF + 0xBCE6, FF + 0xBCE6 + 13)
    for a, wdt in ((0xB8A4, 2), (0xB9CA, 2), (0xB8CA, 2), (0xB9F0, 2), (0xBB33, 1)):
        t('shop HDMA ($FF:%04X)' % a, FF + a, hdma_runs(rom, FF + a + 1, wdt))
    # the item and tech pop-up: the curve ($FF:CEBA: HP from 0, MP from 2A, each to a byte with bit 7), the digits'
    # 2bpp tiles ($FF:9390, 10 x 16), the column offsets ($C2:90F8, positions 0-3), the two palettes
    a = FF + 0xCEBA + 0x2A
    while not rom[a] & 0x80:
        a += 1
    t('pop-up curve ($FF:CEBA)', FF + 0xCEBA, a + 1)
    t('pop-up digits ($FF:9390)', FF + 0x9390, FF + 0x9390 + 160)
    t('pop-up columns ($C2:90F8)', C2 + 0x90F8, C2 + 0x90F8 + 4)
    t('pop-up palettes ($FF:CB9C, CBAC)', FF + 0xCB9C, FF + 0xCBAC + 8)
    # the party's places on the menus' screens
    t('item screen party ($C2:B045, 3 words)', C2 + 0xB045, C2 + 0xB045 + 6)
    t('tech screen partners ($C2:BB1A, 3)', C2 + 0xBB1A, C2 + 0xBB1A + 3)
    t('tech target party ($C2:BF2F, 3 words)', C2 + 0xBF2F, C2 + 0xBF2F + 6)
    t('Tab stat bytes ($C2:B315, X 0-6)', C2 + 0xB315, C2 + 0xB315 + 7)
    t('gear slot labels ($C2:A3BA, 4 x 10)', C2 + 0xA3BA, C2 + 0xA3BA + 40)
    t('item classes ($C2:88A7)', C2 + 0x88A7, C2 + 0x88A7 + 5)
    # the item names (11 bytes an id, the id 8-bit) and records ($CC: FF:CF0E[class] + index x FF:CF18[class])
    t('item names ($CC:0B5E, 11 x 256)', CC + 0x0B5E, CC + 0x0B5E + 11 * 256)
    cls = list(rom[C2 + 0x88A7:C2 + 0x88A7 + 5]) + [0x100]
    for c in range(5):
        base, stride = _w(rom, FF + 0xCF0E + 2 * c), rom[FF + 0xCF18 + c]
        t('item records, class %d' % c, CC + base, CC + base + (cls[c + 1] - cls[c]) * stride)
    # the shops: $CC:2C6D's pointers (to its first list), each list 32 bytes (C2/DAF5)
    ls, end = word_table(rom, CC + 0x2C6D, CC)
    t('shop list pointers ($CC:2C6D)', CC + 0x2C6D, end)
    for p in set(ls):
        t('shop list ($CC:%04X)' % p, CC + p, CC + p + 32)
    # the exchange screen (C2/C7AC): its graph above, FF/F677's map rows, palettes, FF/F716's wave, $FF:D57A
    t('exchange map rows ($FF:F6F2, 6 words)', FF + 0xF6F2, FF + 0xF6F2 + 12)
    t('exchange palettes ($FF:F6FE 8, $FF:F706 16)', FF + 0xF6FE, FF + 0xF706 + 16)
    t('exchange wave ($FF:F759, x <= 7A)', FF + 0xF759, FF + 0xF759 + 0x7C)
    t('exchange HDMA seed ($FF:D57A, 12)', FF + 0xD57A, FF + 0xD57A + 12)
    # the treasure messages ($DE:FF00, three) and the dictionary they use ($DE:FA00: 128 [len][bytes] substrings)
    ms = [_w(rom, DE + 0xFF00 + 2 * i) for i in range(3)]
    t('treasure messages ($DE:FF00)', DE + 0xFF00, DE + 0xFF06)
    for p in ms:
        t('treasure message', DE + p, until0(rom, DE + p))
    t('text dictionary pointers ($DE:FA00, 128)', DE + 0xFA00, DE + 0xFA00 + 256)
    for i in range(128):
        ds = DE + _w(rom, DE + 0xFA00 + 2 * i)
        t('text dictionary entry', ds, ds + 1 + rom[ds])
    # the field's caption records ($FD:A000, C0/3E84's operand & 7F: 128 runs, each to a byte with bit 7)
    a = 0x3DA000
    for _ in range(128):
        while not rom[a] & 0x80:
            a += 1
        a += 1
    t('caption records ($FD:A000, 128)', 0x3DA000, a)
    return out


def menu_streams(rom):
    """The menus' LZ streams: the exchange's BG3 characters ($FF:E8DC, FF/F677's C3/0002)."""
    return {'menu LZ streams': {FF + 0xE8DC}}
