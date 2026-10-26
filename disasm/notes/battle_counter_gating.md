# BATTLE — the WHEN-HIT COUNTER GATING rule (GATO attack-B): a PROXIMITY condition, now LIVE because GATO walks

Closes GAP 3 / FIX C of `battle_pacing_v2.md`. Derived from the ROM disasm (`tools/dis.py`, HiROM flat =
`(bank−0xC0)<<16|addr`, battle bank C1 = 16-bit M=0 X=0) + the LakeSnes oracle (`ct_probe.exe --monitor
--diff --writer` on `gato_press_action.state`) **only**. Nothing invented; the rule comes from the ROM +
the oracle **agreeing** on 11 PC hits across 4 fights.

> **THE RULE.** GATO's when-hit counter (attack-B, `$9877`=0x34) is his AI block-2, section-1, and that
> section is guarded by the condition **`1F 06`** (cond-op 0x1F, arg 0x06). The condition resolves the
> **nearest living PC** (targeting value 0x06) and passes **iff that PC is within Euclidean distance 48 px
> of GATO** at the counter-decision frame — exactly `dist² = dx²+dy² ≤ $9893 = 0x0900 = 2304`
> (`dx=|GATO.X−PC.X|`, `dy=|GATO.Y−PC.Y|`). If it passes, GATO counters that PC ~4 frames after the hit,
> **timer-independent** (not his ATB turn). If even the nearest PC is beyond 48 px, the condition fails and
> block-2 falls through to its message-only section → **NO counter**. The gate went **live** because
> **GATO relocates** (his autonomous block-1 approach/return walk during un-hit gaps): the oracle's
> un-countered ledger hit (f1769) landed after GATO had wandered from (136,118) to (128,95), putting both
> PCs ~50 px away — just past the 48-px ring. It is **NOT** random, **NOT** a busy/latch/once-per-turn
> gate, **NOT** an HP threshold (all ruled out below, §4).

---

## §1 — THE GATE, byte-exact (the ROM chain)

### 1.A GATO's AI script CC/A77B — block-2 is `[cond 1F 06] → counter ; [always] → message`
Raw dump (`rom[0xCA77B..]`), segmented with the `FD/BA4A[op]` length grammar (attack=4, tech=6, msg=2,
move-only 00=4; `battle_enemy_move_target.md` ADDENDUM):
```
BLOCK1 (autonomous)  @+0x00 : 00 00 00 00 FE | 01 00 05 01 · 00 00 06 06 · 02 10 05 00 00 00 FE | FF
BLOCK2 (when-hit)    @+0x15 : 1F 06 00 00 FE | 01 01 06 00 FE                 ← §1: IF(1F 06) → ATTACK-B(p1=01→$AE93=1, tgt=06 nearest, mode=00)
                              00 00 00 00 FE | 0F 03 FE | FF                  ← §2: [always] → MESSAGE 03 (no attack) → block end
```
Block choice = when-hit → block-2 (scan past the first `$FF`, `C1/B488`). The **only** substantive counter
is §1, and it is **conditional on `1F 06`**; §2 is the un-conditional no-op fallthrough that runs when §1's
condition fails.

### 1.B cond-op 0x1F = `C1/9765` — resolve NEAREST PC, then a mode-5 geometry test → `$9872`
`dis.py 19765 … 1 0` (cond handlers run M=8/X=16):
```
19765  JSR C1/AC14          ; resolve the targeting byte after 1F (=06) -> NEAREST living PC -> $AECC  (06 -> C1/A4AF via ($B8BB,X))
19768  LDA $AECB / BEQ 197A5 ; no valid target -> FAIL (set $AF24=1)
1976D  LDA $B252 / ADC #03 / STA $986F   ; $986F = GATO combatant idx (enemy slot+3)
19776  LDA #0E / STA $986E               ; $986E = 0x0E  (a SUB-MODE selector, NOT a threshold)
1977B  LDA $AECC / STA $9870             ; $9870 = the nearest-PC combatant idx
19781  LDA #05 / JSR C1/0003             ; mode-5 geometry -> writes $9872
19786  LDX $B1D2 / LDA $CC0001,X         ; the cond's 3rd byte (=00 for GATO)  -> BEQ 19796
19796  LDA $9872 / BNE 197A5             ; *** arg==0 path: PASS iff $9872==0, else FAIL ***
1979B  (PASS) LDA #01/STA $AECB/JSR C1/8C3E   ; leaves $AF24=0 (from STZ @AFE6) -> §1 action runs
197A5  (FAIL) LDA #01/STA $AF24               ; $AF24=1 -> §1 skipped, advance to §2 (message)
```
`$AF24`: **0 = condition PASSED (run §1 = counter)**, nonzero = FAILED (skip to §2). For GATO's `1F 06 00`
this reduces to **counter iff `$9872 == 0`**.

