# BATTLE — the PC ATB-timer RELOAD rule + the re-enqueue guard (the "player can attack twice" bug)

User report (translated from Portuguese): *"the ATB bar should only be reset after the player selects an action. the way it is now,
right after attacking the player can attack again, that is, twice."* — the ATB bar must reset when the PC
SELECTS the action; in our engine a PC can attack twice in a row.

**Verdict (both halves proven, byte-cited):**
- **(A) The canonical rule:** the PC ATB timer `$AFAB+seat` (`$AF27,X`) is RELOADED **only at the action's
  SPAN END** — the frame the action-animation finishes and HP applies (`$A013` 1→0). It is **NOT** reloaded at
  the target-select CONFIRM, and **NOT** at the action's ANIMATE-start. Writer **C1:BCEB** (`$AFAB,X ← $B158,X`),
  value = the speed-derived reload (`$B158[Crono]=0x48=72`, the `FD/B807` formula already ported). This holds
  **identically for a normal un-frozen confirm (reload f558) and for a confirm scheduled during an enemy freeze
  (reload f769)** — the reload always tracks the *scheduled action's own* span-end, never the commit.
- **(B) Our bug is real, deterministic, and reproduced.** `src/battle.c` reloads at span-end correctly
  (battle.c:1650) — but its PC-enqueue step is a **LEVEL** check `timer==0 && !queued` (battle.c:1997). A
  committed PC is DEQUEUED at confirm (battle.c:682) yet his timer stays 0 until span-end, so on the tick
  between commit and dispatch he satisfies `timer==0 && !queued` and is **RE-ENQUEUED**. His command box never
  closes → he attacks again with no recharge. The ROM's enqueue is **EDGE**-gated (fires once per readiness,
  latched by the per-obj turn-state `$93EE`), so a committed seat is never re-enqueued until its ATB reloads
  (span-end) and counts back down to 0.

Method: oracle = `ct.exe --monitor --btnscript --writer` on `gato_press_action.state`; ROM = `tools/dis.py`
(fileoffset `(bank−0xC0)<<16|addr`, battle bank C1 is 16-bit M=0 X=0). Our engine = `Belthasar.exe` headless
`CT_BATTLE_SCRIPT`/`CT_BATTLE_TRACE` on `saves/battle_re/gato_t0.raw.wram`. Writer PC = the instr AFTER the store.

---

## (0) THE INITIAL LOAD — FD/B8F8-B93A, added 2026-08-22

Distinct from the SPAN-END reload this dossier is mostly about: the ONE-TIME initial ATB at battle boot.
`load[i] = $B158[i] - (MIN - 1)`, MIN = the smallest reload `$B158` over the PRESENT, nonzero combatants,
so the fastest starts one tick from ready and the rest trail by their reload gap. The scan (FD/B8FD) seeds
its minimum with slot 0 unconditionally, then over X=1..0x0A keeps a candidate iff `$B158,X < min` AND
`!= 0` AND `$AEFF,X != FF`; FD/B923-B933 subtracts `min-1` from every present nonzero slot.

This port had carried a FITTED `$B158 - 0x47`, right for every capture on disk because all of them have
MIN = 0x48 (Crono fastest), so `min-1 = 0x47`. A synthetic seed (gato_fastmarle: Marle's sheet speed
$2688 := 0x10) makes her the fastest, MIN = 0x36, and the two laws diverge: the oracle subtracts 0x35 and
gives Crono `0x48-0x35=0x13`, Marle `0x01`, GATO `0x31` — none of which `-0x47` can produce. Ported in
battleCombatStart; pinned by `field-battle-atb-initial-load` (2 mutations, both KILLED). Surfaced by the
2026-08 read-only battle audit.

## (A) THE ORACLE TIMELINE — reload is at SPAN-END in both scenarios

### A.1 Scenario (i): a NORMAL un-frozen confirm — reload at the action's span-end
`ct_probe --monitor "AFAB:8,9609:1,95D5:1,A013:1,A6D9:3,A6DD:1,A6DE:1,6130:2,5E30:2" --from
gato_press_action.state --btnscript "100:8,0:392,100:6,0:20,100:6,0:640" --diff --writer`
(boot A; idle → Crono pre-charged ready f381; A@f400 = Att→target-select; A@f426 = CONFIRM, un-frozen since
GATO's freeze doesn't start until f478; idle → watch the reload). `$AFAB` bytes = idx0..7 = **Crono, Marle,
—, —, —, —, GATO, —**.

