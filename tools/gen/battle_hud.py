#!/usr/bin/env python3
"""Bake the CT battle-mode HUD STRIP into src/data/battle_hud.h + verify vs the oracle.

The strip (renderY 160..207 = oracle-fb-y 168..215, 8px overscan) is ONE 32x32 tilemap at
VRAM word 0x7800, rendered twice per the HDMA split (BG1 4bpp chrome + BG3 2bpp text, TM 0x05).
The whole 256x48 CHROME -- window bodies (gradient + fixed dither), box bevel frames, the 2px
inter-box separators and the 2px outer screen-edge frame -- is the opaque BG1 layer (subscreen
= BG1+BG3, CGWSEL=0: field-independent); the TEXT is BG3 2bpp font (ROM $FF:8C60 ==
CT_ASSET_MENUFONT) with tile index = 0x100 + CT-textcode, palette 2 (CGRAM[8..11]) except PC
name rows (per-PC palette), composited ON TOP of the chrome.

PROBED (both phases): the BG1-only render (PEEK_ISOBG=0) == the normal render byte-exact at
EVERY strip pixel outside glyph INK (cmd: 2192 diffs all under ink, 0 outside; enemy: 1256/0).
So the chrome is baked verbatim as the full-strip BG1 layer (ctBattleHudBg1{Enemy,Cmd}) --
per-pixel, since the dither is non-periodic (no tileable matrix) AND position-dependent (a
single box's bevel profile does NOT transfer to another box, which was the old
ctBattleHudBevel* d>16 residual: 74 enemy / 101 cmd px).

Two settled phases:
  - ENEMY-NAME (2 boxes): enemy name (left) + party window (right). Settled ~f320-370.
  - COMMAND    (3 boxes): 2 command windows "Att./Tech/Item" + party window. Settled ~f400+.

Sources: committed saves/battle_re/p600.raw* (settled COMMAND). The settled ENEMY-NAME frame is
regenerated from gato_press_action.state (btnscript '100:8,0:360') because the committed p300 /
gato_t2 dumps are MID-SLIDE (window not fully open). ROM = the font + provenance.

Run from repo root. Emits src/data/battle_hud.h; with --verify, reconstructs the strip from the baked
data and pixel-diffs against the oracle fb over renderY 160..207, printing diff counts.
"""
import os, sys, subprocess
sys.path.insert(0, 'tools')
from ctlib import fb, oam

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
SP    = os.environ.get('CT_SCRATCH', 'build')          # where to drop the regen enemy-name frame
FONT_FILEOFF = 0x3F8C60                                  # $FF:8C60, no copier header (verified: 'A'@$FF:9660)

# ---- strip geometry (measured; see disasm/notes/battle_hud_render.md) --------------------------
STRIP_Y0 = 160            # render-y of the window top (oracle-fb-y 168)
STRIP_H  = 48             # 160..207 inclusive
# text glyph-row render-y by MAP ROW (non-uniform: per-scanline HDMA V-scroll compresses blanks)
ROWY = {1: 163, 2: 171, 4: 183, 6: 195}   # map row 1=headers, 2/4/6 = text lines
BOX_CMD   = [(2, 53), (56, 101), (104, 253)]    # command phase: 3 boxes (x0..x1 inclusive)
BOX_ENEMY = [(2, 95), (98, 253)]                # enemy-name phase: 2 boxes
BEVEL = 3                 # box L/R frame thickness (px); only used for the interior ACCOUNTING

# ---- helpers ----------------------------------------------------------------------------------
def load_font():
    rom = open(ROM, 'rb').read()
    tiles = []
    for code in range(256):
        o = FONT_FILEOFF + code * 16
        px = [[0]*8 for _ in range(8)]
        for r in range(8):
            p0, p1 = rom[o+r*2], rom[o+r*2+1]
            for b in range(8):
                bit = 7-b
                px[r][b] = ((p0>>bit)&1) | (((p1>>bit)&1)<<1)
        tiles.append(px)
    return tiles

def tmword(vram, i):
    o = 0x7800*2 + i*2
    return vram[o] | (vram[o+1] << 8)