### 1.C `C1/0003` mode-5 → `C1/2986` → sub-0E `C1/2B2B` = Euclidean dist² ≤ 2304
`C1/0003` (A=5) is an indexed dispatcher → **`C1/2986`**:
```
12986  X=$986F(GATO)  Y=$9870(target); refresh $A039,X/$A050,X mirrors from $1D0C,X/$1D23,X (LIVE pos)
129A6  STZ $9872                         ; default 0 (= "in range")
129A9  LDA $986E(=0x0E) / ASL A / TAX / JSR ($2D81,X)  ; sub-mode 0x0E -> C1/2B2B
```
sub-0E **`C1/2B2B`** (`rom[0x12B2B] = A9 00 8D 93 98 A9 09 8D 94 98 80 0A …`) then the shared dist code at
`C1/2B41`:
```
2B2B  LDA #$00/STA $9893 · LDA #$09/STA $9894   ; $9893 (16-bit) = 0x0900 = 2304  ← THE THRESHOLD (=48²)
2B35  BRA 2B41
2B41  dx = |A039[GATO] − A039[tgt]|            (abs, 16-bit)
      dy = |A050[GATO] − A050[tgt]|
      $AF = dx*dx (C1/0089 hw-mul) ; += dy*dy   -> dist² = dx²+dy²
2BA5  CMP $9893 / BEQ 2BB5 / BCC 2BB5           ; dist² <= 2304 -> PASS
2BAF  LDA #FF / STA $9872 / RTS                 ; dist² >  2304 -> $9872 = 0xFF  (FAIL = out of range)
2BB5  STA $9872 (A=0) / RTS                     ; dist² <= 2304 -> $9872 = 0     (PASS = in range)
```
`sqrt(2304) = 48.0 px` exactly (= 3 tiles). **Counter iff the nearest living PC is within 48 px of GATO**
(inclusive; `dist²=2304` passes). Mode-5 refreshes the mirrors from the **LIVE** positions `$1D0C,X/$1D23,X`
each call, so the test uses positions AT the decision frame — which is why GATO's walking makes it vary.

---

## §2 — THE HIT TABLE (11 PC hits, 4 fights; ROM value + oracle agree)

Method: `ct_probe --monitor "1D0C:11,1D23:11,6130:2,9877:1,A013:1,AECC:1,AE93:1" --from gato_press_action.state
--input <mash> --frames 2600 --diff --writer <ROM>`; combatant bytes = cmb0 Crono / cmb1 Marle / cmb6 GATO
(X=`$1D0C+cmb`, Y=`$1D23+cmb`). A hit = `$6130` decrease (writer C1:ED1A); "counter" = `$9877→0x34` within
~6 f. Mash cadences: **A** `100:20,0:40` (ledger, A≈60f), **B** `100:12,0:20` (≈32f), **C** `100:16,0:100`
(≈116f), **D** `100:8,0:160` (≈168f).

| fight | hit f | GATO (x,y) | Crono dist² | Marle dist² | nearest | dist² ≤ 2304 ? | **counter?** | target (`$AECC`) |
|---|---|---|---|---|---|---|---|---|
| A | f768  | (136,118) | 845  | 1649 | Crono | **YES** | **COUNTER** | Crono (00) |
| A | f1151 | (136,118) | 845  | 1649 | Crono | **YES** | **COUNTER** | Crono (00) |
| **A** | **f1769** | **(128, 95)** | 2740 | **2560** | Marle | **no**  | **none** | — |
| B | f556  | (136,111) | 1093 | 1600 | Crono | **YES** | **COUNTER** | Crono (00) |
| **B** | **f1019** | (150,111) | 1345 | **676** | **Marle** | **YES** | **COUNTER** | **Marle (01)** |
| C | f768  | (136,118) | 845  | 1649 | Crono | **YES** | **COUNTER** | Crono (00) |
| C | f1147 | (136,118) | 845  | 1649 | Crono | **YES** | **COUNTER** | Crono (00) |
| **C** | **f1893** | **(135, 85)** | 3845 | **2357** | Marle | **no**  | **none** | — |
| D | f802  | (136,120) | 733  | 1681 | Crono | **YES** | **COUNTER** | Crono (00) |
| D | f1177 | (136,123) | 580  | 1744 | Crono | **YES** | **COUNTER** | Crono (00) |
| **D** | **f2320** | **(128, 80)** | 4525 | **3265** | Marle | **no**  | **none** | — |

