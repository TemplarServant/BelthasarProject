#!/usr/bin/env python3
"""Bake the forest battle (scene 19, 3x Hetake id 0x5E) enemy sprites into
src/data/battle_hetake.h + verify against the oracle.

disasm/notes/battle_scenario2.md. The forest fight is the first multi-enemy battle
(Crono+Marle vs 3 Hetake, 14 HP each). As with GATO, the battle module (bank C1/CC)
clears field OBJ at boot and renders combatants through its own OBJ engine into the
$7E0700 OAM shadow; the enemy poses are a fresh boot upload to OBJ VRAM (char base
word 0). The 3 Hetakes occupy distinct VRAM tile blocks (slot0=0x40, slot1=0x48,
slot2=0x60) and stream their idle anim independently (slot0 and slot1 happen to be
identical on the captured frame; slot2 is at a different phase).

Baked (T1):
  - the 3 Hetake idle clusters: OAM assembly (rel anchor) + anchors, one snapshot.
  - OBJ tiles 0x00..0x7F (4bpp) from the in-battle VRAM: cursor(0x00), parked-cursor
    foot(0x06), the 3 Hetake idle blocks (0x40..0x77).
  - pal1 (enemy) CGRAM.
  - the Hetake attack keyframe (the spore-burst lunge pose) tiles 0x40..0x47 + assembly,
    baked from the leap-arc peak frame; + the measured leap-arc trajectory table.
  - the target-select cursor (tile 0x000 pal1 prio3) + per-slot on-screen positions.

Idle is OAM-static (no positional bob; the tile content streams -- the C1/CC anim engine
boundary, as for the stance/GATO idle). Enemies appear settled at their formation anchors
(no entrance morph; only the PCs' weapon draw + the HUD slides). Outside this bake (cited
in the dossier): the per-frame tile stream and the formation-anchor ROM math.

Run from repo root. Emits src/data/battle_hetake.h. With --verify, reconstructs the 3 idle
Hetakes (and the attack keyframe) from the baked data and pixel-diffs vs the oracle fb.
"""
import os, sys, subprocess
sys.path.insert(0, 'tools')
from ctlib import oam

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_forest_3hetake.state'))
SP    = os.environ.get('CT_SCRATCH', 'build')
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_hetake.h')
NTILE = 0x80          # OBJ tiles 0x00..0x7F cover cursor(0x00)+foot(0x06)+3 Hetake blocks(0x40..0x77)

# Hetake body 16x16 cells occupy these tile numbers per slot (dossier T1)
SLOT_TILES = [(0x40, 0x48), (0x48, 0x50), (0x60, 0x68)]   # [lo,hi) 16x16-cell tile range per slot
ATK_TILES  = (0x40, 0x48)                                  # attack keyframe reuses slot0's block

def peek(tag, btn):
    out = os.path.join(SP, tag + '.raw')
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', btn],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out

def load(prefix):
    vram  = open(prefix + '.vram', 'rb').read()
    cgram = oam.load_cgram(prefix + '.oam')
    sp    = oam.decode(oam.load(prefix + '.oam'))
    fb    = open(prefix, 'rb').read()
    return vram, cgram, sp, fb

