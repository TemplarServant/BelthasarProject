# The field's BACKGROUND picture: colour math, the animated tiles, the colour animation, a seed's map

Ported 2026-09-24. Found from the user's report "the FIELD picture differs from the logical oracle before any menu is
opened" (tbcolour: the tent keeper, `saves/field_re/tent_keeper_lake.state` vs `tent_keeper.lakewram`, scene 5, 2353 px
at f2; party3_field, scene 130, ~455 thumbnail px). Four mechanisms, none of them scene-specific.
Code: `src/scene.c` (CtComposeScene, CtFieldSceneTileDma / SetCgram / MapFromWram), `src/tileanim.c`, `src/objslot.c`
(CtObjSlotLoad, modes 1/2/3/8), `src/lz.c` (ct_lz_load_anim_tiles), `src/field.c` (fieldBgAnimLoad, fieldIrqTiles, the
fades). Checks: `sweep-bganim-machines` (micro-executed), `field-bg-seed-tent-keeper-vs-oracle`,
`field-bg-seed-party3-vs-oracle`, `field-bg-seed-loc13B-colour-cycle-vs-oracle`. Trace: `CT_TILEANIMTRACE=1`.

## 1. Colour math is the PPU's, in 5 bits

`ct.exe --hdma` on both seeds: the tent keeper `CGADSUB = $42` (half-ADD, math on BG2), `CGWSEL = $02` (the sub screen is
the operand), TM `$13`, TS `$04` -- every shadow is BG2 half-added over BG3. party3_field `CGADSUB = $24` (full ADD on
BG3 and the backdrop), TM `$04`, TS `$13` -- the torch glow is BG3 added over BG1/BG2. The map header's effect byte is the
CGADSUB value (scene 5 `effect=42`, scene 130 `effect=24`, `CT_SCENEDBG=1`).

snes/ppu.c `ppu_handlePixel`: 5-bit operands, add or subtract, halve (only when the sub pixel is not the backdrop), THEN
clamp 0..31. The field compositor did `(d >> 1) + (s >> 1)` on 8-bit colours: off by up to 4 per channel -- oracle
`524231` = conv5(10,8,6) against the port's `564131` (0x56 is no conv5 value at all; (11+10)>>1 = 10 in 5 bits, 90+82 >> 1
= 86 in 8). The same law the overworld and the Save screen already follow (memory ct-snes-color-math-5bit).
Measured after: the tent keeper f2 2353 -> 9 px; the attract's loc 01B fade frame (attract-fade-rate-01B-vs-oracle)
51827 -> 53248 of 53248 identical -- its note had blamed its residue on "low-bit colour-math rounding".

## 2. The animated tiles -- FD/DE98 (load), FD/E022 (run)

Jump table `$FD:FFE5..FFFF`: FFFA -> DE98, FFFD -> E022, FFF4 -> E292, FFF7 -> E39C (FFEE -> E275, no caller found).

**Load**, C0/0112 `JSL FD/FFFA` (the location load C0/0100-0119, after C0/56D4). D = $0500, DB = 0, X/Y 16-bit.
`LDX $01FE / LDA $F60001,X / AND #3F / ASL` -> `$FD:F290,X` = an offset into `$FD:F310`. Twelve records, Y = k*5:

```
fc = [X]; X++
fc == 80: $05B3,Y = 80 (no animation); next record -- parsing goes ON from the next byte
else:     $05B0,Y = fc; $05B2/3,Y = the VRAM word address; $05B4,Y = 7F; then $05B0/1,Y = 0 (the accumulator)
          fc 2: durations d0 d1 written as words d*4, four times; sources s0 s1 (4 bytes) four times
          fc 4: d0..d3 twice; s0..s3 (8 bytes) twice
          else: eight durations, eight sources
```

The words go out through the WRAM port (`$2181 = 0400`, `$2183 = 7F`; `STA $217F` 16-bit puts the high byte in
`$2180`), so record k's eight steps are at `$7F:0400 + k*$20` and its eight sources at `+$10` -- a terminator writes
nothing and does not advance the port. ROM census (all 64 sets): durations `$1A $20 $2B $40 $80` only; destinations
`$2000-$4FFF` (the BG1/BG2 characters), sources `$7F:6000-$7FFF`, all character-aligned.

**The source graphics**, C0/0960's end: C0/6D65 / C0/6D97 decompress the tileset's packets 6 and 7 to `$7F:6000` and
`$7F:7000` with no DMA and no length cap (lz.c ct_lz_load_anim_tiles).

**Run**, from the field's IRQ: C0/ECCC (`$010F` bit 7 -> nothing; `$4211` TIMEUP; force blank; HDMA off), then
`LDA $0153 / AND #01 / BNE ->ED08` -- `JSL FD/FFFD`; with the bit clear C0/ED15 (the other uploads) instead. FD/C2C1
(C0/00C7 in the main loop) flips `$0153` bit 0 every iteration, so the runner runs every other frame. FD/E022, D = $0500:

```
INC $1D ($051D); even -> records 0-5, odd -> 6-11
per record: LDA $B2 (the VRAM word) / BMI skip
            X = (acc & 0E00) >> 8                       the slot * 2 (bits 9-11)
            acc += $7F:0400+k*20,X                      the slot's step, CARRYING into the next slot
            DMA7: mode 1 -> $2118, $80 bytes from bank $B4 : $7F:0410+k*20,X  (VMAIN $80)
```

So each record steps every 4 frames, and a duration byte d is d*4 per step of 0x200 per slot: `$40` = 2 steps (8
frames), `$80` = 1 (4), `$20` = 4 (16), `$1A` / `$2B` do not divide -- the slots alternate lengths as the remainder
carries. The port's old model ran the animation off its own frame counter with `$10 -> 16, $20 -> 12, $40 -> 8, $80 -> 4`
frames per slot, from frame 0 of its own run.

Measured (tent keeper, `--monitor 7E05B0:60 --writer`): records `0C90327F`, `0D..` at f1 (writer FD:E140), group 6-8 at
f3 (FD:E1D1), `$051D` 83 -> 84 at f1 (FD:E03F), `$0153` 00/01 by FD:C2DE / C2D2. The port's `CT_TILEANIMTRACE` gives the
same bytes on the same frames from the seed.

**Under the fades.** The field's interrupt keeps running through the synchronous fades, so the tiles keep stepping:
C0/193E (the X menu's entry: `DEC $19 / JSL FD/C2C1 / JSR C0/EC60`, no FD/FFF7), C0/0CCE (the shop's: `DEC $19 / JSR
C0/881E / JSL FD/FFF7 / JSR C0/EC60`, no C2C1), C0/2824 (the menu's resume: `INC $19 / JSR C0/881E / JSR C0/00DE` =
C2C1 + FFF7). MEASURED on party3_field, X at f4: `$051D` steps every other frame through f18, stops while the menu owns
the machine, runs on from the same value at f299 -- the menu's exit does NOT rerun the loaders. The port's fades still
show a frozen picture (menuFb); only the machines' state is carried through them.

## 3. The colour animation -- FD/E292 (load), FD/E39C modes 1/2/3/8

**Load**, C0/0116 `JSL FD/FFF4`. `LDA $F60003,X / ASL A / TAX` (an 8-bit ASL) -> `$FD:F9C7,X` = an offset into `$FD:FA77`.
Records 0-5 (12 bytes each at `$0520`):

```
[X] & F0 == 0     : X++; the record's mode byte = 0 (the rest left as it was)
1x / 8x           : mode ci cc period, 3 table bytes (+6..+8), +3 = +4 = 0; an 8x also +9/+10 = X + FA77, +11 = FD,
                    X += (mode & 0F) + 1 (its frame list)
any other         : mode ci cc period, +3 = +4 = 0
```

then the mode byte of records 6-11 = 0. ROM census over the 512 locations: `1x` 219, `2x` 152, `3x` 24, `8x` 513 --
modes 4-7 are never loaded (they are op 0x88's and the battle's).

**Run**, FD/E39C (already ported for modes 4/5, src/objslot.c). On the tick's wrap (+4 == +5), 1x/8x step +3; then:

```
1 FD/E437  cc words from $F6:(+6/7 + frame * (cc*2 & FF)) to $7E:2200 + ci*2        (the live page only)
8 FD/E485  the same, the frame = the byte at +9..+11 + frame                          (bank $FD)
3 FD/E4E8  colours ci..ci+cc of $2000 AND $2200 rotate up (colour ci takes the old ci+cc)
2 FD/E532  the other way (colour ci+cc takes the old ci)
```

The NMI uploads `$7E:2200` to CGRAM (C0/712E), so the field's BG colours are that page's rows 0-7 (scene.c
CtFieldSceneSetCgram every frame) -- except after a frame whose NMI a style tilemap takes (`EAD1 LDA $46 / BNE` ->
`BRL EC15`, field_tile_copy_e5.md section 2): then CGRAM keeps the previous upload while FD/E39C goes on stepping the
page. The port bakes from `g_fieldCgram` (field.c), the image C0/712E leaves, not from the page. op 0x88's `$20` / `$30` rotations (C0/4919) are modes 2/3 on OBJ rows: with the page rotated, the sprite
renderer applies its palRot mirror only on the fallback path (an empty page row), as it already did for the blob.
MEASURED (saves/field_re/scene8_gate_cutscene.state, A three frames in thirty, into loc 071): the iris opens at f3309,
C0/4919 arms record 6 = `20 91 0E .. 00 06` at f3320, and the LIVE page's row 9 ($7E:2322..) shifts one colour left
at f3326, f3332, f3338 ... -- every 6 frames, in the page itself. The
battle's call keeps modes 1/2/3/8 inert (`colourModes = 0`): its palettes are not graded against them (the attract's
demo battle does carry a live mode-2 record, `201D0100..`).

