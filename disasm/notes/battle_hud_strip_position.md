# The battle HUD strip has TWO resting positions — `$7E:2989` bit1

Derived 2026-08-29 from the ROM (`tools/romdis.py`) and MEASURED on the oracle. User report:
*"the hud does not change position in battle; in some battles it appears at the top and at the bottom"* (translated from Portuguese).

`battle_hud_render.md` pins the strip's PIXELS at renderY 160..207. That geometry is only ONE of the
two the ROM uses; which one a fight gets is decided before the battle module even starts.

## 1. The chain

```
C0/3A91   op 0xD8 (BATTLE), imm1 -> $7E:2989          the battle's flags byte
CF/FB86   LDA $2989 / AND #02 / LSR A / STA $EC       $7E:00EC = bit1
CF/FB8E   JSL CD0000                                  ...then the battle boots
D1/F1DF   LDA $EC / AND #01 / STA $CCA1 / STA $CA21 / STA $CA22
D1/FE4C   LDA $CA21 / CMP $CA22 / (adopt) ...
D1/FE5E   LDA $CA25 / CLC / ADC $CA22 / CLC / ADC $CCEC / JSL CE:EE6E
CD/05D7   LDA $CA22 / JSL CE:EE6E                     (the open/close animator's own reload)
```

`CE/EE6E` is the band-preset loader: `A` = preset id → `$CE:F24E,X` → nine pointers at `$CE:F58E+X ..
$CE:F59E+X` (with `$CEF5A0` swapped in for `$CEF592` when `$2A21 & 1`) → nine `MVN $7E,$CE` of 0x1E
bytes each into **`$7E:C867, C894, C8C1, C8EE, C91B, C948, C975, C9A2, C9CF`** — the indirect HDMA
header tables armed by `CD/09F1` (7 channels; ch0 mode4 `$2107` rewrites the four BG map bases to word
`0x7800` inside the strip, ch4 flips TM to `0x05`, ch5 streams the `$2130-33` gradient).
So **`$CA22` offsets the preset id, and the preset IS the strip's position.**

`CD/053C` / `CD/0577` then walk every one of those tables (`X += $2D` until `X == $0195`) two scanlines
per frame — that is the OPEN/CLOSE slide, not the resting place. Watching it live:
`--monitor 7EC867:1,7EC86A:1 --writer` on a forest battle entry shows `CE:EEBC` writing the preset at
f173 and `CD:0541` ramping `$C86A` `0x66 → 0x50` over f246-f257 (the window unfurling to its rest).

## 2. Measurement — `--hdma`, the TM=05 band, over `saves/battle_re/bat1..9_lake.state`

| state | `$2989` | bit1 | strip scanlines | renderY |
|---|---|---|---|---|
| bat1 | 92 | 1 | 161..208 | **160..207** |
| bat2 | 00 | 0 | 9..56 | **8..55** |
| bat3 | 02 | 1 | 161..208 | 160..207 |
| bat4 | 88 | 0 | 9..56 | 8..55 |
| bat5 | 12 | 1 | 161..208 | 160..207 |
| bat6 | 08 | 0 | 9..56 | 8..55 |
| bat7 | 02 | 1 | 161..208 | 160..207 |
| bat8 | 92 | 1 | 161..208 | 160..207 |
| bat9 | 12 | 1 | 161..208 | 160..207 |

9/9. **bat5 and bat6 are the SAME location (070)** with opposite positions, so this is per-BATTLE (the
op-0xD8 operand), never per-scene — a scene gate here would have been wrong.

`$7E:00EC` is also the frame-parity byte `CD/093A`-`CD/0970` toggles against `$CA21`; that is why it
reads 1/0 in lockstep with the position and is NOT an independent flag.

## 3. The strip is TRANSLATED, not re-drawn

