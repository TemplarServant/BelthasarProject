#!/usr/bin/env python3
"""Bake the CT battle-END poses (victory celebration + PC fallen/dead) into
src/data/battle_end.h + verify vs oracle.

Dossier: disasm/notes/battle_end.md.  GATO fight, party Crono seat0 + Marle seat1.

Three baked pose keyframes (each = OAM assembly + its OWN OBJ tiles, since battle poses
STREAM tile content per pose -- same shape as battle_entrance.h):
  - VICTORY Crono (pal5) : the celebration pose held after the killing blow (dossier T1)
  - VICTORY Marle (pal6) : "
  - FALLEN  Crono (pal5) : the dead PC "collapsed on the ground" pose (dossier T3); dead
                           PCs STAY rendered (do not vanish, unlike enemies)

Source frames (deterministic peek timelines; GATO at 19 HP = the recipe stand-in below, LOW_POKE on gato_mid):
  VIC  = the stand-in + mash-A to the kill + 30 (the kill at f601) then IDLE 180 = kill + 210
         (idle holds the celebration pose indefinitely -- the reform needs an A-press,
          dossier T2 -- so idle gives a clean, A-noise-free victory pose).
  FALL = the stand-in with Crono at 1 HP + IDLE: GATO's first action kills him at f235 -> fallen pose, taken
         100 frames later.
  (--lowhp <state>: the old capture gato_lowhp.state's frames -- VIC mash to f980 + 180, FALL Crono at 10 HP, f100.)

Run from repo root:  python tools/gen/battle_end.py [--state <recipe state>] [--lowhp <state>] [--forest <state>] [--verify] [--emit].
Emits src/data/battle_end.h.  With --verify, reconstructs each pose over its bbox and
pixel-diffs vs the oracle fb (fb row = OAM y + 8, as the stance/entrance bakes).
"""
import os, sys, subprocess
sys.path.insert(0, 'tools')
from ctlib import oam

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
SP    = os.environ.get('CT_SCRATCH', 'build')
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_end.h')
NTILE = 0x40                                    # 8x8 tiles 0x00..0x3F: Crono(0x08..)+Marle(0x20..)
WB    = 0x45                                    # LakeSnes .state WRAM base offset (battle_loop ADDENDUM)

def mash_to(f):
    """A-mash btnscript (100:16/0:16) of exactly f frames."""
    n=0; parts=[]; hold=True
    while n<f:
        c=min(16,f-n); parts.append(('100:%d' if hold else '0:%d')%c); n+=c; hold=not hold
    return ','.join(parts)

# (name, pose, pal, srckey) ; srcframe built below.  Marle-fallen (T4) is APPENDED so the 3 original
# keyframes' baked bytes stay identical (only CT_END_NKF grows + a 4th keyframe/tile-set is added).
KEYS = [
    ('Crono', 'victory', 5, 'VIC'),
    ('Marle', 'victory', 6, 'VIC'),
    ('Crono', 'fallen',  5, 'FALL'),
    ('Marle', 'fallen',  6, 'MFALL'),      # T4 (battle_end.md ADD): Marle collapse, forest fight
]

def _arg(name, env, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else os.environ.get(env, default)
# GATO at low HP is made here from the recipe, as tools/gen/battle_settle.py makes gato_kill: gato_mid = the GATO fight
# 400 frames in (STATE + "100:8,0:392" + PEEK_SAVE, battle_gato_idle_victory.md), then a PEEK_POKE as it loads --
# GATO's curHP $6130 = 19 and Crono's $5E30 = 56 (the HP the old capture gato_lowhp.state carried). On that stand-in
# the A-mash kills GATO at f601 (the capture's f950): the victory frame is taken at the capture's distance from the
# kill (mash to kill + 30, then idle 180 = kill + 210, 7 frames before $A0FE turns 80 on both). Crono's fall: Crono
# at 1 HP, GATO's first action (f78-f235) kills him at f235 and the frame is 100 later (the capture: 10 HP, the
# physical in flight kills him at f0, frame f100). --lowhp <state> still bakes from a capture (the old frames).
# The forest fight is the recipe battle_forest_3hetake (Crono where the capture's stood: Marle's fall is the
# capture's byte for byte).
STATE = _arg('--state', 'CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state')
LOWHP = _arg('--lowhp', 'CT_GEN_LOWHP', None)
FOREST = _arg('--forest', 'CT_GEN_FOREST', 'build/recipes/battle_forest_3hetake.state')
LOW_POKE = '6130=13,6131=00,5E30=38,5E31=00'      # GATO 19 HP, Crono 56 HP
VIC_KILL, FALL_KILL = 601, 235                     # the stand-in's kill frames (GATO by the mash, Crono by GATO)

def make_gato_mid():
    mid = os.path.join(SP, 'be_gato_mid.state')
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, os.path.join(SP, 'be_mid.raw'), '--btnscript', '100:8,0:392'],
                   env=dict(os.environ, PEEK_SAVE=mid), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return mid

