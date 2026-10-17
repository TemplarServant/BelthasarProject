# The world map: `$027C = 6`, C2/258D -> C2/631F (2026-10-17)

ROADMAP G1a item 3. Select aboard the Epoch or a Dactyl (the vehicle's `$00F8 & 04`) sets `$027C = 6`. Port: `src/world.c`
(CtWorldMapEnter, obMapCtl, obMapMarker, obMosaic, the VM's ops 2A / 2B), the phases in `src/field.c` (`owMapPh`), the
picture `worldComposeMap`. Check: `ow-world-map-vs-oracle` (`tools/epoch_board_cmp.py --map`). Follows
[ow_time_gauge.md](ow_time_gauge.md), whose shape it shares.

## 1. The module (C2/258D)

| | |
|---|---|
| 258D-25A1 | script C2:2EC7 (`00 2A 07 02 38 10 28 01 52`: the mosaic up on BG1-BG3 a size every 2 frames, 16 frames, the fade out a step a frame), 34 NMIs (C2/0454) |
| 25A4-25CF | NMI and HDMA off, `$4A = 0`; MVN `$0000-00EF` -> `$7F:3F00`, the object array `$0B30-1B2F` -> `$7F:2800` |
| 25CF | C2/631F (below): the set-up, NMI on, the map until `$027C != 6`, 17 NMIs, `$1E = 09`, `$2101 = 60`, NMI and HDMA off |
| 25D2-25F3 | the two blocks back, C2/03EF, C2/2C93 (the world rebuilt) |
| 25F6-2617 | script C2:2ED0 (`00 29 01 38 08 2B 07 02 52`: the fade in, 8 frames, the mosaic down), `$1B58` -> `INC $1B59`, `TSB $4C #03`, NMI on, 41 NMIs, `$027C = 1` |

C2/631F: BGMODE `$1E = 07`, OBSEL `$2101 = 63` (16 / 32, word `$6000`), CGWSEL `$45 = 00`, CGADSUB `$46 = 20` (the fixed colour
added to the backdrop); C2/26A8 (all of VRAM cleared by one 64 KB DMA), C2/03EF, C2/0471 (the object array's class words);
C2/6965 (the era's label: its id from C2:69F0[world] -- `6C` before storyline 36 and `6B` without `$1BAD` bit 0 become `6F` --
drawn by the proportional-text engine C2/57DF / 5823 into `$7E:8621`, sent to VRAM `$6200`); C2/68F4 (the backdrop's ramp:
16 shades x 7 lines at `$8B56`, the rest on from `$8BC6`; HDMA channel 1, DMAP 40 to `$2132`, indirect from `$8A35`: `F0`
twice -- 112 lines each); C2/63DF (the map, sec. 2); the markers' tiles C6:F500 / F520 to VRAM `$6000` / `$6020`, CGRAM 0 = 0,
C6:F540 to `$0A20`; the controller `$666C` (C2/048A, at `$0C30`), script C2:69F8 (`00 2B 03 02 29 02 52`: the mosaic down on
BG1/BG2, the fade in a step every 2 frames); `TSB $4C #03`, NMI on; then `JSR C2/046E` and a look at `$027C` after every NMI.

## 2. The map (C2/63DF)

- C2/27DE: the world's seven char packets (C6:FD00[world] -> its first seven bytes, index into C6:FE20) into `$7F:9000 + k x 1000`.
- C2/6400: one colour a tile for 0x380 tiles -- the pixel at column 3 of row 2 (its four planes: +04 / +05 / +14 / +15) -- at
  `$7E:AB25`.
- C2/645C: 64 rows of 96 cells of the world's two map layers (`$7F:5800` with blocks at `$3800`, `$7F:4000` with blocks at
  `$3000`; the second layer skipped on 1F5): C2/653E puts each cell's two pixels -- the colours of its tiles 1 and 3 OR their
  palettes (attr & 1C) << 2, through the filter C2/658E (0F, 2F and 24-2B dropped; on 1F3 40-4E instead) -- one above the
  other in a mode-7 char; four rows at a time from the staging `$7E:F000` to the chars' high bytes from VRAM `$0040`.
- C2/65B2: the mode-7 map -- chars 1..C0, 12 a row, 16 rows, at column 2, row 6 of the plane (low bytes).

## 3. The objects

- **The controller** `$666C` (states C2:6675): 0 -- the plane's centre on the party (`$34 = ($0283 >> 4) + 10`, `$20 = -$34`,
  `$36 = ($0285 >> 3) + 30`, `$26 = 0`); 1 -- the zoom out, 32 steps of the scales C6:F560 / F5A0 (+24 / +26) into the matrix
  C2/6736 + 67B3 (cos x 8 / sx, sin x 8 / sx, -(sin x 8 / sy), cos x 8 / sy through $4204-$4206, the signs put back), then
  script C2:6A0A (`10 48 1B`: `$1B48 = 0`; `35` the markers `$67FA` -- the party, `$683B` -- the Epoch on its world, `$68A3` --
  the label); 2 -- B (`$00F6 & 08`): script C2:69FF (`11 48 1B`: `$1B48 = 1`; the mosaic up, the fade out); 3 -- the zoom back,
  then `$027C = 7` and SEC.
- **The markers**: C2/688A's place (x / 8 + 20, y / 8 + 30, each with the shift's carry), their blink in bank C2 (6A18 /
  6A20), gone once `$1B48` is set. The label: C2:68E4[world], y C0, pose 47.
