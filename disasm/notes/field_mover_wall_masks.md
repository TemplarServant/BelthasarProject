# A mover's wall test reads the cell under the map's masks

Derived 2026-11-12 from the ROM and the oracle. Found by the whole-run sound audit: in playthrough leg 03 (Crono's
kitchen, loc 001) the port posted the cat's meow `18 4E` at frame 793 and the cartridge posted nothing in the leg.

## 1. What the cartridge does

Every step a script move issues goes through C0/ACFD, which turns the heading into a velocity and tests the tile the
step lands on (`romdis.py 00AD75`):

```
AD81  LDX $6D / REP #20
AD85  LDA $1800,X / CLC / ADC $DD / STA $DF        the new X (16 bits: $E0 its tile byte)
AD8D  LDA $1880,X / CLC / ADC $DB                  the new Y (A's high byte its tile byte)
AD93  SEP #20 / LDA $E0                            A = (new Y's tile) << 8 | (new X's tile)
AD97  REP #30 / AND $1D1E / SEP #20 / TAX          under the map's masks: $1D1E the columns', $1D1F the rows'
AD9F  LDY $6D / LDA $1C80,Y / BIT #01 / BEQ ->ADFA the object's "collides with the map" bit
ADA8  LDA $7E7000,X / STA $E1
ADAE  LDA $7E7040,X / STA $E5 / BMI ->AE27         the mover plane's bit 7: a wall
AE27  SEP #10 / INC $F8 / LDX $F8 / LDA $FE00,X / STA $EE      a random heading from the noise table, cursor $01F8
...   the same test on that heading's landing (AEB7-AEE1); a second wall zeroes the velocity (AF27)
```

The cell is the two tile BYTES under the masks, not the tile: a mover outside the map tests the cell its coordinates
wrap to.

## 2. The kitchen

The room's cats run `B6 00` (C0/354F -> C0/5410 -> 5228: chase party slot 0) for ever. Object 9 visits every 17 frames
(`$1012` = 11); object 10 is hidden and stands at `$1814` / `$1894` = FF00 / FF00, visited every 4 frames. The map is
64x64 with masks 3F / 3F (`$1D1E` / `$1D1F`), and the mover plane's cell (3F, 3F) is 81 on both sides.

So on the cartridge the hidden cat's step always lands on a wall: each visit draws a random heading (`INC $F8`), finds
that one walled too and stalls. `--monitor 7E01F8:1` from the leg's start: 1D, 1F at frame 3 (both cats), then +1 on
frames 7, 11, 15, 19 .. and one more on the visible cat's visits (20, 37, 54). The visible cat's own direct heading
(DA, up and right toward the lead) lands on a wall too, so ITS velocity is the random heading's: nothing on frames 3 and
20, and on frame 37 the noise entry 80 -- left -- which is free: `$1912` = FFE9, ten frames to the tile's edge
(`--cputrace` at frame 36: 5410, 5228-5252, 5281, ABA2, 5B95, 52A8, ACFD .. AE27-AE88 with X = 80, AF18-AF26).

The port asked its host for tile (255, 255), outside every map, never solid: the hidden cat took its direct heading
and flew off across the 256-tile coordinate (`$1814` FFE8, FFC8 ..), the cursor moved on the visible cat's visits alone
(1E for the cartridge's 29 at frame 37), and the visible cat drew other entries: it first moved at frame 54, on another
vector. By frame 757 it stood at (188, 144) for the cartridge's (223, 128), inside the reach of the lead's A press at
frame 758 -- its function 1 ran (`AB 03 / E8 4E`) and it meowed.

## 3. The port

ct_vm.c `mvCrossesSolid` masks the landing cell with `$1D1E` / `$1D1F` before it asks the host (an image with no masks
keeps the cell as it is). Leg 03 from frame 2, 236 frames: `$01F8` and the two cats' position words are the
cartridge's step for step (112 steps, 0 differing; 111 before), and the leg's sound posts are the cartridge's.

Check `leg-window-offmap-mover-walls-under-the-masks-vs-cartridge` (two mutations: the unmasked cell, the columns' mask
alone).

## 4. Open

- A cell inside the masks' range but outside the map's own size (a 48-wide map under mask 3F) is read from WRAM by the
  cartridge, whatever the load left there; the host's hook answers "free" for it.
- The room's reload at leg frame 245 leaves the object arrays zero in the port for two frames (257-258) where the
  cartridge has the new values.
