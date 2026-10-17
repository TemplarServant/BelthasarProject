# The Epoch's time gauge: `$027C = 8`, C2/261D -> C2/6A34 (2026-10-16)

ROADMAP G1a item 3. Port: `src/world.c` (CtWorldGaugeEnter / Leave, obGaugeCtl, obGaugeSwirl, obGaugeMarker, owGaugeNeedle,
the Epoch's states 6 / 7 and C2/4387), the phases in `src/field.c` (`owGaugePh`), the picture `worldComposeGauge`.
Check: `ow-epoch-time-gauge-vs-oracle` (`tools/epoch_board_cmp.py --gauge`). Follows [ow_vehicle.md](ow_vehicle.md).

## 1. The way in and out (the main loop, C2:23DB by `$027C`)

| Module | Address | |
|---|---|---|
| 0 / 1 / 7 | 23EF | the plain loop (the sound post) |
| 2 | 24D5 | location change |
| 3 | 2402 | entering a location |
| 5 / 9 | 251E | the menu |
| 6 | 258D | another screen, C2/631F -- not ported (the Epoch's `$00F8 & 04`) |
| 8 | 261D | the time gauge |

C2/261D: the fade script C2:2EC1 and `LDX #0012 / JSR C2/0454` (18 NMIs, the world live); NMI off: `$1E00 = 82` (the song
stopped, JSL C7/0004), C2/2E23 (the six-block stash), C2/6A34; then C2/2E72 (back), C2/03EF and -- unless `$7F01F4 & 0A` or
`$7F01F5 & 02` -- C2/2C93 (the rebuild), the fade-in script C2:2EC4, 17 NMIs, `$027C = 1`.

C2/6A34: C2/26A8 (all of VRAM cleared by a fixed DMA), C2/03EF, C2/0471 (the object array's class words zeroed, `$4E = 0B30`),
the shadows -- BGMODE `$1E = 07`, OBSEL `$2101 = 03`, M7HOFS/M7VOFS `$20 = FF80`, `$26 = FF90`, M7A = M7D = 0100, the rest
0, W12SEL/W34SEL 0, WOBJSEL `$3A = 20` (the colour window on window 1), TMW/TSW 11, CGWSEL `$45 = 10` (colour math only inside
the colour window), CGADSUB `$46 = 17` (add, BG1/BG2/BG3/OBJ), fixed colour `$47-$49 = 0F` --, `$1BF5 = 0`, C2/7389, `TSB $4C
#03`, NMI on, 18 NMIs, then a look at `$027C` after every NMI until it is not 8; then script C2:74B3 (fade out), 18 NMIs,
`$1E = 09`, `$2101 = 60`, NMI off.

C2/7389, the loads: packet C2:7220[0] (C3:8000) into `$7F:9000`, its `$9800` half to VRAM word `$6400`, 64 tiles of it by the
list C2:7321 to VRAM `$6000` (the OBJ); packet 1 (C6:FB00) into `$7F:8C00` (the palettes), copied to `$7E:C000` and to the CGRAM
shadow `$0920`, `$7F:8DA2 = FFFF`; packet 2 (C6:2000) into `$7F:9000`, the one 32 x 32 block written 16 times over the mode-7
map (VRAM low bytes, C2/7254 by C2:72B8); C6:F700's nybbles into `$7F:9000` and to the high bytes of VRAM words 0-7FF (the
32 mode-7 chars, C2/7361 + C2/722C). The objects, from `$0C30`: the controller `$6AC8`, scripts C2:74B8 and C2:7504 (palette
cycles), the swirl `$6F85`, seven markers `$7143` (+22 = 0..6, the parent `$0B30`'s +22 stepped between the spawns), the
fade-in script C2:74AE.

Measured (oracle, the rule): the fade's 18th NMI at f29 of saves/epoch_re/epoch_user.state's drive, the first gauge pass at f41
-- 11 frames of NMI-off work: C2/2E23 1,142,350 + C2/26A8 524,674 + C2/7389 2,248,948 cycles (`--callrom`, refresh excluded);
the port prices the stash at 46 cycles a byte as the X menu does.

## 2. The gauge's objects

- **The controller** `$6AC8`: state 0 -- `+22` = the era of the current world (C2/747A over the table C2:6C97, or C2:6CA5 from
  storyline CC: `1D9 1F2 1F7 1F0 1F1 1F4/1F6 1F3`), HDMA channel 1 (DMAP 41: WH0/WH1 from `$8A35`), the needle's buffers
  primed, `+18 = +14` = C2:6CB3[era] (the angle: FF80 0000 0064 00CC 0130 0198 0200), `$1BB7` = the era, C2/6CC1, on into
  state 1 -- the pad (`$00F8`): 10 / 100 / 800 an era back, 20 / 200 / 400 one on (clamped 0..6, `+20` = -4 / +4), A (80): the
  era's world, `$02AF` = it and `$027C = 7` (or the buzz `18 01 80` when it is this world), 08: `$02AF = FFFF`, `$027C = 7` --
  state 2, the turn: `+18 += +20` and C2/6CC1 each frame; on the target `$1BB7` = the era, unless it is this world's era: on past
  it (bouncing at the ends).
- **The needle**, C2/6CC1: the triangle C2:6F2D turned by `+18` about (84, 70) -- C2/6F33 (signed 16 x 8 through the
  multiplier), the carries of the 16-bit sums kept -- and rasterized edge by edge (8.8 slopes by $4204/$4206) into window 1's
  per-line edges, two line buffers `$8B56` / `$8DF6` swapped by `$1BF5` bit 0.
- **The swirl**, `$6F85`: HDMA channels 2 / 5 (M7A, M7D) from `$8A42` and 3 / 4 (M7B, M7C) from `$8A4F`; per line A = D =
  cos(t), B = C = sin(t + 80), t + 4 a line; buffers `$9176`/`$9416` and `$9796`/`$9A36` (`$1BF5` bit 1) shifted a line a frame,
  the newest at the bottom.
- **The markers**, `$7143`: placed from C2:71DD; drawn only while their era is `$1BB7` (C2:71EB's appear script, C2:71EC's
  vanish, each held to its end).

## 3. The answer: the Epoch's state 6 (C2/4728) and the trip

`$02AF = FFFF`: back aloft (wings, state 3) or parked (state 8). A world: state 7 with script C2:4AAD, run inline each frame
(C2/0F63 on the Epoch's own block; its op 52 is the script's end, not the block's); 1999 A.D. (01F7) becomes 01B0 (the cockpit,
with wings) or 01D8, 01D9 and 01F2 (no wings) take their own cells; otherwise `$0102/$0103` from the Epoch's position, `$0104`
its facing. On the new world the Epoch comes up with `$0294` bit 4 still set: C2/4387, the sound `86`, state 7 with script
C2:4B8C (the arrival), and on its end bit 4 cleared, aloft again.

## 4. The picture (worldComposeGauge)

Mode 7, ppu.c's: the start of each line `((A*cH)&~63) + ((B*y)&~63) + ((B*cV)&~63) + (X<<8)` with the 13-bit scroll and
centre, the field repeating (M7SEL 0), BG1's pixel the high byte of the char word, the matrix from the swirl's channels --
scanline s takes table line s (screen row y = scanline y + 1). The OBJ with OBSEL 03 (8 / 16, word `$6000`). Mode-7 order
OBJ3 > OBJ2 > OBJ1 > BG1 > OBJ0. Window 1 from channel 1 takes table line y for row y (as owWin1Line's window); inside it
the fixed colour is added to BG1 and to OBJ palettes 4-7.

## 5. Graded, and what is left

`ow-epoch-time-gauge-vs-oracle`: ch24 to the free flight, Y, Up, A -- every change of `$0100 $027C $0294 $0290/$0292 $02AF
$1BB7` the same to `$027C = 7`; the picture pixel for pixel but the last line (below). Left:

- The way back is a frame short and the trip two frames early: the port prices C2/2C93 with the X menu's model (a remainder
  measured on world 0); on 1F6 C2/2C93 costs 11,225,250 cycles (`--callrom`), ~0.8 M more -- the X menu's return there too
  (ROADMAP F1, the load clock).
- The last line of the gauge's picture (scanline 224, just past the HDMA tables' 224 lines) is not the cartridge's (158-201 px);
  13 px of the needle's edge differ while it turns.
- `$027C = 6` (C2/258D -> C2/631F), the trip flags `$7F01F4 & 0A` / `$7F01F5 & 02` (the world is rebuilt anyway, said once),
  the `$027C = 9` menu.
