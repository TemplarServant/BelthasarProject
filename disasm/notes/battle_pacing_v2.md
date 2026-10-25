# BATTLE PACING LEDGER V2 — the complete oracle-vs-ours frame accounting AT HEAD (bfdc812), every gap classified

Re-measures the GATO fight pacing at the **current HEAD** (the `$A013` action-freeze, the `CT_SPAN_*` law, the
commit/animate split, and the GATO walk are all SHIPPED since the old `battle_loop.md §E-L` ledger) and closes
the residual gaps the caller flagged. Method = LakeSnes oracle (`ct.exe`→`ct_probe.exe`) `--monitor` (RAM
timelines, `--writer` = PC after the store retires) on `gato_press_action.state`, vs our engine
(`Belthasar.exe --battletest` + `CT_BATTLE_TRACE`/`CT_BATTLE_MASHA`) on `saves/battle_re/gato_t0.raw.wram`.
Every claim is anchored to a ROM address (`tools/dis.py <fileoffset>`, fileoffset=(bank−0xC0)<<16|addr) or an
oracle monitor row. **⚠ `--peek` ≠ `--monitor` timelines — ALL frame work here is `--monitor`.** Mandate: nothing
invented; where the exact rule is the bank-CC AI interpreter it is marked BOUNDARY, never guessed.

> **HEADLINE.** The freeze/span machinery ported since the old ledger is FAITHFUL: span lengths, ATB cadence,
> intro, and victory-hold all byte-match the oracle. The residual "ours faster" (our MASHA fight **1650f** vs the
> oracle mash fight **1950f**) is **dominated by ONE structural fact: our GATO acts 1× where the oracle GATO acts
> 3×.** That splits into (i) a **byte-wrong enemy first-turn delay** — `CT_BATTLE_AI_FIRSTDELAY 0x48` was derived
> from a **misread** (GATO dispatches at f478, not f635; f635 is the attack-A SPAN-END) and over-delays his first
> action ~72 ticks; and (ii) the **un-modelled when-hit COUNTER** (GATO's attack-B fires ~4f after a PC hit,
> **timer-independent**, not on his ATB). The small per-action gaps (think-time 24f, enqueue/confirm latency ~6f,
> victory −10f) are real but sum to <100f. Full derivation + fix spec below.

---

## §1 — THE TWO-COLUMN LEDGER (aligned by EVENT: dispatch → span → HP → next)

Both fights driven to victory. **ORACLE** = `gato_press_action.state`, input `('100:20,0:40')×70` (A every 60f)
→ boot f219, ack f2169 = **1950f** (`oracle_ledger.txt`, identical to `battle_loop.md §G`). **OURS** = `field-
battleloop-gato` argv (`CT_BATTLE_MASHA=1`, auto-A the instant a command opens) → combat f159, victory f1650 =
**1650f** (`ours_trace.txt`). Frame origins differ (each side's own boot); align by span TYPE (`$9877`).

### 1.A ORACLE spans — 7 action spans (4 PC hits + 3 GATO actions)

| # | dispatch `$A013`0→1 | end `$A013`1→0 | span | `$9877` | action | HP (writer C1:ED1A) | **trigger** |
|---|---|---|---|---|---|---|---|
| — | — | — | — | — | INTRO f219→378 (159) + TICK f378→478 (100) | — | boot+HUD+`$993A` ramp; Crono ready f381, GATO ready f454 |
| 1 | f478 (C1:40A9) | f635 | **157** | 0x31 (C1:44BB) | GATO attack-A (walk-in) | Crono 70→61 (−9) | **ATB** (timer-0 f454 +24f think) |
| 2 | f643 | f768 | **125** | 0x00 (C1:43FF) | PC melee | GATO 76→60 (−16) | PC ready + confirm |
| 3 | f772 | f998 | **226** | 0x34 (C1:44E7) | GATO attack-B | Crono 61→47 (−14) | **COUNTER** (4f after f768 hit; timer=**0x64**≠0) |
| 4 | f1005 | f1151 | **146** | 0x0F (C1:4392) | PC (crit) | GATO 60→26 (−34) | PC ready + confirm |
| 5 | f1155 | f1377 | **222** | 0x34 (C1:44E7) | GATO attack-B | Crono 47→32 (−15) | **COUNTER** (4f after f1151 hit; timer=**0x62**≠0) |
| 6 | f1627 | f1769 | **142** | 0x00 (C1:43FF) | PC melee | GATO 26→10 (−16) | PC ready (250f recharge) + confirm |
| 7 | f1791 | f1938 | **147** | 0x0F (C1:4392) | PC | GATO 10→0 **KILL** (C1:ED12) | PC ready + confirm |
| — | f1938→2169 (231) | | | | VICTORY-HOLD → field-ack `$0117`→0001 (C0:18C1) | | fixed ~200f + A-dismiss + 11f handoff |

**Inter-span gaps** (end→next dispatch): **8** (f635→643, enqueue), 4 (counter reaction), **7** (f998→1005),
4 (counter), **250** (f1377→1627, Marle/Crono recharge), **22** (f1769→1791). GATO's 2nd *ATB* turn (timer-0
f1588) produced **NO span** — reload only (C1:84E1 f1593), absorbed (his block2 ATB turn is a no-op; his damage
comes from the reactive counters). See §3.C.