def cgram_palettes(oampath):
    """CGRAM -> pal[8][4] RGB for 2bpp BG (palette P = CGRAM[P*4 .. P*4+3])."""
    cg = oam.load_cgram(oampath)
    def rgb(w):
        # BGR555 -> RGB888 by 5->8 bit replication (what LakeSnes' PPU does; floor r*255/31 is off by 1)
        r=(w&31); g=(w>>5)&31; b=(w>>10)&31
        return ((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2))
    pal=[]
    for p in range(8):
        row=[]
        for c in range(4):
            idx=p*4+c
            w=cg[idx*2] | (cg[idx*2+1]<<8)
            row.append(rgb(w))
        pal.append(row)
    return pal

def gen_enemy_frame():
    out = os.path.join(SP, 'bh_enemy.raw')
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', '100:8,0:360'],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out

def gen_cmd_frame():
    """The settled COMMAND frame (was the committed saves/battle_re/p600.raw: the same peek, verified identical)."""
    out = os.path.join(SP, 'bh_cmd.raw')
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', '100:8,0:600'],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out

def gen_iso0(btnscript, name):
    """Regenerate a settled battle frame with ONLY BG1 composited (PEEK_ISOBG=0). The whole strip
    CHROME is 100% BG1 (probed: iso0 == normal byte-exact at every non-glyph-ink strip pixel,
    both phases -- bevels, separators and the outer frame included; battle_hud_render.md Q2), so
    this is the pure chrome with NO text/OBJ overlay -- the exact source to bake.
    Enemy uses 100:8,0:360 (== gen_enemy_frame's settled f360); command uses 100:8,0:600 (regen of
    the committed p600 -- verified byte-identical: cmd600 NORMAL == saves/battle_re/p600.raw)."""
    out = os.path.join(SP, name)
    env = dict(os.environ, PEEK_ISOBG='0')
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', btnscript],
                   env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out

def bake_bg1(isopath):
    """The full-strip BG1 CHROME layer: EVERY pixel (all 256 cols x 48 rows) of the BG1-only
    render (PEEK_ISOBG=0). This is the window bodies (per-scanline COLDATA gradient + the fixed
    non-periodic dither carried in the BG1 tiles, char word 0x1000), the per-box bevel frames,
    the inter-box separators and the outer screen-edge frame, all in one baked table -- the strip
    has NO field-showing gap (TM/TS=0x05, opaque). Replaces the old single-box bevel_box()
    profile, whose stamp-on-every-box approximation was the d>16 residual (the bevel columns are
    part of the same position-dependent dithered bitmap as the body, so a profile lifted from one
    box does not match another box's edges). Returns tex[48][256] of (r,g,b)."""
    w, h, rgb = fb.load(isopath)
    def px(x, ry):
        oy = STRIP_Y0 + ry + 8; o = (oy*w + x)*3
        return (rgb[o], rgb[o+1], rgb[o+2])
    return [[px(x, ry) for x in range(256)] for ry in range(STRIP_H)]

def bg1_palette(*texes):
    """Shared color palette (index table) for the chrome textures; 0xFF reserved as a sentinel,
    so the palette must be <= 255 entries."""
    cols = {}
    for tex in texes:
        for ry in range(STRIP_H):
            for x in range(256):
                cols[tex[ry][x]] = True
    pal = sorted(cols, key=lambda c: (sum(c), c))
    assert len(pal) <= 255, "chrome palette too large for a uint8 index (%d)" % len(pal)
    return pal

def bg1_index(tex, pal):
    idx = {c: i for i, c in enumerate(pal)}
    return [[idx[tex[ry][x]] for x in range(256)] for ry in range(STRIP_H)]

# ---- bake -------------------------------------------------------------------------------------
def bake_map(vram):
    """map rows 0..6 (the window's 7 tile rows), full 32 cols, as (tile,pal) pairs."""
    rows=[]
    for r in range(7):
        row=[]
        for c in range(32):
            wd=tmword(vram, r*32+c)
            row.append((wd & 0x3FF, (wd>>10)&7))
        rows.append(row)
    return rows