Same 48 rows, same COLDATA ramp (`--hdma` reports the identical `fixed 8→0 ADD` / `0→7 SUB` bands in
both layouts). bat4's oracle framebuffer at renderY 8-11 = `{173,181,181} {255,255,255} {90,82,82}
{165,173,173}` and at 52-55 = `{0,0,0} {49,57,57} {0,0,8} {24,16,16}` — the baked BOTTOM strip's rows
0-3 and 44-47, byte for byte. One offset (−152) moves everything; nothing needs a second bake.

## 4. Port

`CtBattleHudRender` (src/field.c) sets `g_bhudStripDy = (mem[0x2989] & 0x02) ? 0 : (8 - 160)` and
applies it in the chrome loop and inside `battleHudGlyph`, so the baked row table `ctBattleHudRowY` and
every `rowY - 8` derivation stay in baked (BOTTOM) space — including their `y < 0` "row does not render"
sentinels.

Verified against the oracle: bat1 (bottom) **12288/12288 byte-exact**, unchanged; bat4 (top) outer frame
+ box0 + party box **9792/9792 byte-exact at renderY 8**.

**Known gap (pre-existing, orthogonal):** bat4's residual is entirely x 54..103 — it is a SOLO-PC fight
and the command-box chrome bake is a 2-PC capture, so the second command box is drawn where the ROM
draws window body. That is the horizontal box-count layout, not the vertical placement.

Pinned: `field-battle-hud-strip-top` (bat4, bit1 clear → 8), `field-battle-hud-strip-bottom` (bat9,
bit1 set → 160). Both read the strip's top back out of the RENDERED pixels — the topmost row carrying a
≥100px unbroken white run is the strip's row-1 border — so the check grades pixels, not the branch.

---

## 5. The RESULTS / message window rides the SAME presets — and sits OPPOSITE the strip

Second user report (2026-08-29): *"when the battle hud is at the top, the rewards message did not
appear"*.

`D1/FE5E`'s preset index is `$CA25 + $CA22 + $CCEC`. `$CCEC` is the **message window's open flag**
(`CD/01FD STZ $CCEC` closes it between reward lines, `battle_results_box_timing.md`), and it carries
**`$10`** — so opening the window selects a preset **0x10 higher**, one that describes BOTH windows.

Decoding the ch0 header table of each preset (`$CE:F24E[p*0x14]` → the pointer at `$CE:F58E+X`; each
entry is `count` + a 16-bit indirect address, so the counts ARE the scanline layout — `$CE:BC85` = the
field's own map bases, `$CE:BE9B` = the window map base 78/78/78/78):

| preset | `$CA25` | `$CA22` | `$CCEC` | bands (renderY) |
|---|---|---|---|---|
| **$02** | 2 | 0 | 0 | strip **8..55** |
| **$03** | 2 | 1 | 0 | strip **160..207** |
| **$12** | 2 | 0 | $10 | strip **8..55** + **window 184..207** |
| **$13** | 2 | 1 | $10 | **window 8..31** + strip **160..207** |

**The message box always takes the side the strip is not on**, and it is **24 scanlines** either way.

MEASURED LIVE, not just read off the table: `--hdma saves/battle_re/bat4_lake.state --btnscript <A-mash
to a frame where $CCEC=$10>` (bat4 is `$2989=$88`, strip-TOP) reports exactly two `TM=05` bands —
scanlines **9..56** and **185..208** = renderY **8..55** and **184..207**. Preset $12, verbatim.
Preset $13's window band (8..31) is the layout `battle_results_window.md` §V1 was captured in, and its
rows agree (top border renderY 8, white line 9, body 11-25, bottom border ~29).

### Port

`battleResultsRender` anchors the box at `(mem[0x2989] & 0x02) ? 8 : 184`. It had been pinned at 8
unconditionally, so in a strip-TOP battle the strip — opaque, and drawn AFTER the box in the compose —
covered it completely. Pinned: `field-battle-results-box-opposite-{top,bottom}`, both reading the
window tops back out of the rendered pixels (`windows=[top:runlen]`, 147 = the strip's white row-1
line, 249 = the full-width dialogue box's).

**OPEN, pre-existing, not introduced by the placement fix:** the port draws that window **40 rows**
tall where both presets give it **24** (8..31 / 184..207), and §V1's own oracle rows say 24 as well.
Wrong in BOTH positions, and the six victory `battleshot` fnv pins are self-pinned around the 40.

## 6. The windows move out of the target hand's way (2026-11-13)

The side is not only the player's (the NMI's toggle, CD/0953-096B) and the boot's (`$2989` bit 1): the target select
moves it. The single arm of the hand routine C1/1979 ends with

    C1/1A53  LDA $A4EE / BEQ ->1A78        the hand was placed or moved since the last check
    C1/1A58  STZ $A4EE
    C1/1A5B  LDA $960A / BEQ ->1A78        a list one hand walks
    C1/1A60  LDA $EC / BNE ->1A6F
    C1/1A64  LDA $0701 / CMP #2D / BCS ->1A78 / INC $EC          strip at the top, the hand's OAM row above 2D: to the bottom
    C1/1A6F  LDA $0701 / CMP #9C / BCC ->1A78 / STZ $EC          strip at the bottom, the hand at 9C or below: to the top
    C1/1A78  JMP C1/1AFB

(`$0701` = the hand's OAM Y, `$1D23,X + $9713,X`, C1/19CE-19D4). The NMI's CD/093A-094A then takes the new `$EC` on its
next even `$7C` with no layout open (`$CA21 := ($CA21 + 1) & 1`), and D1/FE4C installs the other preset (`$CA22`).

`$A4EE` is counted where the hand gets a new place: the select's opens -- C1/12B3 (Att, after `JSR C1/1F79`), C1/1386
(a tech, behind `LDA $9613 / BPL`), C1/14D2 (an item, the same) -- and its walk, C1/15FF (`$EF & 05`) and C1/160E
(`$EF & 0A`), both behind C1/15F4 `LDA $960C / BMI`. Only C1/1A58 clears it: a multi-hand pass (C1/1A7B) leaves it for
the next single one.

`$960A` is the pass's: C1/1F86 `STZ $960A` at the head of C1/1F79, then the group's handler (C1/1FF8[`$960D` & 7F], 21 and
up -> 20):

| handler | groups | `$960A` |
|---|---|---|
| C1/203A (`INC` at C1/2042) | 00 and the table's default (0E, 10, 15-17, 19, 1C-20) | 1 |
| C1/20E0 | 03 | 1 |
| C1/2136 / C1/2163 (`INC` at C1/213A) | 05, 06 | 1 |
| C1/20A9 (`INC` at C1/20B1) | 07 | 1 |
| C1/2169, C1/21AF, C1/2203, C1/225F, C1/22F5 (`JSR C1/20A9`, then `STZ $960A`) | 0B, 0D, 0C, 12 / 1A, 0F | 0 |
| C1/209A, C1/20D6, C1/20B6, C1/20C6, C1/224B, C1/22A4, C1/2329, C1/22D3 | 01 / 04, 02, 08 / 0A, 09, 11, 13 / 14, 18, 1B | 0 |

Measured on the rule's cartridge, playthrough leg 113 (the Bantam Imps and the Gargoyle, the strip at the bottom from
the boot):

    f626    `$960A` = 01 (C1:20B4, the first Att select; its hand, row 8B and above, moves nothing)
    f2427   an item's select opens on the party (`$9609` 00 -> 01, `$A62D` = 02: Ayla, low on the screen): `$0701` = 9F
            (C1:19D7), `$EC` 01 -> 00 (C1:1A78)
    f2428   `$CA21` 01 -> 00 (the NMI)
    f2429   `$CA22` 01 -> 00 (the preset): the strip is at the top from the next picture, for the rest of the fight
    f2439   the select's walk (`$9614` 00 -> 01, `$A62D` = 00): `$0701` = 7F, between the two thresholds -- no move

The port had the NMI's half (battleWindowSideFrame) and nothing that wrote `$EC` after the boot: its strip stayed at the
bottom, over the enemy the hand pointed at -- 24,577 px of every frame from there on (leg 113 f3600; the same count in
the picture audit's samples of legs 42, 65, 93 and 135). battle.c: `battleSelectOneHand` (the pass's `$960A`, from
battleTargetLitFlag -- every stand-in for C1/1F79 ends there), the five `$A4EE` counts at their sites, and the single
arm's check in battleTargetHandBuild.

Measured: an item's open (C1/14D2) on the party's side, bottom -> top. Not measured (the ROM's listing only): the
top -> bottom direction (C1/1A64), a move made by the walk's count (C1/15FF / C1/160E -- leg 113's walk lands between
the thresholds), the Att's and a tech's opens (C1/12B3, C1/1386). `$960C` on the PC side's all-targets groups (01 / 04: C1/20A2 `LDA #80 / STA $960C`) is still
written 00 by the port's battlePcTargetPublish -- its tick asks the group instead; no direction press in such a select
has been measured.

Check: `leg-window-windows-side-follows-the-hand-vs-cartridge`.
