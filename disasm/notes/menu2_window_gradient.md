# menu2_window_gradient — THE MENU WINDOW SHADING IS GENERATED PER SCREEN (C2/86DD), not baked

**Question that started it:** the user reported that the menu's character-exchange (Form) screen was
still wrong. Rendering it and reading the pixels showed the third party panel and the "Exchange"
window fading to near-black while the oracle draws them as bright as the first. This is the general
mechanism behind that, and it is not a Form bug — it is a bug in every screen that was not
special-cased.

---

## 1. VERDICT

The dark menu windows are not painted with a stored gradient. **HDMA channel 0 writes `$2131`/`$2132`
(CGADSUB + COLDATA) once per scanline**: a grey fixed colour is **ADDED** on the way down each window
and **SUBTRACTED** on the way out — one **16-step ramp `+8 .. -7` in 5-BIT colour space** that **RESETS
and HEIGHT-NORMALISES per window**.

The generator is **C2/86DD**. It consumes a **WINDOW LIST** of 2-byte `[kind, height]` entries and
emits the HDMA line-count table. The lists are **ROM data at `$FF:BBF2`**, one per ribbon option.

That is why a 192 px window steps every 12 scanlines, a 48 px window every 3, a 32 px window every 2
and an 80 px window every 5 — all out of one routine, with no per-screen constants.

