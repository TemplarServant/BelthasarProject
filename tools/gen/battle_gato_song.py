#!/usr/bin/env python3
"""GATO SONG-presentation bake (disasm/notes/battle_animeng_corpus.md CAPTURE 2) -> src/data/battle_gato_song.h.

The corpus proved the singing presentation ("the singing animation is strange", translated from Portuguese) is:
 (2.1) GATO holds his FRONT/DOWN idle assembly with a GENTLE ~6px claw SWAY on the top-right claw cells
       068/06A (NOT the big idle claw-SWAP, and NOT the attack claw-swipe our engine wrongly plays for the
       song's first 49f). Sway active only in the lit MIDDLE (co-timed with the spotlight window). Base
       assembly + the two swaying cells captured from song_f470 (foot 128,85, anchor 96,29 -- matches the
       dossier 2.1 assembly verbatim); 068/06A de-swayed to their ARMS-OUT reference (anchor-rel 47,11 /
       47,27) so the render adds the sway offset from zero.
 (2.2) A NOTE MACHINE spawns 16x16 pal0 note sprites (tiles 0x0E6/0E8/0EA twinkle) near GATO's head.
       These are now DERIVED BYTE-EXACT and baked in src/data/battle_fx_song.h (tools/_gen_battlefx_song.py) --
       the per-frame OAM ledger + glyph tiles + pal0 -- so this script no longer emits them (it kept only
       the glyph tiles + pal0 as a fixture that drove the OLD invented drift; that is retired).

FIXTURE for the future anim-engine port (one rep frame for the singing pose; the mouth tile-stream stays
the streamer boundary). The note sprites are byte-exact (battle_fx_streamer.md SONG NOTES).

Run from repo root:  python tools/gen/battle_gato_song.py [--state <recipe state>] [--corpus <dir holding song_f470.raw.*>]
"""
import os, sys, hashlib, subprocess
sys.path.insert(0, 'tools')
from ctlib import oam

# The frame: the corpus' song_f470 was gato_moving_lake.state (a snes9x capture, lost) 470 frames in: its song span
# ($A013 = 1 with $9877 = 0x10) ran f373-685, so f470 is the span's frame +97, in the lit middle (dossier 2.1). From
# the recipe: GATO's HOLD fight (STATE + "100:8,..." -- one boot A, then nothing) sings first at f1146-1455 (the rule's
# oracle and the plain one alike), so the bake frame is f1146 + 97 = f1243. GATO sings there at (135,85), not the
# corpus' (128,85): the cells are stored relative to the live foot, so only the anchor's offset from the foot is
# checked against the dossier's assembly (anchor (96,29) at foot (128,85)). --corpus <dir> reads the old capture.
STATE = (sys.argv[sys.argv.index('--state') + 1] if '--state' in sys.argv else os.environ.get('CT_GEN_STATE', 'build/recipes/battle_gato_press_action.state'))
SONG_START, SONG_AT = 1146, 97
OUT_H = os.environ.get('CT_GEN_OUT', 'src/data/battle_gato_song.h')
NTILE = 0xA0
PEEK = 'song_f470'
if '--corpus' in sys.argv:
    SP = sys.argv[sys.argv.index('--corpus') + 1]
