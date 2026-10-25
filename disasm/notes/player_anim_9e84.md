# `C0/9E84` — the player's FACING + WALK-ANIM selector

Derived 2026-08-01, first-hand from raw ROM bytes (`dis.py` desyncs on the jump table, which is
data). This is what `--portrun`'s idx-02 residual is: 28 of its 32 remaining divergences are the
player, and most of them are `$0F01`, the animation frame.

`C0/9E29` — the PC controller's apply — calls this **first**, at `9E29 JSR C0/9E84`, before it
copies the velocity. The port models `9E29` and deliberately not this, so the player never enters
walk-animation mode and its frame lookup drifts.

## The index

Raw bytes `9EAF..9ED0`:

```
AD 2C 1D  10 05  49 FF  18  69 21     LDA $1D2C / BPL +5 / EOR #FF / CLC / ADC #21
4A 4A 4A  85 DB                       LSR A x3 / STA $DB          <- X axis SHIFTS
AD 2D 1D  10 05  49 FF  18  69 21     LDA $1D2D / BPL +5 / EOR #FF / CLC / ADC #21
05 DB  29 7E  AA                      ORA $DB / AND #7E / TAX     <- Y axis does NOT shift
FC F7 9F  60                          JSR ($9FF7,X) / RTS
```

Two things a port gets wrong by default:

- **The negative branch is not a two's-complement negate.** It is `EOR #FF / CLC / ADC #$21`, i.e.
  `(~a + 0x21) & 0xFF` = `(0x20 - a) & 0xFF` for the byte value `a`. `#21`, not `#01`.
- **Only the X axis is shifted** (`LSR A` three times). The Y term is ORed in unshifted, so the two
  occupy different bit ranges of one index, and `AND #7E` keeps bits 1..6 — an **even** index into a
  **word** table, 64 entries.

## The table at `$9FF7`

64 words, dumped raw. Seventeen distinct targets: `9FF2` (the default) and sixteen handlers.

```
X=00: 9FF2 A083 A0AB A079 A0A1 9FF2 9FF2 9FF2 A097 A0DE A132 A0C9 A11D 9FF2 9FF2 9FF2
X=20: A0BF A132 A132 A11D A11D 9FF2 9FF2 9FF2 A08D A108 A15C A0F3 A147 9FF2 9FF2 9FF2
X=40: A0B5 A15C A15C A147 A147 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2
X=60: 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2 9FF2
```

`9FF2` is `LDX $6D / BRL ->A1E9` — a different tail, and it does **not** touch the facing.

### The handlers — CORRECTED

> An earlier revision of this file said all eight cardinal handlers fall into `A171`, and explained
> the two halves as the index math "folding the sign". **Both were wrong.** I had decoded the BRL at
> the wrong offset (+5 lands mid-instruction; it is at +7). Re-decoded from raw bytes:

**Group 1 — cardinal, stride `$0A` from `A079`.** Each is
`LDX $6D / LDA #f / STA $1600,X / BRL tail`, an **unconditional facing** — and the two halves are
**WALK vs RUN**, not sign duplicates:

| handler | facing | tail | | handler | facing | tail |
|---|---|---|---|---|---|---|
| `A079` | 2 | `A171` walk | | `A0A1` | 2 | `A18C` run |
| `A083` | 3 | `A171` walk | | `A0AB` | 3 | `A18C` run |
| `A08D` | 0 | `A171` walk | | `A0B5` | 0 | `A18C` run |
| `A097` | 1 | `A171` walk | | `A0BF` | 1 | `A18C` run |

**Group 2 — diagonal, stride `$15` from `A0C9`.** Each is
`LDA $1600,X / CMP #a / BEQ tail / CMP #b / BEQ tail / LDA #a / STA $1600,X`, i.e. **keep the current
facing if it is already one of the diagonal's two components, else take the first** — and the same
walk/run split:

| handler | keep if | else | tail | | handler | keep if | else | tail |
|---|---|---|---|---|---|---|---|---|
| `A0C9` | 02, 01 | 02 | `A171` | | `A11D` | 02, 01 | 02 | `A18C` |
| `A0DE` | 03, 01 | 03 | `A171` | | `A132` | 03, 01 | 03 | `A18C` |
| `A0F3` | 02, 00 | 02 | `A171` | | `A147` | 02, 00 | 02 | `A18C` |
| `A108` | 03, 00 | 03 | `A171` | | `A15C` | 03, 00 | 03 | `A18C` |