| frame | event | evidence | `AFAB[Crono]` |
|---|---|---|---|
| f381 | Crono ready → ENQUEUED, current | `$95D5`=00 (**C1:058D**), `$A6D9`=00FFFF (**C1:1B83**), `$A6DD`=00 (**C1:1BA9**), `$A6DE`=01 (**C1:1BA0**) | **00** |
| f402 | target-select opens | `$9609`=01 (**C1:12B9**) | 00 |
| **f427** | **CONFIRM (un-frozen)** | `$9609`→00 (**C1:1629**), `$A6D9`→**FFFFFF** (**C1:172A**, DEQUEUE), `$A6DD`→FF (C1:1742), `$A6DE`→00 (C1:1721), `$95D5`→FF (C1:1745) | **00 — NO reload** |
| f433 | Crono's attack ANIMATES | `$A013`→01 (**C1:40A9**) | 00 |
| **f558** | **SPAN END** | `$A013`→00 (**C1:41BD**); GATO `$6130` 76→59 (−17, **C1:ED1A**); **`$AFAB[0]` 00→0x48 RELOAD (C1:BCEB)** | **0x48 (72)** |

**Reload = span-end (f558), NOT confirm (f427), NOT animate (f433).** Value 0x48=72 = `$B158[Crono]`.
After f558 the timer counts 0x48→…→0 again over ~72 ticks (f790+), i.e. a full recharge before the next turn.

### A.2 Scenario (ii): a confirm DURING an enemy freeze (the SCHEDULED case) — reload still at span-end
`… --btnscript "100:8,0:392,100:6,0:114,100:6,0:300" …` (A@f400 Att→target-select; A@f520 CONFIRM mid-freeze).

| frame | event | evidence | `AFAB[Crono]` |
|---|---|---|---|
| f478 | GATO action freeze STARTS | `$A013`→01 (C1:40A9); all ATB frozen | 00 |
| **f524** | **CONFIRM (mid-freeze)** | `$9609`→00 (C1:1629), `$95D5`→**01 Marle** (**C1:1735**), `$A6D9`→**FF01FF** (C1:172A, Crono DEQUEUED), `$A6DE`→01 (C1:1721) | **00 — NO reload** |
| f636 | GATO freeze ENDS | `$A013`→00 (C1:41BD); GATO's damage → Crono `$5E30` 70→61 (C1:ED1A); GATO timer `$AFB1`←0x66 (**C1:84E1**) | 00 |
| f644 | Crono's SCHEDULED attack ANIMATES | `$A013`→01 (C1:40A9) — ~8f after GATO's freeze end | 00 |
| **f769** | **Crono's SPAN END** | `$A013`→00 (C1:41BD); GATO `$6130` 76→60 (−16, C1:ED1A); **`$AFAB[0]` 00→0x48 RELOAD (C1:BCEB)** | **0x48 (72)** |

**The scheduled action's reload is at ITS OWN span-end (f769)** — not the commit (f524), not the freeze-end
(f636), not the animate-start (f644). Answer to the task's explicit question: *reload happens when the
SCHEDULED action ANIMATES-to-completion, i.e. at span-end — never at the commit.*

### A.3 The re-enqueue guard PROOF (both scenarios)
In BOTH runs, from the CONFIRM (dequeue, `AFAB[Crono]`=00) through the SPAN-END reload, Crono's timer sits at
00 and he is OUT of the ready-queue (`$A6D9` = FFFFFF / FF01FF), and he is **NEVER re-enqueued** — the enqueue
request `$95D6` stays FF and `$A6D9[0]` never returns to 00. Critically this includes the **un-frozen gaps**:
scenario (i) f427→f433 (`$A013`=0) and scenario (ii) f636→f644 (`$A013`=0, 8 frames). So the guard is **not**
"the scheduler is frozen"; a committed seat is simply not re-offered until its ATB reloads.

---

## (B) THE ROM MECHANISM — edge-triggered enqueue, per-obj turn-state latch, span-end reload

### B.1 Readiness: the ATB decrement + the "ready" flag (`C1/8246`, dis `18246`/`18260`, 8-bit resync)
```
18260  LDA $AF27,X / CMP #00 / BEQ 1826F   ; timer already 0 -> DON'T dec (no underflow -> stays 0)
18267  DEC A / STA $AF27,X                  ; else timer-- (writer C1:826B)
1826B  CMP #00 / BNE 18277                  ; reached 0?
1826F  LDA $AFB6,X / ORA #80 / STA $AFB6,X  ; set READY bit7 ($AFB6 is a PER-FRAME scratch, re-zeroed C1:B1BE)
```
A timer at 0 does not underflow — it holds 0 and re-flags ready each tick. `$AFB6` is transient (all-00 between
frames), so it is NOT the persistent guard.

