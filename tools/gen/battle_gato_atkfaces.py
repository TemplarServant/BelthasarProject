#!/usr/bin/env python3
"""GATO directional ATTACK-pose bake (disasm/notes/battle_animeng_corpus.md CAPTURE 1) ->
src/data/battle_gato_atkfaces.h.

The corpus proved GATO's ATTACK is fully directional: FOUR byte-distinct rear-up + strike pose
sets (front/left/right/up) selected by his walk-arrival facing $96DE, exactly like his idle/walk
facings. Same tile NUMBERS (0x68-0x8E), hf=vf=0 everywhere; the direction is the OAM assembly +
streamed tile CONTENT (all VRAM-distinct). This bake carries the THREE off-front facings
(UP/LEFT/RIGHT); FRONT (facing 1) stays the shipped kf8 rear-up + ctGatoSwipeKf GS stream (front
strike is a tile-stream on a FIXED OAM; the side strikes REARRANGE the OAM -- LEFT lunges the claw
-34px, RIGHT extends it +34px -- so each side carries its OWN strike assembly + tiles, not just a
stream, corpus 1.2).

Rep frames (corpus 1.1 rear-up / 1.2 strike), all byte-verified against the dossier assemblies
(verify_atk.py, anchors 110,56 / 102,37 / 86,37 / 128,37 / 128,37 / 110,27 all OK):
    UP    (facing 0)  rear-up  U_f185                    (no side-strike capture -> hold rear-up)

The corpus took UP at f195 of the capture gato_press_action.state. From the recipe (the rule's cartridge) GATO's UP
rear-up -- the ROM's assembly 0x11, which field.c's animengcheck-atk gathers -- holds f175-f194 at anchor (110,27) and
f195 is already the next frame of his stream (tiles 0x6C/0x6F/0x7C/0x7F): the capture's own lag frames had put its
dwell a frame or more later. f185 is the middle of the dwell. The LEFT/RIGHT frames are inside the recipe's dwells as
they are (L rear-up f100-f119, L strike f120-f125, R the capture's bytes).
    LEFT  (facing 2)  rear-up  L_f108   strike  L_f122
    RIGHT (facing 3)  rear-up  R_f153   strike  R_f166

Cells stored RELATIVE TO GATO'S LIVE FOOT ($1D0C+6/$1D23+6 from the capture .wram), drawn at the
render-side live foot verbatim -- position-independent, the same convention as battle_gato_faces.h.
strikeAt = rel (rear-up onset) at which the pose flips rear-up -> strike, from the corpus 1.3
timeline (rear-up onset == our actStart rel 0; LEFT rear-up f99-118 => strike onset rel 20; RIGHT
rear-up f143-163 => rel 21). The strike beat CT_GATO_STRIKE=27 then lands ~7f into the strike pose,
matching the corpus "strike lands mid-rear-up, flash 7f later" (CAPTURE 4). These become FIXTURES
for the future anim-engine port (one rep frame per phase; the within-pose tile stream stays the
C1/CC streamer boundary).

Run from repo root:  CT_SCRATCH=<scratchpad>/corpus python tools/gen/battle_gato_atkfaces.py
"""
import os, sys, hashlib, subprocess
sys.path.insert(0, 'tools')
from ctlib import oam

# The corpus captures are made here (battle_animeng_corpus.md "Repro"): gato_mid = the GATO fight 400 frames in
# (STATE + "100:8,0:392" + PEEK_SAVE, battle_gato_idle_victory.md), then Crono moved beside / above GATO and Marle
# killed (curHP $5EB0 = 0, $5ECA = 80) by a PEEK_POKE as it loads, and the fight run N frames: GATO walks to Crono
# and attacks him facing that way.
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
SP = os.environ.get('CT_SCRATCH', 'build')
EXE = os.path.abspath(os.environ.get('CT_EXE', 'build/ct.exe'))
CRONO_AT = {'L': (96, 111), 'R': (180, 111), 'U': (136, 55)}     # crono_left / crono_right / crono_up

_MADE = set()