def emit_header(fp, palE, palC, mapE, mapC, bg1Pal, bg1IdxE, bg1IdxC):
    def carr_pal(name, pal):
        fp.write("static const uint8_t %s[8][4][3] = {\n" % name)
        for p in range(8):
            fp.write("  {" + ', '.join('{%d,%d,%d}'%pal[p][c] for c in range(4)) + "},\n")
        fp.write("};\n")
    def carr_map(name, m):
        fp.write("static const uint16_t %s[7][32] = {   /* (tile<<0)|(pal<<10) per cell */\n" % name)
        for r in range(7):
            fp.write("  {" + ', '.join('0x%04X'%((t)|(p<<10)) for (t,p) in m[r]) + "},\n")
        fp.write("};\n")
    fp.write("""// battle_hud.h - BYTE-EXACT CT battle-mode HUD STRIP, baked from the oracle. NOTHING invented.
// The strip = ONE 32x32 tilemap at VRAM word 0x7800 rendered as BG1 4bpp CHROME + BG3 2bpp TEXT
// via the HDMA split (scanlines 160-207, TM/TS 0x05); see disasm/notes/battle_hud_render.md.
//   * CHROME (BG1)  = ctBattleHudBg1{Enemy,Cmd}[48][256] : the FULL-strip BG1 layer -- window bodies
//                     (per-scanline COLDATA gradient + the fixed non-periodic dither in the BG1
//                     tiles), per-box bevel frames, inter-box separators, outer screen-edge frame.
//                     Baked per-pixel from the BG1-only oracle render (PEEK_ISOBG=0), which is
//                     byte-exact to the normal render at every non-glyph-ink strip pixel (probed,
//                     both phases). uint8 index into ctBattleHudBg1Pal. Opaque, field-INDEPENDENT.
//   * TEXT   (BG3)  = 2bpp font (ROM $FF:8C60 == CT_ASSET_MENUFONT; tile = 0x100 + CT textcode),
//                     palette 2 = CGRAM[8..11] (transparent/dark/grey/WHITE); PC-name rows differ.
//                     Drawn ON TOP of the chrome (the hand cursor overlaps box0's bevel columns).
//   * Tilemaps      = ctBattleHudMap{Enemy,Cmd}[7][32] : the settled 0x7800 map rows 0-6, (tile|pal<<10).
//   * Palettes      = ctBattleHudPal{E,C}[8][4][3] : the 8 BG 2bpp sub-palettes (CGRAM[P*4..P*4+3]).
// Two phases: ENEMY-NAME (2 boxes: enemy name + party) and COMMAND (3 boxes: 2 cmd menus + party).
// Provenance per array below. Generated by tools/gen/battle_hud.py.
#ifndef CT_BATTLE_HUD_H
#define CT_BATTLE_HUD_H
#include <stdint.h>
#define CT_BHUD_Y0   160    /* render-y of the strip top (oracle-fb-y 168; overscan 8) */
#define CT_BHUD_H     48    /* strip height (renderY 160..207) */
/* The strip has TWO resting positions, selected per battle -- see CtBattleHudRender's "STRIP ORIGIN"
   comment. TOP places the same 48-row strip at renderY 8; the bake (chrome + row table) is BOTTOM. */
#define CT_BHUD_Y0_TOP 8    /* render-y of the strip top in the TOP layout (oracle scanlines 9..56) */
#define CT_BHUD_TILE0 0x100 /* strip map tile = 0x100 + CT textcode; glyph = CT_ASSET_MENUFONT[textcode] */
// text glyph-row render-y by MAP ROW (non-uniform; per-scanline HDMA BG3 V-scroll drops blank rows):
//   map row 1 = HP/MP headers @163, row 2 = line0 @171, row 4 = line1 @183, row 6 = line2 @195
static const int ctBattleHudRowY[7] = { -1, 163, 171, -1, 183, -1, 195 };
// box x-extents (inclusive) per phase; boxes are separated by ~2px dark bevels
static const int ctBattleHudBoxEnemy[2][2] = { {2,95}, {98,253} };
static const int ctBattleHudBoxCmd[3][2]   = { {2,53}, {56,101}, {104,253} };
""")
    fp.write("// --- 2bpp BG sub-palettes (CGRAM[P*4..P*4+3]); text uses pal2, PC-name rows pal per char ---\n")
    fp.write("// ENEMY-NAME phase (regen f360):\n"); carr_pal("ctBattleHudPalEnemy", palE)
    fp.write("// COMMAND phase (p600):\n");           carr_pal("ctBattleHudPalCmd", palC)
    fp.write("// --- settled strip tilemaps (word 0x7800, rows 0-6) ---\n")
    fp.write("// ENEMY-NAME: box0='Gato' (enemy name, ROM CC/6500), box1=party (Crono/Marle + HP:MP + bar)\n")
    carr_map("ctBattleHudMapEnemy", mapE)
    fp.write("// COMMAND: box0=Crono cmd(hand+Att/Tech/Item), box1=Marle cmd, box2=party\n")
    carr_map("ctBattleHudMapCmd", mapC)
    # --- the full-strip BG1 CHROME layer: per-pixel color, baked from the BG1-only render
    #     (PEEK_ISOBG=0). One table per phase covers body + bevels + separators + outer frame; the
    #     old split (flat gradient + single-box bevel profile + separator/outer tables) is gone --
    #     the dither is non-periodic AND position-dependent, so only the verbatim layer is exact.
    fp.write("// --- full-strip BG1 CHROME: per-pixel index into ctBattleHudBg1Pal ---\n")
    fp.write("// Baked from the BG1-only oracle render (iso0): bodies (COLDATA gradient + fixed dither in\n")
    fp.write("// the BG1 tiles), per-box bevel frames, separators, outer frame. iso0 == normal byte-exact\n")
    fp.write("// at every non-glyph-ink strip pixel (probed both phases; battle_hud_render.md Q2), so\n")
    fp.write("// chrome + the BG3 glyphs on top reconstructs the strip 12288/12288.\n")
    fp.write("static const uint8_t ctBattleHudBg1Pal[%d][3] = {\n  " % len(bg1Pal))
    fp.write(', '.join('{%d,%d,%d}'%c for c in bg1Pal))
    fp.write("\n};\n")
    def carr_bg1(name, idx):
        fp.write("static const uint8_t %s[%d][256] = {\n" % (name, STRIP_H))
        for ry in range(STRIP_H):
            fp.write("  {" + ','.join('%d'%v for v in idx[ry]) + "},\n")
        fp.write("};\n")
    fp.write("// ENEMY-NAME phase (2 boxes) chrome, baked from iso0 @f360:\n")
    carr_bg1("ctBattleHudBg1Enemy", bg1IdxE)
    fp.write("// COMMAND phase (3 boxes) chrome, baked from iso0 @f600 (== committed p600 BG1):\n")
    carr_bg1("ctBattleHudBg1Cmd", bg1IdxC)
    fp.write("#endif\n")

