#!/usr/bin/env python3
"""src/data/menu_itemuse.h -- the Item screen's Use / Organize data (menu.c), ROM-free.

ctItemProp   = the oracle's $7E:7700 item-property table (ids 0x00..0xF2) as the menu holds it once open: bit1 (0x02)
               = usable from the field (the gate of C2/B0AB's caller), bit3 (0x08) = key item (C2/ABF5's Organize
               skip). The weapon/armour bits (0x40/0x80/0x14) depend on who is in the party, so the state matters:
               the X menu open in front of GATO with Crono and Marle (recipe menu_open_gato2, standing in for
               saves/menu_open_gato2.state -- the same 243 bytes).
ctItemFx*    = the effect of the consumables 0xBC..0xCF, decoded the way C2/B0AB does it (disasm/notes/menu2_item_rest.md
               sec. 2.5): the record $CC:05CC[sub*4] (sub = id - 0xBC; byte 1 = the HP/MP bits, byte 2 & 0F = the
               multiplier, 0F -> $0DC0 = 40 = to the maximum, byte 3 & 3F = the base), the scope (($CC:1BB5[sub] + 1)
               & 3) << 2 (bit 3 = all the party), and the kind by the dispatch C2/B0FA / ($B0EE,X) on byte 0: bit 7 ->
               C2/B17F (HP/MP restore), bit 4 -> C2/B107 (the Power Meal: C2/B10D's first arm, $0DBD = C2 and
               $0DC0 = 40 -> HP+MP to the maximum), nothing set down to bit 3 -> C2/B200 (the tabs: power for CD, magic
               for CE, speed for the rest). An item the field cannot use ($7700 bit 1 clear) is ITFX_NONE.
ctOrg*       = Organize's group ranges, C2/AC12 (start ids) and C2/AC17 (lengths).

    python tools/gen/menu_itemuse.py [--state S] [--out src/data/menu_itemuse.h]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, ROM, recipe  # noqa: E402
from recipes import wram_of  # noqa: E402

N = 20                       # ids 0xBC..0xCF


def fx(rom, prop):
    kind, amt, allp, pool = [0] * N, [0] * N, [0] * N, [0] * N
    # C2/B10D's first arm (X = 0): LDA #C2 / STA $0DBD / LDA #40 / STA $0DC0
    arm0 = rom[0x2B141] | rom[0x2B142] << 8
    a = 0x20000 + arm0
    assert rom[a] == 0xA9 and rom[a + 2:a + 5] == b'\x8D\xBD\x0D', 'C2/B10D arm 0 ($0DBD)'
    pm_bits, pm_dc0 = rom[a + 1], rom[a + 6]
    assert rom[a + 5] == 0xA9 and rom[a + 7:a + 10] == b'\x8D\xC0\x0D', 'C2/B10D arm 0 ($0DC0)'
    for s in range(N):
        b0, b1, b2, b3 = rom[0x0C05CC + s * 4:0x0C05CC + s * 4 + 4]
        scope = ((rom[0x0C1BB5 + s] + 1) & 3) << 2
        allp[s] = 1 if scope & 8 else 0
        dbd, dc0 = b1, (0x40 if (b2 & 0x0F) == 0x0F else b2 & 0x0F)
        x = 0                                      # C2/B0FA: X = 0; ASL until a carry, X += 2, while X < 0A
        v = b0
        while x < 10:
            c = v & 0x80
            v = (v << 1) & 0xFF
            if c:
                break
            x += 2
        handler = rom[0x2B0EE + x] | rom[0x2B0EF + x] << 8
        if handler == 0xB107:                      # the Power Meal
            dbd, dc0 = pm_bits, pm_dc0
        pool[s] = (1 if dbd & 0x80 else 0) | (2 if dbd & 0x40 else 0)
        if not prop[0xBC + s] & 0x02:
            continue
        if handler in (0xB17F, 0xB107):
            if dc0 == 0x40:
                kind[s] = 3
            elif dbd & 0x80:
                kind[s], amt[s] = 1, (b3 & 0x3F) * dc0
            elif dbd & 0x40:
                kind[s], amt[s] = 2, (b3 & 0x3F) * dc0
        elif handler == 0xB200:                    # C2/B200: CD -> power, CE -> magic, else speed
            kind[s], amt[s] = 4, {0xCD: 0, 0xCE: 1}.get(0xBC + s, 2)
    return kind, amt, allp, pool


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--state', help='a state with the X menu open instead of the recipe (saves/menu_open_gato2.state)')
    ap.add_argument('--out', default=os.path.join('src', 'data', 'menu_itemuse.h'))
    a = ap.parse_args()
    rom = open(ROM, 'rb').read()
    w = wram_of(a.state or recipe('menu_open_gato2'))
    prop = w[0x7700:0x7700 + 0xF3]
    kind, amt, allp, pool = fx(rom, prop)
    org_s, org_l = rom[0x2AC12:0x2AC17], rom[0x2AC17:0x2AC1C]
    j = lambda v: ', '.join(str(x) for x in v)
    L = ['// GENERATED (menu_itemuse gen) -- item-menu Use/Organize data, ROM-free bake.',
         '// ctItemProp = oracle $7E7700 static item-property table (bit1=field-usable, bit3=key-item).',
         '// ctItemFx*  = ROM effect table $CC:05CC + scope $CC:1BB5 decoded (Tonic oracle-validated).',
         '#ifndef CT_MENU_ITEMUSE_H', '#define CT_MENU_ITEMUSE_H', '#include <stdint.h>', '',
         '// $7700[id] for id 0x00..0xF2 (static per-item props). bit1(0x02)=usable, bit3(0x08)=key.',
         'static const uint8_t ctItemProp[0xF3] = {']
    for i in range(0, 0xF3, 16):
        L.append('  ' + ''.join('0x%02X,' % b for b in prop[i:i + 16]))
    L += ['};', '', '// Effect kinds', '#define ITFX_NONE 0', '#define ITFX_HP   1', '#define ITFX_MP   2',
          '#define ITFX_FULL 3  /* restore HP and/or MP to max (pools in ctItemFxPool) */',
          '#define ITFX_TAB  4  /* permanent +1 stat (amount = 0 pow,1 mag,2 spd) */', '',
          '// Per-consumable effect for ids 0xBC..0xCF (index = id-0xBC).', '#define ITFX_BASE 0xBC',
          '#define ITFX_N    20',
          'static const uint8_t  ctItemFxKind[ITFX_N] = { %s };' % j(kind),
          'static const uint16_t ctItemFxAmt [ITFX_N] = { %s };' % j(amt),
          'static const uint8_t  ctItemFxAll [ITFX_N] = { %s };' % j(allp),
          'static const uint8_t  ctItemFxPool[ITFX_N] = { %s }; // bit0=HP bit1=MP (for FULL); idx13=C9 PowerMeal=HP+MP'
          ' (dispatch B107)' % j(pool), '',
          '// Organize group ranges (ROM C2/AC12 start ids, C2/AC17 lengths).',
          'static const uint8_t ctOrgStart[5] = { %s };' % ', '.join('0x%02X' % b for b in org_s),
          'static const uint8_t ctOrgLen  [5] = { %s };' % ', '.join('0x%02X' % b for b in org_l), '', '#endif', '']
    path = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    open(path, 'w', newline='\n').write('\n'.join(L))
    print('wrote %s' % a.out)


if __name__ == '__main__':
    main()
