#!/usr/bin/env python3
"""Bake the CT battle ACTION-FEEDBACK digit popup (the rising DAMAGE NUMBERS) into
src/data/battle_fx.h + verify vs oracle.

Derivation dossier: disasm/notes/battle_actionfx.md (P1). When a hit lands, the battle
module draws the damage value as decimal digits in OBJ tiles 0x190+d ('0'..'9'), palette 0,
8x8 (sz=0), prio 3, over the target -- rising in a parabolic hop then holding, then vanishing
just before the HP write. Proven byte-exact for two independent hits:
  * Crono -> GATO  "17"  (GATO curHP 76->59 = -17)  -> tiles 0x191,0x197
  * GATO  -> Crono "14"  (Crono curHP 70->56 = -14) -> tiles 0x191,0x194
The digit-to-tile map is tile = 0x190 + digit (rendered strip 0x190..0x199 = '0'..'9', S-verify).

Baked (all read from the oracle, deterministic):
  - ctBattleFxDigitTile[10][32]  -- OBJ 8x8 tiles 0x190..0x199 ('0'..'9'), 4bpp planar, from the
    in-battle OBJ VRAM (0x190 lands at byte 0x3200, inside the battle-boot OBJ upload region
    0x3000-0x3C00 -- same provenance as the stance pose tiles, battle_stance.md S1).
  - ctBattleFxDigitPal[16]  -- OBJ palette 0 (BGR555 LE); the digits use indices 1(white fill=
    0x7FFF)/2/3(dark outline); index 0 transparent. pal0 is stable across the fight (verified).
  - ctBattleFxFlashPal[16]  -- OBJ palette 4 = the HURT/HIT FLASH palette: entries 1..15 all
    0x7FFF (white), so the target's OWN pose tiles rendered in pal4 = a solid-white silhouette
    (P3; the flash is a palette SWAP 1/5/6 -> 4 on the target's sprites, not an overlay).
  - ctBattleFxRiseDY[CT_FX_RISE_N]  -- the popup's vertical offset from its rest row, per render
    frame, index 0 = birth. MEASURED byte-exact from a step-1 dense --peek sweep and confirmed
    target-independent (identical for the "17"/GATO and "14"/Crono popups). After the table the
    number HOLDS at rest (dy=0) for ~31 frames, then vanishes (lifetime ~56 frames).

Render rule (composite as OBJ, prio 3, over the combatants):
  leftX = numberOriginX(target)         # left digit x; origin measured per target (see spec)
  for k,ch in enumerate(decimal(value)):# no leading zeros
      draw 8x8 tile (0x190+ch) pal0 at ( leftX + 8*k , restY + ctBattleFxRiseDY[min(age,N-1)] )
  # an OBJ pixel at OAM (x,y) lands at oracle fb true-pixel (x, y+8) -- same as the stance bake.

Run from repo root (needs Chrono_Trigger_USA_SNES.sfc + ct_assets.dat + gato_press_action.state in
cwd). Emits src/data/battle_fx.h. With --verify, regenerates the settled "17"(GATO f540) and "14"(Crono
f755) frames, reconstructs the digits from the baked tiles+pal0 at the oracle's own digit OAM
positions, and pixel-diffs vs the oracle fb (verified 73/73 + 79/79 opaque px, 0 mismatch d>24).
"""
import os, sys, subprocess
sys.path.insert(0, 'tools')
from ctlib import oam

ROM   = os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc')
EXE   = os.environ.get('CT_EXE', 'build/ct.exe')
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
SP    = os.environ.get('CT_SCRATCH', 'build')
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_fx.h')

DIGIT_BASE = 0x190                       # OBJ tile of '0'; digit d -> tile DIGIT_BASE + d
NDIGIT     = 10
DIGIT_W    = 8                           # px between digit columns (8x8 sprites, sz=0)
DIGIT_PAL  = 0
FLASH_PAL  = 4
MISS_BASE  = 0x19A                        # the "MISS" glyph = OBJ tiles 0x19A (left) + 0x19B (right), pal0
MISS_NTILE = 2                            #   (battle_miss_evade.md §2: spawned by C1/E8E2 on a physical miss)

# The A-mash that boots the GATO fight and drives Crono->GATO / GATO->Crono physicals (loop dossier
# mash). build(F) is deterministic; --peek to exactly F frames reproduces the frame byte-identical.
AON = AOFF = 16
def build(F):
    seq = []; n = 0
    while n < F:
        on = min(AON, F - n)
        if on: seq.append("100:%d" % on); n += on
        if n >= F: break
        off = min(AOFF, F - n)
        if off: seq.append("0:%d" % off); n += off
    return ','.join(seq)

