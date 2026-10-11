#!/usr/bin/env python3
"""Render a Chrono Trigger scene (field map) in color, via the ROM tables
(reverse-engineered as in CTViewer's backend_snes / scene / tileset / maps /
map_renderer). Composites all three layers and emulates the SNES color-math
(main + sub screen blend) for translucency / night-lighting effects.

CLI: python assets/maps.py <rom.sfc> scene <index> <out.png> [--no-blend]
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '../tools')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lz import lz_decompress
from palettes import snes15_to_rgb, write_png_rgb
from gfx import decode_tiles
from sprites import u16, get_entries_3byte

SCENE_HDR = 0x360000; SCENE_TS = 0x361C00; SCENE_PAL = 0x3624C0
MAP_PTRS  = 0x361E00; TS_PTRS  = 0x362220; ASM_PTRS = 0x362100; ASM3_PTRS = 0x3621C0
ANIM_PTRS = 0x3DF290; ANIM_REL = 0x3DF310; ANIM_END = 0x3DF9CC   # chip-anim table (64)
L1, L2, L3 = 0x01, 0x02, 0x04           # coverage bits, matching SCREEN_/EFFECT_ low bits

def _entry_sizes(addrs, end):
    return [min([b for b in addrs if b > a] + [end]) - a for a in addrs]

def chip_anim_data(rom, index):
    """Return the raw chip-animation block for tileset `index` (get_relative_entries)."""
    addrs = [u16(rom, ANIM_PTRS + i * 2) + ANIM_REL for i in range(64)]
    sizes = _entry_sizes(addrs, ANIM_END)
    a, n = addrs[index], sizes[index]
    return rom[a:a + n] if n > 0 else b''

_CHIP_DUR = {0x10: 16, 0x20: 12, 0x40: 8, 0x80: 4}     # duration nibble -> 60Hz ticks

def parse_chip_anims(data):
    """Each anim covers 4 chips: returns [(dest_chip, [src per frame], [dur ticks])]."""
    anims = []; i = 0
    while i < len(data):
        fc = data[i]; i += 1
        if fc == 0 or fc == 0x80:
            break
        dest = (u16(data, i) - 0x2000) // 16; i += 2
        durs = [_CHIP_DUR.get(data[i + f] & 0xF0, 0) for f in range(fc)]; i += fc
        srcs = []
        for _ in range(fc):
            srcs.append((u16(data, i) - 0x6000) // 32); i += 2
        anims.append((dest, srcs, durs))
    return anims

def anim_frame_at(durs, tick):
    """Which animation frame is showing at 60Hz `tick`, given per-frame durations."""
    total = sum(durs)
    if total <= 0:
        return 0
    t = tick % total; acc = 0
    for f, d in enumerate(durs):
        acc += d
        if t < acc:
            return f
    return len(durs) - 1

PAL_ADDR = 0x3DF9C7   # 0xB6: u16 ptr per anim set (into PAL_ANIM)
PAL_ANIM = 0x3DFA77   # 0x56E: anim descriptor stream
PAL_COLOR = 0x367380  # 0xC80: predefined color sets (SNES addr - 0x7380 => offset here)

def scene_palette(rom, pi):
    p = SCENE_PAL + pi * 210
    cols = [(0, 0, 0)] * 16
    for _ in range(7):
        cols.append((0, 0, 0))
        for _ in range(15):
            cols.append(snes15_to_rgb(u16(rom, p))); p += 2
    return cols

def read_palette_anims(rom, index):
    """Parse the scene's palette-animation set (index = scene palette index)."""
    if index * 2 + 1 >= 0xB6:
        return []
    p = PAL_ANIM + u16(rom, PAL_ADDR + index * 2)
    end = PAL_ANIM + 0x56E
    anims = []
    while p < end:
        idb = rom[p]; p += 1
        if idb == 0:
            break
        typ = idb & 0xF0
        if typ not in (0x80, 0x30, 0x20, 0x10):   # broken descriptor: skip this byte
            continue
        ci, cc, delay = rom[p], rom[p + 1], rom[p + 2]; p += 3
        frames, colors = [], []
        if typ in (0x80, 0x10):                   # Sequence: swap colors from a table
            addr = u16(rom, p) - 0x7380; p += 3   # u16 addr + 1 skipped byte
            fc = (idb & 0x0F) + 1
            if typ == 0x80:
                frames = [rom[p + i] for i in range(fc)]; p += fc
            else:
                frames = list(range(fc))
            total = (max(frames) + 1) * cc
            cp = PAL_COLOR + addr
            colors = [snes15_to_rgb(u16(rom, cp + i * 2)) for i in range(total)]
        anims.append((typ, ci, cc, delay, frames, colors))
    return anims

