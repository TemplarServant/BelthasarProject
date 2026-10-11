# BATTLE — DEATH interactions with target-select + the commit/schedule FIFO (two user reports)

Two user reports about what happens when a combatant DIES around the target cursor / the committed-action
queue:

1. **"When an enemy dies while the pointer (hand) points at it because of an allied attack already
   in the queue, the hand stays."** — the target-select HAND stays on an enemy that dies (killed by an
   allied attack already queued). Canonical: what happens to the cursor / the pending action when the
   pointed enemy dies.
2. **"Crono's attack that was already in the queue must be cancelled when he faints/dies."** — a
   committed-but-not-yet-animated PC action whose ATTACKER faints must CANCEL.

**Verdicts (both derived from ROM disasm + LakeSnes oracle; NOTHING implemented, src/ + git untouched):**

- **(1) The ROM RE-RESOLVES the target cursor to the next LIVING enemy — target-select does NOT close.**
  The hand tracks `$A62D`, which is rebuilt every menu-update as `$A62D = $99C0[$9614]` where **`$99C0` is a
  COMPACTED list of living-enemy combatant indices** (dead enemies compacted OUT), built by `C1/2047-2099`
  (`JSR C1/27C5`). When the pointed enemy dies its entry drops out of the compacted list, so the same cursor
  position `$9614` now indexes the next living enemy → `$A62D` advances → the hand moves. A committed action
  whose target dies before it animates **retargets** to the first living enemy at dispatch (proven: Marle's
  confirmed hit re-aimed Het0→Het1). **OUR bug:** our `$9614` is a PHYSICAL enemy slot (0..7, indexed into
  the `$AF02` alive-list) that `battleTargetCursorRender` (field.c:3092) reads RAW, and it is re-validated
  ONLY on nav input / entry / commit — never per-frame — so a dead-under-cursor enemy leaves the hand parked
  on the dead/dissolving slot ("the hand stays").
- **(2) The ROM CANCELS a committed action when its attacker dies.** Death zeroes the dead actor's 7-byte
  `$93EE` action record (`FD/A8FE`, writer `FD:A914`); the pending-action executor `C1/8580` then pops the
  actor off the `$99D4/$99D8` stack and finds an empty record → NO attack animates. Proven: a 3-HP Crono
  confirms, an enemy kills him at freeze-end, and his committed attack NEVER lands (the target enemy keeps
  full HP). **OUR bug:** `battleDispatchScheduled` (battle.c:1665) pops the `schedAtk` FIFO and animates with
  **no attacker-alive check**, and `battleRemoveDead` (battle.c:588) purges only the ready-queue `$A6D9`, not
  the `schedAtk` FIFO — so a PC who commits and then dies still fires his attack.

Method: oracle = scratch `ct_probe.exe` `--monitor --btnscript --diff --writer` on `saves/battle_re/
forest_battle_3hetake.state` (an in-battle state: `$AF02 = 5E 5E 5E FF …` = 3 Hetakes alive slots 0/1/2,
staging DONE, Crono current). ROM = `tools/dis.py` (fileoffset `(bank-0xC0)<<16|addr`; battle bank C1 is
16-bit M=0 X=0). Low-HP repro via a WRAM `.state` patch at file/payload offset `0x45` (the LakeSnes payload
preamble is 69 bytes; legacy bare states have WRAM at 0x45, CTLS at 0x55, CRC re-fixed). Writer PC = the
instruction AFTER the store retires. Enemy blocks: `ENEMY_BLOCK(slot)=$5FAD+slot*0x80`; forest Hetake curHP
= `$5FB0/$6030/$60B0` (slots 0/1/2); GATO = slot 3, `$6130`. PC blocks `$5E2D+seat*0x80`; dead flag =
block+0x1D bit7 (`$5E4A` Crono).

---

## PART A — Q1: the target cursor when the pointed enemy DIES (re-resolves; does NOT persist)

### A.1 The cursor-validity path (ROM), disassembled

The target-select "hand" does NOT point via `$9614` directly. The chain is a per-menu-update rebuild:

**Entry** — command-menu A-on-Att opens target-select (`dis.py 11200 0xE0 0 0`):
```
112B0  JSR C1/1F79        ; build the target cursor (below)
112B6  INC $9609          ; target-select OPEN   (writer C1:12B9)
```
`C1/1F79` (`dis.py 11F79 0x70`): `$960F := $95D5` (the choosing actor), zero `$9613/$960C/$960A/$A64F/$A6D8`,
scan 11 slots to build the eligibility, then pick the first eligible into `$9613`.

