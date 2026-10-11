#!/usr/bin/env python3
"""Regenerate src/data/menu_saveform.h -- the Save screen's (opt5) BG palettes, ctMenuChromeSavePal.

The Save screen reloads CGRAM: the three save-slot button palettes (rows 1-3, used by tiles 0x300-0x313) all become the
same grey marble in indices 8-15, unlike the opt0 status CGRAM that ctMenuChromePal (menu_chrome.h) holds. The capture:
the X menu open in front of GATO (the recipe menu_open_gato2), Right x5 (the ribbon on opt5), CGRAM rows 0-7 from the
--peek sidecar, each 5-bit channel expanded (v<<3)|(v>>2) -- the conversion LakeSnes composes with. The entered screen
(Right x5 + A, on the overworld where C2/CEDC lets it open) has the same CGRAM; the file's comments say so.

    python tools/gen/menu_saveform.py                         the recipe menu_open_gato2, under the rule
    python tools/gen/menu_saveform.py --state <path> [--norule]   another start state (the owner's capture
                                                                 saves/menu_open_gato2.state was taken without the rule)
Run from the repo root; needs the ROM + build/ct.exe.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, recipe, peek, scratch, cgram_of  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--state', help='start state (default: the recipe menu_open_gato2)')
ap.add_argument('--norule', action='store_true', help='run the oracle without the rule (the private capture)')
ap.add_argument('--out', default=os.path.join(ROOT, 'src', 'data', 'menu_saveform.h'))
a = ap.parse_args()
STATE = a.state or recipe('menu_open_gato2')

RIGHT5 = '0:20' + ',80:2,0:8' * 5 + ',0:40'          # the ribbon moves to opt5 (Save)
out = peek(STATE, scratch('menu_saveform'), RIGHT5, rule=not a.norule)
w = open(out + '.raw.wram', 'rb').read()
cg = cgram_of(out + '.raw')


def conv(x):
    return (x << 3) | (x >> 2)


HEAD = r"""// gen_saveform.h - BYTE-EXACT Save-screen (opt5) BG palette, captured from the LakeSnes
// oracle. State: saves/menu_open_gato2.state + btnscript Right x5 (opt5 ribbon); the entered
// Save screen (Right x5 + A) has IDENTICAL CGRAM (re-verified on the OPENED screen: the
// overworld capture build/savechrome.raw, _gen_menuchrome_save.py). CGRAM read from
// <peek>.raw.oam[1056:1568] (256 BGR555 entries); BG palettes = rows 0-7. Values are
// (v<<3)|(v>>2) per channel -- the conversion LakeSnes itself renders with (measured: the
// composed Save text body is 231 = (28<<3)|(28>>2), not 230 = round(28*255/31)).
// WHY: the Save screen reloads CGRAM so the three save-slot button palettes (pal1/2/3, used by
// tiles 0x300-0x313 in ctMenuChromeRibbonTM[5]/ctMenuChromeEnteredTM[5]) all become the SAME
// grey marble in indices 8-15; the stored ctMenuChromePal is the opt0 status CGRAM where those
// same palettes are black/cyan/orange -> our slot 3 rendered orange, slots 1-2 black. NOTHING
// invented: replayed from the oracle's own CGRAM at the Save screen.
#ifndef CT_GEN_SAVEFORM_H
#define CT_GEN_SAVEFORM_H
#include <stdint.h>
static const uint8_t ctMenuChromeSavePal[8][16][3] = {
"""
TAIL = r"""};
// The Save screen's per-scanline WINDOW GRADIENT USED TO LIVE HERE, as a 224-entry table derived
// pixel-exactly from three oracle cursor captures. It is gone because it was one INSTANCE of a
// generator: the ROM builds every screen's ramp with C2/86DD out of a [kind, height] window list at
// $FF:BBF2 (src/data/menu_wingrad.h), and mWinGrad(5) reproduces the table that stood here at all
// 224 rows with ZERO mismatches -- which is what licensed deleting it. Its two derived facts survive
// in the generator: the ramp is +8..-7 in 5-BIT space (conv5(clamp(pal5 + d5[y], 0, 31)), exact where
// an 8-bit add is +-1 off at some tones), and it RESETS AND HEIGHT-NORMALISES per window -- 2 rows a
// step on the three 32px slot buttons, 5 on the 80px bottom windows.
#endif
"""

rows, ents = [], []
for p in range(8):
    ent = []
    for k in range(16):
        q = cg[(p * 16 + k) * 2] | (cg[(p * 16 + k) * 2 + 1] << 8)
        ent.append('{%d,%d,%d}' % (conv(q & 31), conv((q >> 5) & 31), conv((q >> 10) & 31)))
    ents.append(ent)
    rows.append('  {' + ','.join(ent) + '}, /* pal%d */\n' % p)
# the three slot-button rows share the marble's indices 8-15 on this screen (the reason the table exists)
if not ents[1][8:] == ents[2][8:] == ents[3][8:] == ents[0][8:]:
    sys.exit('menu_saveform: CGRAM row 1 does not carry the marble -- the ribbon is not on Save ($0067 %02X)' % w[0x67])
with open(a.out, 'w', newline='\r\n') as f:
    f.write(HEAD + ''.join(rows) + TAIL)
print('wrote', os.path.relpath(a.out, ROOT))
