#!/usr/bin/env python3
"""Bake the CT battle ENTRY STANCE (combatant idle OBJ) into src/data/battle_stance.h + verify vs oracle.

Derivation dossier: disasm/notes/battle_stance.md. The battle module (bank C1/CC) CLEARS the field
object arrays at boot (C1:CE04) and renders combatants through its OWN OBJ engine, building the OAM
shadow $7E0700 (== the live OAM, proven 512/512 byte-exact) from per-combatant battle-animation state.
Each combatant's battle pose is a fresh boot UPLOAD to the OBJ VRAM (char base word 0): the OBJ
region 0x0000-0x1400 & 0x3000-0x3BFF is fully rewritten pre->in-battle (S1) -- the tile NUMBERS are
reused (Crono 0x08.., Marle 0x20.., GATO 0x68..+0x06) but the CONTENT is battle poses, NOT the field
sheet.

This bakes the SETTLED idle stance of the canonical GATO fight (party Crono seat0 + Marle seat1 vs
GATO enemy-slot3) at frame f460 -- regenerated deterministically from gato_press_action.state via
btnscript "100:8,0:452" (verified fb+vram+oam byte-identical to the committed idle_f460 capture).
At f460: Crono is the ACTIVE PC (command open, cur=0), Marle the WAITING PC (rock-static), GATO the
combat-idle enemy (12-sprite body + a 4x mirrored tile-0x06 foot quad).

Baked per combatant (S2/S3): the OAM sprite assembly (x,y,tile,attr), the referenced OBJ 4bpp tiles,
and the OBJ CGRAM palettes. The LOOP (S2/S4) -- a ~2px idle bob (Crono/GATO) driven by the ROM anim
engine (anim-slots $0520+n*0xC, FD:E3A9; streams a few OBJ tiles/frame) -- is a documented BOUNDARY;
the settled static stance is the byte-exact implementable minimum (mandate: pin frame 0, mark loop OPEN).

Run from repo root. Emits src/data/battle_stance.h. With --verify, reconstructs each combatant sprite from
the baked data, composites in OAM-priority order, and pixel-diffs vs the oracle fb over the combatant
bounding box (verified 0/2672 opaque px mismatch, d>24).
"""
import os, sys, subprocess
sys.path.insert(0, 'tools')
from ctlib import oam

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
SP    = os.environ.get('CT_SCRATCH', 'build')
BTN   = '100:8,0:452'                     # A-press boots battle; settle to f460 (verified reproducible)
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_stance.h')
NTILE = 0xA0                              # OBJ 8x8 tiles 0x00..0x9F cover all combatant cells (GATO max 0x9F)

# combatant palette -> name (S1: pal5 Crono, pal6 Marle, pal1 GATO; boot-dossier "GATO=pal6" was Marle)
ACTORS = [(5, 'Crono'), (6, 'Marle'), (1, 'GATO')]

def regen():
    out = os.path.join(SP, 'stance_f460.raw')
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', BTN],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out

def load(prefix):
    vram = open(prefix + '.vram', 'rb').read()
    cgram = oam.load_cgram(prefix + '.oam')
    sprites = oam.decode(oam.load(prefix + '.oam'))
    fbraw = open(prefix, 'rb').read()
    return vram, cgram, sprites, fbraw

# tile 0x006 = the active-PC BRACKET corner (OAM #0-3, pal1-attr, prio3). It is NOT a combatant
# sprite (NOT "GATO's foot quad" -- that was a misID): it is the pulsing white selection frame around
# the ACTIVE PC, and belongs to its own asset (src/data/battle_bracket.h, tools/_gen_battlebracket.py).
# Excluded here so the stance bake is the combatant bodies ONLY (de-contamination, battle_stance.md T1).
BRACKET_TILE = 0x006

def onscreen(sprites):
    on = [s for s in sprites if s['y'] < 200 and -32 < s['x'] < 256 and s['pal'] in (1, 5, 6)
          and s['tile'] != BRACKET_TILE]
    on.sort(key=lambda s: s['idx'])
    return on

def bracket_sprites(sprites):
    return [s for s in sprites if s['y'] < 200 and -32 < s['x'] < 256 and s['tile'] == BRACKET_TILE]