def make_crono_low():
    """gato_lowhp with Crono's curHP $5E30 = 10 (a PEEK_POKE on the state as it loads)."""
    return LOWHP, '5E30=0A,5E31=00'

def make_marle_fall():
    """T4: forest fight (3x Hetake). Patch Marle curHP $5EB0=3 (dies to one Hetake hit) + Crono
    curHP $5E30=1 (dies first -> the Hetakes retarget Marle, since with Crono alive they only ever
    hit the lead). IDLE -> Marle's death handler streams her fallen tiles; captured before the
    party-wipe defeat transition (stable f435-520). Both deaths run IN-ENGINE."""
    return FOREST, '5EB0=03,5E30=01'

def peek(state, btnscript, tag):
    poke = None
    if isinstance(state, tuple):
        state, poke = state
    out=os.path.join(SP,'be_%s.raw'%tag)
    env = dict(os.environ, PEEK_POKE=poke) if poke else None
    subprocess.run([os.path.abspath(EXE),ROM,'--peek',state,out,'--btnscript',btnscript],
                   check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=env)
    return out

def srcframes():
    if LOWHP:                                              # the old capture (gato_lowhp.state)
        vic = peek(LOWHP, mash_to(980)+',0:180', 'VIC')   # -> f1160 celebration hold
        fall= peek(make_crono_low(), '0:100', 'FALL')      # -> f100 fallen Crono
    else:                                                  # the recipe stand-in (see LOW_POKE)
        mid = make_gato_mid()
        vic = peek((mid, LOW_POKE), mash_to(VIC_KILL+30)+',0:180', 'VIC')                  # -> kill + 210
        fall= peek((mid, LOW_POKE.replace('5E30=38', '5E30=01')), '0:%d' % (FALL_KILL+100), 'FALL')
    mfall= peek(make_marle_fall(), '100:0,0:470', 'MFALL') # -> f470 fallen Marle (forest, stable)
    return {'VIC':vic, 'FALL':fall, 'MFALL':mfall}

def load(prefix):
    vram=open(prefix+'.vram','rb').read()
    cgram=oam.load_cgram(prefix+'.oam')
    sprites=oam.decode(oam.load(prefix+'.oam'))
    fbraw=open(prefix,'rb').read()
    return vram,cgram,sprites,fbraw

def combatant(sprites,pal):
    on=[s for s in sprites if s['y']<210 and -40<s['x']<256 and s['pal']==pal]
    on.sort(key=lambda s:s['idx']); return on

def rgb555(w):
    rep=lambda c:(c<<3)|(c>>2)
    return (rep(w&31),rep((w>>5)&31),rep((w>>10)&31))

def collect():
    frames={}
    for _,_,_,k in KEYS:
        if k not in frames: frames[k]=None
    srcs=srcframes()
    for k in frames: frames[k]=load(srcs[k])
    kfs=[]; obj_flat=[]; tiles_per_kf=[]; cgram_ref=None
    for name,pose,pal,srckey in KEYS:
        vram,cgram,sprites,fbraw=frames[srckey]
        if srckey!='MFALL': cgram_ref=cgram   # keep ctBattleEndPal from the GATO-fight frames (byte-identical)
        on=combatant(sprites,pal)
        ax=min(s['x'] for s in on); ay=min(s['y'] for s in on)
        kfs.append(dict(name=name,pose=pose,pal=pal,srckey=srckey,objOff=len(obj_flat),
                        nObj=len(on),ax=ax,ay=ay,vram=vram,cgram=cgram,fbraw=fbraw,on=on))
        obj_flat.extend(on)
        tiles_per_kf.append([[vram[t*32+i] for i in range(32)] for t in range(NTILE)])
    return kfs,obj_flat,tiles_per_kf,cgram_ref