So the table encodes **direction AND speed**: the Y magnitude enters the index unshifted, so a large
delta selects the run half. The keep-else-force rule is why a diagonal walk does not flip the sprite.

## The tails — seven terminal writers, one shape

Every tail ends in the same 15-byte CMP-guarded body:

```
LDA $1680,Y / CMP #k / BEQ rts / LDA #k / STA $1680,Y / $1681,Y := 0 / $1601,Y := 0 / RTS
```

| tail | `k` | what it is |
|---|---|---|
| `A171` | `01` | WALK |
| `A1A4` | `06` | RUN |
| `A1BB` | `17` | run-into-wall HEAD-SHAKE |
| `A1D2` | `19` | |
| `A211` | `00` | plain stand |
| `A23D` | `21` | idle BLINK |
| `A254` | `1B` | |

**The guard is load-bearing:** when the mode is unchanged, phase and timer are NOT reset — which is
why the walk phase does not stutter while a direction is held. `A171` and `A18C` additionally
`STZ $58/$59` on **every** call; `A211`/`A23D` do not.

`k = $17` and `k = $21` corroborate the independently-derived `ct-player-anim-walkidle` memory
("run-into-wall HEAD-SHAKE 0x17; idle-BLINK 0x21, ctr `$58` >= `$2FF`"), which is a useful
cross-check that this table reading is right.

`9FF2` is `LDX $6D / BRL ->A1E9` — the idle chain, and it does not touch the facing.

## Early arms `9E88`–`9EAE` — PARTLY DERIVED

```
9E88  LDA $1D34 / BEQ ->9EAF
9E8D  BIT #04 / BNE ->9E95
9E91  INC $E5 / BRA ->9EAF
9E95  LDA $1D2D / BEQ ->9EA4 / (abs) / CMP #18 / BRL ->A254   <- the BRL is UNCONDITIONAL
9EA4  $1681,Y = 0 / $1601,Y = 0 / RTS
```

`9EA1` is opcode `82` = **BRL, unconditional** — so the `CMP #$18` above it has NO consumer and is
dead. Reading that branch as conditional is the obvious mistake.

`$1D34`, the `BIT #04` bit and `$E5`'s consumer are **not derived**. Do not guess them; the `9EA4`
arm looks like a stand-still reset (it zeroes phase and timer but deliberately leaves `$1680`
alone) but that is a reading, not a derivation.

## Why this closes the `--portrun` residual

`$0F01` comes from `CtFieldFrameLookup` (`C0/CB3A`), which reads `$1600` (facing), `$1680` (mode),
`$1681` (phase) and `$1601` (timer). `C0/9E84` is the only writer of all four for the player, and
the port does not call it — so the lookup runs off stale state. Observed divergences match the
shape: `f266` port `0F06` vs oracle `0F12`, `f278` `0F0E` vs `0F08`.

## PORTED AND BYTE-EXACT (2026-08-02) — `CtPlayerAnimSelect`, `--sweepanim` 22400/22400

**22400/22400 byte-exact against micro-execution, all 17 dispatch slots reached.** Wiring it at
`C0/9E29`'s head then took `--portrun` from 6454/6466 to **6462/6466**, 12 divergences to **4**, and
**idx 02 — the player — is clean across all 360 frames.**

### The defect the first cut had

`C0/A171`, the WALK tail, opens `STZ $58 / STZ $59` **exactly like** `C0/A18C`, the RUN tail. Only
A18C's copy had been transcribed, so every seeded idle counter survived a walk. Reverting that pair
alone costs **1280 of 22400** — and no scenario check could have named it, because `--portrun`
reaches one facing sequence on one actor.

> **The order of work was the whole difference.** The 2026-08-01 attempt (3648128) wired this into a
> scenario *first* and never graded the routine itself. This time the sweep came first and the port
> was byte-exact before it was wired to anything.

### The seed grid is part of the check

