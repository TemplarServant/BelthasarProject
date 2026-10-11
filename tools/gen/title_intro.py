#!/usr/bin/env python3
r"""Regenerate src/data/title_intro.h -- the data the TITLE SEQUENCE's engine runs on.

disasm/notes/title_intro.md is the derivation.  The port does not bake pictures: it runs the
overlay's own machine (the $C3 script interpreter, the NMI's hook lists, the tasks, the sprite
drawers, the ROM's palette-job runner) and renders what that machine produces.  So what is
generated here is only what cannot be computed:

  * the GRAPHICS.  The overlay builds them at boot -- it transposes 160 4bpp tiles into the
    SNES's interleaved mode-7 pair ($7E:5E3C) and decompresses the mode-2 tilemaps/characters
    out of blobs -- so they are captured from the ORACLE'S OWN VRAM, with refusals if anything
    about the capture is not what the derivation says it is.
  * the overlay's own DATA: the sequence script ($7E:5B05), the sprite packages ($7E:603F) and
    the letter-slide tables ($7E:5D5D/$5D67).  Byte-code and byte tables, read out of the
    decompressed overlay image the same way.
  * the SEED: the machine's WRAM state at the frame the port calls swing frame 0 (oracle boot
    frame 140 -- measured: its mode-7 matrix is the one the PPU renders boot frame 141 with).
  * the ROM's own sine table, $C0:F900, read from the ROM.

⭐ What is NOT here any more: the per-frame mode-7 matrix.  It used to be captured because one
carry per multiply looked underivable; `$7E:31BF` zeroes $F6 (and so $F7) and then ROLs the WORD
at $F7, which rotates BIT 7 OF THE STALE $F8 into the middle ADC.  $F8 is not noise -- it is the
quotient the same driver's divide ($7E:339F) left there, shifted one place left by each
following multiply.  Modelled, the whole swing computes exactly (title_intro.md section 3.1).

Run from the repo root; needs the ROM + build/ct.exe.
"""
import os
import re
import subprocess
import sys

ROM = sys.argv[1] if len(sys.argv) > 1 else 'Chrono_Trigger_USA_SNES.sfc'
EXE = os.path.abspath('build/ct.exe')
OUT = 'src/data/title_intro.h'
TMP = 'build/_titleintro'
SEED_FRAME = 140                      # ⭐ the port's swing frame 0 (see the module header)
NL = chr(10)
os.makedirs('build', exist_ok=True)


def run(args, **kw):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, **kw)


# ---- the captures ------------------------------------------------------------------------------
# The SEED is the overlay's live WRAM at boot frame 140; the graphics come from a frame deep in the
# logo phase (the mode-2 tilemaps are only meaningful once they are loaded), and a second frame
# proves they are static.
run([EXE, ROM, '--fb', TMP + '_seed.raw', '--frames', str(SEED_FRAME)],
    env=dict(os.environ, PEEK_SAVE=TMP + '_seed.state'))
run([EXE, ROM, '--fb', TMP + '_gfx.raw', '--frames', '900'],
    env=dict(os.environ, PEEK_SAVE=TMP + '_gfx.state'))
run([EXE, ROM, '--peek', TMP + '_gfx.state', TMP + '_a.raw', '--btnscript', '0:200'])
run([EXE, ROM, '--peek', TMP + '_gfx.state', TMP + '_b.raw', '--btnscript', '0:600'])

seed = open(TMP + '_seed.raw.wram', 'rb').read()
vram = open(TMP + '_a.raw.vram', 'rb').read()
vramB = open(TMP + '_b.raw.vram', 'rb').read()

# ⭐ The only VRAM the sequence ever writes after the boot load is the 64-word OFFSET-PER-TILE
# table at word $7000 (the vblank hook $7E:6212 DMAs it every frame); everything else is static,
# and the port depends on that.
for i in range(0, 0x10000, 2):
    if vram[i:i + 2] != vramB[i:i + 2] and not (0xE000 <= i < 0xE080):
        sys.exit("REFUSING: VRAM word $%04X changes during the logo phase ($%04X -> $%04X); the "
                 "port bakes it as static." % (i // 2, vram[i] | (vram[i + 1] << 8),
                                               vramB[i] | (vramB[i + 1] << 8)))

u16 = lambda b, a: b[a] | (b[a + 1] << 8)
W = lambda a: vram[a * 2] | (vram[a * 2 + 1] << 8)

# ---- the mode-7 texture (the swing) -------------------------------------------------------------
words = [W(i) for i in range(0x4000)]
tmap = [w & 0xFF for w in words]              # the mode-7 tilemap: the LOW byte
chr8 = [w >> 8 for w in words]                # the mode-7 characters: the HIGH byte

