#!/usr/bin/env python3
"""Bake the CT battle ACTION/IDLE animations (Crono attack swing, GATO idle, GATO attack) into
src/data/battle_anim.h + verify vs oracle.

Derivation dossiers: disasm/notes/battle_actionfx.md (Crono attack ADDENDUM A2 / GATO attack ADDENDUM A3)
and disasm/notes/battle_stance.md (idle ADDENDUM 4). All frames are the deterministic peek/monitor
timelines from gato_press_action.state (party Crono seat0 + Marle seat1 vs GATO enemy-slot3):
  - MASH driver ("100:16,0:16" repeated) -> Crono dispatches one physical on GATO (attack swing window
    f447-526) then GATO counters (f596-680).
  - HOLD driver ("100:8,0:N-8": one boot-A, then hold) -> Crono sits ACTIVE in the command menu (never
    attacks) so we get a CLEAN idle window (f384-504) AND an ISOLATED GATO attack (rear-up f505-553).
Both are byte-reproducible per frame (the --peek/--monitor determinism; monitor fN == peek fN, and the
OAM shadow $7E0700[0:512]+$7E0900[0:32] == the live .oam, both proven).

Like the stance/entrance bakes, each combatant animation POSE streams its own OBJ tile CONTENT per frame
(the C1/CC OBJ anim engine + FD:E170 tile streamer, battle_stance.md S4) -- so a byte-exact pose replay
bakes EACH keyframe's own OBJ tiles + OAM assembly. The fine per-frame tile stream WITHIN a pose (the
sword-arc / claw-swipe drawn frame-by-frame) is the documented anim-engine BOUNDARY; the per-frame ANCHOR
trajectory + the pose assemblies are pinned byte-exact here.

Run from repo root. Emits src/data/battle_anim.h. With --verify, reconstructs each baked keyframe over the
acting combatant's bbox and pixel-diffs vs the oracle fb (fb row = OAM y + 8, as the stance bake).
"""
import os, sys, subprocess, re
sys.path.insert(0, 'tools')
from ctlib import oam

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
SP    = os.environ.get('CT_SCRATCH', 'build')
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_anim.h')
NTILE = 0xA0                                    # 8x8 tiles 0x00..0x9F cover Crono(0x08..)/Marle(0x20..)/GATO(0x68..0x9F)
# tile 0x006 (OAM #0-3, pal1-attr, prio3, 4x mirror) = the pulsing white ACTIVE-PC selection BRACKET,
# NOT a combatant sprite ("GATO's foot quad" was a misID -- it sits around CRONO, follows $95D5). It is
# its own asset now (src/data/battle_bracket.h). Excluded from every keyframe here (battle_stance.md T1).
BRACKET_TILE = 0x006

# ---- reference (stance) anchors: animation deltas are relative to each combatant's idle-rest anchor.
CRONO_REF = (109, 108)     # battle_stance.h Crono anchor (= Crono idle-rest in the mash timeline)
GATO_REF  = (104, 62)      # GATO idle-rest anchor in the HOLD timeline (stance bake caught 104,63 at a bob phase)

# ---- keyframes: (driver, pal, name, pose, repFrame, startF, endF). repFrame = mid-dwell (avoids boundary).
KEYS = [
    # Crono attack swing (MASH), battle_actionfx.md A2 -- P0 idle = battle_stance.h (not re-baked here)
    ('mash', 5, 'Crono', 'A1_approach', 450, 447, 452),
    ('mash', 5, 'Crono', 'A2_gather',   455, 453, 457),
    ('mash', 5, 'Crono', 'A3_raise',    465, 458, 472),
    ('mash', 5, 'Crono', 'A4_strike',   485, 473, 499),
    ('mash', 5, 'Crono', 'A5_recoil',   503, 500, 508),
    ('mash', 5, 'Crono', 'A6_settle',   515, 509, 525),
    # GATO idle (HOLD), battle_stance.md ADDENDUM 4 -- two arm states; foot-quad Y-bob is a per-frame table
    ('hold', 1, 'GATO',  'I0_armsRest', 395, 384, 433),
    ('hold', 1, 'GATO',  'I1_armsSwap', 460, 434, 472),
    # GATO attack rear-up (HOLD), battle_actionfx.md A3 -- one lunge pose (claw-swipe streams in tiles)
    ('hold', 1, 'GATO',  'G1_rearup',   520, 505, 553),
]

