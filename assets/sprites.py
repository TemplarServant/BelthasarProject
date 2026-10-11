#!/usr/bin/env python3
"""Pair Chrono Trigger NPC/PC sprite graphics with their palettes via the ROM
tables (reverse-engineered as in CTViewer's backend_snes.rs), so any sprite pack
can be colorized with its real palette.

Tables (SNES/USA):
  - 7-pointer table @ 0x24FFE0 (2-byte local pointers in bank 0x240000):
      [0] -> sprite graphics pointer table (248 entries, 3-byte SNES pointers, LZ)
      [4] -> sprite palette pointer table (253 entries, 2-byte local pointers)
  - sprite header @ 0x24F000 + index*5 = [bitmap, assembly, palette, anim, size]
  - palette = 24 bytes (12 BGR15 colors) at pal_ptrs[palette_index]
  - graphics = LZ block at gfx_ptrs[bitmap_index], decoded as 4bpp tiles

CLI:
  python assets/sprites.py <rom.sfc> info
  python assets/sprites.py <rom.sfc> frame <sprite_index> <out.png>   # assembled sprite
  python assets/sprites.py <rom.sfc> chars <out.png>                  # the 7 PCs
  python assets/sprites.py <rom.sfc> render <sprite_index> <out.png>  # raw chip sheet
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lz import lz_decompress
from palettes import snes15_to_rgb, write_png_rgb
from gfx import decode_tiles, render_rgb

HEADER_BASE = 0x24F000
PTR_TABLE   = 0x24FFE0

def u16(d, o): return d[o] | (d[o + 1] << 8)

def get_entries_3byte(rom, table_addr, count):
    """3-byte SNES pointers -> file offsets: 0x10000*(bank-0xC0)+ptr."""
    out = []
    for i in range(count):
        o = table_addr + i * 3
        ptr = rom[o] | (rom[o + 1] << 8)
        bank = rom[o + 2]
        out.append(0 if (bank == 0 and ptr == 0) else 0x10000 * (bank - 0xC0) + ptr)
    return out

def get_entries_2byte(rom, table_addr, count):
    """2-byte local pointers -> offsets within the table's bank."""
    bank_start = table_addr & 0xFF0000
    return [bank_start + u16(rom, table_addr + i * 2) for i in range(count)]

def parse_tables(rom):
    ptrs = get_entries_2byte(rom, PTR_TABLE, 7)
    gfx_ptrs = get_entries_3byte(rom, ptrs[0], 248)   # [0] graphics
    asm_ptrs = get_entries_3byte(rom, ptrs[1], 231)   # [1] assemblies
    pal_ptrs = get_entries_2byte(rom, ptrs[4], 253)   # [4] palettes
    return ptrs, gfx_ptrs, pal_ptrs, asm_ptrs

# size_flags & 3 -> (groups_per_frame, tiles_per_group); each "tile" is a 16x16
# unit of 4 chips, placed by a signed (ox,oy) offset.
SIZE_TABLE = {0: (1, 4), 1: (1, 8), 2: (3, 4), 3: (3, 8)}
CHIP_LOCAL = {0: (0, 0), 1: (8, 0), 2: (0, 8), 3: (8, 8)}  # TL, TR, BL, BR

def assembly_frame(rom, asm_ptrs, h, frame):
    """Parse one sprite assembly frame -> (chips[units*4] u16, offs[units] (ox,oy signed),
    units). chips are in flat unit-major order (each unit = TL,TR,BL,BR); a chip's low 10
    bits = source tile, bit 0x4000 = flip-x, bit 0x8000 = flip-y."""
    groups, tpg = SIZE_TABLE[h['size'] & 3]
    units = groups * tpg
    pos = asm_ptrs[h['assembly']] + frame * units * 10
    chips = [0] * (units * 4)
    for g in range(groups):
        gs = g * tpg * 4
        for t in range(tpg):                       # upper row of each unit (TL, TR)
            chips[gs + t * 4 + 0] = u16(rom, pos); pos += 2
            chips[gs + t * 4 + 1] = u16(rom, pos); pos += 2
        for t in range(tpg):                       # bottom row of each unit (BL, BR)
            chips[gs + t * 4 + 2] = u16(rom, pos); pos += 2
            chips[gs + t * 4 + 3] = u16(rom, pos); pos += 2
    offs = []
    for _ in range(units):
        ox, oy = rom[pos], rom[pos + 1]; pos += 2
        offs.append((ox - 256 if ox >= 128 else ox, oy - 256 if oy >= 128 else oy))
    return chips, offs, units

