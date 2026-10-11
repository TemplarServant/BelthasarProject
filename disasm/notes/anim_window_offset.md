# The playAnim window offset is `anim * 4` — one law for PCs and enemies

Derived 2026-08-16, chasing "the hetake's attack animation is bad". The question was: the anim
script's playAnim ops take an operand (`02 05`, `03 07`, `06 03`) — what does the ROM do with it to
produce a displayed pose sequence? **Answer: it is a byte offset of `operand * 4` into the
combatant's pose/dwell pointer pair.** Nothing here is implemented; this is the derivation.

## 1. The handler — `C1/4D04` (op 0x02, play-loop)

`python tools/dis.py 14D04 0x50 1 0`:

```
  14D04: LDA #02 / STA $8E              ; $8E = the MODE (2 = loop)
  14D08: INY / LDA [$E6],Y / STA $80    ; $80 = the ANIM OPERAND
  14D1B: LDX $A010 / LDA $A014,X / STA $8A     ; $8A = the acting COMBATANT
  14D23: ASL A / TAY                    ; Y = cmb*2
  14D25: CLC / ADC $8A / TAX            ; X = cmb*3
  14D29: LDA $1C99,X / STA $A1E8,Y      ; POSE ptr  := [$1C99 + cmb*3]   (16-bit, +1 at 14D2F)
  14D35: LDA $1CBA,X / STA $A1FE,Y      ; DWELL ptr := [$1CBA + cmb*3]   (16-bit, +1 at 14D3B)
  14D41: LDA $80 / REP #20 / ASL A / ASL A / STA $82      ; <<<< $82 = operand * 4
  14D49: CLC / ADC $A1FE,Y / STA $A1FE,Y                  ; DWELL ptr += operand*4
  14D50: CLC / LDA $82 / ADC $A1E8,Y / STA $A1E8,Y        ; POSE  ptr += operand*4
  14D5C: LDA $8A / TAY
  14D5F: LDA $8E / STA $A124,Y          ; $A124,cmb = the mode
  14D64: TDC   / STA $A12F,Y            ; $A12F,cmb = the dwell counter := 0
  14D68: DEC A / STA $A13A,Y            ; $A13A,cmb = the pose CURSOR := 0xFF (reset)
  14D6C:         STA $A214,Y
```

`C1/4E22` (op 0x03, play-ONCE) is the same body with `$8E = 1`, entered at `14D08`, plus a yield:
it re-visits until `$A124,X` reads 0 (`14E41 LDA $A124,X / BNE ->14E4E`) and only then advances the
script pointer by 2 (`14E46 STZ $A145,X / LDA #02 / JMP C1/75BB`). So a play-once BLOCKS its track
until the window finishes, and completion is signalled by the mode going back to 0.

## 2. FIVE independent confirmations of `* 4`

1. **`C1/4D04` itself** — the two `ASL A` above, added to BOTH pointers.
2. **`C1/2E40`, a second site with a literal**: `LDA #2B / STA $A440 … LDA $A440 / REP #20 / ASL A /
   ASL A / ADC $A1FE / … ADC $A1E8` — anim `0x2B`, same arithmetic, no operand fetch.
3. **Our own baked enemy "class bases"** (`src/battle.c` `CtBattleEnemyIdleWin`):
   `base = (cls == 0) ? 0x0C : (cls == 1) ? 0x04 : 0x18`. Those are **anims 3, 1 and 6** times 4.
   `CC/FB91[mode]` was never a separate mechanism — it just picks WHICH ANIM the idle plays.
