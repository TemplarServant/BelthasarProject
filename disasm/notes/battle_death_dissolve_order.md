# SIMULTANEOUS DEATHS DISSOLVE **ONE AT A TIME** — the serial death-routine queue

User report (translated from Portuguese): *"the two enemies, when dying, did the dissolve at the same time; in the original game it is one at a time."*
When Cyclone's AoE kills two enemies on the SAME frame our engine started both dither-dissolves together;
the ROM runs one death routine, then the other. Derived from the **oracle** only, with a discriminating
experiment that separates the two candidate ordering rules.

**Base state.** `saves/battle_re/forest_holdleft_pre.state` + `$2837 |= 0x80` (the canonical Cyclone-learned
injection), the 3× Hetake forest formation. Cyclone hits the cursor primary + every alive enemy inside
radius² 9 (`battle_multitarget.md`), so slots 0+1 die together and slot 2 survives.

---

## 1 — THE MEASUREMENT

The signal is **`$96F5,X`** — the per-combatant presence byte the bank-CF death handler clears when a
corpse's routine *finishes* (writer **CF:FE7E**, the `STZ $96F5,X` at `CF/FE7B`; located while deriving
`battle_end_ui_teardown.md`, where it is also what gates the victory scan). `--monitor "96F5:11,AF02:4"
--diff` over the whole cast:

| run | driver | delist (`$AF02`) | 1st corpse done | 2nd corpse done |
|---|---|---|---|---|
| **A** primary = slot0 | `40:60,0:275,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:900` | f707 | **cmb4** f750 (+43) | **cmb3** f793 (+43) |
| **B** primary = slot1 | …`,10:1,0:12,100:2,0:900` (one Up on the target cursor) | f723 | **cmb4** f766 (+43) | **cmb3** f809 (+43) |

Both enemies are delisted from `$AF02` on ONE frame (the span-end apply loop, `battle_multitarget.md` §3.3)
— but their death routines complete **43 frames apart**, and the second starts when the first ends.

### 1.1 The discriminating experiment (this is the load-bearing part)

Run A's hit set is `$99C0 = 03 04 FF` (primary cmb3 first); run B's is `$99C0 = 04 03 FF` — the **reverse**.
If the order followed the hit set, run B would dissolve cmb3 first. It does not: **cmb4 finishes first in
BOTH runs**. So the rule is **descending combatant index**, not hit-set order. (One run alone cannot tell
the two apart — with primary = slot0 they happen to predict the same thing.)

### 1.2 The waiting corpse stays intact

Peeking the OAM through run A's window and counting each enemy's sprites/palettes:

| rel | slot0 @(120,63) | slot1 @(88,95) |
|---|---|---|
| 312, 318 | 6 spr **pal1**,5 | 5 spr **pal1**,6 |
| 324 … 364 | 6 spr **pal1**,5 (untouched) | 5 spr **pal4**,6 (recoloured, dithering) |
| 372 … 402 | 6 spr **pal4**,5 (now dithering) | gone |
| 408+ | gone | gone |

So the queued corpse keeps rendering **exactly as it did alive** (pal1, undissolved) until its turn — the
death recolour pal1→pal4 (`battle_dissolve.md`) happens at the *start of its own* routine, not at the kill.

---

## 2 — WHAT OUR ENGINE DID INSTEAD

`battleAdvanceDissolves` (`src/battle.c`) advanced **every** `dissolveActive[slot]` in the same loop, and
`battleApplyDamage` arms one per victim on the span-end frame — so an AoE kill started N dithers together
and they finished together. Nothing anywhere serialized them.

---

## 3 — IMPLEMENTATION (shipped)

* `src/battle.h` — `int dissolveRunning;` = the ONE slot whose dither is advancing (−1 = none).
* `src/battle.c` `battleAdvanceDissolves` — if nothing is running, promote the **highest-index** active slot;
  advance only that slot; when it clears, release the token so the next corpse starts on the very next frame.
  The queued-corpse-renders-intact behaviour (§1.2) falls out for free: a queued slot's `dissolveStep` stays
  frozen at its `-CT_DISSOLVE_HOLD` arm value, and `battleDissolveRender` draws that as the live palette with
  no dither cleared.

**Validated** — opaque-pixel counts per enemy box across the forest AoE kill: slot0 holds a constant **621 px**
(fully drawn) through f770/f790/f800/f815 while slot1 falls 342 → 443 → 248 → **0**; only then does slot0
start, falling 463 → 170 → **0** by f860. One at a time, higher combatant index first. Pinned as
`field-battle-dissolve-serial` (shot f790: slot1 mid-dither, slot0 intact). Suite FULL 134/0/3 with **no**
re-pins — nothing else in the suite kills two enemies at once.

---

## 4 — OPEN

