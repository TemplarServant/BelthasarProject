# The battle HUD strip has TWO resting positions — `$7E:2989` bit1

Derived 2026-08-29 from the ROM (`tools/dis.py`) and MEASURED on the oracle. User report:
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