**8/8 in-range hits countered; 3/3 out-of-range hits NOT countered — 11/11 on `nearest dist² ≤ 2304`.**
- The counter targets whoever it tests: **B/f1019 counters MARLE** (nearest 676, in-range) — proof the gate
  and target are both "nearest," not "the attacker" and not "lead PC."
- Tightest bracket: PASS observed up to nearest-dist²=**1093** (B/f556); FAIL down to **2357** (C/f1893,
  48.5 px — only 53 over 2304). The disasm value 0x0900=2304 lies inside `[1094, 2357]`; ROM + data agree.
- **Why the same hit type flips:** A/f768 vs A/f1769 are both Crono melees; the difference is purely GATO's
  position. Between the 2nd counter (f1377) and f1769 GATO's autonomous block-1 walk carried him
  (136,118)→(136,125)→(128,125)→(128,95) (pos trace `pos_capture.txt`) during the 250 f un-hit gap, so both
  PCs fell outside 48 px. Fast mash (fight B) keeps hitting him → he stays in block-2 (no walk) → close →
  always counters; sparse mash (C/D) gives him block-1 walk turns → he wanders → misses.

---

## §3 — THE REACTIVE DISPATCH PIPELINE (hit → ~4f → counter-or-nothing)

Ledger hit #1 timeline (`gate_capture.txt`/`pipe.txt`, `--writer`):
```
f768  $6130 76→60 (C1:ED1A)   ; PC-attack SPAN-END: HP applied, $A013 1→0 (C1:41BD)
f769  $AEC8=03 (C1:86D5)       ; executor selects the hit GATO (enemy slot 3); block2 positioned (C1/B488)
      $AE93 0→1 (C1:AD08)      ; cond-1F PASSED -> attack-B decoded
f772  $9877=0x34 (C1:44E7), $A013 0→1 (C1:40A9)   ; attack-B anim DISPATCHES  (hit + 4 f)
```
The chain, disasm-pinned:
1. **Trigger.** A PC hit lands (`C1:ED1A`) at the attacker's span-end. GATO carries a persistent
   *has-reaction* enable `$B2B6[slot]=01` (set once at battle start, `C1:8CF8`).
2. **Schedule.** The turn executor **`C1/8580`**, after processing the just-finished action, scans for an
   enemy needing to react and selects the hit GATO: `$AEC8=slot` (`C1:86D5`, @186CB), then **`JSR C1/B488`**
   positions his block-2 (`$B273/$B1D2` at the `1F` condition).
3. **GATE.** `C1/8749 JSR ($B80D,X)` evaluates the block-2 §1 condition = **cond-1F** (`C1/9765`, §1.B/1.C).
   `C1/876C LDA $AF24 / BNE →87AE`: if the proximity test **failed** (`$AF24≠0`), advance past `$FE` to §2
   (message) and loop (`187FB JMP C1/8706`) → **no attack**.
4. **Dispatch.** If it **passed** (`$AF24==0`), `C1/8780 JSR C1/8CF9` runs the §1 action = **attack-B**
   (action table `($B88D,X)`, op 0x01 → decode `$AEE3/E4/E5`, `$AE93=$AEE4=1` via `C1/AD02`), whose anim
   dispatches `$9877=0x34` ~4 f after the hit. Timer-independent: GATO's ATB `$AFB1` was 0x64/0x62/0x5A
   (≠0) at the three ledger decisions (`oracle_atb.txt`).
- The `~4 f` = span-end → f+1 executor reaction+cond-eval+action-decode → f+~4 anim reach (`C1/40A6`
  latency, `battle_pacing_v2.md §3.D`).
- At the **un-countered** hit the reaction **still ran** — cond-1F executed at f1770 (its `#0E` write,
  `C1:9781`, is on that frame) and **failed on distance** (`$AE93` written 0 by `C1:8D0D`, the no-action
  path). So it is not a missed/disarmed reaction; the executor reached the gate and the gate said no.