# ---------------------------------------------------------------- drivers
def mash_bs(F, AON=16, AOFF=16):
    seq = []; n = 0
    while n < F:
        on = min(AON, F - n)
        if on > 0: seq.append("100:%d" % on); n += on
        if n >= F: break
        off = min(AOFF, F - n)
        if off > 0: seq.append("0:%d" % off); n += off
    return ','.join(seq) if seq else "0:%d" % F

def hold_bs(F, K=8):
    return "100:%d" % F if F <= K else "100:%d,0:%d" % (K, F - K)

def bs_for(driver, F):
    return mash_bs(F) if driver == 'mash' else hold_bs(F)

def regen(driver, frame):
    out = os.path.join(SP, 'anim_%s_%d.raw' % (driver, frame))
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', bs_for(driver, frame)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out

_MON = re.compile(r"^\s*f(\d+)\s+000700=([0-9A-Fa-f]+)\s+000900=([0-9A-Fa-f]+)")
def run_monitor(driver, maxF):
    """One oracle run streaming the OAM shadow per frame -> {frame: [sprites]}."""
    p = subprocess.run([os.path.abspath(EXE), ROM, '--monitor', '0700:512,0900:32', '--from', STATE,
                        '--btnscript', bs_for(driver, maxF)], capture_output=True, text=True)
    frames = {}
    for ln in p.stdout.splitlines():
        ln = ln.replace('*', ' ')                          # --monitor marks changed regions with '*'
        m = _MON.match(ln)
        if m: frames[int(m.group(1))] = oam.decode(bytes.fromhex(m.group(2)) + bytes.fromhex(m.group(3)))
    return frames

# ---------------------------------------------------------------- decode helpers
def load(prefix):
    vram = open(prefix + '.vram', 'rb').read()
    cgram = oam.load_cgram(prefix + '.oam')
    sprites = oam.decode(oam.load(prefix + '.oam'))
    fbraw = open(prefix, 'rb').read()
    return vram, cgram, sprites, fbraw

def combatant(sprites, pal):
    on = [s for s in sprites if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == pal
          and s['tile'] != BRACKET_TILE]        # drop the active-PC bracket (own asset, battle_bracket.h)
    on.sort(key=lambda s: s['idx'])
    return on