**Per-menu-update REBUILD** — the compacted living-enemy list + the resolved target (`dis.py 12070 0x40`):
```
12047..1207A  (loop)      ; build the COMPACTED living-enemy list into $99C1,Y (living combatant idx per entry)
1207B  JSR C1/27C5        ; finalize: copy $99C1,X -> $99C0,X for 11 entries (127CC: LDA $99C1,X / STA $99C0,X;
                          ;   loop 127CC-127D6, writer C1:27D2)
1207E  LDA $9614 / TAX
12082  LDA $99C0,X
12085  STA $A62D          ; *** $A62D = $99C0[$9614] ***  (writer C1:2088) — the ACTUAL target under the cursor
12088  LDA $960C / BPL    ; ($960C = all-targets flag; else copy the whole $99C0 list into $A62D,X)
```
So **`$99C0[i]` = the i-th LIVING enemy's combatant index, FF-padded** (dead enemies are compacted OUT, not
left as holes), and **`$9614` is a POSITION in that compacted list**, and `$A62D` = the enemy under the hand.

**Nav** — L/R / U/D while target-select is held (`dis.py 1176C 0x50`):
```
1176C  INC $9614 / LDA $9614 / (wrap 0x0B) / STA $9614 / TAX
1177B  LDA $99C0,X / BMI 1176C     ; skip trailing-FF (past the living count)
11780  STA $A62D                   ; resolved target
11786  DEC $9614 …                 ; the -1 (Up/Left) mirror, same $99C0,X BMI skip
```

**Confirm** zeroes the cursor slot (`dis.py 11620 0x140`): `116D4 STZ $9614` (writer C1:16D7) — the cursor
position resets at each confirm.

### A.2 The DEATH path touches NOTHING in the cursor state

`dis.py 1ECC0 0xC0` (the HP-apply → death):
```
1ECC4  STA $5E30,X                 ; write curHP
1ECC7  LDA $5E30,X / BMI / CMP #0 / BEQ 1ECDB    ; curHP <= 0 -> death
1ECDB  (death) LDA $5E4A,X / BIT #80 / BNE       ; already dead? skip
       ... CPY #0C / BCC 1ED04                    ; PC (Y<0x0C) -> skip the reward JSL
       (enemy Y>=0x0C) JSL FD/ABA2                ; reward accrue + $AF02 delist
1ED04  LDA $5E4A,X / ORA #80 / STA $5E4A,X        ; DEAD FLAG block+0x1D bit7 (enemy path)
1ED0F  TDC / STA $5E30,X                          ; zero curHP
```
The death routine sets the dead flag and (enemies) delists `$AF02`/`$AEFF` — it writes **nothing** to
`$9614/$9609/$99C0/$A62D/$A6D9`. So the cursor re-resolution is NOT done at death; it is done by the
per-frame rebuild (A.1) reading the freshly-dead alive-state.

### A.3 ORACLE — the hand re-resolves to the next living enemy (target-select stays open)

Crono commits an attack on Het0; Marle opens target-select on Het0 during Crono's attack freeze; Crono's
attack kills Het0 UNDER Marle's hand:
```
ct_probe --monitor "95D5:1,9609:1,9614:1,95DC:1,A013:1,AF02:4,99C0:4,A62D:1,960D:1,5FB0:2,5E30:2,5EB0:2" \
  --from saves/battle_re/forest_battle_3hetake.state --btnscript "0:30,100:4,0:24,100:4,0:184,100:4,0:220" \
  --diff --writer
```
| frame | event | evidence |
|---|---|---|
| f33 | Crono target-select opens | `$9609`=1 (C1:12B9); **`$99C0`=`03 04 05 FF`** (C1:27D2 — living idx 3,4,5), `$A62D`=03 (Het0, C1:2088), `$9614`=0 |
| f61 | Crono CONFIRMS (mid enemy-freeze) | `$9609`→0 (C1:1629), `$9614`→0 (C1:16D7), `$95D5`→1 Marle |
| f236 | Crono's committed attack animates | `$A013`→1 (C1:40A9) |
| **f250** | **Marle opens target-select on Het0** | `$9609`→1 (C1:12B9); `$99C0`=`03 04 05 FF`, `$A62D`=03 (Het0), `$9614`=0 — the hand is on Het0, during Crono's freeze |
| f377 | Crono's attack span ends → Het0 killed | `$5FB0`→0 (C1:ED12) |
| **f378** | **Het0 delisted** | `$AF02`=`FF 5E 5E FF` (C1:B291). BUT `$9614`=0, `$9609`=1, `$A62D`=03, `$99C0`=`03 04 05 FF` — all STILL point at the dead Het0 (1-frame stale) |
| **f381** | **RE-RESOLVE** | **`$99C0`→`04 05 FF FF` (C1:27D2)** — Het0 compacted out; **`$A62D`→04 (C1:2088)** = Het1. `$9614`=0 UNCHANGED. `$9609`=1 STILL OPEN. The hand moved to the next living enemy on its own. |

