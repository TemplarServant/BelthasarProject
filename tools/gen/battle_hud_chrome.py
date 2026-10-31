#!/usr/bin/env python
"""The battle HUD strip's chrome as the ROM builds it: BG1 tiles out of the tilemap rows 16..21 ($7E:0B40).

    python tools/gen/battle_hud_chrome.py [out.h]      (default src/data/battle_hud_chrome.h)

The mechanism (disasm/notes/battle_hud_command_windows.md): in the strip's lines every BG reads the tilemap at VRAM
word $7800 (the HDMA channel 0 register block $BE9B = 78 78 78 78); BG3 draws the text rows 1..7, BG1 the frame rows
16..21, whose WRAM image is $7E:0B40 (6 rows x $40 bytes, DMA'd beside the text buffer $0CC0). The ROM writes it in
two places only:
  * C1/1C3A (and the boot's CC/E4ED): copy the base $D1:5800 -- the enemy-name window (cols 0..11) + the party window
    (cols 12..31) -- $180 bytes;
  * C1/081E -> C1/0929: for every enqueued seat s ($A6D9,s != FF), the command window's frame $D1:59FC (7 cells x 6
    rows) at cols 6s..6s+6 -- or its last 6 cells at cols 6s+1..6s+6 when seat s-1 is enqueued too, so s-1 keeps
    its right edge. (C1/0547/054C/0564: only with $A6DE != 0, $A86B == 0 and no submenu, $95DB == 0.)
So the chrome is a function of the queue. The pixels of a tile depend on the tile and the line (the COLDATA gradient
runs per line; the dither is in the tile), never on the column.

What this does: the three chrome bakes (src/data/battle_hud.h ctBattleHudBg1Enemy / ctBattleHudBg1Cmd,
src/data/battle_hud_sub.h ctBattleHudBg1Tgt -- PEEK_ISOBG=0 captures of the cartridge, see tools/gen/battle_hud.py /
tools/gen/battle_hud_sub.py) are three queue states of that one machine: no seat enqueued, seats {0,1}, seat {1} (the "target
picker" bake is the seat-1-only window: the fixture gato_target.raw.wram has $A6D9 = FF 01 and $9609 = 0). Laid over
the ROM's tilemap for its queue, every bake pixel becomes a sample of (tile word, strip line, x & 7); this script
checks that the samples never disagree, that every (tile, line) the law can produce for any queue is sampled,
and writes the table. The renderer then draws any queue state -- seat 0 alone, 3 PCs -- from $0B40.
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '..')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def bake(path, name):
    s = open(os.path.join(ROOT, path), encoding='utf-8', errors='replace').read()
    i = s.index(name + '[48][256] = {')
    j = s.index('};', i)
    nums = [int(x) for x in re.findall(r'\d+', s[s.index('{', i) + 1:j])]
    assert len(nums) == 48 * 256, (name, len(nums))
    return [nums[k * 256:(k + 1) * 256] for k in range(48)]


def chrome(rom, queue):
    """C1/1C3A + C1/081E: the $0B40 image for the enqueued seats `queue` (3 booleans)."""
    b = bytearray(rom[0x115800:0x115800 + 0x180])                     # $D1:5800 -> $0B40, $180 bytes
    fr = rom[0x1159FC:0x1159FC + 6 * 14]                              # $D1:59FC, 6 rows x 7 cells
    for s in range(3):
        if not queue[s]:
            continue
        skip = 1 if s > 0 and queue[s - 1] else 0                     # C1/0837 / C1/0854: $84 = 1
        dst = 12 * s + 2 * skip                                       # $82 = 0 / 0C / 0E / 18 / 1A
        for r in range(6):
            src = fr[r * 14 + 2 * skip:r * 14 + 14]                   # C1/0937 INX INX: the first cell dropped
            b[r * 0x40 + dst:r * 0x40 + dst + len(src)] = src
    return b


def word(b, r, c):
    return b[r * 0x40 + c * 2] | (b[r * 0x40 + c * 2 + 1] << 8)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'src', 'data', 'battle_hud_chrome.h')
    rom = _romlib.load(os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'))
    bakes = [('ctBattleHudBg1Enemy', 'src/data/battle_hud.h', (0, 0, 0)),
             ('ctBattleHudBg1Cmd', 'src/data/battle_hud.h', (1, 1, 0)),
             ('ctBattleHudBg1Tgt', 'src/data/battle_hud_sub.h', (0, 1, 0))]
    table, n = {}, 0
    for name, path, q in bakes:
        px, tm = bake(path, name), chrome(rom, q)
        for ry in range(48):
            for x in range(256):
                key = (word(tm, ry // 8, x // 8), ry, x & 7)
                n += 1
                if key in table and table[key] != px[ry][x]:
                    sys.exit('CONFLICT: %s line %d x %d tile %04X: %d vs %d' % (name, ry, x, key[0], table[key], px[ry][x]))
                table[key] = px[ry][x]
    need = set()                                                      # every (tile, line) of every queue state
    for m in range(8):
        tm = chrome(rom, (m & 1, (m >> 1) & 1, (m >> 2) & 1))
        for ry in range(48):
            for c in range(32):
                need.add((word(tm, ry // 8, c), ry))
    miss = sorted(k for k in need if any((k[0], k[1], p) not in table for p in range(8)))
    if miss:
        sys.exit('UNCOVERED (tile, line): %s' % miss[:12])
    words = sorted(set(k[0] for k in table))
    print('chrome: %d samples, %d keys, 0 conflicts; %d tiles; every (tile, line) of the 8 queue states covered'
          % (n, len(table), len(words)))
    L = ['// The battle HUD strip CHROME, per BG1 tile -- GENERATED by tools/gen/battle_hud_chrome.py (the mechanism, the',
         '// derivation and the checks are in that file and in disasm/notes/battle_hud_command_windows.md).',
         '// ctBattleHudChromePix[t][line][x & 7] = the ctBattleHudBg1Pal index of tile ctBattleHudChromeWord[t] on strip line',
         '// `line` (0..47): the three cartridge bakes laid over the ROM\'s own $0B40 tilemap for their queue states, %d samples,'
         % n,
         '// 0 conflicts. 0xFF = the tile never sits on that line.',
         '#ifndef CT_BATTLE_HUD_CHROME_H', '#define CT_BATTLE_HUD_CHROME_H', '#include <stdint.h>',
         '#define CT_BHUD_CHROME_NT %d' % len(words),
         'static const uint16_t ctBattleHudChromeWord[CT_BHUD_CHROME_NT] = { %s };'
         % ', '.join('0x%04X' % w for w in words),
         'static const uint8_t ctBattleHudChromePix[CT_BHUD_CHROME_NT][48][8] = {']
    for w in words:
        rows = []
        for ry in range(48):
            rows.append('{%s}' % ','.join(str(table.get((w, ry, p), 255)) for p in range(8)))
        L.append('  { /* %04X */ %s },' % (w, ', '.join(rows)))
    L += ['};', '#endif', '']
    open(out, 'w', newline='\n').write('\n'.join(L))
    print('wrote', out)


if __name__ == '__main__':
    main()