| # | item | what would close it |
|---|---|---|
| D1 | **The ROM side of the queue.** The serialization + order are measured; the actual death-routine dispatcher (which per-combatant slot it walks, and where the "one at a time" token lives) was not disassembled — the bank-CF handler around `CF/FE7B` is the entry point. | disassemble the CF death handler's caller and find the per-frame slot selector |
| D2 | **Three or more simultaneous deaths.** Both runs had exactly two victims. Descending index predicts 5→4→3, but that is an extrapolation from a 2-sample rule. | a 3-victim AoE kill (needs a formation where the radius covers three, or a stronger AoE tech) |
| D3 | **The 43 vs 41-frame length.** Each ROM death routine spans 43 render frames; our fixture is HOLD 9 + STEPS 32 = 41. The residual rides `battle_dissolve.md`'s bake, not this queue. | re-measure the dither step count against a per-frame OAM/fb capture |
| D4 | **Mixed death kinds.** GATO's anim-set-2 death *stands* instead of dissolving (`battle_death_dissolve_gate.md`); whether a standing death also occupies the queue slot (and so delays a dissolving one) is unexercised — no capture kills a stander and a dissolver together. | an AoE kill mixing anim-set 0 and anim-set 2 enemies |

---

# ADDENDUM — THE DEATH ROUTINE ALSO **FREEZES THE BATTLE TICK**

Follow-up user report (translated from Portuguese): *"the time, atb bars etc should not keep going while the two enemies have not
finished the dissolve animation."* Correct, and it is the same `$96F5` clock.

## A.1 Measurement (`--monitor "AFAB:3,A013:1,96F5:11,993A:1"`, same forest state)

**Two victims (Cyclone):**
```
f706  $A013 1->0   (span end)          $993A=01
f707  $AF02 = FFFF5EFF (both delisted) ATB reloads -> 48 0A FF
f708..f793   ATB *** 48 0A FF, UNCHANGED for 86 frames ***   $993A flat 00
f750  $96F5[4] -> 0   (cmb4's routine done)
f793  $96F5[3] -> 0   (cmb3's routine done)
f794  $993A=01 -> the tick resumes; f798 ATB 46 08, f804 43 05, f810 40 02 ...
```
**One victim** (primary = slot2, a single-target set — the same law, so it is not AoE-specific):
```
f720  $AF02 = 5E5EFFFF, ATB reloads -> 48 06 FF
f721..f763  ATB *** 48 06 FF, UNCHANGED for 43 frames ***
f763  $96F5[5] -> 0
f768+ ATB 46 04, 44 02, 42 00 ...
```
So the whole battle TICK — ATB, scheduler, dispatch, end-check — is held from the delist until the LAST
death routine completes. Note `$A013` is already **0** here: this is a *different* owner from the action
freeze, and it is the render side simply not producing `$993A`.

## A.2 What this explains, retroactively

`battle_end_ui_teardown.md` measured a delist→victory-scan gap that was **enemy-dependent**: 45/46 frames
for the dissolving ids but only **2** for GATO. That is not a property of the teardown at all — it is this
freeze. GATO's anim-set-2 death *stands* instead of dissolving, runs (almost) no routine, and so freezes
nothing. Modelling the wait here makes the teardown's own rule trivial again: the victory scan runs
immediately once it can see the win, exactly as `C1/8186 → C1/356D` does.

## A.3 Implementation (shipped)

`CtBattleTick`: `battleAdvanceDissolves` moved to the head of the simulation ticks (it owns the freeze), and
while `dissolveRunning >= 0`
* the enemy walk / think-dispatch / reactive-counter drains are skipped, and
* the function returns before the logic-tick accumulator — so no ATB decrement, no dispatch, no end-check.

The action-window FX and the dissolve itself keep advancing (they are render-frame work, like the popups).
The victory-side teardown gate collapses to `victoryHold > 0` — the freeze already guarantees the corpses
are done, so the two no longer encode the same law twice.

**Validated:** forest AoE kill — the next scheduler event moves f775 → **f855**, an 82-frame stall = the two
41-frame dissolves (oracle 86 = 2×43, the known dissolve-length residual). Single last kill — delist f1635,
hand closes the SAME frame, victory f1676 (+41), teardown f1677.

**Re-pin wave (all frame-axis, HP/attack-counts/field-fnv identical):** `field-battleloop-forest`
f1261→f1383 (+122 = 3 kills), `field-battle-forest-return` f1381→f1503 with the field fnv **unchanged**,
`field-battle-end-uiteardown`, and the two dissolve shots. ⚠ `field-battle-forest-lastkill-dissolve` had to
be RE-ANCHORED a second time (its kill moved f1017→f1100, so the old AT sat before the kill again);
`field-battle-ambush-generic-dissolve` legitimately changed content — that fight's first act-end kills BOTH
Beetles, so its shot now shows one dithering while the other waits, fully drawn.

---

## ADDENDUM (2026-08-23) — THE DEATH **SOUND** IS SERIALIZED TOO

User report (translated from Portuguese): *"when two enemies are killed with one cyclone, both do the dissolve animation,
but only one of them makes the sound"*.