---

## §4 — THE OTHER CANDIDATES, RULED OUT (mission's suspect list)

- **Random roll — NO.** 11/11 deterministic on `dist² ≤ 2304`. The only RNG in the reaction path
  (`C1/8DE8 LDA #30 / JSR C1/AF22`) is the *fallback* that re-picks a target when the resolved one is
  dead/absent (`$AEFF,X==FF`) — never reached here; it does not gate the counter.
- **Busy / `$A013` / already-acting gate — NO.** Counters fire while GATO's own ATB is mid-cycle and during
  other combatants' freezes; the reaction ran even at f1769. No busy-state correlation.
- **Once-per-own-turn latch — NO.** 2–3 counters per fight; B/f1019 counters right after prior cycles. The
  per-slot reaction cursor `$B242,X` re-arms (→0) at *every* GATO action span-end (`C1:8C49`) and was armed
  at f1769 too — it is a "may react" state, not the gate (the gate is cond-1F, which failed on distance
  with the reaction fully armed).
- **HP threshold — NO.** Countered across GATO HP 76→11; the un-countered hits span the same HP range.
- **Mid-walk vs at-rest — INDIRECT.** Every hit lands while GATO is frozen at rest (during the PC attack);
  the gate reads his **rest position**, not a "walking" flag. Walking matters only because it *moves* that
  rest position out of the 48-px ring.
- **cond-1F distance — YES, this is it** (§1). The prior lanes left it always-pass because their captures
  never moved GATO out of range; with movement shipped it is live.

---

## §5 — FIX C SPEC (mechanism-shaped; where it inserts in our `battle.c`)

Our engine already has the state this needs: live positions `h->mvLiveX/mvLiveY[11]` (`battle.h:251`;
GATO=cmb6, PCs=cmb0/1/2), `aiHit[slot]` set on a hit (`battle.c:815`), and the movement port that walks
GATO (`battle_enemy_move_target.md` Wave-3). Two coupled changes:

**(C1) Implement the cond-1F proximity gate** — replace the stub `aiCondEval` `case 0x1F: return 1`
(`battle.c:390`) with the real test:
```
// cond 0x1F (C1/9765 -> C1/2986 mode-5 -> C1/2B2B): counter iff the NEAREST living PC is within 48px.
// nearest = min dist² over living PCs (matches the oracle's C1/A4AF pick in all captures, §2);
// dist² = (GATOx−PCx)² + (GATOy−PCy)² from the live foot positions (mvLiveX/Y), cmb6 = enemy slot 3.
int gx = h->mvLiveX[6], gy = h->mvLiveY[6], best = 0x7fffffff;
for (seat 0..2 if battleAlive) { d2 = (gx-mvLiveX[seat])²+(gy-mvLiveY[seat])²; best = min(best,d2); }
return best <= 0x0900;   // 2304 = 48²  (C1/2B2B threshold $9893)
```
This makes GATO's `1F 06` gate emerge; it is inert for enemies whose scripts never use cond-1F. Because it
reads `mvLiveX/Y`, it fires/suppresses exactly as GATO walks in/out of range (as far as our movement port is
byte-exact — the walk residuals of Wave-3 are the only slack, §6).

**(C2) Make attack-B REACTIVE, not ATB-timed** (the pacing GAP-3 half) — today GATO's counter only fires on
his ATB turn via `battleAiRunTurn` picking block-2 (`battle.c:454`). Mirror the ROM's reactive dispatch:
- At the `aiHit[slot]=1` site (`battle.c:815`), if that enemy's block-2 §1 is a conditional counter, arm a
  small `enCounterDelay[slot] ≈ 4` countdown (instead of waiting for `$AFAB==0`).
- Drain it in the tick; on expiry, run the block-2 §1 path: **evaluate the cond-1F gate (C1)** — if it
  passes, dispatch attack-B at the nearest PC via the existing `battleOpenAction` block-2 path; if it
  fails, do nothing (the ROM's §2 message no-op).
- Make GATO's block-2 *ATB* turn a no-op (`oracle_atb.txt` f1588: ATB-ready-while-hit = reload only, no
  span), so his damage comes only from the reactive counters.
