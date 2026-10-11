#!/usr/bin/env python3
"""ADDITIVE bake: MARLE's ranged (crossbow) ATTACK animation -- the T1 completeness pass.
Emits src/data/battle_marleatk.h. Does NOT touch other headers; NOT wired.

Dossier: battle_actionfx.md ADDENDUM MA (Marle attack). Method mirrors tools/gen/battle_anim.py A2/A3:
per-pose keyframe = pal6 OAM assembly + its streamed OBJ tiles 0x20-0x37 + pal6, baked at a rep
frame; pixel-verify composites all combatants by OAM index and diffs Marle's pixels vs the oracle fb
(fb row = OAM y + 8). Base gato_press_action.state, the canonical 42x A-MASH; Marle attacks at f869
($9877=0x0F, AEEB=02 PC_RANGED). Her attack is a crossbow: she aims side-on and fires a BOLT drawn
by her own streamed pal6 sprite (tiles stream, like Crono's sword-arc), + a small pal0 IMPACT SPARK
at GATO (tiles 0x12E/0x108/0x10C/0x10E), then GATO hurt-flashes (strike). NO lunge (steps ~10px).

Run from repo root: CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/gen/battle_marleatk.py [--verify] [--emit]
"""
import os, sys, subprocess
sys.path.insert(0,'tools'); from ctlib import oam
ROM=os.environ.get('CT_ROM','Chrono_Trigger_USA_SNES.sfc')
EXE=os.environ.get('CT_EXE','build/ct.exe')
SP=os.environ.get('CT_SCRATCH','build'); STATE=(sys.argv[sys.argv.index('--state')+1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE','build/recipes/battle_gato_press_action.state'))
OUT_H=os.environ.get('CT_GEN_OUT','src/data/battle_marleatk.h'); NTILE=0xA0; BRK=0x006; MARLE_HOME=(164,63)

# 42x A-mash prefix that lands exactly on frame T (cycle = 100:16,0:16 = 32f)
def bs(T):
    ci=T//32; rem=T%32; s="100:16,0:16,"*ci
    if rem==0: return s[:-1]
    return s+("100:%d"%rem if rem<=16 else "100:16,0:%d"%(rem-16))
def regen(T):
    out=os.path.join(SP,'ma_%d.raw'%T)
    subprocess.run([os.path.abspath(EXE),ROM,'--peek',STATE,out,'--btnscript',bs(T)],
                   check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return out
def load(p): return (open(p+'.vram','rb').read(), oam.load_cgram(p+'.oam'),
                     oam.decode(oam.load(p+'.oam')), open(p,'rb').read())
def rgb555(w):
    r=lambda c:(c<<3)|(c>>2); return (r(w&31),r((w>>5)&31),r((w>>10)&31))
def cg(cgram,pal,v): return rgb555(cgram[(128+pal*16+v)*2]|(cgram[(128+pal*16+v)*2+1]<<8))
def onpal(spr,pal,tlo=None,thi=None):
    o=[s for s in spr if s['y']<210 and -60<s['x']<256 and s['pal']==pal and s['tile']!=BRK]
    if tlo is not None: o=[s for s in o if tlo<=s['tile']<thi]
    o.sort(key=lambda s:s['idx']); return o
def _plot(buf,owner,s,tb,cgram,key):
    for cy in range(2):
        for cx in range(2):
            t=s['tile']+cx+0x10*cy; blk=tb(t)
            for r in range(8):
                p0,p1,p2,p3=blk[r*2],blk[r*2+1],blk[16+r*2],blk[16+r*2+1]
                for c in range(8):
                    b=7-c; v=((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
                    if v==0: continue
                    sx,sy=cx*8+c,cy*8+r
                    if s['hflip']: sx=15-sx
                    if s['vflip']: sy=15-sy
                    X,Y=s['x']+sx,s['y']+sy
                    if 0<=X<256 and 0<=Y<224: buf[Y][X]=cg(cgram,s['pal'],v); owner[Y][X]=key

# ---- Marle pose keyframes (rep frames within each pose dwell)
MARLE_KF=[('MA1_aim',905),('MA2_raise',918),('MA3_fire',930),('MA4_recover',948)]
# ---- impact spark (pal0), tile-animated at the impact point; rep frames per tile-variant.
# NB peek timeline = monitor+2 (battle_hud_render peek/monitor skew): monitor 0x12E@922 0x108@930
# 0x10C@932 0x10E@934 -> peek 924 932 934 936.
SPARK_KF=[('E0_spark',924),('E1_ring0',932),('E2_ring1',934),('E3_ring2',936)]

def bake_marle():
    kfs=[]; tiles=[]
    for pose,rf in MARLE_KF:
        vram,cgram,spr,fb=load(regen(rf))
        on=onpal(spr,6,0x20,0x38)
        ax=min(s['x'] for s in on); ay=min(s['y'] for s in on)
        allc=[s for s in spr if s['y']<210 and -60<s['x']<256 and s['pal'] in (1,5,6) and s['tile']!=BRK]
        kfs.append(dict(pose=pose,rf=rf,ax=ax,ay=ay,on=on,all=allc,vram=vram,cgram=cgram,fb=fb))
        tiles.append([[vram[t*32+i] for i in range(32)] for t in range(NTILE)])
    return kfs,tiles

def bake_spark():
    kfs=[]; tiles=[]
    for pose,rf in SPARK_KF:
        vram,cgram,spr,fb=load(regen(rf))
        eff=[s for s in spr if s['pal']==0 and 0x100<=s['tile']<0x190 and s['y']<200 and -30<s['x']<250]
        if not eff: kfs.append(None); tiles.append(None); continue
        s0=eff[0]
        kfs.append(dict(pose=pose,rf=rf,spr=s0,cgram=cgram,fb=fb,vram=vram))
        tiles.append([[vram[t*32+i] for i in range(32)] for t in range(0x200)])  # need 0x100+ range
    return kfs,tiles

def marle_traj():
    # per-frame anchor delta + poseId over the attack window
    import re
    _M=re.compile(r"^\s*f(\d+)\s+000700=([0-9A-Fa-f]+)\s+000900=([0-9A-Fa-f]+)")
    MASH=','.join(['100:16,0:16']*42)
    p=subprocess.run([os.path.abspath(EXE),ROM,'--monitor','0700:512,0900:32','--from',STATE,'--btnscript',MASH],
                     capture_output=True,text=True)
    fr={}
    for ln in p.stdout.splitlines():
        ln=ln.replace('*',' '); m=_M.match(ln)
        if m: fr[int(m.group(1))]=oam.decode(bytes.fromhex(m.group(2))+bytes.fromhex(m.group(3)))
    poses={}; pid=0; rows=[]
    for F in range(872,951):
        if F not in fr: continue
        on=onpal(fr[F],6,0x20,0x38)
        if not on: continue
        ax=min(s['x'] for s in on); ay=min(s['y'] for s in on)
        asm=tuple(sorted((s['tile'],s['x']-ax,s['y']-ay) for s in on))
        if asm not in poses: poses[asm]=pid; pid+=1
        rows.append((F,ax-MARLE_HOME[0],ay-MARLE_HOME[1],poses[asm]))
    return rows

def verify(mkfs,mtiles,skfs,stiles):
    print("[MARLE attack verify] pose reconstructed from BAKED pal6 tiles, composited by OAM index:")
    tok=tot=0
    for i,kf in enumerate(mkfs):
        cgram=kf['cgram']; fb=kf['fb']; vram=kf['vram']; baked=mtiles[i]
        def raw_tb(t): o=(t&0x1FF)*32; return vram[o:o+32]
        def bak_tb(t): return baked[t] if (t&0x1FF)<NTILE else bytes(32)
        buf=[[None]*256 for _ in range(224)]; owner=[[None]*256 for _ in range(224)]
        for s in sorted(kf['all'],key=lambda s:-s['idx']):
            _plot(buf,owner,s, bak_tb if s['pal']==6 else raw_tb, cgram, 6 if s['pal']==6 else s['pal'])
        on=kf['on']; x0=min(s['x'] for s in on); x1=max(s['x'] for s in on)+15
        y0=min(s['y'] for s in on); y1=max(s['y'] for s in on)+15
        d=t2=0
        for y in range(max(0,y0),min(224,y1+1)):
            for x in range(max(0,x0),min(256,x1+1)):
                if owner[y][x]!=6: continue
                c=buf[y][x]; t2+=1; o=((2*(y+8))*512+2*x)*4
                if abs(c[0]-fb[o+3])+abs(c[1]-fb[o+2])+abs(c[2]-fb[o+1])>24: d+=1
        tok+=t2-d; tot+=t2
        print("  %-12s (f%d) anchor(%d,%d) bbox x[%d..%d] exact=%d/%d mism=%d"%(kf['pose'],kf['rf'],kf['ax'],kf['ay'],x0,x1,t2-d,t2,d))
    print("  MARLE subtotal exact=%d/%d (%.2f%%)"%(tok,tot,100.0*tok/tot if tot else 0))
    # spark verify (its own pixels)
    print("[MARLE impact-spark verify] pal0 effect sprite from baked tiles:")
    sok=stot=0
    for i,kf in enumerate(skfs):
        if not kf: continue
        s=kf['spr']; cgram=kf['cgram']; fb=kf['fb']; baked=stiles[i]
        d=t2=0
        for cy in range(2):
            for cx in range(2):
                t=s['tile']+cx+0x10*cy; blk=baked[t&0x1FF]
                for r in range(8):
                    p0,p1,p2,p3=blk[r*2],blk[r*2+1],blk[16+r*2],blk[16+r*2+1]
                    for c in range(8):
                        b=7-c; v=((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
                        if v==0: continue
                        sx,sy=cx*8+c,cy*8+r
                        if s['hflip']: sx=15-sx
                        if s['vflip']: sy=15-sy
                        X,Y=s['x']+sx,s['y']+sy
                        if not(0<=X<256 and 0<=Y<224): continue
                        col=cg(cgram,0,v); t2+=1; o=((2*(Y+8))*512+2*X)*4
                        if abs(col[0]-fb[o+3])+abs(col[1]-fb[o+2])+abs(col[2]-fb[o+1])>24: d+=1
        sok+=t2-d; stot+=t2
        print("  %-12s (f%d) tile 0x%03X @(%d,%d) exact=%d/%d mism=%d"%(kf['pose'],kf['rf'],s['tile'],s['x'],s['y'],t2-d,t2,d))
    print("  SPARK subtotal exact=%d/%d (%.2f%%)"%(sok,stot,100.0*sok/stot if stot else 0))
    return tok,tot,sok,stot

def emit(mkfs,mtiles,skfs,stiles,traj):
    with open(OUT_H,'w') as fp:
        W=fp.write
        W("// AUTO-GENERATED by tools/gen/battle_marleatk.py -- DO NOT EDIT. NOT WIRED.\n")
        W("// MARLE crossbow ATTACK ($9877=0x0F, script CD/7164, AEEB=02 PC_RANGED). battle_actionfx.md ADD-MA.\n")
        W("#ifndef CT_BATTLE_MARLEATK_H\n#define CT_BATTLE_MARLEATK_H\n#include <stdint.h>\n#include \"battle_anim.h\"\n\n")
        # pose keyframes
        obj=[];
        for kf in mkfs: obj.extend(kf['on'])
        W("#define CT_MARLEATK_NOBJ %d\n"%len(obj))
        W("static const CtAnimObj ctMarleAtkObj[CT_MARLEATK_NOBJ] = {\n")
        for s in obj: W("  { %d, %4d, %3d, 0x%03X, %d, %d, %d, %d },\n"%(s['idx'],s['x'],s['y'],s['tile'],s['pal'],s['prio'],s['hflip'],s['vflip']))
        W("};\n#define CT_MARLEATK_NKF %d\n"%len(mkfs))
        W("static const CtAnimKeyframe ctMarleAtkKf[CT_MARLEATK_NKF] = {\n")
        off=0
        for i,kf in enumerate(mkfs):
            W('  { "Marle", "%s", 6, %d, %d, %d, %d, %d, %d, %d },\n'%(kf['pose'],kf['rf'],kf['rf'],off,len(kf['on']),i,kf['ax'],kf['ay'])); off+=len(kf['on'])
        W("};\n")
        W("static const uint8_t ctMarleAtkTiles[CT_MARLEATK_NKF][CT_ANIM_NTILE][32] = {\n")
        for i,kf in enumerate(mkfs):
            W("  { // %s\n"%mkfs[i]['pose'])
            for t in range(NTILE): W("    {%s},\n"%','.join('0x%02X'%x for x in mtiles[i][t]))
            W("  },\n")
        W("};\n\n")
        # spark: 4 tile-variants (0x100-range) + position schedule
        W("// IMPACT SPARK: single pal0 OBJ at the bolt-impact point, tile-animated. dx/dy = OAM pos.\n")
        W("typedef struct { const char* pose; uint16_t repF; uint16_t tile; int16_t x; uint8_t y; } CtSparkKf;\n")
        sv=[k for k in skfs if k]
        W("#define CT_MARLESPARK_NKF %d\n"%len(sv))
        W("static const CtSparkKf ctMarleSparkKf[CT_MARLESPARK_NKF] = {\n")
        for i,kf in enumerate(skfs):
            if not kf: continue
            W('  { "%s", %d, 0x%03X, %d, %d },\n'%(kf['pose'],kf['rf'],kf['spr']['tile'],kf['spr']['x'],kf['spr']['y']))
        W("};\n")
        W("// spark tiles keyed absolute (0x100-0x12F); 16x16 uses T,T+1,T+0x10,T+0x11.\n")
        W("static const uint8_t ctMarleSparkTiles[CT_MARLESPARK_NKF][4][32] = {\n")
        for i,kf in enumerate(skfs):
            if not kf: continue
            s=kf['spr']
            W("  { // %s tile 0x%03X\n"%(kf['pose'],s['tile']))
            for q in (s['tile'],s['tile']+1,s['tile']+0x10,s['tile']+0x11):
                W("    {%s},\n"%','.join('0x%02X'%x for x in stiles[i][q&0x1FF]))
            W("  },\n")
        W("};\n\n")
        # trajectory
        W("// per-frame (dx,dy rel home 164,63 ; poseId 0..3) over the attack. Strike (GATO flash) ~f939.\n")
        W("typedef struct { int16_t dx; int8_t dy; uint8_t pose; } CtMarleAtkStep;\n")
        W("#define CT_MARLEATK_TRAJ_F0 %d\n#define CT_MARLEATK_TRAJ_N %d\n"%(traj[0][0],len(traj)))
        W("static const CtMarleAtkStep ctMarleAtkTraj[CT_MARLEATK_TRAJ_N] = {\n")
        for (F,dx,dy,p) in traj: W("  { %4d, %3d, %d }, // f%d\n"%(dx,dy,p,F))
        W("};\n#define CT_MARLEATK_STRIKE_F 939\n")
        W("#endif\n")

def main():
    mkfs,mtiles=bake_marle(); skfs,stiles=bake_spark(); traj=marle_traj()
    if '--verify' in sys.argv: verify(mkfs,mtiles,skfs,stiles)
    if '--verify' not in sys.argv or '--emit' in sys.argv:
        emit(mkfs,mtiles,skfs,stiles,traj); print("wrote %s (marleKf=%d spark=%d traj=%d)"%(OUT_H,len(mkfs),len([k for k in skfs if k]),len(traj)))
main()