The first grid reached **16 of 17** slots and still reported a clean 14336/14336. The hole was
`C0/A0B5` (facing 0, RUN tail), which the table holds at `X = $40` **and nowhere else** — unreachable
while every seeded Y delta folded below it, since `ix = (fold(dy) | fold(dx)>>3) & $7E`. Deltas `$40`
and `$C0` close it, and the mode now prints a **warning** when `distinct < 17` so a future seed change
cannot silently re-blind it.

### Acceptance — eight facts, each reverted alone, each confirmed red

| reverted fact | score |
|---|---|
| walk tail's `STZ $58/$59` (the real defect) | 21120 |
| `ctAnimMode`'s CMP guard | 18140 |
| negative fold as a two's-complement negate | 14720 |
| shifting BOTH axes instead of only X | 10064 |
| diagonal forcing its facing unconditionally | 20624 |
| `9EA1` read as a conditional branch | 21760 |
| idle chain's `>$02FF` non-`$7F` arm writing mode 0 instead of nothing | 21860 |
| blink's `TSB $58` using the masked counter | 20240 |

## Grading

The project's `--sweepXXX` pattern: seed `$1D2C`/`$1D2D`/`$1D34`/`$1600`/`$1680`/`$1681`/`$1601`,
micro-execute `C0/9E84` to its RTS with `CtOracleRunToPC`, and compare every byte it writes
(`$1600`, `$1680`, `$1681`, `$1601`, `$0158`, `$0159`, `$01E5`). The seed cross-product must reach
**every distinct table slot**, including the `9FF2` default, and must include a case where the
actor is ALREADY in mode 1 (so `A175`'s early-out is graded) and both diagonal cases (already-a-
component vs not).

---

## 2026-09-07 — the routine was right and **nobody called it** (the interactive field lead)

User report (translated from Portuguese): *"crono, when RUNNING down (B + DOWN) and pressing LEFT, moves properly
to the lower-left diagonal, but in the original game the animation stays crono's
running-down one, and in ours it is changing to the left animation."*

Everything above was already ported and graded (`ct.exe --sweepanim`, 17/17 slots). The defect was a
**consumer** gap: `Belthasar`'s interactive loop runs `C0/881E` itself (`CtPlayerControl`) but never
reaches op `0xB0`'s state-0 arm for the lead, so `C0/9E29` — and with it `JSR C0/9E84` — never ran for
the player. `field.c` stood in with

```c
if (dx > 0) pfac = 3; else if (dx < 0) pfac = 2; else if (dy > 0) pfac = 1; else if (dy < 0) pfac = 0;
```

an **X-beats-Y priority that is not in the ROM**. It agrees with `$9FF7` on all four cardinals and on
a diagonal entered from a facing that matches neither axis, and disagrees on exactly the case the user
hit: the diagonal handlers *keep* a facing that already matches **either** axis.

### The two arms, measured on the oracle

`saves/truce_market.state`, lead is obj1 so its facing byte is `$7E1602`:

```
build/ct.exe --monitor "7E1602:1,7E1D2C:1,7E1D2D:1" --frames 40 \
  --from saves/truce_market.state --btnscript "0:6,21:14,61:20"
```

(serial button bits: `B`=$01, `Up`=$10, `Down`=$20, `Left`=$40 — so `21` = dash+Down, `61` =
dash+Down+Left, `11` = dash+Up.)

| drive | oracle |
|---|---|
| `0:6,21:14,61:20` — dash DOWN, then dash DOWN+LEFT | `$1602` 00 → **01** at f7, **stays 01** when `$1D2C` turns `E0` at f21 |
| `0:6,11:12,61:16` — dash UP, then dash DOWN+LEFT | `$1602` holds **00**, becomes **02** at f19 |

Same slot (`$A11D`), opposite outcome, decided by the facing already standing — which is precisely
`CMP #02 / BEQ / CMP #01 / BEQ / LDA #02 / STA $1600,X`. The walk magnitudes (`±$10`) index `$A0C9`
instead, identical shape, so this is **one** mechanism and not a run-specific case.

### The fix

