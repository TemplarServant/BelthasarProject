# The field's BG tilemaps, scroll registers and scroll mask

Derived 2026-09-15 from the attract's loc 1B0 (the time vortex) and 0A5 (the moon), where the port drew the wrong part of a
layer. ROM listings via tools/romdis.py; oracle = build/ct.exe from saves/attract_cena_inicial.state.

## 1. Tilemap geometry

FD/C0D7 (the field's PPU init) writes BG1SC/BG2SC/BG3SC = `$69 / $71 / $79`: size bits `01`, so every field BG has a
**64x32** tilemap. LakeSnes (snes/ppu.c ppu_getPixelForBgLayer) addresses a 64-wide map as two 32x32 halves: words
`row*32 + col` for x < 256 and `+$400` for x >= 256. The height is 32 rows (256 px) for every layer.

## 2. The load's tilemap build (C0/0A50)

`$1D7C/$1D7E/$1D80/$1D82 = $6000/$6800/$7000/$7800`, then:

| layer | routine | condition | upload |
|---|---|---|---|
| BG1 | C0/75E9 | always | `$800` bytes (C0/7F62): the 32x32 chip window from the camera, rows `$1D0E..+31`, metatile columns `$1D0A..+15` (C0/7612: `LSR / AND $1F` row, `AND $1E` column, `$7E:3000,X` id, `$7E:30C0,X` bit 8, `$7E:B000 + id*8` the four words) |
| BG2 | C0/78EC | `$1D35 BIT #02` clear | the same camera window (C0/7913), or the half-camera window `$12..$18` when `$0BC9` bit 7 (C0/792F) |
| BG2 | C0/794E | `$1D35 BIT #02` set | `$1000` bytes (C0/7F77): metatiles (0..15, 0..15) into the first half; the second half gets columns 16..31 of rows 0..15 when `$0BCF != $10` (C0/7991), rows 16..31 of columns 0..15 when `$0BD1 != $10` (C0/79AE, `$85 = 2`), else the first block again (C0/7976) |
| BG3 | C0/7CB5 | `$0BCA & 1` and `$01BB == 0` | as BG2 with `$1D35 BIT #04`, `$0BD3/$0BD5` (C0/7CCD camera, C0/7CE7 map, 7D28 / 7D45 / 7D0D) -- no half-camera arm |

`$1D36` = detached (md[1] bit 5 -> BG3, bit 4 -> BG2, C0/A3A7-A3BF); `$1D35` = `$1D36` OR has-own-speed (A4A5 / A4F5 when
md[2] / md[3] is non-zero). Map sizes from the header (C0/A3C1-A408): `$0BCB/$0BCD` BG1, `$0BCF/$0BD1` BG2, `$0BD3/$0BD5` BG3.
The NMI streamer (C0/EB91: `$1D78-$1D7B` bits 1/2/4 -> C0/8445 .. C0/87BF, twelve row/column uploaders) is not derived here.

## 3. The scroll registers are the step counters

The field NMI's HDMA (FD/C1EE): channel 1 `$43` `$0D` (BG1 H/V), channel 2 `$0F` (BG2), channel 3 `$11` (BG3), tables in
bank $7F alternating by `$0153 & $0F` (`$0FD7/$102E/$1085` or `$128F/$12E6/$133D`), each entry indirect to four bytes. Loc 1B0
f8470: BG1 `h0 v65502` = `$1D89`, BG2 `v514 / v517` = `$1D8D` (the two buffers a frame apart); loc 0A5 likewise.
So a layer built from the camera shows map pixel `entry camera + counter`, a map-built layer map pixel `counter`.

## 4. The scroll mask (C0/7477-74A5)

`LDX $01FE / LDA $F6000A,X / BMI ->7495`: bit 7 of the location record's byte 10 selects the whole map (`$1A = $1C = 0`,
`$1B = $0BCB - 1`, `$1D = $0BCD - 1`); otherwise `$1A = byte 10`, `$1C = byte 11`, `$1B = byte 12`, `$1D = byte 13`, unclamped.
Loc 1B0: `00 0F 00 3F` on a 32x32 map; arrival (7, 27) -> C0/7553 `$1D0E = (27 - 8) * 2 = $26` (writer C0:7581). The port
clipped the far bounds to its canvas and pinned the camera to the bottom (`$24`).

## 5. What the port does