def capture(prefix):
    """<SP>/<prefix>.raw(.vram/.oam/.wram): prefix = <L|R|U>_f<N>."""
    side, n = prefix.split('_f')
    x, y = CRONO_AT[side]
    mid = os.path.join(SP, 'gato_mid.state')
    if mid not in _MADE:                                   # made once per run, from this run's STATE
        _MADE.add(mid)
        subprocess.run([EXE, '--peek', STATE, os.path.join(SP, '_gato_mid.raw'), '--btnscript', '100:8,0:392'],
                       env=dict(os.environ, PEEK_SAVE=mid), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    poke = '1D0C=%02X,1D23=%02X,5EB0=00,5EB1=00,5ECA=80' % (x, y)
    subprocess.run([EXE, '--peek', mid, os.path.join(SP, prefix + '.raw'), '--btnscript', '0:%s' % n],
                   env=dict(os.environ, PEEK_POKE=poke), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_gato_atkfaces.h')
NTILE = 0xA0
BRACKET_TILE = 0x006
GATO_BODY = {0x68, 0x6A, 0x6C, 0x6E, 0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C, 0x8E}

def load(prefix):
    capture(prefix)
    vram = open(os.path.join(SP, prefix + '.raw.vram'), 'rb').read()
    spr  = oam.decode(oam.load(os.path.join(SP, prefix + '.raw.oam')))
    wram = open(os.path.join(SP, prefix + '.raw.wram'), 'rb').read()
    return vram, spr, wram

def gato_cells(sprites):
    on = [s for s in sprites if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == 1
          and s['tile'] in GATO_BODY and s['tile'] != BRACKET_TILE]
    on.sort(key=lambda s: s['idx'])
    return on

def foot(wram):
    return wram[0x1D0C + 6], wram[0x1D23 + 6]      # GATO = cmb 6 (enemy slot 3): $1D12 / $1D29

def bodymd5(vram):
    body = bytearray()
    for t in sorted(GATO_BODY):
        for sub in (t, t + 1, t + 0x10, t + 0x11):
            body += bytes(vram[(sub & 0x1FF) * 32:(sub & 0x1FF) * 32 + 32])
    return hashlib.md5(bytes(body)).hexdigest()[:8]

# ---- capture set: (name, facing, strikeAt, rear-prefix, expected rear anchor, strike-prefix, expected strike anchor)
#      strike-prefix None => no side-strike capture (UP) -> strike phase reuses the rear-up pose+sheet.
CAPS = [
    ('GA0_up',    0, 49, 'U_f185', (110, 27), None,     None),        # UP: no PC sits above in the GATO formation
    ('GA2_left',  2, 20, 'L_f108', (102, 37), 'L_f122', (86, 37)),    # LEFT strike lunges the claw -34px
    ('GA3_right', 3, 21, 'R_f153', (128, 37), 'R_f166', (128, 37)),   # RIGHT strike extends the claw +34px
]

# ---- decode + bake ----
poses = []   # flat list of (label, rel_cells[list of (rx,ry,tile)], tiles[NTILE][32], md5)
faces = []   # (name, facing, strikeAt, rearIdx, strikeIdx)  indices into poses
def bake_pose(label, prefix, exp_anchor):
    vram, spr, wram = load(prefix)
    cells = gato_cells(spr)
    fx, fy = foot(wram)
    ax = min(s['x'] for s in cells); ay = min(s['y'] for s in cells)
    assert (ax, ay) == exp_anchor, "%s anchor (%d,%d) != dossier %s" % (label, ax, ay, exp_anchor)
    assert len(cells) == 12, "%s: %d cells (want 12)" % (label, len(cells))
    assert all(not (s['hflip'] or s['vflip']) for s in cells), "%s: hf/vf must be 0 (corpus 1.1)" % label
    rel = [(s['x'] - fx, s['y'] - fy, s['tile']) for s in cells]
    tiles = [[vram[t * 32 + i] for i in range(32)] for t in range(NTILE)]
    md5 = bodymd5(vram)
    poses.append((label, rel, tiles, md5))
    print("%-12s %s foot=(%d,%d) anchor=(%d,%d) cells=%d md5=%s"
          % (label, prefix, fx, fy, ax, ay, len(cells), md5))
    return len(poses) - 1

for name, fc, sat, rp, ra, spx, sa in CAPS:
    ri = bake_pose(name + '_rear', rp, ra)
    si = bake_pose(name + '_strike', spx, sa) if spx else ri     # UP reuses rear for the strike phase
    faces.append((name, fc, sat, ri, si))

md5s = [p[3] for p in poses]
assert len(set(md5s)) == len(md5s), "attack pose VRAM must be DISTINCT (corpus 1.1/1.2): %s" % md5s

# ---- emit ----
with open(OUT_H, 'w') as f:
    W = f.write
    W("// GENERATED by tools/gen/battle_gato_atkfaces.py -- do not edit. GATO's directional ATTACK poses\n")
    W("// (disasm/notes/battle_animeng_corpus.md CAPTURE 1): the four-facing attack is a rear-up +\n")
    W("// strike per facing, selected by $96DE (mvFacing[3]) exactly like the walk/idle facings. This\n")
    W("// carries UP/LEFT/RIGHT; FRONT (facing 1) keeps the shipped kf8 rear-up + ctGatoSwipeKf GS\n")
    W("// stream (front strike = tile-stream on a FIXED OAM). The SIDE strikes rearrange the OAM (the\n")
    W("// claw cell 0x86 lunges: LEFT -34px, RIGHT +34px), so each carries its own strike assembly +\n")
    W("// tiles. UP has no side-strike capture (no PC above GATO in this formation) -> its strike phase\n")
    W("// holds the rear-up. Cells RELATIVE TO GATO'S LIVE FOOT ($1D12/$1D29); hf=vf=0 (corpus 1.1).\n")
    W("// strikeAt = rel (rear-up onset) at which rear-up flips to strike (corpus 1.3). All poses\n")
    W("// byte-verified vs the dossier assemblies. FIXTURE for the future anim-engine port (one rep\n")
    W("// frame per phase; the within-pose tile stream is the C1/CC streamer boundary).\n")
    W("#ifndef CT_BATTLE_GATO_ATKFACES_H\n#define CT_BATTLE_GATO_ATKFACES_H\n#include <stdint.h>\n\n")
    W("typedef struct { int16_t relX, relY; uint16_t tile; } CtGatoAtkCell;\n")
    W("typedef struct { const char* name; uint8_t facing; uint8_t strikeAt;\n")
    W("                 uint16_t rearOff; uint8_t rearN; uint8_t rearTile;\n")
    W("                 uint16_t strikeOff; uint8_t strikeN; uint8_t strikeTile; } CtGatoAtkFace;\n\n")
    # cells
    W("static const CtGatoAtkCell ctGatoAtkCell[] = {\n")
    offs = []
    off = 0
    for label, rel, tiles, md5 in poses:
        W("  // %s (md5 %s)\n" % (label, md5))
        for (rx, ry, t) in rel:
            W("  { %4d, %4d, 0x%03X },\n" % (rx, ry, t))
        offs.append((off, len(rel)))
        off += len(rel)
    W("};\n\n")
    # faces: rearTile/strikeTile are the poses[] index == the tilesheet index
    W("#define CT_GATO_ATK_NFACE %d\n" % len(faces))
    W("static const CtGatoAtkFace ctGatoAtkFace[CT_GATO_ATK_NFACE] = {\n")
    for name, fc, sat, ri, si in faces:
        ro, rn = offs[ri]; so, sn = offs[si]
        W("  { \"%s\", %d, %d, %d, %d, %d, %d, %d, %d },\n"
          % (name, fc, sat, ro, rn, ri, so, sn, si))
    W("};\n\n")
    # tile sheets (one per pose)
    W("#define CT_GATO_ATK_NTILE 0x%X\n" % NTILE)
    for k, (label, rel, tiles, md5) in enumerate(poses):
        W("// %s tile sheet (capture VRAM OBJ half, body md5 %s)\n" % (label, md5))
        W("static const uint8_t ctGatoAtkTiles%d[CT_GATO_ATK_NTILE][32] = {\n" % k)
        for t in range(NTILE):
            W("  {" + ",".join("0x%02X" % b for b in tiles[t]) + "},\n")
        W("};\n")
    W("static const uint8_t (*const ctGatoAtkTilesTab[%d])[32] = { %s };\n"
      % (len(poses), ", ".join("ctGatoAtkTiles%d" % k for k in range(len(poses)))))
    W("#endif\n")
print("wrote", OUT_H, "(%d poses, %d faces)" % (len(poses), len(faces)))