### 1.B OURS spans — 6 action spans (5 PC hits + 1 GATO action)

| # | dispatch (pc-act/enemy-act) | act-end | span | `$9877` | action | HP | **trigger** |
|---|---|---|---|---|---|---|---|
| — | — | — | — | — | INTRO f0→159 (`h->frames==CT_BATTLE_INTRO`, L1938) | — | combat-start f159; MASHA fires ready Crono at f168 |
| 1 | f168 | f293 | **125** | 0x00 | Crono melee | GATO 76→59 (−17) | PC ready (pre-charged) + MASHA (instant) |
| 2 | f368 | f514 | **146** | 0x0F | Marle | GATO 59→43 (−16) | PC ready (Marle 27→0) + MASHA |
| 3 | f624 | f749 | **125** | 0x00 | Crono melee | GATO 43→27 (−16) | PC ready + MASHA |
| 4 | f811 | f1035 | **224** | 0x34 | GATO attack-B | Crono 70→57 (−13) | **ATB** (timer-0 f811; NO counter) |
| 5 | f1123 | f1269 | **146** | 0x0F | Marle | GATO 27→11 (−16) | PC ready + MASHA |
| 6 | f1304 | f1429 | **125** | 0x00 | Crono melee | GATO 11→0 **KILL** | PC ready + MASHA |
| — | f1431→1650 (219) | | | | VICTORY: celebrate f1431→1634 (203) → dismiss f1639 → handoff f1650 | | §K faithful |

**Inter-span gaps** in ours are dominated by PC ATB **recharge** (75f, 110f, 62f, 88f, 35f) — the SAME 2.5f/tick
drain as the oracle's 250f gap, just distributed differently because MASHA fires PCs the instant they charge
(no menu-cadence delay). No small 4-8f enqueue gaps appear because a PC is never *already* ready at a freeze-end
in MASHA (they recharge from scratch between attacks).

### 1.C What MATCHES byte-for-byte (the freeze port is faithful — the bulk of the pacing is correct at HEAD)

| mechanism | ORACLE | OURS | verdict |
|---|---|---|---|
| PC melee span (`$9877`=0x00) | 125 (f643→768), 142 (f1627) | 125 (`CT_SPAN_PC_NORMAL`) | ✅ (ours = the 125 sample; 142 is a within-range variance, §6) |
| PC 0x0F span | 146 (f1005→1151) | 146 (`CT_SPAN_PC_RANGED`) | ✅ |
| GATO attack-A span (0x31) | 157 (f478→635), HOLD f1741→1899=158 | 157 (`CT_SPAN_GATO_A`) | ✅ |
| GATO attack-B span (0x34) | 226/222 (f772→998, f1155→1377) | 224 (`CT_SPAN_GATO_B`) | ✅ |
| GATO song span (0x10) | 311 (HOLD f1151→1462) | 311 (`CT_SPAN_GATO_SPEC`) | ✅ |
| ATB cadence | 2.53 f/tick (Marle 30 ticks f378→454=76f; gaps 4,2,3,2,3…) | 2.50 f/tick (`tickAcc+=2/≥5/-=5`) | ✅ |
| reload value `$AFAB←$B158` | Crono 0x48=72, Marle/GATO 0x66=102 (C1:BCEB/84E1) | 72 / 102 (`battleReload`) | ✅ |
| intro (boot→combat) | 159f (f219→378; §J: 82 boot/HUD + 77 `$993A` ramp) | 159f (`CT_BATTLE_INTRO`) | ✅ |
| victory hold | 231f (§K: ~200 fixed + A-skip + 11 handoff) | 219f (200 celebrate + A + 11) | ✅ (−12f, §3.E) |