### B.2 Enqueue REQUEST `$95D6` (one-shot) — the EDGE + the per-obj turn-state latch `$93EE`
Empirical (`--monitor "93EE:12,95D6:1,A6D9:3,AFAB:8,95D5:1,9609:1" --writer`, scenario i):

| frame | `$93EE[obj0]` | `$95D6` | note |
|---|---|---|---|
| f378 | 00 | FF | Crono timer just reached 0 |
| **f379** | **0x81** (writer **C1:1B2E**) | **00** (writer **C1:1B37**) | readiness detector sets latch bit7 + posts request |
| f381 | 0x81 | **FF** (writer **C1:1B8E**) | enqueue `C1/1B67` CONSUMES the request (resets `$95D6`) |
| f382–f424 | 0x81 (held) | FF | queued/in-menu — **request NEVER re-fires** |
| **f427** | **0x41** (writer **C1:1713**) | FF | CONFIRM clears latch bit7 (`AND #$7F`) |
| f430 | 0x01 (writer C1:B98B) | FF | committed, awaiting/animating |
| **f558** | **0x00** (writer **C1:85D8**) | FF | SPAN-END: latch fully cleared, SAME frame as the C1:BCEB reload |

The readiness detector **C1/1B19** (writes `$95D6` at 1B34→writer **C1:1B37**; sets `$93EE,X|=0x80` at
1B2B→writer **C1:1B2E**) guards with `11B1C LDA $A6D9,X / BPL RTS` (skip if the seat is already queued). The
enqueue **C1/1B67** pops `$95D6` (reset FF at C1:1B8E), restores the seat's saved menu-state
(`$A863,X→$95DF,X`, `$A866,X→$95EB,X`, 11B70/11B76), writes `$A6D9[seat]=seat` (C1:1B83), `INC $A6DE`
(C1:1BA0), sets head `$A6DD` (C1:1BA9). `$95D5` = queue head via `C1/0560` (`STA $95D5` at 1058A→C1:058D,
gated `$95DB==0`).

**NET (this IS the re-enqueue guard):** the enqueue request fires **once per readiness edge**, latched by the
per-obj turn-state `$93EE[obj]` which is NONZERO for the whole span *ready → committed → acting* and is cleared
to 0 **only at the span-end reload** (C1:85D8, the same frame as the C1:BCEB `$AFAB←$B158`). Because the timer
is reloaded only at span-end and a committed seat's timer therefore stays 0 (no fresh 1→0 edge) with its
turn-state latch still set, the seat is never re-requested/re-enqueued between commit and reload — proven in
A.3. The seat re-arms only after the reload (timer→0x48) drains back to 0.

### B.3 The CONFIRM (dis `11620`, corroborates battle_confirm_queue.md B.1)
`11626 DEC $9609` (C1:1629, close target-select) · save chosen action `$95EB→$A866` / `$95DF→$A863` /
`$9EE3→$A0D8` (11644-11653, the confirm_queue OPEN "chosen-action record" — now decoded) · dequeue
`LDA #FF / STA $A6D9,X` (writer C1:172A) · advance head/current `STA $A6DD` / `STA $95D5`
(1732/1735 when a next seat exists → scenario ii `$95D5`=1; 1742/1745 when the queue empties → scenario i
`$95D5`=FF) · clear `$93EE` bit7 (C1:1713). **No `$AFAB` write — the confirm does NOT reload.**

### B.4 The RELOAD site (dis `1BCD0`, confirms battle_loop.md ADDENDUM §B)
```
1BCE1  LDA $B18B / TAX          ; X = the just-completed action's actor index
1BCE5  LDA $B158,X              ; the speed-derived reload value
1BCE8  STA $AFAB,X              ; RELOAD (writer C1:BCEB)   <-- value $B158[Crono]=0x48=72
```
Called at action-completion (span-end). `$B158[a] = 105 − 6·speed + CC2E31[(speed−1)+(bs&7)·16]` (already ported
`battleReload`, battle.c:261). Enemy parallel: GATO reloads at HIS span-end `$AFB1←0x66` (C1:84E1, A.2 f636).

---

## (C) OUR BUG — pinpointed in src/battle.c, with a reproducing script + trace

### C.1 Root cause: step E is a LEVEL enqueue with no committed-seat guard
- **Commit** dequeues but does NOT reload: `battleCommitPcAction` (battle.c:677) — `battleQueueRemove` (682),
  push `schedAtk` FIFO (686); `$AFAB[atk]` untouched → stays 0.
- **Reload** is correct and at span-end: `battleAdvanceAction` (battle.c:1650) `s[0xAFAB+atk]=s[0xB158+atk]`
  under `if (rel >= spanRel)` (1646). This matches the ROM (C1:BCEB, span-end) — **the reload is NOT the bug**.
