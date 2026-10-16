# Op 0xE4's picture: the VRAM keeps its chips until the ROM uploads them

Derived 2026-10-15 (ROM + the rule's cartridge, `CT_ORACLE_LOGICAL=1`). Ported in `src/scene.c` (`CtFieldScenePicHold` /
`CtFieldScenePicRelease` / `CtFieldScenePicReleaseRect`, the held chips in `draw_layer`) and `src/field.c`
(`fieldPicWindow`, `fieldPicStream`, `fieldTileCopyDrain`, the load's fill, the door and chest handlers). Graded by
`tools/dragon_tank_cmp.py` (the `railing` lines, check `field-dragon-tank-bridge-vs-rule-cartridge`).

## 1. The op writes WRAM and nothing else

```
3D97 TYX / INX / REP #20
3D9B LDA $7F2001,X / STA $3E ... $40 ... $42      the rect (16-bit pairs), as op 0xE5 (field_tile_copy_e5.md 1)
3DB5 LDA $7F2001,X / STA $44                     the plane mask
3DBB STX $C7 / 3DBD JSR C0/AF4E                  THE COPY, on the map in WRAM ($7E:3000/3040/3080, 30C0, 7000/7040)
3DC0 LDX $C7 / INX / CLC / RTS                   the visit ends (field_tile_copy_e5.md 5)
```

No `$0117` bit, no `$46`, no VRAM queue: C0/AF4E has two callers, 3DBD and C0/1790 (op 0xE5's service, which then runs
the style branch's rebuild). So after an E4 the picture is whatever the tilemaps held, until something rewrites them.

## 2. What rewrites the field's tilemaps

| writer | what | source |
|---|---|---|
| the load's fill C0/0A50 (C0/286C's `286F JSR`, a location load and the menu's reload) | each layer's whole window: C0/75E9 (BG1), 78EC / 794E (BG2), 7CB5 (BG3), DMA C0/7F62 / 7F77 | the WRAM map after obj0's slot 1 (C0/56C8, C0/0206) |
| op 0xE5's style branch C0/179A | the style's layers' whole windows (field_tile_copy_e5.md 2) | the WRAM map after C0/1790's AF4E |
| the camera streamer | one chip line per direction per frame (below) | the WRAM map in the camera tick |
| the doors' C0/0D78..1454, the chest's C0/1705 | their own block | the bumped WRAM cells |
| C0/2617 / 2658 (75E9 + C0/74D4 / 74E8 / 74F7), on the way into the C3 sequences (`JML C3/0000`) | BG1 (and the origins) | the WRAM map (not modelled here: the sequences are their own programs) |

**A battle does not touch them.** The rule's cartridge, the 01C seed below: VRAM `$6000-$7FFF` (BG1, BG2, BG3) is byte
for byte the same at c7360 (inside the fight) and c7363 (the field back): the fight's own layers live elsewhere and the
return (C0/0304-038C: A33B, 09DD, 0A14, 56D4, 595C, A712, 2B78) rebuilds the WRAM map, not the tilemaps.

### The streamer

The camera tick C0/881E ends in C0/93E1, the commit: the per-pixel loops C0/944B (X+) / 947D (X-) / 94AD (Y+) / 94DF (Y-)
move BG1's chip window `$1D0A..$1D0C` x `$1D0E..$1D10` at each 8-px crossing (phase 07/0F) and set `$1D76` / `$1D77` /
`$1D74` / `$1D75`; then

```
9420 STZ $78 / STZ $79 / STZ $7A / STZ $7B
9428 LDA $0BC9 / BMI ->943C                   the half-camera mode's own set
942D JSR 98C1 (Y-)  JSR 985F (Y+)  JSR 97FD (X-)  JSR 979B (X+)  BRL 7F9A (the VRAM addresses)
```

Each consumer tests its flag's bits 0/1/2 (BG1/BG2/BG3) and runs one builder into `$7E:C800/D000/D800` (+$80 for the
columns), which the NMI DMAs (C0/EB91 -> C0/8445..87BF):

| builder | line | start | count |
|---|---|---|---|
| C0/8243 / 8263 / 82A5 (Y+) | chip row `$1D10` | chip column `$1D0A - 1` | `$12` iterations = 36 chips (C0/7612) |
| C0/82C5 / 82E5 / 8325 (Y-) | chip row `$1D0E` | chip column `$1D0A - 1` | 36 chips |
| C0/8345 / 8365 / 83A5 (X+) | chip column `$1D0C` | chip row `$1D0E - 1` | `$0F` iterations = 30 chips (C0/77E4) |
| C0/83C5 / 83E5 / 8425 (X-) | chip column `$1D0A` | chip row `$1D0E - 1` | 30 chips |

The half-camera BG2 builders (C0/8285 / 8305 / 8385 / 8405) use `$1D12..$1D18`. BG2 and BG3 built from the camera use BG1's
window. A layer built from the map (`$1D35` bit 1 / 2) is never streamed: C0/87F1's `8803` / `8814`, and its consumers' bits
come only from a camera-built layer's crossings. The builders read `$7E:3000,X` with X = (row & `$1F`) << 8 | (col &
`$1E`), `$1D1E..$1D23` the metatile masks, and `$7E:B000 + id * 8` the four chip words.