def bracket_ink(sprites, vram):
    """Screen (X,Y) of every non-transparent tile-0x006 (bracket) pixel -> excluded from combatant verify."""
    mask = set()
    for s in sprites:
        if s['tile'] != BRACKET_TILE or s['y'] >= 210 or not (-60 < s['x'] < 256):
            continue
        for cy in range(2):
            for cx in range(2):
                t = s['tile'] + cx + 0x10*cy; o = (t & 0x1FF) * 32
                for r in range(8):
                    p0, p1, p2, p3 = vram[o+r*2], vram[o+r*2+1], vram[o+16+r*2], vram[o+16+r*2+1]
                    for c in range(8):
                        b = 7 - c
                        v = ((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
                        if v == 0: continue
                        sx, sy = cx*8+c, cy*8+r
                        if s['hflip']: sx = 15-sx
                        if s['vflip']: sy = 15-sy
                        mask.add((s['x']+sx, s['y']+sy))
    return mask

def cluster_sig(sprites, pal):
    on = [s for s in sprites if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == pal]
    if not on: return None, None, None
    ax = min(s['x'] for s in on); ay = min(s['y'] for s in on)
    parts = []
    for s in sorted(on, key=lambda s: s['tile']):
        fl = ('V' if s['vflip'] else '') + ('H' if s['hflip'] else '')
        parts.append("%03X@(%d,%d)%s" % (s['tile'], s['x'] - ax, s['y'] - ay, fl))
    return ax, ay, ' '.join(parts)

def rgb555(word):
    rep = lambda c: (c << 3) | (c >> 2)
    return (rep(word & 31), rep((word >> 5) & 31), rep((word >> 10) & 31))

# ---------------------------------------------------------------- per-frame trajectory tables
def crono_attack_table(mon):
    """(F, dx, dy, poseId) over the Crono attack window, poseId keyed to KEYS A1..A6 (P0 idle=-1)."""
    idle_sig = None
    rows = []
    # map assembly sig -> poseId 0..5 using the KEYS rep frames
    keysig = {}
    for pid, (drv, pal, nm, pose, rf, s0, s1) in enumerate([k for k in KEYS if k[2] == 'Crono']):
        if rf in mon:
            _, _, sg = cluster_sig(mon[rf], 5); keysig[sg] = pid
    for F in range(440, 531):
        if F not in mon: continue
        ax, ay, sg = cluster_sig(mon[F], 5)
        if sg is None: rows.append((F, None, None, -2)); continue
        pid = keysig.get(sg, -1)   # -1 = idle/other (P0 settled stance)
        rows.append((F, ax - CRONO_REF[0], ay - CRONO_REF[1], pid))
    return rows

def gato_idle_table(mon):
    """(F, dx, dy, armState, footDY) over the GATO idle window. armState 0=rest 1=swap; footDY = tile-0x06 dy."""
    rest_sig = swap_sig = None
    for (drv, pal, nm, pose, rf, s0, s1) in KEYS:
        if nm == 'GATO' and pose == 'I0_armsRest' and rf in mon: _, _, rest_sig = cluster_sig(mon[rf], 1)
        if nm == 'GATO' and pose == 'I1_armsSwap' and rf in mon: _, _, swap_sig = cluster_sig(mon[rf], 1)
    def armstate(sprites):
        # tile 0x68 dy: rest -> ~10 (top), swap -> ~48 (bottom). Robust discriminator.
        c = [s for s in sprites if s['tile'] == 0x68 and s['pal'] == 1]
        if not c: return 0
        on = [s for s in sprites if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == 1]
        ay = min(s['y'] for s in on)
        return 1 if (c[0]['y'] - ay) > 25 else 0
    rows = []
    for F in range(384, 505):
        if F not in mon: continue
        on = [s for s in mon[F] if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == 1
              and s['tile'] != BRACKET_TILE]            # GATO body only (bracket is not GATO's)
        if not on: continue
        ax = min(s['x'] for s in on); ay = min(s['y'] for s in on)
        footdy = 0   # GATO has NO foot quad: the old tile-0x06 "foot-bob" was the active-PC BRACKET
                     # measured against a moving GATO anchor (spurious). Neutralized (battle_stance.md T1).
        rows.append((F, ax - GATO_REF[0], ay - GATO_REF[1], armstate(mon[F]), footdy))
    return rows

def crono_bob_table(mon):
    """(F, dx, dy, state) Crono active-idle bob (HOLD). state by (00A dy, 00C dx): 0 rest / 1 down / 2 side."""
    rows = []
    for F in range(400, 505):
        if F not in mon: continue
        on = [s for s in mon[F] if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == 5]
        if not on: continue
        ax = min(s['x'] for s in on); ay = min(s['y'] for s in on)
        a = next((s for s in on if s['tile'] == 0x0A), None)
        c = next((s for s in on if s['tile'] == 0x0C), None)
        ady = (a['y'] - ay) if a else 0; cdx = (c['x'] - ax) if c else 0
        st = 0
        if ady >= 9: st = 1
        elif cdx >= 17: st = 2
        rows.append((F, ax - CRONO_REF[0], ay - CRONO_REF[1], st))
    return rows

# ---------------------------------------------------------------- collect keyframes
def collect():
    frames = {}
    kfs = []; obj_flat = []; tiles_per_kf = []; cgram_ref = None
    for driver, pal, name, pose, rf, s0, s1 in KEYS:
        key = (driver, rf)
        if key not in frames: frames[key] = load(regen(driver, rf))
        vram, cgram, sprites, fbraw = frames[key]
        cgram_ref = cgram
        on = combatant(sprites, pal)
        allspr = [s for s in sprites if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] in (1, 5, 6)
                  and s['tile'] != BRACKET_TILE]         # bracket excluded from occlusion + bake
        ax = min(s['x'] for s in on); ay = min(s['y'] for s in on)
        kfs.append(dict(driver=driver, pal=pal, name=name, pose=pose, rf=rf, s0=s0, s1=s1,
                        objOff=len(obj_flat), nObj=len(on), ax=ax, ay=ay,
                        vram=vram, cgram=cgram, fbraw=fbraw, on=on, allspr=allspr,
                        brk=bracket_ink(sprites, vram)))
        obj_flat.extend(on)
        tiles_per_kf.append([[vram[t*32 + i] for i in range(32)] for t in range(NTILE)])
    return kfs, obj_flat, tiles_per_kf, cgram_ref

# ---------------------------------------------------------------- emit
def emit(kfs, obj_flat, tiles_per_kf, cgram, crono_tab, bob_tab, gidle_tab):
    with open(OUT_H, 'w') as fp:
        W = fp.write
        W("// AUTO-GENERATED by tools/gen/battle_anim.py -- DO NOT EDIT.\n")
        W("// CT battle ACTION/IDLE animations, baked byte-exact from the oracle.\n")
        W("// Dossiers: battle_actionfx.md (Crono attack A2 / GATO attack A3), battle_stance.md (idle ADD.4).\n")
        W("// GATO-fight, Crono seat0 + Marle seat1. Each keyframe = a pose OAM assembly + its OWN OBJ tiles\n")
        W("// (pose tiles stream per frame; the fine within-pose tile stream is the anim-engine BOUNDARY).\n")
        W("// A pose OBJ pixel at OAM (x,y) lands at oracle fb true-pixel (x, y+8) (verified, as the stance).\n")
        W("#ifndef CT_BATTLE_ANIM_H\n#define CT_BATTLE_ANIM_H\n#include <stdint.h>\n\n")

        # flat OBJ list
        W("// Flat OBJ list across all keyframes (OAM-index order per keyframe; composite HIGH oidx first).\n")
        W("typedef struct { uint8_t oidx; int16_t x; uint8_t y; uint16_t tile;\n")
        W("                 uint8_t pal, prio, hf, vf; } CtAnimObj;\n")
        W("#define CT_ANIM_NOBJ %d\n" % len(obj_flat))
        W("static const CtAnimObj ctBattleAnimObj[CT_ANIM_NOBJ] = {\n")
        for s in obj_flat:
            W("  { %3d, %4d, %3d, 0x%03X, %d, %d, %d, %d },\n"
              % (s['idx'], s['x'], s['y'], s['tile'], s['pal'], s['prio'], s['hflip'], s['vflip']))
        W("};\n\n")

        # keyframe schedule
        W("// Keyframe schedule. startF..endF = the pose's render-frame dwell. tileKf indexes ctBattleAnimTiles.\n")
        W("// anchorX/Y = the pose's OAM min-corner (absolute, in the capture); refX/refY = the combatant's\n")
        W("// idle-rest anchor (Crono %d,%d / GATO %d,%d) so (anchorX-refX,anchorY-refY) = the pose delta.\n"
          % (CRONO_REF[0], CRONO_REF[1], GATO_REF[0], GATO_REF[1]))
        W("typedef struct { const char* name; const char* pose; uint8_t pal;\n")
        W("                 uint16_t startF, endF; uint8_t objOff, nObj, tileKf;\n")
        W("                 int16_t anchorX; uint8_t anchorY; } CtAnimKeyframe;\n")
        W("#define CT_ANIM_NKF %d\n" % len(kfs))
        W("static const CtAnimKeyframe ctBattleAnimKf[CT_ANIM_NKF] = {\n")
        for i, kf in enumerate(kfs):
            W('  { "%s", "%s", %d, %d, %d, %d, %d, %d, %4d, %3d },\n'
              % (kf['name'], kf['pose'], kf['pal'], kf['s0'], kf['s1'], kf['objOff'], kf['nObj'], i, kf['ax'], kf['ay']))
        W("};\n\n")

        # per-keyframe tiles
        W("// Per-keyframe OBJ 8x8 tiles 0x00..0x%02X (4bpp planar, 32 B) -- streamed per pose.\n" % (NTILE-1))
        W("// 16x16 sprite of tile T uses 8x8 tiles T, T+1, T+0x10, T+0x11.\n")
        W("#define CT_ANIM_NTILE 0x%02X\n" % NTILE)
        W("static const uint8_t ctBattleAnimTiles[CT_ANIM_NKF][CT_ANIM_NTILE][32] = {\n")
        for i, kf in enumerate(kfs):
            W("  { // kf%d %s %s\n" % (i, kf['name'], kf['pose']))
            for t in range(NTILE):
                W("    {%s},\n" % ','.join('0x%02X' % b for b in tiles_per_kf[i][t]))
            W("  },\n")
        W("};\n\n")

        # palettes
        W("// OBJ CGRAM palettes (BGR555 LE). OBJ palette P = CGRAM 128+P*16+[0..15]; idx0 transparent.\n")
        W("static const uint16_t ctBattleAnimPal[8][16] = {\n")
        for p in range(8):
            vals = [cgram[(128+p*16+c)*2] | (cgram[(128+p*16+c)*2+1] << 8) for c in range(16)]
            used = ' USED' if p in (1, 5, 6) else ''
            W("  {%s}, // pal%d%s\n" % (','.join('0x%04X' % v for v in vals), p, used))
        W("};\n\n")

        # Crono attack per-frame trajectory
        W("// ---- Crono ATTACK per-frame trajectory (MASH timeline f440..530). dx,dy = anchor - (%d,%d).\n"
          % (CRONO_REF[0], CRONO_REF[1]))
        W("// poseId: -1=idle(battle_stance.h P0) 0=A1_approach 1=A2_gather 2=A3_raise 3=A4_strike\n")
        W("//         4=A5_recoil 5=A6_settle  (indexes ctBattleAnimKf's Crono entries).\n")
        W("typedef struct { int16_t dx; int8_t dy; int8_t poseId; } CtAnimStep;\n")
        W("#define CT_CRONO_ATK_F0 440\n")
        W("#define CT_CRONO_ATK_N %d\n" % len(crono_tab))
        W("static const CtAnimStep ctCronoAttackTraj[CT_CRONO_ATK_N] = {\n")
        for (F, dx, dy, pid) in crono_tab:
            W("  { %4d, %4d, %2d }, // f%d\n" % (dx if dx is not None else 0, dy if dy is not None else 0, pid, F))
        W("};\n\n")

        # GATO idle per-frame
        W("// ---- GATO IDLE per-frame (HOLD timeline f384..504). dx,dy = anchor - (%d,%d).\n" % (GATO_REF[0], GATO_REF[1]))
        W("// arm: 0=I0_armsRest 1=I1_armsSwap (the claw up/down swap). footDY = 0 (DEPRECATED: the old\n")
        W("// tile-0x06 'foot-bob' was the active-PC BRACKET, not GATO -- now its own asset battle_bracket.h).\n")
        W("// GATO's real idle = the claw swap + body-tile stream. One observed cycle (GATO acts at f505).\n")
        W("typedef struct { int16_t dx; int8_t dy; uint8_t arm; uint8_t footDY; } CtGatoIdleStep;\n")
        W("#define CT_GATO_IDLE_F0 384\n")
        W("#define CT_GATO_IDLE_N %d\n" % len(gidle_tab))
        W("static const CtGatoIdleStep ctGatoIdleTraj[CT_GATO_IDLE_N] = {\n")
        for (F, dx, dy, arm, footdy) in gidle_tab:
            W("  { %4d, %3d, %d, %3d }, // f%d\n" % (dx, dy, arm, footdy, F))
        W("};\n\n")

        # Crono idle bob
        W("// ---- Crono ACTIVE-PC idle bob (HOLD f400..504). dx,dy = anchor - (%d,%d). state 0=rest 1=down 2=side.\n"
          % (CRONO_REF[0], CRONO_REF[1]))
        W("// A subtle +-1..2px 3-state micro-oscillation (irregular cadence -- observed sequence, anim-slot\n")
        W("// phase-driven; only ~2 sub-tiles stream). The WAITING PC (Marle) is static (no bob).\n")
        W("typedef struct { int16_t dx; int8_t dy; uint8_t state; } CtCronoBobStep;\n")
        W("#define CT_CRONO_BOB_F0 400\n")
        W("#define CT_CRONO_BOB_N %d\n" % len(bob_tab))
        W("static const CtCronoBobStep ctCronoIdleBob[CT_CRONO_BOB_N] = {\n")
        for (F, dx, dy, st) in bob_tab:
            W("  { %4d, %3d, %d }, // f%d\n" % (dx, dy, st, F))
        W("};\n\n")

        W("#endif // CT_BATTLE_ANIM_H\n")

# ---------------------------------------------------------------- verify
def _plot(buf, owner, s, tb_provider, cgram):
    """Composite one 16x16 sprite into buf/owner. tb_provider(tile)->32B tile block."""
    for cy in range(2):
        for cx in range(2):
            t = s['tile'] + cx + 0x10*cy
            tb = tb_provider(t)
            for r in range(8):
                p0, p1, p2, p3 = tb[r*2], tb[r*2+1], tb[16+r*2], tb[16+r*2+1]
                for c in range(8):
                    b = 7 - c
                    v = ((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
                    if v == 0: continue
                    sx, sy = cx*8+c, cy*8+r
                    if s['hflip']: sx = 15-sx
                    if s['vflip']: sy = 15-sy
                    X, Y = s['x']+sx, s['y']+sy
                    if 0 <= X < 256 and 0 <= Y < 224:
                        buf[Y][X] = rgb555(cgram[(128+s['pal']*16+v)*2] | (cgram[(128+s['pal']*16+v)*2+1] << 8))
                        owner[Y][X] = s['pal']

def verify(kfs, tiles_per_kf):
    # Composite ALL combatants (acting one from BAKED tiles, others from raw VRAM for correct occlusion),
    # then score only pixels the acting combatant OWNS on top -- the cross-combatant-overlap-safe method.
    total_ok = total = 0
    print("[battle-anim verify] per keyframe: acting combatant reconstructed from BAKED tiles, overlap-safe")
    print("  (all combatants composited by OAM idx; scored where the actor is top). d>24 = mismatch.")
    for i, kf in enumerate(kfs):
        cgram = kf['cgram']; fbraw = kf['fbraw']; vram = kf['vram']; pal = kf['pal']
        baked = tiles_per_kf[i]
        def raw_tb(t): o = (t & 0x1FF) * 32; return vram[o:o+32]
        def baked_tb(t): return baked[t] if (t & 0x1FF) < NTILE else bytes(32)
        buf = [[None]*256 for _ in range(224)]; owner = [[None]*256 for _ in range(224)]
        # draw high OAM idx first so idx0 ends on top; acting combatant uses baked tiles, others use VRAM
        for s in sorted(kf['allspr'], key=lambda s: -s['idx']):
            _plot(buf, owner, s, baked_tb if s['pal'] == pal else raw_tb, cgram)
        act = kf['on']; x0 = min(s['x'] for s in act); x1 = max(s['x'] for s in act)+15
        y0 = min(s['y'] for s in act); y1 = max(s['y'] for s in act)+15
        brk = kf['brk']                                    # bracket-owned pixels -> its own asset, EXCLUDE
        d = tot = excl = 0
        for y in range(max(0, y0), min(224, y1+1)):
            for x in range(max(0, x0), min(256, x1+1)):
                if owner[y][x] != pal: continue
                if (x, y) in brk: excl += 1; continue
                c = buf[y][x]; tot += 1
                o = ((2*(y+8))*512 + 2*x)*4
                if abs(c[0]-fbraw[o+3])+abs(c[1]-fbraw[o+2])+abs(c[2]-fbraw[o+1]) > 24: d += 1
        total_ok += tot-d; total += tot
        print("  %-5s %-11s (%s f%d)  bbox x[%d..%d] y[%d..%d]  exact=%d/%d  mismatch=%d  [brkExcl=%d]"
              % (kf['name'], kf['pose'], kf['driver'], kf['rf'], x0, x1, y0, y1, tot-d, tot, d, excl))
    print("  TOTAL exact=%d/%d  (%.2f%%)" % (total_ok, total, 100.0*total_ok/total))

def main():
    kfs, obj_flat, tiles_per_kf, cgram = collect()
    if '--verify' in sys.argv:
        verify(kfs, tiles_per_kf)
    if '--verify' not in sys.argv or '--emit' in sys.argv:
        mash = run_monitor('mash', 533)
        hold = run_monitor('hold', 507)
        crono_tab = crono_attack_table(mash)
        bob_tab = crono_bob_table(hold)
        gidle_tab = gato_idle_table(hold)
        emit(kfs, obj_flat, tiles_per_kf, cgram, crono_tab, bob_tab, gidle_tab)
        print("wrote %s (%d keyframes, %d OBJ; traj Crono=%d GATOidle=%d bob=%d)"
              % (OUT_H, len(kfs), len(obj_flat), len(crono_tab), len(gidle_tab), len(bob_tab)))

if __name__ == '__main__':
    main()