# Verify frames: settled popups (dy=0, held) -- the clean byte-exact instants.
VERIFY = [(540, "17 over GATO",  0x6130, 76, 59),   # (frame, label, curHP addr, before, after)
          (755, "14 over Crono", 0x5E30, 70, 56)]
# The flash palette (pal4) is only all-white DURING a flash; f480 = GATO's hit-flash (f476-484).
FLASH_FRAME = 480

# ---- P1 the vertical rise, MEASURED byte-exact (dossier P1, step-1 dense sweep; index 0 = birth).
#      Offset from the number's rest row. Identical for both observed targets (target-independent).
RISE_DY = [7, -1, -7, -10, -12, -13, -14, -15, -16,
           -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0]
HOLD_FRAMES = 32                          # frames held at rest (dy=0); battle_hp_timing.md §c.4: bumped
                                          # 31->32 so LIFETIME 56->57 = the MEASURED 57 present-frames
                                          # (popup f572..f628 inclusive = 57), matching the late-spawn timing.
LIFETIME    = len(RISE_DY) + HOLD_FRAMES  # 57 render frames total (25 rise + 32 hold)

def peek(frame, out):
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, out, '--btnscript', build(frame)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out

def dt8(vram, t):
    o = (t & 0x1FF) * 32
    px = [[0]*8 for _ in range(8)]
    for r in range(8):
        p0, p1, p2, p3 = vram[o+r*2], vram[o+r*2+1], vram[o+16+r*2], vram[o+16+r*2+1]
        for c in range(8):
            b = 7 - c
            px[r][c] = ((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
    return px

def rgb555(w):
    rep = lambda c: (c<<3)|(c>>2)
    return (rep(w&31), rep((w>>5)&31), rep((w>>10)&31))

# ------------------------------------------------------------------ emit
def emit(fp, vram, cgram, fpal):
    W = fp.write
    W("// AUTO-GENERATED by tools/gen/battle_fx.py -- DO NOT EDIT.\n")
    W("// CT battle ACTION FEEDBACK: the rising DAMAGE-NUMBER popup + the hurt/hit flash palette.\n")
    W("// Dossier: disasm/notes/battle_actionfx.md. Baked byte-exact from the oracle (GATO fight).\n")
    W("// Digit d -> OBJ tile 0x%03X + d ('0'..'9'), palette %d, 8x8 (sz=0), prio 3. A digit OBJ\n" % (DIGIT_BASE, DIGIT_PAL))
    W("// pixel at OAM (x,y) lands at oracle fb true-pixel (x, y+8) (verified 73/73 + 79/79 px).\n")
    W("#ifndef CT_BATTLE_FX_H\n#define CT_BATTLE_FX_H\n#include <stdint.h>\n\n")

    W("#define CT_FX_DIGIT_BASE 0x%03X   // OBJ tile of '0'; digit d -> CT_FX_DIGIT_BASE + d\n" % DIGIT_BASE)
    W("#define CT_FX_DIGIT_W    %d       // px between digit columns (right-adjacent 8x8 cells)\n" % DIGIT_W)
    W("#define CT_FX_DIGIT_PAL  %d       // OBJ palette for the digits\n" % DIGIT_PAL)
    W("#define CT_FX_FLASH_PAL  %d       // OBJ palette used for the target's hurt/hit FLASH (all white)\n" % FLASH_PAL)
    W("#define CT_FX_MISS_BASE  0x%03X   // \"MISS\" glyph left OBJ tile (+1 = right); pal0, sz0, prio3 (battle_miss_evade.md)\n" % MISS_BASE)
    W("#define CT_FX_MISS_NTILE %d       // \"MISS\" glyph width in 8x8 OBJ tiles (spawned on a physical to-hit MISS)\n" % MISS_NTILE)
    W("#define CT_FX_RISE_N     %d       // entries in ctBattleFxRiseDY (birth..rest)\n" % len(RISE_DY))
    W("#define CT_FX_HOLD       %d       // frames held at rest (dy=0) after the rise, then vanish\n" % HOLD_FRAMES)
    W("#define CT_FX_LIFETIME   %d       // total popup lifetime in render frames (rise + hold)\n\n" % LIFETIME)

    # --- digit tiles 0x190..0x199 (4bpp, 32 B each) ---
    W("// Digit OBJ 8x8 tiles '0'..'9' (4bpp SNES planar, 32 B each), verbatim from the in-battle\n")
    W("// OBJ VRAM (tile 0x%03X = byte 0x%04X, inside the battle-boot OBJ upload 0x3000-0x3C00).\n" % (DIGIT_BASE, DIGIT_BASE*32))
    W("static const uint8_t ctBattleFxDigitTile[%d][32] = {\n" % NDIGIT)
    for d in range(NDIGIT):
        o = (DIGIT_BASE + d) * 32
        row = ','.join('0x%02X' % vram[o+i] for i in range(32))
        W("  {%s}, // '%d' (tile 0x%03X)\n" % (row, d, DIGIT_BASE+d))
    W("};\n\n")

    # --- "MISS" glyph tiles 0x19A/0x19B (4bpp, 32 B each) ---
    W("// The \"MISS\" glyph = OBJ 8x8 tiles 0x%03X (left) + 0x%03X (right), palette %d, sz0, prio3. On a\n" % (MISS_BASE, MISS_BASE+1, DIGIT_PAL))
    W("// physical to-hit MISS the module spawns THIS (C1/E8E2, popup type 5) instead of the damage digits,\n")
    W("// at the target's damage-number anchor (oracle leftX 128 / centerX 136 over GATO, 16px = 2 tiles).\n")
    W("// Verbatim from the in-battle OBJ VRAM (same 0x3000-0x3C00 boot upload as the digit strip); pixel-\n")
    W("// verified 61/61 + 58/58 opaque px, 0 mismatch, vs a forced-miss fb (--verify, evade patched high).\n")
    W("static const uint8_t ctBattleFxMissTile[%d][32] = {\n" % MISS_NTILE)
    for k in range(MISS_NTILE):
        o = (MISS_BASE + k) * 32
        row = ','.join('0x%02X' % vram[o+i] for i in range(32))
        W("  {%s}, // tile 0x%03X (\"MISS\" %s)\n" % (row, MISS_BASE+k, 'left' if k == 0 else 'right'))
    W("};\n\n")

    # --- digit palette (OBJ pal0) ---
    W("// OBJ palette %d (BGR555 LE). Digits use idx 1 (white fill 0x7FFF) + 2/3 (outline); 0 = clear.\n" % DIGIT_PAL)
    W("static const uint16_t ctBattleFxDigitPal[16] = {")
    dpal = [cgram[(128+DIGIT_PAL*16+c)*2] | (cgram[(128+DIGIT_PAL*16+c)*2+1]<<8) for c in range(16)]
    W(','.join('0x%04X' % w for w in dpal))
    W("};\n")

    # --- heal/restore digit palette: the ROM's sub-palette D1/4D88, next to the damage one D1/4D80 ---
    rom = open(ROM, 'rb').read()
    sub = lambda a: [rom[a + 2*i] | (rom[a + 2*i + 1] << 8) for i in range(4)]
    dmg, heal = sub(0x114D80), sub(0x114D88)                    # D1/4D80, D1/4D88 (HiROM, no copier header)
    assert dmg == dpal[:4], 'D1/4D80 is not the baked damage palette'
    for a in (0x114D80, 0x114D88):
        assert rom.count(rom[a:a + 8]) == 1, 'the sub-palette at %X is not unique in the ROM' % a
    W("// THE HEAL/RESTORE NUMBER IS GREEN, and it is a different OBJ PALETTE, not a different tile. C1/5ADB\n")
    W("// (ShowDamage) defaults the OAM attribute to 0x31 (`15B49 LDA #31 / STA $8C` -> prio 3, OBJ pal 0) and\n")
    W("// swaps it to 0x39 (pal 4) for the RESTORE kinds -- `15B99 CMP #03 / BEQ` keeps 0x31 for ordinary damage,\n")
    W("// `15B9D CMP #01 / BEQ 15BAD` and `15BA6 CMP #02 / BNE` route HP-restore (kind 1, writer C1/DAC9 for an\n")
    W("// item, C1/DDA0 for a healing tech) and MP-restore (kind 2, C1/DB54) to `15BAD LDA #39`. The two 4-colour\n")
    W("// digit sub-palettes are adjacent in ROM and each occurs EXACTLY ONCE in the whole 4 MB image:\n")
    W("//   D1/4D80 = %s   damage  (== ctBattleFxDigitPal[0..3], already baked)\n" % ' '.join('%04X' % w for w in dmg))
    W("//   D1/4D88 = %s   restore (idx1 = 0x%04X -> RGB 132,255,148 = the green ink)\n"
      % (' '.join('%04X' % w for w in heal), heal[1]))
    W("// Verified on a rendered oracle frame, not just in ROM: the Elixir's HP number over Marle draws GREEN\n")
    W("// (gato_elixir.state, popup window f532-f590 measured by `--monitor \"A3D1:1\"`).\n")
    W("// Indices 4-15 are never reached by a digit or the MISS glyph -- the ten digit tiles use colour indices\n")
    W("// 0,1,2,3 ONLY (counted over ctBattleFxDigitTile: 159/159/93/229 pixels, nothing above 3) -- so they are\n")
    W("// filled with pal0's values rather than a second guess at OBJ pal4, whose upper half is dual-use (the\n")
    W("// all-white hurt flash on a damage frame, the heal-effect sprite colours on a heal frame).\n")
    W("static const uint16_t ctBattleFxHealPal[16] = {")
    W(','.join('0x%04X' % w for w in heal + dpal[4:]))
    W("};\n\n")

    # --- flash palette (OBJ pal4, all white) -- read from a FLASH frame (fpal), not the bake frame ---
    W("// OBJ palette %d = the HURT/HIT FLASH: entries 1..15 all white (0x7FFF). Render the TARGET's\n" % FLASH_PAL)
    W("// own pose tiles (from battle_stance.h) in this palette for a solid-white silhouette flash.\n")
    W("// (Read from the GATO hit-flash frame f%d; pal4 is a normal OBJ palette outside a flash.)\n" % FLASH_FRAME)
    W("static const uint16_t ctBattleFxFlashPal[16] = {")
    W(','.join('0x%04X' % (fpal[(128+FLASH_PAL*16+c)*2] | (fpal[(128+FLASH_PAL*16+c)*2+1]<<8)) for c in range(16)))
    W("};\n\n")

    # --- rise table ---
    W("// The damage number's vertical offset from its rest row, per render frame (index 0 = birth).\n")
    W("// MEASURED byte-exact (dossier P1) and target-independent. Parabolic hop: born +7 below rest,\n")
    W("// up to -16 (peak, idx 8), linear +1/frame back to 0 (idx 24=rest). Then hold dy=0 (CT_FX_HOLD).\n")
    W("static const int8_t ctBattleFxRiseDY[CT_FX_RISE_N] = {%s};\n\n" % ','.join('%d' % v for v in RISE_DY))

    W("// Number origin (dossier P1, MEASURED). leftX = left-digit x; restY = rest row (dy applied on\n")
    W("// top). Horizontal: centered on the ENEMY target's sprite center (GATO exact); PC target uses a\n")
    W("// stored per-combatant anchor (formula = boundary). Observed constants (GATO fight):\n")
    W("//   GATO  target: leftX 128, restY 96   (2-digit; centerX 136 ~= GATO sprite centerX 135)\n")
    W("//   Crono target: leftX 126, restY 129\n")
    W("#endif // CT_BATTLE_FX_H\n")

# ------------------------------------------------------------------ verify
def verify():
    total = []
    for frame, label, hpaddr, before, after in VERIFY:
        prefix = peek(frame, os.path.join(SP, 'fx_v%d.raw' % frame))
        vram = open(prefix+'.vram', 'rb').read()
        cgram = oam.load_cgram(prefix+'.oam')
        fb = open(prefix, 'rb').read()
        w = open(prefix+'.wram', 'rb').read()
        sp = oam.decode(oam.load(prefix+'.oam'))
        hp = w[hpaddr] | (w[hpaddr+1] << 8)
        digs = [s for s in sp if s['y'] < 220 and -32 < s['x'] < 256 and DIGIT_BASE <= s['tile'] < DIGIT_BASE+NDIGIT]
        digs.sort(key=lambda s: s['x'])
        diff = tot = 0
        for s in digs:
            px = dt8(vram, s['tile'])
            for yy in range(8):
                for xx in range(8):
                    v = px[yy][xx]
                    if v == 0: continue
                    sx = 7-xx if s['hflip'] else xx
                    sy = 7-yy if s['vflip'] else yy
                    X, Y = s['x']+sx, s['y']+sy
                    col = rgb555(cgram[(128+s['pal']*16+v)*2] | (cgram[(128+s['pal']*16+v)*2+1]<<8))
                    o = ((2*(Y+8))*512 + 2*X) * 4
                    d = abs(col[0]-fb[o+3]) + abs(col[1]-fb[o+2]) + abs(col[2]-fb[o+1])
                    tot += 1
                    if d > 24: diff += 1
        num = ''.join(str(s['tile']-DIGIT_BASE) for s in digs)
        print("[battle-fx verify] f%d %-14s number=\"%s\" curHP %d->%d (=%+d)  digits @%s"
              % (frame, label, num, before, hp, hp-before, ' '.join("(%d,%d)"%(s['x'],s['y']) for s in digs)))
        print("    digit opaque px exact=%d/%d  mismatch(d>24)=%d" % (tot-diff, tot, diff))
        total.append((tot-diff, tot, diff))
    return total

# Verify the "MISS" glyph: patch GATO's evade ($6168 = block+0x3B) HIGH so Crono's physical MISSES, mash-peek
# a settled miss-popup frame, and pixel-diff the baked 0x19A/0x19B tiles (pal0) vs the oracle fb. Proves the
# glyph + palette + anchor byte-exact, the same way verify() proves the damage digits.
def verify_miss():
    from ctlib import lake
    # 1. Capture a MID-BATTLE Crono-command-ready state (battle blocks populated). Patching evade in the
    #    PRE-battle state does NOT work -- battle-init (C1/CF3B stat copy from ROM CC/4700) overwrites it. A
    #    HOLD drive (one A boots the fight, then hold) parks Crono at the command menu; PEEK_SAVE snapshots it.
    ready = os.path.join(SP, 'fx_miss_ready.state')
    env = dict(os.environ, PEEK_SAVE=ready)
    subprocess.run([os.path.abspath(EXE), ROM, '--peek', STATE, os.path.join(SP, 'fx_miss_pre.raw'),
                    '--btnscript', '100:8,0:400'], check=True, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # 2. Patch GATO evade ($6168 = block+0x3B) HIGH in the mid-battle state so Crono MISSES.
    payload, fmt = lake.unwrap(open(ready, 'rb').read())
    pl = bytearray(payload); pl[lake.WRAM_OFF + 0x6168] = 0xFF
    mstate = os.path.join(SP, 'fx_miss.state'); open(mstate, 'wb').write(lake.wrap(bytes(pl)))
    # 3. Mash-peek and SCAN for the settled "MISS" popup (RNG-robust: a specific frame may roll a rare hit).
    for frame in (135, 130, 140, 150, 125, 145, 120, 155):
        prefix = os.path.join(SP, 'fx_miss.raw')
        subprocess.run([os.path.abspath(EXE), ROM, '--peek', mstate, prefix, '--btnscript', build(frame)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        miss = [s for s in oam.decode(oam.load(prefix+'.oam'))
                if MISS_BASE <= s['tile'] < MISS_BASE+MISS_NTILE and s['y'] < 220]
        if miss:
            break
    if not miss:
        print("[battle-fx miss] NO \"MISS\" glyph found in the scan window (RNG rolled hits -- re-tune)"); return
    miss.sort(key=lambda s: s['x'])
    vram = open(prefix+'.vram', 'rb').read(); cgram = oam.load_cgram(prefix+'.oam'); fb = open(prefix, 'rb').read()
    diff = tot = 0
    for s in miss:
        px = dt8(vram, s['tile'])
        for yy in range(8):
            for xx in range(8):
                v = px[yy][xx]
                if v == 0: continue
                col = rgb555(cgram[(128+s['pal']*16+v)*2] | (cgram[(128+s['pal']*16+v)*2+1]<<8))
                o = ((2*(s['y']+yy+8))*512 + 2*(s['x']+xx)) * 4
                d = abs(col[0]-fb[o+3]) + abs(col[1]-fb[o+2]) + abs(col[2]-fb[o+1])
                tot += 1
                if d > 24: diff += 1
    print("[battle-fx miss] f%d \"MISS\" tiles %s @%s  opaque px exact=%d/%d  mismatch(d>24)=%d"
          % (frame, ','.join('0x%03X' % s['tile'] for s in miss),
             ' '.join('(%d,%d)' % (s['x'], s['y']) for s in miss), tot-diff, tot, diff))

def main():
    # bake from the settled "17" frame (has the digit strip in VRAM + pal0/pal4 in CGRAM)
    prefix = peek(540, os.path.join(SP, 'fx_bake.raw'))
    vram = open(prefix+'.vram', 'rb').read()
    cgram = oam.load_cgram(prefix+'.oam')
    fprefix = peek(FLASH_FRAME, os.path.join(SP, 'fx_flash.raw'))
    fpal = oam.load_cgram(fprefix+'.oam')
    if '--verify' in sys.argv:
        verify()
        verify_miss()
    if '--verify' not in sys.argv or '--emit' in sys.argv:
        with open(OUT_H, 'w') as fp:
            emit(fp, vram, cgram, fpal)
        print("wrote %s (%d digit tiles + %d MISS tiles, pal0+pal4, %d-entry rise table)"
              % (OUT_H, NDIGIT, MISS_NTILE, len(RISE_DY)))

if __name__ == '__main__':
    main()
