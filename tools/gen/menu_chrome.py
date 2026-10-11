#!/usr/bin/env python3
"""Regenerate src/data/menu_chrome.h -- the X menu's BG2 chrome: tiles, palettes and every screen's BG2 tilemap.

The menu's BG2 (mode 1, tilemap at VRAM word 0x6000, char base 0) is the marble desktop, the dark-blue 9-slice window
panels and the ribbon box. Everything here is read off the oracle's VRAM/CGRAM, nothing is drawn by hand:

  ctMenuChromePal        CGRAM rows 0-7, each 5-bit channel expanded (v<<3)|(v>>2) (what the PPU composes with), on
                         the menu open with the ribbon on opt0                                   [menu_open_gato2]
  ctMenuChromeTile       VRAM tiles 0x300-0x3EF as 8x8 colour indices (4bpp planar decoded), same capture
  ctMenuChromeRibbonTM   the ribbon view per option: Right x n (n = 0..5)                        [menu_open_gato2]
  ctMenuChromeEnteredTM  the entered screen per option: Right x n + A. opt0-3 from the same menu; opt4 (Form) and opt5
                         (Save) have ROM entry gates the GATO state fails, so they come from states that pass them:
                         opt4 needs ($0F0B + $0F0C) >= 2 unlocked members (C2/C627)           [grupo_3_mapa]
                         opt5 needs the overworld or a save point (C2/CEDC + C2/9875)          [ow_w0]
  ctMenuChromeEquipTM    the Equip detail (substate 2): A (member select), A (Crono)             [menu_open_gato2]
  ctMenuChromeEquipListTM  the Equip candidate list (substate 3): X, A, A, Down (Helmet), A -- a bag with gear Crono can
                         wear, or the ROM buzzes (C2/9C8D) and the list never opens            [menu_equip_spare]

Each tilemap is the visible 32x28 region at word 0x6000, verbatim. The captures were first taken from the owner's
states (saves/menu_open_gato2, grupo_3_mapa, ow_w0, eq_spare; one-off scripts, then pasted); this script makes the whole
file in one run and the recipes give the same bytes.

    python tools/gen/menu_chrome.py                    the recipes, under the rule
    python tools/gen/menu_chrome.py --private [--norule]   the owner's four captures (taken without the rule)
Run from the repo root; needs the ROM + build/ct.exe.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, recipe, peek, scratch, cgram_of  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--private', action='store_true', help="the owner's captures under saves/ instead of the recipes")
ap.add_argument('--norule', action='store_true', help='run the oracle without the rule')
ap.add_argument('--out', default=os.path.join(ROOT, 'src', 'data', 'menu_chrome.h'))
a = ap.parse_args()
if a.private:
    S = {k: os.path.join(ROOT, 'saves', v + '.state') for k, v in
         (('menu', 'menu_open_gato2'), ('form', 'grupo_3_mapa'), ('save', 'ow_w0'), ('eqlist', 'eq_spare'))}
else:
    S = {'menu': recipe('menu_open_gato2'), 'form': recipe('grupo_3_mapa'), 'save': recipe('ow_w0'),
         'eqlist': recipe('menu_equip_spare')}
RULE = not a.norule
TMW, TMH, BASE = 32, 28, 0x6000


def cap(key, name, script):
    out = peek(S[key], scratch('menu_chrome_' + name), script, rule=RULE)
    return (open(out + '.raw.vram', 'rb').read(), open(out + '.raw.wram', 'rb').read(), cgram_of(out + '.raw'))


def tilemap(vram):
    return [vram[BASE * 2 + i * 2] | (vram[BASE * 2 + i * 2 + 1] << 8) for i in range(TMW * TMH)]


def need(cond, what):
    if not cond:
        sys.exit('menu_chrome: REFUSING -- ' + what)


def ribbon(n):
    return '0:20' + ',80:2,0:8' * n + ',0:40'          # Right x n: the ribbon on option n


OPEN_X = '0:20,200:2,0:60'                               # from the field: X opens the menu
RIB, ENT = [], []
for n in range(6):
    v, w, cg = cap('menu', 'rib%d' % n, ribbon(n))
    need(w[0x67] == 0, 'the ribbon view %d is not the main menu ($0067 = %02X)' % (n, w[0x67]))
    if n == 0:
        vram0, cg0 = v, cg
    RIB.append(tilemap(v))
for n in range(4):
    v, w, _ = cap('menu', 'ent%d' % n, ribbon(n) + ',100:2,0:150')
    need(w[0x67] == n + 1, 'option %d did not enter its screen ($0067 = %02X)' % (n, w[0x67]))
    ENT.append(tilemap(v))
v, w, _ = cap('form', 'form', OPEN_X + ',80:2,0:4' * 3 + ',80:2,0:8,100:2,0:150')
need(w[0x67] == 5, 'the Form screen did not open ($0067 = %02X; C2/C627 wants two unlocked members)' % w[0x67])
ENT.append(tilemap(v))
v, w, _ = cap('save', 'save', OPEN_X + ',80:2,0:4' * 4 + ',80:2,0:8,100:2,0:150')
need(w[0x67] == 6, 'the Save screen did not open ($0067 = %02X; C2/CEDC wants the overworld or a save point)' % w[0x67])
ENT.append(tilemap(v))
v, w, _ = cap('menu', 'equip', '100:2,0:10,100:2,0:50')
need(w[0x67] == 1 and w[0xBE] == 2, 'not the Equip detail ($0067 = %02X, $00BE = %02X)' % (w[0x67], w[0xBE]))
EQ = tilemap(v)
v, w, _ = cap('eqlist', 'eqlist', OPEN_X + ',100:2,0:40,100:2,0:60,20:2,0:20,100:2,0:60')
need(w[0x67] == 1 and w[0xBE] == 3, 'not the Equip candidate list ($00BE = %d)' % w[0xBE])
need(w[0x0FC6] > 0, 'empty candidate list')
EQL = tilemap(v)


def conv(x):
    return (x << 3) | (x >> 2)


pal = []
for p in range(8):
    ent = []
    for k in range(16):
        q = cg0[(p * 16 + k) * 2] | (cg0[(p * 16 + k) * 2 + 1] << 8)
        ent.append('{%d,%d,%d}' % (conv(q & 31), conv((q >> 5) & 31), conv((q >> 10) & 31)))
    pal.append('  {' + ','.join(ent) + '},\n')
tiles = []
for t in range(0x300, 0x3F0):
    o, px = t * 32, []
    for y in range(8):
        for x in range(8):
            b = 7 - x
            px.append(((vram0[o + y * 2] >> b) & 1) | (((vram0[o + y * 2 + 1] >> b) & 1) << 1)
                      | (((vram0[o + 16 + y * 2] >> b) & 1) << 2) | (((vram0[o + 16 + y * 2 + 1] >> b) & 1) << 3))
    tiles.append('  {' + ','.join('%d' % c for c in px) + '}, /* %03X */\n' % t)


def plane(tm, label):
    s = '  { /* %s */\n' % label
    for ty in range(TMH):
        s += '    ' + ','.join('0x%04X' % tm[ty * TMW + tx] for tx in range(TMW)) + ',\n'
    return s + '  },\n'


def flat(tm):
    return ''.join('  ' + ', '.join('0x%04X' % tm[ty * TMW + tx] for tx in range(TMW)) + ',\n' for ty in range(TMH))


HEAD = """// menu_chrome.h - BYTE-EXACT CT menu BG2 chrome, captured from the oracle (saves/menu_open_gato2;
// mode-1 BG2 tilemap @VRAM word 0x6000, char base 0). NOTHING invented: the chrome tile graphics
// (VRAM 0x300-0x3EF: 0x3A0-0x3A3 marble, 0x3A4-0x3AC ribbon box, 0x3DC-0x3EF the 9-slice window
// panels, 0x300-0x313 the Save-slot buttons), the BG palettes (CGRAM; panels use rows 0 & 4, marble
// + ribbon box row 7), and the per-screen BG2 tilemaps. Replaying a tilemap reproduces the marble
// desktop + dark-blue window panels + the ribbon box pixel-identically (verified vs PEEK_ISOBG=1).
// Generated from the oracle VRAM/CGRAM.
#ifndef CT_MENU_CHROME_H
#define CT_MENU_CHROME_H
#include <stdint.h>
#define CT_MENU_CHROME_TILE0 0x300
#define CT_MENU_CHROME_NTILE 0xF0   /* tiles 0x300..0x3EF */
#define CT_MENU_CHROME_TMW 32
#define CT_MENU_CHROME_TMH 28
static const uint8_t ctMenuChromePal[8][16][3] = {
"""
ENTERED_DOC = """// Entered sub-screen per option (no ribbon box). BOTH gated screens are now the REAL entered
// captures: opt5 (Save) from the overworld, where C2/CEDC lets it open (_gen_menuchrome_save.py),
// and opt4 (Form) from a THREE-MEMBER field state, where C2/C627's ($0F0B + $0F0C) >= 2 finally
// passes (_gen_menuchrome_form.py). Both used to hold REFUSED captures -- the ROM had bounced the
// screen and what was stored was the ribbon's hover preview, leaving a rectangle of ribbon-box
// cells that CtMenuChrome then skipped, i.e. a hole (menu_open_items.md 3.4/5).
"""
TM = 'static const uint16_t %s[%sCT_MENU_CHROME_TMW*CT_MENU_CHROME_TMH] = {\n'
labels = ['opt0', 'opt1', 'opt2', 'opt3',
          'opt4 -- the ENTERED Form screen (3-member capture): _gen_menuchrome_form.py',
          'opt5 -- the ENTERED Save screen (overworld capture): _gen_menuchrome_save.py']
text = (HEAD + ''.join(pal) + '};\n'
        + 'static const uint8_t ctMenuChromeTile[CT_MENU_CHROME_NTILE][64] = {\n' + ''.join(tiles) + '};\n'
        + "// Main-menu RIBBON view per option: marble + that option's panels + the ribbon box (center).\n"
        + TM % ('ctMenuChromeRibbonTM', '6][') + ''.join(plane(RIB[n], 'opt%d' % n) for n in range(6)) + '};\n'
        + ENTERED_DOC
        + TM % ('ctMenuChromeEnteredTM', '6][') + ''.join(plane(ENT[n], labels[n]) for n in range(6)) + '};\n'
        + '\n// --- ENTERED EQUIP/STATUS detail chrome (3-box left column) : _gen_menuchrome_equip.py ---\n'
        + TM % ('ctMenuChromeEquipTM', '') + flat(EQ) + '};\n'
        + '// --- EQUIP CANDIDATE-LIST (ROM substate 3) chrome : _gen_menuchrome_equiplist.py ---\n'
        + TM % ('ctMenuChromeEquipListTM', '') + flat(EQL) + '};\n'
        + '\n#endif\n')
with open(a.out, 'w', newline='\r\n') as f:
    f.write(text)
print('wrote', os.path.relpath(a.out, ROOT))