So the canonical answer: **target-select does NOT close; the cursor RE-RESOLVES to the next living enemy**
(`$99C0` compacts the dead one out, `$A62D` follows), with a ~3-frame lag (the rebuild cadence).

### A.4 ORACLE — a COMMITTED action whose target dies RETARGETS at dispatch

Crono AND Marle both confirm attacks on Het0; Crono's kills Het0 first; where does Marle's committed attack
land?
```
ct_probe --monitor "A013:1,95D5:1,AF02:4,5FB0:2,6030:2,60B0:2,5EB0:2,9614:1,9609:1,A0D8:3,93F0:1" \
  --from saves/battle_re/forest_battle_3hetake.state --btnscript "0:30,100:4,0:24,100:4,0:184,100:4,0:36,100:4,0:300" \
  --diff --writer
```
- f61 Crono confirms Het0; f290 Marle confirms Het0 (`$95D5`→FF, queue empty; both committed).
- f378 Crono's attack kills Het0 (`$5FB0`→0); f379 `$AF02`=`FF 5E 5E FF`.
- f430 **Marle's committed attack animates** (`$A013`→1); **f576 it lands on Het1** (`$6030`→0, C1:ED12),
  f577 `$AF02`=`FF FF 5E FF`. Marle's confirmed hit **re-aimed the dead Het0 → the first living enemy Het1**.

(The confirm did NOT stash a fixed combatant idx: `$A0D8` stayed `00 00 00` and `$93F0` stayed 0 the whole
run — the committed target is re-resolved from the live compacted list at animate-time, not a frozen id.)

### A.5 OUR divergence (Q1) — the hand renders a raw physical slot with no per-frame re-validation

- **The hand render reads `$9614` RAW as a physical slot** — `battleTargetCursorRender` (field.c:3090-3094):
  ```c
  if (mem[0x9609] != 1) return;
  int slot = mem[0x9614];                    // physical enemy slot 0..7 (indexes $AF02 / ENEMY_BLOCK)
  uint8_t id = mem[CT_ENEMY_BLOCK(slot) + 0];
  ```
  It draws the hand over `slot`'s staged enemy with NO skip-dead. If `slot` died, the hand stays on the dead
  (dissolving/empty) enemy → "the hand stays."
- **Our `$9614` is a PHYSICAL slot, re-validated only on nav/entry/commit — never per-frame.** The re-validation
  sites: `battleTargetNav` (battle.c:305, on nav input only, `if (s[0xAF02+cur]!=0xFF)`), the target-select
  ENTRY re-resolve (battle.c:727 frozen / battle.c:2179 step-F, `if (s[0xAF02+s[0x9614]]==0xFF) s[0x9614]=
  battleFirstEnemySlot`), and `battleSelectedEnemy` at commit (battle.c:316). None run while target-select is
  merely HELD. So an enemy dying under a stationary cursor is never noticed until the PC navs.
- **Reachability is exactly the report's "committed allied attack" case.** PC-B confirms (his action goes to
  `schedAtk`, `$95D5` advances to PC-A); PC-A opens target-select (auto-resolves to the first-alive = the same
  enemy); PC-B's committed attack dispatches (`battleDispatchScheduled`) and animates; at its span-end
  (`battleApplyDamage`, battle.c:818) it kills that enemy (`$AF02[slot]=FF`); `battleTargetCursorRender` keeps
  drawing PC-A's hand on the dead slot every frame (the render is not tick-gated). Only when PC-A navs does
  `battleTargetNav` skip-dead.
