#!/usr/bin/env python3
"""Generator for the enemy-death dissolve table (src/data/battle_dissolve.h), measured on the oracle.

    python tools/gen/battle_dissolve.py            (run from the repository root; needs the recipe forest_battle_3hetake)

disasm/notes/battle_dissolve.md (T2). The enemy-death dissolve is a two-part OBJ effect on the dying enemy's sprite:
  1. Palette: the corpse draws with the live OBJ palette its $993C names (CF/FE20's copy into palette 4 and CD/00A5's
     red fade record, battle_death_dissolve_gate.md PART 5) -- not part of this table.
  2. Ordered-dither tile clear: an 8x8 dither matrix (global, indexed by the pixel's sprite-local (x%8, y%8)) gives
     each enemy OBJ pixel a clear frame. Pixel present iff dither[y%8][x%8] >= (curFrame - dissolveStart). ~20
     px/frame, 32 frames, then the OAM sprites are removed. Streamed by the C1/CC battle-anim engine.

Measurement: from the forest fight (the recipe forest_battle_3hetake: three Hetake, Crono's command
menu up) Crono attacks the lower-right Hetake (slot 2, VRAM tiles 0x60-0x77); every frame of its death is dumped
(one ct.exe run, PEEK_SHOTS) and each of its 16 OBJ tiles decoded. The dissolve's start S is the last frame on which
every pixel of the sprite is still there; a pixel's clear step is the last frame it is present, minus S. The step is
a function of (x%8, y%8) only -- checked over every inked pixel of the 32x32 sprite (16 samples per cell); one
disagreeing pixel stops the generator. --verify re-predicts every frame from (the tiles at S + the table) and
compares all 16 x 64 px of every frame.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from ctlib import peekshots  # noqa: E402

STATE = os.environ.get('CT_DISSOLVE_STATE', os.path.join(ROOT, 'build', 'recipes', 'forest_battle_3hetake.state'))
NAV = "0:6,100:3,0:8,20:3,0:8,20:3,0:8,100:3"   # A = Att., Down, Down = target slot 2, A = confirm (39 frames)
WINDOW = range(120, 300)                         # peek frames searched for the death (the reference: S = 189)
OUT = os.path.join(ROOT, 'src', 'data', 'battle_dissolve.h')

ENEMY_TILES = [0x60, 0x61, 0x70, 0x71, 0x62, 0x63, 0x72, 0x73,
               0x64, 0x65, 0x74, 0x75, 0x66, 0x67, 0x76, 0x77]
# sprite-local (gx,gy) 0..31 of each 8x8 subtile's top-left (2x2 cells of 16x16)
TILE_ORIGIN = {0x60: (0, 0), 0x61: (8, 0), 0x70: (0, 8), 0x71: (8, 8),
               0x62: (16, 0), 0x63: (24, 0), 0x72: (16, 8), 0x73: (24, 8),
               0x64: (0, 16), 0x65: (8, 16), 0x74: (0, 24), 0x75: (8, 24),
               0x66: (16, 16), 0x67: (24, 16), 0x76: (16, 24), 0x77: (24, 24)}


def tile_pixels(vram, t):
    """Decode one 8x8 4bpp OBJ tile -> [8][8] pixel values."""
    tb = vram[t * 32:t * 32 + 32]
    g = []
    for row in range(8):
        p0, p1, p2, p3 = tb[row * 2], tb[row * 2 + 1], tb[16 + row * 2], tb[16 + row * 2 + 1]
        g.append([((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) | (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3)
                  for b in range(7, -1, -1)])
    return g


def sprite(vram):
    """{(gx, gy): pixel} over the 32x32 sprite."""
    out = {}
    for t in ENEMY_TILES:
        ox, oy = TILE_ORIGIN[t]
        px = tile_pixels(vram, t)
        for y in range(8):
            for x in range(8):
                out[(ox + x, oy + y)] = px[y][x]
    return out


def capture():
    frames = list(WINDOW)
    got = peekshots.shots(STATE, NAV, frames, sidecars=True)
    try:
        return {n: sprite(open(got[n] + '.vram', 'rb').read()) for n in frames}
    finally:
        peekshots.clean(got)


def measure(seq):
    """(S, dither[8][8]) from the per-frame sprites."""
    frames = sorted(seq)
    # the dissolve: from some frame on, pixels only ever go from inked to 0; S = the last frame before the first clear
    # whose content is otherwise unchanged (the idle animation has stopped)
    for i in range(1, len(frames)):
        a, b = seq[frames[i - 1]], seq[frames[i]]
        cleared = [k for k in a if a[k] and not b[k]]
        changed = [k for k in a if b[k] and b[k] != a[k]]
        if cleared and not changed:
            start = frames[i - 1]
            break
    else:
        sys.exit('battle_dissolve: no dissolve in peek frames %d..%d' % (frames[0], frames[-1]))
    base = seq[start]
    last = {}
    for k, v in base.items():
        if not v:
            continue
        n = start
        while n + 1 in seq and seq[n + 1][k] == v:
            n += 1
        if n + 1 not in seq:
            sys.exit('battle_dissolve: pixel %s still present at the window end' % (k,))
        if any(seq[m][k] for m in range(n + 1, frames[-1] + 1)):
            sys.exit('battle_dissolve: pixel %s comes back after it cleared' % (k,))
        last[k] = n - start
    dither = [[None] * 8 for _ in range(8)]
    for (gx, gy), step in last.items():
        c = dither[gy % 8][gx % 8]
        if c is not None and c != step:
            sys.exit('battle_dissolve: cell (%d,%d) disagrees: step %d and %d' % (gx % 8, gy % 8, c, step))
        dither[gy % 8][gx % 8] = step
    if any(v is None for row in dither for v in row):
        sys.exit('battle_dissolve: a dither cell has no inked pixel to measure it')
    return start, dither


def verify(seq, start, dither):
    base = seq[start]
    bad = tot = 0
    for n in sorted(seq):
        if n < start:
            continue
        step = n - start
        for (gx, gy), v in base.items():
            pred = v if dither[gy % 8][gx % 8] >= step else 0
            tot += 1
            bad += pred != seq[n][(gx, gy)]
    print('battle_dissolve verify: frames %d..%d, %d px predicted from the start frame + the table, %d differ'
          % (start, max(seq), tot, bad))
    return bad == 0


def emit(dither):
    L = []
    L.append("/* battle_dissolve.h -- enemy-death DISSOLVE (GENERATED by")
    L.append(" * tools/gen/battle_dissolve.py; derived in disasm/notes/battle_dissolve.md T2).")
    L.append(" * Mechanism: on enemy death, its OBJ sprites move to pal4 (CF/FE20), then an 8x8")
    L.append(" * ordered-dither matrix clears the OBJ pixels ~20/frame over 32 frames, then the")
    L.append(" * OAM sprites are removed. Pixel present iff ctBattleDissolveDither[y%8][x%8] >= step,")
    L.append(" * step = curFrame - dissolveStart (0..31). Byte-exact vs oracle (see --verify). */")
    L.append("#ifndef CT_BATTLE_DISSOLVE_H")
    L.append("#define CT_BATTLE_DISSOLVE_H")
    L.append("")
    L.append("#define CT_DISSOLVE_STEPS      32   /* dither levels; pixel clears at step==dither+1 */")
    L.append("/* The timing is not here either: CD/0043's own waits (battle.c battleDeathRoutineStart, battleAdvanceDissolves). */")
    L.append("#define CT_DISSOLVE_RATE_PX    20   /* px cleared per frame (emergent from the matrix) */")
    L.append("/* The recolor is not here: the corpse draws with the live OBJ palette its $993C names -- CF/FE20's copy into")
    L.append(" * palette 4 and CD/00A5's red fade record (battle_death_dissolve_gate.md PART 5). */")
    L.append("")
    L.append("/* clear-STEP per sprite-local pixel (x%8,y%8): present iff this >= step. */")
    L.append("static const unsigned char ctBattleDissolveDither[8][8] = {")
    for row in dither:
        L.append("  {%s}," % ", ".join("%2d" % v for v in row))
    L.append("};")
    L.append("")
    L.append("#endif /* CT_BATTLE_DISSOLVE_H */")
    with open(OUT, 'w', newline='\n') as f:
        f.write("\n".join(L) + "\n")
    print("wrote %s" % os.path.relpath(OUT, ROOT))


def main():
    seq = capture()
    start, dither = measure(seq)
    steps = sorted(set(v for row in dither for v in row))
    print('battle_dissolve: dissolve start at peek frame %d, steps %d..%d (%d levels)'
          % (start, steps[0], steps[-1], len(steps)))
    if not verify(seq, start, dither):
        return 1
    if len(steps) != 32 or steps != list(range(32)):
        print('battle_dissolve: the steps are not 0..31 (CT_DISSOLVE_STEPS)')
        return 1
    emit(dither)
    return 0


if __name__ == '__main__':
    sys.exit(main())