- ct_vm.c CtSceneLayerOffsets: the origin is chosen by `$1D35` (map-built -> counter - camera) instead of `$1D36`.
- scene.c draw_layer (`mapRing`): a map-built layer wraps at 256 px down and at the tilemap's two halves across.
- field.c: the mask exactly as C0/7477; the frame crop wraps (fieldBaseAt) instead of clamping to the canvas.

MEASURED: loc 1B0, port f8463 vs cartridge f8455 57144 / 57344 identical (22642 with the old selector); the port's camera and
six counters equal the cartridge's over the visit, 10 frames later. Checks: attract-1b0-vortex-bg2-vs-oracle,
field-scroll-mask-entry-camera-1b0.

## 6. The half-camera map mode (`$0BC9` bit 7) -- PORTED

md[1] bit 6 sets `$0BC9 = 80` (C0/A397). C0/74B1-74C5 give BG2 a window at half the entry camera (`$12 = ($0A >> 1) & FE`,
`$14 = $12 + 20`, `$16 = $0E >> 1`, `$18 = $16 + 1C`, chips) and C0/792F builds BG2 from it. 93F5 `LDA $0BC9 / BMI` swaps BG2's
committers for C0/95E3 (X+) / 9619 (X-) / 964D (Y+) / 9683 (Y-): a 32-step phase in `$94` / `$97`, the window moved a chip at the
0F/10 and 1F/00 crossings, and the counter `$8B` / `$8D` stepped only when the new phase is even -- half the camera. BG3's
committers (96B7..) are the ordinary ones in both modes.

Loc 0A5 (cartridge f3977): camera row 9 phase A, `$1D89 = FECA`, `$1D8D = FF65`. BG2 map pixel = (entry row 48 >> 1) * 8 + counter
= 192 - 155 = 37. Port f5394 vs cartridge f3979: 56654 / 57344 (the bats' phase). Check attract-0a5-halfcamera-bg2-vs-oracle.

(The NMI streamer, open here until 2026-11-12, is sec. 7.)

## 7. The tilemaps are kept, and the picture is theirs (2026-11-12) -- PORTED

The port composed every camera-built layer from the map. That is the tilemap only while a layer's own scroll is the
camera's: playthrough leg 86, loc 0A3 (Mystic Mountain's cutscene), is where it is not. Op 0xE6 drifts BG1 alone 80 px
right (leg frames 15860-16018), then the three layers go 16 px left and 31 up (16431-16462) with BG2 standing 80 px
behind BG1 -- hscroll -16, so the screen's left 16 px are columns 62-63 of BG2's tilemap. The cartridge shows there the
map's chip columns 9 and 8 (metatile 4: `5945 5944` on tilemap row 24); BG2's own neighbour, the wrap of a 16-metatile
layer, is metatile 15 (`5947 5946`). 269 px of the picture. All of it below is D = `$1D00`.

### 7.1 The camera's commit names the crossings (C0/93E1)

93E1 `STZ $76 / $77 / $74 / $75`, then each layer's committers step its counter and phase a pixel at a time and flag
the 8-px crossings: `$74` down, `$75` up, `$76` right, `$77` left; bit 0 BG1, bit 1 BG2, bit 2 BG3.

| layer | X+ | X- | Y- | Y+ | counters | phases | the flag |
|---|---|---|---|---|---|---|---|
| BG1 | 944B | 947D | 94DF | 94AD | `$87` / `$89` | `$93` / `$96` | `LDA #01 / STA` |
| BG2 | 950F | 9545 | 95AF | 9579 | `$8B` / `$8D` | `$94` / `$97` | `TSB #02` unless `$35 & 02` |
| BG2, half camera (`$0BC9` bit 7) | 95E3 | 9619 | 9683 | 964D | the same | the same (32 steps) | `TSB #02`, its window `$12-$18` moved |
| BG3 | 96B7 | 96ED | 975F | 9721 | `$8F` / `$91` | `$95` / `$98` | `TSB #04` unless `$35 & 04` |

### 7.2 The tail builds the rows and columns (C0/9420-9448)

9420 `STZ $78 / $79 / $7A / $7B`, then up (98C1, on `$75`), down (985F, `$74`), left (97FD, `$77`), right (979B, `$76`):
BG1's, BG2's and BG3's builder in each, the layer's tilemap origin (`$99,$9A` / `$9B,$9C` / `$9D,$9E`: column 0-63, row
0-31) stepped after it, and C0/7F9A at the end.