---

## §2 — THE PACING ACCOUNTING (where the 300f went)

| class | ORACLE | OURS | Δ | note |
|---|---|---|---|---|
| INTRO | 159 | 159 | 0 | faithful (§J) |
| PC-action FROZEN | **560** (4 hits: 125+146+142+147) | **667** (5 hits: 125+146+125+146+125) | **+107** | ours needs 1 MORE PC hit (oracle got a −34 crit, ours no crit → RNG, not pacing) |
| GATO-action FROZEN | **605** (3: 157+226+222) | **224** (1: 224) | **−381** | **the core gap: GATO acts 3× vs 1×** (§3.A + §3.C) |
| TICKING (ATB advancing) | ~395 | ~379 | −16 | think-time + enqueue gaps ours skips (§3.B/§3.D) |
| VICTORY-HOLD | 231 | 219 | −12 | §3.E |
| **TOTAL** | **1950** | **1648** | **−302** | ≈ boot→ack vs combat→handoff |

**⇒ 274 of the 302 missing frames are the GATO-FREEZE deficit** (−381 GATO + 107 PC). Everything else (think-time,
enqueue, victory) is <30f combined. The freeze/span port is NOT the problem — **the enemy ACTION COUNT is.**

---

## §3 — PER-GAP DERIVATIONS (ROM mechanism byte-cited, not just the constant)

### §3.A — GAP 1: the enemy FIRST-TURN delay `CT_BATTLE_AI_FIRSTDELAY 0x48` is a **MISREAD** (corrects suspect a)

**The caller's suspect (a) "~60-80f enemy think time between ATB fire and dispatch" DOES NOT EXIST as stated —
it was the attack-A SPAN misattributed as a pre-action delay.** Byte-exact oracle proof (`oracle_atb.txt`,
GATO timer = `$AFB1` = `$AFAB`+6):

```
f378  g=1E (30)                 ; combat-start stagger (FD/B935 loaded 0x1F @f290 -> 0x1E after tick 1)
f454  g=00  (writer C1:826B)    ; GATO's ATB reaches 0  -- READY
f478  A013 0->1 (C1:40A9)       ; attack-A DISPATCHES  ($9877=0x31, C1:44BB)   <-- 24f after ready
f635  g=66  (writer C1:84E1)    ; the RELOAD ... = the attack-A SPAN-END ($A013 1->0, C1:41BD)
```

The old `battle_loop.md` ADDENDUM T2 read the **f635 reload** as GATO's "act" frame and called f454→635 (181f) a
"~72-logic-tick bank-CC AI FIRST-TURN delay while the timer sits at 0." **That 181f is [24f think + 157f frozen
attack-A span], not a delay.** GATO DISPATCHES at f478. Confirmed on BOTH the mash (`oracle_ledger.txt`) and the
isolated HOLD fight (`oracle_hold.txt`: attack-A f478, SONG f1151, attack-A f1741 — every attack-A dispatch is
timer-0 +24f, never +181f).

**Our engine bug (`src/battle.c:635`, `battleCombatStart`):**
```c
int load = ... : battleIsPc(i) ? 0x1E                         // other PC: 30 stagger  (correct)
              :                  0x1E + CT_BATTLE_AI_FIRSTDELAY;  // enemy: 0x1E + 0x48 = 0x66 = 102  (WRONG)
```
The enemy's initial ATB is loaded at **102** instead of **30** — a **+72-tick (~+180 real-frame) over-delay**.
In the MASHA fight this pushes GATO's ONLY ATB turn out to **f811** (by which point Crono has hit him 3× → he
picks block2 = attack-B), instead of an early attack-A. It is the single largest reason ours has fewer GATO
spans. (Note the comment at battle.c:615-621 that documents the 0x48 as the T2 "72-tick delay" fold — that
premise is now refuted.)

