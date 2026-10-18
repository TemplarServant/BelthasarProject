# BATTLE — target-select CONFIRM during another action's animation (the "sometimes A does nothing" drop)

User report (translated from Portuguese): *"sometimes, when the pointer is on GATO, pressing ACTION does nothing. the attack
move should be **scheduled**."* — sometimes, with the target cursor on GATO, pressing A (confirm) does
nothing; canonically the confirmed attack should be SCHEDULED and fire when the current action finishes.

**Verdict (both halves proven):**
- **(A) OUR BUG is real and reproducible** — a *boundary race* in the deferred-confirm path. `battleMenuFrozen`
  (src/battle.c) does NOT commit the confirm; it merely LATCHES it into the shared `h->pendEdges` A-bit, which
  the tick gate drains into `step F` — a path that is unreachable / already-consumed on **exactly three frames
  per enemy action span** (the two GATO-dispatch frames + the one span-end frame). A confirm pressed on any of
  those frames is **silently and permanently dropped**; the PC stays in target-select and must press A again.
- **(B) The CANONICAL mechanism is: the confirm is registered IMMEDIATELY, during the freeze** — the ROM's
  target-select confirm handler runs every frame *independent of the `$A013` action freeze*: it closes
  target-select (`$9609`→0), dequeues the confirming PC (`$A6D9[seat]`→FF), and advances the head/current actor
  (`$A6DD`/`$95D5`) the instant the A edge is seen. The **animation** of the committed action then fires when the
  currently-animating action's freeze ends. There is **no latch-then-drain and no boundary hole** in the ROM.

Method: our engine driven headless via `CT_BATTLE_SCRIPT`/`CT_BATTLE_TRACE` on `Belthasar.exe`
(`saves/battle_re/gato_t0.raw.wram`); the oracle driven via `ct.exe --monitor --btnscript` on
`gato_press_action.state` (+ `saves/battle_re/gato_wait.state` for Wait mode) + `tools/dis.py`. All addresses are
ROM/oracle-cited; writer PCs = the PC after the store retires.

---

## (A) OUR BUG — the deferred-confirm BOUNDARY RACE (src/battle.c)

### A.1 The code path that drops the A

The target-select confirm reaches TWO different places depending on whether an action is animating:

**Not frozen** (`$A013`==0) — the tick gate `step F`, target-select branch (`CtBattleTick`, src/battle.c:1534-1543):
```c
} else {                              // --- TARGET-SELECT ($9609==1)
  int dir = ...;
  if (tA) { s[0x9609] = 0; battleOpenAction(h, cur); battleTrace(h,"pc-act"); }  // A -> DISPATCH
  else if (tB) { s[0x9609] = 0; ... }                                            // B -> CANCEL
}
```
`tA` here is drained from `h->pendEdges` at the tick (src/battle.c:1411-1414), and this whole block is gated on
**`if (!h->actActive)`** (src/battle.c:1494).

**Frozen** (`$A013`==1) — `battleMenuFrozen` is called from the freeze gate (src/battle.c:1400) and its
target-select branch merely DEFERS (src/battle.c:637-642):
```c
} else {                              // TARGET-SELECT: enemy nav + confirm(DEFER)/cancel
  int dir = ...;
  if (aE)      h->pendEdges |= CT_BIN_A;                 // CONFIRM: DEFER -- step F dispatches at freeze-end
  else if (bE) { s[0x9609] = 0; ... }                   // B cancels immediately
}
```

So the confirm is never *committed* while frozen — it is parked in the shared `pendEdges` A-bit and is only ever
acted on later by `step F` (the not-frozen tick path), which is gated on `!h->actActive` and reached only via
the logic-tick drain. **Three frames per span** make that hand-off fail:

| drop frame | what happens | why the confirm is lost |
|---|---|---|
| **GATO-dispatch tick** (our f415) | `step D` (src/battle.c:1467) opens the enemy action → `actActive=1`, `$A013=1` (`battleOpenAction`, src/battle.c:1150) **before** `step F` runs that same tick | the confirm was drained from `pendEdges` on this tick (`tA=1`, `pendEdges=0` at src/battle.c:1414) but `step F` is skipped by `if(!h->actActive)` (src/battle.c:1494) → the drained A is discarded |
| **frame before that tick** (our f414) | the A edge is latched into `pendEdges` here, its *draining* tick is the f415 dispatch tick | same as above — drained on the dispatch tick, `step F` skipped |
| **span-end frame** (our f572) | at frame top `$A013`==1 so the top-of-frame latch is **skipped** (`if(!s[0xA013])`, src/battle.c:1362); then `battleAdvanceAction` clears `$A013` mid-frame (src/battle.c:1208); then the freeze gate `if(s[0xA013])` is **also** skipped (src/battle.c:1400) → `battleMenuFrozen` is NOT called | the frame's raw A edge is neither latched (top-latch skipped) nor handled (freeze pass skipped); `pendEdges` never receives it → lost |

Every frame from **416 through 571** works (the confirm is latched by `battleMenuFrozen` mid-freeze and drained
cleanly at freeze-end → dispatch at f574). Only the 3 boundary frames drop it. That narrow window is exactly the
user's *"sometimes"*.

### A.2 Reproduction (deterministic, our binary)

Copy `build/Belthasar.exe` + `build/SDL2.dll` to a scratch dir (a running `build/Belthasar.exe` locks the build).
Script grammar: `btn:frames` where `btn ∈ {a,u,d,l,r,b,n}`; index `p = h->frames - CT_BATTLE_INTRO(159) - 1`.
The first `a` (p=20 → frame 180) enters target-select; the second `a` is the confirm.

```
# DROP — confirm on the span-end frame 572 (n = 572-181 = 391):
CT_BATTLE_SCRIPT="n:20,a:1,n:391,a:1,n:500" CT_BATTLE_TRACE=1 \
  Belthasar.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
#   -> pc-act count = 0   (Crono stranded in target-select, $9609=1, never attacks; runs to the f4000 debug cap)

# WORKS — confirm one frame earlier (571, n=390):
CT_BATTLE_SCRIPT="n:20,a:1,n:390,a:1,n:500" ...
#   -> battletrace f574 pc-act cur=1 A013=1 9609=0   (dispatches right after GATO's freeze ends f572)
```

Trace excerpt (the two runs are byte-identical up to the confirm frame):
```
confirm @ f571 (WORKS):                          confirm @ f572 (DROP):
 f180 target-select cur=0 A013=0 9609=1            f180 target-select cur=0 A013=0 9609=1
 f415 enemy-act ... cur=0 A013=1 9609=1            f415 enemy-act ... cur=0 A013=1 9609=1
 f572 act-end       cur=0 A013=0 9609=1            f572 act-end       cur=0 A013=0 9609=1
 f574 pc-act        cur=1 A013=1 9609=0    <-- HP   (no pc-act ever; 9609 stays 1)
```
A single-frame sweep of the confirm frame over 410..580 gives DROP at **exactly {414, 415, 572}**, dispatch
(pc-act f574) for every frame 416..571, and normal pre-freeze dispatch for ≤413.

### A.3 Secondary fidelity gap (same root cause)

Even for the frames that DO work (416-571), our engine keeps `$95D5=0` (Crono) and `$9609=1` through the whole
freeze and only advances at freeze-end (f574). The ROM advances `$95D5`→Marle and clears `$9609` **during** the
freeze (see B.2, f524). So even the "working" case mis-models the mid-freeze state — the next PC's command menu
should already be open while the current action animates.

---

## (B) THE CANONICAL MECHANISM (ROM disasm + oracle frame evidence)

### B.1 The confirm handler (runs every frame, independent of `$A013`)