used = [t for t in set(tmap) if t]
if not used:
    sys.exit("REFUSING: the mode-7 tilemap is empty -- the capture is not inside the sequence.")
NT = max(used) + 1
if NT > 160:
    sys.exit("REFUSING: tilemap references tile %d, past the 160 tiles $7E:5E3C converts." % NT)
rows = [r for r in range(128) if any(tmap[r * 128 + c] for c in range(128))]
cols = [c for c in range(128) if any(tmap[r * 128 + c] for r in range(128))]
R0, R1, C0, C1 = rows[0], rows[-1], cols[0], cols[-1]
MW, MH = C1 - C0 + 1, R1 - R0 + 1

rom = open(ROM, 'rb').read()
sin = rom[0xF900:0xF900 + 1024]               # $C0:F900 -- HiROM bank $C0 == file offset 0

# ---- the overlay's own data ----------------------------------------------------------------------
SCRIPT_BASE, SCRIPT_END = 0x5B05, 0x5B99      # the sequence script (both halves, contiguous)
PKG_BASE, PKG_END = 0x603F, 0x6212            # the eleven sprite packages, contiguous
if seed[SCRIPT_BASE] != 0xFA or u16(seed, SCRIPT_BASE + 1) != 0x3749:
    sys.exit("REFUSING: $7E:%04X does not open with `FA 3749` (add the script runner to the task "
             "list) -- this is not the title's script." % SCRIPT_BASE)
p = PKG_BASE                                  # the packages must tile the region exactly
npkg = 0
while p < PKG_END:
    p += 1 + (seed[p] - 1) * 4
    npkg += 1
if p != PKG_END or npkg != 11:
    sys.exit("REFUSING: the sprite packages do not tile $%04X..$%04X (ended at $%04X after %d)."
             % (PKG_BASE, PKG_END, p, npkg))
slide_pkg = [u16(seed, 0x5D5D + i * 2) for i in range(5)]
slide_spd = [u16(seed, 0x5D67 + i * 2) for i in range(5)]
if any(not (PKG_BASE <= a < PKG_END) for a in slide_pkg):
    sys.exit("REFUSING: a letter-slide entry ($7E:5D5D) does not point into the package region: %s"
             % [hex(a) for a in slide_pkg])

# ---- the seed state -------------------------------------------------------------------------------
TASKS = {0x3749, 0x5BCF, 0x37FA, 0x5FFE, 0x5D32, 0x5B99, 0x5D01, 0x5C98,
         0x5D71, 0x5D20, 0x5BBF, 0x5E1E, 0x34AB}
VBLS = {0x388B, 0x380B, 0x3843, 0x5C57, 0x6212}
seedTask = [u16(seed, 0x0920 + i * 2) for i in range(16)]
seedVbl = [u16(seed, 0x0940 + i * 2) for i in range(8)]
for t in seedTask:
    if t and t not in TASKS:
        sys.exit("REFUSING: the seed's task list holds $%04X, which the port does not model." % t)
for h in seedVbl:
    if h and h not in VBLS:
        sys.exit("REFUSING: the seed's vblank list holds $%04X, which the port does not model." % h)
if u16(seed, 0x0008) != 1:
    sys.exit("REFUSING: the sequence's frame counter $7E:0008 is %d at the seed frame, not 1 -- "
             "the seed is not the sequence's own second NMI." % u16(seed, 0x0008))
for r in range(12):                           # every palette-job slot must be free at the seed
    if seed[0x520 + r * 12]:
        sys.exit("REFUSING: palette-job record %d is busy at the seed frame ($%02X)."
                 % (r, seed[0x520 + r * 12]))
# $038D is the brightness the NMI pushes to $2100 every frame and the value $7E:34C2's fade-out walks
# down to zero; a seed taken mid-fade would bake a dimmed title.
if seed[0x0389] != seed[0x038A] or seed[0x0388]:
    sys.exit("REFUSING: the song watcher is mid-flight at the seed frame ($0388=%02X $0389=%02X "
             "$038A=%02X); C3/09A4 would load a song on the port's first frame."
             % (seed[0x0388], seed[0x0389], seed[0x038A]))
if seed[0x038D] != 0x0F:
    sys.exit("REFUSING: the brightness shadow $7E:038D is $%02X at the seed frame, not $0F -- the "
             "capture is inside a fade." % seed[0x038D])

# ---- the mode-2 VRAM window ------------------------------------------------------------------------
VBASE, VWORDS = 0x4000, 0x3000                # OBJ chr $4000, BG chr $5000, BG2 map $6000, BG1 map $6400
bg1 = [W(0x6400 + i) for i in range(2048)]
bg2 = [W(0x6000 + i) for i in range(1024)]
if not any(bg1) or not any(bg2):
    sys.exit("REFUSING: a mode-2 tilemap is empty -- the graphics capture is not in the logo phase.")
