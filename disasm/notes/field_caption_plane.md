# The caption plane -- the endings' staff roll on BG3 through WRAM

2026-10-08. The VM side (ops 0x29 0x2A 0x2B 0x2C 0x2F 0x32, move.c `CtVmCaptionFamily`) came with the op-gap census
(live_vm_op_gaps.md). This dossier is their CONSUMERS: the main loop's C0/274D, the field NMI's `$46` arms, the load's
BG3 register bytes, and what the PPU draws. All addresses are bank C0 unless written otherwise; the field runs with
D = $0100, so DP `$54` = `$0154`, `$46` = `$0146`, `$D9` = `$01D9`.

## 1. The producers (ops, already ported)

| op | handler | effect |
|---|---|---|
| 0x29 xx | 3E84 | `$1D35`/`$1D36` \|= 04, `$1D39`/`$1D3A` = 0; C0/6E27: font `$FD:0000` -> `$7F:5080` (C3/0557), DMA `$0400` bytes to VRAM `$5000` at once (C0/2DC8); C0/75A0; `$0BE9` = 1 (xx bit 7 clear) / 2 (set); `$0BE1` = the start of record xx & 7F of `$FD:A000` |
| 0x2A / 0x2B / 0x32 | 3ED6 / 3EDE / 3EE2 | `TSB $54` with 04 / 08 / 10, CLC (the visit ends) |
| 0x2C a b | 3EE6 | `$1D3F`/`$1D40`/`$1D92` = 0, `$1D8F` = a*8, `$1D91` = b*8 (BG3's step counters) |
| 0x2F c r | 3F12 | `$0BE3` = c (column), `$0BE4` = r (row) |

`$FD:A000` is ASCII, records ended by a byte with bit 7 set, lines by `/`: record 0 `PRODUCER//KAZUHIKO AOKI`, 1
`DIRECTOR//TAKASHI TOKITA/...`, ... 37 `THE END`, 38 `B.C.650000000` (dis: `python` walk of the ROM, sec. 7).

## 2. C0/274D -- the main loop's C0/00B4 leg

```
274D LDA $54 / BIT #1C / BNE 277E          ; none of 2/3/4:
2753 BIT #20 / BNE 2758 / RTS              ;   bit 5 -> 2758 (sec. 8)
277E BIT #10 / BNE 278F                    ; a line first: the clears wait for a frame without one
2782 AND #0C / ASL / TSB $46               ; bit 2 -> $46 bit 3, bit 3 -> $46 bit 4
2787 JSR C0/75A0 / LDA #0C / TRB $54 / RTS
278F REP #20 / PHB / LDX #0000 / LDY #D800 / LDA #001F / MVN $7E,$D2     ; $D2:0000 x $20 -> $7E:D800
279E LDX #0000 / LDA #001F / MVN $7E,$D2 / PLB / SEP #20               ; again, Y running on: D800-D83F zeroed
27AA LDX #D800 / STX $2181 / LDA #00 / STA $2183                      ; WMADD = $7E:D800 (hardware registers)
27B5 LDX $0BE1
27B8 LDA $FDA000,X / BMI 27E0 / CMP #2F / BEQ 27EB
27C2 CMP #2E -> #1B ; CMP #20 -> #00 ; else SEC / SBC #40
27D5 STA $2180 / LDA #2C / STA $2180 / INX / BRA 27B8                 ; tile, attribute 2C (palette 3, priority 1)
27E0 LDA #10 / TRB $54 / JSR 27EB / INC $0BE4 / RTS                   ; the record's end
27EB INX / STX $0BE1 / LDA $0BE3 / REP #20 / AND #001F / STA $D9
27F9 LDA $0BE4 / AND #003F / ASL x5 / CLC / ADC $D9 / CLC / ADC #7000 / STA $0BE5    ; VRAM word
280E LDA #0040 / SEC / SBC $D9 / SEC / SBC $D9 / STA $0BE7                 ; bytes from the column to the row's end
281A SEP #20 / INC $0BE4 / LDA #20 / TSB $46 / RTS
```

`/` returns through 27EB's RTS with bit 4 still set, so the next line is written on the next frame: one line a frame.
`$D2:0000-001F` is thirty-two zero bytes.

C0/75A0: `MVN $7E,$D2` of `$20` from `$D2:0000` into `$7E:C800`, then six `MVN $7E,$7E` doubling it (`$1F $3F $7F $FF
$1FF $3FF`): `$C800-$CFFF` zero.

## 3. The field NMI's arms (C0/EAD1)

After `CMP #01..#04` (op 0xE5's styles):
```
EB46 BIT #08 / BEQ / JSR C0/7F5D / LDA #08 / TRB $46 / JSL FD:C1EE / STZ $52 / BRL EC15
EB5A BIT #10 / BEQ / JSR C0/7F58 / LDA #10 / TRB $46 / JSL FD:C1EE / STZ $52 / BRL EC15
EB6E BIT #20 / BEQ / JSR C0/EC77 / LDA #20 / TRB $46            ; then into EB79 (the NMI's ordinary work)
7F58 LDX #7400 / BRA 7F60 ; 7F5D LDX #7000 ; 7F60 STX $48 ; 7F62 $4E = $0800, $4A = 01, $4B = $C800/$7E, BRL 2DC8
EC77 $2116 = $0BE5, $2115 = 80, DMA 7: $4370 = 01, $4371 = 18, A1 = $7E:D800, DAS = $0BE7, $420B = 80
```
One arm an NMI; 08 and 10 skip C0/EB79-EC13 (no CGRAM, OAM, text line, VRAM queue -- ct_vm.c `CtFieldNmiTilemapArm`).

## 4. The load and the PPU

C0/0A50 (the entry tilemap fill) `0A73-0A93`: `$1D7C/$1D7E/$1D80/$1D82` = `$6000/$6800/$7000/$7800`, `$1D84/$1D85/$1D86`
= 01 (64x32); BG3 is built from the map only when `$0BCA & 1` and `$01BB == 0` (0ACC-0AD6), one half (7F62) or both
(7F77, `$1D35` bit 2); `0AF6 LDA $0BE9 / BEQ / STA $1D86` -- op 0x29's size (1 = 64x32, 2 = 32x64). C0/0A50 runs after
fn0 and obj0's slot 1 (C0/286C's second JSR). FD/C0D7 (`0A5D JSL`) sets `$2105` = 09 (mode 1, BG3 priority), `$210B`
= 11, `$210C` = 05 (BG3 characters at `$5000`). FD/C124 (C0/0B28, C0/286C's tail, no script in between) writes the
HDMA records: `C13F-C147 $7F:14F2 = $1D81 | $1D86` (channel 0's BG3SC) and `C1AD-C1B8 $7F:1520 = $0BD7`, `$1521 =
$0BD8` (channel 4's TM / TS).

## 5. Measured (the rule's cartridge, CT_ORACLE_LOGICAL=1)

Captures: `--teleport 91 8 8 0 --from saves/field_re/s19_power_tab.state --frames 200` (scene 05B) ->
build/_caption/s5b_tp.state; + 2280 frames -> saves/field_re/s5b_roll_f2280.state (tools/caption_plane_cmp.py makes it,
byte-identical).

- 05B from the teleport (`--monitor 0154,0146,0BE1 --writer`): `$0154` 04 at f103 (C0:3EDA), 00 + `$46` 08 at f104
  (C0:278E / 2787), `$46` 00 at f105 (C0:EB51); 08 / 10 at f107-109 (EB65); line request 10 at f123; `$0BE1` 09 / 0A
  / 18 at f124-126 (record 0: `PRODUCER`, the empty line, `KAZUHIKO AOKI`), `$0BE4` 0E 0F 10 12; `$46` 20 (C0:2823),
  `$0154` 00 at f126 (C0:27E4), `$46` 00 at f127 (C0:EB79).
- `--ppuregs` f200: mode 1, BG3 map `$7000` 32x64 (`higher=1`), chars `$5000`, TM 14 (`--hdma`: TM=14, bg3prio=1).
  VRAM row 0E from column 0B: `2C10 2C12 2C0F 2C04 2C15 2C03 2C05 2C12` (PRODUCER); a space is `2C00`.
- 05C's load (from s5b_roll_f2280): `$0100` = 5C at f30 (C0:3B89), `$0BD7` 10 (C0:A459) then 14 at f44 (C0:67E0 --
  slot 1 `4A D7 0B 7E 14`), `$7F:1520` = 14 at f47 (FD:C1B1). From s5b_tp: `$1D35` 0404 (C0:3E8D) f2311, `$0BE9` = 2
  f2312, `$1D86` = 01 (C0:0A96) f2324, = 02 (C0:0AFE) f2326. `$0154` = 0C -> `$46` = 18 at f64, 10 at f65 (EB51), 00
  at f66 (EB65): one arm an NMI.

## 6. The port

- move.c `CtVmCaption274D` (the leg, bits 2/3/4 + 27EB), `CtVmCaptionNmi` (EB46-EB77), `vmFill75A0` (shared with op
  0x29), `g_ctVramSink` (a DMA into the host's VRAM image; NULL for the sweeps and `--vm`; muted during
  `CtVmResolveActors`, a probe on its own copy of memory). Op 0x29 now sends its font DMA through it.
- ct_vm.c `CtSceneTick`: the NMI arm at the frame boundary beside the style's `STZ $46`; the 274D leg after C0/21E1.
- field.c: `fieldVramSink` (into `g_fieldVram`; an upload over BG3's tilemap `$1D80..+$800` sets `g_bg3Plane`),
  `fieldEntryTilemapFill` (0A73-0A93, 0AF6, FD/C124's `$14F2` / `$1520` / `$1521`; a BG3 build from the map clears
  the plane), the NMI block (style 4 clears it; `CtFieldSceneSetVramLayers` leaves BG3 out of the bake while it
  holds), and `fieldBg3PlaneRow` -- BG3 from the VRAM image at the bake's BG3 scroll (camx + o3x), priority-0 cells
  where the bake left the backdrop, priority-1 cells after the sprites (mode 1 + BG3 priority), gated on TM
  `$7F:1520` bit 2.
- scene.c `CtFieldSceneSetVramLayers`, `CtFieldSceneScreenLayers`.

tools/caption_plane_cmp.py (check `field-caption-plane-staff-roll-vs-rule-cartridge`): 05C 461/461 frames identical
(204 with captions), 05D 789/789 (312) (port f560-f1348, to its exit; sec. 9) -- at d = +2 / +7 until 2026-10-09, at d = 0
under the rule's RDNMI (sec. 7). Mutations: no 274D leg, palette 2, the plane never
flagged -- all killed.

## 7. Open

- **The E1 exit's offset (CPU-timing class).** The roll's exits are `E1` (C0/3B4B): `3B71 LDA $4210 / BPL 3B71` spins on
  the NMI flag, which the field NMI's own `$4210` read races, then C0/0B4E (four stores) and `$0100` at 3B89. The rule's
  cartridge spends 6 frames there leaving 05C (obj1's E1 visit at f549, `$0100` = 5D at f555, `$1002` frozen 0404
  f549-555) and 2 more than the port leaving 05B; the port reboots on the next vblank. So 05C runs 2 frames and 05D 7
  frames ahead of the cartridge. Not a caption question. 2026-10-08, the user delegated the ruling; CLOSED 2026-10-09: the
  rule decides the race for the poll (the vblank that rises while the spin waits is seen; the default, `CT_ORACLE_RDNMI=0`
  measures without), the store lands one frame after the visit on both sides (05B f27 -> f28, 05C f547 -> f548) and both
  scenes grade at d = 0 (oracle_rdnmi_rule.md sec. 2; its first version, the store in the visit's own frame, read -1 / -2).
- **05D's missing NPC** -- closed: sprite 186 was not in the dat (sec. 9).
- **C0/2758 (`$54` bit 5)** -- ported (sec. 8).
- **Not exercised by a capture:** the 64x32 size (`$0BE9` = 1, operand bit 7 clear: scenes 002, 00C, 024 ...), rows
  >= 32, a scrolled BG3 (`2C a b`, `E6` -- scene 002's obj17 fn3 alternates `E6 10 / E6 00`), BG3 on the sub screen or
  under colour math (the port prints a one-time note and draws it plain), the plane in scene 002 over a location's own
  BG3 (its fn0 `29 02` without `2A/2B`: the lines land on the map's tilemap).
- **One flag for two halves.** A map build through C0/7F62 rewrites only `$7000`; the port drops the plane for both.
- **A seed has no VRAM.** A state taken mid-roll seeds the port without the plane (the first frames of the check are the
  seed's); the check starts after the port's own load.

## 8. C0/2758 -- a chest's box closed by the camera (`$54` bit 5)

2026-10-08. Reached only when none of `$54` bits 2/3/4 is set (274F `BIT #1C` first). M8 (the main loop's `SEP #20`):
```
2758 LDA $1D0A / LSR / CMP $5D / BCS 2779     ; the camera's left column >= the chest's column -> close
2760 LDA $1D0C / LSR / CMP $5D / BCC 2779     ; its right column < the chest's                -> close
2768 LDA $1D0E / LSR / CMP $5E / BCS 2779     ; top row >= the chest's row                    -> close
2770 LDA $1D10 / LSR / CMP $5E / BCC 2779     ; bottom row < the chest's                      -> close
2778 RTS
2779 LDA #0D / STA $29 / RTS                  ; the box machine's CLOSE state: C0/2032 from the next frame
```
The point is closed on the edge: a chest in the camera's first column or row counts as outside (`BCS`).

- **The only setter of bit 5** in bank C0 is the treasure probe's C0/1F1C `LDA #20 / TSB $54` (byte scan `A9 20 04 54`:
  one hit); its clearers are C0/2051 (the close ramp's end, 204D) and op 0xBB's family C0/35A0 / 35E7 / 362E / 36A3
  (`A9 20 14 54`). **The only writer of `$5D`** is C0/1E52 `LDX $DB / STX $5D` (`86 5D`: one hit; the oracle's
  `--writer` names C0:1E54), the chest's tile `(ty << 8) | tx`, written only when the chest was closed (an empty chest
  takes 1E44 -> 1E8D and leaves `$5D` alone -- and still sets bit 5 at 1F1C, so the arm then tests the PREVIOUS chest's
  tile).
- The chest's box does not hold the lead: he walks with it on screen (state 7), and it goes when the chest leaves the view.

MEASURED, the rule's cartridge: `saves/field_re/s49_chest_walk.state` (`--teleport 49 12 38 0 --from
saves/field_re/s19_power_tab.state --frames 120`: loc 031, the tile below the chest (12,37), facing it), drive A f10,
Down f34, Right f134, Down f534, `--monitor 0129:1,0154:1,0127:1,015D:2,1D0A:8 --diff --writer`:

| f | event |
|---|---|
| 11 | `$015D` = 0C25 (C0:1E54), `$0154` = 20 (C0:1F20), `$0129` = 02 |
| 25 / 51 | `$0129` = 06 (C0:2049) / 07 (C0:21DC) -- the lead walks from f34 with the box up |
| 280 | `$1D0E` = 4A (C0:94C3): the top row 25 = the chest's; `$0129` = 0D (C0:277D) the same frame |
| 281-290 | `$0127` 24 .. 00 (C0:2044), `$0129` = 0D rewritten every frame by the arm |
| 291 | `$0129` = 00 (C0:204F), `$0154` = 00 (C0:2055) |

(A first try in loc 01A, the chest (30,37) of field-chest-opens-item, could not reach the arm: the room's camera never
leaves the chest.)

The port: `$015D/$015E` lived in field.c's collision RAM (`g_collRam`), not in the scene's WRAM page the main loop's legs
run on, and the arm did not exist -- the box stood in state 7 until A. Now the probe writes them into the scene page
(C0/16DC's metatile bump reads them there), and move.c `CtVmCaption274D` runs the arm in the C0/00B4 slot, after the box
machine (C0/00AD) and the camera, so its `$29 = 0D` is ramped from the next frame by the same machine as an A press.
Port: 0D at f281, 00 at f292 -- the cartridge's frames + 1, the same +1 as every earlier edge of the box (02 / 05 / 06
at f12 / 15 / 26), the key alignment (memory ct-frame-alignment-p2).

Check `field-chest-box-closed-by-camera-vs-rule-cartridge` (tools/chest_box_close_cmp.py): the two `$0129` timelines and
the whole picture f1-320 at P + 2, residuals pinned. Mutations: the store dropped, the 2753 branch dropped, the tile back
in the collision RAM -- all killed.

Residuals of the same run, not this arm (OPEN):
- the chest box's typewriter: state 7 at port f55 against the cartridge's f51 + 1 (3 frames late) and the open ramp's
  picture f12-24 -- the cartridge draws this box with C2/0003, the port with the field's textbox;
- the close ramp's picture f281-288: the cartridge squeezes the text with the window, the port clips it (the same
  residual the power-tab check pins on an A-press close, f166-169);
- f203-244: the camera X one pixel apart while the lead stands against a wall (the cartridge's `$1D93` holds 02 from
  f203 to f245); from f246 Marle's follower sprite differs (~286 px).

## 9. 05D's NPCs -- sprite 186 was not in the dat (closed 2026-10-08)

Up to 2026-10-08 the check graded 05D only to port f905: from f906 the port lacked one NPC the cartridge draws, 404 px
at x 80-95 / y 79-110, every frame after. It is obj6. Scene 05D (script 151) loads every one of its ten NPCs hidden at
init -- obj6 fn0 `82 B3 / 8B 05 06 / 91` -- and obj1's fn1 shows them in groups with its captions (`7C 04..0A`, then
`2F 0F 0C / 32 / 7C 0C / 7C 0E` for obj6 and obj7, ...).

- The cartridge at c919 (port f910 + 2 + 7): obj6 `$110D` = B3, active like obj7; OAM 32-35 at (88,79) (72,79) (88,95)
  (72,95), tiles 88 8A 8C 8E, attribute 21 -- obj6's 16x32 at its foot (5,6) = (88,111).
- The port: `CT_DRAWDBG` draws obj6 every frame from f896 (`spr=186 af=1 at(88,111)`), but `CT_SPRITEPROBE=186,<f>`
  reads `blob=0 ok=-1` for every frame: the asset had no sprite 186, so the compositor drew nothing.

The cause is the extractor, not the engine. assets/extract_assets.py `build` gathers the sprites to pack from each
scene's visible init actors, then sweeps that scene's script for the loads `80/81 xx` (xx), `82 xx` (xx + 7), `83 xx`
(xx + 256) -- but it skipped the sweep for a scene with no visible init actor (`if not scene_sprites: continue`). 05D has
none, and 186 (B3 + 7) is used by no other scene. With the early-out removed, every scene is swept: the dat gains
sprites 45, 99, 186, 373, 399, 432, 433, 434, 455, 463, 468, 505, 506 (+ the animation records of the >= 256 ones), and
14 already-packed sprites gain the static frames (`AC xx`) of the scenes that load them hidden; no entry is removed and
no other byte changes. ct_assets.dat sha256 ad1e2e... -> 10fb32e5....

Measured after (tools/caption_plane_cmp.py at d = +7): port f880-f1400 all identical (521/521); the graded 05D segment
f560-f1348 789/789. obj6's box (x 80-95, y 79-110) is black on both sides through f905 and lit from f906, though
`CT_DRAWDBG` lists obj6 from f896 (obj7 from f900); what keeps it dark those ten frames was not traced -- the two sides
agree pixel for pixel there.