`tools/dis.py 11600 170` (bank C1) — the target-select confirm path:
```
1626 DEC $9609              ; CLOSE target-select               (writer C1:1629)
1629 LDA $95DB / BNE ...    ; ($95DB!=0 = a Tech/Item submenu variant; the plain Att path is $95DB==0)
...
1725 LDA #FF               ;
1727 STA $A6D9,X           ; clear the CONFIRMING seat's ready-queue slot  (X = confirming seat)
172A TDC / TAX             ; X = 0
172C LDA $A6D9,X           ; scan the fixed 3-seat queue for the next occupied slot
172F STA $A6DD             ; queue HEAD = next seat            (writer C1:1732)
1732 STA $95D5             ; CURRENT actor = next seat         (writer C1:1735)
1735 BPL ->11745           ; valid seat (>=0) -> done; else advance/wrap (empty -> 0xFF, C1/1745 STZ $99E0)
```
So the confirm = **DEC `$9609`** (close) + **`$A6D9[seat]`←0xFF** (dequeue the confirming PC) + **`$A6DD`/`$95D5`←
next queued seat** (advance head/current, or 0xFF if the queue empties). This is byte-for-byte our
`battleQueueRemove` + `$95D5 = battleQueueHead` — the difference is purely *when* it runs (see B.2).

### B.2 Oracle: the confirm registers IMMEDIATELY, mid-freeze; the action animates at freeze-end

```
ct.exe --monitor "9609:1,95D5:1,A6DD:1,A013:1,AE93:1,6130:2,5E30:2,AFB1:1" \
  --from gato_press_action.state --btnscript "100:8,0:392,100:6,0:114,100:6,0:260" --diff --writer
```
(boot A; idle; A@f400 = Att→target-select; idle; **A@f520 = confirm, mid-freeze**; observe). Key rows:

| frame | event | evidence |
|---|---|---|
| f402 | Crono enters target-select | `$9609`=01, `$9614`=00 (GATO) |
| f478 | GATO's action freeze STARTS | `$A013`=01 (writer **C1:40A9**); ATB `$AFAB..`, `$AFB1`(GATO) all FREEZE |
| **f524** | **CONFIRM (during the freeze)** | **`$9609` 1→0 (C1:1629)**, **`$A6DD` 0→1 (C1:1732)**, **`$95D5` 0→1 (C1:1735)** — all while `$A013`==01. Crono is dequeued, Marle(1) becomes the head/current chooser, target-select closes. **The action is COMMITTED, not yet animated.** |
| f636 | GATO's freeze ENDS | `$A013` 1→0 (C1:41BD); GATO's own damage applies — Crono `$5E30` 46→3D (70→61, −9, C1:ED1A); GATO timer `$AFB1`←66 (C1:84E1) |
| **f644** | **Crono's SCHEDULED attack animates** | `$A013` 0→1 (C1:40A9) — the committed attack starts, ~8f after the previous freeze ended (enqueue latency) |
| f769 | Crono's attack applies | GATO `$6130` 4C→3C (76→60, −16, C1:ED1A) — the confirmed-during-freeze attack DID land |

So the ROM answer to the task's question is unambiguous: **the confirm registers during the freeze (the queue
write / dequeue happens immediately at f524), and the action fires at freeze-end (f644).** It is *never* ignored
and *never* needs re-pressing.

**What records the confirmed-but-waiting action (f524→f644).** Monitoring the action record
`$9877`/`$AE93`/`$AEE3`/`$B1F4` across the whole window shows they **do not change at the confirm** — they hold
GATO's *in-flight* action values (`$9877`=0x31 GATO-attack-A, `$AEE3`=`01 00 05`, `$B1F6`=00=target Crono) from
f478 straight through to f636, and Crono's own record (`$9877`, damage stage `$B328`, target `$B1F6`) is written
only when his action ANIMATES at f644. So the confirm does NOT stage the attack record; the "waiting" state is
carried by **(a) Crono removed from the command ready-queue** (`$A6D9[0]` 00→FF at f524, `$A6D9`=`FF 01 …`,
head `$A6DD`→1, count `$A6DE` 2→1) **+ (b) his ATB timer still 0** (`$AFAB`[0]=00, frozen ready) **+ (c)
target-select closed** (`$9609`=0). The confirm handler also touches a per-combatant region (`$A862`/`$A863,X`/
`$A866,X`/`$A0D8,X`, C1/1644-1748) that plausibly holds the chosen target/action, but its exact contents were not
decoded (OPEN). The practical consequence for our port: **stage the attack + draw the damage RNG at ANIMATION
time, not at confirm time** — the ROM does exactly that (record written at f644, not f524).