The `#95` death sound is not a per-kill event fired at damage time. It is part of the DEATH
ROUTINE: `CD/0043` (vector #12, entered from `CF/FE76` with A = the dying combatant → `$CD00`) is
one corpse's whole death — the `$CCEA` wait, then `LDA #95 / JSR CD/3B71` with X = `$1D0C[$CD00]`
as the pan. Since the routines are serialized (this dossier), **so are the posts**.

The port armed the sound for every kill on the damage frame and fired all of them on the same
frame — two `#95`s into one driver tick, of which only one is audible.

### Measured

Same fixture and cast as the ordering runs above (`forest_cyclone = forest_holdleft_pre.state +
$2837 |= 0x80`), `--monitor "7E1E00:4" --writer --diff`:

```
f708   7E1E00=00955804      id 95, pan 58 = cmb4's screen X
f751   7E1E00=00957804      id 95, pan 78 = cmb3's
                            -> TWO posts, 43 frames apart
```

which lines up one-for-one with this dossier's own run A: `delist f707 → cmb4 done f750 → cmb3
done f793`. Each corpse posts one frame after its own routine starts, in the **descending
combatant index** order the dissolve already uses.

Ours, before and after:

| | first | second | gap |
|---|---|---|---|
| before | f565 cmb3 pan 78 | f565 cmb4 pan 58 | **0** (same frame, one audible) |
| after | f565 cmb4 pan 58 | f606 cmb3 pan 78 | **41** |

The fix is one gate in `battleDeathSfxTick`: a corpse whose dissolve is active but is **not the
one running** holds its countdown. A death with no dissolve at all (anim-set 2, the stand-in-place
corpses) never sets `dissolveActive`, so the gate is inert for it and its post keeps the timing it
already had.

Pinned by `field-battle-death-sfx-serial` (mutation: drop the gate → both posts collapse onto one
frame again, KILLED).

**Not closed:** our gap is 41 frames against the oracle's 43. The separation is `CT_DISSOLVE_HOLD +
CT_DISSOLVE_STEPS`, the run length pinned separately by `battle_dissolve.md` — so the two frames
belong to that constant, not to this gate.

## ADDENDUM (2026-10-13) — THE CORPSE STANDS STILL: THE SCAN STOPS ITS WALK, THE HEAD WAITS FOR THE SCAN

The user (translated from Portuguese): "after the final blow on an enemy, it moves a little before the dissolve".

**Measured**, the rule's cartridge, `forest_wait_ambush_active.state` with A held (`--monitor` of `$AF02`, `$98A7`, `$9819`,
`$A013`, `$A0FD`, `$1D0C`/`$1D23`, `$96F5`, `$0027`, `--writer`): the second Beetle (slot 2) and the Avian Chaos (slot 5)
die with a walk segment in flight (`$98A7,X = 01`). On the delist's frame (f819 / f1361) the segment flag reads 00
(writer CF:FE36), and the corpse's foot holds through the dissolve. `$A0FD` stays 01 from the action's open until
`C1:1076` zeroes it at f613 / f862 / f1404 -- the frame each corpse's `$96F5` clears (CF:FE7E), with `$0027`++ (C1:8124):
the loop's head runs when the scan returns, 43 frames after the delist.

**The ROM** (`dis.py 0FFE20 0x65 1 0`): CF/FE20, X = 10 down to 3, for each `$AEB0,X` set:

```
FE28  LDA $9FF7,X / BNE FE7B
FE2D  INC $A0A8,X
FE30  STZ $AEB0,X
FE33  STZ $98A4,X        ; $98A7 + slot: the corpse's segment flag -- CF/F978 never steps it again
FE36  LDA $99B5,X / BEQ FE7B ... (the record's +7 bit 6, the palette copy) ... FE76 JSL CD/0024 (the routine)
FE7B  STZ $96F5,X
```

and CF/EFAF, the NMI's stepper, runs only while `$A013` and `$A0FD` are both 0.

**The port** kept the corpse's segment, so CF/F978 stepped it on: the Avian slid 7 px right, the Beetle 1 px right and
6 down, while they dissolved. Now `battleDeathScanStores` (CF/FE30-FE33) runs at each routine's start
(`battleAdvanceDissolves` picking it) or at the death for a corpse with no routine. Check
`battle-corpse-holds-its-foot-through-the-dissolve-vs-rule-cartridge` (tools/corpse_still_cmp.py): 0 of 3 corpses move;
before, 2 of 3.

**Not made, and why:**
- CF/FE2D `INC $A0A8,X`: the port's C1/2FBD store (`battleStatusScanStore`) already sets `$A0A8,X = 1` on the death's
  frame, which the cartridge does not (party3_field.state: `$A0AC` reads 00 until CF:FE30, then 01); the INC would leave 02.
- The head's wait: the cartridge's C1/1076 `STZ $A0FD` comes when the scan returns (43 frames from the delist), which stops
  EVERY walker through the routines; the port's head runs on the delist's frame (`$A0FD` 0 at once -- the living walkers
  step through the routine). Deferring it (the head pending while `dissolveRunning >= 0`) was tried: the forest fight held,
  but with the routine 3 frames short (D3: the port's `$A0FD` held 40 frames against 43) party3_field's next enemy turn
  chose another attack (`$9877` 04 against the cartridge's 05) and four pinned fights moved
  (battle-slow-doubles-reload-and-cuts-damage, field-battle-lucca-type2, field-battle-bat9-return,
  field-battle-solo-victory-no-marle). Do D3 first, then the head's wait.