### §3.B — GAP 2: the REAL think-time = the arrival→dispatch handshake, **24f** (closes E.3, suspects a+b)

What actually runs in the f454→478 gap (`oracle_gato_walk.txt`, monitor `1D12/1D29`=GATO live X/Y, `9750`=mode,
`B1D2`=AI cursor, `986E`=distance):

```
f429-453  Y 6F->76 (111->118)   ; GATO's small approach, mover CF:F9C5 (7px, +1/segment) -- OVERLAPS the recharge
f454      Y=76, timer=0         ; ARRIVED + READY same frame
f456,469  986E 0200<->0201 (C1:2F5E)   ; per-frame bounding-box DISTANCE recompute (arrival test C1/28B0 inputs)
f461      mode-machine write (C1:384C) ; registers arrival / stops the walk (C1/3714->373B)
f475      B1D2 82A6->82A7 (C1:9A1E); AI cursor DECODES the attack entry -> $AEE3=01,00,05 (C1:9A25)
f478      A013 0->1 (C1:40A9)    ; DISPATCH  ($9877 written C1:44BB)
```
So the **24f** = [mode-machine confirms the C1/28B0 box-overlap arrival + halts the walk ~f454-461] + [the
bank-CC AI interpreter decodes the attack entry at cursor `$B1D2` ~f461-475] + [dispatch f478]. This is exactly
the E.3 "arrival→dispatch gap" (the converted-state fight measured ~42f, `enemy_move_target.md §A.6`; the value
is AI-state-dependent → the exact per-frame pacing is the **S4 bank-CC BOUNDARY**, but the 24-42f envelope is
pinned). **Our engine dispatches the enemy the SAME frame his timer hits 0 / he arrives** (`battle.c:2111`
`s[0xAFAB+i]==0` → immediate `battleOpenAction`; the walk arrival `battleArrive` dispatches on box-overlap with
no post-arrival hold) → **0f think-time**. Under-count ≈ 24f per enemy attack-A.

### §3.C — GAP 3: the when-hit COUNTER (attack-B) is **REACTIVE, timer-independent** — un-modelled (suspect e, the biggest)

**GATO's attack-B (0x34) is a hit-triggered counter, NOT an ATB turn.** Byte-proof (`oracle_atb.txt`, the smoking
gun is GATO's timer at the two attack-B dispatches):

| PC hit lands (`$6130`↓, C1:ED1A) | GATO attack-B dispatch (`$A013`0→1) | gap | GATO timer `$AFB1` at dispatch |
|---|---|---|---|
| f768 (76→60) | **f772** | **4f** | **0x64 = 100** (≠0 — NOT his ATB) |
| f1151 (60→26) | **f1155** | **4f** | **0x62 = 98** (≠0) |

His ATB reloads to 0x66 after attack-A (f635) and just DRAINS (66→64→62…) across the counters without gating
them. When his ATB *does* reach 0 (f1588, un-hit block1 phase) it produces **no span** (reload only, C1:84E1
f1593). So: **GATO's ATB fires block1 (attack-A/song); being-hit fires block2 (attack-B) reactively ~4f later.**
The `$AE93` A/B selector flips 0→1 at **C1:AD08** right after the hit (`f770`, `confirm_queue.md §B.4`), and the
attack-B dispatches immediately. **Not every hit is countered** (the f1769 hit → 26→10 was NOT countered; the
next action is the killing PC attack f1791) — the exact gating (why f768/f1151 but not f1769) is the **bank-CC
AI interpreter S4 BOUNDARY** (`enemy_move_target.md §B.1` "damaged-since-last-turn" block choice + the un-derived
cond-1F distance formula E.4).

**Our engine (`battle.c:2111`, step D):** GATO acts ONLY when `s[0xAFAB+i]==0` (ATB timer zero); when he acts,
`battleAiRunTurn` picks block2 if `aiHit[slot]` is set → attack-B. So our attack-B is **ATB-timed** (fires on his
own recharge, ~every 262f), never a reactive counter. Verified: driving ours at the oracle's slower cadence
(`ours_cadence_trace.txt`, `a:1,n:29`×60) still yields **1 GATO span** (f811 ATB attack-B), never a post-hit
counter. **This is why the oracle gets 3 GATO spans and ours gets 1.** (The engine comment at battle.c:382-384
claims the counter "fires UNCONDITIONALLY after a hit" and that ATB-modelling reproduces it — the oracle refutes
both: it is reactive-timed AND conditional.)