- A row (8243 / 8263 / 82A5 down, 82C5 / 82E5 / 8325 up): into `$7E:C800` / `D000` / `D800`, 18 metatiles from the chip
  left of the window (`$58 = $0A - 1`, `$5C = #12`) at the window's bottom row `$10` (down) or top row `$0E` (up); the
  flag is `$78` (down) / `$79` (up).
- A column (8345 / 8365 / 83A5 right, 83C5 / 83E5 / 8425 left): into the same buffers `+$80`, 15 metatiles from the
  chip row above the window (`$5A = $0E - 1`, `$5C = #0F`) at the window's right column `$0C` or left column `$0A`;
  `$7A` (right) / `$7B` (left).
- **BG2's and BG3's builders read the same `$0A-$10`: the camera's window**, each into its own tilemap. Only BG2's
  half-camera twins (8285 / 8305 / 8385 / 8405) read a window of its own (`$12-$18`). So a layer whose scroll is not
  the camera's is given the camera's rows and columns at its own edges: leg 86's columns 62-63.
- The readers (rows C0/7612 / 79CF / 7D66, columns 77E4 / 7BA9 / 7E60): the cell is `$7E:3000,X` with X = row << 8 |
  col, the layer's plane at +`$00` / +`$40` / +`$80`; BG1 and BG2 take a ninth bit from the plane at +`$C0` (bit 0 /
  bit 1) and index the records at `$7E:B000` (8 bytes: TL TR BL BR words), BG3 is 8 bits into `$7E:C000`. The masks
  `$1E,$1F` / `$20,$21` / `$22,$23` (columns, rows) are applied to the metatile index. A row from an odd chip column
  writes the first metatile's right chip, `DEC $5C`, the whole metatiles, and ends on the NEXT metatile's right chip
  again (C0/76DB `LDA $7EB002,X`; BG3 writes the right chip and all `$5C` metatiles, 7DBB). A column from an odd chip
  row writes the bottom chip and all `$5C` metatiles: 31 words, 30 from an even row.
- C0/7F9A, per layer: where this tick's uploads go. 813D: the row at the origin's row, from the column left of the
  origin, 34 words cut where the tilemap's 32-column halves meet (up to three runs: `$9F / $A1 / $A3` words, `$DB /
  $DD / $DF` bytes for BG1); 81A1 the same 28 rows down (`$A5 / $A7 / $A9`); 81D6 the column at the origin from the row
  above it, 32 words cut at the bottom (`$AB / $AD`, bytes `$E1 / $E3`); 8218 the same 32 columns on (`$AF / $B1`).
  BG2's cells are `$B3-$C5` / `$E5-$ED`, BG3's `$C7-$D9` / `$EF-$F7`.

### 7.3 The NMI uploads them and spends the flags (C0/EB91-EC07)

`$78`: 8445 / 84C1 / 8523 (down); `$79`: 8585 / 85E7 / 8649 (up); `$7A`: 86AB / 86F7 / 8729 (right); `$7B`: 875B / 878D
/ 87BF (left). A row is DMA'd with VMAIN `$80` (84A7), a column with `$81` (86DD): 32 words, more than its builder
wrote -- the last one or two are what the buffer held. Then `EC03 REP #20 / EC05 STZ $78 / EC07 STZ $7A`: the four
flag bytes are spent. An NMI that takes a `$46` arm (EAD1: a style's tilemap, the caption plane) `BRL`s past all of it.

### 7.4 The entry and op 0xE5's styles

- The entry (C0/0A50): 74A6 zeroes the three origins; 75E9 builds BG1 (32 chip rows of 16 metatiles from `$0A, $0E`)
  and 7F62 DMAs `$0800` bytes to `$1D7C`; 78EC BG2 (from the camera 7913, its half-camera window 792F, or the map
  794E with a second half, 7F77 `$1000` bytes) to `$1D7E`; 7CB5 BG3 to `$1D80` when the room has one (`$0BCA & 1`,
  `$01BB` = 0); then C0/87F1: the right columns (8345, 8365 / 8385 unless `$35 & 02`, 83A5 unless `$35 & 04`) and 7F9A
  -- the first NMI lays the 33rd column beside the window. A camera-built layer gets ONE 32x32 half: the other half of
  its tilemap keeps what VRAM held.
- The styles (C0/179A on `$45`; the main loop is inside the branch, so no camera tick runs): 1 = 75E9, 74D4 (the origin
  and counters zeroed, 7F9A), `$46 = 1`, then 87F1; 2 = 78EC, 74E8, `$46 = 2`, 87F1; 3 = 75E9 `$46 = 3` (to the back
  buffer `$1D82`), 78EC + the `$7C` <-> `$82` swap + 74D4 + 74E8 `$46 = 2`, 75E9 + the swap + 74D4 `$46 = 1`, 87F1;
  4 = 7CB5, 74F7, `$46 = 4`, 87F1. The NMI's arms (EAD8-EB43) DMA `$0800` bytes of `$7E:C800` to `$1D7C` / `$1D7E` /
  `$1D82` / `$1D80`.

### 7.5 What the port does

- move.c: `CtVmBgBuildLayer` (75E9 / 78EC / 7CB5), `CtVmBgEntryFill` (0A96-0AF3 + 87F1), `CtVmBgRightCols87F1`,
  `CtVmBgEdgeStream` (9420-9448), `CtVmBgAddr7F9A`, `CtVmBgEdgeNmi` (EB91-EC07, the flags spent) -- on the VM's WRAM,
  the scratch written as the ROM leaves it, the DMAs through `g_ctVramSink`.
- ct_vm.c: `camCommit93E1` steps BG2's and BG3's phases and flags, then streams and uploads (the NMI that ends the
  frame; nothing else touches the buffers between); `ctXferStyleFrame` builds, uploads the `$46` arm's tilemap and, on
  the style's last frame, 87F1's columns.
- field.c: the VRAM image (`g_fieldVram`) takes the words and `g_ringKnown` marks the ones the port's uploads wrote.
  Not known: a column's leftover words (the scratch at `$7E:C800` is other routines' too); a word read from a cell
  outside the layer's own map (below); everything at a fill (the load's and the menu's return: what ran in between --
  the overworld, a menu -- wrote VRAM of its own), on a snapshot's restore, when a stage hands its VRAM back and while
  another program draws; BG1's cells of a door's block (C0/0D78..1454 write VRAM themselves through the IRQ's
  C0/ED24.., not ported).
