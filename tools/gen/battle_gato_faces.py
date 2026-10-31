#!/usr/bin/env python3
"""GATO directional-facing bake (disasm/notes/battle_pc_approach_facing.md PART 2) -> src/data/battle_gato_faces.h.

GATO has four distinct directional pose sets (up/down/left/right; not h-flips, dossier 2.4) selected
per frame by the facing byte $96DE+slot = ctBattleFaceTab[walk bearing] (C1/37E9, table in battle_trig.h).
Down (front, facing 1) is the idle bake (battle_anim.h kf6/7 + ctGatoIdleTrajV2, dossier 2.6) and is not
baked here; this bakes one representative frame for each of the other three:

    UP    (facing 0)  gm_60      (the up-return walk, gm_* sidecar set)
    LEFT  (facing 2)  facing/left    (HOLD fight, GATO walks left toward Marle, f739-771)
    RIGHT (facing 3)  facing/right   (same fight, walks right, f1018-1050)

Each is its OAM assembly (pal1 body cells) + streamed tile content, cells stored relative to the live foot
($1D0C+6/$1D23+6 from the capture's .wram sidecar) so the renderer draws them at GATO's live foot in any
fight. Coordinates: oracle fbRow = OAM y + 8 (checked on _gidle400: pal1 ink top at OAM y 60, fb row 68) and
the port's render rows = oracle - 8, so the port's fb space is OAM space, as the idle render's GATO_REF
(104,62) assumes: rel = cellOAM - footWRAM. Not baked: the within-facing walk-cycle tile stream (dossier P4).

Run from repo root:  python tools/gen/battle_gato_faces.py   (CT_GEN_STATE / CT_SCRATCH override the state / scratch dir)

The captures are made here from the recipe battle_gato_press_action (tools/recipes/): its hold fight (one A boots the
battle, then nothing -- GATO acts un-hit and walks, battle_pc_approach_facing.md 2.3) passes through every facing.
The frames (peek frames from the state, "100:8,0:N-8"): the idle control _gidle400 = f400; left = f749 (the walk
toward Marle, f739-779); up = f813 (the return walk up -- the same cells and tile sheet as the corpus' gm_60, from
another capture of the return walk); right = f902. They give the same cells and OBJ sheet 0x00-0x9F as the corpus
captures above, from gato_press_action.state and from the recipe alike.
"""
import os, sys, hashlib, subprocess
sys.path.insert(0, 'tools')
from ctlib import oam

STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
SP = os.environ.get('CT_SCRATCH', 'build')
EXE = os.path.abspath(os.environ.get('CT_EXE', 'build/ct.exe'))
HOLD_FRAME = {'_gidle400': 400, 'gm_60': 813, 'left': 749, 'right': 902}

