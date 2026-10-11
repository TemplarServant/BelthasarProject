#!/usr/bin/env python3
"""ADDITIVE companion bake to src/data/battle_anim.h -- the sprite/anim COMPLETENESS pass (T1/T2/T3).
Emits src/data/battle_anim_add.h; does NOT touch battle_anim.h (existing arrays
stay byte-identical). Kept separate because tools/gen/battle_anim.py is concurrently owned.

Dossiers: battle_stance.md ADD.5 (T1 Crono idle tile-stream, T3 GATO idle re-derive),
          battle_actionfx.md ADD.A4 (T2 GATO claw-swipe tile-stream).
Base state gato_press_action.state (Crono seat0 + Marle seat1 vs GATO). tile 0x006 = the active-PC
selection BRACKET (its own asset battle_bracket.h), NOT a combatant -- excluded everywhere (matches
tools/gen/battle_anim.py). fb row = OAM y + 8 (as the stance bake).

  T1  ctCronoIdleAddKf[3]  -- Crono's active-idle streams sub-tiles 0x0C/0x0D/0x19/0x1B; the 3 tile
      variants map 1:1 to ctCronoIdleBob.state (0/1/2, locked exact f400-500). rep f400/f408/f420.
  T2  ctGatoSwipeKf[3]     -- GATO's claw-swipe streams tiles 0x80-0x9F over the FIXED rear-up OAM;
      3 swipe frames (windup f508-526 / strike f529-532 / follow f535-553). rel = frame - f505.
  T3  ctGatoIdleTrajV2[]   -- CORRECTED steady idle (no-attack capture): body bob +-1px (the shipped
      ctGatoIdleTraj loops the 6px ENTRANCE-SETTLE f384-433 + a -4px wrap pop = the 'estranho').

Run from repo root: CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/gen/battle_anim_add.py [--verify] [--emit]
"""
import os, sys, subprocess, re
sys.path.insert(0, 'tools')
from ctlib import oam

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
SP    = os.environ.get('CT_SCRATCH', 'build')
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_anim_add.h')
NTILE = 0xA0
BRACKET_TILE = 0x006
WB    = 0x45
CRONO_REF = (109, 108); GATO_REF = (104, 62)

# T1: (pal, pose, repF, bobState) ; T2: (pal, pose, repF, relStart, relEnd)
CRONO_IDLE_KEYS = [(5, 'CI0_rest', 400, 0), (5, 'CI1_down', 408, 1), (5, 'CI2_side', 420, 2)]
GATO_SWIPE_KEYS = [(1, 'GS1_windup', 520, 0, 24), (1, 'GS2_strike', 531, 24, 30), (1, 'GS3_follow', 544, 30, 49)]
# rel windows tile the rear-up gap-free: GS1 [0,24) (measured content f508-526; == G1_rearup, also covers the
# rel0-2 idle->rearup transition), GS2 [24,30) (strike, flash-on-Crono @rel27), GS3 [30,49) (follow f535-553).
GATO_V2_F0, GATO_V2_F1 = 67, 186     # one A(87f)+B(33f)=120f steady period (ADD8: F0=72 clipped arm-A by 5f -> 115 drifted 5f/cycle); loops A->A

# ---- drivers / probes
def hold_bs(F, K=8): return "100:%d" % F if F <= K else "100:%d,0:%d" % (K, F - K)
def regen(frame):
    out = os.path.join(SP, 'add_hold_%d.raw' % frame)
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', hold_bs(frame)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out
_MON = re.compile(r"^\s*f(\d+)\s+000700=([0-9A-Fa-f]+)\s+000900=([0-9A-Fa-f]+)")
def monitor(state, btnscript, poke=None):
    env = dict(os.environ, PEEK_POKE=poke) if poke else None
    p = subprocess.run([os.path.abspath(EXE), ROM, '--monitor', '0700:512,0900:32', '--from', state,
                        '--btnscript', btnscript], capture_output=True, text=True, env=env)
    frames = {}
    for ln in p.stdout.splitlines():
        ln = ln.replace('*', ' '); m = _MON.match(ln)
        if m: frames[int(m.group(1))] = oam.decode(bytes.fromhex(m.group(2)) + bytes.fromhex(m.group(3)))
    return frames

def load(prefix):
    return (open(prefix + '.vram', 'rb').read(), oam.load_cgram(prefix + '.oam'),
            oam.decode(oam.load(prefix + '.oam')), open(prefix, 'rb').read())
def combatant(sprites, pal):
    on = [s for s in sprites if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == pal and s['tile'] != BRACKET_TILE]
    on.sort(key=lambda s: s['idx']); return on
def rgb555(w):
    rep = lambda c: (c << 3) | (c >> 2); return (rep(w & 31), rep((w >> 5) & 31), rep((w >> 10) & 31))
