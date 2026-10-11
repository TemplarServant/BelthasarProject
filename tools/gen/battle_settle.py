#!/usr/bin/env python3
"""Bake the CT victory SETTLE-IN ladder (the ~72f animated celebration that plays INTO the
held victory pose) into src/data/battle_settle.h + verify vs oracle.

Dossier: disasm/notes/battle_gato_idle_victory.md section C (mission 2, the "travada" fix).
GATO fight, party Crono seat0 + Marle seat1.

The held victory pose (battle_end.h kf0/kf1) is correct; what our engine was MISSING is the
discrete ~72f keyframe ladder that plays into it (dossier C.2). Each ladder keyframe = OAM
assembly + its OWN OBJ tiles (poses STREAM tile content, dossier E) -- same bake shape as
battle_end.h / battle_entrance.h.  Endpoints: C0/M0 = the idle-stance held at the kill; the
final pose C5/M5 == battle_end.h kf0/kf1 (reused by the render, NOT re-baked here).

Source: gato_kill.state (gato_mid with GATO curHP $6130=6) + mash-A to f110 then IDLE to N
(the fine_celebration peek recipe, scratchpad/gato_idle/fine_celebration.py; kill ~N170).
The ladder keyframes' representative peek frames (dossier C.2 dwell windows):
  Crono  C0@N178 C1@N194 C2@N204 C3@N214 C4@N226   (pal5)
  Marle  M0@N178 M1@N186 M2@N196 M3@N210 M4@N228   (pal6)
The C1/M1 assemblies were cross-checked byte-exact vs dossier C.2 (anchor + cells).

Run from repo root:  CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python
tools/gen/battle_settle.py [--verify] [--emit].  Needs gato_kill.state in CT_SCRATCH/gato_idle
(or the fc_N.raw sidecars already present there -- read directly if a re-peek isn't wanted).
Emits src/data/battle_settle.h.  With --verify, reconstructs each keyframe over its bbox and
pixel-diffs vs the oracle fb (fb row = OAM y + 8, as the stance/entrance/end bakes).
"""
import os, sys, subprocess
sys.path.insert(0, 'tools')
from ctlib import oam

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
SP    = os.environ.get('CT_SCRATCH', 'build')
GID   = os.path.join(SP, 'gato_idle')            # the fine_celebration capture dir
# gato_kill = gato_mid with GATO's curHP $6130 = 6; gato_mid = the GATO fight 400 frames in (battle_gato_idle_victory.md:
# STATE + "100:8,0:392" + PEEK_SAVE). Both are made here, from the recipe (CT_GEN_STATE overrides).
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
KILL_POKE = '6130=06'

def make_gato_mid():
    mid = os.path.join(GID, 'gato_mid.state')
    os.makedirs(GID, exist_ok=True)
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, os.path.join(GID, '_mid.raw'), '--btnscript', '100:8,0:392'],
                   env=dict(os.environ, PEEK_SAVE=mid), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return mid
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_settle.h')
NTILE = 0x40                                      # 8x8 tiles 0x00..0x3F: Crono(0x08..)+Marle(0x20..)
BRACKET = 0x006                                   # the active-PC selection bracket -- EXCLUDED (its own asset)

# (name, repN, pal) -- the ladder keyframes (dossier C.2). C0/M0 = idle at kill; C1-C4/M1-M4 = the settle.
KEYS = [
    ('C0', 178, 5), ('C1', 194, 5), ('C2', 204, 5), ('C3', 214, 5), ('C4', 226, 5),
    ('M0', 178, 6), ('M1', 186, 6), ('M2', 196, 6), ('M3', 210, 6), ('M4', 228, 6),
]

def mash(f):
    n=0; parts=[]; hold=True
    while n<f:
        c=min(16,f-n); parts.append(('100:%d' if hold else '0:%d')%c); n+=c; hold=not hold
    return ','.join(parts)