if max((e & 0x3FF) for e in bg1 + bg2 if e) * 16 + 0x5000 >= VBASE + VWORDS:
    sys.exit("REFUSING: a mode-2 tilemap references character data past the captured window.")


def emit(f, name, vals, per, fmt='%d'):
    f.write('static const %s[%d] = {\n' % (name, len(vals)))
    for i in range(0, len(vals), per):
        f.write('  ' + ','.join(fmt % v for v in vals[i:i + per]) + ',\n')
    f.write('};\n')


with open(OUT, 'w', encoding='utf-8') as f:
    f.write("// title_intro.h - what the TITLE SEQUENCE's engine runs on (disasm/notes/title_intro.md).\n"
            "// GENERATED by tools/gen/title_intro.py from the oracle's own VRAM/WRAM and the ROM's own\n"
            "// sine table. DO NOT EDIT.\n"
            "//\n"
            "// The port RUNS the overlay's machine rather than baking its output, so this file holds\n"
            "// only what a machine cannot compute: the graphics the boot load built, the overlay's own\n"
            "// byte-code and package tables, the seed state, and $C0:F900.\n\n")

    f.write('// ---- the MODE-7 texture (the pendulum swing) -------------------------------------\n')
    f.write('#define CT_TITLE_M7_TILES %d\n' % NT)
    f.write('#define CT_TITLE_M7_MAP_R0 %d\n#define CT_TITLE_M7_MAP_C0 %d\n' % (R0, C0))
    f.write('#define CT_TITLE_M7_MAP_W %d\n#define CT_TITLE_M7_MAP_H %d\n\n' % (MW, MH))
    f.write('// The mode-7 CHARACTERS: %d tiles, 8x8 8bpp (the HIGH byte of VRAM words 0..%d).\n'
            % (NT, NT * 64 - 1))
    emit(f, 'unsigned char ctTitleM7Chr', [chr8[t * 64 + i] for t in range(NT) for i in range(64)], 64)
    f.write('\n// The mode-7 TILEMAP, cropped to its non-zero bounding box (the field is 128x128).\n')
    emit(f, 'unsigned char ctTitleM7Map',
         [tmap[r * 128 + c] for r in range(R0, R1 + 1) for c in range(C0, C1 + 1)], MW)
    f.write('#define CT_TITLE_M7_X 32\n#define CT_TITLE_M7_Y -64\n')

    f.write('\n// ---- the MODE-2 VRAM window: OBJ chr $4000, BG chr $5000, BG2 map $6000, BG1 map\n'
            '// $6400. Captured whole and addressed exactly as the PPU addresses it, so the render\n'
            '// is LakeSnes\' own fetch (snes/ppu.c) rather than a re-decoding of the tiles.\n')
    f.write('#define CT_TITLE_VRAM_BASE 0x%04X\n#define CT_TITLE_VRAM_WORDS 0x%04X\n' % (VBASE, VWORDS))
    emit(f, 'unsigned short ctTitleVram', [W(VBASE + i) for i in range(VWORDS)], 16, '0x%04X')

    f.write('\n// ---- the overlay\'s own DATA -------------------------------------------------------\n')
    f.write('// The SEQUENCE SCRIPT: the byte-code the $C3 interpreter (C3/0B03) runs with DP = $0A00.\n')
    f.write('#define CT_TITLE_SCRIPT_BASE 0x%04X\n' % SCRIPT_BASE)
    emit(f, 'unsigned char ctTitleScript', list(seed[SCRIPT_BASE:SCRIPT_END]), 16, '0x%02X')
    f.write('\n// The eleven SPRITE PACKAGES: [count+1][tile, attr, dx, dy] x n, as C3/0D0E reads them.\n')
    f.write('#define CT_TITLE_PKG_BASE 0x%04X\n' % PKG_BASE)
    emit(f, 'unsigned char ctTitlePkg', list(seed[PKG_BASE:PKG_END]), 16, '0x%02X')
    f.write('\n// The five CHRONO letters the task $7E:5D71 slides: package pointer + speed (1/16 px).\n')
    emit(f, 'unsigned short ctTitleSlidePkg', slide_pkg, 5, '0x%04X')
    emit(f, 'unsigned short ctTitleSlideSpd', slide_spd, 5, '%d')

    f.write('\n// ---- the PALETTE ------------------------------------------------------------------\n'
            '// $7E:2000 is the base the palette jobs blend FROM; $7E:2200 is the shadow the vblank\n'
            '// hook $7E:388B DMAs into CGRAM every frame. Raw 5-5-5, expanded on the way out.\n')
    emit(f, 'unsigned short ctTitlePal0', [u16(seed, 0x2000 + i * 2) for i in range(256)], 16, '0x%04X')

    f.write('\n// ---- the SEED: the machine\'s WRAM at oracle boot frame %d --------------------------\n'
            % SEED_FRAME)
    emit(f, 'unsigned short ctTitleSeedReg', [u16(seed, 0x0A00 + i * 2) for i in range(16)], 8, '0x%04X')
    emit(f, 'unsigned short ctTitleSeedCtr', [u16(seed, 0x0A22 + i * 2) for i in range(16)], 8, '0x%04X')
    f.write('#define CT_TITLE_SEED_IP  0x%04X\n' % u16(seed, 0x0A20))
    f.write('#define CT_TITLE_SEED_RET 0x%04X\n' % u16(seed, 0x0A30))
    emit(f, 'unsigned short ctTitleSeedTask', seedTask, 8, '0x%04X')
    emit(f, 'unsigned short ctTitleSeedVbl', seedVbl, 8, '0x%04X')
    CELLS = (0x08, 0x11, 0x13, 0x15, 0x17, 0x44, 0x5A, 0x5C, 0x5E, 0x60,
             0x62, 0x64, 0x66, 0x68, 0x6C, 0x6E, 0x70, 0x72, 0xFE)
    # The NMI's own brightness trio: $2100 = $038D (or $80 = forced blank when it is 0), stepped down
    # once every $038E frames by the fade-out task $7E:34C2, which counts $038F.
    # $0388/$0389/$038A -- the NMI's song-change watcher (C3/09A4): busy latch, song PLAYING, song
    # WANTED. The title's setup latches both to its own song, so the watcher finds nothing to do.
    f.write('#define CT_TITLE_SEED_SNDBUSY 0x%02X%s' % (seed[0x0388], NL))
    f.write('#define CT_TITLE_SEED_SNDCUR  0x%02X%s' % (seed[0x0389], NL))
    f.write('#define CT_TITLE_SEED_SNDREQ  0x%02X%s' % (seed[0x038A], NL))
    f.write('#define CT_TITLE_SEED_BRI    0x%02X%s' % (seed[0x038D], NL))
    f.write('#define CT_TITLE_SEED_BRIPER 0x%02X%s' % (seed[0x038E], NL))
    f.write('#define CT_TITLE_SEED_BRICNT 0x%02X%s' % (seed[0x038F], NL))
    f.write('// The overlay\'s live cells, in the order src/title_intro.c names them.\n')
    emit(f, 'unsigned short ctTitleSeedCell', [u16(seed, a) for a in CELLS], 8, '0x%04X')
    emit(f, 'unsigned short ctTitleSeedSlide', [u16(seed, 0x46 + i * 2) for i in range(5)], 5, '0x%04X')
    emit(f, 'unsigned short ctTitleSeedTgt', [u16(seed, 0x50 + i * 2) for i in range(5)], 5, '0x%04X')
    emit(f, 'unsigned short ctTitleSeedWave', [u16(seed, 0x0C00 + i * 2) for i in range(64)], 16, '0x%04X')
    f.write('// The OAM shadow $7E:0700 (512 sprite bytes + the 32-byte high table).\n')
    emit(f, 'unsigned char ctTitleSeedOam', list(seed[0x0700:0x0920]), 16, '0x%02X')
    f.write('// $7E:00F0..$00F9 -- the multiply/divide scratch. ⭐ $F8 is NOT dead: bit 7 of it is\n'
            '// the carry $31BF rotates into its middle ADC, so the seed has to carry it.\n')
    emit(f, 'unsigned char ctTitleSeedScratch', list(seed[0xF0:0xFA]), 10, '0x%02X')
    emit(f, 'unsigned short ctTitleSeedPalShadow',
         [u16(seed, 0x2200 + i * 2) for i in range(256)], 16, '0x%04X')

    f.write('\n// ---- the ROM\'s own SINE TABLE, $C0:F900 (1024 bytes, a full circle = 0x400) -------\n'
            '// $7E:3465 (sine) = `AND #$03FF / TAX / LDA $C0F900,X / AND #$00FF`, NEGATED for X < $200,\n'
            '// and its `CPX #$0200` leaves the CARRY the callers\' next ADC picks up.\n'
            '// $7E:3461 (cosine) = the same entry with `CLC / ADC #$0100` in front.\n')
    emit(f, 'unsigned char ctTitleSin', list(sin), 32)

print('wrote %s: m7 %d tiles (box %dx%d at r%d,c%d), vram window $%04X+%d words, script %d B, '
      'packages %d B / %d entries' % (OUT, NT, MW, MH, R0, C0, VBASE, VWORDS,
                                      SCRIPT_END - SCRIPT_BASE, PKG_END - PKG_BASE, npkg))