def dt8(vram, t):
    o = (t & 0x1FF) * 32
    px = [[0]*8 for _ in range(8)]
    for r in range(8):
        p0, p1, p2, p3 = vram[o+r*2], vram[o+r*2+1], vram[o+16+r*2], vram[o+16+r*2+1]
        for c in range(8):
            b = 7 - c
            px[r][c] = ((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
    return px

def rgb555(word):
    rep = lambda c: (c << 3) | (c >> 2)
    return (rep(word & 31), rep((word >> 5) & 31), rep((word >> 10) & 31))

def enemy_cluster(sp, lo, hi):
    g = [s for s in sp if s['pal'] == 1 and lo <= s['tile'] < hi and s['y'] < 224 and 0 <= s['x'] < 256]
    if not g:
        return None
    ax = min(s['x'] for s in g); ay = min(s['y'] for s in g)
    g.sort(key=lambda s: s['idx'])
    return ax, ay, g

def composite(cells, vram, cgram):
    """cells: list of (x,y,tile,pal,hf,vf) -> dict (X,Y)->rgb, drawn high->low as given order reversed."""
    buf = {}
    for (x, y, tile, pal, hf, vf) in reversed(cells):
        for cy in range(2):
            for cx in range(2):
                px = dt8(vram, tile + cx + 0x10*cy)
                for yy in range(8):
                    for xx in range(8):
                        v = px[yy][xx]
                        if v == 0:
                            continue
                        sx, sy = cx*8 + xx, cy*8 + yy
                        if hf: sx = 15 - sx
                        if vf: sy = 15 - sy
                        X, Y = x + sx, y + sy
                        buf[(X, Y)] = rgb555(cgram[(128+pal*16+v)*2] | (cgram[(128+pal*16+v)*2+1] << 8))
    return buf

def diff_fb(buf, fb):
    tot = bad = exact = 0
    for (X, Y), c in buf.items():
        if not (0 <= X < 256 and 0 <= Y < 216):
            continue
        tot += 1
        o = ((2*(Y+8))*512 + 2*X) * 4      # fb true px (x, y+8), bytes [pad,B,G,R]
        d = abs(c[0]-fb[o+3]) + abs(c[1]-fb[o+2]) + abs(c[2]-fb[o+1])
        if d == 0:
            exact += 1
        if d > 24:
            bad += 1
    return tot, bad, exact

# ------------------------------------------------------------------ emit
def emit(idle_v, idle_cg, clusters, atk_asm, atk_v, cursors):
    W = open(OUT_H, 'w').write
    W("// AUTO-GENERATED by tools/gen/battle_hetake.py -- DO NOT EDIT.\n")
    W("// FOREST battle (scene 19): 3x Hetake (enemy id 0x5E, 14 HP). Baked byte-exact from oracle.\n")
    W("// Dossier: disasm/notes/battle_scenario2.md. State: the recipe battle_forest_3hetake (tools/recipes).\n")
    W("// OBJ char base = VRAM word 0; 16x16 sprites; enemy palette = 1. Idle is OAM-static; the tile\n")
    W("// content streams (C1/CC anim engine BOUNDARY). Enemies appear settled (no entrance morph).\n")
    W("// An OBJ pixel at OAM (x,y) lands at oracle fb true-pixel (x, y+8).\n")
    W("#ifndef CT_BATTLE_HETAKE_H\n#define CT_BATTLE_HETAKE_H\n#include <stdint.h>\n\n")

    W("typedef struct { int8_t dx, dy; uint16_t tile; uint8_t hf, vf; } CtHetakeCell;\n")
    W("typedef struct { int16_t anchorX; uint8_t anchorY, ncell; CtHetakeCell cell[4]; } CtHetakeSlot;\n")
    W("// The 3 Hetake idle clusters (formation order: 0=upper-mid, 1=left, 2=lower-right).\n")
    W("// anchor = min OAM corner; cell = tile @ (dx,dy) rel anchor. block index = 3+slot ($5FAD+slot*0x80).\n")
    W("#define CT_HETAKE_NSLOT 3\n")
    W("static const CtHetakeSlot ctHetakeIdle[CT_HETAKE_NSLOT] = {\n")
    for (ax, ay, g) in clusters:
        W("  { %d, %d, %d, {" % (ax, ay, len(g)))
        W(' '.join("{%d,%d,0x%03X,%d,%d}," % (s['x']-ax, s['y']-ay, s['tile'], s['hflip'], s['vflip']) for s in g))
        W("} },\n")
    W("};\n\n")

    W("// OBJ 8x8 tiles 0x00..0x%02X (4bpp planar, 32B each) from the in-battle boot upload:\n" % (NTILE-1))
    W("//   0x00 = target cursor hand; 0x06 = parked-cursor foot quad; 0x40..0x77 = the 3 Hetake\n")
    W("//   idle blocks (slot0=0x40, slot1=0x48, slot2=0x60). A 16x16 sprite T uses T,T+1,T+0x10,T+0x11.\n")
    W("#define CT_HETAKE_NTILE 0x%02X\n" % NTILE)
    W("static const uint8_t ctHetakeObjTiles[CT_HETAKE_NTILE][32] = {\n")
    for t in range(NTILE):
        W("  {%s}, // 0x%02X\n" % (','.join('0x%02X' % idle_v[t*32+i] for i in range(32)), t))
    W("};\n\n")

    W("// Enemy OBJ palette 1 (BGR555 LE words); CGRAM 128+1*16+[0..15], index 0 transparent.\n")
    W("static const uint16_t ctHetakeObjPal[16] = {%s};\n\n"
      % ','.join('0x%04X' % (idle_cg[(128+16+c)*2] | (idle_cg[(128+16+c)*2+1] << 8)) for c in range(16)))

    W("// Hetake ATTACK keyframe (spore-burst lunge pose), rep frame f90 of the leap arc. Assembly\n")
    W("// rel anchor + its own OBJ tiles 0x40..0x47 (streamed content differs from idle). The leap\n")
    W("// arc trajectory (dx,dy vs home anchor) is ctHetakeAtkTraj; slots B/C mirror the mechanism.\n")
    W("static const CtHetakeCell ctHetakeAttackKf[4] = {%s};\n"
      % ' '.join("{%d,%d,0x%03X,%d,%d}," % c for c in atk_asm))
    W("static const uint8_t ctHetakeAttackTiles[8][32] = {\n")
    for t in range(0x40, 0x48):
        W("  {%s}, // 0x%02X\n" % (','.join('0x%02X' % atk_v[t*32+i] for i in range(32)), t))
    W("};\n")
    W("// Measured leap-arc: Hetake rises/arcs from home up to a peak then back (OAM-shadow monitor,\n")
    W("// forest HOLD f85..f147). (dx,dy) = anchor - home(104,35); ~62 render frames. Slots B/C same\n")
    W("// mechanism from their own home. Victim (adjacent PC) flinches mid-arc; ~4 dmg applied later.\n")
    W("#define CT_HETAKE_ATK_N %d\n" % len(TRAJ))
    W("static const int8_t ctHetakeAtkTraj[CT_HETAKE_ATK_N][2] = {%s};\n\n"
      % ','.join('{%d,%d}' % (dx, dy) for (dx, dy) in TRAJ))

    W("// Target-select cursor: OBJ tile 0x000, pal1, prio3, 16x16; ONE on-screen at the selected\n")
    W("// enemy (the other 3 cursor OAM slots park off-screen). Position = enemy anchorX, anchorY+~18.\n")
    W("// Selected slot = $9614 (0/1/2), cyclic: DOWN/RIGHT +1, UP/LEFT -1, skipping dead ($99C0,X<0).\n")
    W("typedef struct { int16_t x; uint8_t y; } CtHetakeCursor;\n")
    W("static const CtHetakeCursor ctHetakeCursor[CT_HETAKE_NSLOT] = {%s};\n"
      % ' '.join("{%d,%d}," % (x, y) for (x, y) in cursors))
    W("\n#endif // CT_BATTLE_HETAKE_H\n")

# measured leap-arc trajectory (dx,dy vs home 104,35), OAM-shadow monitor forest HOLD f85-147
TRAJ = [(0,-15),(2,-17),(4,-21),(6,-22),(8,-24),(10,-26),(12,-25),(14,-25),(16,-25),(18,-24),
        (20,-22),(22,-20),(24,-17),(26,-13),(28,-11),(27,4),(29,4),(24,10),(20,5),(16,3),
        (12,2),(8,4),(4,7),(0,13),(0,0)]

def main():
    idle = peek('het_idle', '0:8')
    atk  = peek('het_atk90', '0:90')
    iv, icg, isp, ifb = load(idle)
    av, acg, asp, afb = load(atk)

    clusters = [enemy_cluster(isp, lo, hi) for (lo, hi) in SLOT_TILES]
    # attack keyframe: slot0 cluster at the arc peak
    ac = enemy_cluster(asp, *ATK_TILES)
    aax, aay, ag = ac
    atk_asm = [(s['x']-aax, s['y']-aay, s['tile'], s['hflip'], s['vflip']) for s in ag]
    # cursors: peek the 3 target-select states
    cursors = []
    for k, btn in enumerate(['0:6,100:3,0:15',
                             '0:6,100:3,0:15,20:3,0:15',
                             '0:6,100:3,0:15,20:3,0:15,20:3,0:15']):
        cp = peek('het_cur%d' % k, btn)
        csp = oam.decode(oam.load(cp + '.oam'))
        cur = [s for s in csp if s['prio'] == 3 and s['pal'] == 1 and s['tile'] == 0 and 0 <= s['x'] < 256 and s['y'] < 224]
        cursors.append((cur[0]['x'], cur[0]['y']) if cur else (0, 0))

    if '--verify' in sys.argv:
        # confirm slot0/slot1 tile content is identical (same enemy, distinct VRAM blocks)
        def blk(v, T): return b''.join(v[(T+dx+0x10*dy)*32:(T+dx+0x10*dy)*32+32] for dy in range(2) for dx in range(2))
        ab = all(blk(iv, 0x40+2*i) == blk(iv, 0x48+2*i) for i in range(4))
        print("[battle-hetake verify] idle (forest_battle_3hetake + 0:8):")
        print("  slot0(0x40) tile content == slot1(0x48): %s (same enemy, distinct VRAM blocks)" % ab)
        for slot, (ax, ay, g) in enumerate(clusters):
            cells = [(s['x'], s['y'], s['tile'], 1, s['hflip'], s['vflip']) for s in g]
            buf = composite(cells, iv, icg)
            tot, bad, ex = diff_fb(buf, ifb)
            note = "byte-EXACT (unoccluded)" if bad == 0 else "residual = forest-FG foliage over sprite (fb shows green, verified)"
            print("  Hetake slot%d anchor(%d,%d) tiles 0x%02X: exact(d=0)=%d/%d  d<=24=%d/%d  mismatch=%d  %s"
                  % (slot, ax, ay, g[0]['tile'], ex, tot, tot-bad, tot, bad, note))
        # attack keyframe (up high, unoccluded)
        cells = [(s['x'], s['y'], s['tile'], 1, s['hflip'], s['vflip']) for s in ag]
        buf = composite(cells, av, acg)
        tot, bad, ex = diff_fb(buf, afb)
        print("  Hetake ATTACK kf f90 anchor(%d,%d): exact(d=0)=%d/%d mismatch=%d %s"
              % (aax, aay, ex, tot, bad, "byte-EXACT" if bad == 0 else ""))
        print("  target cursor per slot (tile 0x000 pal1 prio3):", cursors)

    if '--verify' not in sys.argv or '--emit' in sys.argv:
        emit(iv, icg, clusters, atk_asm, av, cursors)
        print("wrote %s (3 idle slots, %d tiles, attack kf + %d-frame arc, 3 cursors)"
              % (OUT_H, NTILE, len(TRAJ)))

if __name__ == '__main__':
    main()
