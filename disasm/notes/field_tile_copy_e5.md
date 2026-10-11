# Op 0xE5, the queued tile copy, and the `$0117` bit-5 service C0/1770

Derived 2026-09-26 (ROM + the logical oracle, `CT_ORACLE_LOGICAL=1`). Ported in `src/ct_vm.c` (the `xfer` machine at the
top of `CtSceneTick`), `src/move.c` (`CtVmTileStageE5`, `CtVmTileCopyAF4E`), `src/field.c` (the host's legs, the
battle consumer's bit 0, the picture's upload) and `src/scene.c` (`CtFieldSceneMapFromWramRect`). Graded by
`tools/e5_copy_cmp.py` (check `field-e5-copy-service-vs-logical-oracle`).

It retires three stand-ins: `copyFreeze = 8` / `copyLatency = 1` with the issuer re-timed to `dc = 17 - 8`
(memory ct-field-copy-freeze, fitted to the REAL-TIME cartridge's lag frames), the render copy deferred 16 frames, and
the fade held while any render copy was queued (`tcN == 0`, 8b3b845).

## 1. The op: C0/3DC5

```
3DC5 TYX / INX / REP #20
3DC9 LDA $7F2001,X / STA $3E   INX INX      $013E = src left/top     (16-bit)
3DD1 LDA $7F2001,X / STA $40   INX INX      $0140 = src right/bottom (16-bit)
3DD9 LDA $7F2001,X / STA $42   INX INX      $0142 = dst left/top     (16-bit)
3DE1 SEP #20 / LDA $7F2001,X / STA $44      $0144 = the plane mask
3DE9 AND #07 / STA $45                      $0145 = mask & 7 (the ONLY writer of $0145 in the ROM)
3DED LDA #20 / TSB $17                      $0117 bit 5
3DF1 INX / CLC / RTS                        eight bytes; CLC ends the object's visit (C0/5AB4 BCC)
```

No copy. Op 0xE4 (C0/3D97) is the same staging without `$0145`/`$0117` and with `3DBD JSR C0/AF4E` inline.

## 2. The service: C0/0C76 -> C0/16DC -> C0/1770

The field main loop:

```
0097 LDA $00F0 / TSB $51     00A1 JSR 18D9     00A4 JSR 0C76     00A7 JSR 881E     00AA JSR 1AAC
00AD JSL C0/1F87             00B1 JSR 21E1     00B4 JSR 274D     00B7 JSR 00BF     00BA JSR EC60 / BRA 0097
```

C0/0C76 runs the fades (`$18` bit 1 -> 1F24, bit 3 -> 1F5A) and then dispatches `$0117`: bit 7 first (0C8F, the
location change's fade counter; returns), then bit 6 (0CC7, the menu), bit 4 (0D3E, a door; its handlers end in
`BRL 16DC`), bit 1 (16DC, a chest; falls into 1770), then:

```
1770 LDA $17 / BIT #20 / BNE 1779 / BRL 188C          bit 5 clear -> bit 0 (C0/188C, the battle)
1779 LDA #20 / TRB $17 / LDA #01 / STA $38
1781 JSR 885A                                          the camera's align step (state $0138 1 -> 2 -> 0)
1784 JSR 00BF                                          a WHOLE event frame: 59D9, 5A46, FD/C2C1, FD/FFF7, A810, CA76, B271
1787 LDA $38 / BEQ 1790 / JSR EC60 / BRA 1781          one frame per step until the camera is 16-aligned
1790 JSR AF4E                                          THE COPY, on $013E-$0144 as they stand
1793 JSL FDFFF7                                        (a second objslot step this frame)
1797 JSR EC60
179A LDA $45 -> the style branch
```

The style branch (C0/179A-188B) rebuilds the tilemaps of the layers the mask touched. Every frame of it ends in
C0/EC60 and runs only C0/00DE (`JSL FD/C2C1 / JSL FD/FFF7`) besides its builders; it returns (RTS, or `BRL EC60` and
that RTS) into C0/00A7, so the frame after it runs 881E .. 00BF without C0/0097, C0/18D9 or C0/0C76.

| `$0145` | frames | its work (each line = one frame, ending in EC60) | the upload the picture shows |
|---|---|---|---|
| 1 | 2 | 17A9 75E9 (BG1) 74D4 00DE `$46=1` STZ $45 / 17BC 00DE 87F1 | the 1st |
| 2 | 2 | 17D2 78EC (BG2) 74E8 00DE `$46=2` STZ $45 / 17E5 00DE 87F1 | the 1st |
| 3 | 4 | 17FE 75E9 00DE `$46=3` STZ $45 / 1817 78EC, swap $7C<->$82, 74D4 74E8 00DE `$46=2` / 183E 75E9, swap, 74D4 00DE `$46=1` / 1859 00DE 87F1 | the 2nd |
| 4 | 2 | 186F 7CB5 (BG3) 74F7 00DE `$46=4` STZ $45 / 1882 00DE 87F1 | the 1st |
| 0, 5-7 | 0 | 1862 `CMP #04 / BNE 188B` RTS | no upload |

The NMI (C0/EAD1) uploads the built map to `$1D7C` (1) / `$1D7E` (2) / `$1D82` (3) / `$1D80` (4) through C0/7F62,
`STZ $46`, and for 1 and 2 writes `$1D7D | 1` into the BG1SC HDMA byte `$7F:14F0`.

**That NMI does nothing else.** C0/EAD1 is `LDA $46 / BNE ->EAD8`; each tilemap arm (`CMP #01`..`#04`, and `BIT #08` ->
C0/7F5D, `BIT #10` -> C0/7F58) ends `JSL FD:C1EE / STZ $52 / BRL EC15`, past C0/EB79-EC13: no text line (`$36` ->
C0/6ECB), no CGRAM (C0/712E: `$2121 = 0`, channel 7 from `$7E:2200`, `$200` bytes), no OAM (C0/ECA3), no VRAM queue
(C0/B1B2, `$09A0`'s clear), no edge columns (`$1D78-$1D7B` -> C0/8445..87BF). `BIT #20` (C0/EC77) and any other value
fall into EB79. So a style's frames that set `$46` (3 for style 3, 1 for styles 1/2/4) leave CGRAM as the frame before
left it, while C0/00DE's FD:FFF7 keeps stepping the page. In the port: ct_vm.c `CtFieldNmiTilemapArm` (the branch),
field.c `g_fieldCgram` (what C0/712E last uploaded -- the BG bake, the sprites' OBJ rows and the wipe's backdrop read
it), and the same predicate gates the port's B1B2 flush and text-line upload. Style 3 builds BG1 into the back
buffer `$1D82` (invisible), swaps it to the front with BG2's upload (the change), then rebuilds the other buffer and
swaps back. C0/74D4 / 74E8 / 74F7 zero a layer's tilemap origin (`$1D99` / `$1D9B` / `$1D9D`) and its HDMA step
counters (`$1D87,$1D89` / `$1D8B,$1D8D` / `$1D8F,$1D91`, sixteen-bit) and call C0/7F9A (edge-upload addresses
`$1D9F-$1DAB`). The rebuild is at the live camera, which C0/1781's loop has just 16-aligned.

Census (`ctlib.script` over every script, linear decode): E5 masks `04 0B 30 3B 3C 41 51 71 79 82 B2 BA` -- styles
0 (1 site), 1 (108), 2 (120), 3 (194), 4 (9); none 5-7. Two slot-1 scripts carry an E5 (scenes 268, 422): the load's
`STZ $17` (C0/0B96) precedes C0/56C8, so their bit 5 survives the fade-in ramp (C0/2848 calls 00BF, not 0C76) and is
served by the first main-loop frame.

Op 0xD8 sets bits 5 and 0: its battle waits behind this same service (style 0 unless an E5 left `$0145`), and its
C0/1790 re-runs AF4E on whatever `$013E-$0144` hold.

## 3. Measured (logical oracle)

**Loc 002, the curtain, closing** -- `saves/field_re/loc002_curtain_ctl.state`, `--btnscript "0:4,10:40,0:10,100:2,0:60"`
(walk up, A at the curtain; obj10's fn1 `E5 13 12 1B 1C 13 02 3C / E5 13 12 1B 1C 13 02 3B`), `--monitor` per frame:

| f | `$0117` | `$013E..$0145` | `$0146` | obj10 `$1015` | the map |
|---|---|---|---|---|---|
| 73 | 20 | `13121B1C13023C04` | 00 | 09 (its visit) | |
| 74 | 00 | | 00 | 08 | `$7E:3293..` = 0 (plane $3080, style 4's copy) |
| 75 | 00 | `..3C00` | 04 | 08 held | |
| 76 | 00 | | 00 | 08 held | |
| 77 | | | | 07 | |
| 84 | 20 | `13121B1C13023B03` | | 09 | |
| 85 | 00 | | | 08 | `$7E:3253..` = `3D3E3F` (the closed curtain) |
| 86-89 | | `..3B00` | 03 02 01 00 | 08 held | |
| 90 | | | | 07 | |
| 97 | | | | 09 (obj10 back at its idle loop, IP 0761) | |

Pictures (`PEEK_THUMBS`, 1 per frame): the BG3 light leaves at thumbnail f77 (287 thumbnail pixels), the curtain
closes at f89 (1941) -- the copy frame N plus 1 (styles 1/2/4) or 2 (style 3), plus the P+2 of the thumbnails
(memory ct-frame-alignment-p2).

**The new game's opening** -- `start_scene.state`, `--btnscript` `0:1540` then `100:2,0:10` x 65 (the text mashed),
`PEEK_SAVE` at f2320 = `saves/field_re/loc002_intro_curtain.state`: from it, E5 `33 32 3B 3C 13 02 3B` at f19 (copy
f20, held f21-f24), E5 `..3C` at f32 (copy f33, held f34-f35).

**Scene 236, the lever (loc $0EC)** -- `saves/field_re/s236_lever.state` (the oracle's own `--teleport 0xEC 24 36 0`
from truce_market), `--btnscript 0:10,100:4,0:100,100:4,0:130`: obj9's fn1 runs four E5 of style 3 (mask `3B`), copied
f16 f56 f120 f160. `--hookpc` per frame: C0/7F62 once on f17, f18, f19 (and f57-59, f121-123, f161-163); C0/EB79,
C0/712E, C0/ECA3 and C0/B1B2 on none of those frames and once on every other. The room's lights are CGRAM 55-59
(`3168 4A0B 566D 7FB5 7FFF`); an FD/E39C record flips them on the page to `2D47 45EA 524C 7B94 77BD` every 2 frames
(`$7E:226E`, both sides frame for frame). The page reads dim at f18/f19, the cartridge's CGRAM (`--peek` sidecar) stays
bright at peek f20/f21 and turns dim again at f24/f25 (page f22/f23). Before the port modelled the branch it baked the
page every frame and its P18-19, P57-58, P122-123 and P161-163 showed the dim lights against the cartridge's P+2 (265
thumbnail px each, rows y72-140). Check `field-nmi-tilemap-holds-cgram-vs-logical-oracle` (tools/nmi_cgram_cmp.py).

Neither curtain hold is a lag-frame count: the real-time cartridge holds the same copies 7 and 3 frames (the old measurement behind
`copyFreeze`); the builders overrun the NMI. By the CLAUDE.md exception the port grades against the logical frames.

## 4. The port

- `step()` op 0xE5 = `CtVmTileStageE5` + yield. Op 0xE4 unchanged (inline AF4E + the host's copy).
- `CtSceneXferFrame` = the kind of the coming frame: NORMAL, ENTER (bit 5 pending, bits 7/6 clear: the dispatch's
  frame), ALIGN (inside 1781-178E, also a seed with `$0138` live), STYLE, RESUME (the frame after the branch).
- `CtSceneTick`: `head` legs (the `$51` latch, 0C76's fades and bit-7 drain) on NORMAL/ENTER; `body` legs (the box
  machine, the wipe) on NORMAL/RESUME; ENTER does 1779-177F before the camera tick (which then runs 885A); STYLE runs
  `ctXferStyleFrame` after the NMI flush and returns; after the VM pass `ctXferAfterVm` is C0/1787-1797.
- `field.c`: the host's own C0/00A7-00B4 legs (player controller + exits, A handler, typewriter) run on NORMAL/RESUME,
  the decision cursor (C0/1927 in 18D9) on NORMAL/ENTER. `fieldBattleConsumer` keeps C0/188C only: bit 0 when the NEXT
  frame is NORMAL and bits 7/6 are clear (it runs at a frame's end).
- The host's copies: the collision planes (kept outside WRAM, `fieldCollCopyTiles`) at C0/1790's frame (`CT_TC_COLL`),
  the picture at the style's showing frame (`CT_TC_RENDER`), re-read from the WRAM map over the destination rect
  (`CtFieldSceneMapFromWramRect`) -- the render's own layers do not have the rows a location parks rects in (loc 002's
  `$12-$1C` / `$32-$3C`), and its old in-layer copy clipped them: the curtain never moved on screen before this.
- The layer origins: after 74D4/74E8/74F7 the port's BG2/BG3 offsets use `bgD2*/bgD3*` (a rebuilt layer's camera
  minus BG1's), zeroed at the entry camera.

Tracing: `CT_XFERTRACE=1` prints `xfer: t<tick> C0/1790 AF4E <rect> mask <m> style <s>` (VM) and
`tilecopy: f<N> ... planes|picture` (ct_field's drain).

## 5. Boundaries

- The builders' WRAM (C0/75E9/78EC/7CB5 into `$7E:C800..`, C0/87F1's edge buffers, C0/7F9A's `$1D9F-$1DAB`) is not
  modelled; the port composes its own picture.
- Of what the tilemap arm skips, the port models the CGRAM upload, the B1B2 flush and the text line. It has no OAM
  image (its sprites are drawn from the actors, which no style frame moves) and no edge-column upload.
- `$46` values other than the style branch's (bits 3/4/5: C0/7F5D, 7F58, EC77) are decoded by `CtFieldNmiTilemapArm` but
  no port code writes them; their writers are not traced.
- An ALIGN seed is recognised by `$0138`; a seed taken inside the style branch is not (no byte carries it).
- The init pass (`runFunc`, the fn0s) still has no E4/E5 arm.
- Op 0xE4 in the live VM ENDS the visit (fixed after this dossier, check `field-e4-visit-end-vs-logical-oracle`).
  C0/3D97 exits `3DC0 LDX $C7 / INX / CLC / RTS`, and C0/5AB4 `BCC ->5ABA` ends the visit on CLC. The port's arm returned
  "continue", so the op after an E4 ran in the same visit. The carry list at the end of `step()` already named E4, but
  that arm returned first. MEASURED on the logical oracle, scene 236's lever (`saves/field_re/s236_lever.state`, obj9's
  fn1, cadence 4): the E4 at f95 leaves `$1192 = 05EA` with `$7F:00EC` and `$011F` unchanged, and the next visit at f99
  runs `65 01 EC` / `E3 01` / `00`. On the second arm the E4 runs at f199 and the ops after it at f203. The slot-1 runner
  C0/595C ignores the carry (`CtSceneRunObj0Slot1`), so obj0's slot-1 E4 chains were never affected. The ROM has 15 E4
  sites in live scripts (scenes 4, 21, 131, 230, 231, 236, 256, 419-423). The two in init-pass fn0s (scenes 141 and 198)
  fall under the `runFunc` boundary above. Graded by `tools/e4_visit_cmp.py`.
- `saves/field_re/loc002_intro_curtain.lakewram` seeded a port picture that was black -- WRAM equal to the cartridge's
  every frame, `sc->bright` = 0. The create-time prescan runs every fn0 before the seed replaces WRAM, and loc 002's at
  storyline 0 runs the opening's `F0 00`; `CtSceneSeedFromWram` rebuilt `$011F`/`$0162`'s views but not `sc->bright`, the
  C view of `$0119` that the NMI writes to INIDISP (C0/EC4E `LDA $0119 / BNE ->EC55 / LDA #80 / STA $2100`). It does
  now (2026-09-26), and the intro drive grades its picture too: port P = cartridge P+2 on f0-f57, the two copies at
  P22/P34 and the message box's first ramp frame at P57 (see textbox.md, "the band is this iteration's `$0127`").

## 6. Open questions

- Styles 1 and 2 are derived from the disassembly (their upload shows on the 1st frame); no fixture exercises them.
- Scene 236's lever (style 3, mask `3B`) shows a PICTURE residual during the style hold that loc 002 does not show. The
  WRAM matches on every frame (`e4_visit_cmp.py`). The port's P against the cartridge's P+2 differs on 2 frames after
  each E5 copy (P18-19, P57-58, P122-123, P161-163), by 265 thumbnail px in the band y36-70 (screen y72-140) across
  the width: the room's panels. Frames around the E4 copies are identical. Not derived yet. The panel band points at
  a BG animation machine (FD/E022 or FD/E39C) that runs, or pauses, differently during C0/179A's hold. That is a
  HYPOTHESIS. Repro: CT_SHOTS_THUMBS on `ct_field --scene 236 --loadwram saves/field_re/s236_lever.lakewram --walkscript
  n:11,a:4,n:100,a:4,n:130` against PEEK_THUMBS on the oracle with `--btnscript 0:10,100:4,0:100,100:4,0:130`.