### §3.D — GAP 4: enqueue latency + PC confirm→animate, **~6f oracle vs ~0-3f ours** (suspects c+d)

One shared mechanism: after any dispatch trigger (a freeze-end freeing the scheduler, or a PC confirm), the
action-execution routine `C1/4090` (`INC $A013` @C1/40A6) is reached only after the scheduler re-selects/enqueues.

- **Enqueue latency** (freeze-end → next-ready-actor dispatch): oracle **8f** (f635→643), **7f** (f998→1005) —
  the readiness detector `C1/1B19` posts `$95D6`, enqueue `C1/1B67` consumes it, `$95D5`/`$A6DD` advance, then
  `C1/4090` dispatches (`battle_loop.md §S2`).
- **PC confirm→animate**: oracle **6f** — un-frozen confirm f427 (`$9609`→0, C1:1629) → animate f433 (`$A013`→1,
  C1:40A9) (`atb_reload.md §A.1`); mid-freeze confirm f524 → animate f644 = freeze-end f636 + **8f**
  (`confirm_queue.md §B.2`).

**Ours:** `battleCommitPcAction` + `battleDispatchScheduled` run the SAME tick (MASHA: pc-commit f168 = pc-act
f168 = 0f); the scheduled dispatch is on the next free frame (~0-3f, `confirm_queue.md §S2` "~3f ours vs ~8f
oracle"). Under-count ≈ 4-6f per action transition.

### §3.E — GAP 5: victory hold, **231f oracle vs 219f ours** (−12f, suspect e)

Oracle kill f1938 → field-ack f2169 = **231f** (§K: fixed ~200f celebration [dissolve+pose+fanfare+results-
populate, victory-render BOUNDARY] + A-dismissable results hold + ~11f handoff C0:18C1→A730→3AB1). Ours (`battle.c`
`victoryHold`, `CT_BATTLE_VICTORY_CELEBRATE=200`; `field.c battleEndPoseRender` + results box): kill f1429 →
celebrate to f1634 (203f) → results-advance → dismiss f1639 → handoff +11 → f1650 = **221f** end-to-end (219f
from the victory-latch f1431). Faithful port of §K; the −10-12f is inside the fixed-celebration BOUNDARY (the
oracle's ~200f is `≈214−11 handoff` from a mash-A capture, ours pins 200 exactly). Sub-pixel; not the "fast"
complaint.

---

## §4 — FIX SPECS (where each inserts in `battle.c`; expected re-pin impact)

Ordered by pacing impact. Fixes **A/C change the fight** (GATO acts more → re-pin `field-battleloop-*`, cronoHP
drops); fixes **B/D/E are pure per-frame gates** (damage/order byte-identical). Per-hit DAMAGE FORMULA is
untouched by all five.

**FIX A (biggest, byte-correct) — remove the `0x48` enemy first-turn over-delay.** `battle.c:635`: load the enemy
initial ATB at the **plain 0x1E stagger** (same as a PC), NOT `0x1E + CT_BATTLE_AI_FIRSTDELAY`. Delete/retire
`CT_BATTLE_AI_FIRSTDELAY` (the 72-tick fold is the refuted T2 misread, §3.A). Enemy `$9F22`=0 unchanged.
*Impact:* GATO's ATB turn arrives ~72 ticks (~180f) earlier → he acts more often (an early attack-A in
interactive play; ≥2 ATB turns in MASHA) → the fight stretches toward the oracle. Re-pin `field-battleloop-gato`
/ `field-battlewait-gato` (victory frame later, cronoHP lower). ⚠ Re-verify `field-battletest-gato` (debug-end
@f60, pre-combat — should be UNCHANGED) and the HOLD-fight walk pins (`field-battle-gato-walk` — GATO's
autonomous cycle timing shifts).

**FIX B (pure pacing) — insert the ~24f enemy arrival/think handshake.** Between an enemy becoming ready+arrived
and `battleOpenAction`, hold a fixed **~24f** (the C1/28B0-arrival → C1/40A6-dispatch handshake, §3.B/E.3). In
`battle.c`: for the walk-in path, delay `battleArrive`'s dispatch by `CT_BATTLE_ENEMY_THINK≈24` frames after the
box overlaps; for a non-walk enemy action, delay `battleOpenAction` (step D, battle.c:2111) by the same after
`$AFAB==0`. *Impact:* +24f per enemy attack; no damage/order change (the enemy just idles at arrival, as the
oracle does f454-478). Pins shift by a few frames → re-pin.

**FIX C (structural, BOUNDARY-gated) — the reactive when-hit counter.** Model attack-B as fired **~4f after a
qualifying PC hit**, timer-independent, instead of on GATO's ATB (§3.C). In `battle.c`: when a PC hit lands on an
enemy (the `aiHit[slot]=1` site, battle.c:815), if that enemy's block2 is a counter, SCHEDULE its attack-B
dispatch ~4f later (a small `enCounterDelay[slot]` countdown drained in the tick, dispatching via the existing
`battleOpenAction` block2 path) rather than waiting for `$AFAB==0`; and make his block2 *ATB* turn a no-op
(oracle f1588: ATB-ready while hit → reload only). *Impact:* the dominant stretch (adds the 448f of counter
freeze the oracle has). **⚠ CHANGES DAMAGE/ORDER** (GATO hits Crono after every counter-worthy hit) → cronoHP
drops substantially; re-pin. **⚠ BOUNDARY:** the counter-GATING (the oracle countered f768/f1151 but NOT f1769)
is the un-derived cond-1F / block-choice rule (S4, `enemy_move_target.md E.4`). Do NOT ship "counter every hit"
(that would OVER-fire vs the oracle's 2-of-3) — this fix needs the gating derived first, or a captured
multi-hit counter sequence to pin the condition. Until then it stays a documented boundary; FIX A alone
recovers most of the GATO-span deficit without inventing the gate.

**FIX D (pure pacing) — the ~6f enqueue/confirm→animate latency.** Add a ~4-6f delay between a dispatch trigger
(freeze-end scheduler free, or PC confirm-commit) and the `$A013`-set animate, mirroring `C1/4090`'s reach
latency (§3.D). In `battle.c`: gate `battleDispatchScheduled` (and step-D `battleOpenAction`) behind a small
`dispatchDelay` counter set when the trigger fires. *Impact:* +4-6f per action; no damage/order change; re-pin.

**FIX E (pure pacing, cosmetic) — victory hold +~10f.** Raise the fixed celebration or add a ~10f post-results
settle to reach the oracle's 231f (§3.E). Lowest priority (inside the victory-render boundary). Re-pin the
victory frame only.

**Net expected shape:** FIX A + (eventually) FIX C move the GATO-span count 1→3 (the −381f core); B+D+E add the
<30f of per-action polish. Ours moves from ~1650f toward the ~1950f canon **for the SAME damage pipeline** (each
hit's value computed identically; only the count/timing of GATO's participation grows).

---

## §5 — USER-FACING FEEL ITEMS (part 4): FAITHFUL at HEAD

- **Results-window A-pacing** — MATCHES §K. Our `battle.c:2066-2080`: after the fixed `CT_BATTLE_VICTORY_CELEBRATE`
  (200f) the results lines advance ONE per A-edge (`results-advance`), then `victory-dismiss`→`handoff`(11f). The
  masher's A-edges "only dismiss AFTER the fixed phase" (battle.c:2068) = the oracle's ~200f non-skippable +
  A-dismissable results (§K's mash-A f950→f1164=214f envelope). Trace: celebrate f1431→1634 (203f) → advance
  f1634 → dismiss f1639 → handoff f1650. ✅
- **Intro hold** — `CT_BATTLE_INTRO=159` = the oracle's boot→combat 159f (§J: 82 boot/HUD + 77 `$993A` ramp). ✅
- **Victory hold** — 219-221f vs oracle 231f; faithful §K envelope, −10-12f inside the render boundary (§3.E). ✅
- **ATB bar fill rate** — the 2.5f/tick cadence + 72/102 reloads + the `$A013` freeze all match
  (`battle_timing_audit.md §1`, re-confirmed here §1.C): the bar charges at the oracle's wall-clock rate. ✅

---

## §6 — OPEN / BOUNDARIES

- **The counter GATING (§3.C / FIX C)** — why the oracle counters some PC hits and not others (f1769) is the
  bank-CC AI interpreter (cond-1F distance + block-choice, `enemy_move_target.md E.4`). Un-derived; a captured
  multi-hit counter sequence is needed before FIX C can be byte-exact. The reactive TIMING (~4f post-hit,
  timer-independent) and the 2-of-3 in this fight are pinned.
- **PC melee span variance** — the oracle shows 0x00 = 125f (f643) AND 142f (f1627); our fixed 125 = the shorter
  sample. The +17f is likely the acting-seat / menu-nav tail folded into the span (Marle-melee vs Crono-melee);
  un-decomposed. Within the §G "125-157f" envelope.
- **The `$9877`=0x0F −34 crit (oracle span 4)** — the anim selector (0x0F, span 146) and the −34 damage are
  decoupled (anim cosmetic, damage separate); which PC/how it crits with the crossbow anim is not decoded. Not
  load-bearing for pacing (span 146 matches).
- **Enemy think-time exact pacing (§3.B)** — 24f (mash) / 42f (converted state); the per-frame make-up (mode-
  machine stop + AI cursor decode) is cited but the interpreter's frame budget is the S4 boundary. Pinned as a
  24-42f envelope.
- **Absolute-frame alignment** — the two fights have different boot origins and (because MASHA fires PCs instantly
  vs the oracle's 60f mash cadence) different action ORDER; the ledger aligns by span TYPE, not absolute frame.
  A cadence-matched capture (driving ours at the oracle's exact A-timing) still diverges structurally (§3.C
  proof) — the divergence is the GATO action count, not the input.

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/9e84b785-.../scratchpad/`
- `oracle_ledger.txt` — the 1950f oracle ledger (A013/95D5/9877/9876/HP/0117, `--diff --writer`).
- `oracle_atb.txt` — full ATB-timer timeline (`AFAB:8`+A013+95D5+A6DD+95D6): the counter proof (GATO timer 0x64/
  0x62 at f772/f1155) + the 30-stagger drain + the f1588 absorbed ATB turn.
- `oracle_gato_walk.txt` — GATO live pos/mode/timer through attack-A: the 24f think-gap makeup (C1:384C/2F5E/9A1E).
- `oracle_hold.txt` — the HOLD fight (attack-A f478 / SONG f1151 / attack-A f1741): attack-A dispatch = timer-0+24f.
- `ours_trace.txt` / `ours_cadence_trace.txt` — our MASHA (f1650) and slow-cadence (f1762) fights: 6 spans, GATO
  acts 1×.
- `ct_probe.exe` (=build/ct.exe), `ct_field_probe.exe` (=build/Belthasar.exe), `SDL2.dll`, `gato_press_action.state`.

Repro (probe/field copied to the scratchpad; ROM = repo root):
```
# ORACLE ledger + ATB timers:
ct_probe --monitor "A013:1,95D5:1,9877:1,9876:1,6130:2,5E30:2,5EB0:2,0117:2" --from gato_press_action.state \
  --input <('100:20,0:40')x70> --frames 2400 --diff --writer <ROM>
ct_probe --monitor "AFAB:8,A013:1,95D5:1,A6DD:1,95D6:1" --from gato_press_action.state --input <mash> --diff --writer
# ORACLE the 24f think-gap + HOLD:
ct_probe --monitor "1D12:1,1D29:1,9750:1,AFB1:1,A013:1,986E:2" --from gato_press_action.state --input <mash> --diff --writer
ct_probe --monitor "A013:1,9877:1,1D29:1,9750:1,AFB1:1,95D5:1" --from gato_press_action.state --input "100:8,0:2600" --diff --writer
# OURS (MASHA + slow-cadence):
CT_BATTLE_MASHA=1 CT_BATTLE_TRACE=1 CT_ROM=<ROM> ct_field_probe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
CT_BATTLE_SCRIPT=<('a:1,n:29')x60> CT_BATTLE_TRACE=1 CT_ROM=<ROM> ct_field_probe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
# ROM sites:
python tools/dis.py 18260 0x40 1 0   # ATB dec + ready (C1:826B)   ;  python tools/dis.py 14090 0x140 0 0  # action exec (C1/40A6 INC $A013)
python tools/dis.py 184DB 0x10 0 0   # enemy reload $AFB1<-$B158 (C1:84E1, at SPAN-END not first-turn)
```
