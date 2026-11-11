# BG3's wave a line at a time, and a sprite under a BG pixel that has the colour math (2026-11-11)

Found on playthrough legs 134 / 135 (Death Peak, loc 0F6): the port drew the snow as one flat layer over plain sprites,
19,770 of 57,344 pixels apart from the cartridge 40 idle frames after the cartridge's own state. Three mechanisms were
missing; a fourth case of the second one turned up at the End of Time (loc 1D0) while the first was being checked there.

Every picture below is `tools/field_picture_cmp.py` or its scratch twin: both sides start on ONE cartridge state (the
route's, `saves/playthrough/<leg>.state`), the pad is idle, and the port's picture is compared with the cartridge's at
the lag that fits best (the port's picture P is the cartridge's P+2).

## 1. BG3's scroll is HDMA channel 3's, one line at a time

- FD/C1EE programs channel 3 as DMAP 43 on `$2111` (BG3HOFS twice, BG3VOFS twice), indirect, bank $7F, the table at
  `$7F:1085` (set A) or `$7F:133D` (set B) by `$0153 & 0F`, as the other channels (field_hdma_tables.md).
- FD/C2C1 builds the table each frame (native: src/hdma_table.c). With the window wave on (`$1DF9`, a location
  property) channel 3's entries are thirteen `90 <wave>` runs of 16 lines over the wave's sixteen dwords, which the
  field tail C5E2 / C732 fills: `$1DFB` += 1, X = `$1DFB & 3E`, `LDA $C0FF30+4k,X / ADC $001D8F / STA wave+4k` and
  `$1D91` at wave+2+4k.
- **Measured, the rule's cartridge** (`ct.exe --hdma <state>` with `HDMA_TRACE_BG=1`, loc 0F6): BG3HOFS on lines 1.. =
  985 986 985 984 982 980 978 977 976 975 976 977 979 981 982 984 ..., BG3VOFS one value: +-5 px over 16 lines.
- **Rooms with `$1DF9` set among the route's 137 end states:** Death Peak 0F6 (legs 134, 135) and the End of Time 1D0
  (legs 54, 72, 83, 106, 130, 137); loc 1A7 (a cutscene room inside leg 130) has it too. `$1D35` is 04 in those end
  states (BG3 built from the map), but nothing in the channel depends on that.

**The port (field.c, the layer-offset block; scene.c `CtFieldSceneSetBg3Rows`, `draw_layer`'s `gDrawRowDx`).** With
channel 3 on (`$0128` bit 3) the field walks the table (`fieldHdmaWalk`, as it does channels 4 and 6). Where a line's
BG3HOFS differs from the first line's, BG3 takes the first line's words as its offset and each row its difference.

- **Row r shows transfer r - 1.** A scroll HDMA writes in a line's H-blank is the next line's. Measured: with row r on
  transfer r, 43 rows of 224 are the cartridge's; with r - 1, all of them.
- **The lines of an open text box are the box's.** Modes 1 / 2 put a plain entry (`$14FC` / `$1504`, another BG3VOFS) on
  the box's 2n lines: those rows are not the layer's and take no shift. Measured at loc 1A7 with the top box open
  (`$0126` = 01, `$0127` = 28; leg 130 f2531): the wave under and around the box is the cartridge's (12,179 -> 846 px,
  the 846 being sec. 4's disc).

## 2. The wave's phase rides BG3's vertical steps

`$1DFB` is not only the builder's: BG3's vertical committers move it four a pixel.

```
C0/9721  (BG3 down, $4C pixels)   per pixel: INC $FB x4, then LDX $91 / INX / STX $91
C0/975F  (BG3 up,   $4B pixels)   per pixel: DEC $FB x4, then LDX $91 / DEX / STX $91
```

The horizontal committers (C0/96B7 / 96ED) and BG2's leave it alone. So the wave is fixed to the layer's rows: as the
snow climbs two pixels a frame the phase goes -8, +1 from the builder.

- **Measured** (`PEEK_WRITEWATCH=1DFB:1`, loc 0F6): eight `DEC $FB` a frame (C0/9768..976E twice) and the builder's INC:
  DB -> D4 -> CD -> C6. The port stepped +1 a frame.
- **The port:** ct_vm.c `camCommit93E1`, the BG3-Y counter's arm adds `4 * w` to `$1DFB`.

## 3. A sprite under a BG pixel that has the colour math is that pixel's SUB pixel

The field's BGMODE is 09 on every frame (the NMI's C0/EA7B `LDA #09 / STA $2105`: mode 1 with BG3's priority bit), so
the order is

    BG3.1 > OBJ3 > BG1.1 > BG2.1 > OBJ2 > BG1.0 > BG2.0 > OBJ1 > BG3.0 > OBJ0

With OBJ on both screens (TM and TS), a sprite pixel that loses the main screen to a BG pixel can still win the sub
screen; if that BG's layer is in the math (CGADSUB) the PPU adds (subtracts, halves) the two: the sprite shows through.

- **Death Peak (loc 0F6):** TM 17, TS 13, CGWSEL 02, CGADSUB 44. The snow's priority-1 pixels are above every OBJ and
  are half-added to the sub screen -- the room, and the party and the Poyozos where they stand.
- **The End of Time (loc 1D0):** TM 17, TS 12, the math adds (the oracle's `--ppuregs`). A character inside a pillar of
  light is under the pillar's priority-1 pixels (OBJ priority 2) and wins the sub screen, so the pillar adds it. The
  port's foreground re-draw covered the character: the lead disappeared in the light.

**The port.**

- scene.c: each compose keeps, for the main screen and for the sub screen, the top BG pixel's *level* against the
  sprites (`BG_OBJ_LVL`, marked by `draw_layer` as each pixel is written, so the last layer drawn decides): the lowest
  OBJ priority that still wins the pixel -- BG3.1 4 (none), BG1.1 / BG2.1 3, BG1.0 / BG2.0 2, BG3.0 1, no BG pixel 0.
  `CtFieldSceneObjLevels` hands both maps out; `CtFieldSceneMainScreen` returns the main screen before the math for
  this setup too (`*objBoth`).
- field.c: `g_objSubMask` is the main level where the pixel's layer has the math (levels 3 and 4), `g_objSubTop` the sub
  level. `drawSprite` mixes a pixel of priority p when `p < mask && p >= top` (the PPU's 5-bit sum, as for a sprite on
  the sub screen alone) and marks it (`g_objSubDone`); the foreground's re-draw leaves marked pixels.
- **Not covered, no case measured:** a priority-0 BG pixel with the math over an OBJ of priority 0 / 1 (levels 2 and 1)
  stays the stencil's (the bake's pixel, unmixed); a sprite that loses BOTH screens under BG3.1 is drawn plain where no
  foreground tile re-draws over it; and the old path for OBJ on the sub screen alone (Manoria, loc 081) still mixes
  without asking the sub level.

## 4. A main pixel no layer covers is the backdrop, and the backdrop is in the math only by CGADSUB bit 5

Seen while measuring sec. 1's text-box case: loc 1A7 (leg 130 f2531), the black disc in the middle of the plasma. The
cartridge draws it solid black; the port drew dimmed plasma, 846 px, before and after secs. 1-3.

- The screen: TM 13 (BG1, BG2, OBJ), TS 04 (BG3, the plasma), CGWSEL 02, CGADSUB 02 (the math on BG2 alone, adding),
  W12SEL 22 with TMW 17: window 1 takes BG1 and BG2 off the main screen inside the disc.
- Inside the disc no main layer is left, so the main pixel is the backdrop (CGRAM 0, black). The PPU mixes the sub
  pixel into it only when the backdrop is in the math -- CGADSUB bit 5 (`ppu_handlePixel`: layer 5's `mathEnabled`). It
  is clear here: black.
- **The port** showed the sub pixel wherever no main layer covered, whatever CGADSUB said (the right picture when bit
  5 is set, the backdrop is black and the math adds without halving). scene.c `CtComposeScene`: `back_en` = bit 5 of the
  live CGADSUB; with it the backdrop takes the sub pixel through the same 5-bit sum as a layer, without it the pixel is
  CGRAM 0. `CtSceneLayers.backEn` hands the same bit to the frames-between drawer (fieldnative.c).

## 5. Results

| state (idle pad) | before | after |
|---|---|---|
| leg 134's end, Death Peak 0F6 (+12, +40, +133) | 19,770 px | 0 |
| leg 106's end, End of Time, the lead in a pillar | 3,605 | 0 |
| leg 130's end, End of Time | 2,187 | 0 |
| legs 54, 72, 83, 137's ends, End of Time | -- | 0 |
| scene 200 (the oracle teleported there), sunbeams over three sprites | 428 | 0 |
| leg 130 f2531, loc 1A7, top box open: the wave | 12,179 | 846 |
| the same, with sec. 4's backdrop | 846 | 0 |

Checks (docs/TESTING.md, `tools/field_picture_cmp.py`): `field-picture-death-peak-snow-vs-cartridge`,
`field-picture-end-of-time-pillar-vs-cartridge`, `field-picture-backdrop-out-of-the-math-vs-cartridge`; nine mutations,
all killed. In the playthrough check ten legs' end pictures moved with secs. 1-3, all toward the cartridge.

## 6. The frames between (a mod), and what is left

- **The native drawer of the frames between** (fieldnative.c, the 60+ fps mod) composes the map's layers at their own
  scrolls and knows neither BG3's rows nor a sprite mixed in as a sub pixel. On a frame that has either (`g_bg3RowOn`,
  `g_objSubMixed`) the field hands it no layers, and that frame's in-betweens are the reprojection's (`CtMotRaster`),
  from the frame's own picture. Teaching the native drawer both is open.
- **Death Peak's frame rate in the port:** a BG3 that moves every frame re-bakes the room every frame (about 20-30
  frames a second headless on a loaded efficiency core against 90-120 in other rooms). The picture is right; the cost
  is the bake's.