- Target = the **nearest** living PC (the same min-dist² pick as the gate), NOT the lead-PC proxy at
  `battle.c:434` — the oracle counters Marle when she is nearest (B/f1019).

*Impact / re-pin:* GATO gains reactive counters (the −381 f GATO-span deficit of `battle_pacing_v2.md §2`);
`cronoHP` drops; re-pin `field-battleloop-gato` / `field-battlewait-gato`. MASH fights where GATO is always
adjacent counter every hit exactly as before the fix's ATB model did — but sparse/paced fights now correctly
**suppress** the counter when GATO has walked away (the behavior the old "counter every hit" comment at
`battle.c:382` got wrong). Add a regression check: a paced fight (`CT_BATTLE_SCRIPT` sparse) where GATO
walks off and a hit lands out of 48 px must produce **no** attack-B.

---

## §6 — BOUNDARIES / OPEN

- **The nearest-resolver metric (`C1/A4AF`) is un-disassembled**; its *pick* equalled `min Euclidean dist²`
  in all 11 captured hits, so the fix uses min-dist² for both the pick and the gate (consistent + oracle-
  matching). If a future capture shows `C1/A4AF` diverging from min-Euclidean at a 1–2 px tie, that pick is
  the residual — the cond-1F **test** itself is definitively Euclidean dist² ≤ 2304 (§1.C, byte-read).
- **Positional exactness of the gate depends on our movement port.** The RULE (≤48 px) is byte-exact; the
  exact frame GATO crosses the ring is only as exact as `mvLiveX/Y`, whose return-stop is the Wave-3
  geometric approximation (rest spots land at the derived ref, not the ROM's exact px). A hit that lands
  within a px or two of the 48-px boundary could flip vs the oracle until the walk is byte-exact.
- **Other enemies' cond-1F / other sub-modes.** The `C1/2D81` sub-table has 16 geometry sub-modes (0x00–0x0F,
  e.g. sub-04 = the same dist² with threshold 0x0400=1024=32², axis tests 06/07, atan test 08); only GATO's
  sub-0E (2304) is load-bearing here. Other enemies that use cond-1F with a different arg byte[2] (≠0) take
  the `$9872!=0` PASS polarity (C1/9765 @1978F) — decode per-enemy before generalizing.
- **`$B242,X` re-arm cadence** (the "may react" state, `C1:8C49` at each GATO span-end) was observed but not
  fully disassembled; it never gated any captured hit (the gate is cond-1F), so it is not load-bearing for
  FIX C, but pin it before modelling multi-reaction-per-turn enemies.

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/9e84b785-.../scratchpad/`
- `gate_capture.txt` — the ledger fight with the gate vars (`A013/9877/AE93/AF24/9872/986E:3/AECC/6130`,
  `--diff --writer`): the discriminator (countered = `$AE93→01` C1:AD08 / target Crono; un-countered f1770 =
  cond-1F body C1:9781 runs, `$AE93` stays 0 via C1:8D0D).
- `pos_capture.txt` — combatant positions (`1D0C:11,1D23:11,…`): GATO's (136,118)→(128,95) walk f1398–1624.
- `hits_A/B/C/D.txt` — the four cadences; the §2 hit table is auto-extracted from these.
- `pipe.txt` — the reactive pipeline (`6130/9877/A013/AE93/B2B6:4/B242:4/99D4:4/99D8/AEC8/95D5`, `--writer`).
- `ct_probe.exe` (=build/ct.exe), `SDL2.dll`, `gato_press_action.state`; ROM = repo root.

Repro (probe copied to scratch; ROM at repo root):
```
# the gate + the hit table (vary the mash cadence to move GATO):
ct_probe --monitor "1D0C:11,1D23:11,6130:2,9877:1,A013:1,AECC:1,AE93:1" --from gato_press_action.state \
  --input <('100:20,0:40')xN | ('100:8,0:160')xN> --frames 2600 --diff --writer <ROM>