- **Where we are ALREADY correct:** `battleDispatchScheduled` re-resolves a dead committed TARGET to first-alive
  (battle.c:1677: `if (slot<8 && s[0xAF02+slot]!=0xFF) tgt=3+slot; else first-alive`) — this MATCHES the ROM's
  retarget (A.4). So only the OPEN-cursor per-frame re-resolution is missing.

---

## PART B — Q2: the committed action when its ATTACKER dies (CANCELLED)

### B.1 The confirm PUSHES the action onto a pending-action stack + writes a `$93EE` record

`dis.py 11620 0x140` (the confirm handler; corroborates battle_confirm_queue.md B.1 + battle_atb_reload.md B.3):
```
11626  DEC $9609                        ; close target-select
11640  LDA $95D5 / TAX                  ; X = the confirming seat
11644  LDA $95EB,X / STA $A866,X        ; save chosen action byte  (gated $A0D5, 1163B)
1164A  LDA $95DF,X / STA $A863,X        ;   "
11650  LDA $9EE3   / STA $A0D8,X        ; save target byte
116D4  STZ $9614                        ; reset the cursor slot   (writer C1:16D7)
116DA  LDA $99D8 / TAX / LDA $95D5 / STA $99D4,X   ; *** PUSH the actor onto the pending-action stack ***
116EB  LDA $93EE,X / AND #7F / STA $93EE,X         ; clear the turn latch bit7
116F2  STA $93EE,X ...                  ; write the 7-byte action record $93EE,X: flags, $9615(target),
116FD  ... $9615 -> $93F0,X ...         ;   $A62D->$93F1,X, $82->$93F3,X, $84->$93F4,X (keyed by a $CCFAF0 table)
11713  INC $99D8                        ; stack depth++
1172A  LDA #FF / STA $A6D9,X            ; dequeue from the ready-queue
1172C  LDA $A6D9,X / STA $A6DD / STA $95D5 ; advance head/current to the next queued seat (else 0xFF)
```
So a confirm = **push actor onto `$99D4[$99D8++]`** + **write his 7-byte action record `$93EE,X`** + dequeue +
advance. `$99D4/$99D5/$99D6` is the pending-action ACTOR stack (party-depth), `$99D8` = depth.

### B.2 The executor POPS the stack; DEATH zeroes the record first

**Executor front-end** `C1/8580` (`dis.py 18590 0x40`): the turn/action processor pops the pending stack:
```
18592  LDA $99D4 / BMI 1860A           ; empty (FF)? skip
18597  LDA $99D4 / STA $B18B           ; $B18B = the acting combatant (the reload C1/BCE1 later reads this)
1859D  LDA $99D5 / STA $99D4           ; pop: shift the stack down
185A3  LDA $99D6 / STA $99D5
185AE  DEC $99D8                       ; depth--
185B1  LDA $B18B / CMP … / BEQ 185F3 / BRA 1860A     ; (validity gate — see OPEN)
185C2  LDA $93EE / EOR $B188 / BPL …   ; process the action's effect flags from the $93EE record
```
**Death-side turn cleanup** `FD/A8FE` (`dis.py 3DA8FE 0x40`): when a combatant dies, its `$93EE` record is
purged:
```
3DA8FE  LDA $14 / TAX / LDA $B188,X / BEQ         ; X = the dying actor ($14)
3DA907  LDA $14 / ASL/ASL/ASL / SEC / SBC $14 / TAX   ; X = actor*7 (the 7-byte $93EE stride)
3DA914  STZ $93EE,X / STZ $93EF,X                 ; *** ZERO the dead actor's action record ***  (writer FD:A91A)
3DA920  LDA $14 / TAX / LDA $B158,X / STA $AFAB,X ; reload the dead actor's timer (harmless; he's dead)
```
**PC-death flag** `C1/B2B1` (`dis.py 1B2A0 0x40`): the enemy hit that kills a PC sets the dead flag + delist:
```
1B2B4  LDA #80 / STA $5E4A,X            ; DEAD FLAG block+0x1D bit7 (PC path)   (writer C1:B2B9)
1B2BE  LDA #FF / STA $AEFF,Y            ; mark the PC dead in the target-eligibility table
```
So on a PC's death the ROM zeroes his `$93EE` committed-action record (`FD:A914`) and sets his dead flag; the
executor then pops his (now-empty) pending entry and produces no action.