def compose_frame(rom, index, frame=0):
    """Compose one sprite frame from its assembly: returns (pixels SxS indices,
    palette, S, origin) with chips placed and flipped per the assembly data."""
    _, gfx_ptrs, pal_ptrs, asm_ptrs = parse_tables(rom)
    h = read_header(rom, index)
    tiles = list(decode_tiles(read_sprite_graphics(rom, gfx_ptrs, h['bitmap']), 4))
    palette = read_sprite_palette(rom, pal_ptrs, h['palette'])
    chips, offs, units = assembly_frame(rom, asm_ptrs, h, frame)
    S, O = 96, 40
    cv = [0] * (S * S)
    for k in range(units * 4):
        v = chips[k]
        src, fx, fy = v & 0x3FF, bool(v & 0x4000), bool(v & 0x8000)
        if src >= len(tiles):
            continue
        t = tiles[src]
        lx, ly = CHIP_LOCAL[k % 4]
        ox, oy = offs[k // 4]
        for y in range(8):
            for x in range(8):
                val = t[(7 - y if fy else y) * 8 + (7 - x if fx else x)]
                if val == 0:
                    continue
                px, py = ox + lx + x + O, oy + ly + y + O
                # the FIRST unit is in front: unit k is OBJ tile k, copied into ascending OAM slots, and a
                # lower slot wins (src/scene.c CtComposeSprite; Flea's cape pose hides her face otherwise)
                if 0 <= px < S and 0 <= py < S and not cv[py * S + px]:
                    cv[py * S + px] = val
    return cv, palette, S, O

def render_frame(rom, index, out, zoom=4):
    cv, palette, S, _ = compose_frame(rom, index)
    # crop to content
    xs = [i % S for i in range(S * S) if cv[i]]
    ys = [i // S for i in range(S * S) if cv[i]]
    if not xs:
        print("sprite %d: empty frame" % index); return
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    w, ht = x1 - x0 + 1, y1 - y0 + 1
    px = bytearray(w * zoom * ht * zoom * 3)
    for y in range(ht):
        for x in range(w):
            r, g, b = palette[cv[(y0 + y) * S + (x0 + x)]]
            for zy in range(zoom):
                for zx in range(zoom):
                    o = ((y * zoom + zy) * w * zoom + (x * zoom + zx)) * 3
                    px[o], px[o + 1], px[o + 2] = r, g, b
    write_png_rgb(out, px, w * zoom, ht * zoom)
    print("sprite %d frame 0: %dx%d (zoom %d) -> '%s'" % (index, w, ht, zoom, out))

def read_header(rom, index):
    if index >= 0x100:
        # ENEMY sprite (op-0x83; our index = 256 + enemy id): the field enemy record at
        # $E4:F600 + id*10 opens with the SAME 5-byte header shape as the NPC table --
        # the ROM op-0x83 handler C0/46DF consumes byte0->gfx set ($E3 -> allocator
        # C0/5C90), byte1->assembly ptr ($1380/$1300 via [$B2]), byte2*24->palette
        # offset ($1400), byte3->anim set ($1480/$1500/$1580 via [$B5]/[$B8]),
        # byte4->size ($1201). Oracle fresh loc-6: GATO id 0x92 rec `7B 76 B6 57 02`
        # -> $1300/$1380 = E3:0000 = asm_ptrs[0x76], $1400 = 0x1110 = 0xB6*24,
        # $1201 = 02. Reading the NPC header at 0x24F000 + index*5 for these indices
        # landed inside the enemy table itself = garbage sprite records.
        o = 0x24F600 + (index - 0x100) * 10
        return {'bitmap': rom[o], 'assembly': rom[o + 1], 'palette': rom[o + 2],
                'anim': rom[o + 3], 'size': rom[o + 4]}
    o = HEADER_BASE + index * 5
    return {'bitmap': rom[o], 'assembly': rom[o + 1], 'palette': rom[o + 2],
            'anim': rom[o + 3], 'size': rom[o + 4]}

# ---- sprite animation (facing/walk -> assembly frame) ----
# 7-pointer meta @0x24FFE0 (local 2-byte ptrs, bank 0x24): [2]=anim-frame(slot) ptr
# table, [3]=anim-duration ptr table (194 each). A sprite's header byte 3 = anim-set
# index. Within a set the slot data is [facing0 block][f1][f2][f3] of data_per_facing
# bytes; animation A (0=idle,1=walk,6=run) is at A*4, frame f at A*4+f (<=4 frames,
# delay 0 ends it). Facing picks one of the 4 per-frame assembly frames.
SPRITE_META = 0x24FFE0

def _sprite_anim_tables(rom):
    p2 = 0x240000 + u16(rom, SPRITE_META + 4)
    p3 = 0x240000 + u16(rom, SPRITE_META + 6)
    fp = [0x240000 + u16(rom, p2 + i * 2) for i in range(194)]
    dp = [0x240000 + u16(rom, p3 + i * 2) for i in range(194)]
    return fp, dp

def _anim_set_index(rom, sprite_index):
    if sprite_index < 0x100:
        return rom[HEADER_BASE + sprite_index * 5 + 3]
    return rom[0x24F600 + (sprite_index - 256) * 10 + 3]

def build_anim(rom, sprite_index, action):
    """Frames of `action` (0=idle,1=walk) for a sprite: [([U,D,L,R asm frame], delay)]."""
    fp, dp = _sprite_anim_tables(rom)
    si = _anim_set_index(rom, sprite_index)
    if si >= len(fp):
        return []
    base = fp[si]
    nxt = min([a for a in fp if a > base] + [0x24A800])
    dpf = (nxt - base) // 4
    if dpf <= 0:
        return []
    if action * 4 >= dpf:                          # no such animation -> idle (CT get_anim)
        action = 0
    dbase = dp[si]
    frames = []
    # The ROM ticker (C0/CA0F) addresses rows at anim*4 but ends them ONLY at a zero
    # duration byte -- rows freely run past their 4-byte slot (the player's walk = 6
    # steps, frames 10-15, overrunning into the anim-2 slot).
    for f in range(32):
        delay = rom[dbase + action * 4 + f]
        if delay == 0:
            break
        frames.append(([rom[base + fa * dpf + action * 4 + f] for fa in range(4)], delay))
    return frames

def pick_frame(rom, sprite_index, facing, walking, tick):
    """Assembly frame to render for (facing 0=U/1=D/2=L/3=R, walking?, 60Hz tick)."""
    frames = build_anim(rom, sprite_index, 1 if walking else 0)
    if not frames:
        return 0
    fi = facing if 0 <= facing < 4 else 1
    total = sum(d for _, d in frames)
    if total <= 0:
        return frames[0][0][fi]
    t = tick % total; acc = 0
    for sf, d in frames:
        acc += d
        if t < acc:
            return sf[fi]
    return frames[-1][0][fi]

def read_sprite_palette(rom, pal_ptrs, palette_index):
    # CT sprite palettes are 12 ROM colors mapped to indices 1..12; index 0 is
    # transparent and indices 13..15 are unused (read_palette set_start=1, pad=3).
    off = pal_ptrs[palette_index]
    cols = [(0, 0, 0)]                                                   # 0: transparent
    cols += [snes15_to_rgb(u16(rom, off + i * 2)) for i in range(12)]    # 1..12
    cols += [(0, 0, 0)] * 3                                              # 13..15
    return cols

# bitmap_index <= 6 => the 7 playable characters: graphics are stored UNCOMPRESSED.
# bitmap_index >  6 => NPC/enemy graphics, LZ-compressed.
PC_GFX_BYTES = 0x1A00

def read_sprite_graphics(rom, gfx_ptrs, bitmap_index):
    off = gfx_ptrs[bitmap_index]
    if bitmap_index <= 6:
        # PC graphics are uncompressed; the block runs to the next gfx pointer (capped at the
        # 10-bit chip range, 1024 tiles). The old fixed PC_GFX_BYTES (0x1A00 = 208 tiles) truncated
        # the set, dropping the higher tiles some frames use -> incomplete frames (e.g. Crono's
        # up-walk references tiles 839-858, beyond 208, so 6 chips were skipped on one frame).
        hi = [p for p in gfx_ptrs if p > off]
        size = min(0x8000, (min(hi) - off) if hi else 0x8000)
        return rom[off:off + size]
    return lz_decompress(rom, off)

def render_sprite(rom, index, out):
    _, gfx_ptrs, pal_ptrs, _ = parse_tables(rom)
    h = read_header(rom, index)
    gfx = read_sprite_graphics(rom, gfx_ptrs, h['bitmap'])
    palette = read_sprite_palette(rom, pal_ptrs, h['palette'])
    ntiles = len(gfx) // 32
    px, w, ht = render_rgb(decode_tiles(gfx, 4), 16, 4, palette)
    write_png_rgb(out, px, w, ht)
    print("sprite %d: bitmap=%d palette=%d -> %d tiles, %dx%d '%s'"
          % (index, h['bitmap'], h['palette'], ntiles, w, ht, out))

# Render the 7 playable characters, each with its own paired palette, stacked.
def render_chars(rom, out, tiles_each=96):
    _, gfx_ptrs, pal_ptrs, _ = parse_tables(rom)
    cols, rows_each = 16, (tiles_each + 15) // 16
    bw, bh = cols * 8, rows_each * 8
    W, H = bw, bh * 7
    full = bytearray(W * H * 3)
    names = ['Crono', 'Marle', 'Lucca', 'Robo', 'Frog', 'Ayla', 'Magus']
    for pc in range(7):
        h = read_header(rom, pc)
        gfx = read_sprite_graphics(rom, gfx_ptrs, h['bitmap'])[:tiles_each * 32]
        pal = read_sprite_palette(rom, pal_ptrs, h['palette'])
        px, w, ht = render_rgb(decode_tiles(gfx, 4), cols, 4, pal)
        for y in range(min(ht, bh)):
            row = (pc * bh + y) * W * 3
            full[row:row + w * 3] = px[y * w * 3:(y + 1) * w * 3]
        print("  %-6s sprite %d: bitmap=%d palette=%d" % (names[pc], pc, h['bitmap'], h['palette']))
    write_png_rgb(out, full, W, H)
    print("7 playable characters -> %dx%d '%s'" % (W, H, out))

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    rom = open(sys.argv[1], 'rb').read()
    cmd = sys.argv[2]
    if cmd == 'info':
        ptrs, gfx_ptrs, pal_ptrs, _ = parse_tables(rom)
        print("7-pointer table @0x%X:" % PTR_TABLE,
              ' '.join('0x%X' % p for p in ptrs))
        print("gfx table @0x%X, palette table @0x%X" % (ptrs[0], ptrs[4]))
        print("first 12 sprite headers (bitmap, palette):")
        for i in range(12):
            h = read_header(rom, i)
            print("  sprite %2d: bitmap=%3d palette=%3d  gfx@0x%06X pal@0x%06X"
                  % (i, h['bitmap'], h['palette'], gfx_ptrs[h['bitmap']], pal_ptrs[h['palette']]))
    elif cmd == 'render':       # raw chip sheet (debug)
        render_sprite(rom, int(sys.argv[3]), sys.argv[4])
    elif cmd == 'frame':        # assembled, colorized frame 0 (the real sprite)
        render_frame(rom, int(sys.argv[3]), sys.argv[4])
    elif cmd == 'chars':
        render_chars(rom, sys.argv[3])
    else:
        print("unknown command:", cmd); sys.exit(1)