## 4. A seed's map and VRAM

`CtSceneSeedFromWram` copies the whole 128 KB, but the bake drew from the asset map, so party3_field's open door (the
C0/0D4C bump of its `$7E:3000` cells before the capture) came out closed. On a `--loadwram` seed the port now reloads
BG1/BG2/BG3 from `$7E:3000` / `$3040` / `$3080` (+256 in `$30C0` bits 0/1; lz.c ct_lz_map_to_wram's layout).

The seed carries no VRAM, so which frame each animated record SHOWS before the runner's next DMA is taken from the
accumulator: the one slot s with `slot(acc - step[s]) == s` (the step it took last). When more than one slot fits, the
record is left as the tileset loaded it and the load says so (`[seed] tile animation k: ...`).

## 5. Where the loads sit, and a script that writes into them

The ROM's location load is C0/0085: `JSR C0/0B86` (clears `$0153`, C0:0BD4), `JSR C0/00F4` (C0/0960 tiles and packets
6-7, C0/7084 palettes, A33B map, 09DD/0A14 assemblies, 56D4 script, then C0/0112 / C0/0116 = FD/DE98 + FD/E292), and only
then C0/008E `JSR C0/56A6`, the object init that runs every fn0. Order matters: loc 01B's obj0 fn0 OPENS with
`4A 01 04 7F 00` x6 (IP 0320) -- slot 0's step of all six tables zeroed, so the stained glass holds its first frame until
the script's later `4A .. 7F 01` arms (IP 03CE) and `.. 00` (IP 040C). MEASURED: the attract f1118, FD:DF0A loads the
records and C0:67E0 writes $7F:0401/0421/../04A1 = 00 in the same frame; at f1384 the six accumulators are still 0000.
The port runs the loaders in fieldLoad just before CtSceneCreateIn (its C0/56A6), and its init pass (ct_vm.c runFunc)
now runs the poke family 0x4A-0x4D through move.c CtVmPokeFamily -- it walked over them. The same poke is what puts
$0110 = 01 at the title (loc 000 / 1B1 obj0 fn0 `4A 10 01 00 01`; the oracle reads 01 at power-on f150/f300).

## 6. Graded

`ct.exe --sweepbganim` (check `sweep-bganim-machines`): FD/DE98 + FD/E292 for all 512 locations over junk-filled records
(512/512), FD/E022 on 3000 seeded states with the whole of VRAM compared (3000/3000), FD/E39C on 6000 seeded record sets
(6000/6000; the mode 4/5 arm seeded on ranges inside the page -- objslotPalRange stops at colour 256 where the ROM runs on
to `$2400`). The seeds against the logical oracle, 90 idle frames (menu_audit_cmp.py drives `tkfield`, `p3field`):
the tent keeper's BG identical on every frame (full-resolution f2 and f45: 0 px), party3's BG identical (its one pixel is
Crono's). Loc 13B's orange circuits (four `8x` records; the state made by `ct.exe --teleport 315 2 8 0`, so its
VRAM and CGRAM are the ROM's own): identical but for one pixel of the player's -- and with the bake's CGRAM read
turned off, 90 of 91 frames diverge over the whole glow (party3's torch records colour CGRAM 17-19 and 60, which
nothing on its screen uses). The attract's loc 01B fade frame (attract-fade-rate-01B): 53248 of 53248 identical. The field VM's write mask
on party3 (vmdiff-wmask-party3): seven more modelled bytes -- the torches' colour words -- and no new divergence.

## 7. Left open

- AFTER A LOCATION CHANGE the machine is exact but its phase inherits the port's transition clock: loc 0AD in the
  attract, the port's first runner call is 23 frames after its loaders, the cartridge's 12 after FD/DE98 (f1430 -> f1442,
  and it does not run on f1439/f1440 although `$0153` is 1 there); `$051D`, a count since power-on, enters 0AD at 4F
  here and 46 there, so the two halves of the records step in the other order. The fade-out's last frame is one the
  cartridge's IRQ runs without a flip (f1414). Seeds and steady play are lockstep; a location entered in play is not yet.
- The menu's fades draw a frozen frame (menuFb); the cartridge's flags keep waving through them (the tiles' state is
  right, the picture is not).
- Scene 5 comes back from the menu one main-loop iteration ahead of the cartridge (`$0153` parity and the colour tick at
  f430 of tbcolour; NPCs too) -- the reload's length, not the animation.
- The battle's FD/E39C leaves modes 1/2/3/8 inert (the attract's demo battle carries a live mode-2 record); the battle
  arena keeps the field's page from before the battle (the field's CGRAM read is gated to field frames).
- `--battletest` seeds take the shown tiles from the battle WRAM (its records, tables and $7F:6000 survive: gato_t0) but
  not the map.
- Two sprite pixels, older than this work: tbcolour/tkfield's bottom-left NPC group on f1/f9/f10/f30/f34/f70, and
  party3 (68,110) -- a colour of Crono's that the port draws one pixel over.
