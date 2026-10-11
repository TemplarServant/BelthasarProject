#!/usr/bin/env python3
"""Build saves/ow_balloons.spr from the settled Leene's Square oracle state
(anim_leenes_square.state, brightness 15 = full CGRAM; the scene darkness is color-math, not
INIDISP): the booth EXPLOSION puffs (char $00/$02/$04/$06/$08, 16x16, pal 1, OBJ page 1 / byte 0)
AND the rising BUNCHES (char $0A, pal 1/2/3 = red/blue/yellow). Why not --owballoons: the new-game
pan loads the coastal BOAT into the SAME chars $00-$08, so a capture there only ever grabs the boat's
red tiles -- the real cream/white puffs only exist in VRAM in this settled fair state. Run AFTER
--owballoons (which creates the file; this OVERWRITES it), BEFORE extract_assets:
  ct.exe --owballoons start_scene.state saves/ow_balloons
  python tools/owbpal0.py
  python assets/extract_assets.py Chrono_Trigger_USA_SNES.sfc
"""
import sys
sys.path.insert(0, 'tools')
from parse_ppu_state import parse_state_file

STATE = 'anim_leenes.state'
SPR = 'saves/ow_balloons.spr'

res, blk = parse_state_file(STATE)
p = res['ppu']; cg = p['cgdata']; vra = blk['VRA']; ob = p['objNameBase']


def render(tile, pal):                                  # 16x16 OBJ at OBJ char base = objNameBase (word)
    px = bytearray(16 * 16 * 4)
    for py in range(16):
        for pxx in range(16):
            t = (tile + (pxx // 8) + (py // 8) * 0x10) & 0xFF
            ba = (ob + t * 16) * 2
            row = py & 7; kk = 7 - (pxx & 7)
            b0 = vra[ba + row * 2]; b1 = vra[ba + row * 2 + 1]
            b2 = vra[ba + 16 + row * 2]; b3 = vra[ba + 16 + row * 2 + 1]
            v = ((b0 >> kk) & 1) | (((b1 >> kk) & 1) << 1) | (((b2 >> kk) & 1) << 2) | (((b3 >> kk) & 1) << 3)
            if not v:
                continue
            cw = cg[128 + pal * 16 + v]; d = (py * 16 + pxx) * 4
            px[d] = ((cw & 31) << 3) | ((cw & 31) >> 2)
            px[d + 1] = (((cw >> 5) & 31) << 3) | (((cw >> 5) & 31) >> 2)
            px[d + 2] = (((cw >> 10) & 31) << 3) | (((cw >> 10) & 31) >> 2)
            px[d + 3] = 255
    return bytes(px)


# Build the WHOLE .spr from the settled oracle state -- the new-game pan loads the coastal BOAT into the
# explosion chars $00-$08, so --owballoons can only ever grab the boat there; the real cream/white PUFFS
# only exist in VRAM in the settled fair state (page 1, byte 0). All frames are 16x16, pal 1 (bunches 1/2/3).
EXPL = (0x00, 0x02, 0x04, 0x06, 0x08)                   # field.c maps anim 0xE671.. -> char fr*2 = these
frames = [(ch, 1, render(ch, 1)) for ch in EXPL]        # EXPLOSION: $00 round puff -> $02 cloud -> $04 break -> $06 debris -> $08 fade
frames += [(0x0A, pal, render(0x0A, pal)) for pal in (1, 2, 3)]   # BUNCHES: red/blue/yellow
out = bytearray([len(frames)])
for i, (tile, pal, body) in enumerate(frames):
    out += bytes([i, tile, pal, 16, 16, 0xF8, 0xF8]) + body       # all 16x16, ox/oy = -8
open(SPR, 'wb').write(out)
print('rebuilt %s from oracle: %d explosion (char 00/02/04/06/08) + 3 bunches (pal 1/2/3) = %d frames'
      % (SPR, len(EXPL), len(frames)))