### B.3 No boundary race in the ROM (the ROM's whole point)

Pressing the confirm at the freeze BOUNDARIES still registers cleanly — the ROM has no latch-then-drain-on-tick
structure to create a hole:
- confirm just BEFORE the freeze (`...100:6,0:66,100:3,0:230`): registers at f473 (`$9609`→0, `$95D5`→1, `$A6DD`→1),
  `$A013`=0.
- confirm at the freeze-END boundary (`...0:228,100:5,0:260`): GATO's freeze ends f635, confirm registers f637
  (`$9609`→0, `$95D5`→1, `$A6DD`→1), Crono's attack then animates f643→f768 (GATO 76→60).

The confirm handler runs on a per-frame path that is **not** gated by `$A013` (the freeze only halts the ATB
countdown `$AF27,X`/`C1:826B` via the `$993A`-gated tick `C1/10E3`; the command/target-select input handling is
decoupled — the same fact the D1 fix already recorded: menu-nav `$95DC` cycles straight through a freeze). The
exact per-frame ROM routine that invokes C1/162x during `$A013`=1 is not traced here (see OPEN), but its EFFECT
is byte-pinned above.

### B.4 `$AE93`'s role — it is NOT the confirm record

The task flagged `$AE93` ("attack-related, rule OPEN — may be written at confirm"). It is **not** touched by the
target-select confirm. In the same run `$AE93` is written by **C1:AD08** (0→1 at f770, after Crono's hit lands)
and read/reset by **C1:BFB6** — i.e. `$AE93` is the ENEMY-AI attack-A/B selector toggled between GATO's turns
(exactly `battle_loop.md` §I: writer C1:AD08, the `CD/4926`/`CD/4F26` attack-table switch). The PC confirm writes
only `$9609`/`$A6D9`/`$A6DD`/`$95D5` (B.1). So `$AE93` is orthogonal to this bug.

### B.5 Wait mode (completeness)

`gato_wait.state` (`$2990` bit6 clear), same overlap btnscript: the confirm-during-`$A013`-freeze behaves
**identically** to Active — f524 `$9609`→0, `$95D5`→1, `$A6DD`→1 while `$A013`==1, `$99E0` stays **00** the whole
time. Target-select (`$9609`=1, `$95DB`=0) never sets the Wait-only submenu freeze `$99E0` (that needs `$95DB ∈
{1,2}`, a Tech/Item submenu — `battle_atbmode.md` / `field-battlewait-gato`), and the `$A013` action freeze is
mode-independent (`battle_loop.md` §F). So the confirm-during-freeze scenario is the same in both modes; the Wait
`$99E0` timer-freeze does not arise for target-select.

---

## (C) IMPLEMENTATION SPEC (mandate-clean; matches B.1/B.2)

**Principle:** stop DEFERRING the confirm. COMMIT it the instant the A edge is seen (any frame, frozen or not),
exactly as the ROM does — then ANIMATE the committed action when the execution engine is free. This removes the
`pendEdges` latch-then-drain that owns all three drop frames.

### C.1 State to add (src/battle.h `CtBattleHost`)
- `int schedAtk[3];` + `int schedTgtSlot[3];` + `int schedN;` — a small FIFO of **committed-but-not-yet-animated
  PC actions** (attacker combatant idx + the enemy SLOT captured at confirm). Party-size (≤3) because after
  Crono confirms, `$95D5` advances to Marle who may ALSO confirm during the same freeze (a second commit) — the
  ROM's queue supports back-to-back commits; one scalar would drop the second. Reset in `CtBattleBegin`.
  (A single scalar `schedAtk`/`schedTgtSlot` is the minimum that fixes the reported single-PC case; the FIFO is
  the correct generalization.)

### C.2 The confirm = an immediate COMMIT (split `battleOpenAction`)
Refactor the current `battleOpenAction` (which conflates dequeue + damage-compute + window-start) into two:
- `battleCommitPcAction(h, atk)` — the CONFIRM half (ROM C1/1626-1735), callable ANY frame:
  1. `slot = battleSelectedEnemy(s)` resolved-alive (capture `$9614` NOW — it may change once `$95D5` advances);
  2. `s[0x9609] = 0;` (DEC `$9609`, C1/1626);
  3. `battleQueueRemove(s, atk);` (`$A6D9[atk]`←FF, C1/1727) + `s[0x95D5] = battleQueueHead(s)` (or 0xFF)
     (C1/1732/1735); `s[0x95DC] = 0;` (next chooser's cursor → Att);
  4. push `{atk, slot}` onto the `schedAtk` FIFO. **Do NOT** set `$A013`/`actActive` and **do NOT** compute
     damage here (an action may already be animating).
- `battleDispatchScheduled(h)` — the ANIMATE half, run each frame gated `if (!h->actActive && schedN>0)`:
  pop the oldest `{atk, slot}`, then run the existing dispatch body — `battleComputeDamage` (RNG draws HERE, at
  animate-time, preserving the existing draw order), set `actActive/actAttacker/actTarget/actValue/actStart/
  actSpan`, `s[0x95DB]=1`, `s[0xA013]=1` (C1/40A6). It must NOT re-dequeue (the commit already did).

### C.3 Where each runs (in `CtBattleTick`)
- **Confirm dispatch to `battleCommitPcAction`:** invoke it wherever an A edge is seen while `$9609==1`, from a
  path that runs EVERY frame regardless of `$A013`/`actActive`:
  - in `battleMenuFrozen`'s target-select branch, REPLACE `h->pendEdges |= CT_BIN_A;` (src/battle.c:640) with
    `battleCommitPcAction(h, cur);`
  - in the not-frozen tick `step F` target-select branch (src/battle.c:1537), REPLACE the direct
    `s[0x9609]=0; battleOpenAction(h,cur);` with `battleCommitPcAction(h, cur);`
  - fix the SPAN-END frame hole so `battleMenuFrozen` still owns the confirm on the frame `$A013` clears:
    capture `int wasFrozen = s[0xA013];` at the top of `CtBattleTick` **before** `battleAdvanceAction`, and gate
    BOTH the top-of-frame edge latch (src/battle.c:1362) AND the freeze pass (src/battle.c:1400) on `wasFrozen`
    (not the post-advance `s[0xA013]`). This routes the span-end frame's raw A edge into `battleMenuFrozen` →
    `battleCommitPcAction` instead of dropping it.
- **`battleDispatchScheduled(h)`:** call once per frame just before the enemy-turn `step D` (src/battle.c:1467),
  gated `!h->actActive`. This makes a committed PC action the next thing to animate the instant the current
  freeze clears — matching the oracle's f644 dispatch after the f636 freeze-end. (Enemy actions in `step D` keep
  calling the enemy dispatch, which never touches the queue.)

### C.4 Interaction with the queue and the freeze
- The **queue** advance now happens at COMMIT (like the ROM f524), so the next PC's command box opens during the
  freeze (fixes A.3). `battleAdvanceAction`'s span-end block already does NOT dequeue (src/battle.c:1205-1206) —
  correct, since the commit dequeued.
- The **freeze** ($A013) still serializes ANIMATION: `battleDispatchScheduled` is gated `!actActive`, so exactly
  one action animates at a time; the committed action waits out the current freeze and fires at its end. The
  `$A013` action-freeze / ATB-timer semantics are otherwise untouched (a pure input-routing change).
- **MASHA** (`CT_BATTLE_MASHA`, tick-level synthetic A) must be routed through the SAME
  `battleCommitPcAction` + `battleDispatchScheduled` so its behavior is preserved; because MASHA never presses A
  during a freeze (the freeze gate returns before the MASHA tick code, src/battle.c:1400 vs 1415), its commits
  all happen un-frozen and dispatch the same frame — but the commit→dispatch split can shift the victory frame by
  a few frames, so **re-verify determinism + identical per-hit damage/HP and re-pin `field-battleloop-gato` /
  `field-battlewait-gato`** after implementing (the DAMAGE/turn-order logic is unchanged; only input routing).

### C.5 What NOT to change
- Not the confirm's RAM effects (`$9609`/`$A6D9`/`$A6DD`/`$95D5`) — they are already correct (B.1), only mis-timed.
- Not `$AE93` (B.4), the ATB cadence, the span-law, or the damage pipeline.
- Do not "fix" it by special-casing GATO or the boundary frame numbers — the fix is the structural
  commit-vs-animate split, which is correct for every enemy/scene.

---

## (D) OPEN questions
- **Exact per-frame ROM routine that invokes the confirm handler (C1/162x) during `$A013`=1.** Its EFFECT is
  byte-pinned (B.2 writers C1:1629/1732/1735 at f524, mid-freeze) and the D1 fix already established the menu
  input runs through the freeze, but the precise call site (a HUD/NMI path vs the action loop `C1/4090`'s
  `JSR C1/007E` kernel spin vs a non-`$993A`-gated command updater) is not traced. Not needed for the fix (the
  contract is "process the confirm every frame"), but pin it before claiming the invocation path.
- **Multiple commits in one freeze.** The ROM's queue supports Crono-then-Marle both confirming during a single
  enemy freeze; only single-PC commit was captured on the oracle here. The C.1 FIFO is the derived-correct shape,
  but the two-commit ordering (which animates first at freeze-end) was not directly captured — verify on the
  oracle (`...A@f500 (Crono), then nav+A@f560 (Marle)` during one GATO freeze) before relying on it.
- **Enqueue latency of the scheduled dispatch.** Oracle shows GATO freeze-end f636 → Crono animate f644 (~8f).
  Our engine's dispatch is on the next free frame/tick; the exact 8f gap is the general enqueue latency
  (`battle_loop.md` §S2/§H) and is not separately pinned for the scheduled-PC case.
- **Command-level (not target-select) A during a freeze** already works via `battleMenuFrozen`'s command branch
  (enter target-select live); the fix leaves it intact. B (cancel) already fires immediately in both paths.
- **The per-combatant "chosen action + target" record** the confirm handler stages (the `$A862`/`$A863,X`/
  `$A866,X`/`$A0D8,X` region touched at C1/1644-1748) was not byte-decoded — the pinned facts are the dequeue
  + head-advance + the observation that `$9877`/`$AEE3`/`$B1F4` are staged only at ANIMATION time (f644), so a
  faithful port can defer the whole attack-record + damage draw to dispatch (matching the ROM) without needing
  this record. Decode it before claiming byte-exact mid-freeze state for a PC that confirmed but hasn't animated.

---

## Reproduction commands (paths relative to repo root)
```
# --- (A) our bug (copy Belthasar.exe + SDL2.dll to a scratch dir first) ---
CT_BATTLE_SCRIPT="n:20,a:1,n:391,a:1,n:500" CT_BATTLE_TRACE=1 \
  <scratch>/ct_field_probe.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
#   confirm @ frame 572 -> pc-act count 0 (DROP);  n:391->n:390 (frame 571) -> pc-act f574 (WORKS)
#   drop frames = {414, 415, 572}; all of 416..571 dispatch at f574

# --- (B) canonical (copy ct.exe to a scratch dir first) ---
<scratch>/ct_probe.exe --monitor "9609:1,95D5:1,A6DD:1,A013:1,AE93:1,6130:2,5E30:2,AFB1:1" \
  --from gato_press_action.state --btnscript "100:8,0:392,100:6,0:114,100:6,0:260" --diff --writer
#   f478 A013=1 (C1:40A9); f524 confirm mid-freeze 9609->0 (C1:1629) 95D5->1 (C1:1735) A6DD->1 (C1:1732);
#   f636 freeze end (C1:41BD) Crono 70->61 (C1:ED1A); f644 Crono attack animates (A013=1); f769 GATO 76->60
python tools/dis.py 11600 170     # the confirm handler C1/1626 DEC $9609 + C1/1727-1735 dequeue/advance

# Wait mode: same btnscript --from saves/battle_re/gato_wait.state -> identical, $99E0 stays 00
```