`field.c` now calls `CtPlayerAnimSelect` — the routine above, unmodified — at the ROM's own seam:
immediately after `CtPlayerControl` published `$1D2C/$1D2D/$1D32/$1D33/$1D34`, on `g_collRam`, which
*is* this host's `C0/881E` RAM ( `$0197` = 02, the player slot, is already set there by
`fieldPlayerCollide`). Its `LDA $1600,X` input is seeded from the host's live `pfac` mirror and the
answer is read back out of `$1600 + 2`. The host still owns its own `$1680` model (the free-roam
`pwant` block) — that is a separate, documented derivation and was not touched here.

Pinned by `field-diagonal-keeps-run-facing` (dash DOWN then DOWN+LEFT → mode 06, `$1600` = 01) and
`field-diagonal-forces-horizontal` (walk UP then DOWN+LEFT → mode 01, `$1600` = 02). Four declared
mutations, all killed: restoring the X-first rule, dropping the handler's two `CMP` guards, dropping
its `LDA #a / STA $1600,X`, and not calling `C0/9E84` at all.

---

## 2026-09-07 (same day, follow-up) — the consequence: "when talking to GATO, nothing happens"

Reported right after the fix above landed. It is not a regression; it is the ROM, and the fix is what
made it visible.

### The two halves

1. **The diagonal KEEPS the facing** (the section above). Run DOWN and add LEFT and you are still
   facing DOWN — and releasing the d-pad does not change it, because `C0/9E84`'s index is then 0 and
   the `$9FF2` idle chain writes no `$1600` at all.
2. **The activate probes the FACED AXIS ONLY.** `C0/1D08` walks the object array and calls one box
   test per candidate with the lead's `$1600`: the faced axis must be signed the right way and within
   `#01C0` (28 px), the perpendicular axis within `#00C0` (±12 px). There is no second pass, no
   fallback direction, and the box is much narrower than a big sprite like GATO's.

So: walk down-left into GATO's right side, stop, press A — the probe looks DOWN, GATO is to the LEFT,
nothing happens. No song, no battle.

### Measured on the oracle, one pixel, one button, two facings

`saves/field_re/scene7_talk_obj13.state` (the pinned talk fixture: lead obj1 at (152.0, 239.94),
obj13 exactly 16 px above it), `build/ct.exe --monitor "7E1602:1,7E0129:1" --frames 40 --diff`:

| drive (serial bits: Left=$40, Up=$10, A=$100) | `$1602` | `$0129` |
|---|---|---|
| `40:1,0:6,100:2,0:31` — one frame of **Left**, then A | 02 | **00 for the whole run — no box** |
| `10:1,0:6,100:2,0:31` — one frame of **Up**, then A | 00 | 01 → 02 → 03 → 04 → 05 → 06 |

Same position, same press, different `$1600`. The ROM refuses the activate when you are not facing
the target.

### The port does the same

`--scene 6 --at 175 110 --walkscript "d:22,z:26,.:4,a:4,.:20,l:4,.:4,a:4,.:50"` — Crono runs DOWN then
DOWN+LEFT into GATO's right side and stops at (157,150), still facing 1:

```
[act f52] player(157,150) face=1 -> hit=-1     <- nothing happens
[act f84] player(157,150) face=2 -> hit=8      <- four frames of Left (he cannot move: GATO's
                                                  own solid box holds him) and the SAME press
                                                  from the SAME pixel opens the song box
```

Pinned as `field-activate-follows-facing`. Until this fix the port hid the rule: the invented
X-beats-Y facing turned every down-left approach into a LEFT facing, which happened to point at
whatever you had just walked past.

### And the encounter itself is fine

`field-gato-talk-starts-battle` walks Crono UP into GATO and presses A: the activate finds obj8,
obj0's autorun loop runs `EA 42` (the song) / `BB 03` / `E7` / `D9` / `D8 92 20`, and op 0xD8 stages
GATO into enemy **slot 3** (`$019D = 10 80 …`, back-map `$01A3 = FF FF FF 10 FF FF FF FF`) — the seat
every `--battletest` check then asserts against. Verified end to end on four approach directions, on
the real door entry from loc 439's left-edge exit, and seeded from the oracle's own pre-cutscene WRAM
(the port reaches the D8 at f65, the oracle at f73).