**The port had three baked tables, each one instance of this machine** — `ctMenuWinGrad` (the status
screen's single 192 ramp), `ctMenuItemGrad` (the Item list) and `ctMenuSaveGrad5` (the Save list) —
and every screen that was not special-cased got the **status** table. So the Tech, Config and Form
windows ran one continuous 192-line ramp down the whole screen instead of restarting per box.

---

## 2. ROM TRACE

### 2.1 The generator — C2/86DD

```
286DD PHP / PHB / REP #30
286E1 LDX #8771 / LDY #9DE9 / LDA #001F / MVN $7E,$C2   ; copy the 16 (CGADSUB, COLDATA) pairs to $7E:9DE9
286ED LDA #3101 / STA $4300      ; ch0: bAdr = $31 ($2131), mode 1 = TWO registers per entry
286F4 LDA #007E / STA $4304      ; table bank $7E
286FB LDY #0000 / LDA #9E19
28701 LDX $A01D / BNE ->2870C
28706 LDY #0100 / LDA #9F19      ; DOUBLE BUFFERED: alternates $7E:9E19 / $7E:9F19
2870C STY $A01D
2870F STZ $A01B                  ; the 16-bit scanline accumulator
28712 LDX #0000
28715 SEP #20
28717 LDA $9D99,X                ; ★ THE WINDOW LIST, $7E:9D99, stride 2
2871A BMI ->2876E                ; bit7 -> END (the terminator is ONE byte)
2871C REP #20
2871E BEQ ->2875B                ; kind 0 -> the FLAT arm
   ; ---- RAMP ----
28720 LDA $9D9A,X / AND #00FF / ASL A x4 / STA $A019    ; $A019 = height * 16
2872D PHX / LDX #0000
28731 REP #20
28733 LDA $A01B / CLC / ADC $A019 / STA $A01B           ; accumulate
2873D SEP #20 / XBA / STA $9E19,Y                       ; line count = the INTEGER part (hi byte)
28743 STZ $A01C                                          ; keep only the FRACTION
28746 REP #20 / LDA $9DE9,X / STA $9E1A,Y               ; the two data bytes = ramp pair X
2874E INX / INX / INY / INY / INY
28753 CPX #0020 / BCC ->28731                            ; 16 pairs
28758 PLX / BRA ->2876A
   ; ---- FLAT ----
2875B LDA $9D9A,X / STA $9E19,Y                          ; ONE entry of `height` lines
28761 LDA $9DF9 / STA $9E1A,Y                            ; $9DE9+0x10 = the NEUTRAL pair 02 E0
28767 INY / INY / INY
2876A INX / INX / BRA ->28715
2876E PLB / PLP / RTS
```

### 2.2 The pair table — C2/8771 (32 bytes)

```
02 E8  02 E7  02 E6  02 E5  02 E4  02 E3  02 E2  02 E1
02 E0  82 E1  82 E2  82 E3  82 E4  82 E5  82 E6  82 E7
```

First byte → `$2131` CGADSUB (`0x02` = colour math on BG2, add; `0x80` set = **subtract**). Second byte
→ `$2132` COLDATA (`0xE0` = all three channel-select bits, low 5 bits = the intensity). So pair `i`
carries the signed 5-bit delta **`8 - i`**, i.e. `+8 +7 … +1 0 -1 … -7`. Index 8 (`02 E0`, delta 0) is
the neutral pair the FLAT arm reads at `$9DF9`.

### 2.3 The window lists — ROM `$FF:BBF2`, length-prefixed and contiguous

Copied to `$7E:9D99` by the generic bank-`$FF`→`$7E` block copier **C2/F362**
(`LDX $5B / LDA $0000,X / DEC A / INX / LDY $61 / MVN $7E,$FF` — byte 0 is the length, the list starts
at +1). Writer PC seen by the monitor: **C2:F376**.

| ribbon option | ROM | list (`kind` × `height`) | sums to |
|---|---|---|---|
| 0 Status | `FF/BBF3` | flat 16 · **RAMP 192** · flat 16 | 224 |
| 1 Item | `FF/BBFB` | flat 16 · RAMP 32 · RAMP 32 · RAMP 128 · flat 16 | 224 |
| 2 Tech | `FF/BC07` | flat 16 · RAMP 144 · RAMP 48 · flat 16 | 224 |
| 3 Config | `FF/BC11` | flat 16 · RAMP 160 · RAMP 32 · flat 16 | 224 |
| 4 **Form** | `FF/BC1B` | flat 16 · **RAMP 48 × 4** · flat 16 | 224 |
| 5 Save | `FF/BC29` | flat 16 · RAMP 32 × 3 · flat 16 · RAMP 80 · flat 16 | 224 |

More lists follow at `$FF:BC38` (`flat 8 · RAMP 32 · RAMP 32 · …`) for the shop screens, which this
port does not reach.

---

## 3. ORACLE MEASUREMENTS

### 3.1 The list, per hover — `--monitor` on `$7E:9D99`

```
$ ct.exe … --monitor "7E9D99:20" --from saves/com_jogo_salvo.state \
      --btnscript "0:20,200:2,0:40,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:20,100:2,0:60" --diff --writer
  f59   7E9D99=0010 01C0 0010 80…  *(C2:F376)      ; opt0
  f63   7E9D99=0010 0120 0120 0180 0010 80…        ; opt1
  f73   7E9D99=0010 0190 0130 0010 80…             ; opt2
  f84   7E9D99=0010 01A0 0120 0010 80…             ; opt3
  f93   7E9D99=0010 0130 0130 0130 0130 0010 80…   ; opt4  ← FOUR windows
```

**Entering a sub-screen never rewrites it.** Driving `opt N` then `A` (and, for opt 0, `A` again into
the equip DETAIL, `$0067=01 $00BE=02`) produced **no further row** on any option — so an entered
screen keeps the hovering option's list. That matters for the port: `CtMenuChrome(fb, screen, opt, …)`
must index by `screen - 1` when a screen is entered, which is the same option in practice.

### 3.2 The hardware, per option — `--hdma`

`--hdma`'s `scanline trace` section reports the fixed colour and the sub flag the PPU **actually saw**
each line, independent of any table decode. For option 0 and option 4:

```
opt0:  lines  17.. 28  fixed=(8,8,8) sub=0     opt4:  lines 17..19 fixed=(8,8,8) sub=0
       lines  29.. 40  fixed=(7,7,7) sub=0            lines 20..22 fixed=(7,7,7) sub=0
       …                                              …
       lines 197..208  fixed=(7,7,7) sub=1            lines 62..64 fixed=(7,7,7) sub=1
                                                      lines 65..67 fixed=(8,8,8) sub=0   ← RESTART
```

12 scanlines a step for one 192 px window; 3 for four 48 px windows, restarting at 16/64/112/160.
(The trace runs one line behind the HDMA table entry — the usual latency.)

**Graded row by row against the port's generator: `224/224` scanlines identical on ALL SIX options.**
That comparison is `tools/regress_checks.json → menu-window-gradient-vs-hdma`, whose expected bands
are these measurements.

### 3.3 Two independent confirmations

* **`ctMenuSaveGrad5` is reproduced exactly.** That 224-entry table was derived pixel-exactly from
  three oracle Save-screen captures in an earlier pass, with no knowledge of C2/86DD. Running the
  generator on option 5's list gives it back with **0 mismatches at all 224 rows**.
* **The rendered frame moved toward the cartridge.** Against `scratchpad/menu/or_opt0.png` (the oracle
  status screen for the same party), our opt-0 frame went **24.32% → 59.27% pixel-identical**, and
  pixels off by more than 4 channels **10257 → 5187**. On the Form screen, sampling the window fill at
  `x=69` down panels 1/2 and the Exchange box against `scratchpad/menu/_av_form1.png`: **2/19 → 17/19**
  sampled rows exactly equal (the two remaining are where our panel TEXT sits 32 px left of the ROM's —
  a separate, still-open defect).

---

## 4. IMPLEMENTATION

* `tools/_gen_menuwingrad.py` → `src/data/menu_wingrad.h`: `ctMenuWinList[6][16]`, the six lists baked
  straight out of `$FF:BBF2`. The script asserts that C2/8771 decodes to the `+8..-7` ramp it assumes.
* `mWinRamp()` in `src/menu.c` is C2/86DD line for line (including `$A01C` zeroing, which keeps the
  fraction so a height that is not a multiple of 16 splits its steps unevenly).
* `mChromeTMPal2L()` now takes the generated `int8_t d5[224]` and applies the **hardware** model
  everywhere: `out = conv5(clamp((pix >> 3) + d5[y], 0, 31))`. The 8-bit add it replaced was ±1 off at
  some tones (a fact the Save-screen derivation had already recorded).
* `ctMenuWinGrad` / `ctMenuItemGrad` / `ctMenuSaveGrad5` are **deleted**.

---

## 5. NOT GRADED, ON PURPOSE

The **fractional carry** (`STZ $A01C`, not `STZ $A01B`) has **no reachable witness**. Every ramp height
in the six X-menu lists is a multiple of 16 — 192, 160, 144, 128, 80, 48, 32 — so the remainder is
always zero and a mutation that drops the carry SURVIVES. That is a fact about the lists, not about the
routine: the shop lists at `$FF:BC38` begin `flat 8` and would exercise it. **Do not add a mutation for
it without a witness.**

---

## 6. WHAT THE SAME PASS THEN FIXED ON THE FORM SCREEN (measured here, fixed in the next commit)

All of these were found while grading the gradient against `_av_form1.png`, and all are closed except
the last:

* **CLOSED — the party-panel text column.** The Form layout puts the name at **x = 72** (tile col 9) on
  every panel; the port drew it at x = 40, so the hand cursor (x 56..71, the ROM's own `$7E:9380` nav
  column) sat on top of the name. The Tech screen's x = 40 and the status screen's x = 24 are different
  layouts and were both already right, so this is a Form-only inset (`MFORM_INSET` 56).
* **CLOSED — the numeric fields were 4 px right on EVERY screen.** Current value right-aligned to
  `x0+48`, `/` at `x0+48`, maximum right-aligned to `x0+80` — measured on `or_opt0.png` (Crono HP 70/70
  at x0 = 24: digits on x 56/64, `/` at 72, max on 88/96). Cross-correlating our HP row against that
  capture peaks at exactly **-4 px with a perfect 35/35 ink overlap**. Fixing it took the opt-0 frame
  from 59.27% to **60.39%** pixel-identical and collapsed the worst-delta histogram (231-off pixels
  69 → 4).
* **CLOSED — the fourth text row.** Each Form panel carries weapon icon + attack, armour icon + defence
  at tile row `6n+6` (y `48n+49`); the port drew only three rows. Offsets `x0+8 / x0+40 / x0+48 / x0+80`
  reproduce the oracle status capture's ink runs exactly.
* **CLOSED — the dim rule.** `C2/CC4F` acts only where `$0F02,X & 0xC0` — LOCKED or EMPTY. The port dimmed
  every panel except the first selectable one, so a full three-member party showed two greyed members.
* **CLOSED — the panel array.** `C2/CD2B` **compacts** (`menu2_form_screen.md` AV D3); the port was
  index-aligned. Witness: a party with a LEADING hole, which field-VM op `0xD6` (`C0/3A07`) produces.
* **CLOSED — the standing sprites' anchor.** `C2/CDE3` writes each character's menu-object position word
  from `C2/CE6E`, X-menu index `$12`: `(0x38,0xA8) (0x68,0xA8) (0x98,0xA8)`. Read as the field engine's
  convention (X = centre, Y = feet) that is **centre 168, feet 56/104/152**, and all three oracle sprites
  confirm it — ink centred on 168, ending at y 55/100/151. The port had them at the Tech screen's x 17/20.
* **CLOSED — the swap indices.** `C2/C6A8` and `C2/C6D5` both store `$0F0D`, the panel ENTRY under the
  cursor, into `$0F00`/`$0F01`, and `C2/C70F` loads X and Y from those — so an exchange permutes the two
  picked ENTRIES, not the two cursor CELLS. With a leading hole the cells 0 and 1 are roster slots 1 and
  2, so the cell version would swap the EMPTY slot with a member. Also `CtMenuFormSwap` clamped to 0..2;
  `C2/C70C`'s X and Y are 8-bit and index `$2980..$2988` uniformly.
* **CLOSED — the opt-4 HOVER dimmed nothing.** `C2/CA3F` runs `C2/CC4F` exactly as `C2/CB0D` does, so
  the locked panel is dim on the hover as well as on the entered screen (`_av_rib4.png` shows it). The
  port built the hover's panel array with no lock information; `CtMenuHost` now carries the `$7F` image.
* **STILL OPEN — the menu sprite ASSEMBLER and five characters' art.** Only Crono and Marle have art
  baked (`menu_partyport.h`, lifted from oracle OBJ VRAM for the party in that capture); the other five
  draw nothing rather than a guess. And the ROM's per-character frame SHAPE varies — Crono assembles as
  16x64 at OAM y 8, Lucca and Frog as 32x32 at y 70 / 120 — which comes from the menu OBJ engine at
  `$7E:1800` (stride 0x40, records built by `C2/CE49`: `+0x00` charId, `+0x0E` the position word,
  `+0x11` the pose byte `C2/C973` sets) run by **`C2/F657`**. That assembler is NOT ported; the port
  anchors its blob by the blob's own opaque box, which lands Crono's ink on the oracle's to the pixel
  horizontally and to the row vertically, and would be up to 3 px out for a frame with transparent rows
  at its bottom (Lucca's is).