def bracket_ink(sprites, vram):
    mask = set()
    for s in sprites:
        if s['tile'] != BRACKET_TILE or s['y'] >= 210 or not (-60 < s['x'] < 256): continue
        for cy in range(2):
            for cx in range(2):
                t = s['tile'] + cx + 0x10*cy; o = (t & 0x1FF) * 32
                for r in range(8):
                    p0, p1, p2, p3 = vram[o+r*2], vram[o+r*2+1], vram[o+16+r*2], vram[o+16+r*2+1]
                    for c in range(8):
                        b = 7-c; v = ((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
                        if v == 0: continue
                        sx, sy = cx*8+c, cy*8+r
                        if s['hflip']: sx = 15-sx
                        if s['vflip']: sy = 15-sy
                        mask.add((s['x']+sx, s['y']+sy))
    return mask
def _plot(buf, owner, s, tb, cgram):
    for cy in range(2):
        for cx in range(2):
            t = s['tile'] + cx + 0x10*cy; blk = tb(t)
            for r in range(8):
                p0, p1, p2, p3 = blk[r*2], blk[r*2+1], blk[16+r*2], blk[16+r*2+1]
                for c in range(8):
                    b = 7-c; v = ((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
                    if v == 0: continue
                    sx, sy = cx*8+c, cy*8+r
                    if s['hflip']: sx = 15-sx
                    if s['vflip']: sy = 15-sy
                    X, Y = s['x']+sx, s['y']+sy
                    if 0 <= X < 256 and 0 <= Y < 224:
                        buf[Y][X] = rgb555(cgram[(128+s['pal']*16+v)*2] | (cgram[(128+s['pal']*16+v)*2+1] << 8))
                        owner[Y][X] = s['pal']

# ---- bake one keyframe group (T1/T2). specs: (pal, pose, repF, *extra)
def bake_group(specs):
    kfs = []; tiles = []
    for spec in specs:
        pal, pose, rf = spec[0], spec[1], spec[2]
        vram, cgram, sprites, fbraw = load(regen(rf))
        on = combatant(sprites, pal)
        allspr = [s for s in sprites if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] in (1, 5, 6) and s['tile'] != BRACKET_TILE]
        ax = min(s['x'] for s in on); ay = min(s['y'] for s in on)
        kfs.append(dict(pal=pal, pose=pose, rf=rf, extra=spec[3:], nObj=len(on), ax=ax, ay=ay,
                        vram=vram, cgram=cgram, fbraw=fbraw, on=on, allspr=allspr, brk=bracket_ink(sprites, vram)))
        tiles.append([[vram[t*32 + i] for i in range(32)] for t in range(NTILE)])
    return kfs, tiles

# ---- T3 corrected steady idle
def make_gato_noatk():
    idle = os.path.join(SP, 'gato_idle400.state')
    env = dict(os.environ, PEEK_SAVE=idle)
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, os.path.join(SP, '_gidle400.raw'),
                    '--btnscript', '100:8,0:392'], env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return idle                         # gato_noatk = this + NOATK_POKE (GATO's two action slots emptied)
NOATK_POKE = 'AFAC=FF,AFB1=FF'
def gato_idle_v2():
    frames = monitor(make_gato_noatk(), '100:0,0:%d' % (GATO_V2_F1 + 4), NOATK_POKE)
    rows = []
    for F in range(GATO_V2_F0, GATO_V2_F1 + 1):
        if F not in frames: continue
        on = [s for s in frames[F] if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == 1 and s['tile'] != BRACKET_TILE]
        if not on: continue
        ax = min(s['x'] for s in on); ay = min(s['y'] for s in on)
        c68 = next((s for s in on if s['tile'] == 0x68), None)
        arm = 1 if (c68 and (c68['y'] - ay) > 25) else 0
        rows.append((F, ax - GATO_REF[0], ay - GATO_REF[1], arm, 0))   # footDY=0 (no feet; bracket removed)
    return rows

def read_committed_gato_idle():
    rows = []; inh = False
    for ln in open('src/data/battle_anim.h'):
        if "ctGatoIdleTraj[" in ln: inh = True; continue
        if inh:
            m = re.match(r"\s*\{\s*(-?\d+),\s*(-?\d+),\s*(\d+),\s*(\d+)\s*\}", ln)
            if m: rows.append(tuple(int(x) for x in m.groups()))
            if "};" in ln: break
    return rows

# ---- emit
def _emit_group(W, arr, kfs, tiles, kf_extra_doc):
    obj_flat = []
    for kf in kfs: obj_flat.extend(kf['on'])
    W("#define %s_NOBJ %d\n" % (arr.upper(), len(obj_flat)))
    W("static const CtAnimObj %sObj[%s_NOBJ] = {\n" % (arr, arr.upper()))
    for s in obj_flat:
        W("  { %3d, %4d, %3d, 0x%03X, %d, %d, %d, %d },\n" % (s['idx'], s['x'], s['y'], s['tile'], s['pal'], s['prio'], s['hflip'], s['vflip']))
    W("};\n")
    W("%s\n" % kf_extra_doc)
    W("#define %s_NKF %d\n" % (arr.upper(), len(kfs)))
    W("static const CtAnimAddKf %sKf[%s_NKF] = {\n" % (arr, arr.upper()))
    off = 0
    for i, kf in enumerate(kfs):
        a = kf['extra'][0] if len(kf['extra']) > 0 else 0
        b = kf['extra'][1] if len(kf['extra']) > 1 else 0
        W('  { "%s", %d, %d, %d, %d, %d, %4d, %3d, %d, %d },\n' % (kf['pose'], kf['pal'], kf['rf'], off, kf['nObj'], i, kf['ax'], kf['ay'], a, b))
        off += kf['nObj']
    W("};\n")
    W("static const uint8_t %sTiles[%s_NKF][CT_ANIM_NTILE][32] = {\n" % (arr, arr.upper()))
    for i, kf in enumerate(kfs):
        W("  { // %s\n" % kf['pose'])
        for t in range(NTILE): W("    {%s},\n" % ','.join('0x%02X' % x for x in tiles[i][t]))
        W("  },\n")
    W("};\n\n")

def emit(cidle, cidle_t, gswipe, gswipe_t, v2):
    with open(OUT_H, 'w') as fp:
        W = fp.write
        W("// AUTO-GENERATED by tools/gen/battle_anim_add.py -- DO NOT EDIT.\n")
        W("// ADDITIVE companion to battle_anim.h (sprite/anim completeness T1/T2/T3). Reuses CtAnimObj +\n")
        W("// CT_ANIM_NTILE + CtGatoIdleStep from battle_anim.h. tile 0x006 = active-PC bracket, excluded.\n")
        W("#ifndef CT_BATTLE_ANIM_ADD_H\n#define CT_BATTLE_ANIM_ADD_H\n#include <stdint.h>\n#include \"battle_anim.h\"\n\n")
        W("// keyframe: a/b = per-target args. T1: a = bob.state served (0/1/2). T2: [a,b) = rel-frame\n")
        W("// window (rel to rear-up dispatch, oracle f505). anchorX/Y = OAM min-corner; fb row = y+8.\n")
        W("typedef struct { const char* pose; uint8_t pal; uint16_t repF; uint8_t objOff, nObj, tileKf;\n")
        W("                 int16_t anchorX; uint8_t anchorY; int16_t a, b; } CtAnimAddKf;\n\n")

        W("// ===== T1: CRONO active-idle TILE-CONTENT variants (streams 0x0C/0x0D/0x19/0x1B; cell 0x0E\n")
        W("// static). 1:1 with ctCronoIdleBob.state -> render the ACTIVE PC's idle from the variant for\n")
        W("// bob.state (OAM offset still ctCronoIdleBob dx,dy); replaces the STATIC battle_stance.h tiles.\n")
        _emit_group(W, "ctCronoIdleAdd", cidle, cidle_t, "// a = bob.state this variant serves (0=rest 1=down 2=side).")
        W("static const uint8_t ctCronoIdleStateToKf[3] = { 0, 1, 2 };  // bob.state -> ctCronoIdleAddKf index\n\n")

        W("// ===== T2: GATO claw-SWIPE tile stream over the FIXED rear-up OAM. During the rear-up window\n")
        W("// pick the frame by rel=(frame - rearup-dispatch): a<=rel<b. GS1 == the shipped G1_rearup(kf8)\n")
        W("// content; strike (flash on Crono) lands @rel27, inside GS2.\n")
        _emit_group(W, "ctGatoSwipe", gswipe, gswipe_t, "// [a,b) = rel-frame window (rel to rear-up dispatch, oracle f505).")

        W("// ===== T3: CORRECTED GATO idle trajectory (steady, +-1px body bob). The shipped ctGatoIdleTraj\n")
        W("// still loops f384-504 whose f384-433 rows are the ENTRANCE-SETTLE (6px body sink) + a -4px\n")
        W("// loop-wrap pop = the 'movimento estranho'. Truth (no-attack capture): body bob only +-1px,\n")
        W("// arm A-dominant, NO feet (footDY=0; the old foot-bob was the bracket). One A+B period; loops.\n")
        W("// Drop-in replacement for ctGatoIdleTraj (same CtGatoIdleStep; index (frame-COMBAT_LIVE) %% N).\n")
        W("#define CT_GATO_IDLE2_F0 %d\n" % GATO_V2_F0)
        W("#define CT_GATO_IDLE2_N %d\n" % len(v2))
        W("static const CtGatoIdleStep ctGatoIdleTrajV2[CT_GATO_IDLE2_N] = {\n")
        for (F, dx, dy, arm, fdy) in v2:
            W("  { %4d, %3d, %d, %3d }, // f%d\n" % (dx, dy, arm, fdy, F))
        W("};\n\n")
        W("#endif // CT_BATTLE_ANIM_ADD_H\n")

# ---- verify
def verify_kfs(title, kfs, tiles):
    print(title)
    tok = tot = 0
    for i, kf in enumerate(kfs):
        cgram = kf['cgram']; fbraw = kf['fbraw']; vram = kf['vram']; pal = kf['pal']; baked = tiles[i]; brk = kf['brk']
        def raw_tb(t): o = (t & 0x1FF) * 32; return vram[o:o+32]
        def baked_tb(t): return baked[t] if (t & 0x1FF) < NTILE else bytes(32)
        buf = [[None]*256 for _ in range(224)]; owner = [[None]*256 for _ in range(224)]
        for s in sorted(kf['allspr'], key=lambda s: -s['idx']):
            _plot(buf, owner, s, baked_tb if s['pal'] == pal else raw_tb, cgram)
        act = kf['on']; x0 = min(s['x'] for s in act); x1 = max(s['x'] for s in act)+15
        y0 = min(s['y'] for s in act); y1 = max(s['y'] for s in act)+15
        d = t2 = 0
        for y in range(max(0, y0), min(224, y1+1)):
            for x in range(max(0, x0), min(256, x1+1)):
                if owner[y][x] != pal or (x, y) in brk: continue   # skip bracket-owned pixels
                c = buf[y][x]; t2 += 1; o = ((2*(y+8))*512 + 2*x)*4
                if abs(c[0]-fbraw[o+3])+abs(c[1]-fbraw[o+2])+abs(c[2]-fbraw[o+1]) > 24: d += 1
        tok += t2-d; tot += t2
        print("  %-11s (f%d) bbox x[%d..%d] y[%d..%d]  exact=%d/%d  mismatch=%d" % (kf['pose'], kf['rf'], x0, x1, y0, y1, t2-d, t2, d))
    print("  subtotal exact=%d/%d (%.2f%%)" % (tok, tot, 100.0*tok/tot) if tot else "  (empty)")

def verify_t3(v2):
    cur = read_committed_gato_idle()
    cbt = [62 + r[1] for r in cur]; vbt = [62 + r[2] for r in v2]
    curfoot = sorted(set(r[3] for r in cur))     # footDY column in the committed table (0 post bracket-fix)
    print("[T3 GATO-idle divergence] committed ctGatoIdleTraj (looped) vs corrected ctGatoIdleTrajV2:")
    print("  committed footDY column = %s (0 = concurrent bracket fix already removed the spurious foot-bob;" % curfoot)
    print("     this V2 addresses the REMAINING body-bob = the entrance-settle rows looped as if idle)")
    print("  body-top Y bob   : CURRENT %d..%d = %dpx (loops the ENTRANCE-SETTLE)  CORRECTED %d..%d = %dpx (steady)"
          % (min(cbt), max(cbt), max(cbt)-min(cbt), min(vbt), max(vbt), max(vbt)-min(vbt)))
    print("  loop-wrap pop    : CURRENT %+dpx body-top JUMP (last->first)   CORRECTED %+dpx" % (cbt[0]-cbt[-1], vbt[0]-vbt[-1]))
    print("  arm=B swap frac  : CURRENT %.0f%%   CORRECTED %.0f%%" % (100.0*sum(r[2] for r in cur)/len(cur), 100.0*sum(r[3] for r in v2)/len(v2)))

def main():
    cidle, cidle_t = bake_group(CRONO_IDLE_KEYS)
    gswipe, gswipe_t = bake_group(GATO_SWIPE_KEYS)
    v2 = gato_idle_v2()
    if '--verify' in sys.argv:
        verify_kfs("[T1 Crono idle-stream verify] variant reconstructed from BAKED tiles (bracket-excluded):", cidle, cidle_t)
        verify_kfs("[T2 GATO claw-swipe verify] swipe frame reconstructed from BAKED tiles:", gswipe, gswipe_t)
        verify_t3(v2)
    if '--verify' not in sys.argv or '--emit' in sys.argv:
        emit(cidle, cidle_t, gswipe, gswipe_t, v2)
        print("wrote %s (T1 cidle=%d, T2 gswipe=%d, T3 v2rows=%d)" % (OUT_H, len(cidle), len(gswipe), len(v2)))

if __name__ == '__main__':
    main()