- A chest (C0/16DC): `1700 INC A / STA $7E3000,X` bumps the WRAM cell, 1705-1767 put four tilemap words in `$09B2-$09B8`
  -- the cell at column (2 x col - `$0A` + `$99`) & 3F, row (2 x row - `$0E` + `$9A`) & 1F (C0/1B36), its right, below
  and below-right neighbours -- and `TSB $5F` bit 1 asks the IRQ's C0/EFD6 for the four words of `$7E:BFF8` (metatile
  `$1FF`, the `$FE` lid) or `$7E:BF08` (`$1E1`, the `$E0` lid). field.c fieldChestVram does both; the port used to bump
  only its picture's copy of the cell.
- Outside the layer's own map: the masks `$1E-$23` can reach past the map's own size (`$0BCB / $0BCD` BG1, `$0BCF /
  $0BD1` BG2, `$0BD3 / $0BD5` BG3, metatiles) -- a 48-wide BG1 has mask `$3F` -- and the cells of `$7E:3000` there are
  what earlier rooms and other routines left. The port's WRAM is not the cartridge's in them. MEASURED: loc 01C
  (playthrough leg 34's end; BG1 `$30` x `$40` under mask `$3F`): the right column C0/87F1 lays at chip column `$60` is
  31 words of another room on the cartridge (its own `$7E:3030,row`; 254-256 of those 1024 cells change from one visit
  of the room to the next), the port's are one metatile repeated. move.c marks such words as it builds them (gBgOut).
- scene.c `CtFieldSceneSetRing` (RingOv): every frame, the cells of each tilemap that are on the 256x224 screen, known,
  and whose word is not the one the map puts there (draw_layer's own law, the held chips included) are drawn from the
  word; the bake leaves the map's pixels out there. No such cell, no difference from before. The widescreen (a mod)
  keeps the map; the frames between fall back to reprojection on such a frame.

MEASURED (tools/field_ring_cmp.py, checks `field-ring-camera-columns-vs-cartridge` and
`field-ring-half-camera-edge-vs-cartridge`): from the cartridge's state at f15100
of leg 86 (before the room's load at f15146), 1400 frames of the leg's pad on: BG1 1374 known words and BG2 1122, none
different from the cartridge's VRAM; the picture 0 px from the cartridge's (269 before), 38 cells of BG2 drawn from the
tilemap. Two more rooms the tilemaps put right, found by the sweep: loc 0EB (leg 54 f150; BG2 64x64 standing 560 px
below BG1, given the camera's rows) -- 240 px, the port with the tilemaps is the cartridge's in every one; loc 0DD (leg
40 f2846, BG2 in the half-camera mode) -- 73 px on one frame in every 16, the cartridge's in every one, the frame 0 px
from the cartridge's, with BG1 1958 / BG2 1505 / BG3 2048 known words and none different from its VRAM (the second
check); loc 0A9 (leg 92 f200; BG3 standing 288 px above BG1, given the camera's rows) -- 1888 px, the cartridge's in
every one; loc 0F3 (leg 109 f11950, the Epoch's launch: BG2 driven up 7 px a frame under a still camera, so every row
it crosses is built from the camera's bottom row -- 894 cells of BG2 at the most) -- 27,320 px for some 340 frames,
the cartridge's in every one, the frame 0 px from the cartridge's.

THE SWEEP (scratch `s14_cmpall2.py` / `s14_cmp_tee2.py`, build/_pt77/scratch_tools): every playthrough leg on both
sides with CT_RINGDBG and CT_SHOTS_RING -- the frames where a cell was drawn from a tilemap, and the port's tilemaps
at the leg's end against the cartridge's VRAM. 138 legs, 100 SAME as before, every leg's line as pinned (leg 93's
re-pinned, below); at the end of the 100 SAME legs 296,730 known words, none different from the cartridge's VRAM (a
DIFFERENT leg ends in another room or moment: its words say nothing). Every span of drawn cells was put beside the
cartridge (`s14_threeway.py`: where the builds with and without the cells differ, which one is the cartridge's): the
rooms above, or no visible difference -- the one frame of a load or of the overworld's entry, black, where the new
room's fill meets the camera of the frame before. (The whole sweep ran on the build before BG3's phases were zeroed at
the menu's return; legs 118 and 92 and the full suite again after it.)

Found on the way (leg 19, loc 078: 525 px with the first form): without the NMI's `STZ $78 / STZ $7A` a later upload
sent the same flags again, after a style's 75E9 had filled `$7E:C800` -- the build's first row landed on tilemap row 28.
And leg 33, loc 048 (118 px): with the chest's WRAM cell left closed, the row streamed over an open chest laid the
closed lid back.
And leg 118, loc 165 (34 px): BG3's phases `$95` / `$98` were not zeroed with the rest of the `$1D` page on the menu's
return (the port zeroed BG1's and BG2's only: nothing read BG3's before), so BG3's crossings came a few pixels off
the camera's and its origin stood a column and a row ahead.

What else moved, with the builders running on the VM's WRAM (their buffers `$7E:C800-$D7FF` held the port's zeros
before):

- Entry 3's hand-off VRAM (attract-entry3-stage-picture-vs-oracle, which listed "the streamer's column" as not
  modelled): BG1's map 2048 / 2048 words (was 2017), BG3's 2047 / 2048 (was 2016; the word left, `$77C0`, is the
  leftover 32nd word of BG3's column DMA).
- The battle's work RAM lies INSIDE the buffers (`$7E:CA1E` .. `$7E:CD37` and more: battle.c), so a cell a fight
  does not initialise starts with the words the field's builders left -- as on the cartridge. MEASURED:
  lavos-day-chain-in-the-port went onto the cartridge's frames (15A f5500, 1D3 f5931; the fight ended 83 frames
  late before), with `$7E:C800-$D7FF` the only WRAM difference at the loads before the fight (CT_LOADWALK_DUMP, the
  builds before and after). Playthrough leg 93, a fight-length leg (DIFFERENT before and after), ends on another
  moment of the same desynchronised run (its line re-pinned; the picture is the same with and without the
  tilemap's cells drawn). Which cell moves each fight is not singled out.

OPEN: the half of a tilemap no upload of the room wrote (after a load it is the room before's or the overworld's
words) is not known to the port and is drawn from the map; the `$7E:3000` cells outside a layer's own map (the
cartridge's leftovers, above); the doors' own VRAM writes (their cells are forgotten); an edge upload on a frame whose
NMI takes the caption plane's `$46` arm (the cartridge drops it, the port sends it).
