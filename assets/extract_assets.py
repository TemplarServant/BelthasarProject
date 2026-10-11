#!/usr/bin/env python3
"""Build ct_assets.dat — extract Chrono Trigger assets from the ROM into a single
container the C engine consumes (the Python->C bridge). Assets are ROM-derived
(copyrighted) so the .dat is git-ignored; supply your own ROM.

Container: 'CTAS', version u32, entry_count u32,
  directory[entry_count] = { type u32, id u32, offset u32, length u32 },
  then asset blobs (offset absolute from file start).

Asset types (all little-endian; enough to render any field scene WITHOUT the ROM):
  1 scene palette  : 128 x RGB
  2 sprite palette : 16 x RGB
  3 scene header   : 14 raw bytes
  4 pal anim       : per anim [typ,ci,cc,delay,fc, fc*frame, ncol u16, ncol*RGB], end typ=0
  5 tileset L12    : [768*64 chips][768 present][512 metas*16][nanim u16][nanim*64][dlen u16][chipanim]
  6 tileset L3     : [nchips u16][nchips*64][nchips present][has_metas u8][256 metas*16?]
  7 map            : [w1,h1,w2,h2,w3,h3,has_l3,screen,effect][L1/L2/L3 u16, +256 baked]
A meta corner is {chip u16, base u8, flags u8 (fx|fy<<1|prio<<2)}.

Usage: python assets/extract_assets.py <rom.sfc> [out=ct_assets.dat]
"""
import sys, os, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import maps
import sprites
import script
import text
from gfx import decode_tiles
from lz import lz_decompress
from maps import (SCENE_HDR, SCENE_TS, TS_PTRS, ASM_PTRS, ASM3_PTRS, MAP_PTRS,
                  scene_palette, _metatiles, parse_chip_anims, chip_anim_data,
                  read_palette_anims, parse_props)
from sprites import get_entries_3byte

T_SCENE_PAL, T_SPRITE_PAL, T_SCENE_HDR, T_PAL_ANIM, T_TS12, T_TS3, T_MAP = 1, 2, 3, 4, 5, 6, 7
T_STRINGS, T_DIALOGUE = 8, 9
# field assets (so a C field engine can run without the ROM):
T_ACTORS, T_COLLISION, T_EXITS, T_SPRITEANIM, T_FONT = 10, 11, 12, 13, 14
T_SPRITE = 15   # per sprite: graphics + assembly + palette (the C sprite compositor input)
T_SCRIPT = 16   # per script index: the decompressed scene-script bytecode (the C VM input)
T_WORLD = 17    # per world: overworld grids/defs/pal/char/props/triggers/entry records (M3)
T_WORLDSPR = 18 # the overworld player sprite atlas (captured live via ct.exe --owsprite)
T_WINDOW = 19   # the dialogue window bitmap lifted from the live oracle fb ([h u8] + 256*h*3 RGB)
T_MOVER = 20    # field mover tables: C0/F300 atan 32x32 (0x400) + C0/F700 facing (0x100) + C0/F800 sine (0x140)
T_PORTRAIT = 22    # per-char (0-6) 48x48 name-screen portrait: ROM gfx $FF:0000 + per-char palette
T_NAMESPRITES = 23 # name-screen cursor sprites: the grid HAND + the name-cursor TRIANGLE (16x16 RGBA)
T_MENUFONT = 24    # the 8x8 menu font (keyboard + names): raw 2bpp, tile=char-code, ROM $FF:8C60
T_NAMEWIN = 25     # name-screen window background (BG2 isolated): [w u16][h u16] + w*h*3 RGB (256x224)
T_NAMEMSG = 26     # "Enter a name..." prompt box (new-game Crono): [x0,y0,w,h u16] + w*h*3 RGB (224x48@16,104)
T_WORLDINTROSEED = 27  # id = world; the NEW-GAME INTRO object blocks (64 x 0x40 = 0x1000). The BASE
                   # CT_ASSET_WORLD seed is the INTERACTIVE snapshot (sparse gulls, boats, NO fair); this
                   # ships the intro cutscene's flock+fair spawner-master seed as an OVERRIDE that field.c
                   # overlays onto $0B30 ONLY on the --newgame intro entry (the 496-pan). Without it the
                   # intro shows no dense flock + no Leene's-Square fair; WITH the base swap, interactive
                   # overworld play stays sparse (no master pc=06CD).
# T_STRINGS ids: 0=dictionary, 1=item names, 2=world names, 3=location names
S_DICT, S_ITEMS, S_WORLD, S_LOCATIONS = 0, 1, 2, 3

def _strlist(strings):
    """Serialize a list of UTF-8 strings: [count u16][ {u16 len, bytes} ... ]."""
    b = bytearray(struct.pack('<H', len(strings)))
    for s in strings:
        u = s.encode('utf-8')[:0xFFFF]
        b += struct.pack('<H', len(u)) + u
    return bytes(b)

def _hdr(rom, s):
    return rom[SCENE_HDR + s * 14: SCENE_HDR + s * 14 + 14]

def _distinct(rom, byte_index, exclude=(0xFF,)):
    return sorted(set(_hdr(rom, s)[byte_index] for s in range(512)) - set(exclude))

def _ser_metas(metas, count):
    b = bytearray()
    for t in range(count):
        m = metas[t] if t < len(metas) else [(0, 0, False, False, False)] * 4
        for (chip, base, fx, fy, prio) in m:
            flags = (1 if fx else 0) | ((1 if fy else 0) << 1) | ((1 if prio else 0) << 2)
            b += struct.pack('<HBB', chip & 0xFFFF, base & 0xFF, flags)
    return bytes(b)