def emit(kfs,obj_flat,tiles_per_kf,cgram):
    with open(OUT_H,'w') as fp:
        W=fp.write
        W("// AUTO-GENERATED by tools/gen/battle_end.py -- DO NOT EDIT.\n")
        W("// CT battle-END poses: victory celebration (Crono/Marle) + PC fallen/dead pose.\n")
        W("// Dossier: disasm/notes/battle_end.md. GATO fight, Crono seat0 + Marle seat1.\n")
        W("// Each keyframe = OAM assembly + its OWN OBJ tiles (poses STREAM tile content).\n")
        W("// A pose OBJ pixel at OAM (x,y) lands at oracle fb true-pixel (x, y+8) (verified).\n")
        W("#ifndef CT_BATTLE_END_H\n#define CT_BATTLE_END_H\n#include <stdint.h>\n\n")

        W("// --- baked pose keyframes -------------------------------------------------\n")
        W("// Flat OBJ list across all keyframes (OAM-index order; composite HIGH oidx first).\n")
        W("// x signed 9-bit screen x; y OAM y (fb row = y+8); tile 9-bit; pal; prio; hf/vf.\n")
        W("typedef struct { uint8_t oidx; int16_t x; uint8_t y; uint16_t tile;\n")
        W("                 uint8_t pal, prio, hf, vf; } CtEndObj;\n")
        W("#define CT_END_NOBJ %d\n"%len(obj_flat))
        W("static const CtEndObj ctBattleEndObj[CT_END_NOBJ] = {\n")
        for s in obj_flat:
            W("  { %3d, %4d, %3d, 0x%03X, %d, %d, %d, %d },\n"
              %(s['idx'],s['x'],s['y'],s['tile'],s['pal'],s['prio'],s['hflip'],s['vflip']))
        W("};\n\n")

        W("// Keyframe table. kind: 0=victory-celebration, 1=fallen/dead. objOff/nObj = slice\n")
        W("// of the flat OBJ list. tileKf = index into ctBattleEndTiles. anchorX/Y = OAM min-corner.\n")
        W("typedef struct { const char* name; const char* pose; uint8_t pal, kind;\n")
        W("                 uint8_t objOff, nObj, tileKf; int16_t anchorX; uint8_t anchorY; } CtEndKeyframe;\n")
        W("#define CT_END_NKF %d\n"%len(kfs))
        W("static const CtEndKeyframe ctBattleEndKf[CT_END_NKF] = {\n")
        for i,kf in enumerate(kfs):
            kind=0 if kf['pose']=='victory' else 1
            W('  { "%s", "%s", %d, %d, %d, %d, %d, %4d, %3d },\n'
              %(kf['name'],kf['pose'],kf['pal'],kind,kf['objOff'],kf['nObj'],i,kf['ax'],kf['ay']))
        W("};\n\n")

        W("// Per-keyframe OBJ 8x8 tiles 0x00..0x%02X (4bpp planar, 32 B) -- streamed per pose.\n"%(NTILE-1))
        W("// 16x16 sprite of tile T uses 8x8 tiles T, T+1, T+0x10, T+0x11.\n")
        W("#define CT_END_NTILE 0x%02X\n"%NTILE)
        W("static const uint8_t ctBattleEndTiles[CT_END_NKF][CT_END_NTILE][32] = {\n")
        for i,kf in enumerate(kfs):
            W("  { // kf%d %s %s\n"%(i,kf['name'],kf['pose']))
            for t in range(NTILE):
                W("    {%s},\n"%','.join('0x%02X'%b for b in tiles_per_kf[i][t]))
            W("  },\n")
        W("};\n\n")

        W("// OBJ CGRAM palettes (BGR555 LE). OBJ palette P = CGRAM 128+P*16+[0..15]; idx0 transparent.\n")
        W("static const uint16_t ctBattleEndPal[8][16] = {\n")
        for p in range(8):
            vals=[cgram[(128+p*16+c)*2]|(cgram[(128+p*16+c)*2+1]<<8) for c in range(16)]
            used=' USED' if p in (5,6) else ''
            W("  {%s}, // pal%d%s\n"%(','.join('0x%04X'%v for v in vals),p,used))
        W("};\n\n")

        W("// --- REGROUP walk (dossier T2): FIELD-side party conga reform on battle-return ------\n")
        W("// After the player dismisses the results (A-press -> C0/A730 handoff, $95D5/$95DB/$AFAB\n")
        W("// all -> 0x80), the FIELD gathers followers onto the LEADER's formation tile. Measured\n")
        W("// (GATO fight): Crono(lead) holds at (124,99); Marle walks from her battle pos (168,68)\n")
        W("// diagonally to (124,99) and STACKS EXACTLY on the leader (NOT offset) over ~40 render\n")
        W("// frames (~1.35 px/frame). The endpoint is the LEADER's screen pos; render = the field\n")
        W("// walk sprite (field engine), so only the target + rate are baked, not pose pixels.\n")
        W("#define CT_END_REGROUP_LEADER_X   124\n")
        W("#define CT_END_REGROUP_LEADER_Y    99\n")
        W("#define CT_END_REGROUP_MARLE_X0   168   /* Marle walk start (her battle anchor)   */\n")
        W("#define CT_END_REGROUP_MARLE_Y0    68\n")
        W("#define CT_END_REGROUP_WALK_FRAMES 40   /* render frames Marle -> leader, measured  */\n\n")

        W("// --- timing (render frames, gato_lowhp peek timeline; global 2.5f/tick applies) ------\n")
        W("#define CT_END_SETTLE_FRAMES 60  /* kill -> victory pose settled (~f960->f1020)      */\n")
        W("#define CT_END_HOLD_MIN     140  /* victory pose held (>=; holds until A dismisses)   */\n")
        W("#endif // CT_BATTLE_END_H\n")