# ---- reconstruction + verify ------------------------------------------------------------------
def reconstruct(phase, pal, mp, font, bg1):
    """Render the strip (256 x 48 RGB, renderY 160..207) from baked data: the full-strip BG1
    chrome layer, then the BG3 text glyphs from the tilemap on top (the oracle's own compositing
    order -- the hand cursor's ink lands on box0's bevel columns)."""
    W=256
    img=[[bg1[ry][x] for x in range(W)] for ry in range(STRIP_H)]
    for r in range(7):
        sy=ROWY.get(r, -1)
        if sy<0: continue
        for c in range(32):
            tile,p = mp[r][c]
            if tile < 0x100: continue
            code=tile-0x100
            g=font[code]; colors=pal[p]
            for gy in range(8):
                yy=sy+gy-STRIP_Y0
                if yy<0 or yy>=STRIP_H: continue
                for gx in range(8):
                    v=g[gy][gx]
                    if v==0: continue           # transparent -> chrome shows
                    xx=c*8+gx
                    if 0<=xx<W: img[yy][xx]=colors[v]
    return img

def ink_positions(mp, font):
    """Set of (ry,x) covered by glyph INK (font value>0) per the baked map."""
    ink=set()
    for r in range(7):
        sy=ROWY.get(r,-1)
        if sy<0: continue
        for c in range(32):
            tile,p=mp[r][c]
            if tile<0x100: continue
            g=font[tile-0x100]
            for gy in range(8):
                ry=sy+gy-STRIP_Y0
                if ry<0 or ry>=STRIP_H: continue
                for gx in range(8):
                    if g[gy][gx]==0: continue
                    x=c*8+gx
                    if 0<=x<256: ink.add((ry,x))
    return ink

