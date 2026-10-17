# Bank C2's proportional-text engine: C2/57DF, C2/5823, C2/58B2.. (2026-10-17)

Port: `src/vwf.c` (CtVwfBegin = C2/57DF, CtVwfRun = C2/5823), graded by the shadow (`ct.exe --shadow`, entries `text
set-up (CtVwfBegin)` / `text run (CtVwfRun)`: every call the cartridge makes, against all 128 KB of WRAM) and by the world
map's era label (check `ow-world-map-vs-oracle`, [ow_world_map.md](ow_world_map.md)). Earlier partial reads:
[menu2_save_rest.md](menu2_save_rest.md) sec. 5.2 (the glyph C2/5DC4), [battle_top_message_box.md](battle_top_message_box.md).

## 1. The interface

Entered by `JSL C2:0003` (-> `JMP C2/57DF`) and `JSL C2:0009` (-> `JMP C2/5823`), or straight (C2/6965, C2/5628). Both
`PHP / REP #20 / PHD / LDA #0200 / TCD`: the engine's state is the direct page `$0200-$02A1`, kept between calls.

| caller sets | |
|---|---|
| `$020C` | the string's id |
| `$020D-$020F` | long pointer to the strings' pointer table (C6:F400 for the world's names and eras) |
| `$0210-$0212` | the tile strip's long address |
| `$0213` | the glyphs one call may draw |
| `$0214` | the mode: `& 0F` = 0 a 2 bpp strip (C2/5E36), else 4 bpp (C2/5F07); 2 = pen from x 0, else from x 8; bit 7 = pen from x 0 |
| `$02A1` | the number codes in hex instead of decimal |

**C2/57DF**: `$31-$33` = [$0D + 2 x $0C] with bank `$0F`; `$30` = 0 (the state); `$34` (the pen) as above; `$15` = 4;
`$3D-$3F` = 00:0200 (the argument cursor the number codes read); `$17` = 0 (the glyph count).

**C2/5823**: C2/584A first, on `$15` (C2:5851, 0x21 entries): 0 -> SEC, the call does nothing (the string had ended);
5/7/9/B -> pen 8, 6/8/A/C -> pen 14 (`$17` = 0; mode 2 takes 8 off) -- a new line; the rest go on. Then the state
`$30` through C2:5842: 0 the string (58B2), 1 a run from [$37] (5BF5: a dictionary entry or a name), 2 digits from
`$0240` (5C3E: + D4), 3 a run from `$0240` with prefixed pairs (5C77: the hex digits). Exit: `$15` = 10 when the quota
`$13` ran out, else the control code that stopped the call; A = 0000.

## 2. The string loop (C2/58B2)