- **The mosaic** (VM ops 2A / 2B, classes `$21F8` / `$2194`, C2/15A7 / 15B5): +1 the layers OR-ed into `$1F`, +2 the frames a
  step (its first visit only arms it: the first step is on its third visit); up to F, down to 0.

## 4. The stretches and the NMI's return

Measured from the oracle under the rule (`CT_ORACLE_LOGICAL_TRACE` prints each stretch's cycles and where the NMI came back):

| | 1F0 (saves/epoch_re) | 1F6 (ch24) |
|---|---|---|
| the way in (stash + C2/631F), refresh excluded | 24,801,448 | 24,313,912 |
| the way back (stash + C2/03EF + C2/2C93) | 11,635,250 | 11,107,072 |

The difference between worlds is C2/27DE's decompression (the model, owMapLzCycles: 4,235,520 on world 0, 3,747,516 on world
6 -- 488,004 apart; measured 487,536); the rest of C2/631F is the same, 20,366,238 (`--callrom C2/631F` to the wait loop,
less world 0's decompression). C2/2C93 = ct_lz_world_return_cost + 1,733,474 (owRebuildCycles, shared with the time gauge's
return, whose own stretch on 1F6 it also meets: 12,049,946 against 12,050,166).

**The NMI's return** (owNmiOffFrames): C cycles cross floor(C / F) vblanks; the NMI comes at the next -- unless the stretch
ends inside the vblank it last crossed (lines 225-261): RDNMI is still up and `LDA #81 / STA $4200` raises the NMI at once
(snes.c `$4200`), a frame early. The way in on 1F6 is 70.09 frames: NMI on at line 249 of its 70th vblank, 69 frames without
an NMI; the way back 32.02, line 230: 31. And that NMI is not one C2/0454 counts: C2/0346 reads `$4B` after it and waits for
the next change, so the 41 NMIs after C2/260C take 42 passes.

**The cartridge's lost picture**: snes_runFrame cannot stop inside an opcode, and C2/26A8's DMA (1.47 frames) starting 0.57
frame after the phase-0 vblank ends at line 248 of the vblank after next -- one call for two frames (`CT_ORACLE_RUNFRAME_TRACE`:
`runframe: call 3118 ran frames 3975-3976 (to V=248) at C2:6331`). The console shows both; the oracle's harness one picture
fewer from there (68 black pictures for 69 frames). The port keeps the console's clock; the check reads the lost pictures from
the trace and moves the cartridge's events and the presses after them a frame on.

## 5. The picture (worldComposeMap)

Mode 7, ppu.c's: the matrix from the shadows `$2C-$33` (no HDMA), the centre `$34` / `$36`, the scroll `$20` / `$26`, the
field repeating; BG1 under the mosaic of `$1F` (ppu.c: column x - x % size, line from the block's first). OBJ with OBSEL 63;
mode-7 order OBJ3 > OBJ2 > OBJ1 > BG1 > OBJ0. The backdrop: CGRAM 0 (= 0) plus the fixed colour, its blue from channel 1 a line
at a time (row y takes entry y -- a colour register reads like a window, not like a scroll), red and green the NMI's `$48` /
`$49`. Every shadow the NMI pushes is the LAST pass's (C2/01D5 `STA $2106` and the mode-7 block run before C2/051D's pass):
`g_owZpShow`, the direct page latched with the camera; the world's own compositor takes its mosaic from it too. On the
world's clock (ch24: 359 frames behind from the free flight) the port's picture P is the cartridge's `--peek` P + 1.

## 6. Graded, and what is left

`ow-world-map-vs-oracle`: every change of `$0100 $027C $1C $1D $1F $1B48` and of the controller's state the same, frame for
frame, to `$027C = 1`; twelve pictures pixel for pixel (the mosaic and the fades both ways, the zoom, the map, the way back)
but the label. Left:

- **The era's label** (355 px): C2/6965's text runs through the proportional-text engine C2/57DF / 5823 (C2/58B2..), not
  ported -- the label's tiles stay blank (`owmap: UNDERIVED ...`, said once). The same engine draws the world's location names.
- C2/631F's rest is measured on world 0 and graded on 1F6 only; C2/6965's share (the text) moves with the label's length.