MEASURED (01C, every post-load frame captured): `$1D0C = $1D0A + $20`, `$1D10 = $1D0E + $1C` -- 33 x 29 chips, the screen
at any phase. Each line built covers the window's span, so **every chip that enters the window is built from the WRAM of
that frame's camera tick** (before the VM pass C0/00BF), and a chip that never leaves it is never rebuilt.

## 3. Measured

The VRAM against the WRAM map, chip by chip: slot = (chip - `$1D0A` + `$1D99`) & 63, (chip - `$1D0E` + `$1D9A`) & 31 in
the 64x32 tilemap at `$6000` (`row * 32 + (x & 31) + (x >= 32 ? $400 : 0)`), word = the metatile's at `$7E:B000`. After a
fill all 957 window chips match (c7363, 01C; f94-f200, scene 236).

**Loc 01C, the Dragon Tank's bridge** -- `saves/field_re/tank/tele01c.state`, `tools/dragon_tank_cmp.py`'s drive. At the
fight's return obj0 fn1 runs `E4 07 0C 0B 0D 07 09 41`: BG1 cells 7-11 x 9-10 (the railing, ids `86 85 86 85 86` /
`96 95 96 95 96`) become 0 in WRAM at c7363 (`--monitor 7E3907:5`). From c7364 the VRAM still holds the railing's words
(`8100 C100 / 393D 793A 3939 393A / 394D 794A ...`) at 35 of the 40 chips (5 words coincide with the empty cell's) --
through c7600, after the camera's pan of eight chips left (`$1D0A` 0A -> 02, c7528-c7556): the cells stay inside the
window. The picture: the railing in front of the wreck's lower piece for the rest of the scene.

**Scene 236, the lever** -- `saves/field_re/s236_lever.state`, `0:10,100:4,0:100,100:4,0:130`: obj9's
`E4 08 21 0A 22 08 15 3B` writes BG1 rows $15-$16; the window is rows 56-84 (cells 28-42), so no chip is held and VRAM =
WRAM at every captured frame (f94-f200).

**Loc 002's curtain** -- the E4s are obj0 slot 1's (C0/56C8, C0/0206), before the fill: the fill shows them.

## 4. The port

- `scene.c`: `SceneData.held[3]`, per layer (2w) x (2h) chips, the metatile a held chip shows (0xFFFF = the layer's own
  cell). `draw_layer` reads it per corner. The layers (`l1t`..) follow the WRAM map.
- `field.c fieldTileCopyDrain` (after each tick): op 0xE4 = hold the destination's chips inside the window
  (`fieldPicWindow`: `$1D0A/$1D0E/$1D0C/$1D10`, `$1D12..` in the half-camera mode, every chip of a map-built layer), then
  re-read the rect from WRAM (was: `CtFieldSceneCopyTiles` inside the render's copy, which also clipped rows the layers
  lack); the `$30C0` prio copy (mask bit 3) unchanged. Op 0xE5's showing frame = release the style's layers and re-read
  them whole (was: the destination rect only; C0/75E9 / 78EC / 7CB5 rebuild the whole window).
- `fieldPicStream` (before the drain, each frame): release every held chip outside the window -- the line that brings it
  back is built from the WRAM map. The window is read from the field page, `$7F:2000`'s copy under a fight.
- `fieldEntryTilemapFill` (C0/0A50): drain the pending copies without holding, release all, the layers from WRAM.
- The door handlers (`CtWramDoorOpen`) and the chest (`CtFieldSceneChestOpen`) release their block.
- A seed or a restored snapshot (`fieldMapFromWram`) releases all.

## 5. Boundaries

- The ROM builds ONE line per direction per frame (the flags are TSB'd, the builders read the final `$1D0A..`): a camera
  that crosses two chips in one frame leaves a stale line on the cartridge. The port releases every chip outside the
  window. No measured case.
- The mods (the widescreen) draw columns outside the ROM's window; there a released chip shows the WRAM map.
- An F5 snapshot does not carry the held chips: F9 rebuilds the picture from WRAM (as before).
- The battle-return's slot-1 copies (C0/0330) hold like a live object's (the fight leaves the tilemaps); the headless
  clock runs the WRAM half of that tail a frame later (field.c fieldResumeRebuildRun), unchanged.