def apply_palette_anims(pal, anims, frame):
    """Advance each palette animation by `frame`//delay steps (frame = 60Hz tick)."""
    for typ, ci, cc, delay, frames, colors in anims:
        step = frame // max(1, delay)
        if typ == 0x30:                           # CycleForward: rotate cc+1 entries up
            n = cc + 1
            seg = pal[ci:ci + n]
            for _ in range(step % n if n else 0):
                seg = [seg[-1]] + seg[:-1]
            pal[ci:ci + n] = seg
        elif typ == 0x20:                         # CycleBackward: rotate down
            n = cc + 1
            seg = pal[ci:ci + n]
            for _ in range(step % n if n else 0):
                seg = seg[1:] + [seg[0]]
            pal[ci:ci + n] = seg
        elif frames and colors:                   # Sequence: pick the frame's color set
            src = frames[step % len(frames)] * cc
            if src + cc <= len(colors):
                pal[ci:ci + cc] = colors[src:src + cc]

def _metatiles(asm, count, pal_mul, sub256):
    out = []; p = 0
    for _ in range(count):
        corners = []
        for _ in range(4):
            if p + 1 >= len(asm):
                corners.append((0, 0, False, False, False)); continue
            v = asm[p] | (asm[p + 1] << 8); p += 2
            chip = v & 0x3FF
            if sub256:
                chip = chip - 256 if chip >= 256 else 0
            corners.append((chip, ((v >> 10) & 7) * pal_mul,
                            bool(v & 0x4000), bool(v & 0x8000), bool(v & 0x2000)))
        out.append(corners)
    return out

def load_tileset_l12(rom, ts_index, ts_gfx_ptrs, asm_ptrs, anim_frame=0):
    chipsets = rom[SCENE_TS + ts_index * 8: SCENE_TS + ts_index * 8 + 8]
    chips = [None] * 768
    for slot in range(6):
        cs = chipsets[slot]
        if cs == 0xFF or cs >= len(ts_gfx_ptrs):
            continue
        for j, t in enumerate(list(decode_tiles(lz_decompress(rom, ts_gfx_ptrs[cs]), 4))[:128]):
            chips[slot * 128 + j] = t
    # Animated chips: chipset slot 6 is the animated chip graphics; chip_anims map
    # source animated chips onto destination static chips per frame (tileset.tick).
    cs6 = chipsets[6]
    if cs6 != 0xFF and cs6 < len(ts_gfx_ptrs):
        animated = list(decode_tiles(lz_decompress(rom, ts_gfx_ptrs[cs6]), 4))
        for dest, srcs, durs in parse_chip_anims(chip_anim_data(rom, ts_index)):
            src = srcs[anim_frame_at(durs, anim_frame)] if srcs else 0
            for k in range(4):
                if dest + k < len(chips) and 0 <= src + k < len(animated):
                    chips[dest + k] = animated[src + k]
    return chips, _metatiles(lz_decompress(rom, asm_ptrs[ts_index % len(asm_ptrs)]), 512, 16, True)