# -------- packers (each yields (type, id, blob)) --------

def pack_scene_palettes(rom, T):
    for pi in _distinct(rom, 3):
        yield (T_SCENE_PAL, pi, b''.join(bytes(c) for c in scene_palette(rom, pi)))

def pack_sprite_palettes(rom, T):
    _, _, pal_ptrs, _ = sprites.parse_tables(rom)
    for i in range(253):
        yield (T_SPRITE_PAL, i, b''.join(bytes(c) for c in sprites.read_sprite_palette(rom, pal_ptrs, i)))

def pack_scene_headers(rom, T):
    for s in range(512):
        yield (T_SCENE_HDR, s, bytes(_hdr(rom, s)))

def pack_pal_anims(rom, T):
    for pi in _distinct(rom, 3):
        d = bytearray()
        for typ, ci, cc, delay, frames, colors in read_palette_anims(rom, pi):
            d += bytes([typ & 0xFF, ci & 0xFF, cc & 0xFF, delay & 0xFF, len(frames) & 0xFF])
            for f in frames:
                d.append(f & 0xFF)
            d += struct.pack('<H', len(colors))
            for (r, g, b) in colors:
                d += bytes([r & 0xFF, g & 0xFF, b & 0xFF])
        d.append(0)
        yield (T_PAL_ANIM, pi, bytes(d))

def pack_tileset12(rom, T):
    ts_gfx = T['ts_gfx']; asm_ptrs = T['asm_ptrs']
    for ts in _distinct(rom, 1):
        if ts >= 256:
            continue
        chipsets = rom[SCENE_TS + ts * 8: SCENE_TS + ts * 8 + 8]
        chips = bytearray(768 * 64); present = bytearray(768)
        for slot in range(6):
            cs = chipsets[slot]
            if cs == 0xFF or cs >= len(ts_gfx):
                continue
            for j, t in enumerate(list(decode_tiles(lz_decompress(rom, ts_gfx[cs]), 4))[:128]):
                chips[(slot * 128 + j) * 64:(slot * 128 + j) * 64 + 64] = bytes(t)
                present[slot * 128 + j] = 1
        raw = _metatiles(lz_decompress(rom, asm_ptrs[ts % len(asm_ptrs)]), 512, 16, False)
        # rebase chip words to the scene chip space WITHOUT losing chip 0: word 0x0100 =
        # chips12[0] (a real tile -- the bedroom's dark drape column); words < 0x100 do not
        # address scene chips -> 0xFFFF sentinel (skipped by the compositor)
        metas = _ser_metas([[(c - 256 if c >= 256 else 0xFFFF, b, fx, fy, pr)
                             for (c, b, fx, fy, pr) in m] for m in raw], 512)
        anim = bytearray(); nanim = 0
        cs6 = chipsets[6]
        if cs6 != 0xFF and cs6 < len(ts_gfx):
            atiles = list(decode_tiles(lz_decompress(rom, ts_gfx[cs6]), 4))
            nanim = len(atiles)
            for t in atiles:
                anim += bytes(t)
        desc = bytearray()
        if cs6 != 0xFF and cs6 < len(ts_gfx):
            for dest, srcs, durs in parse_chip_anims(chip_anim_data(rom, ts)):
                desc.append(len(srcs) & 0xFF)
                desc += struct.pack('<H', dest & 0xFFFF)
                for d in durs:
                    desc.append(d & 0xFF)
                for sc in srcs:
                    desc += struct.pack('<H', sc & 0xFFFF)
        desc.append(0)
        blob = (bytes(chips) + bytes(present) + metas + struct.pack('<H', nanim)
                + bytes(anim) + struct.pack('<H', len(desc)) + bytes(desc))
        yield (T_TS12, ts, blob)

def pack_tileset3(rom, T):
    ts_gfx = T['ts_gfx']; asm3_ptrs = T['asm3_ptrs']
    for ts in _distinct(rom, 2):
        if ts >= len(ts_gfx) or ts_gfx[ts] == 0:
            continue
        tiles = list(decode_tiles(lz_decompress(rom, ts_gfx[ts]), 2))
        chips = bytearray(); present = bytearray(len(tiles))
        for j, t in enumerate(tiles):
            chips += bytes(t); present[j] = 1
        has_metas = 0; metab = b''
        if ts < len(asm3_ptrs) and asm3_ptrs[ts] != 0:
            raw3 = _metatiles(lz_decompress(rom, asm3_ptrs[ts]), 256, 4, False)
            metab = _ser_metas([[(c if c else 0xFFFF, b, fx, fy, pr)
                                 for (c, b, fx, fy, pr) in m] for m in raw3], 256)
            has_metas = 1
        blob = struct.pack('<H', len(tiles)) + bytes(chips) + bytes(present) + bytes([has_metas]) + metab
        yield (T_TS3, ts, blob)