# ROM sites:
python tools/dis.py 19765 0x48 1 0    # cond-1F C1/9765 (resolve nearest 06, mode-5, PASS iff $9872==0)
python tools/dis.py 12986 0x30 0 0    # mode-5 C1/2986 (STZ $9872, dispatch $986E=0x0E -> C1/2B2B)
python tools/dis.py 12B2B 0x40 0 0    # sub-0E C1/2B2B: $9893=0x0900=2304, dist²=dx²+dy² <= 2304 -> $9872=0
python tools/dis.py 18720 0xE0 0 0    # reaction pipeline: cond dispatch 8749, $AF24 gate 876C, action 8780=C1/8CF9
python -c "print('GATO CC/A77B',open('Chrono_Trigger_USA_SNES.sfc','rb').read()[0xCA77B:0xCA79A].hex())"
```

## §7 — 2026-10-04: THE COUNTER PATH AS THE ROM RUNS IT (C1/8626-87FB), replacing FIX C's stand-in

FIX C armed a counter per victim on DAMAGE (dmg > 0) and only for an enemy whose block 2 carried a non-zero condition
(`battleEnemyHasCounter`), and ran block 2 through the port's own `aiCondEval`. Enemy D9 (`00 00 00 00 / 02 76 05` -- an
always-true block 2) never countered: on the rule's cartridge its tech 14 lands on itself and action 76 answers at once.

**The arm ($B2C0).** Set after the action: a PC's command, after its C1/AC57, `LDA $AECC / CMP #03 / BCC` (C1/BB2D,
C1/BC4A, C1/C011 -- hit or miss, nothing reads the result); an enemy's turn, C1/8E8C-8EA3: the first victim an enemy (not
FF) and the action record's `$AE55` bit 3 clear, then `STZ $AE55`. C1/8626-8635 copies the victim list $AD8E to $B3AC and
`$B3B7 = $B18B`; C1/8640 `$2989 & 20` or `$B2C0 = 0` -> no counter (C1/8650 JSL FD/AC6E -> C1/881C); C1/8657 an enemy
actor with +1E bit 2 -> none.