### B.3 ORACLE — a 3-HP Crono commits, the enemy kills him at freeze-end, his attack is CANCELLED

Patch Crono to 3 HP (`patch_wram.py forest_battle_3hetake.state forest_lowhp.state 5E30=0300`), commit his
attack on Het0, let the enemy's hit (−4) kill him at freeze-end BEFORE his attack animates:
```
ct_probe --monitor "A013:1,95D5:1,AF02:4,5E30:2,5E4A:1,5FB0:2,99D4:4,99D8:1,93EE:4,A6D9:3,A6DE:1,9614:1,9609:1" \
  --from forest_lowhp.state --btnscript "0:30,100:4,0:24,100:4,0:360" --diff --writer
```
| frame | event | evidence |
|---|---|---|
| f61 | Crono CONFIRMS (mid enemy-freeze) | `$99D4`=`00 FF FF FF` (**C1:16E5** — Crono pushed), `$99D8`=1 (C1:1716), `$93EE`=`41 80 00 03` (C1:1709, committed), `$A6D9`=`FF 01 FF` (dequeued), `$9609`→0 |
| **f228** | **enemy hit kills Crono** | `$5E30`→0 (**C1:ED12**), `$5E4A`→0x80 (**C1:B2B9** dead flag), **`$93EE`→`00 00 00 03` (`FD:A91A` — Crono's record ZEROED)**; `$99D4` still `00 FF FF FF` (stack still holds Crono) |
| **f232** | **pending entry popped, NO action** | **`$99D4`→`FF FF FF FF` (C1:85AE), `$99D8`→0 (C1:85B1)** — Crono's committed action is discarded |
| f251 | (a later, unrelated action animates) | `$A013`→1; **Het0 `$5FB0` stays `0E 00` = 14 the ENTIRE run** — Crono's attack NEVER landed |

Compare the ALIVE control (A.3, same commit): there Crono's attack animated (f236) and killed Het0 (f377/378).
Here (Crono dead) Het0 is untouched. **The committed action is CANCELLED by the attacker's death.**

### B.4 OUR divergence (Q2) — the schedule FIFO is not purged and the dispatch has no attacker gate

- **`battleDispatchScheduled` (battle.c:1665-1693) animates with NO attacker-alive check.** It pops
  `schedAtk[0]`/`schedTgtSlot[0]`, re-resolves the TARGET (skip-dead, 1677), then unconditionally
  `h->actActive=1; h->actAttacker=atk; …` — it never tests `battleAlive(s, atk)`. A dead PC's committed attack
  animates and lands.
- **`battleRemoveDead` (battle.c:588-591) purges only the ready-queue.** It loops dead+enqueued seats and calls
  `battleQueueRemove` (`$A6D9`) — it does NOT touch the `schedAtk`/`schedTgtSlot`/`schedN` FIFO nor clear
  `pcInTurn[atk]`. The death path `battleApplyDamage` (battle.c:818-824) sets block+0x1D bit7 (+ `$AF02` for
  enemies) and nothing else.
- **Result:** a PC commits (`battleCommitPcAction`, battle.c:687 pushes `schedAtk`), then an enemy action's
  span-end (`battleAdvanceAction`→`battleApplyDamage`) kills him the same tick `battleRemoveDead` runs
  (battle.c:2121) — the queue is cleaned but `schedAtk` still holds him — then `battleDispatchScheduled`
  (battle.c:2212) fires his attack. Ours does NOT cancel; the ROM does (B.3).

---

## PART C — FIX SPECS (mandate-clean; expressed in our commit/dispatch/FIFO/render terms)

### C.1 Q1 — re-validate the OPEN target cursor every frame (mirror the `$99C0`/`$A62D` rebuild, C1/2047-2099)

The ROM re-resolves the hand every menu-update by rebuilding the compacted living list and recomputing
`$A62D = $99C0[$9614]`. Our `$9614` is a physical slot, so the equivalent is a **per-frame skip-dead
re-validation of `$9614` while `$9609==1`**:

- Add, on EVERY frame that target-select is held (`$9609==1`), BEFORE the hand renders: if
  `s[0xAF02 + s[0x9614]] == 0xFF` (or `$9614 >= 8`), advance `$9614` to the next living slot — reuse the
  existing skip-dead walk `battleTargetNav(s, +1)` (battle.c:305) or `battleFirstEnemySlot(s)` (battle.c:297).
  This reproduces "the hand moves to the next living enemy" (A.3). The ROM's ~3-frame lag is cosmetic;
  re-resolving within the frame is acceptable and cleaner.
- **It must run PER FRAME, not tick-gated** — the enemy dies at an action span-end which is inside the
  `$A013` freeze for the pointing PC, so the re-validation belongs on the same per-frame path as the render,
  NOT only in the tick-gated step F. Options: (a) a per-frame guard in `CtBattleTick` right after
  `battleAdvanceAction` (battle.c:2004, where the kill lands) and also reachable in the frozen pass; or
  (b) clamp inside `battleTargetCursorRender` itself (field.c:3092) — resolve `slot` skip-dead before drawing
  (render-only, zero engine-state change, the tightest "hand never draws on a dead slot"). (a) also keeps the
  live `$9614` honest for a subsequent confirm; (b) is the minimal visual fix. Prefer (a) for state fidelity.
- **All-enemies-dead** is the victory condition (battle ends, `CtBattleTick` step G, battle.c:2219) so the
  cursor state is moot — no need to special-case "close target-select on empty" (the ROM never reaches it
  either; it would be victory).
- **Do NOT** change `battleDispatchScheduled`'s committed-target re-resolve (battle.c:1677) — it already
  matches the ROM's retarget (A.4).

### C.2 Q2 — cancel a committed action whose attacker died (mirror FD/A914 + the C1/8580 pop)

The ROM zeroes the dead actor's action record at death, so the executor's pop finds nothing. Two changes,
both needed for a clean mirror:

- **Purge the FIFO on death (the FD/A914 twin).** In `battleRemoveDead` (battle.c:588) — which already
  iterates dead seats — for each dead PC seat ALSO remove it from the `schedAtk`/`schedTgtSlot` FIFO
  (compact `schedN`) and clear `pcInTurn[seat]`. This is the direct analog of `FD/A8FE` zeroing the dead
  actor's `$93EE` record + the turn-latch clear. (Reset state already exists: `schedAtk`/`schedTgtSlot`/
  `schedN`/`pcInTurn` in `CtBattleBegin`, battle.c:1839-1840.)