def pack_maps(rom, T):
    map_ptrs = T['map_ptrs']
    seen = set()
    for s in range(512):
        h = _hdr(rom, s); mi = h[4] | (h[5] << 8)
        if mi in seen or mi >= len(map_ptrs):
            continue
        seen.add(mi)
        md = lz_decompress(rom, map_ptrs[mi])
        size, bits = md[0], md[1]
        w1 = ((size >> 0) & 3) * 16 + 16; h1 = ((size >> 2) & 3) * 16 + 16
        w2 = ((size >> 4) & 3) * 16 + 16; h2 = ((size >> 6) & 3) * 16 + 16
        w3 = ((bits >> 0) & 3) * 16 + 16; h3 = ((bits >> 2) & 3) * 16 + 16
        has_l3 = 1 if (bits & 0x80) else 0
        p = 6
        l1 = list(md[p:p + w1 * h1]); p += w1 * h1
        l2 = list(md[p:p + w2 * h2]); p += w2 * h2
        l3 = list(md[p:p + w3 * h3]) if has_l3 else []
        if has_l3:
            p += w3 * h3
        sw, sh = max(w1, w2), max(h1, h2)
        l1add, l2add = parse_props(md, p, sw * sh)
        for y in range(h1):
            for x in range(w1):
                if l1add[y * sw + x]:
                    l1[y * w1 + x] |= 256
        for y in range(h2):
            for x in range(w2):
                if l2add[y * sw + x]:
                    l2[y * w2 + x] |= 256
        # per-tile props PRIO plane (prop1/prop2 bit 0x40 = the tile's upper/lower half
        # chips assemble as BG priority-1 -- the bed-sheet that covers sleeping Crono)
        tprio = bytearray(sw * sh)
        i, cnt = p, 0
        while cnt < sw * sh and i + 2 < len(md):
            b0, b1, b2 = md[i], md[i + 1], md[i + 2]; i += 3
            rep = 1
            if b0 & 0x80:
                rep = md[i] if i < len(md) else 0; i += 1
                rep = rep or 256
            tp = (1 if (b1 & 0x40) else 0) | (2 if (b2 & 0x40) else 0)
            for _ in range(rep):
                if cnt >= sw * sh: break
                tprio[cnt] = tp; cnt += 1
        blob = bytes([w1, h1, w2, h2, w3, h3, has_l3, md[4], md[5]])
        blob += struct.pack('<%dH' % len(l1), *l1)
        blob += struct.pack('<%dH' % len(l2), *l2)
        if has_l3:
            blob += struct.pack('<%dH' % len(l3), *l3)
        blob += bytes(tprio)
        # The map header's LAYER-SCROLL bytes, appended AFTER tprio so an older reader (which sizes
        # tprio by the remaining length) is unaffected. The ROM's loader decodes them at C0/A385-A4FB
        # off the decompressed header at $7E:B500: md[1] bits $20/$10 -> $1D36 (BG3/BG2 detached from
        # the camera), md[2] -> BG2's own X/Y speed $1D37/$1D38, md[3] -> BG3's $1D39/$1D3A, each as a
        # 3-bit magnitude code (speed = 1 << (code-1)) plus a sign bit. They were never packed, so every
        # scrolling background -- the Blackbird's parallax clouds, Zeal's drifting starfield -- sat still.
        blob += bytes([md[1], md[2], md[3]])
        yield (T_MAP, mi, blob)

def pack_strings(rom, T):
    subs = text.load_dict(rom)
    yield (T_STRINGS, S_DICT, _strlist(subs))
    yield (T_STRINGS, S_ITEMS, _strlist(text.item_names(rom)))
    yield (T_STRINGS, S_WORLD, _strlist(text.world_names(rom, subs)))
    yield (T_STRINGS, S_LOCATIONS, _strlist(text.location_names(rom, subs)))

def pack_dialogue(rom, T):
    subs = text.load_dict(rom)
    # pb='\f' marks the CT page-break codes (0x0B/0x0C) so the field engine breaks pages
    # exactly where the game does (the 'wait for input + clear box' separator). namesub=True
    # emits the name-substitution markers ('\x01'+code) for the character-name control codes
    # 0x13-0x20 -- the field engine resolves them at render time from its name table (the
    # variables the ROM keeps at $7E:2C23), so dialogue tracks the names exactly like the game.
    # ind='\x02'*3 keeps the BREAK TOKEN'S OWN INDENT distinguishable from literal spaces.
    # The ROM's line-break codes 0x06/0x08/0x0A (and the page break 0x0C) are ONE token that
    # means "break, then indent three", and the typewriter C0/2126 spends exactly one iteration
    # on a control code -- that indent is never typed. Their bare siblings 0x05/0x07/0x09/0x0B
    # carry nothing, and the ROM's dialogue DOES follow a bare break with literal spaces in 16
    # places (measured across every dialogue table), so "the first three spaces after a break
    # are free" is not a rule the engine can rediscover from the flattened text -- it has to be
    # carried. '\x02' draws exactly like ' ' and costs no typewriter iteration.
    for addr, strs in sorted(text.scan_dialogue_tables(rom, subs, pb='\f', namesub=True,
                                                       ind='\x02' * 3).items()):
        yield (T_DIALOGUE, addr, _strlist(strs))