4. **Our own baked PC window names** (`src/battle.h`, `CtBattlePcWinPose`): idle `+0x0C`, run `+0x18`,
   recover `+0x38`, crit-windup `+0x98`, strike `+0xC4`, strike-follow `+0xC8` = anims
   3, 6, 0x0E, 0x26, **0x31**, **0x32**. And the PC scripts read straight out of bank CE contain
   exactly those operands: `CE/038E … 03 31 …` (the normal attack's strike) and `CE/03DC … 03 32 …`
   (the crit's SECOND slash). The names and the script operands are the same numbers.
5. **The Hetake's own windows** reproduce the ladder that was hardcoded from a capture — §4.

## 3. The window data

`poses[i]  = ptrA[anim*4 + facing*stride + i]`, terminated by `0xFF`
`dwells[i] = ptrB[anim*4 + i]` — **facing-independent** (C1/33F1-3405), and the RAW byte is the live
frame count; `CC/FBCE[raw]` is the same value in TICKS (the `floor(raw/5)` staircase our
`CtBattleEnemyIdleWin` already applies).

ptrA/ptrB are the per-combatant pair at `$1C99 + cmb*3` / `$1CBA + cmb*3`, which
`CtBattleEnemyPoseStage` already resolves from the id-keyed chain (`E4:2600`/`E4:2800[animId*2]`).

## 4. The Hetake, and where the hardcoded ladder came from

Its attack script is `CD/5FF0[0x14] = CD/72B9` (see the previous commit for how `$9877 = 0x14` is
itself a ROM table read). Track t0 plays four anims. Their windows, id `0x5E`, facing 0:

| script op | anim | base | poses (to the `FF`) | raw dwells |
|---|---|---|---|---|
| `02 05` approach, loop | 5 | `0x14` | **03** | 5 |
| `03 07` strike, once   | 7 | `0x1C` | **04, 05** | 5, 5 |
| `02 00` return, loop   | 0 | `0x00` | **06** | 5 |
| `06 03` settle, 1st    | 3 | `0x0C` | **01** | 15 |

`src/field.c`'s `battleHetakeRender` hardcodes

```c
pose = (rel < 3) ? 0x03 : (rel < 8) ? 0x04 : (rel < 29) ? 0x05 : 0x06;
```

which is **exactly the concatenation of the first three windows, in order** — 03, then 04+05, then
06. It was read off a capture. What it misses is the fourth window: the settle pose **01** never
appears, because the ladder freezes on 06 for the rest of a ~200-frame span.

## 5. The live machine — MEASURED

`build/ct.exe --monitor "7EA124:11,7EA13A:11,7EA12F:11,7EA013:1" --from
saves/battle_re/forest_battle_3hetake.state --input "0:260" --frames 260 --diff`
(the acting enemy is combatant 3 = byte 3 of each 11-byte row):

```
  f51   mode=00 cursor=00 dwell=00   A013 0->1   the action window opens
  f83   mode=02 cursor=FF dwell=00               <- `02 05` ARMS (cursor reset = 14D68's TDC/DEC A)
  f84   mode=02 cursor=00 dwell=05               <- entry 0, RAW dwell 5
  f89   mode=02 cursor=00 dwell=05               ... reloads 5 every 5 frames (a 1-entry loop)
  f102  mode=01 cursor=FF dwell=00               <- `03 07` ARMS (mode 1 = ONCE)
  f145  mode=01 cursor=FF dwell=00
  f146  mode=00 cursor=00 dwell=0F               <- the once-anim COMPLETES: mode back to 0
  f161  ...     dwell=00
  f228  A013 1->0                                the span ends
```

So: approach 32 frames, `02 05` loops f83-f101, `03 07` runs f102-f145, mode clears at f146. That is
the play-once yield of §1 observed directly, and the RAW dwell 5 is the ptrB byte of §4 observed
directly.

## 6. NOT DERIVED — stated plainly

* **The `02 00` and `06 03` tail never appeared in the measurement.** After f146 the mode stays 0
  through the span end at f228. Either those two ops are reached later than this capture's window,
  or the track does not reach them on this path. Until that is measured, the return/settle half of
  §4's table is **ROM data whose PLAYBACK is unconfirmed** — do not drive a render from it yet.
* **The transition FRAMES between windows** (f83, f102, f146) are measured on ONE attack. The same
  fight's second attack has a different span (177 vs 202, previous commit), so these are not
  constants; the approach walk is inside the ROM's action window and outside ours.
* **The facing term for the enemy attack path.** §3's `facing*stride` is what
  `CtBattleEnemyIdleWin` already applies and is validated for IDLE; neither C1/4D04 nor C1/2E40 adds
  a facing term at the pointer, so whether it enters at the pose READ instead was not traced.
* **`$A214,cmb`** (written alongside the cursor at 14D6C) is not identified.
* **`$A145,X`**, the play-once re-entry latch (14E32/14E37/14E46), is transcribed, not measured.