def peek(N):
    """Peek gato_kill.state to peek-frame N (mash-A to f110 then idle), reproducing fine_celebration's
    fc_N.raw.  If the sidecar already exists (from that lane), reuse it (no oracle needed)."""
    out=os.path.join(GID,'fc_%d.raw'%N)
    bs=mash(110)+(",0:%d"%(N-110) if N>110 else "")
    subprocess.run([os.path.abspath(EXE),ROM,'--peek',MID,out,'--btnscript',bs],check=True,
                   env=dict(os.environ, PEEK_POKE=KILL_POKE),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return out

def load(prefix):
    vram=open(prefix+'.vram','rb').read()
    cgram=oam.load_cgram(prefix+'.oam')
    sprites=oam.decode(oam.load(prefix+'.oam'))
    fbraw=open(prefix,'rb').read()
    return vram,cgram,sprites,fbraw

def cluster(sprites,pal):
    """The pal5(Crono)/pal6(Marle) OBJ cells on-screen, bracket excluded (fine_celebration recipe)."""
    on=[s for s in sprites if s['y']<210 and -60<s['x']<256 and s['pal']==pal and s['tile']!=BRACKET]
    on.sort(key=lambda s:s['idx'])
    return on

def rgb555(w):
    rep=lambda c:(c<<3)|(c>>2)
    return (rep(w&31),rep((w>>5)&31),rep((w>>10)&31))

def collect():
    global MID
    MID=make_gato_mid()
    kfs=[]; obj_flat=[]; tiles_per_kf=[]; cgram_ref=None
    for name,N,pal in KEYS:
        pre=peek(N); vram,cgram,sprites,fbraw=load(pre)
        if cgram_ref is None: cgram_ref=cgram
        on=cluster(sprites,pal)
        if not on:
            print("WARNING: keyframe %s (N%d pal%d) has NO cells"%(name,N,pal)); continue
        ax=min(s['x'] for s in on); ay=min(s['y'] for s in on)
        kfs.append(dict(name=name,N=N,pal=pal,objOff=len(obj_flat),nObj=len(on),
                        ax=ax,ay=ay,vram=vram,cgram=cgram,fbraw=fbraw,on=on))
        obj_flat.extend(on)
        tiles_per_kf.append([[vram[t*32+i] for i in range(32)] for t in range(NTILE)])
    return kfs,obj_flat,tiles_per_kf,cgram_ref

def emit(kfs,obj_flat,tiles_per_kf,cgram):
    with open(OUT_H,'w') as fp:
        W=fp.write
        W("// AUTO-GENERATED by tools/gen/battle_settle.py -- DO NOT EDIT.\n")
        W("// CT victory SETTLE-IN ladder: the ~72f animated celebration that plays INTO the held\n")
        W("// victory pose (battle_end.h kf0/kf1). Dossier: battle_gato_idle_victory.md section C.\n")
        W("// GATO fight, Crono seat0 (pal5) + Marle seat1 (pal6). Each keyframe = OAM assembly +\n")
        W("// its OWN OBJ tiles (poses STREAM tile content). Endpoints: C0/M0 = idle-at-kill; the\n")
        W("// FINAL pose C5/M5 == battle_end.h kf0/kf1 (reused by the render, not re-baked here).\n")
        W("// A pose OBJ pixel at OAM (x,y) lands at oracle fb true-pixel (x, y+8) (verified).\n")
        W("#ifndef CT_BATTLE_SETTLE_H\n#define CT_BATTLE_SETTLE_H\n#include <stdint.h>\n\n")

        W("// Flat OBJ list across all keyframes (OAM-index order; composite HIGH oidx first).\n")
        W("typedef struct { uint8_t oidx; int16_t x; uint8_t y; uint16_t tile;\n")
        W("                 uint8_t pal, prio, hf, vf; } CtSettleObj;\n")
        W("#define CT_SETTLE_NOBJ %d\n"%len(obj_flat))
        W("static const CtSettleObj ctBattleSettleObj[CT_SETTLE_NOBJ] = {\n")
        for s in obj_flat:
            W("  { %3d, %4d, %3d, 0x%03X, %d, %d, %d, %d },\n"
              %(s['idx'],s['x'],s['y'],s['tile'],s['pal'],s['prio'],s['hflip'],s['vflip']))
        W("};\n\n")

        W("// Keyframe table. pc: 0=Crono(pal5) / 1=Marle(pal6). objOff/nObj = slice of the flat OBJ\n")
        W("// list; tileKf = index into ctBattleSettleTiles; anchorX/Y = OAM min-corner; repN = source\n")
        W("// peek frame (kill ~N170). The RENDER schedule (victoryHold windows) is below.\n")
        W("typedef struct { const char* name; uint8_t pc, pal, objOff, nObj, tileKf;\n")
        W("                 int16_t anchorX; uint8_t anchorY; uint16_t repN; } CtSettleKeyframe;\n")
        W("#define CT_SETTLE_NKF %d\n"%len(kfs))
        W("static const CtSettleKeyframe ctBattleSettleKf[CT_SETTLE_NKF] = {\n")
        for i,kf in enumerate(kfs):
            pc = 0 if kf['pal']==5 else 1
            W('  { "%s", %d, %d, %d, %d, %d, %4d, %3d, %d },\n'
              %(kf['name'],pc,kf['pal'],kf['objOff'],kf['nObj'],i,kf['ax'],kf['ay'],kf['N']))
        W("};\n\n")

        W("// Per-keyframe OBJ 8x8 tiles 0x00..0x%02X (4bpp planar, 32 B) -- streamed per pose.\n"%(NTILE-1))
        W("// 16x16 sprite of tile T uses 8x8 tiles T, T+1, T+0x10, T+0x11.\n")
        W("#define CT_SETTLE_NTILE 0x%02X\n"%NTILE)
        W("static const uint8_t ctBattleSettleTiles[CT_SETTLE_NKF][CT_SETTLE_NTILE][32] = {\n")
        for i,kf in enumerate(kfs):
            W("  { // kf%d %s (N%d pal%d)\n"%(i,kf['name'],kf['N'],kf['pal']))
            for t in range(NTILE):
                W("    {%s},\n"%','.join('0x%02X'%b for b in tiles_per_kf[i][t]))
            W("  },\n")
        W("};\n\n")

        W("// --- RENDER SCHEDULE (battle_gato_idle_victory.md C.2): victoryHold (real frames since the\n")
        W("// win) -> ladder keyframe. Per PC, pick the LATEST keyframe whose startV <= victoryHold; at/after\n")
        W("// endV land on the HELD battle_end.h kf0(Crono)/kf1(Marle). victoryHold maps to the dossier peek\n")
        W("// dwell windows (kill ~N170 -> startV = repN-170-ish, snapped to the C.2 window onsets). A dead PC\n")
        W("// does NOT celebrate (the caller keeps the fallen path). kfIdx = index into ctBattleSettleKf.\n")
        W("typedef struct { uint8_t startV, kfIdx; } CtSettleStep;\n")
        # startV per keyframe (dossier C.2 dwell-window onsets, kill N170 -> V = N-170; snapped)
        SCHED = {
            'C0':1, 'C1':20, 'C2':32, 'C3':40, 'C4':52,
            'M0':1, 'M1':12, 'M2':24, 'M3':32, 'M4':52,
        }
        END = {'C5':64, 'M5':68}   # victoryHold at/after which the held kf0/kf1 takes over
        idx={kf['name']:i for i,kf in enumerate(kfs)}
        for pc,tag in ((0,'CRONO'),(1,'MARLE')):
            steps=[(SCHED[n],idx[n]) for n in idx if (kfs[idx[n]]['pal']==(5 if pc==0 else 6))]
            steps.sort()
            W("#define CT_SETTLE_%s_NSTEP %d\n"%(tag,len(steps)))
            W("static const CtSettleStep ctBattleSettle%s[CT_SETTLE_%s_NSTEP] = {\n"%(tag.capitalize(),tag))
            for sv,ki in steps: W("  { %d, %d },\n"%(sv,ki))
            W("};\n")
        W("#define CT_SETTLE_CRONO_ENDV %d   // >= this victoryHold -> held battle_end kf0\n"%END['C5'])
        W("#define CT_SETTLE_MARLE_ENDV %d   // >= this victoryHold -> held battle_end kf1\n"%END['M5'])
        W("#endif // CT_BATTLE_SETTLE_H\n")

def decode8(tb):
    px=[[0]*8 for _ in range(8)]
    for r in range(8):
        p0,p1,p2,p3=tb[r*2],tb[r*2+1],tb[16+r*2],tb[16+r*2+1]
        for c in range(8):
            b=7-c; px[r][c]=((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
    return px

def verify(kfs,tiles_per_kf):
    tot_ok=tot=0
    print("[battle-settle verify] per ladder keyframe, over its bbox (d>24 = mismatch):")
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
        if not xs: print("  %-3s NO pixels"%kf['name']); continue
        d=t2=0
        for y in range(min(ys),max(ys)+1):
            for x in range(min(xs),max(xs)+1):
                c=buf[y][x]
                if c is None: continue
                t2+=1; o=((2*(y+8))*512+2*x)*4
                if abs(c[0]-fbraw[o+3])+abs(c[1]-fbraw[o+2])+abs(c[2]-fbraw[o+1])>24: d+=1
        tot_ok+=t2-d; tot+=t2
        print("  %-3s N%d pal%d  bbox x[%d..%d] y[%d..%d]  exact=%d/%d  mismatch=%d"
              %(kf['name'],kf['N'],kf['pal'],min(xs),max(xs),min(ys),max(ys),t2-d,t2,d))
    print("  TOTAL exact=%d/%d (%.1f%%)"%(tot_ok,tot,100.0*tot_ok/tot if tot else 0))

def main():
    kfs,obj_flat,tiles_per_kf,cgram=collect()
    if '--verify' in sys.argv: verify(kfs,tiles_per_kf)
    if '--verify' not in sys.argv or '--emit' in sys.argv:
        emit(kfs,obj_flat,tiles_per_kf,cgram)
        print("wrote %s (%d keyframes, %d OBJ)"%(OUT_H,len(kfs),len(obj_flat)))

if __name__=='__main__':
    main()
