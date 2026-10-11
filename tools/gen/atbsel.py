#!/usr/bin/env python3
"""Bake the CT new-game ATB BATTLE-MODE selector screen (Active/Wait) into src/data/atbsel.h + verify vs oracle.

Derivation dossier: disasm/notes/battle_atbmode.md PART 1. The selector is menu "mode 3" screen 0x0D
(handler C2/E1E3), shown at new-game start BEFORE the Crono naming step (both are the SAME mode-3
screen 0x0D, sequenced: ATB-select -> naming, battle_atbmode.md S1.1). Two vertically-stacked options,
cursor cell $0054 (0=ACTIVE default, 1=WAIT); A-confirm writes the persistent flag $2990 bit6
(C2/E24C: TSB on row0/ACTIVE -> bit6=1, TRB on row1/WAIT -> bit6=0).

Unlike the naming screen (dynamic typed name -> font/tile composite), this screen has exactly TWO
possible static states (cursor row 0 or 1), so it bakes as a literal indexed-color FULL-FRAME capture
-- guaranteed byte-exact because it IS the oracle's own composited framebuffer, no re-derivation risk.

Captured from saves/battle_re/atb_vram.state (s9x source: 'aux crono decompile/
active_time_battle_selector.state'; already $0A00=3 $0067=0x0D pre-confirm; WRAM+CPU logic-runnable --
verified: Up AND Down both TOGGLE $0054 0<->1, the expected behaviour of a 2-item circular nav list
where a single step in EITHER direction lands on the other item; C2:EA4A writer both ways). Two frames:
  ROW0 (ACTIVE, default cursor, no input) -- deterministic (0-diff f8 vs f20).
  ROW1 (WAIT, one Down press) -- the description-box text is a VWF reveal that settles at f30 (0-diff
  f30 vs f40+); captured at f40 for margin.
Measured: only 76 distinct RGB colours across both frames, and only the description-box text + hand
cursor differ between them (a single bounding rect, ~2019 px) -- everything else (title/subtitle/
ACTIVE/WAIT labels/window chrome) is pixel-identical. So ROW1 bakes as ROW0 + a small override PATCH
over that rect: reconstruction is verified byte-exact below (0 mismatches).

Run from repo root. Emits src/data/atbsel.h. With --verify, re-captures both frames fresh from the oracle
and checks the ROW0+patch reconstruction against the real ROW1 capture pixel-for-pixel."""
# The EMPTY box's line 0 between ACTIVE's first pen and WAIT's (x = 53, y149-154) comes from the ACTIVE reveal's first
# frame (from power-on: the recipe attract_boot + POWERON_SCRIPT, tools/naming_cmp.py boot f301): BTNE's WAIT capture
# holds its first glyph pair there. (It was a hand patch on the header until 2026-10-10; active_first() makes it.)
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '..')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import os, sys, subprocess

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/atb_vram.state'))
SP    = os.environ.get('CT_SCRATCH', 'build')
OUT_H = 'src/data/atbsel.h'
W, H  = 256, 224                          # engine fb dims (CAMW x CAMH)
BTN0  = '0:8'                             # ACTIVE (row0, default cursor); stable from f8
BTNE  = '0:6,20:2,0:1'                     # the reveal's FIRST frame: line0 has 2 glyphs, line1 is EMPTY
BTN1  = '0:6,20:2,0:40'                   # WAIT (Down -> row1); description VWF settles f30, +margin
BTNA  = BTN1 + ',20:2,0:1'                 # back to ACTIVE: its reveal's first frame (line 0 empty right of pen 21)

def peek(btn, out):
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', btn],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out

def regen():
    return (peek(BTN0, os.path.join(SP, 'atbsel_row0.raw')),
            peek(BTN1, os.path.join(SP, 'atbsel_row1.raw')),
            peek(BTNE, os.path.join(SP, 'atbsel_empty.raw')))