def verify(name, rgb, w, pal, mp, font, phase, bg1):
    boxes = BOX_ENEMY if phase=='enemy' else BOX_CMD
    rec=reconstruct(phase, pal, mp, font, bg1)
    ink=ink_positions(mp, font)
    interior={(ry,x) for (x0,x1) in boxes for ry in range(STRIP_H)
              for x in range(x0+BEVEL, x1-BEVEL+1)} - ink   # box-interior body px (the Q2 figure)
    exact=near=far=total=0; sae=0; mx=0
    bodyE=0
    for ry in range(STRIP_H):
        oy=STRIP_Y0+ry+8
        for x in range(256):
            o=(oy*w+x)*3; oc=(rgb[o],rgb[o+1],rgb[o+2]); rc=rec[ry][x]
            total+=1
            d=max(abs(oc[0]-rc[0]),abs(oc[1]-rc[1]),abs(oc[2]-rc[2]))
            sae+=d; mx=max(mx,d)
            if d==0:
                exact+=1
                if (ry,x) in interior: bodyE+=1
            elif d<=16: near+=1
            else: far+=1
    print("  [%s vs %s] %d strip px (renderY 160-207): exact=%d (%.1f%%)  d<=16=%d (%.1f%%)  d>16=%d (%.1f%%)"
          % (phase, name, total, exact, 100*exact/total, near, 100*near/total, far, 100*far/total))
    # glyph-INK-only exactness (font value>0), isolating text fidelity from the chrome
    inkE=inkT=0
    for r in range(7):
        sy=ROWY.get(r,-1)
        if sy<0: continue
        for c in range(32):
            tile,p=mp[r][c]
            if tile<0x100: continue
            g=font[tile-0x100]; colors=pal[p]
            for gy in range(8):
                oy=sy+gy+8                       # render-y -> oracle-fb-y (overscan 8)
                for gx in range(8):
                    v=g[gy][gx]
                    if v==0: continue
                    x=c*8+gx
                    if not (0<=x<256): continue
                    o=(oy*w+x)*3; oc=(rgb[o],rgb[o+1],rgb[o+2]); rc=colors[v]
                    inkT+=1
                    if max(abs(oc[i]-rc[i]) for i in range(3))==0: inkE+=1
    print("        mean|err|=%.2f  max|err|=%d ; glyph-INK pixels %d/%d byte-EXACT (%.1f%%) ;"
          " box-interior BODY %d/%d byte-EXACT (%.1f%%)"
          % (sae/total, mx, inkE, inkT, 100*inkE/max(1,inkT),
             bodyE, len(interior), 100*bodyE/max(1,len(interior))))
    return exact,near,far,total

# ---- ADDITIVE: diff a C-produced CtBattleHudRender dump (CT_BATTLESHOT, field.c) vs the SAME oracle
# targets `verify()` uses above. Reuses the existing oracle loaders (fb.load / gen_enemy_frame /
# bake_map / load_font) unchanged -- no new derivation logic, just a second thing to diff against the
# same ground truth. The C dump is [u32 w][u32 h][RGB], TRUE 256x224 engine coords (NO 2x doubling, NO
# 8px overscan) -- unlike the oracle fb, so renderY ry maps to dump row STRIP_Y0+ry directly, and to
# oracle row STRIP_Y0+ry+8 (the same "+8" every other helper here already applies).
def verify_c(name, cpath, phase):
    font = load_font()
    wC, hC, rgbC = fb.load(cpath)
    if phase == 'cmd':
        wO, hO, rgbO = fb.load('saves/battle_re/p600.raw')
        vram = open(os.environ.get('CT_GEN_P600') or os.path.join(SP, 'bh_cmd.raw') + '.vram', 'rb').read()
    else:
        ef = gen_enemy_frame()
        wO, hO, rgbO = fb.load(ef)
        vram = open(ef + '.vram', 'rb').read()
    mp = bake_map(vram)

    def cpx(x, y):
        o = (y * wC + x) * 3
        return (rgbC[o], rgbC[o+1], rgbC[o+2])
    def opx(x, y):
        o = (y * wO + x) * 3
        return (rgbO[o], rgbO[o+1], rgbO[o+2])

    exact=near=far=total=0; sae=0; mx=0
    for ry in range(STRIP_H):
        myrow = STRIP_Y0 + ry
        oy = STRIP_Y0 + ry + 8
        for x in range(256):
            cc = cpx(x, myrow); oc = opx(x, oy)
            total += 1
            d = max(abs(cc[i]-oc[i]) for i in range(3))
            sae += d; mx = max(mx, d)
            if d == 0: exact += 1
            elif d <= 16: near += 1
            else: far += 1
    print("  [C-dump %s vs %s] %d strip px (renderY 160-207): exact=%d (%.1f%%)  d<=16=%d (%.1f%%)  d>16=%d (%.1f%%)"
          % (phase, name, total, exact, 100*exact/total, near, 100*near/total, far, 100*far/total))
    inkE=inkT=0
    for r in range(7):
        sy = ROWY.get(r, -1)
        if sy < 0: continue
        for c in range(32):
            tile, p = mp[r][c]
            if tile < 0x100: continue
            g = font[tile-0x100]
            for gy in range(8):
                myrow = sy + gy
                oy = sy + gy + 8
                for gx in range(8):
                    if g[gy][gx] == 0: continue
                    x = c*8+gx
                    if not (0 <= x < 256): continue
                    cc = cpx(x, myrow); oc = opx(x, oy)
                    inkT += 1
                    if cc == oc: inkE += 1
    print("        mean|err|=%.2f  max|err|=%d ; glyph-INK pixels %d/%d byte-EXACT (%.1f%%)"
          % (sae/total, mx, inkE, inkT, 100*inkE/max(1, inkT)))
    return exact,near,far,total,inkE,inkT