- **Guard the dispatch (defensive, the executor's own dead-gate).** In `battleDispatchScheduled` (battle.c:1668)
  after popping `atk`, if `!battleAlive(s, atk)` then DISCARD (return without opening a window; do NOT set
  `actActive`) — mirroring `C1/8580` producing no action for an actor whose record was zeroed. This also
  covers the ordering window where death and dispatch land in the same tick.
- Net effect = the ROM's B.3: the dead PC's queued attack never animates, the enemy keeps its HP.

### C.3 What NOT to change / boundaries

- Do NOT scene-gate or enemy-gate (the death/cancel path `C1/ECDB → C1/B2B1 → FD/A8FE → C1/8580` has ZERO
  enemy-id branching — it is the general turn machinery; forest vs GATO is identical).
- Do NOT alter the confirm's commit semantics (battle_confirm_queue.md), the ATB reload/`pcInTurn` latch
  (battle_atb_reload.md), the damage pipeline, or the cadence — these are input/death-routing fixes only.
- After implementing: re-verify the pinned MASHA fights `field-battleloop-gato` / `field-battlewait-gato`
  (a committed PC that then dies is not in those scripts, so damage/victory frames must stay byte-identical),
  and add an interactive death-repro check.

---

## PART D — OPEN

- **The executor's own dead-gate `C1/8580` @185B1-185C0** (`LDA $B18B / CMP … / BEQ 185F3 / BRA 1860A`) was
  not cleanly disassembled (data-misaligned) so it is unproven whether the pop cancels because it re-reads the
  zeroed `$93EE` (B.2) OR independently tests `$B18B`'s dead flag. The BEHAVIORAL cancellation and the death-
  time `$93EE`-zero (`FD:A91A`) are byte-pinned regardless; the fix (C.2) mirrors the state change, so this is
  not load-bearing — but decode it before claiming the exact executor branch.
- **GATO cross-check not run.** The Q2 repro used the controllable weakened-Hetake forest fight (Crono at
  3 HP, an enemy physical kills him at freeze-end) — structurally the mission's "GATO kills Crono mid-freeze"
  scenario. The mechanism is enemy-agnostic (D/C.3), so GATO is the same path; a direct GATO capture would
  need a mid-battle low-HP `$5E30` state (patch a `PEEK_SAVE` snapshot AFTER staging — the pre-staging
  `gato_press_action.state` `$5E30` is overwritten by battle staging, confirmed at settle 40).
- **The exact menu-update cadence of the `$99C0`/`$A62D` rebuild** (the ~3-frame f378→f381 lag) is not pinned
  to a specific tick source; cosmetic for the fix (C.1 re-resolves within the frame).
- **`$99C0` slot semantics fully:** confirmed COMPACTED (`03 04 05 FF` all-alive → `04 05 FF FF` after slot0
  dies, NOT the slot-indexed `FF 04 05 FF`), FF-padded past the living count; the `$99C1`-build loop
  `12047-1207A` compacting rule was read structurally, not byte-decoded op-by-op.

---

## Reproduction commands (repo root; ct_probe.exe + SDL2.dll + ROM + ct_assets.dat in the scratch dir)

```
# --- Q1: the hand re-resolves when the pointed enemy dies (target-select stays open) ---
ct_probe --monitor "95D5:1,9609:1,9614:1,95DC:1,A013:1,AF02:4,99C0:4,A62D:1,960D:1,5FB0:2,5E30:2,5EB0:2" \
  --from saves/battle_re/forest_battle_3hetake.state --btnscript "0:30,100:4,0:24,100:4,0:184,100:4,0:220" \
  --diff --writer
#   f250 Marle target-select on Het0 ($A62D=03); f378 Het0 delisted ($AF02 slot0->FF);
#   f381 $99C0->04 05 FF FF, $A62D->04 = re-resolve to Het1, $9609 STILL 1, $9614 unchanged

# --- Q1b: a committed action whose target dies RETARGETS at dispatch (Het0 -> Het1) ---
ct_probe --monitor "A013:1,95D5:1,AF02:4,5FB0:2,6030:2,60B0:2,5EB0:2,9614:1,9609:1,A0D8:3,93F0:1" \
  --from saves/battle_re/forest_battle_3hetake.state \
  --btnscript "0:30,100:4,0:24,100:4,0:184,100:4,0:36,100:4,0:300" --diff --writer
#   f430 Marle's committed attack animates; f576 it lands on Het1 ($6030->0), not the dead Het0

# --- Q2: attacker death CANCELS the committed action (Het0 keeps full HP) ---
python patch_wram.py saves/battle_re/forest_battle_3hetake.state forest_lowhp.state 5E30=0300   # Crono 3 HP
ct_probe --monitor "A013:1,95D5:1,AF02:4,5E30:2,5E4A:1,5FB0:2,99D4:4,99D8:1,93EE:4,A6D9:3,A6DE:1,9614:1,9609:1" \
  --from forest_lowhp.state --btnscript "0:30,100:4,0:24,100:4,0:360" --diff --writer
#   f61 Crono commits ($99D4=00.., $93EE[0]=41); f228 enemy kills Crono ($5E30=0, $5E4A=80, $93EE[0]->0 FD:A91A);
#   f232 pending pop empties $99D4 (C1:85AE); Het0 $5FB0 stays 14 = attack CANCELLED

# --- ROM sites ---
python tools/dis.py 12070 0x40 0 0    # $A62D = $99C0[$9614] (C1:2088); JSR C1/27C5 = $99C0 rebuild (C1:27D2)
python tools/dis.py 1176C 0x50 0 0    # target nav: $99C0,X BMI skip-dead -> $A62D
python tools/dis.py 11620 0x140 0 0   # confirm: STZ $9614 (16D7); push $99D4 (16E5)/$99D8 (1716); $93EE record
python tools/dis.py 18590 0x40 0 0    # executor C1/8580: pop $99D4->$B18B, DEC $99D8 (85AE/85B1)
python tools/dis.py 3DA8FE 0x40 0 0   # death cleanup FD/A8FE: STZ $93EE,X (FD:A914) + timer reload
python tools/dis.py 1B2A0 0x40 0 0    # PC-death flag: STA $5E4A,X #80 (C1:B2B9) + $AEFF,Y=FF
python tools/dis.py 1ECC0 0xC0 0 0    # death path: dead flag (ED04) / zero curHP (ED0F-ED12); no cursor writes
```