# oracle --peek fb = 512x480, 2x pixel-doubled, [pad,B,G,R] per px (read R=+3, per ct-field-textbox
# memory: "fb dumps [pad,B,G,R] (read R=+3!)"). Sample the true (non-doubled) 256x224 grid.
def load_fb(path):
    d = open(path, 'rb').read()
    px = [[None] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            o = ((2 * y) * 512 + 2 * x) * 4
            px[y][x] = (d[o + 3], d[o + 2], d[o + 1])
    return px

def build_palette(frames):
    pal = []; idxmap = {}
    for fr in frames:
        for row in fr:
            for c in row:
                if c not in idxmap:
                    idxmap[c] = len(pal); pal.append(c)
    return pal, idxmap

def to_idx(fr, idxmap):
    return [[idxmap[c] for c in row] for row in fr]

def diff_bbox(a, b):
    xs = []; ys = []
    for y in range(H):
        for x in range(W):
            if a[y][x] != b[y][x]:
                xs.append(x); ys.append(y)
    if not xs:
        return None
    return min(xs), max(xs), min(ys), max(ys)

def emit(fp, pal, idx0, patch, px0, py0, pw, ph):
    W_ = fp.write
    W_("// AUTO-GENERATED by tools/gen/atbsel.py -- DO NOT EDIT.\n")
    W_("// CT new-game ATB BATTLE-MODE selector (Active/Wait), baked byte-exact from the oracle.\n")
    W_("// Dossier: disasm/notes/battle_atbmode.md PART 1/PART 2(A). Menu mode-3 screen 0x0D, handler\n")
    W_("// C2/E1E3, shown at new-game start BEFORE the Crono naming step. Cursor $0054 (0=ACTIVE\n")
    W_("// default row, 1=WAIT); A-confirm writes $2990 bit6 (C2/E24C TSB row0/TRB row1).\n")
    W_("// Two baked FULL FRAMES (this screen has exactly 2 static states, unlike the dynamic naming\n")
    W_("// screen): ROW0 verbatim + a ROW1 override PATCH (the only rect that differs -- description\n")
    W_("// text + hand cursor; verified 0/%d mismatch reconstructing ROW1 = ROW0+patch).\n" % (W * H))
    W_("// Captured saves/battle_re/atb_vram.state, btnscript \"%s\" (ACTIVE) / \"%s\" (WAIT).\n" % (BTN0, BTN1))
    W_("#ifndef CT_ATBSEL_H\n#define CT_ATBSEL_H\n#include <stdint.h>\n\n")
    W_("#define CT_ATBSEL_W %d\n#define CT_ATBSEL_HGT %d\n" % (W, H))
    W_("#define CT_ATBSEL_NPAL %d\n\n" % len(pal))
    W_("static const uint8_t ctAtbSelPal[CT_ATBSEL_NPAL][3] = {\n")
    for c in pal:
        W_("  {%d,%d,%d},\n" % c)
    W_("};\n\n")
    W_("// ROW0 (ACTIVE, default cursor) full frame, palette indices [y][x].\n")
    W_("static const uint8_t ctAtbSelFrame0[CT_ATBSEL_HGT][CT_ATBSEL_W] = {\n")
    for row in idx0:
        W_("  {%s},\n" % ','.join(str(v) for v in row))
    W_("};\n\n")
    W_("// ROW1 (WAIT) override patch: blit ctAtbSelFrame0 THEN this patch at (X,Y) to reproduce ROW1.\n")
    W_("#define CT_ATBSEL_PATCH_X %d\n#define CT_ATBSEL_PATCH_Y %d\n" % (px0, py0))
    W_("#define CT_ATBSEL_PATCH_W %d\n#define CT_ATBSEL_PATCH_H %d\n\n" % (pw, ph))
    W_("static const uint8_t ctAtbSelPatch1[CT_ATBSEL_PATCH_H][CT_ATBSEL_PATCH_W] = {\n")
    for row in patch:
        W_("  {%s},\n" % ','.join(str(v) for v in row))
    W_("};\n\n")
    W_("#endif // CT_ATBSEL_H\n")


# ---------------------------------------------------------------------------------------------
# TYPEWRITER: the description text is drawn 2 CHARACTERS PER FRAME, one frame per pair, on every row
# change (C2/E5D3 at C2/E239 stages it; $0DC9 = 2 is an immediate at C2/E5FB; the char loop C2/58B2
# spends it with DEC $13 / BNE; the task yield C2/821E at C2/FAA5 is the one-frame pace).
#
# ⭐ THE REVEAL IS A CLEAN LEFT-OF-PEN PREFIX -- measured, and it is what lets a BAKED screen do this
# exactly. Diffing every frame of a row change against the settled frame, the first differing pixel is
# exactly 32+pen(N) on every frame and NOTHING left of the pen ever differs. So frame N = the settled
# bitmap for x < 32+pen(N), and the empty box for x >= that.
#
# The pen is ROM data, not eyeballed: start at the $0DD2 = 8 left indent and add the per-char width
# from $C2:60E6 indexed by (code - 0xA0). Strings come from the $FF:CF3B table at index row+1; 0x05 is
# the newline (a frame that draws nothing) and 0x00 terminates.
STR_TBL   = 0xFFCF3B
WIDTH_TBL = 0xC260E6
INDENT    = 8          # $0DD2
TEXT_X    = 32         # screen_x = TEXT_X + pen  (validated against the framebuffer, see the header)

def active_first():
    """ACTIVE's reveal's first frame at full brightness: from the settled WAIT frame, Down once more (BTNA) -- the
    box is cleared and only ACTIVE's first glyph pair drawn, so line 0 is empty right of ACTIVE's first pen."""
    return load_fb(peek(BTNA, os.path.join(SP, 'atbsel_active1.raw')))

def _rom():
    return _romlib.load(ROM)

def _long(a):
    return a - 0xC00000                      # HiROM long -> file offset

def pen_rows(rom):
    """[row][line] -> list of pen positions, one entry per DRAWN frame (2 chars each)."""
    tbl = _long(STR_TBL); wt = _long(WIDTH_TBL); out = []
    for row in (0, 1):
        off = rom[tbl + (row + 1) * 2] | (rom[tbl + (row + 1) * 2 + 1] << 8)
        a = _long(0xFF0000 + off)
        chars = []
        while rom[a] != 0x00:
            chars.append(rom[a]); a += 1
        lines = [[]]
        for c in chars:
            if c == 0x05: lines.append([])
            else: lines[-1].append(c)
        rowpens = []
        for ln in lines:
            pen = INDENT; pens = []
            for k, c in enumerate(ln):
                pen += rom[wt + (c - 0xA0)]
                if k % 2 == 1 or k == len(ln) - 1:
                    pens.append(pen)
            rowpens.append(pens)
        out.append(rowpens)
    return out

def emit_typewriter(W_, pens, empty, ex, ey, ew, eh):
    NL = chr(10)
    def w(t=""):
        W_(t + NL)
    w()
    w("// ---- TYPEWRITER (derivation in tools/gen/atbsel.py) ------------------------------------")
    w("// Per-DRAWN-FRAME pen positions, straight from the ROM: the $FF:CF3B string at index row+1,")
    w("// each char's width from $C2:60E6 indexed by (code - 0xA0), starting at the $0DD2 = 8 indent.")
    w("// Screen x of the write head = CT_ATBSEL_TEXT_X + pen. Two chars are drawn per frame ($0DC9")
    w("// = 2, immediate at C2/E5FB, spent by DEC $13 in C2/58B2); the newline (0x05) and the")
    w("// terminator (0x00) each cost a frame that draws NOTHING (they return early without")
    w("// decrementing the budget), so a line ends with one blank frame before the next begins.")
    w("#define CT_ATBSEL_TEXT_X %d" % TEXT_X)
    w("#define CT_ATBSEL_NLINES 2")
    mx = max(len(p) for r in pens for p in r)
    w("#define CT_ATBSEL_MAXSTEP %d" % mx)
    w("static const uint8_t ctAtbSelPenN[2][CT_ATBSEL_NLINES] = { {%d,%d}, {%d,%d} };"
      % (len(pens[0][0]), len(pens[0][1]), len(pens[1][0]), len(pens[1][1])))
    w("static const uint8_t ctAtbSelPen[2][CT_ATBSEL_NLINES][CT_ATBSEL_MAXSTEP] = {")
    for r in (0, 1):
        parts = []
        for li in (0, 1):
            vals = pens[r][li] + [0] * (mx - len(pens[r][li]))
            parts.append("{" + ",".join(str(v) for v in vals) + "}")
        w("  { " + ", ".join(parts) + " },")
    w("};")
    w()
    w("// The EMPTY text box -- what the two lines look like with no glyphs on them. Captured at the")
    w("// reveal's FIRST frame, which is the only place these pixels exist: that frame erases the old")
    w("// text and draws just two glyphs, so line 1 is untouched and line 0 is empty right of its pen.")
    w("// Both settled frames carry full text and cannot supply this.")
    w("// Line 0 at x = 53 (y149-154) is the ACTIVE reveal's first frame instead: the WAIT capture's first glyph pair")
    w("// reaches x = 54 (pen 22), ACTIVE's cut is x = 53 (pen 21) -- MEASURED from power-on (tools/naming_cmp.py boot, f301).")
    w("#define CT_ATBSEL_EMPTY_X %d" % ex)
    w("#define CT_ATBSEL_EMPTY_Y %d" % ey)
    w("#define CT_ATBSEL_EMPTY_W %d" % ew)
    w("#define CT_ATBSEL_EMPTY_H %d" % eh)
    w()
    w("static const uint8_t ctAtbSelEmpty[CT_ATBSEL_EMPTY_H][CT_ATBSEL_EMPTY_W] = {")
    for j in range(eh):
        w("  {" + ",".join(str(v) for v in empty[j]) + "},")
    w("};")

def main():
    r0p, r1p, rep = regen()
    fr0 = load_fb(r0p); fr1 = load_fb(r1p); fre = load_fb(rep)
    pal, idxmap = build_palette([fr0, fr1, fre])
    idx0 = to_idx(fr0, idxmap); idx1 = to_idx(fr1, idxmap); idxe = to_idx(fre, idxmap)
    bbox = diff_bbox(idx0, idx1)
    x0, x1, y0, y1 = bbox
    pw, ph = x1 - x0 + 1, y1 - y0 + 1
    patch = [[idx1[y0 + j][x0 + i] for i in range(pw)] for j in range(ph)]
    if '--verify' in sys.argv:
        recon = [row[:] for row in idx0]
        for j in range(ph):
            for i in range(pw):
                recon[y0 + j][x0 + i] = patch[j][i]
        mism = sum(1 for y in range(H) for x in range(W) if recon[y][x] != idx1[y][x])
        print("[atbsel verify] row1 reconstruction (frame0+patch vs real row1 capture): "
              "mismatch=%d/%d" % (mism, W * H))
        print("[atbsel verify] palette=%d colors, patch bbox=(x%d..%d,y%d..%d) %dx%d = %d px "
              "(%.1f%% of frame)" % (len(pal), x0, x1, y0, y1, pw, ph, pw * ph, 100.0 * pw * ph / (W * H)))
    if '--verify' not in sys.argv or '--emit' in sys.argv:
        with open(OUT_H, 'w') as fp:
            emit(fp, pal, idx0, patch, x0, y0, pw, ph)
            rom = _rom(); pens = pen_rows(rom)
            # the EMPTY box comes from the reveal's frame 1: line 1 is untouched there, and line 0 is
            # empty right of that frame's pen. Bake the whole patch rect from it and let the renderer
            # take the settled pixels only left of the pen.
            ew, eh = pw, ph
            empty = [[idxe[y0 + j][x0 + i] for i in range(ew)] for j in range(eh)]
            # line 0 between ACTIVE's first pen and WAIT's: the WAIT frame has its first glyph pair there, the
            # ACTIVE reveal's first frame has the empty box.
            fra = active_first()
            ia = [[idxmap.get(c) for c in row] for row in fra]
            ca, cw = TEXT_X + pens[0][0][0], TEXT_X + pens[1][0][0]
            rows = [y for y in range(y0, y0 + ph) for x in range(ca, cw) if ia[y][x] != idxe[y][x]]
            for y in sorted(set(rows)):
                for x in range(ca, cw):
                    empty[y - y0][x - x0] = ia[y][x]
            print('atbsel: x %d..%d, y %d..%d from the ACTIVE reveal first frame' % (ca, cw - 1, min(rows), max(rows)))
            emit_typewriter(fp.write, pens, empty, x0, y0, ew, eh)
        print("wrote %s (%d colors, %dx%d frame, patch %dx%d)" % (OUT_H, len(pal), W, H, pw, ph))

if __name__ == '__main__':
    main()