**The loop (C1/8671-8814), $B315 = 0..7 over $B3AC.** A PC or FF -> next. A fallen enemy (`$AEFF` FF): C1/B488 on its
`$AF0A` id, and only when its block 2 opens with condition 20, `$AF02,X = $AF0D,X` (C1/86A5-86C6). C1/B488 (`$B1D2` =
CC/8B08[id], C1/B4AA's swap, past block 1's FF), `$B273 = $B1D0 = $B1D2`, `$B2D5 = $5E0D,X`; +1E & 8F -> next (C1/86FF).
Per section: `$B1CF = $AF24 = 0`, `$AEBB = $B16E,X`, `$B16E,X = $B3B7`, `$B239` = the condition, `$B1CF = 1` when a second
follows, `$B3CA = $B1D4[$B315 * 2]`, the condition handler(s) (C1/B80D, the eval's own: their commit C1/8C3E takes the
8C64 arm with $B2C0 set -- `$B1D2 = $B273`, the entry's eval op, `$B2B6 = 1`). `$AF24 = 0`: `$B16E,X = $B3B7`, **C1/8780
JSR C1/8CF9** -- the turn, which with $B2C0 set takes the entry at $B273 (C1/8D4F, no $B26B / $B1E4 test) and does not step
(C1/8E4D) -- `$B1D4[k*2] = $B3CA`, `$B24A[k] = $AF24`, `$AF24 = 2 -> $B2B6[k] = 0`. Otherwise past the section (its
conditions unless $AF24 = 2, then its entries); FF ends; `$B263[k]++`, `$B16E,X = $AEBB`. C1/87FE `$5E0D,X = $B2D5`,
C1/8809 JSR C1/B575, next victim; C1/881C `STZ $B2C0` before the eval.

**Condition 1F (C1/9765)** is now the walk's: C1/AC14's target (+1), none -> fail; C1/0003 mode 5 / 0E (C1/2986 ->
C1/2B2B, dist² from $986F = the enemy to $9870 = $AECC against 0900); polarity the +2 byte; pass: `$AECB = 1`, C1/8C3E.

**Timing (MEASURED, the rule's cartridge).** D9 (tools/battle_ab.py enemies D9): tech 14's close `$A013 = 00` (C1:41BD),
`$B2C0 = 01` (C1:8EA3), the counter's `$AEE3 = 02 76` (C1:9A55), `$AE93 = 76` (C1:AD08), `$AD89` (C1:DCED) and the
self-destruct's `$6130 = 0` (C1:F131) all on c530; `$A013 = 01` (C1:40A9) c553, `$0027` standing still between -- the
handler holds the main loop. GATO after a PC's hit (§3): the close f768, the decode f769, `$A013` f772. The port decodes on
the first free frame after the action (one more after a PC's command), holds the main loop and the dispatches
(battleThinkPending) and opens the counter's window CT_BATTLE_COUNTER_DELAY = 3 frames after the decode.

**Also found on the way.** Anim ops 37 / 38 (C1/5956 `DEC $5D80,X`, C1/5965 `DEC $5D9C`, both free) were not ported: action
76's track 0 blinks (`6C 00 04`) until `$5D80` comes back to 0, which its track 8's `37 00` does -- the window never closed.
The counter's action takes its turn's victim list (C1/C1DD: action 76's group 08 is every PC). And C1/8133 tests the defeat
(`$24`) before C1/8167's victory: action 76 kills the three PCs and D9 together, a defeat (c886; `$AE93 = 80` c1042).

Port: battle.c `battleCounterArmROM`, `battleCounterTurn`, `battleCounterDecode`, `battleReactiveCounterTick`, aiCond's
case 1F, battleAiTurn's $B2C0 branch. Graded: tools/battle_ab.py enemies D9 identical through change #15 (was #4: the port
never countered); the first difference is the defeat's own sequence (status wipe c990, port 1008). GATO's whole fight
(battle-gato-fight-vs-logical-oracle) and the status machinery around the counter path (battle-status-machinery-*)
unchanged. Census 222/255, no enemy lost; 19 improved (#10 -> #18).

## §8 — 2026-10-16: THE FALLEN VICTIM, AND THE DEATH SCAN AFTER THE LOOP (battle_sim census family 4)

Derived on the rule's cartridge with `tools/battle_sim.py` (any formation staged by op 0x83, every staged enemy's HP 1 at
f600, A mashed). Ported in `src/battle.c`; check `battle-sim-fallen-enemy-counter-vs-rule-cartridge`.

### 8.1 What the ROM does, in order (one pass of the ATB handler's tail, all on the action's closing frame)

```
C1/8626  JSR C1/B223          the dead-flag sweep: a victim killed by the action leaves the roster ($AEFF/$AF02 := FF,
                               C1/B2C3) and gets its death byte ($AEB0,X := 1, $AEB2 := 1, C1/B3AF)
C1/8671  for $B315 = 0..7 over the victim list $B3AC:
  8694     LDA $AEFF,X / CMP #FF: a FALLEN enemy -> 869B..86C6: its id from $AF0A (JSR C1/B488: block 2), and only if
           block 2 opens with condition 20 (86B4 CMP #20) is it LISTED AGAIN: 86C0 LDA $AF0D,X / STA $AF02,X
           (= $AEFF + combatant); any other block 2 -> C1/8809, no counter
  86FF     LDA $5E4B,X / BIT #8F (stop, lock, chaos, sleep, blind) -> C1/8809: the only gate after that
  8749     block 2's conditions; condition 20 (C1/97AB) holds when $AEB3,X -- the death byte -- is set
  8780     JSR C1/8CF9           the counter's TURN, and with it its whole action (the handler waits inside C1/4058)
  8787     JSR C1/B223           the sweep again: the fallen counterer leaves the roster for good
C1/881C  JSR C1/AC5E          `LDA $AEB2 / BEQ`, then `LDA #06 / JSR C1/0003` -> C1/006D `JSL CF/FE20`: the post-action
                               DEATH SCAN -- every corpse's routine CD/0043 -- only now, after every counter turn
         ... C1/8822 the eval C1/8C09
```

So condition 20 is "I have just died", and an enemy that has it fights back from the grave before it dissolves: the
re-listing makes its own side's tech groups find it (tech 7F's group `CC/86C9 = 01` is the enemies, C1/C37B), and the
whole counter plays before the corpse's routine starts.

### 8.2 Measured

| fight | cartridge |
|---|---|
| Mud Imp (0F), `20 / 02 7F 03` | f931 C1:B291 `$AF06 = 0F`, C1:B2C3 `FF`, C1:86C6 `0F`; C1:8D0D `$AE93 = 00`, C1:AD08 `7F` (same frame: no name); the action to f956; f957 C1:B291 / B2C3 `$AF06 = FF`; `$96FC = 00` f1000 (CF:FE7E, +43) |
| Azala (9A), `20 / 02 1F 05 00 00 43` | f1180 delist / re-list, `$AE93 = 00` (C1:8D0D); the name box `43` (C1/9B35 -> CD/0295, inside op 02's handler); `$AE93 = 1F` f1191 (C1:AD08); delist f1393 |

### 8.3 What the port did, and the fix

1. `battleReactiveCounterTick` dropped any counter whose enemy was dead (`battleAlive`, the +1D bit) -- a test the loop
   never makes. Gone; the loop's own gates (8694-86C6, 86FF) moved from the close's arm (`battleCounterArmROM`) into the
   victim's decode (`battleCounterDecode`), because the port's close runs its sweep after the arm: a re-listing done at
   the arm was undone by it, and the tech was dropped (C1/C72B) -- the Mud Imp took its third section's 6F.
2. The death scan waited for nothing: `battleAdvanceDissolves` started a corpse's routine at the HP write. Its start is
   now `battleDeathScanNext`, held while the counter phase is live (`battleCounterPhase`: a counter armed, decoded, in
   its name box or playing) -- C1/881C after the loop.
3. A counter's tech with a name box decoded at once. `battleCounterDecode` now posts the box and returns 2;
   `battleCounterDecodeRest` (C1/8DA4-8E15) runs when CD/0295 returns (`ctrMsgHold`), and the main loop's iteration
   holds meanwhile (`battleThinkPending`: the handler is inside it).
4. A counter's close ran no sweep: C1/8787's `JSR C1/B223` is now run there (`wasCounter`), with a lifeline revive it
   meets resumed the handler tail's way.
5. The close's own counter loop: so that a victim that does not counter does not hold a corpse for a frame (the forest's
   Cyclone double kill: the #95 post at f528), the decode runs right after `battleAdvanceAction`, before the scan
   (`battleReactiveCounterTick(h, 1)`); the name box and the countdown stay in the frame's regular pass, which skips the
   decode's own frame (`ctrDecodeF`).

### 8.4 Result (`battle_sim sweep` / `fight @...`, hp=1)

22 enemy ids and 38 formations tagged `fallen_counter` by the census: the WRAM's whole monitored sequence is the
cartridge's on 19 ids and 29 formations (Azala alone and in f12C_8 too in a 1450-frame window: at 1500 its party heal falls outside the cartridge's run). The rest are other families: D1 Giga Gaia, D4 Guardian and every fight with an
A1 Dalton or A6 Ozzie take the death routine's other arms (`CD/0E05` #38, `$99D2`/`$A08C`; census family 5); 9D Slash
the same; f0AE_10 differs at its staging (f273, `$5FCA`); f1BE_18 (Roly Bomber + Outlaw) stalls in the port on Crono's
physical at its strike (battle f575, no span end -- HEAD too, not this mechanism). Every match keeps a constant -4: census
family 3, the next action's menu pass one pass early.

Re-pinned with it: `tyranno-ff24-chain-plays-through-in-the-port`. The Black Tyranno (BC) has the same condition-20 block 2:
on the rule's cartridge (saves/tyranno_re/s301_c1_hp1.state, A every 30 frames) he falls at f718 (C1:ED12) and his tech
7F (`$AE93 = 7F`, C1:AD08, the same frame) takes Azala with him (C1:F12E on both enemies) -- the fight ends there. The
port, which dropped the counter, needed another action to kill Azala; the chain after the fight now runs 179-211 frames
sooner, the legs relative to each other unchanged.

### 8.5 The victims are $B3AC, not the action's list (2026-10-25)

The arm (battle.c battleCounterArmROM) walked the port's own victim list (`h->actTgtList`, the $AECC form) where the ROM
walks `$B3AC` -- C1/8629-8635's copy of `$AD8E`. The two part on an enemy turn on a header (C1/8E47 -> C1/AC57 with
$AECC's bit-7 form, C1/AC2A): its target lands in $AECC alone and the $AD8E copy is skipped, so the list stays FF and
nobody counters. MEASURED, the rule's cartridge (the playthrough's 33_escape, the prison guards' fight, `--cputrace`
f2611 / f2714): the guard's call of a second guard (header 04 02 7D, $AECC = 05) -- $B3AC all FF at C1/8682, C1/881C's
eval then walks slot 1 on id FF (no draw). The port took $AECC = 05, walked the new guard's block 2 as a counter
(battleCounterTurn -> C1/8CF9 cleared its $B2B6) and its eval drew on $26 once more (aiCond 17, C1/95E4): every roll
after it one along, Lucca's damage 2 for 1, and the party lost the fight the cartridge wins.