else:
    SP = os.environ.get('CT_SCRATCH', 'build')
    PEEK = 'song_f%d' % (SONG_START + SONG_AT)
    subprocess.run([os.path.abspath(os.environ.get('CT_EXE', 'build/ct.exe')), os.environ.get('CT_ROM', 'Chrono_Trigger_USA_SNES.sfc'),
                    '--peek', STATE, os.path.join(SP, PEEK + '.raw'), '--btnscript', '100:8,0:%d' % (SONG_START + SONG_AT - 8)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
GATO_BODY = {0x68, 0x6A, 0x6C, 0x6E, 0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C, 0x8E}
SWAY_TILES = {0x68, 0x6A}                 # top-right claw cells that sway (corpus 2.1)
# arms-out reference (anchor-rel), corpus 2.1 (068@(47,11), 06A@(47,27) at f360-417 / f619-689)
ARMS_OUT = {0x68: (47, 11), 0x6A: (47, 27)}

vram = open(os.path.join(SP, PEEK + '.raw.vram'), 'rb').read()
spr  = oam.decode(oam.load(os.path.join(SP, PEEK + '.raw.oam')))
wram = open(os.path.join(SP, PEEK + '.raw.wram'), 'rb').read()
cg   = oam.load_cgram(os.path.join(SP, PEEK + '.raw.oam'))
assert cg, "%s.raw.oam has no CGRAM (need the 1568-byte peek sidecar)" % PEEK
fx, fy = wram[0x1D12], wram[0x1D29]
assert wram[0xA013] == 1 and wram[0x9877] == 0x10, "not inside GATO's song ($A013 %02X $9877 %02X)" % (wram[0xA013], wram[0x9877])

cells = [s for s in spr if s['y'] < 210 and -60 < s['x'] < 256 and s['pal'] == 1 and s['tile'] in GATO_BODY]
cells.sort(key=lambda s: s['idx'])
assert len(cells) == 12, "song GATO cluster has %d cells (want 12)" % len(cells)
ax = min(s['x'] for s in cells); ay = min(s['y'] for s in cells)
assert (ax - fx, ay - fy) == (96 - 128, 29 - 85), "song anchor (%d,%d) at foot (%d,%d): not the dossier's assembly" % (ax, ay, fx, fy)

# de-sway 068/06A to arms-out; store all 12 rel-to-foot
rel = []
for s in cells:
    t = s['tile']
    if t in ARMS_OUT:
        arx, ary = ARMS_OUT[t]
        cx, cy = ax + arx, ay + ary                      # arms-out absolute
    else:
        cx, cy = s['x'], s['y']
    rel.append((cx - fx, cy - fy, t, 1 if t in SWAY_TILES else 0))
tiles = [[vram[t * 32 + i] for i in range(32)] for t in range(NTILE)]
bodymd5 = hashlib.md5(bytes(b for t in sorted(GATO_BODY) for b in vram[t*32:t*32+32])).hexdigest()[:8]
# NOTE glyphs/pal0 RELOCATED to tools/_gen_battlefx_song.py (src/data/battle_fx_song.h) -- see the emission below.

print("song pose: foot=(%d,%d) anchor=(%d,%d) cells=12 bodymd5=%s" % (fx, fy, ax, ay, bodymd5))
print("  arms-out 068 rel=(%d,%d) 06A rel=(%d,%d)"
      % (ARMS_OUT[0x68][0]+ax-fx, ARMS_OUT[0x68][1]+ay-fy, ARMS_OUT[0x6A][0]+ax-fx, ARMS_OUT[0x6A][1]+ay-fy))

with open(OUT_H, 'w') as f:
    W = f.write
    W("// GENERATED by tools/gen/battle_gato_song.py -- do not edit. GATO's SONG presentation bake\n")
    W("// (disasm/notes/battle_animeng_corpus.md CAPTURE 2). (2.1) the singing pose = GATO's FRONT idle\n")
    W("// assembly + a gentle ~6px claw SWAY on 068/06A (NOT the big idle swap, NOT the attack swipe our\n")
    W("// engine wrongly played for the song's first 49f); baked from %s (foot %d,%d, anchor\n" % (PEEK, fx, fy))
    W("// %d,%d == the dossier 2.1 assembly), 068/06A de-swayed to their arms-out reference. (2.2) the song\n" % (ax, ay))
    W("// NOTE sprites are DERIVED BYTE-EXACT in src/data/battle_fx_song.h (battleSongNotesRender replays them);\n")
    W("// they no longer live here.\n")
    W("// Cells RELATIVE TO GATO'S LIVE FOOT ($1D12/$1D29); hf=vf=0. FIXTURE for the anim-engine port.\n")
    W("#ifndef CT_BATTLE_GATO_SONG_H\n#define CT_BATTLE_GATO_SONG_H\n#include <stdint.h>\n\n")
    # pose
    W("typedef struct { int16_t relX, relY; uint16_t tile; uint8_t sway; } CtGatoSongCell;  // sway=1: 068/06A\n")
    W("static const CtGatoSongCell ctGatoSongCell[12] = {  // song pose (bodymd5 %s), 068/06A at arms-out\n" % bodymd5)
    for (rx, ry, t, sw) in rel:
        W("  { %4d, %4d, 0x%03X, %d },\n" % (rx, ry, t, sw))
    W("};\n")
    W("#define CT_GATO_SONG_NTILE 0x%X\n" % NTILE)
    W("static const uint8_t ctGatoSongTiles[CT_GATO_SONG_NTILE][32] = {\n")
    for t in range(NTILE):
        W("  {" + ",".join("0x%02X" % b for b in tiles[t]) + "},\n")
    W("};\n")
    # sway offsets from arms-out over the lit window (corpus 2.1: 068 42,11<->42,12<->41,13; ~15f dwell)
    W("// SWAY offsets from arms-out (corpus 2.1: 068 oscillates 47,11 -> 42,11 -> 42,12 -> 41,13 and back;\n")
    W("// ~15f dwells, ~60f period). Applied to the sway cells only, gated to the lit spotlight window.\n")
    W("#define CT_GATO_SONG_SWAY_N 4\n")
    W("#define CT_GATO_SONG_SWAY_DWELL 15\n")
    W("static const int8_t ctGatoSongSway[CT_GATO_SONG_SWAY_N][2] = { {-5,0}, {-5,1}, {-6,2}, {-5,1} };\n\n")
    # NOTE sprites RELOCATED: the song's note glyph tiles + pal0 + the byte-exact per-frame OAM ledger now
    # live in src/data/battle_fx_song.h (tools/_gen_battlefx_song.py), beside the whirlwind/ice effect objects.
    W("// NOTE sprites RELOCATED: the song's floating musical notes (glyph tiles 0x0E6/0E8/0EA + pal0 + the\n")
    W("// byte-exact per-frame OAM ledger) now live in src/data/battle_fx_song.h (tools/_gen_battlefx_song.py),\n")
    W("// the byte-verbatim fixture battleSongNotesRender replays -- retiring the duplicate that was here.\n")
    W("#endif\n")
print("wrote", OUT_H)
