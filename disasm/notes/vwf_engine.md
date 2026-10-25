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
  over the party (x less `$0234` / 2, the centre), `$7E:8604` = 6, C2/0E1D. Ported 2026-10-17 (world.c obNameBanner),
  sec. 6.
- Menus and battles through C2:0003 / 0009 (C2/F90C, CD/025E, CD/027F): the shadow grades those calls wherever the
  oracle goes.

## 6. The place-name banner, class $5628 (C2/5628)

Port: `world.c` obNameBanner, dispatched on its class word in the object pass (CtWorldAmbientFrame). Graded by
`tools/ow_banner_cmp.py` (check `ow-place-name-banner-vs-rule-cartridge`).

States through C2:5637 (`3F 56 63 56 99 56 F9 56`); each falls into the next, every exit is CLC:
- **0 (563F)**: `INC +2`, `+0F` = B0 (world coordinates, priority 3), `+10/+11` = 0; C2/5775 copies C2:57C2 (4 bytes:
  `04 04 86 00`, the anim script -- op 4, the frame `$8604`, dwell 0) to `$7E:8600` and C2:57C6 (MVN, `$19` bytes) to
  `$8604`: a count (0 here) and six pieces `{x 00..50 step 10, y D0, word 01E0 + 2k}` -- 16 x 16 OBJ tiles $1E0-$1EB
  48 px above the block; `+0B` = `$8600`, `+0D` = `$7E`.
- **1 (5663)**: `$1B58` = 0 -> out. Else `INC +2`, `STZ +0E`, `$020C` = `$1B58`, `$020D` = C6:F400, `$0214` = 2,
  `$0210` = 7E:8621, `JSL C2/57DF`.
- **2 (5699)**: `$0213` = 1, `JSL C2/5823`; `$0215` = 10 -> out (more glyphs next frame). Else `INC +2` and two entries on
  the NMI's queue at `$0060 + $5F` (`INC $5E` around them): `$7E:8621` -> VRAM `$1E00`, `$7E:8821` -> `$1F00`, `$0180`
  bytes each, VMAIN 80; `$5F` += 10.
- **3 (56F9)**: `$1B58` = 0 -> state 1, C2/5798 (`$7E:8621-$8A20` zeroed), and on to the draw (so the banner shows once
  more, from the VRAM it still has); `$1B59` set -> state 1, `STZ $1B59`, C2/5798, out. 571E: `$0294` bit 4 -> out. X/Y
  = `$0283/$0285` (on foot) or `$0290/$0292 + 40` (`$027E` = 2, the Epoch); `$8604` = 6; X -= `$0234 >> 1` (half the
  pen: the text centred); `JSR C2/0E1D` -- op 4 with dwell 0 parks every frame and enqueues the frame (C2/0B53).

The name comes from the scanner (class $3264, C2/3349 -> C2/6263 `$1B58`), which only looks when `$0282` bit 0 is set:
on foot by the lead's step arrival, C2/375F-376D -- C2/39AE on +22/+23 (C2/39D5's top pair of the target cell, read on
the step's input frame C2/36A4-36AE): both with bit 2 -> `TSB $0282 #03`, else `TRB $0282 #02` (and the scanner then
clears `$1B58`). The port's free-roam lead is the host mover, so the test is made at its arrival (CtWorldLeadArrive).
C2/0E1D and C2/0D08 read the script and the frame through the block's bank `$7E` (world.c owAnimPtr).

Measured (saves/ow_w0.state, CT_ORACLE_LOGICAL, `--btnscript 40:72,20:8,40:8,0:60,80:30,0:60`, `--monitor
7E0EF2:1,...`; the block is `$0EF0`): f88 `$1B58` = 0B (Market) and states 1 -> 2 in the same pass, the first glyph
(`$0217` = 1, pen 09); one glyph a frame to f93 (`$0217` = 6, pen 29); f94 `$0215` = 0, state 3, the queue (`$5F` = 10),
X = 168 - 14 = 154, Y = 120, `$8604` = 6; first shown in `--peek` 96. The walk off: f149.. X follows the party; f156
`$1B58` = 0, state 1, still drawn (peek 157-158), gone from peek 159. The port, `--loadwram saves/field_re/ow_w0.lakewram`,
the same route 43 frames later: every change of the block the same, and on 15 compared frames the strip, the frame, the
direct page `$0200-$02A1`, the block (but +3/+4/+3E), VRAM `$1E00-$1FFF`, the six OAM entries and the text's pixels
identical.