def load_tileset_l3(rom, l3_index, ts_gfx_ptrs, asm3_ptrs):
    if l3_index >= len(ts_gfx_ptrs) or ts_gfx_ptrs[l3_index] == 0:
        return [], []
    chips = list(decode_tiles(lz_decompress(rom, ts_gfx_ptrs[l3_index]), 2))   # 2bpp
    if l3_index >= len(asm3_ptrs) or asm3_ptrs[l3_index] == 0:
        return chips, []
    return chips, _metatiles(lz_decompress(rom, asm3_ptrs[l3_index]), 256, 4, False)

def parse_props(data, off, n):
    l1, l2 = [], []; i = off
    while len(l1) < n and i + 2 < len(data):
        b0 = data[i]; i += 3
        a1, a2, rle = bool(b0 & 1), bool(b0 & 2), bool(b0 & 0x80)
        rep = 1
        if rle:
            rep = data[i] if i < len(data) else 0; i += 1
            rep = rep or 256
        for _ in range(rep):
            l1.append(a1); l2.append(a2)
    while len(l1) < n:
        l1.append(False); l2.append(False)
    return l1, l2

def scene_props(rom, scene):
    """Full per-tile scene tile properties (3-byte RLE records). Returns (sw, sh, coll,
    zplane, zflags, mover): coll = collision shape (prop0>>2 & 0x1F), zplane = prop2 & 3, zflags
    bits = 1:IGNORE_Z(prop2&4) 2:INVERTED(prop2&8) 4:Z_NEUTRAL(prop2&0x20) 8:DOOR(prop1&0x10)
    16:SPRITE_FRONT_BOTTOM(prop2&0x40) 32:SPRITE_FRONT_TOP(prop1&0x40). The two sprite-front
    flags = the engine's $6E/$6F (= $0C00/$0C01): top half = prop1&0x40, bottom half = prop2&0x40;
    a set flag = that half gets OAM priority 3 (drawn in front of the BG1/BG2 priority-1 foreground).
    mover = prop2 bit7 (b2 & 0x80): the per-tile MOVER-SOLID flag the field-load loop streams RAW to
    $7E7040,X (C0/A630). The cat/object mover path C0/ACFD (and C0/31E5, op-0x96 C0/4FE0) does
    `LDA $7E7040,X / BMI` -> bit7 set = solid for movers (bounce/stall). This is DISTINCT from the
    player path (C0/9C5C reads $7E7080 = the SHAPE-table-expanded plane), so stair-mouth tiles are
    mover-solid here yet player-walkable via the slope shapes. Verified byte-exact against the oracle
    WRAM probe_base.raw.wram[$7040] for scene 1. (Previously dropped: zflags carried prop1&0x40 as its
    top bit, an OAM-priority flag, NOT this mover bit -- that mismatch let the cat climb the stairs.)"""
    h = rom[SCENE_HDR + scene * 14: SCENE_HDR + scene * 14 + 14]
    map_i = h[4] | (h[5] << 8)
    md = lz_decompress(rom, get_entries_3byte(rom, MAP_PTRS, 202)[map_i])
    size, bits = md[0], md[1]
    w1 = ((size >> 0) & 3) * 16 + 16; h1 = ((size >> 2) & 3) * 16 + 16
    w2 = ((size >> 4) & 3) * 16 + 16; h2 = ((size >> 6) & 3) * 16 + 16
    w3 = ((bits >> 0) & 3) * 16 + 16; h3 = ((bits >> 2) & 3) * 16 + 16
    has_l3 = bool(bits & 0x80)
    p = 6 + w1 * h1 + w2 * h2 + (w3 * h3 if has_l3 else 0)
    sw, sh = max(w1, w2), max(h1, h2)
    n = sw * sh
    coll = [0] * n; zplane = [0] * n; zflags = [0] * n; mover = [0] * n
    cnt, i = 0, p
    while cnt < n and i + 2 < len(md):
        b0, b1, b2 = md[i], md[i + 1], md[i + 2]; i += 3
        c = (b0 >> 2) & 0x1F
        z = b2 & 3
        zf = (1 if b2 & 0x04 else 0) | (2 if b2 & 0x08 else 0) | (4 if b2 & 0x20 else 0) | (8 if b1 & 0x10 else 0) | (0x10 if b2 & 0x40 else 0) | (0x20 if b1 & 0x40 else 0)
        # mover plane: bit0 = prop2 bit7 ($7E7040,X bit7 = MOVER-SOLID); bit1 = prop2 bit4, the HIGH
        # bit of the SOLID-plane z ($7E7040 bits3-4, read by the 9B62 solid tail as (b2&0x18)>>3 when a
        # slope shape blocks the foot). zflags only carried prop2 bit3 (INVERTED) + bit5 (Z_NEUTRAL),
        # so bit4 was dropped -> stairs whose slope-blocked half is same-plane (e.g. scene-13 (14,29)
        # b2=0x12: solid-z=2) read as solid-z=0 = transition-solid and wrongly blocked the climb.
        mv = (1 if b2 & 0x80 else 0) | (2 if b2 & 0x10 else 0)
        rep = 1
        if b0 & 0x80:
            rep = md[i] if i < len(md) else 0; i += 1
            rep = rep or 256
        for _ in range(rep):
            if cnt < n:
                coll[cnt] = c; zplane[cnt] = z; zflags[cnt] = zf; mover[cnt] = mv; cnt += 1
    return sw, sh, coll, zplane, zflags, mover