def decode8(tb):
    px=[[0]*8 for _ in range(8)]
    for r in range(8):
        p0,p1,p2,p3=tb[r*2],tb[r*2+1],tb[16+r*2],tb[16+r*2+1]
        for c in range(8):
            b=7-c; px[r][c]=((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
    return px

def verify(kfs,tiles_per_kf):
    tot_ok=tot=0
    print("[battle-end verify] per pose keyframe, over its bbox (d>24 = mismatch):")
    for i,kf in enumerate(kfs):
        vram_tiles=tiles_per_kf[i]; cgram=kf['cgram']; fbraw=kf['fbraw']
        buf=[[None]*256 for _ in range(224)]
        for s in sorted(kf['on'],key=lambda s:-s['idx']):
            for cy in range(2):
                for cx in range(2):
                    t=s['tile']+cx+0x10*cy
                    tb=vram_tiles[t&(NTILE-1)] if (t&0x1FF)<NTILE else [0]*32
                    px=decode8(tb)
                    for yy in range(8):
                        for xx in range(8):
                            v=px[yy][xx]
                            if v==0: continue
                            sx,sy=cx*8+xx,cy*8+yy
                            if s['hflip']: sx=15-sx
                            if s['vflip']: sy=15-sy
                            X,Y=s['x']+sx,s['y']+sy
                            if 0<=X<256 and 0<=Y<224:
                                buf[Y][X]=rgb555(cgram[(128+s['pal']*16+v)*2]|(cgram[(128+s['pal']*16+v)*2+1]<<8))
        xs=[x for x in range(256) if any(buf[y][x] for y in range(224))]
        ys=[y for y in range(224) if any(buf[y][x] for x in range(256))]
        d=t2=0
        for y in range(min(ys),max(ys)+1):
            for x in range(min(xs),max(xs)+1):
                c=buf[y][x]
                if c is None: continue
                t2+=1; o=((2*(y+8))*512+2*x)*4
                if abs(c[0]-fbraw[o+3])+abs(c[1]-fbraw[o+2])+abs(c[2]-fbraw[o+1])>24: d+=1
        tot_ok+=t2-d; tot+=t2
        print("  %-5s %-8s pal%d  bbox x[%d..%d] y[%d..%d]  exact=%d/%d  mismatch=%d"
              %(kf['name'],kf['pose'],kf['pal'],min(xs),max(xs),min(ys),max(ys),t2-d,t2,d))
    print("  TOTAL exact=%d/%d (%.1f%%)"%(tot_ok,tot,100.0*tot_ok/tot))

def main():
    kfs,obj_flat,tiles_per_kf,cgram=collect()
    if '--verify' in sys.argv: verify(kfs,tiles_per_kf)
    if '--verify' not in sys.argv or '--emit' in sys.argv:
        emit(kfs,obj_flat,tiles_per_kf,cgram)
        print("wrote %s (%d keyframes, %d OBJ)"%(OUT_H,len(kfs),len(obj_flat)))

if __name__=='__main__':
    main()
