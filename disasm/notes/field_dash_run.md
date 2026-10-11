# The field lead's RUN = the Dash row of the button map, read from the pad latch

Status: DERIVED + PORTED (2026-10-08). Check `field-dash-run-vs-logical-oracle` (`python tools/dash_run_cmp.py`).

## 1. The ROM

The lead's velocity intent is C0/88EE, reached from the player controller C0/881E (`JSR C0/88EE` at C0/883D,
gated `LDA $0162 / BNE` + `LDA $011F / BEQ`). It indexes a 16-entry table at C0/8902 with the held d-pad nibble
(`LDA $00F9 / AND #0F / ASL A / TAX / JSR ($8902,X)`). Every direction handler opens with the run test, e.g. C0/8925:

```
C0/8925  LDA $00F8        ; the decoded HELD byte (absolute, bank $7E -- not DP: the controller runs with D = $1D00)
C0/8928  BIT #02          ; bit 1
C0/892A  BEQ ->8938       ; clear -> the walk magnitude
C0/892C  LDA #20          ; set   -> accumulator delta 0x20 (2 px/frame) instead of 0x10
```

`$00F8` is written only by the NMI's pad latch C2/84D2 -> C2/8545, the second of three passes through C2/8555
(Y = 0, X = 2: `$F0,X` = `$00F2`, the held byte in CT's layout A=80 X=40 L=20 R=10 B=08 Y=04 Sel=02 St=01). Each pass
builds its byte with eight `LDA $F0,X / AND mask / CMP mask / ROR $F6,X`; the first ROR ends in bit 0, so:

| `$00F6/$00F8/$00FA` bit | mask | Config row (menu2_config_toggles.md sec. 2.6) |
|---|---|---|
| 0 | `$2998` | Warp |
| **1** | **`$2996`** | **Dash** (C2/C2E8) |
| 2 | `$2997` | Map |
| 3 | `$2994` | Cancel |
| 4 | `$2999` | page-R |
| 5 | `$299A` | page-L |
| 6 | `$2995` | Menu |
| 7 | `$2993` | Confirm |

`$00F7/$00F9/$00FB` = the raw high byte (`LDA $F1,X / STA $F7,X`, the d-pad nibble). Factory map C2/FCA9:
`$2996 = 08` = B -- Dash shares B with Cancel (rows 1 and 3 are deliberately absent from each other's conflict
lists). There is NO "Dash on/off" toggle on the SNES Config screen (menu2_config_toggles.md lists every row); nothing
inverts the bit -- holding the Dash button runs, releasing it walks.

The anim follows from the deltas, not the button: C0/9E84's dispatch (move.c CtPlayerAnimSelect) picks the run arms
(A11D, tail A18C: mode 06, or 17 head-shake when blocked) from the 0x20 magnitude.

## 2. Measured (logical oracle, `CT_ORACLE_LOGICAL=1`)

`saves/truce_market.state`, `--monitor "00F2:2,00F8:2,1D32:2" --input "0:4,21:12,20:6"` (B+Down, then Down):
`$00F2 = 08 04`, `$00F8 = 0A 04` (Dash + Cancel, both B), `$1D33 = 20`; Down alone: `$00F8 = 00 04`, `$1D33 = 10`.

`saves/battle_re/party3_field.state` (lead X = `$0197` = 02), `--btnscript "0:4,41:30,40:30,0:10"`: `$1D32 = E0`,
`$1682 = 06` f5-f34; B released: `$1D32 = F0`, `$1682 = 01` from f35. With `PEEK_POKE=2996=04` (Dash on Y) the drive
`42:30,41:30` runs on Y f5-f34 and WALKS on B from f35.

## 3. The port

`src/field.c`, the camera/lead input block: `crun` is the scene WRAM's `$00F8 & 0x02` -- the byte `fieldPadNmi`
(C2/84D2 = player.c CtJoypadPoll) latched this frame from the controller word `fieldLockInputBits` builds. The
keyboard's X is pad B; a drive's `CT_RUN=1` ORs B (0x001) into the word on every frame the drive holds a direction
(the same word the CT_FIELDLOCK oracle gets), `CT_HOLDBTN=08@f:n` holds B over a window. The Shift binding is retired.
`--walktest` (`walkDir`) still hands `CT_RUN` straight to fieldPlayerCollide: that harness has no pad latch.

`python tools/dash_run_cmp.py`: hold / ctrun / remap, f0-f71, 0 frames differ on `$1802 $1882 $1D32 $1682 $00F8`.

## Open questions

- The d-pad itself still comes from the host keys / `wsDx` in that block, not from the latched `$00F9`; they agree
  frame for frame under the port's input convention, but the ROM reads `$00F9`.