def main():
    do_verify = '--verify' in sys.argv
    font = load_font()
    # COMMAND phase from committed p600
    p600 = os.environ.get('CT_GEN_P600') or gen_cmd_frame()
    wC,hC,rgbC = fb.load(p600)
    vramC = open(p600 + '.vram','rb').read()
    palC  = cgram_palettes(p600 + '.oam')
    mapC  = bake_map(vramC)
    # ENEMY-NAME phase: regenerate settled f360
    ef = gen_enemy_frame()
    wE,hE,rgbE = fb.load(ef)
    vramE = open(ef+'.vram','rb').read()
    palE  = cgram_palettes(ef+'.oam')
    mapE  = bake_map(vramE)
    # CHROME: bake the full-strip BG1 layer (bodies + bevels + separators + outer frame) from the
    # BG1-only render. Enemy @f360 (== the enemy target), command @f600 (== committed p600 BG1,
    # byte-verified).
    texE = bake_bg1(gen_iso0('100:8,0:360', 'bh_enemy_iso0.raw'))
    texC = bake_bg1(gen_iso0('100:8,0:600', 'bh_cmd_iso0.raw'))
    bg1Pal  = bg1_palette(texE, texC)
    bg1IdxE = bg1_index(texE, bg1Pal)
    bg1IdxC = bg1_index(texC, bg1Pal)
    # emit header
    outh = os.environ.get('CT_OUT', 'src/data/battle_hud.h')
    if '--emit' in sys.argv or not do_verify:
        with open(outh,'w') as fp:
            emit_header(fp, palE, palC, mapE, mapC, bg1Pal, bg1IdxE, bg1IdxC)
        print("wrote %s (chrome palette %d colors)" % (outh, len(bg1Pal)))
    if do_verify:
        print("=== VERIFY: reconstruct strip from baked data, diff vs oracle fb (renderY 160-207) ===")
        verify('p600(committed,settled)', rgbC, wC, palC, mapC, font, 'cmd',   texC)
        verify('regen-f360(settled)',     rgbE, wE, palE, mapE, font, 'enemy', texE)
    # ADDITIVE: --diffc-enemy <c_dump.raw> and/or --diffc-cmd <c_dump.raw> diff a C-produced
    # CtBattleHudRender dump (CT_BATTLESHOT, src/field.c) against the same oracle targets above.
    if '--diffc-enemy' in sys.argv:
        verify_c('regen-f360(settled)', sys.argv[sys.argv.index('--diffc-enemy')+1], 'enemy')
    if '--diffc-cmd' in sys.argv:
        verify_c('p600(committed,settled)', sys.argv[sys.argv.index('--diffc-cmd')+1], 'cmd')

if __name__ == '__main__':
    main()