`LDA [$31] / INC $31` (16-bit, the bank stays):
- `>= A0` a glyph (C2/5DC4), then `DEC $13`; 0 -> `$15` = 10, the call ends.
- `21-9F` a dictionary entry: [$37] = DE:FA00[2 x (c - 21)], `$3A` = its length byte, `$3B` = c; state 1.
- `00-20` through C2:5903: 00 and 04-0C end the call with `$15` = c; 01 / 02 a two-byte glyph (c x 100 + next, 5B06);
  03 `$16` = the next byte, `$15` = 3; 0D / 0E / 0F a 1 / 2 / 3-byte number from [$3D] (C2/615E / 6180 / 61BD: binary
  long division into decimal digits, C2/5DAD / 5D91 / 5D56 drop the leading zeros keeping one; with `$02A1` set, 0D / 0E
  are hex through C2/5CC0 and C2:5D03); 10 ends the call with `$15` as it was; 11 the name [$3D] picks (C2:5FD8, the
  party's names at `$7E:2C23`, six bytes apart, up to five before a 00); 12 nn: nn >= 8 dictionary entry nn + 17, 00 an
  item's name (CC:15C4 + 11 x id, its icon byte 2F skipped; up to EF), 01 CC:6500 + 11 x id (11 bytes), 02-07 nothing --
  C2/5A87's table has two entries; 13-19 member c - 13's name; 1A `$7E:2C23`; 1B-1D the party slot `$7E:2980[c - 1B]`'s
  name; 1E C2:6146 (5 bytes); 1F CC:0B5F + 11 x `$7F:0200`; 20 `$7E:2C4D`.

States 1-3 count the run `$3A` and the quota `$13` down after each glyph: both left -> go on; the run over -> `$30` = 0
and back to the string if the quota is left; the quota over -> `$15` = 10.

## 3. The glyph (C2/5DC4)

The font: FF:2060 + code x 24 holds each row's left eight pixels (12 rows x 2 planes, interleaved), FF:3860 + (code >> 1)
x 24 the right four (the even code's high nibble, the odd code's low). The 16-bit code wraps (`ASL A / ADC $6C` keeps the
shift's carry). The row lands at the strip's base + 8 -- rows 4-7 of the upper tile, then the 8 rows of the tile below
(2 bpp: + F0, 4 bpp: + 1F0); planes 0 and 1 only. Columns: `$6E` = 2 x (pen >> 3) into C2:5FE6 (2 bpp, 16 columns of
10 then + 200) or C2:6066 (4 bpp, of 20 then + 400). Shift (pen & 7, C2:5EAF / 5F80): 0 both bytes stored; 1-4 shifted
right, the low byte stored one column on, the high OR-ed; 5-7 shifted left 8 - s through `$63`, the low byte two columns
on and the high one on (both STORED, not OR-ed), `$63` OR-ed. The pen then moves by C2:60E6[code - A0] (8-bit), `$17` + 1.

## 4. Graded

- **The shadow** (every call the oracle makes, the C run on a copy of its WRAM, all 128 KB compared at the return): the
  world-map route (ch24, Select on the Epoch: the place-name banner one glyph a frame and the era label) 7 / 7 set-ups and
  262 / 262 runs; `shadow-route-menu-items-equip` 23 / 23 and 60 / 60 (the menu's names and descriptions, 2 bpp);
  `shadow-route-magus-fight` 32 / 32 and 529 / 529 (the battle's messages). 0 mismatched, 0 unported.
- **The era label's strip** against the oracle's `$7E:8621` on 1F6 (`12000 B.C.`): 1024 / 1024 bytes, and the direct page
  `$0240-$027B` the same; the two pictures of `ow-world-map-vs-oracle` that showed it 0 px (were 355).
- **The twelve-glyph cut**: `--callrom C26965` from ch24 with `$0100` = 1F3 -- the cartridge stops at `$17` = 0C with
  `$15` = 10, the final `.` never drawn.

## 5. Who calls it

- **The world map's era label** (C2/6965, from C2/631F): id C2:69F0[world] (6C before storyline 36 and 6B without `$1BAD`
  bit 0 become 6F), C6:F400, mode 2 into `$7E:8621`, `$0213` = 0C -- ONE call, so the label is at most twelve glyphs:
  6D, `65000000 B.C.`, is thirteen and the cartridge shows `65000000 B.C`. The strip goes to VRAM `$6200` (OBJ tiles).
- **The world's place-name banner** (class `$5628`): state 1 the set-up (C2/5775 its sprite frame at `$7E:8600`, the
  same C6:F400 / mode 2 / `$7E:8621`, the id `$1B58`), state 2 ONE glyph a frame (`$0213` = 1) until `$15` != 10, then
  two DMA queue entries (`$5E`/`$5F`: `$8621` -> VRAM `$1E00`, `$8821` -> `$1F00`, 180 bytes each), state 3 the banner
  over the party (x less `$0234` / 2, the centre), `$7E:8604` = 6, C2/0E1D. Not ported: the port's banner is the host's
  (field.c drawOwLocName, the asset font).
- Menus and battles through C2:0003 / 0009 (C2/F90C, CD/025E, CD/027F): the shadow grades those calls wherever the
  oracle goes.