- **Re-enqueue (THE BUG)** — step E, battle.c:1997:
  ```c
  for (int seat = 0; seat < 3; seat++)
    if (battleAlive(s, seat) && s[0xAFAB + seat] == 0 && !battleQueued(s, seat)) battleEnqueue(s, seat);
  ```
  A committed seat is `!queued` (dequeued at commit) and `timer==0` (reload deferred to span-end) → **re-enqueued
  on the tick between commit and dispatch** (the commit runs per-frame at battle.c:1863/step-F 2054; step E at
  1997 runs before the scheduled dispatch battleDispatchScheduled at 2075). The ROM's per-obj `$93EE` latch
  (B.2) — which keeps a committed seat out of the ready cycle until reload — has **no equivalent** here.

### C.2 Reproduction (deterministic; copy `build/Belthasar.exe`+`SDL2.dll` to a scratch dir first)
```
# Smoking gun — after ONE attack Crono STAYS queued (oracle dequeues him):
CT_BATTLE_SCRIPT="n:20,a:1,n:3,a:1,n:220" CT_BATTLE_TRACE=1 \
  <scratch>/Belthasar.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
  f184 pc-commit  cur=-1  q[0..0]  AFAB(c0 …)     <- dequeued at commit
  f185 pc-act     cur=0   q[0..1]  AFAB(c0 …) A013=1   <- RE-ENQUEUED (step E) while his own attack animates
  f310 act-end    cur=0   q[0..1]  AFAB(c72 …) pcAtk=1 <- reloaded to 72 but STILL queued -> box stays open

# The double attack — press A,A again the moment the box reappears:
CT_BATTLE_SCRIPT="n:20,a:1,n:3,a:1,n:135,a:1,n:3,a:1,n:400" CT_BATTLE_TRACE=1  <scratch>/Belthasar.exe …
  f180→f310  ATTACK 1  (target-sel f180, commit f184, animate f185, act-end f310)  GATO 76→59  pcAtk=1
  f320       ATTACK 2  target-select with AFAB(c68)  <- timer only 72->68 (4 ticks), NOT a 72-tick recharge
  f325→f502  ATTACK 2 animates+applies  GATO 59→27  pcAtk=2
```
Crono attacks **twice** (`pcAtk` 0→1→2) with his timer moving only **72→68** between them — exactly
*"right after attacking the player can attack again"*. (Oracle A.1: after acting he is dequeued and idle at
timer 0 until the span-end reload, then recharges 0x48→0 before he can act again.)

---

## (D) IMPLEMENTATION SPEC (mandate-clean; matches A/B — reload is already right, ADD the guard)

**Keep the reload where it is** (`battleAdvanceAction`, battle.c:1650, span-end = C1:BCEB). Do NOT reload at
commit or dispatch — the ROM keeps the timer at 0 until span-end (A.1/A.2).

**Add the missing re-enqueue guard** so step E (battle.c:1997) does not re-offer a COMMITTED seat — mirror the
ROM's per-obj turn-state `$93EE` (nonzero from readiness through the span-end reload). Two equivalent forms:

- **Option A (minimal — reuse existing host state, no new field/reset).** A committed seat is either in the
  `schedAtk` FIFO (commit→dispatch) or is the current `actAttacker` (dispatch→span-end). Add a helper and gate
  step E on it:
  ```c
  static int battleCommitted(CtBattleHost* h, int seat) {
    if (h->actActive && h->actAttacker == seat) return 1;
    for (int i = 0; i < h->schedN; i++) if (h->schedAtk[i] == seat) return 1;
    return 0;
  }
  /* battle.c:1997 */  if (battleAlive(s,seat) && s[0xAFAB+seat]==0 && !battleQueued(s,seat)
                           && !battleCommitted(h,seat)) battleEnqueue(s,seat);
  ```
  Blocks the commit→dispatch re-enqueue (seat ∈ `schedAtk`). After the span-end reload the seat is neither
  committed nor timer-0, so it re-enqueues only after a full recharge. (`schedAtk`/`schedN`/`actAttacker` already
  exist: battle.h:93-94,123-125.)

- **Option B (literal `$93EE` port).** Add `int pcInTurn[3]` to `CtBattleHost` (reset in `CtBattleBegin`):
  SET it when step E enqueues a seat; CLEAR it at the span-end reload (battle.c:1650, the C1:85D8 twin).
  Step E: `timer==0 && !queued && !pcInTurn[seat]`. This is the exact ROM shape (latch set at readiness,
  cleared at reload) and generalizes cleanly to multiple committed PCs.

