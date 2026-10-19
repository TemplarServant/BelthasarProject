"""The battle's closures over the ROM's own tables (package G of the ROM-free runtime, memory rom-free-runtime-goal).

Like assets/rom_closure.py: every extent is the reading code's own index range (src/battle.c) or the closure of a program
it walks -- never a trace of what some run touched. Each function returns (name, start, end) file-offset triples, which
assets/extract_assets.py packs as CT_ASSET_TABLE (table_spans merges the overlapping ones).
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C1, CC, CD, CE, D1 = 0x010000, 0x0C0000, 0x0D0000, 0x0E0000, 0x110000


def _w(rom, o):
    return rom[o] | rom[o + 1] << 8


def spans(offsets):
    """A set of byte offsets -> sorted [start, end) runs."""
    out = []
    for o in sorted(offsets):
        if out and o <= out[-1][1]:
            out[-1][1] = max(out[-1][1], o + 1)
        else:
            out.append([o, o + 1])
    return out


# ---- the anim scripts (C1/4535 .. C1/4CD4, src/battle.c battleAscArm / battleAscTick) -------------------------------

ANIM_OP_JT = C1 + 0x7A6B          # C1/7A6B: the opcode handlers, op*2 (C1/4CD4 JSR ($7A6B,X)); ops 00-DA
ANIM_OPS = 0xDB
CE_SUPER_TBL = CE                 # CE/0000[ss*2]: `1E ss`'s sub-script (C1/56CC); `1F` returns (C1/56FB)


def anim_oplen(rom):
    """saves/anim_oplen.bin (tools/gen/anim_oplen.py: the engine's battleAnimOpLen), -1 = no length."""
    p = os.path.join(ROOT, 'saves', 'anim_oplen.bin')
    if not os.path.exists(p):
        raise SystemExit('rom_closure_battle: saves/anim_oplen.bin is missing -- python tools/gen/anim_oplen.py')
    b = open(p, 'rb').read()
    return [x - 256 if x >= 128 else x for x in b]


def anim_handler(rom, op):
    return _w(rom, ANIM_OP_JT + 2 * op) if op < ANIM_OPS else -1


def anim_walk(rom, q, bank_end, lens, seen, supers, depth=0):
    """One track from `q` the way battleAscTick runs it: each op its length (80: 1 + the count nibble), up to the
    00 / 01-class terminators (handlers C1/4CF0, C1/4CF7, read) or a 1F return; a `1E ss` walks CE/0000[ss*2] too.
    An op with no length stops the track there, as it stops the interpreter."""
    for _ in range(512):
        if q >= bank_end:
            return
        op = rom[q]
        seen.add(q)
        h = anim_handler(rom, op)
        if h in (0x4CF0, 0x4CF7) or op == 0x1F:
            return
        n = (1 + (rom[q + 1] & 0x0F)) if op == 0x80 else lens[op]
        if n is None or n <= 0:
            return
        seen.update(range(q, q + n))
        if op == 0x1E and depth < 3:
            ss = rom[q + 1]
            supers.add(ss)
            tb = CE_SUPER_TBL + 2 * ss
            seen.update((tb, tb + 1))
            anim_walk(rom, CE | _w(rom, tb), CE + 0x10000, lens, seen, supers, depth + 1)
        q += n


def anim_script(rom, hdr, bank, lens, seen, supers):
    """A script's header (word 1 = the $A0B3 = 0 track mask, word 2 the variant's; their pointers follow, word 1's
    first) and every track both masks name."""
    m1, m2 = rom[hdr] << 8 | rom[hdr + 1], rom[hdr + 2] << 8 | rom[hdr + 3]
    n = bin(m1).count('1') + bin(m2).count('1')
    seen.update(range(hdr, hdr + 4 + 2 * n))
    for k in range(n):
        anim_walk(rom, bank | _w(rom, hdr + 4 + 2 * k), bank + 0x10000, lens, seen, supers)


# The dispatch tables (C1/442C, C1/4535, C1/4617, C1/46EF, C1/47C6), each to the next one: [table, end, script bank].
ANIM_TABLES = (
    (CD + 0x5EC6, CD + 0x5EF0, CE),   # PC physical, $9877 (CD/5EC6)
    (CD + 0x5EF0, CD + 0x5FF0, CE),   # PC tech (CD/5EF0)
    (CD + 0x5FF0, CD + 0x61F0, CD),   # enemy attack (CD/5FF0)
    (CD + 0x61F0, CD + 0x63F0, CD),   # enemy tech (CD/61F0)
    (CD + 0x63F0, CD + 0x656E, CE),   # PC item (CD/63F0), to CD/656E's word table
)


def anim_tables(rom):
    lens = anim_oplen(rom)
    seen, supers = set(), set()
    out = [('anim opcode handlers ($C1:7A6B, 00-DA)', ANIM_OP_JT, ANIM_OP_JT + 2 * ANIM_OPS)]
    # battleAnimOpLenRom reads each handler's own advance tail: up to 0x60 bytes + the 5 of `A9 len 4C BB 75`
    for h in sorted({anim_handler(rom, op) for op in range(ANIM_OPS)}):
        out.append(('anim opcode handler C1/%04X (its advance, read)' % h, C1 + h, C1 + h + 0x65))
    for t0, t1, bank in ANIM_TABLES:
        out.append(('anim dispatch table $CD:%04X' % (t0 & 0xFFFF), t0, t1))
        for e in range(t0, t1, 2):
            anim_script(rom, bank | _w(rom, e), bank, lens, seen, supers)
    for a, b in spans(seen):
        out.append(('anim script bytes $%02X:%04X' % (0xC0 + (a >> 16), a & 0xFFFF), a, b))
    return out


# ---- what an action's anim config stages (battleFxStageCfg, C1/4497 / C1/432C / C1/45BB / C1/476F / C1/4693) --------
# $987E -> a D1 word (the effect frames' LZ stream, C1/486A), $9881 -> a CD word (the palette selections op 60 and
# CC/F1E7 read, 2 * an 8-bit index on), $9884 -> a CE word (the effect objects' animation set C1/48EC walks).

def stage_cfgs(rom):
    """Every (987E, 9881, 9884) triple a config can stage, with the three word tables it reads them through, each
    config table over its whole 8-bit index."""
    out = []
    for base, t2, t3 in ((CD + 0x5526, CD + 0x6A2C, CD + 0x666E),    # an enemy tech: $AE93 * 7
                         (CD + 0x5C26, CD + 0x6C2C, CD + 0x656E),    # an item: (id - BC) * 7
                         (CD + 0x45A6, CD + 0x692C, CD + 0x656E)):   # a PC tech: id * 7
        for i in range(256):
            r = base + 7 * i
            out.append((rom[r + 3], rom[r + 4], rom[r + 5], D1 + 0x0060, t2, t3))
    for base in (CD + 0x4926, CD + 0x4F26):                          # an enemy physical: id * 6 (the $AE93 arms)
        for i in range(256):
            r = base + 6 * i
            out.append((rom[r + 3], rom[r + 4], rom[r + 5], D1 + 0x0060, CD + 0x6A2C, CD + 0x666E))
    for base in (CD + 0x4015, CD + 0x401A, CD + 0x401F):             # a PC physical: the weapon * 15 (far, close, crit)
        for i in range(256):
            r = base + 15 * i
            out.append((rom[r + 2], rom[r + 3], rom[r + 4], D1 + 0x0000, CD + 0x686E, CD + 0x64B0))
    return out


def anim_set_end(rom, x):
    """C1/48EC's walk of the CE animation set at `x`: 4-byte records, 16 animations (each closed by bit 7 or bit 5
    alone of its last record's byte 0)."""
    anim = 0
    for _ in range(0x4000):
        b = rom[CE + (x & 0xFFFF)] & 0xE0
        if not b or (b & 0x40 and not b & 0x80):
            x += 4
            continue
        x += 4
        anim += 1
        if anim == 0x10:
            break
        x += 4
    return x


def stage_tables(rom):
    out = [('PC tech anim configs ($CD:45A6, 7 x 256)', CD + 0x45A6, CD + 0x45A6 + 7 * 256),
           ('enemy tech anim configs ($CD:5526, 7 x 256)', CD + 0x5526, CD + 0x5526 + 7 * 256),
           ('item anim configs ($CD:5C26, 7 x 256)', CD + 0x5C26, CD + 0x5C26 + 7 * 256),
           ('enemy physical anim records ($CD:4926 / 4F26, 6 x 256)', CD + 0x4926, CD + 0x4F26 + 6 * 256),
           ('PC physical anim records ($CD:4015, 15 x 256)', CD + 0x4015, CD + 0x401F + 15 * 256)]
    seen = set()
    for e7e, e81, e84, t1, t2, t3 in stage_cfgs(rom):
        seen.update((t1 + 2 * e7e, t1 + 2 * e7e + 1, t2 + 2 * e81, t2 + 2 * e81 + 1, t3 + 2 * e84, t3 + 2 * e84 + 1))
        w2, w3 = _w(rom, t2 + 2 * e81), _w(rom, t3 + 2 * e84)
        seen.update(CD + ((w2 + k) & 0xFFFF) for k in range(512))   # (w + 2 * a1) & FFFF, a1 8-bit, and the next byte
        a = anim_set_end(rom, w3)
        seen.update(CE + (x & 0xFFFF) for x in range(w3, a))
    for a, b in spans(seen):
        out.append(('anim staging words and their data $%02X:%04X' % (0xC0 + (a >> 16), a & 0xFFFF), a, b))
    out.append(('anim palettes, three colours ($D1:4100, 6 x 256)', D1 + 0x4100, D1 + 0x4100 + 6 * 256))
    out.append(('anim palettes, seven colours ($D1:4400, 14 x 256)', D1 + 0x4400, D1 + 0x4400 + 14 * 256))
    out.append(('anim op 7C / 7D sounds ($CD:6D4C, $CD:6DAC, the weapon 8-bit)', CD + 0x6D4C, CD + 0x6DAC + 256))
    out.append(('anim speed and rate ($CC:F4BC, $CC:F5BC, ops 08-0F)', CC + 0xF4BC, CC + 0xF4BC + 8))
    out.append(('anim speed and rate ($CC:F5BC)', CC + 0xF5BC, CC + 0xF5BC + 8))
    return out


def stage_streams(rom):
    """The effect frames' LZ streams (C1/486A: D1 + D1:0060[$987E * 2], or D1:0000's for a PC physical)."""
    return {'battle effect frames ($D1 words, C1/486A)': {D1 + _w(rom, t1 + 2 * e7e) for e7e, _, _, t1, _, _ in stage_cfgs(rom)}}



# ---- the battle effects (CD/0F4F .. CD/3BFD, D1's programs and layouts, CE's tables, CF / D0's sheets) --------------
CF, D0 = 0x0F0000, 0x100000


def word_table(rom, at, bank):
    """16-bit pointers into `bank` from `at`, to where the first one points: (entries, end)."""
    ts, o = [], at
    while o < bank + min(ts + [0x10000]):
        ts.append(_w(rom, o))
        o += 2
    return ts, o


def fx_layout_extent(rom, lp):
    """CD/10AB's layout at D1 + lp: [stride][0x20 bytes of tile mask][the tiles]; 16 rows x 16 columns at most, a
    tile every set bit, `stride` bytes apart -- or, with stride bit 7, one byte a tile cycling in 0x20 (CD/111E)."""
    stride = rom[D1 + lp]
    bits = sum(bin(rom[D1 + ((lp + 1 + k) & 0xFFFF)]).count('1') for k in range(0x20))
    n = 0x20 if stride & 0x80 else bits * stride + 1
    return 1 + 0x20 + max(n, 1)


def effect_tables(rom):
    out = []
    t = lambda name, a, n: out.append((name, a, a + n))
    # the effects (an 8-bit fx id): the two programs and palettes (D1:6240), the block rows (D1:A233)
    t('effect programs and palettes ($D1:6240, 4 x 256)', D1 + 0x6240, 4 * 256)
    t('effect block rows ($D1:A233, 6 x 256)', D1 + 0xA233, 6 * 256)
    t('effect palette records ($D1:6002, 8 x 256)', D1 + 0x6002, 8 * 256)
    # the background programs (CD/15DC: D1:646C[id * 2]) -- the table, the programs from the first to D1:A233
    ps, end = word_table(rom, D1 + 0x646C, D1)
    t('effect program pointers ($D1:646C)', D1 + 0x646C, 2 * 256)
    out.append(('effect programs ($D1:%04X-A233)' % min(ps), D1 + min(ps), D1 + 0xA233))
    # the tile-block layouts (CD/10AB: D1:A575[li * 2]), each by CD/10AB's own reading
    ls, end = word_table(rom, D1 + 0xA575, D1)
    t('effect layout pointers ($D1:A575)', D1 + 0xA575, end - (D1 + 0xA575))
    for lp in sorted(set(ls)):
        out.append(('effect layout D1/%04X' % lp, D1 + lp, D1 + lp + fx_layout_extent(rom, lp)))
    # the BG3 sheets (CD/11E8: CF:8000 + X * 64, 0x40 words) and their tiles (CF + ((w & 1FFF) << 3), 16 bytes)
    seen = set()
    for x in range(256):
        for n in range(0x40):
            wa = CF + ((((x << 6) + 0x8000) + 2 * n) & 0xFFFF)
            seen.update((wa, wa + 1))
            src = CF + (((_w(rom, wa) & 0x1FFF) << 3) & 0xFFFF)
            seen.update(range(src, src + 16))
    # the OBJ sheets (CD/1368: CF:A000 + X * 64, 32 words; CF:E000[X * 2]) and their pool tiles (D0, 16 or 24 bytes)
    for x in range(256):
        seen.update((CF + 0xE000 + 2 * x, CF + 0xE001 + 2 * x))
        for k in range(32):
            wa = CF + ((0xA000 + 64 * x + 2 * k) & 0xFFFF)
            seen.update((wa, wa + 1))
            src = D0 + (((_w(rom, wa) & 0x1FFF) << 3) & 0xFFFF)
            seen.update(range(src, src + 24))
    for a, b in spans(seen):
        out.append(('effect sheets $%02X:%04X' % (0xC0 + (a >> 16), a & 0xFFFF), a, b))
    t('BG3 tile attributes ($CF:E200, 8-bit)', CF + 0xE200, 256)
    t('OBJ sheet masks and VRAM ($CD:1300, $CD:1308)', CD + 0x1300, 8 + 12)
    # the HDMA presets (CE/EE6E: CE:F24E[idx * 2] -> nine words at CE:F58E.. -> 0x1E bytes each, MVN)
    t('HDMA preset bases ($CE:F24E, 8-bit)', CE + 0xF24E, 2 * 256)
    seen = set()
    for i in range(256):
        x2 = _w(rom, CE + 0xF24E + 2 * i)
        for k in list(range(0, 0x12, 2)) + [0x12]:
            a = CE + ((0xF58E + k + x2) & 0xFFFF)
            seen.update((a, a + 1))
            f = CE | _w(rom, a)
            seen.update(range(f, f + 0x1E))
    for a, b in spans(seen):
        out.append(('HDMA preset tables $%02X:%04X' % (0xC0 + (a >> 16), a & 0xFFFF), a, b))
    # CE's small effect tables
    t('fade records, modes ($CE:EFE0-F00E)', CE + 0xEFE0, 0xF00E - 0xEFE0)
    t('block bits and columns ($CE:F03E, 8; $CE:F046, 8-bit)', CE + 0xF03E, 8 + 256)
    t('fade records A/B ($CE:F0E6/F0EE, ($53 & F0) + 8)', CE + 0xF0E6, 0xF8 + 8)
    t('fade ticks and steps ($CE:F126, $CE:F146)', CE + 0xF126, 0x20 + 0x10)
    t('effect load sizes ($CE:F156, 8 words)', CE + 0xF156, 16)
    t('wobble steps ($CE:F1D2, 8-bit)', CE + 0xF1D2, 256)
    t('rim vectors ($CE:F28E, 2 x 256)', CE + 0xF28E, 512)
    t('magnitudes ($CE:F48E, 8-bit)', CE + 0xF48E, 256)
    t('status ramps ($CE:0100)', CE + 0x0100, 0x100)
    # CD's
    t('tilemap quadrant VRAM ($CD:05FF, $CA16 * 2)', CD + 0x05FF, 2 * 256)
    t('object slot records ($CD:2A42, k)', CD + 0x2A42, 256)
    t('object offsets ($CD:26B0, ix)', CD + 0x26B0, 2 * 256)
    t('window line bases ($CD:38AA, 4 words)', CD + 0x38AA, 8)
    t('dissolve wait ($CD:00C0)', CD + 0x00C0, 1)
    # D1's
    t('one-cell records ($D1:50A2, 6 x 16)', D1 + 0x50A2, 6 * 16)
    t('bracket pulse ($D1:5980, 62 words)', D1 + 0x5980, 2 * 0x3E)
    t('status icon sheet ($D1:3900, tiles 1A0-1FF)', D1 + 0x3900, 32 * 0x60)
    t('battle shadow and UI tiles ($D1:5A50-5FFF)', D1 + 0x5A50, 0x5FFF - 0x5A50)
    t('PC down poses ($D1:5F42, 4 x 8)', D1 + 0x5F42, 32)
    t('target strip records ($D1:5E10, $D1:5E49)', D1 + 0x5E10, 0x39 + 0x40 + 0x39)
    t('spin models ($D1:EC5F, 8-bit)', D1 + 0xEC5F, 256 + 3)
    t('shake tables ($D1:F69C-F6B3)', D1 + 0xF69C, 0x18)
    t('OBJ rows ($D1:E70A)', D1 + 0xE70A, 0x100)
    return out


def small_tables(rom):
    """CC:F38C-FC3F, the battle engine's small tables, each by its reader's index (src/battle.c ROM_* names)."""
    out = []
    t = lambda name, a, n: out.append((name, a, a + n))
    t('tech list bases and record offsets ($CC:F38C, $CC:F395)', CC + 0xF38C, 9 + 6)
    t('pose row layouts ($CC:F3B6, 14 x 6)', CC + 0xF3B6, 14 * 6)
    t('PC walk tables ($CC:F414-F693, 32 tracks)', CC + 0xF414, 0xF694 - 0xF414)
    t('arena offsets ($CC:F544, 8-bit)', CC + 0xF544, 256)
    t('status icon periods ($CC:F5FD, 8-bit)', CC + 0xF5FD, 256)
    t('bracket OAM ($CC:F604, 4 x 4)', CC + 0xF604, 16)
    t('weapon ranged ($CC:F63C, char)', CC + 0xF63C, 256)
    t('status icons ($CC:F6D4, 8-bit + 3 x 4)', CC + 0xF6D4, 256 + 16)
    t('status ramp bases ($CC:F754, word)', CC + 0xF754, 257)
    a = CC + 0xF77F
    while not rom[a] & 0x80:
        a += 4
    t('status scan ($CC:F77F, 4-byte records to bit 7)', CC + 0xF77F, a + 1 - (CC + 0xF77F))
    t('effect frame bases and grid ($CC:F7C0, $CC:F820)', CC + 0xF7C0, 0x60 + 0x10)
    t('OAM templates ($CC:F84D)', CC + 0xF84D, 2 * 0x17 + 2 * 44)
    t('battle setup ($CC:F903, 8-bit)', CC + 0xF903, 256)
    t('pose destinations and uploads ($CC:F941-F977)', CC + 0xF941, 0xF977 - 0xF941)
    t('OAM name / attribute pairs ($CC:FAF3)', CC + 0xFAF3, 0xB0)
    t('pose classes, squares, move classes, co-actors, dwells ($CC:FB6F-FCCD)', CC + 0xFB6F, 0xFBCE - 0xFB6F + 256)
    t('sheet offsets ($CC:FC1E, y)', CC + 0xFC1E, 2 * 256)
    t('enemy palette OBJ tail ($FF:9E0A, 3 words)', 0x3F9E0A, 6)
    return out


# ---- the battle's messages, its HUD's setup tables, the anim handlers' free/costing scan --------------------------

def battle_message(rom, a, seen):
    """A posted message ($020D-$020F's table, CtBattleMsgLen / CD/04DF): to its 00; 12 takes one operand byte; the
    dictionary codes 21-9F go to $DE:FA00 (menu_tables packs it)."""
    for _ in range(256):
        seen.add(a)
        v = rom[a]
        a += 1
        if v == 0:
            return
        if v == 0x12:
            seen.add(a)
            a += 1


def anim_free_scan(rom, h, seen):
    """battleAscOpFree's walk of the handler C1/h: up to 0x100 bytes (+4 read ahead) to an `INC $A00F`, an RTS / RTL
    or a JMP -- the advance tail ends it, any other JMP is a tail call it follows (four hops)."""
    hb = C1 + h
    for _ in range(4):
        jumped = False
        for i in range(0x100):
            seen.update(range(hb + i, hb + i + 5))
            b = rom[hb + i]
            if b == 0xEE and rom[hb + i + 1] == 0x0F and rom[hb + i + 2] == 0xA0:
                return
            if b == 0x4C:
                if rom[hb + i + 1] == 0xBB and rom[hb + i + 2] == 0x75:
                    return
                hb = C1 + _w(rom, hb + i + 1)
                jumped = True
                break
            if b in (0x60, 0x6B):
                return
        if not jumped:
            return


def misc_tables(rom):
    out = []
    t = lambda name, a, n: out.append((name, a, a + n))
    seen = set()
    # the messages the battle posts: the tech descriptions $CC:3A09, the results $CE:EF11, the enemy techs $CC:CBC9
    for tb in (CC + 0x3A09, CE + 0xEF11, CC + 0xCBC9):
        bank = tb & 0xFF0000
        for i in range(256):
            seen.update((tb + 2 * i, tb + 2 * i + 1))
            battle_message(rom, bank + _w(rom, tb + 2 * i), seen)
    for op in range(ANIM_OPS):
        anim_free_scan(rom, anim_handler(rom, op), seen)
    for a, b in spans(seen):
        out.append(('battle messages, handler scans $%02X:%04X' % (0xC0 + (a >> 16), a & 0xFFFF), a, b))
    t('message dwells ($CD:019D, 8)', CD + 0x019D, 8)
    t('message sounds ($D1:EE99, 8-bit)', D1 + 0xEE99, 256)
    t('tech names ($CC:15C4, 11 x 256)', CC + 0x15C4, 11 * 256)
    t('battle HUD setup ($CC:F837-FBE9: seats, rows, cursors, 8-bit indices)', CC + 0xF837, 0xFAE9 + 256 - 0xF837)
    t('the boot\'s palettes and UI tiles ($FF:9C68-9D70)', 0x3F9C68, 0x9D70 - 0x9C68)
    t('HUD chrome ($D1:5800, 0x180; $D1:59FC, 6 x 14)', D1 + 0x5800, 0x5A50 - 0x5800)
    t('boot palettes ($D1:38F2, $D1:38FA)', D1 + 0x38F2, 14)
    t('boot palette ($D1:4D80, 16)', D1 + 0x4D80, 16)
    a = E1 + 0xDE00
    while not _w(rom, a) & 0x8000:
        a += 4
    t('boss arenas ($E1:DE00, 4-byte records to bit 15, read whole)', E1 + 0xDE00, a + 4 - (E1 + 0xDE00))
    return out

def battle_tables(rom):
    """Every battle closure (assets/extract_assets.py rom_tables)."""
    return anim_tables(rom) + stage_tables(rom) + record_tables(rom) + effect_tables(rom) + small_tables(rom) + misc_tables(rom)


# ---- the battle's records and small tables (the reading code's own index ranges) -------------------------------------
FD, E1, C2 = 0x3D0000, 0x210000, 0x020000


def ai_section_end(rom, p):
    """C1/AFE6's walk of one section of an AI script ($CC + p): [conditions .. FE][actions .. FE] blocks until FF."""
    while rom[CC + p] != 0xFF:
        while rom[CC + p] != 0xFE:
            p += 1
        p += 1
        while rom[CC + p] != 0xFE:
            p += 1
        p += 1
    return p + 1


def record_tables(rom):
    out = []
    t = lambda name, a, n: out.append((name, a, a + n))
    # the enemies (an 8-bit id): stats, names, rewards, AI pointers and scripts (the turn's section, then the counter's)
    t('enemy stats ($CC:4700, 23 x 256)', CC + 0x4700, 23 * 256)
    t('enemy names ($CC:6500, 11 x 256)', CC + 0x6500, 11 * 256)
    t('enemy rewards ($CC:5E00, 7 x 256)', CC + 0x5E00, 7 * 256)
    t('enemy AI pointers ($CC:8B08)', CC + 0x8B08, 2 * 256)
    for i in range(256):
        p = _w(rom, CC + 0x8B08 + 2 * i)
        out.append(('enemy AI script %02X' % i, CC + p, CC + ai_section_end(rom, ai_section_end(rom, p)) + 4))
    t('enemy action records ($CC:88CA, 11 x 256)', CC + 0x88CA, 11 * 256)
    t('enemy attack records ($CC:89C6, 12 x 256)', CC + 0x89C6, 12 * 256)
    t('class-2 action records ($CC:6FCA, 11 x 256)', CC + 0x6FCA, 11 * 256)
    t('class-2 attack records ($CC:7AC9, 12 x 256)', CC + 0x7AC9, 12 * 256)
    t('enemy action groups and areas ($CC:86C9, act * 2)', CC + 0x86C9, 2 * 256 + 1)
    t('field enemy records ($E4:F600, 10 x 256)', 0x24F600, 10 * 256)
    t('enemy palettes ($E4:0000, 24 x 256)', 0x240000, 24 * 256)
    t('enemy keys ($E1:DE80, id)', E1 + 0xDE80, 256)
    # the PCs and their techs and items
    t('PC action selectors ($CC:2583, char)', CC + 0x2583, 256)
    t('PC action / tech records ($CC:1BEB, 11 x 256)', CC + 0x1BEB, 11 * 256)
    t('PC attack records ($CC:213F, 12 x 256)', CC + 0x213F, 12 * 256)
    t('tech groups ($CC:249F, 3 x 128)', CC + 0x249F, 3 * 128)
    t('tech MP ($CC:253B, & 7F)', CC + 0x253B, 128)
    t('tech targets and areas ($CC:1ACB, 2 x 75)', CC + 0x1ACB, 2 * 0x75)
    t('tech element nibbles ($CC:2BDC, $CC:2BEB, the tech id)', CC + 0x2BDC, 0x0F + 256)
    t('XP to next ($CC:2632, 100 levels)', CC + 0x2632, 2 * 100)
    t('HP / MP growth ($CC:258A, $CC:25C2, 8 x 8)', CC + 0x258A, 0x25C2 - 0x258A + 64)
    t('stat rates ($CC:25FA, 7 x 8)', CC + 0x25FA, 7 * 8)
    t('tech TP ($CC:26FA, 16 x 8)', CC + 0x26FA, 16 * 8)
    t('tech learn ranges ($CC:276A, (id << 1) & FF)', CC + 0x276A, 256)
    t('ATB reload ($CC:2E31, 8 x 16)', CC + 0x2E31, 8 * 16)
    t('item battle records ($CC:0ABC, 3 x 68)', CC + 0x0ABC, 3 * 0x44)
    t('item amounts ($CC:05CC, 4 x 68)', CC + 0x05CC, 4 * 0x44)
    t('item target groups ($CC:1BB5, 68)', CC + 0x1BB5, 0x44)
    t('equipment and the stats header ($CC:0000, the $2600 copy)', CC, 0x230)
    t('tech ranges ($C2:BD40, char + 1)', C2 + 0xBD40, 9)
    # the areas (C1/C586): $CC:2AB0[area * 2] -> [count][count x 3]
    t('area pointers ($CC:2AB0, 8-bit area)', CC + 0x2AB0, 2 * 256)
    for i in range(256):
        r = _w(rom, CC + 0x2AB0 + 2 * i)
        out.append(('area record %02X' % i, CC + r, CC + r + 1 + 3 * rom[CC + r]))
    # FD: the RNG, the AI's action lengths, the seats' blocks and command records, the singles, the row records
    t('battle RNG ($FD:BA61, 8-bit)', FD + 0xBA61, 256)
    t('AI action lengths ($FD:BA4A, the op)', FD + 0xBA4A, 256)
    t('combatant blocks ($FD:A80B, 11 seats)', FD + 0xA80B, 2 * 11)
    t('command records ($FD:A849, 11 seats)', FD + 0xA849, 2 * 11)
    t('tech singles ($FD:A89D, char + 1)', FD + 0xA89D, 9)
    t('seat row pointers ($FD:B957, $29AC * 2)', FD + 0xB957, 2 * 256)
    for i in range(256):
        p = _w(rom, FD + 0xB957 + 2 * i)
        out.append(('seat rows %02X' % i, FD + p, FD + p + 0x15))
    # C1: the AI's target resolvers, the timer flags
    t('AI target resolvers ($C1:B8BB, 24 words)', C1 + 0xB8BB, 48)
    t('timer flag bases ($C1:B947, 13 words)', C1 + 0xB947, 26)
    return out