def pack_portraits(rom, T):
    """The 7 PC name-entry-screen portraits (48x48), straight from the ROM. The naming routine
    (C0/EC38) reads, per char-id: the base tile from $FF:CE70+id, and the 16-colour palette via
    the pointer at $FF:CE78+id*2. The portrait is 6x6 tiles laid row-major from the base tile;
    the 4bpp tile graphics live UNCOMPRESSED at $FF:0000 (tile N, 1-based, at (N-1)*32)."""
    B = 0x3F0000                    # bank $FF -> file 0x3F0000 (HiROM)
    def bgr(w):
        e = lambda v: (v << 3) | (v >> 2)   # full-range 5-bit->8-bit (SNES), max 255 not 248
        return (e(w & 31), e((w >> 5) & 31), e((w >> 10) & 31))
    def palof(c):
        p = rom[B + 0xCE78 + c * 2] | (rom[B + 0xCE78 + c * 2 + 1] << 8)
        return [bgr(rom[B + p + i * 2] | (rom[B + p + i * 2 + 1] << 8)) for i in range(16)]
    def tilepix(t, x, y):
        o = B + (t - 1) * 32; bit = 7 - x
        return ((rom[o + y * 2] >> bit) & 1) | (((rom[o + y * 2 + 1] >> bit) & 1) << 1) \
             | (((rom[o + 16 + y * 2] >> bit) & 1) << 2) | (((rom[o + 16 + y * 2 + 1] >> bit) & 1) << 3)
    for c in range(7):
        pal = palof(c); bt = rom[B + 0xCE70 + c]
        blob = bytearray([48, 48])
        for j in range(48):
            for i in range(48):
                t = bt + (j // 8) * 6 + (i // 8)
                blob += bytes(pal[tilepix(t, i % 8, j % 8)])
        yield (T_PORTRAIT, c, bytes(blob))

def pack_menufont(rom, T):
    """The 8x8 menu font used by the name-entry keyboard + the entered name (the game writes the
    char code straight as the tilemap tile -- C2/E545 `LDA $FFC9AC,X`). Raw 2bpp, 256 tiles x 16
    bytes, tile = char code, from ROM $FF:8C60. Rendered with the menu text palette (CGRAM row 0):
    0=transparent, 1=shadow (24,32,40), 2=AA (136,144,144), 3=white (248,248,248)."""
    yield (T_MENUFONT, 0, bytes(rom[0x3F8C60:0x3F8C60 + 256 * 16]))

def pack_namesprites(rom, T):
    """The name-entry-screen grid HAND pointer, straight from the ROM (OBJ tiles, 4bpp): 16x16 at
    $FF:9CF0, stored as 4 sequential tiles (TL,TR,BL,BR). OBJ palette 0 is at $FF:9DF0 (colour
    0 = transparent). Blob: [w u8][h u8] + w*h*4 RGBA. (The name-cursor arrows are tiny 7px
    triangles drawn procedurally by the engine -- see field.c nsArrow.)"""
    B = 0x3F0000
    def bgr(w):
        e = lambda v: (v << 3) | (v >> 2)   # full-range 5-bit->8-bit (SNES), max 255 not 248
        return (e(w & 31), e((w >> 5) & 31), e((w >> 10) & 31))
    pal = []
    for i in range(16):
        w = rom[B + 0x9DF0 + i * 2] | (rom[B + 0x9DF0 + i * 2 + 1] << 8)
        pal.append(bgr(w) + (0 if i == 0 else 255,))   # index 0 = transparent
    def tilepix(off, x, y):
        bit = 7 - x
        return ((rom[off + y * 2] >> bit) & 1) | (((rom[off + y * 2 + 1] >> bit) & 1) << 1) \
             | (((rom[off + 16 + y * 2] >> bit) & 1) << 2) | (((rom[off + 16 + y * 2 + 1] >> bit) & 1) << 3)
    def sprite16(gfx):
        out = bytearray([16, 16])
        for j in range(16):
            for i in range(16):
                sub = (j // 8) * 2 + (i // 8)       # 0=TL 1=TR 2=BL 3=BR (sequential tiles)
                out += bytes(pal[tilepix(B + gfx + sub * 32, i % 8, j % 8)])
        return out
    def spritecrop(gfx, cx, cy, w, h):             # a tight crop of the 16x16 sprite tile
        out = bytearray([w, h])
        for j in range(h):
            for i in range(w):
                X, Y = cx + i, cy + j
                sub = (Y // 8) * 2 + (X // 8)
                out += bytes(pal[tilepix(B + gfx + sub * 32, X % 8, Y % 8)])
        return out
    yield (T_NAMESPRITES, 0, bytes(sprite16(0x9CF0)))                 # the grid hand pointer (16x16)
    # the name-cursor TRIANGLE (ROM $FF:9D70), an 11x12 3-tone beveled ▲ (outline/gray-bevel/white),
    # cropped to its opaque bounds. The engine blits it as-is for ▲ (below the name) and V-flipped
    # for ▼ (above) -- the real OBJ uses one tile + the OAM vertical-flip bit.
    yield (T_NAMESPRITES, 1, bytes(spritecrop(0x9D70, 2, 1, 11, 12)))

def pack_actors(rom, T):
    """Per-scene resolved actor placement (init interpreted). Blob: [count u8] then per
    visible actor {obj u8, sprite u16, x u16, y u16, facing u8} (9 bytes)."""
    for s in range(512):
        try:
            actors, _ = script.extract_actors(rom, s)
        except Exception:
            actors = []
        vis = [a for a in actors if a['visible'] and a['sprite'] is not None and a['pos'] is not None]
        b = bytearray([min(len(vis), 255)])
        for a in vis[:255]:
            b += struct.pack('<BHHHB', a['obj'] & 0xFF, a['sprite'] & 0xFFFF,
                             int(a['pos'][0]) & 0xFFFF, int(a['pos'][1]) & 0xFFFF, a['facing'] & 0xFF)
        yield (T_ACTORS, s, bytes(b))

def pack_collision(rom, T):
    """Per-map collision. Blob: [w u8][h u8] then w*h shape bytes ((prop0>>2)&0x1F), then w*h z
    bytes (zplane in bits0-1, IGNORE_Z/INV/Z_NEUTRAL/DOOR in bits2-5, bit 0x40 = prop2&0x40 =
    BOTTOM sprite-half OAM priority 3 ($6F/$0C01), bit 0x80 = prop1&0x40 = TOP sprite-half OAM
    priority 3 ($6E/$0C00) = drawn in front of the foreground), then w*h MOVER bytes
    (bit0 = prop2&0x80 = $7E7040,X bit7, the cat/object-mover solid flag tested by C0/ACFD
    `LDA $7E7040,X / BMI`; distinct from the player path's $7E7080 shape-expanded plane.
    bit1 = prop2&0x10, the HIGH bit of the player slope SOLID-plane z (b2 bits3-4, read by the
    C0/9B62 solid tail) -- the [z] plane could not hold it, see maps.scene_props).
    Keyed by map. Old readers that only know the [shape][z] planes still work: the mover plane
    is appended, so length = 2 + 3*w*h (was 2 + 2*w*h)."""
    seen = set()
    for s in range(512):
        h = _hdr(rom, s); mi = h[4] | (h[5] << 8)
        if mi in seen:
            continue
        seen.add(mi)
        try:
            w, hh, coll, zp, zf, mv = maps.scene_props(rom, s)
        except Exception:
            continue
        b = bytearray([w & 0xFF, hh & 0xFF])
        b += bytes(c & 0xFF for c in coll)
        b += bytes(((zp[i] & 3) | ((zf[i] & 0x3F) << 2)) & 0xFF for i in range(len(zp)))
        b += bytes(mv[i] & 0xFF for i in range(len(mv)))    # MOVER plane: bit0=MOVER-SOLID(prop2 bit7), bit1=prop2 bit4 (solid-z hi)
        yield (T_COLLISION, mi, bytes(b))

def pack_exits(rom, T):
    """Per-scene exits (doors). Blob: [count u8] then per exit {x,y,w,h u16, kind u8,
    dest u16, dx,dy i16, facing u8} (16 bytes); kind 1=scene, 0=overworld."""
    for s in range(512):
        try:
            exits = maps.scene_exits(rom, s)
        except Exception:
            exits = []
        b = bytearray([min(len(exits), 255)])
        for e in exits[:255]:
            b += struct.pack('<HHHHBHhhB', e['pos'][0], e['pos'][1], e['size'][0], e['size'][1],
                             1 if e['kind'] == 'scene' else 0, e['dest'] & 0xFFFF,
                             e['dpos'][0], e['dpos'][1], e['facing'] & 0xFF)
        yield (T_EXITS, s, bytes(b))

def pack_sprite_anims(rom, T):
    """Per-sprite FULL animation set (v2). Blob: [nAnims u8] then per anim: [n u8] + n x
    {U,D,L,R assembly idx u8, delay u8} (5 bytes). Anim 0 = idle, 1 = walk, 2+ = the
    scripted animations (event ops 0xAA/AB/AC/B7...). ENEMY sprites (op-0x83, id 256+)
    animate through the SAME ROM anim tables via their $E4/F600 record's anim-set byte
    (+3, sprites._anim_set_index handles 256+) -- pack the USED ones too: GATO's idle bob
    is anim 1, and the battle staging's $0F01 'prio' byte is its live assembly frame."""
    ids = list(range(256)) + sorted(s for s in T['used_sprites'] if s >= 256)
    for si in ids:
        try:
            fp, dp = sprites._sprite_anim_tables(rom)
            ai = sprites._anim_set_index(rom, si)
            base = fp[ai] if ai < len(fp) else 0
            nxt = min([a for a in fp if a > base] + [0x24A800]) if base else 0
            dpf = (nxt - base) // 4 if base else 0
            nanims = max(1, min(dpf // 4, 64))
        except Exception:
            nanims = 1
        b = bytearray([nanims])
        for action in range(nanims):
            try:
                frames = sprites.build_anim(rom, si, action)
            except Exception:
                frames = []
            b.append(min(len(frames), 255))
            for sf, delay in frames[:255]:
                b += bytes([sf[0] & 0xFF, sf[1] & 0xFF, sf[2] & 0xFF, sf[3] & 0xFF, delay & 0xFF])
        yield (T_SPRITEANIM, si, bytes(b))

def pack_mover(rom, T):
    yield (T_MOVER, 0, rom[0xF300:0xF700] + rom[0xF700:0xF800] + rom[0xF800:0xF940])

def pack_font(rom, T):
    """Dialogue VWF font v2, chars 0xA0-0xF3 (84 glyphs), 12x12 px 2bpp. The ROM stores the
    LEFT 8px as 24-byte cells (0x3F2060 + ch*24) and the RIGHT 4px nibble-packed per char
    PAIR (0x3F3860 + (ch>>1)*24; even char = high nibble). Advance widths at 0x0260E6.
    Blob: [84 width bytes] + per glyph 12 rows x {plane0 u16 LE, plane1 u16 LE}, where bits
    11..0 = pixels left..right (RE'd from the C2/5DC4 renderer, verified bit-exact)."""
    b = bytearray(rom[0x0260E6:0x0260E6 + 84])
    for gi in range(84):
        ch = 0xA0 + gi
        for row in range(12):
            for plane in range(2):
                k = row * 2 + plane
                main = rom[0x3F2060 + ch * 24 + k]
                ext = rom[0x3F3860 + (ch >> 1) * 24 + k]
                nib = (ext >> 4) if (ch & 1) == 0 else (ext & 0x0F)
                row12 = (main << 4) | nib
                b += bytes([row12 & 0xFF, row12 >> 8])
    yield (T_FONT, 0, bytes(b))

def pack_sprites(rom, T):
    """Per used sprite: the graphics + assembly + palette the C sprite compositor needs.
    Blob: [size u8][nframes u8][ntiles u16] palette(16*RGB) tiles(ntiles*64 decoded indices)
    then per frame: units*4 chip u16 + units*2 offset bytes (units from size). nframes/ntiles
    are bounded to what the idle+walk animations reference."""
    _, gfx_ptrs, pal_ptrs, asm_ptrs = sprites.parse_tables(rom)
    for si in sorted(T['used_sprites']):
        try:
            h = sprites.read_header(rom, si)
            tiles = list(sprites.decode_tiles(sprites.read_sprite_graphics(rom, gfx_ptrs, h['bitmap']), 4))
            pal = sprites.read_sprite_palette(rom, pal_ptrs, h['palette'])
            mf = 1
            try:                                     # bound by EVERY animation, not just idle/walk
                fp, dp = sprites._sprite_anim_tables(rom)
                ai = sprites._anim_set_index(rom, si)
                base = fp[ai] if ai < len(fp) else 0
                nxt = min([x for x in fp if x > base] + [0x24A800]) if base else 0
                nanims = max(1, min(((nxt - base) // 4) // 4 if base else 1, 64))
            except Exception:
                nanims = 2
            for act in range(nanims):
                for sf, _d in sprites.build_anim(rom, si, act):
                    mf = max(mf, max(sf) + 1)
            mf = max(mf, T.get('static_max', {}).get(si, 0) + 1)  # 0xAC static shapes
            frames, maxsrc = [], 0
            for f in range(mf):
                try:
                    chips, offs, units = sprites.assembly_frame(rom, asm_ptrs, h, f)
                except Exception:
                    break                            # past the sprite's real assembly: stop
                frames.append((chips, offs, units))
                for c in chips:
                    maxsrc = max(maxsrc, c & 0x3FF)
            if len(frames) > 255: frames = frames[:255]   # the blob header is u8
            mf = len(frames)
            ntiles = min(len(tiles), maxsrc + 1)
            blob = bytearray([h['size'] & 3, mf & 0xFF]) + struct.pack('<H', ntiles)
            for r, g, b in pal:
                blob += bytes([r, g, b])
            for t in tiles[:ntiles]:
                blob += bytes(t)
            for chips, offs, units in frames:
                for c in chips:
                    blob += struct.pack('<H', c)
                for ox, oy in offs:
                    blob += bytes([ox & 0xFF, oy & 0xFF])
            yield (T_SPRITE, si, bytes(blob))
        except Exception:
            continue

def pack_scripts(rom, T):
    """Per scene-script index: the decompressed bytecode ([count u8][count*16 u16 ptrs]
    [body]) — the input to the C scene-script VM (src/ct_vm.c). Deduped by script index."""
    sptrs = sprites.get_entries_3byte(rom, script.SCRIPT_PTRS, 513)
    seen = set()
    for s in range(512):
        si = _hdr(rom, s)[8] | (_hdr(rom, s)[9] << 8)
        if si in seen or si >= len(sptrs) or sptrs[si] == 0:
            continue
        seen.add(si)
        try:
            raw = lz_decompress(rom, sptrs[si])
        except Exception:
            continue
        yield (T_SCRIPT, si, bytes(raw))

def pack_world(rom, T):
    """The OVERWORLDS (M3): grids/defs/palette/char/props/trigger map/entry records + spawn,
    one asset per world 0-7. The displayed maps are ASSEMBLED AT RUNTIME (script VM + MVN
    fills), so this packs live engine states captured headlessly: ct.exe --navworld
    saves/ow_w0.state (walks the oracle onto 1000 AD) then --peek ... saves/ow_w0.live (dumps
    .wram/.vram); worlds 1-7 via ct.exe --owforce saves/ow_w0.state <n> saves/ow_w<n>.live
    (re-runs the C2 loader with $0100=0x1F0+n). Missing worlds are skipped with a warning.

    WORLD-0 SEED (the new-game intro pan): saves/ow_w0.live.wram MUST contain the GULL SPAWNER
    (ambient $0B30 script pc=06CD) + its fair-balloon evolution -- WITHOUT it the pan shows no
    gulls and no fair balloons. A plain --navworld capture lands at a moment where the gull tree
    has retired (only the pc=0E81 stub remains). Capture from the intro state instead:
      ct.exe --peek start_scene.state s.bin --btnscript "0:1"  &&  cp s.bin.wram saves/ow_w0.live.wram
    (the field.c +0F-based gull/balloon draw classes assume this real tree, which sets +0F=0xB0|pal*2)."""
    for n in range(8):
        wp, vp = 'saves/ow_w%d.live.wram' % n, 'saves/ow_w%d.live.vram' % n
        if not (os.path.exists(wp) and os.path.exists(vp)):
            print('  [pack_world] world %d dumps missing (%s) -- skipping '
                  '(--navworld / --owforce + --peek to produce)' % (n, wp))
            continue
        w = open(wp, 'rb').read()
        v = open(vp, 'rb').read()
        # BASE object-block seed: for world 0 the BASE asset uses the INTERACTIVE snapshot
        # (saves/ow_w0.live.wram.bak -- a real --navworld capture: NO flock+fair spawner master at
        # $0B30, gulls thin to 0, the lead PC + interactive loader present), so interactive overworld
        # play is sparse. The DENSE flock + the Leene's-Square fair belong ONLY to the new-game intro
        # cutscene (loc 496 pan, loader 7F:046F spawns the master pc=06CD); they ship as a separate
        # T_WORLDINTROSEED override that field.c overlays on $0B30 on the intro entry only. The map
        # data (grids/defs/palette/props/triggers/records/script blob) is byte-identical between the
        # two seeds -- ONLY the object instances differ -- so everything below still reads from `w`.
        objw = w
        bakp = 'saves/ow_w%d.live.wram.bak' % n
        if n == 0 and os.path.exists(bakp):
            objw = open(bakp, 'rb').read()
        # the player object = the block whose handler word is C2/3404 ($4E is only the
        # object-loop cursor); blocks are 0x40-strided at $0B30-$1B30 and relocate per world
        lead = 0
        for b in range(0x0B30, 0x1B30, 0x40):
            if w[b] | (w[b + 1] << 8) == 0x3404:
                lead = b
                break
        posx = w[lead + 0x14] | (w[lead + 0x15] << 8) if lead else 0
        posy = w[lead + 0x18] | (w[lead + 0x19] << 8) if lead else 0
        if not (0 < posx < 1536 and 0 < posy < 1024):
            posx, posy = 768, 512                    # forced loads can leave a blank lead; mid-map
        nrec = w[0x1B38]
        blob = struct.pack('<BBHH', 5, n, posx, posy)
        blob += w[0x4000:0x4000 + 6144] + w[0x5800:0x5800 + 6144]    # BG1/BG2 96x64 block grids
        blob += w[0x3000:0x3000 + 2048] + w[0x3800:0x3800 + 2048]    # BG1/BG2 block defs (256 x 4 chips)
        blob += w[0x18C00:0x18C00 + 512]                             # palette ($7F:8C00, 256 BGR15)
        blob += w[0x7000:0x7000 + 512]                               # per-block property nibbles
        blob += w[0x7200:0x7200 + 3072]                              # region trigger map
        blob += w[0x1B9B:0x1B9B + 16]                                # nibble -> name id table
        blob += struct.pack('<B', nrec) + w[0x7E00:0x7E00 + nrec * 7]  # location entry records
        blob += v[0x4000:0x4000 + 28672]                             # 4bpp char, VRAM tiles 512-1407
        # fmt 2: the ambient-object VM inputs -- the object blocks + the script blob. The object
        # blocks come from `objw` (the INTERACTIVE .bak for world 0); the script blob is identical
        # in both seeds, so it still reads from `w`.
        blob += objw[0x0B30:0x1B30]                                  # 64 x 0x40 object blocks (BASE seed)
        blob += w[0x10400:0x11A00]                                   # script blob ($7F:0400+, 5.5KB)
        # fmt 3: the bird-spawner ROM tables -- the RNG byte table (C0:FE00, indexed by the
        # free-running $1B30) and the 16-entry 16.16 launch-velocity table (C6:F640)
        blob += rom[0xFE00:0xFF00]
        blob += rom[0x06F640:0x06F640 + 128]
        # fmt 4: the BG3 layer (ocean texture + priority CLOUDS): 2bpp char @byte 0xE000 + 64x32
        # tilemap @byte 0xF000. NOTE: an RE pass re-addressed this to v[0xA000:0xF000] (char 0xB000)
        # claiming the old grab read a stale staging map -- but in the ACTUAL render that re-address
        # GARBLED the clouds into text-like fragments. Reverted to v[0xE000:0xF800], which renders
        # the cloud field correctly (the field.c worldBg3Px addressing matches). The 64x32 map is TWO
        # 32-wide screens: screen0 @0xF000-0xF800, screen1 @0xF800-0x10000. Shipping only :0xF800 (screen0)
        # dropped screen1 -> every cloud crossing col 31 (the screen boundary) was CLIPPED, the screen-1
        # half rendered black. Ship the whole char+tilemap block (0xE000:0x10000) so both screens load.
        blob += v[0xE000:0x10000]                                    # char (0xE000-0xF000) + FULL 64x32 tilemap
        # fmt 5: the COLOUR-MATH shadows $0045-$0049. The overworld NMI pushes them to the PPU every
        # frame (C2/0296: LDA $46 / STA $2131, then $47|80 / $49|40 / $48|20 -> $2132 = fixed B/G/R),
        # and each world's own LOAD SCRIPT sets them with VM op 0x13 ("abs := imm", C2/12B8) in its
        # first two instructions at $7F:0405 -- world 0 `13 46 00 44` = HALF-add on BG3, world 1
        # `13 46 00 04` = FULL add on BG3, world 2 `13 46 00 3F`. That script has already retired by
        # the time these snapshots are captured, so the bytes have to travel WITH the snapshot like
        # the palette does; without them the port kept world 0's 0x44 everywhere and 600 A.D.'s BG3
        # mist/ocean composited half-bright ("the 600ad smoke looks strange, dark").
        blob += w[0x0045:0x004A]                                     # CGWSEL, CGADSUB, fixed B/R/G
        yield (T_WORLD, n, bytes(blob))
        # NEW-GAME INTRO override: ship the intro cutscene's object blocks (with the flock+fair
        # spawner master pc=06CD) as T_WORLDINTROSEED so field.c can overlay them on $0B30 for the
        # 496-pan ONLY. Prefer the explicitly-preserved intro capture (saves/ow_w%d.intro.wram);
        # else fall back to the .live.wram (which is the intro capture per the WORLD-0 SEED note above).
        introp = 'saves/ow_w%d.intro.wram' % n
        iw = open(introp, 'rb').read() if os.path.exists(introp) else w
        intro_blocks = iw[0x0B30:0x1B30]
        if len(intro_blocks) == 0x1000:
            yield (T_WORLDINTROSEED, n, bytes(intro_blocks))
    sp = 'saves/ow_crono.spr'                        # the player sprite atlas (ct.exe --owsprite)
    if os.path.exists(sp):
        yield (T_WORLDSPR, 0, open(sp, 'rb').read())
    else:
        print('  [pack_world] %s missing -- run ct.exe --owsprite; the viewers fall back to a marker' % sp)
    gp = 'saves/ow_gulls.spr'                        # the bird frames (filtered from ct.exe --owbirds)
    if os.path.exists(gp):
        yield (T_WORLDSPR, 1, open(gp, 'rb').read())
    btp = 'saves/ow_boat.spr'                         # the coastal boat bob frames (ct.exe --owboat)
    if os.path.exists(btp):
        yield (T_WORLDSPR, 2, open(btp, 'rb').read())
    bln = 'saves/ow_balloons.spr'                     # the Leene fair balloon frames (ct.exe --owballoons)
    if os.path.exists(bln):
        yield (T_WORLDSPR, 3, open(bln, 'rb').read())
    wp = 'saves/ct_window.bin'                       # the dialogue window bitmap (fb-lifted)
    if os.path.exists(wp):
        yield (T_WINDOW, 0, open(wp, 'rb').read())
    nw = 'saves/ct_namewin.bin'                       # the name-screen window background = BG2 rendered
    if os.path.exists(nw):                            #   in ISOLATION by the oracle (PEEK_ISOBG=1): the
        yield (T_NAMEWIN, 0, open(nw, 'rb').read())   #   real weave + HDMA gradient, byte-exact, no content
    nm = 'saves/ct_namemsg.bin'                       # the "Enter a name..." prompt box (new-game Crono);
    if os.path.exists(nm):                            #   byte-exact crop from the oracle (tools/gen_namemsg.py)
        yield (T_NAMEMSG, 0, open(nm, 'rb').read())

PACKERS = [pack_scene_palettes, pack_sprite_palettes, pack_scene_headers,
           pack_pal_anims, pack_tileset12, pack_tileset3, pack_maps,
           pack_strings, pack_dialogue, pack_portraits, pack_namesprites, pack_menufont,
           pack_actors, pack_collision, pack_exits, pack_sprite_anims, pack_font, pack_mover,
           pack_sprites, pack_scripts, pack_world]

def build(rom):
    T = {
        'map_ptrs': get_entries_3byte(rom, MAP_PTRS, 202),
        'ts_gfx': get_entries_3byte(rom, TS_PTRS, 204),
        'asm_ptrs': get_entries_3byte(rom, ASM_PTRS, 64),
        'asm3_ptrs': get_entries_3byte(rom, ASM3_PTRS, 19),
    }
    used = set()                                     # sprites referenced by placed actors
    static_max = {}                                  # sprite -> max 0xAC static assembly frame
    for s in range(512):
        try:
            actors, _ = script.extract_actors(rom, s)
        except Exception:
            continue
        scene_sprites = set()
        for a in actors:
            if a['visible'] and a['sprite'] is not None and a['pos'] is not None:
                used.add(a['sprite']); scene_sprites.add(a['sprite'])
        # sweep the whole scene body for 0xAC (static shape) operands; attribute to all of
        # the scene's sprites (coarse but safe -- a few extra packed frames at most).
        # EVERY scene is swept, also one with no visible init actor: the staff roll's 05D loads all
        # ten of its NPCs hidden ('82 B3 / 8B 05 06 / 91', obj1's '7C 0C' shows them later), and an
        # early-out on "no visible actor" left sprite 186 (B3 + 7, used by no other scene) out of the
        # dat -- the port drew it blank (field_caption_plane.md sec. 9).
        try:
            hdrb = rom[0x360000 + s * 14: 0x360000 + s * 14 + 14]
            _objs, body = script.load_script(rom, hdrb[8] | (hdrb[9] << 8))
            o = 0
            while o < len(body) - 1:
                ln = script.op_len(body, o)
                if ln <= 0: break
                op = body[o]
                if op == 0xAC:
                    for sp in scene_sprites:
                        static_max[sp] = max(static_max.get(sp, 0), body[o + 1])
                # script-loaded sprites (op 0x80-0x83): an object can LOAD a sprite + stay HIDDEN at
                # init (Marle's PENDANT obj11 = '82 63' -> sprite 106, '91' HIDE), then a later REQ
                # SHOWs it. Those are not "visible init actors" so used_sprites missed them -> their
                # graphics were absent from the dat and they rendered blank. Capture them here.
                elif op == 0x80 or op == 0x81: used.add(body[o + 1]); scene_sprites.add(body[o + 1])
                elif op == 0x82: used.add(body[o + 1] + 7); scene_sprites.add(body[o + 1] + 7)
                elif op == 0x83: used.add(body[o + 1] + 256); scene_sprites.add(body[o + 1] + 256)
                o += ln
        except Exception:
            pass
    T['used_sprites'] = used
    T['static_max'] = static_max
    entries = []
    for packer in PACKERS:
        entries.extend(packer(rom, T))
    n = len(entries)
    blob_start = 12 + n * 16
    directory = bytearray(); blobs = bytearray(); off = blob_start
    for (t, id_, blob) in entries:
        directory += struct.pack('<IIII', t, id_, off, len(blob))
        blobs += blob
        off += len(blob)
    return b'CTAS' + struct.pack('<II', 3, n) + bytes(directory) + bytes(blobs)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    rom = open(sys.argv[1], 'rb').read()
    out = sys.argv[2] if len(sys.argv) > 2 else 'ct_assets.dat'
    data = build(rom)
    open(out, 'wb').write(data)
    n = struct.unpack('<I', data[8:12])[0]
    by = collections.Counter(struct.unpack('<I', data[12 + i * 16:16 + i * 16])[0] for i in range(n))
    names = {1: 'scene_pal', 2: 'sprite_pal', 3: 'headers', 4: 'pal_anim', 5: 'tileset12',
             6: 'tileset3', 7: 'maps', 8: 'strings', 9: 'dialogue'}
    summary = ', '.join('%s=%d' % (names.get(t, t), c) for t, c in sorted(by.items()))
    print("wrote %s: %d entries, %.1f MB (%s)" % (out, n, len(data) / 1e6, summary))