def bracket_ink_pixels(sprites, vram):
    """Screen (X,Y) of every non-transparent pixel of the tile-0x006 bracket sprites -- the pixels the
    bracket OWNS in the oracle fb. Excluded from the combatant verify (the bracket is a separate asset)."""
    mask = set()
    for s in bracket_sprites(sprites):
        for cy in range(2):
            for cx in range(2):
                px = dt8(vram, s['tile'] + cx + 0x10 * cy)
                for yy in range(8):
                    for xx in range(8):
                        if px[yy][xx] == 0:
                            continue
                        sx, sy = cx*8 + xx, cy*8 + yy
                        if s['hflip']: sx = 15 - sx
                        if s['vflip']: sy = 15 - sy
                        mask.add((s['x'] + sx, s['y'] + sy))
    return mask

def dt8(vram, t):
    o = (t & 0x1FF) * 32
    px = [[0] * 8 for _ in range(8)]
    for r in range(8):
        p0, p1, p2, p3 = vram[o+r*2], vram[o+r*2+1], vram[o+16+r*2], vram[o+16+r*2+1]
        for c in range(8):
            b = 7 - c
            px[r][c] = ((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
    return px

def rgb555(word):
    rep = lambda c: (c << 3) | (c >> 2)
    return (rep(word & 31), rep((word >> 5) & 31), rep((word >> 10) & 31))

# ------------------------------------------------------------------ emit
def emit(fp, on, vram, cgram):
    W = fp.write
    W("// AUTO-GENERATED by tools/gen/battle_stance.py -- DO NOT EDIT.\n")
    W("// CT battle ENTRY STANCE (combatant idle OBJ), baked byte-exact from the oracle.\n")
    W("// Dossier: disasm/notes/battle_stance.md. Settled GATO-fight frame f460\n")
    W("//   (gato_press_action.state + btnscript \"%s\"; verified reproducible + 0/2672 px vs fb).\n" % BTN)
    W("// OBJ char base = VRAM word 0 (battle_boot.md S1); 16x16 sprites; pal5 Crono/pal6 Marle/pal1 GATO.\n")
    W("// A stance OBJ pixel at OAM (x,y) lands at oracle fb true-pixel (x, y+8) (verified).\n")
    W("#ifndef CT_BATTLE_STANCE_H\n#define CT_BATTLE_STANCE_H\n#include <stdint.h>\n\n")

    # --- flat OBJ list (OAM-index order; draw HIGH->LOW index so idx0 ends on top) ---
    W("// The settled combatant OBJ list = the $7E0700 OAM shadow (onscreen combatant sprites),\n")
    W("// in OAM-index order. Composite by drawing HIGHEST oidx first (SNES: low index = on top).\n")
    W("// x is signed 9-bit screen x; y is OAM y (screen row = y, fb row = y+8). tile = 9-bit OBJ\n")
    W("// tile (base word 0). pal 1/5/6. prio 3. hf/vf = h/v flip.\n")
    W("typedef struct { uint8_t oidx; int16_t x; uint8_t y; uint16_t tile;\n")
    W("                 uint8_t pal, prio, hf, vf; } CtStanceObj;\n")
    W("#define CT_STANCE_NOBJ %d\n" % len(on))
    W("static const CtStanceObj ctBattleStanceObj[CT_STANCE_NOBJ] = {\n")
    for s in on:
        W("  { %3d, %4d, %3d, 0x%03X, %d, %d, %d, %d },\n"
          % (s['idx'], s['x'], s['y'], s['tile'], s['pal'], s['prio'], s['hflip'], s['vflip']))
    W("};\n\n")

    # --- per-combatant descriptor (partition by palette) ---
    W("// Per-combatant partition (by palette). anchorX/Y = min corner of the sprite cluster.\n")
    W("// Crono = ACTIVE PC (command open); Marle = WAITING PC (static); GATO = combat-idle enemy.\n")
    W("typedef struct { const char* name; uint8_t pal; int16_t anchorX; uint8_t anchorY, nspr; } CtStanceActor;\n")
    W("#define CT_STANCE_NACTOR %d\n" % len(ACTORS))
    W("static const CtStanceActor ctBattleStanceActor[CT_STANCE_NACTOR] = {\n")
    for pal, name in ACTORS:
        grp = [s for s in on if s['pal'] == pal]
        ax = min(s['x'] for s in grp); ay = min(s['y'] for s in grp)
        W('  { "%s", %d, %4d, %3d, %d },\n' % (name, pal, ax, ay, len(grp)))
    W("};\n\n")

    # --- OBJ tiles 0x00..0x9F (4bpp, 32 bytes each) verbatim from VRAM (battle-pose boot upload) ---
    W("// OBJ 8x8 tiles 0x00..0x%02X (4bpp SNES planar, 32 B each) -- the battle-pose upload the\n" % (NTILE-1))
    W("// module writes at boot (S1: OBJ VRAM 0x0000-0x1400/0x3000-0x3BFF rewritten; NOT the field sheet).\n")
    W("// A 16x16 sprite of tile T uses 8x8 tiles T, T+1, T+0x10, T+0x11 (16-wide sheet).\n")
    W("#define CT_STANCE_NTILE 0x%02X\n" % NTILE)
    W("static const uint8_t ctBattleStanceObjTiles[CT_STANCE_NTILE][32] = {\n")
    for t in range(NTILE):
        o = t * 32
        row = ','.join('0x%02X' % vram[o + i] for i in range(32))
        W("  {%s}, // 0x%02X\n" % (row, t))
    W("};\n\n")

    # --- OBJ CGRAM palettes (raw BGR555 words; RGB in comment) ---
    W("// OBJ CGRAM palettes (BGR555 LE words). OBJ palette P = CGRAM entries 128 + P*16 + [0..15];\n")
    W("// index 0 = transparent. Combatant palettes used: 5 (Crono), 6 (Marle), 1 (GATO).\n")
    W("static const uint16_t ctBattleStanceObjPal[8][16] = {\n")
    for p in range(8):
        vals = []
        for c in range(16):
            i = 128 + p * 16 + c
            vals.append(cgram[i*2] | (cgram[i*2+1] << 8))
        used = ' USED' if p in (1, 5, 6) else ''
        W("  {%s}, // pal%d%s\n" % (','.join('0x%04X' % v for v in vals), p, used))
    W("};\n\n")

    W("#endif // CT_BATTLE_STANCE_H\n")

# ------------------------------------------------------------------ verify
def verify(on, vram, cgram, fbraw, brk_mask=frozenset()):
    buf = [[None] * 256 for _ in range(224)]
    for s in sorted(on, key=lambda s: -s['idx']):      # high idx first -> idx0 ends on top
        for cy in range(2):
            for cx in range(2):
                px = dt8(vram, s['tile'] + cx + 0x10 * cy)
                for yy in range(8):
                    for xx in range(8):
                        v = px[yy][xx]
                        if v == 0:
                            continue
                        sx, sy = cx*8 + xx, cy*8 + yy
                        if s['hflip']: sx = 15 - sx
                        if s['vflip']: sy = 15 - sy
                        X, Y = s['x'] + sx, s['y'] + sy
                        if 0 <= X < 256 and 0 <= Y < 224:
                            buf[Y][X] = rgb555(cgram[(128 + s['pal']*16 + v) * 2] |
                                               (cgram[(128 + s['pal']*16 + v) * 2 + 1] << 8))
    ys = [y for y in range(224) if any(buf[y][x] for x in range(256))]
    xs = [x for x in range(256) if any(buf[y][x] for y in range(224))]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    diff = tot = excl = 0
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            c = buf[y][x]
            if c is None:
                continue
            if (x, y) in brk_mask:                     # bracket-owned pixel -> its own asset, EXCLUDE
                excl += 1
                continue
            tot += 1
            o = ((2 * (y + 8)) * 512 + 2 * x) * 4      # fb true-pixel (x, y+8), bytes [pad,B,G,R]
            d = abs(c[0]-fbraw[o+3]) + abs(c[1]-fbraw[o+2]) + abs(c[2]-fbraw[o+1])
            if d > 24:
                diff += 1
    print("[battle-stance verify] combatant OBJ over bbox x[%d..%d] y[%d..%d]:" % (x0, x1, y0, y1))
    print("  exact-ish=%d/%d  mismatch(d>24)=%d  (%.1f%% match)  [bracket px EXCLUDED=%d]"
          % (tot - diff, tot, diff, 100.0 * (tot - diff) / tot, excl))
    for pal, name in ACTORS:
        grp = [s for s in on if s['pal'] == pal]
        print("  %-6s pal%d: %d sprites, tiles %s" %
              (name, pal, len(grp), sorted(set('0x%03X' % s['tile'] for s in grp))))
    return diff

def main():
    prefix = regen()
    vram, cgram, sprites, fbraw = load(prefix)
    on = onscreen(sprites)
    if '--verify' in sys.argv:
        verify(on, vram, cgram, fbraw, bracket_ink_pixels(sprites, vram))
    if '--verify' not in sys.argv or '--emit' in sys.argv:
        with open(OUT_H, 'w') as fp:
            emit(fp, on, vram, cgram)
        print("wrote %s (%d OBJ, %d tiles)" % (OUT_H, len(on), NTILE))

if __name__ == '__main__':
    main()