def scene_collision(rom, scene):
    """Per-tile collision shape (0=None,1=Full,2-29=slopes/halves/quadrants,30=Ladder)."""
    sw, sh, coll, _z, _f, _mv = scene_props(rom, scene)
    return sw, sh, coll

def scene_exits(rom, scene):
    """Scene exits (doors): table @0x250000 (512 scenes, local u16 ptrs, ordered); each
    SNES record is 7 bytes — x,y (*16 px), size_bits, dest_index+facing (u16), dest
    tile x,y. Returns [{pos,size,kind('scene'/'world'),dest,dpos,facing}]."""
    BASE = 0x250000
    addr = BASE + u16(rom, BASE + scene * 2)
    nxt = (BASE + u16(rom, BASE + (scene + 1) * 2)) if scene < 511 else 0x251A44
    exits = []
    o = addr
    for _ in range(max(0, (nxt - addr) // 7)):
        ex, ey, sb = rom[o] * 16, rom[o + 1] * 16, rom[o + 2]
        dif = rom[o + 3] | (rom[o + 4] << 8)
        tx, ty = rom[o + 5], rom[o + 6]
        o += 7
        side = ((sb & 0x7F) + 1) * 16
        size = (16, side) if (sb & 0x80) else (side, 16)
        dest = dif & 0x1FF
        facing = (dif & 0x600) >> 9
        shx = -8 if (dif & 0x800) else 0
        shy = -8 if (dif & 0x1000) else 0
        if 0x1F0 <= dest <= 0x1FF:
            exits.append({'pos': (ex, ey), 'size': size, 'kind': 'world',
                          'dest': dest - 0x1F0, 'dpos': (tx * 8 + shx, ty * 8 + shy), 'facing': facing})
        else:
            exits.append({'pos': (ex, ey), 'size': size, 'kind': 'scene', 'dest': dest,
                          'dpos': (tx * 16 + 8 + shx, ty * 16 + 15 + shy), 'facing': facing})
    return exits

def draw_layer(px, cov, W, H, tiles, lw, lh, metas, chips, pal, prio, bit):
    if not metas or not chips:
        return
    # SNES BG layers wrap: a layer smaller than the canvas tiles to fill it
    # (chip_x % width, chip_y % height). Iterate the whole canvas in metatiles
    # and sample the layer modulo its own dimensions.
    cwm, chm = W // 16, H // 16
    for cy in range(chm):
        for cx in range(cwm):
            ti = tiles[(cy % lh) * lw + (cx % lw)]
            if ti >= len(metas):
                continue
            for c in range(4):
                chip, base, fx, fy, cp = metas[ti][c]
                if chip == 0 or chip >= len(chips) or chips[chip] is None:
                    continue
                if cp != (prio == 1):
                    continue
                tile = chips[chip]
                ox, oy = (c % 2) * 8, (c // 2) * 8
                for y in range(8):
                    py = cy * 16 + oy + y
                    if py >= H:
                        break
                    for x in range(8):
                        v = tile[(7 - y if fy else y) * 8 + (7 - x if fx else x)]
                        if v == 0:
                            continue
                        pxx = cx * 16 + ox + x
                        if pxx >= W:
                            break
                        r, g, b = pal[base + v]
                        idx = py * W + pxx
                        o = idx * 3
                        px[o], px[o + 1], px[o + 2] = r, g, b
                        cov[idx] = bit

def draw_pass(px, cov, W, H, targets, layers):
    """layers: dict bit -> (tiles, lw, lh, metas, chips, pal). Draws in the SNES
    back-to-front order L3p0, L2p0, L1p0, L2p1, L1p1, L3p1 for targeted layers."""
    seq = [(L3, 0), (L2, 0), (L1, 0), (L2, 1), (L1, 1), (L3, 1)]
    for bit, prio in seq:
        if (targets & bit) and bit in layers:
            t, lw, lh, m, ch, pal = layers[bit]
            draw_layer(px, cov, W, H, t, lw, lh, m, ch, pal, prio, bit)

def render_scene(rom, scene_index, out, blend=True, full=False, anim_frame=0):
    h = rom[SCENE_HDR + scene_index * 14: SCENE_HDR + scene_index * 14 + 14]
    ts_l12, ts_l3, pal_i, map_i = h[1], h[2], h[3], h[4] | (h[5] << 8)
    map_ptrs = get_entries_3byte(rom, MAP_PTRS, 202)
    ts_gfx = get_entries_3byte(rom, TS_PTRS, 204)
    asm_ptrs = get_entries_3byte(rom, ASM_PTRS, 64)
    asm3_ptrs = get_entries_3byte(rom, ASM3_PTRS, 19)

    chips12, meta12 = load_tileset_l12(rom, ts_l12, ts_gfx, asm_ptrs, anim_frame)
    pal = scene_palette(rom, pal_i)
    apply_palette_anims(pal, read_palette_anims(rom, pal_i), anim_frame)   # color cycling
    md = lz_decompress(rom, map_ptrs[map_i])
    size, bits = md[0], md[1]
    screen_flags, effect_flags = md[4], md[5]
    w1 = ((size >> 0) & 3) * 16 + 16; h1 = ((size >> 2) & 3) * 16 + 16
    w2 = ((size >> 4) & 3) * 16 + 16; h2 = ((size >> 6) & 3) * 16 + 16
    w3 = ((bits >> 0) & 3) * 16 + 16; h3 = ((bits >> 2) & 3) * 16 + 16
    has_l3 = bool(bits & 0x80)
    p = 6
    l1t = list(md[p:p + w1 * h1]); p += w1 * h1
    l2t = list(md[p:p + w2 * h2]); p += w2 * h2
    l3t = []
    if has_l3:
        l3t = list(md[p:p + w3 * h3]); p += w3 * h3
    sw, sh = max(w1, w2), max(h1, h2)
    l1add, l2add = parse_props(md, p, sw * sh)
    for y in range(h1):
        for x in range(w1):
            if l1add[y * sw + x]: l1t[y * w1 + x] |= 256
    for y in range(h2):
        for x in range(w2):
            if l2add[y * sw + x]: l2t[y * w2 + x] |= 256

    chips3, meta3 = load_tileset_l3(rom, ts_l3, ts_gfx, asm3_ptrs) if has_l3 else ([], [])
    layers = {L1: (l1t, w1, h1, meta12, chips12, pal),
              L2: (l2t, w2, h2, meta12, chips12, pal),
              L3: (l3t, w3, h3, meta3, chips3, pal)}

    # screen_flags: low nibble = MAIN layer enables, high nibble = SUB; effect_flags
    # low 3 bits = color-math layer enable, bit6 = half, bit7 = subtract.
    main_t = screen_flags & 0x07
    sub_t = (screen_flags >> 4) & 0x07
    blend_en = effect_flags & 0x07
    sub_flag = bool(effect_flags & 0x80); half = bool(effect_flags & 0x40)
    if not blend:
        main_t |= sub_t; sub_t = 0
    CW = max(w1, w2, w3 if has_l3 else 0); CH = max(h1, h2, h3 if has_l3 else 0)
    W, H = CW * 16, CH * 16

    main_px = bytearray(W * H * 3); main_cov = bytearray(W * H)
    sub_px = bytearray(W * H * 3); sub_cov = bytearray(W * H)
    draw_pass(main_px, main_cov, W, H, main_t, layers)
    draw_pass(sub_px, sub_cov, W, H, sub_t, layers)

    mode = ("SubHalf" if half else "Sub") if sub_flag else ("AddHalf" if half else "Add")
    print("scene %d: ts12=%d ts3=%d pal=%d map=%d  screen=%02X effect=%02X  main=%X sub=%X blend=%X %s"
          % (scene_index, ts_l12, ts_l3, pal_i, map_i, screen_flags, effect_flags,
             main_t, sub_t, blend_en, mode))

    out_px = main_px
    if sub_t:                                   # color-math blend (fixed color = black)
        for i in range(W * H):
            o = i * 3
            sc = sub_cov[i]; mc = main_cov[i]
            inp = (sub_px[o], sub_px[o + 1], sub_px[o + 2]) if sc else (0, 0, 0)
            if (mc & blend_en) and sc:
                for k in range(3):
                    d, s = main_px[o + k], inp[k]
                    if sub_flag:
                        out_px[o + k] = max(0, (d >> 1) - (s >> 1)) if half else max(0, d - s)
                    else:
                        out_px[o + k] = min(255, (d >> 1) + (s >> 1)) if half else min(255, d + s)
            elif mc == 0 and sc:
                out_px[o], out_px[o + 1], out_px[o + 2] = inp

    # Crop to the scroll mask = the camera-visible / playable area (a scene map
    # can hold several rooms; the mask selects this scene's view). h[10..13] =
    # left,top,right,bottom in metatiles (0x80 left => whole layer-1).
    if full:
        cx0, cy0, cx1, cy1 = 0, 0, W, H
    elif h[10] == 0x80:
        cx0, cy0, cx1, cy1 = 0, 0, w1 * 16, h1 * 16
    else:
        cx0, cy0, cx1, cy1 = h[10] * 16, h[11] * 16, (h[12] + 1) * 16, (h[13] + 1) * 16
    cx0, cy0 = max(0, cx0), max(0, cy0)
    cx1, cy1 = min(W, cx1), min(H, cy1)
    cw, ch = cx1 - cx0, cy1 - cy0
    if (cw, ch) != (W, H) and cw > 0 and ch > 0:
        crop = bytearray(cw * ch * 3)
        for y in range(ch):
            s = ((cy0 + y) * W + cx0) * 3
            crop[y * cw * 3:(y + 1) * cw * 3] = out_px[s:s + cw * 3]
        out_px, W, H = crop, cw, ch
    fnv = 2166136261
    for v in out_px:
        fnv = ((fnv ^ v) * 16777619) & 0xFFFFFFFF
    write_png_rgb(out, out_px, W, H)
    print("  -> visible %dx%d '%s'  fnv32=%08X" % (W, H, out, fnv))

if __name__ == '__main__':
    if len(sys.argv) < 5 or sys.argv[2] != 'scene':
        print(__doc__); sys.exit(1)
    rom = _romlib.load(sys.argv[1])
    frame = 0
    if '--frame' in sys.argv:
        frame = int(sys.argv[sys.argv.index('--frame') + 1])
    render_scene(rom, int(sys.argv[3]), sys.argv[4],
                 '--no-blend' not in sys.argv, '--full' in sys.argv, frame)