Either fix removes the double-attack while leaving the reload value/timing, the commit/animate split
(battle_confirm_queue.md), the damage pipeline, and the cadence untouched.

**Re-verify after implementing:** `field-battleloop-gato` / `field-battlewait-gato` (MASHA commits every tick
— confirm a legitimate re-attack still fires after a *full* recharge, and the pinned per-hit damage/HP/victory
frame are byte-identical); re-pin if the guard shifts the victory frame. Multi-PC: a Crono-then-Marle double
commit in one freeze (confirm_queue §C.1 FIFO) must keep BOTH out of the queue until each reloads — the guard
is per-seat, so both options handle it, but capture it.

---

## (E) OPEN
- **The exact CALLER of the readiness detector C1/1B19** (which iterates combatants and invokes it on the
  readiness edge) is not traced: there is **no** bank-C1 `JSR/JMP $1B19` (byte-scanned) — it is reached via a
  computed/table jump or another bank (ct.py refs pointed at `F4/9509`, which disassembles as data). The NET
  behavior (request fires once per readiness, gated by `$93EE` + the `$A6D9,X` queued-check, cleared at reload)
  is byte-pinned by the oracle (B.2); only the dispatch site is open. Not load-bearing for the fix.
- **Precise bit semantics of `$93EE[obj]`** are inferred from the write sequence 0x81→0x41→0x01→0x00
  (bit7 = ready-pending, set C1:1B2E / cleared at confirm C1:1713; bit6 = committed-this-frame, cleared at
  animate C1:B98B; bit0 = in-turn, cleared at reload C1:85D8). A full reader-by-reader decode of each bit was
  not done — the NET (nonzero from readiness to reload) is what the guard needs.
- **Multi-PC commit in one enemy freeze** (Crono then Marle both confirm before the freeze ends): only the
  single-PC re-enqueue was reproduced here; the per-seat guard should cover it, but capture the two-commit
  ordering on the oracle before relying on it (mirrors confirm_queue §C.1 OPEN).

---

## Reproduction commands (paths from repo root; probe/field exes copied to a scratch dir)
```
# (A) oracle — reload at span-end, both scenarios:
<scratch>/ct_probe.exe --monitor "AFAB:8,9609:1,95D5:1,A013:1,A6D9:3,A6DD:1,A6DE:1,6130:2,5E30:2" \
  --from gato_press_action.state --btnscript "100:8,0:392,100:6,0:20,100:6,0:640" --diff --writer
#   f427 confirm (AFAB[0]=00, dequeued) ; f558 SPAN-END reload AFAB[0]=0x48 (C1:BCEB)
<scratch>/ct_probe.exe --monitor "AFAB:8,9609:1,95D5:1,A013:1,A6D9:3,A6DE:1,93EE:8,6130:2,5E30:2" \
  --from gato_press_action.state --btnscript "100:8,0:392,100:6,0:114,100:6,0:300" --diff --writer
#   f524 confirm mid-freeze ; f769 scheduled-action SPAN-END reload AFAB[0]=0x48
# (A) the $93EE latch + $95D6 request (edge, one-shot):
<scratch>/ct_probe.exe --monitor "93EE:12,95D6:1,A6D9:3,AFAB:8,95D5:1,9609:1" \
  --from gato_press_action.state --btnscript "100:8,0:392,100:6,0:20,100:6,0:180" --diff --writer
# ROM sites:
python tools/dis.py 18260 0x40 1 0     # ATB dec + ready-flag (C1:826B)
python tools/dis.py 11B19 0x40 0 0     # readiness detector: $A6D9,X guard + $95D6 request (1B37) + $93EE (1B2E)
python tools/dis.py 11B67 0x50 0 0     # enqueue: consume $95D6 (1B8E), $A6D9/$A6DE/$A6DD (1B83/1BA0/1BA9)
python tools/dis.py 1BCE1 0x10 0 0     # RELOAD $AFAB,X <- $B158,X (C1:BCEB), X = $B18B
python tools/dis.py 11620 0x120 0 0    # confirm: DEC $9609 (1629), chosen-action save, dequeue (172A), $93EE clr (1713)
# (B) our bug (copy Belthasar.exe + SDL2.dll to a scratch dir):
CT_BATTLE_SCRIPT="n:20,a:1,n:3,a:1,n:135,a:1,n:3,a:1,n:400" CT_BATTLE_TRACE=1 \
  <scratch>/Belthasar.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
#   pcAtk 0->1 (f310) ->2 (f502) with AFAB(c) only 72->68 between = the double attack; root cause battle.c:1997
```