def capture(prefix):
    n = HOLD_FRAME[os.path.basename(prefix)]
    os.makedirs(os.path.dirname(prefix) or '.', exist_ok=True)
    subprocess.run([EXE, '--peek', STATE, prefix + '.raw', '--btnscript', '100:8,0:%d' % (n - 8)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_gato_faces.h')
NTILE = 0xA0
BRACKET_TILE = 0x006

def load(prefix):
    capture(prefix)
    vram = open(prefix + '.raw.vram', 'rb').read()
    spr  = oam.decode(oam.load(prefix + '.raw.oam'))
    wram = open(prefix + '.raw.wram', 'rb').read()
    return vram, spr, wram

def gato_cells(sprites):
    on = [s for s in sprites if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == 1
          and s['tile'] != BRACKET_TILE]
    on.sort(key=lambda s: s['idx'])
    return on

def foot(wram):
    return wram[0x1D0C + 6], wram[0x1D23 + 6]      # GATO = cmb 6 (enemy slot 3)

# ---- 1. check the conventions on the at-rest idle control capture ----
vram_i, spr_i, wram_i = load(os.path.join(SP, '_gidle400'))
cells_i = gato_cells(spr_i)
fx, fy = foot(wram_i)
assert (fx, fy) == (136, 111), "idle control capture not at the staged home foot (136,111)"
minx = min(s['x'] for s in cells_i); miny = min(s['y'] for s in cells_i)
assert minx - fx == -32, "idle X extent %d != the A8.1 -32" % (minx - fx)
assert -56 <= miny - fy <= -50, "idle Y rel %d outside the observed idle band" % (miny - fy)
print("convention pin OK: at-rest idle foot=(136,111) min-corner=(%d,%d) rel=(%d,%d)"
      % (minx, miny, minx - fx, miny - fy))

# ---- 2. bake the three facings ----
FACES = [('GF0_up', 0, os.path.join(SP, 'gm_60')),
         ('GF2_left', 2, os.path.join(SP, 'facing', 'left')),
         ('GF3_right', 3, os.path.join(SP, 'facing', 'right'))]
baked = []
for name, fc, prefix in FACES:
    vram, spr, wram = load(prefix)
    cells = gato_cells(spr)
    lx, ly = foot(wram)
    body = bytearray()
    for t in sorted({c['tile'] for c in cells}):
        for sub in (t, t + 1, t + 0x10, t + 0x11):
            body += bytes(vram[(sub & 0x1FF) * 32:(sub & 0x1FF) * 32 + 32])
    md5 = hashlib.md5(bytes(body)).hexdigest()[:8]
    flips = sum(1 for c in cells if c['hflip'] or c['vflip'])
    tiles = [[vram[t * 32 + i] for i in range(32)] for t in range(NTILE)]
    rel = [(c['x'] - lx, c['y'] - ly, c['tile'], c['hflip'], c['vflip']) for c in cells]
    baked.append((name, fc, rel, tiles, md5))
    print("%s: %d cells foot=(%d,%d) tiles=%s flips=%d bodymd5=%s"
          % (name, len(cells), lx, ly,
             '/'.join('%02X' % c['tile'] for c in cells), flips, md5))
    assert flips == 0, "dossier 2.4: hf=vf=0 in every facing"
assert len({b[4] for b in baked}) == len(baked), "facing tile content must be DISTINCT (2.4)"

# ---- 3. emit ----
with open(OUT_H, 'w') as f:
    W = f.write
    W("// GENERATED by tools/gen/battle_gato_faces.py -- do not edit. GATO's directional facing pose bakes\n")
    W("// (disasm/notes/battle_pc_approach_facing.md PART 2): UP/LEFT/RIGHT rep frames from the oracle\n")
    W("// captures (gm_60 up-walk; facing/left, facing/right HOLD-fight walks). DOWN (facing 1) = the\n")
    W("// committed idle bake (battle_anim.h kf6/7), not duplicated here. Cells are RELATIVE TO GATO'S\n")
    W("// LIVE FOOT ($1D0C+6/$1D23+6 at capture; fb-row convention pinned against CT_EXT_GATO). Four\n")
    W("// distinct tile sets -- LEFT/RIGHT are NOT h-flips (2.4); hf=vf=0 throughout. The within-facing\n")
    W("// walk-cycle tile stream is the dossier P4 OPEN boundary (one rep frame per facing).\n")
    W("#ifndef CT_BATTLE_GATO_FACES_H\n#define CT_BATTLE_GATO_FACES_H\n#include <stdint.h>\n\n")
    W("typedef struct { int16_t relX, relY; uint16_t tile; } CtGatoFaceCell;\n")
    W("typedef struct { const char* pose; uint8_t facing; uint8_t cellOff, nCell; } CtGatoFaceKf;\n\n")
    ncell = 0
    W("static const CtGatoFaceCell ctGatoFaceCell[] = {\n")
    for name, fc, rel, tiles, md5 in baked:
        W("  // %s (facing %d)\n" % (name, fc))
        for (rx, ry, t, hf, vf) in rel:
            W("  { %4d, %4d, 0x%03X },\n" % (rx, ry, t))
        ncell += len(rel)
    W("};\n\n")
    W("static const CtGatoFaceKf ctGatoFaceKf[3] = {\n")
    off = 0
    for name, fc, rel, tiles, md5 in baked:
        W("  { \"%s\", %d, %d, %d },\n" % (name, fc, off, len(rel)))
        off += len(rel)
    W("};\n\n")
    W("#define CT_GATO_FACE_NTILE 0x%X\n" % NTILE)
    for k, (name, fc, rel, tiles, md5) in enumerate(baked):
        W("// %s tile sheet (capture VRAM OBJ half, body md5 %s)\n" % (name, md5))
        W("static const uint8_t ctGatoFaceTiles%d[CT_GATO_FACE_NTILE][32] = {\n" % k)
        for t in range(NTILE):
            W("  {" + ",".join("0x%02X" % b for b in tiles[t]) + "},\n")
        W("};\n")
    W("static const uint8_t (*const ctGatoFaceTilesTab[3])[32] = { ctGatoFaceTiles0, ctGatoFaceTiles1, ctGatoFaceTiles2 };\n")
    W("#endif\n")
print("wrote", OUT_H, "(%d cells)" % ncell)
