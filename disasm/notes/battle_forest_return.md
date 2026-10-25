# FOREST BATTLE — interactive entry CAMERA · attack/bracket COORDINATE BASES · post-victory RETURN (freeze + Hetakes reappear)

TRIAGE + DERIVATION (ROM disasm + LakeSnes oracle only) of three user reports about the **interactive**
forest fight (scene 19, hold-LEFT trigger, 3× Hetake). HEAD b43f204. **Nothing implemented; src/ + git
untouched.** Method: our engine = scratch `ct_field_probe.exe` driven through the *genuinely interactive*
entry (`--scene 19 --loadwram <pre-E7 forest wram>` + `--walkscript "l:N"` = hold LEFT); oracle =
`ct_probe.exe --monitor/--peek` from `saves/battle_re/forest_holdleft_pre.state`. FB decode 256×224
[u32 w][u32 h][RGB]; our-y = oracle-fb-y − 8.

> **⚠ The two forest battle regression checks CANNOT see reports 1 & 3.** `forest_pre.raw.wram` (the
> `field-battleloop-forest` seed) is captured **post-op-0xE7 and D8-pending**: bytes read `$1D0A=00 $1D0E=44`
> (arena origin already settled) and `$0117=01` (battle already pending). So the battletest inherits the
> oracle's camera and the parked D8 — it never runs our op-0xE7 (report 1) and, because `--battletest` sets
> `selftest=1` → `interactive=!selftest=0` → the **headless synchronous** battle path (`CtBattleRun` +
> `fieldBattleFinish`, no `battlePhase`), it never exercises the **interactive post-battle field resume**
> where the freeze lives (report 3). `field-battle-forest-camera-composite` likewise boots `--loadwram
> forest_pre_wrapped.lakewram` (same settled origin). Both are green and both are blind to these two bugs.

---

## VERDICTS UP FRONT

1. **Camera (report 1): the mechanism WORKS; the FINAL framing is correct.** Driven through the real
   interactive entry, our VM DOES execute op-0xE7 `E7 00 22`, which snaps `$1D0A=0 / $1D0E=0x44` (arena
   origin), the render camera arms from those, and the composite framing MATCHES the oracle
   (combatants land ON the terrain). The wave-1 fix is effective. The only residual is the **pre-battle
   camera PAN**: the oracle SEEKS the camera ~1px/f over ~116f (a visible glide + party-walk cutscene);
   ours SNAPS at battle entry (the field render camera never tracks `$1D0A/$1D0E` during the pre-battle
   cutscene). Cosmetic — the settled battle framing is byte-right.
2. **Attack + bracket (report 2): a REAL mixed-coordinate-base bug.** After wave 1 every PC render is
   foot-anchored via `battlePcAnchorDelta` EXCEPT (a) `battleBracketRender`, which still uses the fixed
   absolute `ctBracketPos[]` table → the bracket frame detaches from the live-staged PC; and (b) Crono's attack
   LUNGE, whose endpoint is a fixed baked offset (toward GATO's old position) → **target-independent**, so
   it lands nowhere near a forest Hetake that sits somewhere else.
3. **Post-victory (report 3): GAME-BREAKING, ONE root cause.** obj1 (the ambush controller) parks its
   post-battle script in a **regroup-WAIT loop (op-0x22 @ our IP 0x00E5) that never completes** because the
   lead (Crono) is frozen and never reaches the polled spot → it never advances to the op-0xE3 that would
   restore control (`$011F`→1) and never runs the enemy-cleanup ops. Result: **Hetake field sprites stay
   visible ("reappear") AND the party is frozen ("game stuck")** — both proven visually + by RAM.
   **⚠ SUPERSEDED — the mechanism attribution in this verdict + Part C (obj1's script as the restorer,
   op-0x22 as a regroup wait) is WRONG; the symptoms and our-engine deadlock are real, but the canonical
   restore is a follower-object VM CALL to an ENGINE-INSTALLED regroup script ($7F3700), and the enemy
   removal is a battle-epilogue dead-mark ($1000,obj bit7). Fully derived, writer-pinned, with the
   implementation spec, in the ADDENDUM 2026-07-12 at the end of this file.**

---

## PART A — CAMERA TRIAGE (report 1): "the camera is not moving to the correct position"

### A.1 The oracle interactive entry (confirmed, `--monitor "1D00,1D02,1D0A,1D0E,0117" … --input "40:24,0:170" --diff --writer`)
```
f0    $1D0A=03 $1D0E=3C   $1D00=0                       ; camX=3(24px) camY=60(480px)
f2-10 $1D0A 3->2->1 (C0:949F/9491)                      ; FIELD camera follows the held-LEFT walk to the left bound
f52   $1D00=1 (C0:3E57) target $1D02=00,$1D03=44 (C0:3E54) ; op-0xE7 'E7 00 22' ARMS: targetX=0, targetY=0x44=68
f60-116 $1D0A->0 (C0:949F); $1D0E 3C->44 (C0:94C1/94CF) ; X reaches 0 (field follow) + Y SEEKS 60->68 (~1px/f)
f117  $1D00=2 (C0:91F6)   both axes arrived
f120  $1D00=0 (C0:3E3B)   op advances
f168  $0117=2100 (C0:3AA8) D8 fires   ; ARENA ORIGIN = $1D0A=0 (camPx 0), $1D0E=0x44 (camPy 544)
```
So the arena origin (0, 544) is produced by TWO writers: X→0 by the **field-camera follow** (C0:949F, held-LEFT
reaching the left bound) and Y→0x44 by the **op-0xE7 SEEK**. op-0xE7's TARGET is (0,0x44) — its X target is 0 too.

### A.2 OUR interactive entry produces the SAME origin (the divergence hypothesis is REFUTED)
`forest_pre.raw.wram` is post-E7, so I built a **pre-E7 seed**: `ct_probe --peek forest_holdleft_pre.state
pre_e7 --btnscript "40:24,0:16"` → `pre_e7.raw.wram` at oracle-f40 (`$1D0A=01 $1D0E=3C $1D02=00 $1D03=00
$0117=00` — before the E7). Wrapped (`HEADER\nRAM:131072:`+raw) and driven:
`Belthasar --scene 19 --loadwram pre_e7_wrapped.lakewram --walkscript "l:130"`, dump at our f60:
```
$1D02=00 $1D03=44   ; op-0xE7 RAN (it wrote the target)
$1D0A=00 $1D0E=44   ; ARENA ORIGIN correct (identical to the oracle D8-moment)
$0117=01            ; D8 pending
```
Our op-0xE7 (`ct_vm.c:1414-1427`) SNAPS BOTH axes `$1D0A=$1D02=0`, `$1D0E=$1D03=0x44` in one visit (our render
camera never needs the field-follow — the E7 X target is 0). The render camera arms from these
(`field.c:5337-5343`) on the `battlePhase` entry frame → `camx=0, camy=544`. **Composite proof:** the
interactive composite (`CT_BATTLESHOT_FIELD_AT=90`) renders the party + 3 Hetakes correctly ON the forest
terrain, and its terrain framing is **pixel-identical to the wave-1 (`forest_pre_wrapped`) composite** — which
reproduces the pinned `field-battle-forest-camera-composite` fnv `2047A632` exactly. So our E7 path == the
oracle-settled path == the validated framing.

- A **fresh** `--scene 19` load is NOT a valid repro: no party ($2980=`00 80 80`, Marle absent), storyline 0,
  lead at tile (0,0), the ambush never arms ($0117 stays 0). The ambush only exists from a proper forest state.

### A.3 The residual = the missing pre-battle PAN (the only thing that "doesn't move")
The oracle glides the camera over f52→168 (~116 field frames: Y seek 64px + the op-0xD9 party-walk). Our engine
(a) snaps `$1D0A/$1D0E` instantly and (b) the field RENDER camera (`field.c` stateful `camX`/`camy=py−112`) is
**player-follow and NOT gated to `$1D0A/$1D0E` until `battlePhase`** — so through the (compressed ~20f) pre-battle
cutscene our camera sits at the player-follow position, then SNAPS to (0,544) at entry. The FINAL position is
right; the visible pan/seek is absent. This is the most likely thing the user perceives as "the camera not
moving to the battle position": it doesn't glide there, it jumps.

### A.4 FIX SPEC (camera) — structural, low-risk
- **No change needed for the final framing** — it is already byte-correct (proven A.2). Do NOT re-touch the
  arena-origin snap; do NOT scene-gate.
- **(Optional fidelity) render the pre-battle seek.** While op-0xE7 blocks (`$1D00∈{1,2}`, before `battlePhase`),
  drive the field render `camX/camy` toward `$1D02·8 / $1D03·8` at ~1px/f (mirror `C0/91AC`), instead of leaving
  it player-follow, so the cutscene pans like the oracle. Source = the op-0xE7 target already in `$1D02/$1D03`;
  it changes nothing about the settled framing. This is the unported field-camera-seek milestone
  (`battle_entry_camera.md` E, `ct_vm.c:1400`), now the sole camera gap for the forest.

---

## PART B — COORDINATE-BASE AUDIT (report 2): attack lands off the enemy + bracket ≠ char

After wave 1 the PC anchor law is `anchor = 16·relX+fineX+extentX, 16·relY+fineY+extentY` with `rel/fine`
**live** from `$29B0+5·seat` (+GATO extent, since we ship the GATO PC pose) — computed by `battlePcAnchorDelta`
(`field.c:1857`). Enemies use the same via `battleEnemyAnchorDelta` + the live-walk `battleEnemyLiveDelta`.
Every battle render was audited for which base it uses in the FOREST:

| render (field.c) | base USED | base REQUIRED | verdict | oracle evidence |
|---|---|---|---|---|
| `battleStanceRender` (PC idle/waiting) | computed `battlePcAnchorDelta` | computed | ✅ OK | A8.1/D.2 |
| `battleCronoIdleRender` (active bob) | computed | computed | ✅ OK | A8.1 |
| `battleEndPoseRender` (victory/fallen PC) | computed | computed | ✅ OK | end T1/T4 |
| `battleGatoRender` / `battleHetakeRender` | computed `battleEnemyAnchorDelta`+live | computed+live | ✅ OK | A8.1 + enemy_move §A |
| `battleTargetCursorRender` (hand) | computed enemy anchor+live | computed | ✅ OK | scenario2 T3.2 |
| `battleFxPopupsRender` (dmg numbers) | live foot `mvLiveX/Y` or `16·rel+fine` (`battle.c battlePopupOrigin`) | live foot | ✅ OK | actionfx P1.3 |
| `battleDissolveRender` (Hetake corpse) | computed+live | computed | ✅ OK | dissolve gate |
| **`battleBracketRender` (the bracket frame)** | **fixed abs `ctBracketPos[]`** | **computed (foot-anchored)** | **❌ WRONG** | stance ADD6 A6.2 |
| **Crono attack LUNGE** (`battleAttackRender` traj) | **origin computed, but lunge = fixed baked offset toward GATO** | **target-relative (toward victim's live foot)** | **❌ WRONG** | actionfx A2 (GATO-only) + enemy_move §A |
| Marle attack body / spark | computed body + spark on `battleEnemyComputedAnchor(target)` | (she's ranged: planted body ✓, spark on target ✓) | ✅ OK | actionfx MA.2/MA.3 |

### B.1 The BRACKET — the only PC element NOT foot-anchored
`battleBracketRender` (`field.c:2637-2678`) picks `ctBracketPos[]` (`src/battle_bracket.h`) by fight+PC and
uses `pos->originX/originY` **raw** — no `battlePcAnchorDelta`:
```
ctBracketPos = { GATO Crono (122,108), GATO Marle (164,75),
                 Forest Crono (148,35), Forest Marle (115,108) }   // baked ABSOLUTE from the capture
```
But the PC sprite is drawn at the **computed live** anchor. Concrete (my interactive forest, live `$29B0`):
```
Crono staged 0A 04 09 07 00 -> computed anchor (144,35)  ; bracket fixed at (148,35)  -> off by (+4,0)
Marle staged 07 09 08 01 00 -> computed anchor (108,97)  ; bracket fixed at (115,108) -> off by (+7,+11)
```
The offsets happen to be small in my mid-walk seed because it stages close to the capture; the STRUCTURAL fault
is that the bracket is a FIXED absolute while the PC is COMPUTED-LIVE, so they diverge by exactly the amount the
live staging differs from the forest capture (the follower/lead residues, `battle_entry_camera.md` C/D.3; and
they can never match a fight whose formation differs from the two baked captures). A6.2 already recorded that the
bracket→PC offset is pose-dependent ("GATO Δ+13,0; Forest Δ+4,−5") — so a per-fight absolute bake is wrong; the
bracket must track the drawn PC.

**FIX SPEC (bracket):** anchor the bracket to the computed PC, same law as everything else — apply
`battlePcAnchorDelta` to the **GATO** bracket origin (we render the GATO PC pose in all fights, so its
bracket→anchor offset is the right one): `origin = ctBracketPos[GATO][pc] + battlePcAnchorDelta(seat)` =
`computedAnchor + (+13,0 for Crono / +0,+12 for Marle)`. Drop the per-fight `Forest_*` rows (or keep only as the
offset reference). This makes the bracket frame follow the active PC's live foot in any fight — structural, matches
stance/attack/idle. (A byte-exact non-GATO PC *pose* is the separate stance-bake OPEN, `battle_stance.md` A8.4.)

### B.2 Crono's ATTACK LUNGE — target-independent (baked toward GATO)
`battleAttackRender` (`field.c:2324-2336`) draws pose `ctBattleAnimKf[fxAtkPose]` at
`s->x + fxAtkDX + pdx` where `pdx = battlePcAnchorDelta(Crono)`. So the swing ORIGIN correctly follows Crono's
live foot — but `ctBattleAnimObj`/`ctCronoAttackTraj` are baked ABSOLUTE from the GATO fight, where GATO sat
ABOVE Crono, so the strike-pose lunge is a fixed `(dx≈+6, dy≈−13)` "up-forward" hop (`battle_actionfx.md`
A2.1). Adding only Crono's own foot delta translates the WHOLE hop to his live foot but keeps its **direction
fixed** — the lunge always aims where GATO was, never at the actual victim.

Geometry proves the forest miss: our Crono computes to anchor ≈(144,35); the three Hetakes are at
(104,35)[left], (72,67)[lower-left], (152,102)[lower-right]. The baked lunge goes UP (to y≈22) toward none of
them; attacking the lower-right Hetake it lunges the OPPOSITE way. = "hitting a spot that does not match
the enemies' visual position."

**FIX SPEC (attack):** make the excursion endpoint **target-relative**. The dispatched victim is known
(`h->actTarget`); its live foot is `battleEnemyComputedAnchor(actTarget−3)` + `battleEnemyLiveDelta` (the same
anchor the popup + spark + cursor already use correctly). The lunge/strike anchor should be placed relative to
THAT (approach/adjacent to the victim), not a fixed offset from Crono's idle. **⚠ derivation boundary:** the
GATO capture is single-enemy (`battle_actionfx.md` A2 only), so the exact PC approach law is not yet oracle-
pinned — but `battle_enemy_move_target.md` §A proves the ROM has a real per-combatant MOVEMENT subsystem
(GATO WALKS to his victim, box-overlap arrival C1/28B0) and A2.1's "A1 approach" phase shows Crono steps toward
the target too, so a PC physical is an approach-to-the-victim, not an in-place fixed hop. A clean oracle capture
of Crono attacking a NON-GATO-positioned enemy (e.g. a forest Hetake at (152,102)) is needed to pin the exact
endpoint/approach; until then the minimum correct behavior is to key the lunge off the victim's live anchor.
Marle's ranged attack already does this for its spark (`field.c:2388`) and needs no change (planted body is
canonical, MA.2).

---

## PART C — POST-VICTORY: the freeze + Hetakes reappearing (report 3, ONE root cause)

> **⚠ THIS PART IS SUPERSEDED by the ADDENDUM 2026-07-12** (end of file). C.1's "obj1 fires op-0xE3 once
> the regroup completes" and C.5's "drive the lead to the spot obj1 polls" are refuted there: op-0x22
> @0x00E5 is obj1's ordinary trigger-poll idle loop, obj1's post-D8 continuation contains NO `E3 01`, and
> the wait target the C.2 deadlock describes does not exist. The real mechanism (follower VM CALL to the
> $7F3700 regroup script + epilogue dead-mark) and the corrected fix spec are in the addendum. C.2/C.3's
> byte-pinned description of OUR engine's hang remains valid as the symptom record.

### C.1 What the ROM does (oracle, forest_holdleft_pre + hold-LEFT + A-mash to victory; `--monitor "0F00:32,011F,027C,0117,1180" --diff --writer`)
The whole fight resolves, control is handed back, then the field REGROUPS and RESTORES control:
```
f12    $011F 01->00 (C0:3C0D)   ; op-0xE3(0) LOCKS control as the ambush cutscene starts
f168   $0117=2100               ; D8
f235   $1B32=FFFF               ; battle module takes over; f239 all $0F00 -> 00/29 (combat)
--- fight ---
f1581  $0118 bit0=1 (C0:08D2)   ; battle DONE (ack); obj2/obj3 (Crono/Marle) redrawn $0F00=..80..80..
f1599  $0117=0000 (C0:...)      ; the parked op-0xD8 consumes $0118, advances; obj1 IP 0x5EC
f1599-1650  obj2 (Crono) anim frames cycle (1D 98 1E 1F 99 1C 1D 05 06 07) ; the post-battle REGROUP/settle
f1650  obj0 draw 00->06         ; lead reappears
f1651  $011F 00->01 (C0:3C0D)   ; op-0xE3(1) RESTORES control -- obj1 (ambush controller) at IP 0x5CD
```
- **Control restore = op-0xE3 (writer C0:3C0D)** run by **obj1** (the ambush controller: its IP loops
  0x5CD↔0x5F7 post-battle and fires op-0xE3 once the regroup completes). op-0xE3 is the SAME handler that
  LOCKED control at f12.
- **Enemy field objects:** in the oracle the Hetake field objects are `$0F00`=0 (never drawn) through the whole
  entry and STAY removed post-battle (only obj0/obj2/obj3 = the party ever have draw≠0). So the ROM keeps the
  triggering enemies gone. **GATO is the control:** GATO the field NPC canonically STAYS after his fight — the
  enemy-persistence differs per encounter (removed for the wild Hetake ambush, kept for the GATO story NPC), so
  a correct engine must NOT unconditionally re-show or unconditionally kill; it must run the controller's own
  post-D8 script (which does the right thing per encounter).

### C.2 What OUR engine does — the hang, byte-pinned
Driven interactively to victory (`--loadwram pre_e7_wrapped --walkscript "l:1600" CT_BATTLE_MASHA=1`): the
battle resolves cleanly (trace: `victory f751 → results-advance → victory-handoff f980`, `battle-reward xp=9
tp=3 gold+48 drops=1 itemBD`), the ack fires, the D8 advances (`$0117`→0). **Then it deadlocks.** State at
f1100 / f1400 / f1600 (identical — nothing progresses):
```
$011F = 00   (control NEVER restored; oracle restored it at f1651)     Crono(obj2) tile=(10,38)  frozen
$0117 = 00   (battle done)                                             Marle(obj3) tile=(7,43)   frozen
$027C = 03   (normal for the forest -- oracle holds 03 the whole time; not a stuck transition)
```
`CT_VMTRACE_ALL` pinpoints **obj1 = the ambush controller** (E7@0x9F → D9@0xA2 → D8@0xA9). Its post-D8 script:
```
[run obj1] @00A9 op=D8         (battle; blocks on the ack)
[vm  obj1] @00AC op=77 (0B..)  ; op-0x77 zero-local
[vm  obj1] @00B5 op=7C (52..)  ; op-0x7C hide-object (actors[t].draw=0)
[vm  obj1] @00E5 op=22 (00 07 08)   <-- STUCK: printed 5338 times (once/frame, forever)
```
**op-0x22 @0x00E5 is a "wait for Crono to reach the regroup spot" poll** — `GetObjCoords` (ROM C0/65EB,
`ct_vm.c:581`): it reads PC[0]=Crono's live tile (`sc->fx[0]>>4`) into locals 0x07/0x08, and a following
step-side compare/branch loops back to 0x00E5 until Crono is at the target (the same idiom scene-1 Mom uses to
"wait for Crono to reach the spot"). It never completes: **Crono is frozen at tile (10,38) and never walks to
the polled target**, so obj1 never advances to the op-0xE3 that would restore control, and never reaches the
later cleanup ops. The DEADLOCK is circular: control-restore is gated behind the regroup-wait, but the regroup
walk that would satisfy it never runs while control is off and our post-battle resume doesn't drive the lead
there.

### C.3 The Hetakes "reappear" — same root cause, proven visually
Seeded the FROZEN post-battle WRAM and rendered the field (`--loadwram postvict_wrapped --selftest
frozen_field.raw`): the screen shows **all 3 Hetake mushroom field sprites still on the forest floor** with
Crono/Marle standing frozen — exactly "the Hetake reappear outside the battle and the game gets stuck." The
enemy-removal ops that would clear them live in obj1's post-D8 script AFTER the stuck op-0x22 wait, so they
never execute. (The live `$0F00` mirror reads 0 for those slots, but the field render uses the CtScene
`actors[].draw`, which still shows them — a mirror/actor divergence, but the render is the ground truth of what
the player sees.) Both symptoms are the ONE deadlock.

### C.4 Why the regression suite is blind
`--battletest` → `selftest=1` → `fieldBattleConsumer(vm, interactive=!selftest=0, …)` → the **headless
synchronous** path (`CtBattleRun` then `fieldBattleFinish`, `battlePhase` never set) — it runs the battle and
the ack but NEVER enters the interactive per-frame field resume where obj1's post-D8 script (and the deadlock)
run. So `field-battleloop-forest`/`-gato` are green while the interactive resume is broken. **The freeze is
almost certainly NOT forest-specific** — it is the interactive post-battle field resume failing to satisfy the
controller's regroup-wait; the GATO interactive return likely hits the same class of hang (untested).

### C.5 FIX SPEC (post-victory) — structural direction
The interactive post-battle field resume must reproduce the ROM's party REGROUP so the controller's op-0x22
wait completes and its script proceeds to op-0xE3 (restore `$011F`) + the enemy-cleanup ops:
- **Drive the lead (and followers) to the regroup spot the controller polls for.** `fieldBattleFinish` currently
  calls `CtSceneRegroupFollowers` (a follower gather) but leaves the LEAD frozen at (10,38); obj1's op-0x22
  polls PC[0]=the LEAD. The ROM walks the party back (the conga reform / battle-return, `battle_end.md` T2;
  the lead SNAPS to the respawn cell `$0102/03/04` which `fieldBattleConsumer` already stages at battle entry,
  the followers walk in). Ensure the lead is placed at that respawn cell (and its live `fx/fy` updated) so the
  op-0x22 poll can match, and let the controller's own script run the regroup wait to completion instead of a
  detached one-shot.
- **Do NOT** special-case the forest, hardcode `$011F=1`, or skip obj1's script — the control-restore + the
  per-encounter enemy handling (remove Hetakes / keep GATO) are both IN obj1's post-D8 script; running that
  script to completion is what fixes both symptoms at once (and keeps GATO correct).
- **Exact wait target = OPEN (D below):** the tile obj1's compare expects for Crono is one derivation away
  (disassemble the compare op after op-0x22 in obj1's script, or `--monitor` obj1's compared locals across
  f1599-1651). The ROOT (regroup-wait deadlock → op-0xE3 never runs) is pinned; the exact satisfaction tile
  sharpens the fix.

---

## PART D — OPEN / boundaries
- **D1 (report 1) pre-battle camera SEEK** — the ~116f op-0xE7 glide + the field-camera X-follow are not
  rendered (we snap). Cosmetic; the settled framing is byte-correct. Same unported field-camera-seek milestone
  as `battle_entry_camera.md` E.
- **D2 (report 1) fresh-play trigger** — verified only from a proper forest state (the ambush needs party +
  storyline + position a fresh `--scene 19` lacks). The op-0xE7 mechanism is proven; the touch/coordinate
  trigger that STARTS the cutscene in real play is a separate mechanism (not exercised here).
- **D3 (report 2) PC attack approach law** — the exact target-relative endpoint/approach for a PC physical is
  not oracle-pinned (GATO capture is single-enemy). Needs a capture of Crono attacking a non-GATO-positioned
  enemy. The SYMPTOM (fixed baked lunge, target-independent) and the required direction (key off the victim's
  live foot) are certain.
- **D4 (report 2) non-GATO PC stance pose** — we render the GATO sword-stance in every fight; the bracket fix
  uses its offset. A byte-exact forest PC pose (its own extent) is the standing `battle_stance.md` A8.4 item.
- **D5 (report 3) the op-0x22 wait target tile** — the compare after the poll (which tile Crono must reach) is
  not disassembled; needed for the byte-exact regroup fix. Root cause + deadlock proven without it.
- **D6 (report 3) enemy-removal op** — which post-D8 op(s) in obj1's script remove the Hetake field objects
  (op-0x7C/0x91 hide vs a per-object kill flag) is beyond the stuck point; derive by disassembling obj1's
  script past 0x00E5 (unreachable in-engine until the wait is fixed) or by monitoring the oracle's obj-attribute
  writes f1599-1660.
- **D7 (report 3) GATO interactive return** — the freeze is interactive-path-general; confirm/repro the GATO
  interactive post-battle resume (the mission's control case) — expected to hit the same class.

## Repro / artifacts (scratchpad, NOT committed)
- `ct_probe.exe` (oracle), `ct_field_probe.exe` (+SDL2.dll) — scratch copies.
- Seeds: `pre_e7.raw.wram` (oracle-f40, pre-E7) + `pre_e7_wrapped.lakewram` (`HEADER\nRAM:131072:`+raw for
  `--loadwram`); `postvict_f1100/1400/1600.wram` + `postvict_wrapped.lakewram` (frozen post-battle).
- Renders: `ours_forest_comp.png` (our interactive composite, camera correct), `wave1_forest_comp.png`
  (== pinned fnv 2047A632), `frozen_field.png` (the freeze + Hetakes reappearing).
- Logs: `obj1trace.log` (obj1 stuck op-0x22), `allvm_pre.log` (obj1 = E7/D9/D8 controller), `postvict_run.log`
  (victory→handoff→reward, then deadlock).
- Oracle commands inlined per section (A.1, C.1).

---
---

# ADDENDUM 2026-07-12 — POST-VICTORY RETURN DERIVED END-TO-END (writer-pinned, ROM-cited); Part C corrected; donor-stack hypothesis REFUTED

Third round on the ONE question: *what invokes the op-0xE3-carrying handler after victory, restores
control, and removes the enemy objects.* Answer, in one line: **the field's battle-return resume routine
(`C0/0330`) VM-CALLs an ENGINE-INSTALLED regroup script at `$7F3700` on each FOLLOWER PC object; that
script walks the follower to the lead and its `E3 01` tail restores `$011F`; the enemy objects are removed
by a battle-epilogue dead-mark (`$1000,obj |= 0x80`) keyed on the battle staging alive byte.** No scene
object performs the restore; obj41 fn1 (the only `E3 01` in the SCENE script) is the post-ambush NPC's talk
handler and never runs at the return. Everything below is from `ct_probe.exe` (HEAD b60dfa9 oracle) driven
from the FIELD-SIDE state `saves/battle_re/forest_holdleft_pre.state` + `tools/dis.py` + the ROM bytes.

## AD.0 — Folded prior-round notes (were scratch-only: repin_ledger STEP 4/5)

- **op-0x22 @0x00E5 (C.2's "regroup wait") REFUTED**: it is a plain `GetObjCoords` inside obj1's ordinary
  trigger-poll IDLE loop (live 0x054B `22 00 07 08` → IFFLAGs → IF loc → `11 xx` JUMPBACK; the loop
  re-checks the 3 ambush trigger locals every frame). There is no post-battle wait-for-the-lead poll.
- **obj1's post-D8 continuation has NO `E3 01`**: per group it does `77 xx` (zero the trigger local),
  IFFLAG glb[211] → `7C 52`/`7D 52` (hide/show obj41), and re-enters the poll loop (live park 0x05CD).
  Its `E3 00`s are the ambush-START locks of the *next* groups' blocks, not restores.
- **obj41 fn1 (`E3 00 / 65 82 D3 / 91 / … / BB 00 / E3 01` — static 0x04D3, live 0x0A14) = the TALK handler**
  of the post-ambush NPC at tile (46,33): the standard textbox lock/unlock pattern. `--followvm --obj 41`
  through the whole return: it idles at its fn0 RETURN (live 0x0A12) from f1601 to f2573 — never invoked.
  (Live script = static + 0x541; the two prior lanes compared static-decode offsets against live IPs and
  mis-matched.)
- The prior byte-scan "no direct `$011F` writer in bank C0 besides op-0xE3/D8" was **incomplete**: it
  scanned absolute forms (`8D 1F 01`/`9C 1F 01`) and missed **direct-page** writes (field code runs with
  D=$0100): `C0/0359: LDA #01 / STA $1F` is a genuine direct `$011F=1` writer — the resume's
  no-follower branch (AD.3b).
- STEP 5 (camera, Part A): a blocking op-0xE7 + per-frame seek was implemented and REVERTED — it hung the
  headless `--battletest` path (no per-frame seek there, `$1D00` never cleared). A.4's status stands: the
  settled framing is byte-correct; the pre-battle pan remains the unported field-camera-seek milestone.

## AD.1 — The wall was the INPUT SCHEDULE, not the states (donor-stack hypothesis REFUTED)

- Working schedule (LakeSnes serial-bit grammar; A=0x100, LEFT=0x40), from the FIELD-SIDE state:
  `--btnscript "40:24,0:150,(100:20,0:40)×N"` with N≥27. The `100:20,0:40` cadence produces clean A
  press/release EDGES through target-select confirms AND the results dismissal (battle_loop §G/§H: tighter
  mashes can wedge `$A013`).
- **The donor-spliced in-battle state ALSO returns** under the same cadence: `forest_battle_3hetake.state`
  (GATO-donor CPU/stack context) → ack `$0117=0001/$0118=01` f1428 (C0:18C1) → handoff f1442 (C0:A730) →
  D8-consume f1446 (C0:3AB1) → **`$011F=1` f1498 (C0:3C0D)**. So the converted states are NOT
  return-broken by construction; the two prior lanes' "$0117 never clears under any input" was their mash
  schedules never producing a clean dismiss edge. (Monitor: `runC_donor.log`.)
- A single-press dismissal also works: mash to the kill (~22 rounds), idle — the results HOLD indefinitely
  (battle_end T1) — then one `100:2` press: ack fires the next frame (observed f1735 after a press at
  ~f1734; restore f1805).

## AD.2 — THE RETURN TIMELINE (field-side drive, `--monitor --diff --writer`, one representative run)

```
f12    $011F 1->0            C0:3C0D   op-0xE3(0) — obj1's ambush cutscene lock
f52    $1D02/03 = 00/44      C0:3E54   op-0xE7 arms (Part A)
f120   D9 INSTALL            C0:32F4/3322/33A9   op-0xD9 patches the PC walk blocks (AD.3a): writes
       obj2 IP:=0x171F (block2-1), obj3 IP:=0x173F->0x1746, targets 96 0A 26 (Crono tile 10,38) into
       $7F3726 and 96 07 2B (Marle tile 7,43) into $7F3746; $1C04/$1C06 (prio) := 0  (C0:32FB/3380)
f162   PCs arrive; blocks RETURN; prio restored to 7 (C0:5FA3)
f168   $0117=2100            C0:3AA8   op-0xD8 fires (also STZ $011F)
f235   battle module owns the machine ($1C00-table bytes -> FF via FD:AD10; $95D5 by CF/CC/C1)
--- the fight (kills ~f7xx-f14xx under mash; rewards; results) ---
f1601  $0117=0001 $0118=01   C0:18C1   battle DONE ack. SAME frame, module epilogue C0/07C3-0930:
       - enemy slots: alive byte $7E29C6+slot*0x0C: DEAD -> $1000,obj |= 0x80 (C0:07F3-F8, hook C0:07FB)
         [obj21/22/23: $102A/2C/2E lo 02 -> 82]; ALIVE -> field pos restored from staging (not taken here)
       - PC objects: pos re-derived from staging tiles $7E29B0+5*seat (+$F0/$F1): lead obj2 snaps to
         (160.0,615.0) = tile(10,38) (C0/08A0; writers C0:08C0/08CB) — the D9 endpoint, tile-quantized
       - object rows/IP/prio tables restored ($0F00 act bytes, $1180 IPs, $1C00 prios=07: C0:0914)
f1614  the $7F2000 SCRIPT IMAGE is reloaded (obj0 raw@0x054A 80 80 -> 00 22; part of the A712 rebuild)
f1615  the $7F3700 SCRATCH-SCRIPT REGION is re-copied from ROM templates (C0/5CEC MVNs; PLB hook PCs
       C0:5D09/5D17 — block1 rewritten byte-identical, blocks2/3 reset to 96 00 00) + handoff latch
       $95D5/$95DB = 80/80 (C0:A730) [$AF80 rebuild C0:A689, battle_end T2]
f1616  the FOLLOWER CALL: obj3 IP := 0x16FF (C0:5B4C-4F, hook C0:5B52), current IP 0x0634 pushed to
       $7F0580 + prio*0x80 + obj*2, prio $1C06 := 0 (hook C0:5B59), mover/ticket/cadence cleared
f1618  the object-activation window scan DEACTIVATES the dead-marked Hetakes: STZ $0F00,X (C0:A915,
       hook C0:A918) — $0F2A/2C/2E 80 -> 00; they are never re-activated (the $1000 bit7 mark)
f1619  $0117/$0118 -> 0      C0:3AB1   the parked op-0xD8 consumes the ack and advances obj1
f1618-1670  obj3 (Marle) WALKS (127,688) -> (160,615) = the lead's tile (the op-0x95 00 walk-to-PC0)
f1671  arrival -> loc[0xFF]++ -> IF loc[FF]==3 passes -> E3 01 @$7F3714 executes ->
       **$011F=1 (C0:3C0D) — CONTROL RESTORED**
f1672  the block's RETURN pops the VM call: obj3 IP back to 0x0634, prio 07 (C0:5FA3). Field play resumes.
```
(peek timeline shifts a few frames; align by event. Logs: `runA_writers.log`; followvm traces per object.)

## AD.3 — THE MECHANISM, ROM-cited

### AD.3a — the $7F3700 scratch-script region + its ROM templates (C0/5CEC installer)
`C0/5CEC` (REP #20; 5× `LDX #src / LDY #dst / PHB / LDA #len-1 / MVN $7F,$C0 / PLB`; SEP; RTS) copies:
```
C0/5D37 -> $7F3700 (0x20)  the RETURN/regroup block (unique)
C0/5D50 -> $7F3720 (0x20)  the op-0xD9 walk block    (3 copies: $7F3720/40/60, one per PC slot)
C0/5D68 -> $7F3780 (6)     tail: 96 00 00 FF 9D 00
```
Template bytes (ROM = live image, verified):
```
RETURN block  E3 00 | 89 40 | 0D 00 | 0E 02 | AE | 95 00 | 0E 00 | 71 FF | 12 FF 03 04 04 | E3 01 | 00 | 11 09
   = lock ctl | speed | coll-gates | snap ON | autoanime-reset | WALK to PC0 (the LEAD, blocking) |
     snap OFF | loc[0xFF]++ | IF loc[FF]==3 else JUMPBACK-to-IF (the 11 09) | RESTORE ctl | RETURN(pop)
D9 block      E3 00 | 89 40 | 0D 00 | AE | 96 xx yy | A6 ff | 71 FE | 12 FE 04 04 04 | E3 00 | 00 | 11 09 | ...
   = same skeleton; the D9 handler (C0/3322, C0/33A9) PATCHES the 96 target tile + A6 facing per PC and
     repoints each PC object here (C0:32F4); sync = loc[0xFE]; tail relocks (the battle follows).
```
Every op already exists in our VM: 0xAE = autoanime reset (ct_vm.c:1353), 0x71 = loc[i]++ (ct_vm.c:306),
0x95 = move-toward-PC[operand] blocking (ct_vm.c:888-, `95 00` = the lead), 0x0D/$1C80, 0x0E/$1C81, 0x89,
0xE3, 0x12. **The regroup is DATA the engine writes + ops the VM already speaks.** The multi-arriver sync:
each finishing follower increments loc[0xFF]; the seed (AD.3b) makes the LAST arriver's IF pass → only the
final walker executes `E3 01`. IP convention: the repoint writes blockStart−1 (0x16FF); the runner fetches
at IP+1 (obj3 parks at 0x1708 while the walk blocks; after `E3 01` executes the IP reads 0x1715).

### AD.3b — the resume routine C0/0330 + the VM-CALL helper C0/5B1F
```
C0/0330: JSR C0/595C ; DP:=1D00 ; JSR C0/A712     ; the field re-entry rebuild (script image reload,
                                                  ; C0/5CEC template copy, $AF80 MVN C0:A689, $95D5/DB=80
                                                  ; C0:A730, solid plane etc) — spans f1614-1615
         JSR C0/00EB ; JSL C0/2B78 ; TSB $47#02
0334B:   LDA $7E2A1F ; BIT #80 ; BNE no-regroup   ; a skip-regroup mode flag (CLEAR in this fight)
0353:    LDA $99 ; BPL +                          ; follower-1 object index (dp $99; $9B = follower-2)
0359:      LDA #01 ; STA $1F                      ; NO followers: $011F=1 DIRECTLY (dp write!)
035D:      LDA #03 ; STA $7F03FE ; BRL 00EB       ;   loc[0xFF] seed 3 = "regroup complete"
0368:    STA $EB ; JSR C0/5B1F                    ; CALL the regroup block on follower 1
036D:    LDA $9B ; BPL +                          ; follower 2?
0371:      LDA #02 ; STA $7F03FE ; BRA sync       ;   1 follower: loc[0xFF]=2 (2+1 arrival = 3 ✓)
0379:    STA $EB ; JSR C0/5B1F                    ; CALL on follower 2
037E:    LDA #01 ; STA $7F03FE                    ;   2 followers: loc[0xFF]=1 (1+2 arrivals = 3 ✓)
0384:    LDA $AB ; STA $AC ; STA $AD              ; facing sync
```
`$7F03FE` IS `loc[0xFF]` (locals at $7F0200 + idx*2) — the arrival counter the template polls. The helper:
```
C0/5B1F: LDX $EB ; LDA $1100,X ; BMI rts          ; object exists
         LDA $1C00,X ; CMP #01 ; BCC rts          ; current prio >= 1
         STA $4202 ; LDA #80 ; STA $4203          ; prio * 0x80
         LDA $4216 + $EB -> $D9
         LDA $1180,X -> STA $7F0580+($D9)         ; PUSH current IP under the current prio level
         LDA #16FF -> STA $1180,X                 ; IP := the regroup block (start-1)
         LDA #00 -> STA $1C00,X                   ; prio := 0 (highest)
         STZ $1A80,X ; STZ $1A01,X ; STZ $1001,X  ; kill mover + ticket + cadence
```
= a **prioritized VM CALL** ($7F0580 = the per-prio return-IP table, rows of 0x80 = 64 objs × 2; the same
family our CtSS `ret[]` stack models). The block's terminating RETURN pops it: obj3 back to IP 0x0634,
prio 07 (writer C0:5FA3). **The LEAD is never repointed** ($1184 untouched) — nobody moves him; the
followers come to him. `$1C00,X` = the object's current priority byte (07 idle; FF during the battle
module via FD:AD10; restored 07 by the epilogue C0:0914).

### AD.3c — enemy removal: the epilogue dead-mark + the activation scan
`C0/07C3-080C` (module epilogue, ack frame): for each of the **8** enemy slots (field-object
index in dp $A3..$AA — eight unrolled entries at C0/076B, one per slot, each with its own
`BMI` skip and no break; the op-0xD8 binding; staging offset X = slot*0x0C):
```
LDA $7E29C6,X ; BNE alive                ; NOT an "alive" byte -- see the correction below
  dead:  LDA $1000,Y ; ORA #80 ; STA     ; the DEAD-MARK (hook C0:07FB) + mover residue cleared
  alive: restore field pos from staging $7E29BF/C0,X (+$F0/$F1 arena origin), $0F01, $0A00/$0A80, $1601=0
```
`$1000,obj` low byte = the scheduler cadence counter (01↔02 churn, C0:59F6/5A23); **bit7 = "gone"**: the
object-activation window scan — window $C3..$C6 from `$1D0A/$1D0C/$1D0E/$1D10` (built C0/A947-A962),
activate arm C0/A8E5 (`$0F00,X=80` + gfx alloc C0/7170), deactivate arm C0/A910-A915 (`STZ $0F00,X`, hook
C0:A918) — deactivates the marked objects at f1618 and never re-activates them. **GATO emerges correctly
with zero special-casing**: his fight ends with the alive byte ≠ 0 → no mark → the NPC persists. The three
fought Hetakes die → marked → removed; the OTHER two Hetake trigger-groups (obj18-20/obj30-32) are separate
objects, untouched.

### AD.3d — who moves the party
- **Lead**: position re-derived from the battle staging block (`$7E29B0+5*seat` tiles + `$F0/$F1`) by
  C0/08A0 at the ack frame — a tile-quantized snap (observed 160.4→160.0; he stands where the D9 walked
  him). His object is never CALLed.
- **Followers**: the regroup block's `95 00` walk (field mover, ~52 frames here), landing ON the lead's
  tile (battle_end T2's "stacked" reform = this walk), then snap-off, count, restore, pop.

## AD.4 — the "damaged render" (the user-flagged BMP) — RESOLVED: a converted-state artifact + lazy streaming; the battle does NOT touch the field tilemaps

The band of tile-aligned garbage in the post-return frame (`postret.png`) is the ORACLE's own framebuffer,
and it is **not battle damage**:
- **The battle module leaves the field BG tilemaps BYTE-INTACT**: BG1+BG2+BG3 tilemaps are `0/4096 ×3`
  identical between pre-D8 (f174) and post-return (f1734) at the same camera (`pref167.raw.vram` vs
  `postret.raw.vram`). There is NO return-side map re-upload in the ROM because none is needed.
- The band is **inherited from the source savestate**: the pre-battle f40 frame (`pre40.png`) is already
  garbled (different, larger pattern) — `forest_holdleft_pre.state` (s9x→lake conversion) carries stale
  tilemap rows the user's original session had scrolled off and never re-streamed; the hold-LEFT + op-0xE7
  pan drives the camera ONTO those rows.
- It **persists at idle** (+30/+120 peeks pixel-identical; the only VRAM churn at idle = the animated-chip
  slot 0x7622-0x787E) and **heals lazily via the ordinary camera tile-streamer** (`postret_walkR.png`:
  every column the camera crosses re-streams pristine; Marle visibly following Crono — the regroup live).
- A clean single-A dismissal (no mash through the results) shows the SAME band (`cleanret.png`) — not a
  dismissal-race artifact.
- **Impact**: (a) our engine composites from baked assets each frame — immune, nothing to port; (b) oracle
  FB comparisons taken from CONVERTED states are only valid over regions the camera has re-streamed since
  load — a standing caveat for `capture-oracle-state` and any post-return pixel pin; (c) do NOT invent a
  return-side tilemap re-upload.

## AD.5 — IMPLEMENTATION SPEC (our fieldBattleFinish + interactive VM resume)

Mechanism-faithful port; the SCRIPTS do the restore/removal. No scene gates, no hand-`$011F=1` (outside the
ROM's own no-follower branch), no unconditional enemy hiding.

1. **Epilogue (C0/07EC-equivalent), at the ack**: for each battle enemy slot (the D8 binding we already
   stage): staging alive byte == 0 → set the field object's REMOVED mark (our analog of `$1000` bit7:
   excluded from draw AND from any (re)activation; not a draw=0 alone), clear its mover; else restore its
   field position from the staging record. For each PC seat: snap the object's field position from the
   staging tile (`$29B0+5*seat` + arena origin) — tile-quantized, like C0/08A0.
2. **Install the templates (C0/5CEC-equivalent)**: the two script blocks + tail are ROM DATA at
   `C0/5D37 / C0/5D50 / C0/5D68` — bake them (e.g. `ct_regroup.h`, 0x20+0x20+6 bytes) or read via CT_ROM,
   and materialize them in the VM's script space at a fixed engine slot (our analog of $7F3700..).
3. **Resume (C0/0330-equivalent), after the handoff**: seed `loc[0xFF] = 3 − nFollowers`; for EACH follower
   object: prioritized VM CALL of the regroup block at prio 0 through the existing CtSS `ret[]` stack (push
   current addr under the current prio, addr := block start, prio := 0, clear mover/ticket/cadence) — the
   exact C0/5B1F sequence. NO followers → set explore=1 directly (canonical C0/0359).
4. The block then runs on our existing op handlers (0xE3/0x89/0x0D/0x0E/0xAE/0x95/0x71/0x12) — the walk,
   the last-arriver count, the `E3 01`, and the RETURN-pop all emerge. The lead needs nothing.
5. obj1's idle poll (C.2's "stuck op-0x22") is HARMLESS once this lands — it was never the restorer; do NOT
   drive the lead anywhere for its sake (C.5's fix direction is dead).
6. Validation: interactive forest win → `$011F` restores ~20-70f after the ack (follower walk length),
   Hetakes gone, GATO fight keeps GATO; pin an interactive-resume check (the current `--battletest` path is
   blind to all of this — the header warning stands).

## AD.6 — OPEN

- The exact instruction that TESTS `$1000` bit7 in the activation scan (setter C0:07F3-F8 + the observed
  never-re-activate effect are pinned; the BIT/BMI site in the scan head is not).
- 3-member-party return: C0/0330 handles follower-2 symmetrically (`$9B`, loc[FF] seed 1) — un-exercised
  live (bat9 is the 3-PC catalog state if a capture is wanted).
- `$2A1F` bit7 (the skip-regroup mode at C0/034B) — semantics/who sets it unknown (clear here).
- The D9 blocks' tail bytes (`74 5F`, the `FF 9D` in the 6-byte tail) — entry-side, not exercised by the
  return; and the D9's own loc[0xFE] seeding site.
- The dispatcher that sequences C0/0330 via `$7F03FE` states (3/2/1) — the countdown consumer.
- The f1614 `$7F2000` script-image reload's writer PC (MVN inside the A712 rebuild; no per-write hook row).

## AD.7 — repro / artifacts (session scratchpad, NOT committed)

- Drive: `ct_probe.exe --monitor "0117:2,011F:1,0118:1,95D5:2,95DB:1" --from saves/battle_re/forest_holdleft_pre.state
  --btnscript "40:24,0:150,(100:20,0:40)x40" --diff --writer` (the master timeline); same with
  `"0F04:2,0F2A:2,0F2C:2,0F2E:2,1184:2,1186:2,13700:10,13720:10,13740:10,011F:1,0117:2"` → `runA_writers.log`;
  donor drive → `runC_donor.log`; `--followvm --obj {0,1,2,3,21,41}` traces.
- Frames: `postret{,30,120}.png` (+.raw/.wram/.vram/.oam/.state), `pre40.png` (the inherited corruption,
  pre-battle), `pref167.png` (pre-D8 same-camera baseline for the 0-diff tilemap proof), `cleanret.png`
  (single-A dismissal), `postret_walkR/walkU.png` (the streamer heal + Marle following), `iso0/1/2.png`
  (PEEK_ISOBG layer isolation).
- Disasm: `tools/dis.py 5B20 0x90` (helper; NB the true entry is 5B1F — dis.py from 5B20 misaligns by 1),
  `0330 0x70`, `07C0 0x170`, `A8C0 0xB0`; C0/5CEC hand-decoded from raw bytes (MVN sequence); ROM template
  bytes read at file offsets 0x5D37/0x5D50/0x5D68.

---

# IMPLEMENTATION ADDENDUM 2026-07-12 (post-AD.5) — SHIPPED; one mechanism refinement found in-engine

AD.5 is implemented (src/ct_vm.c `CtSceneBattleRemoveObj`/`CtSceneBattleReturn` + `ct_regroup.h` baked from
C0/5D37/5D50/5D68; src/field.c fieldBattleFinish epilogue — dead-mark via the `$AF02!=FF || deadStanding`
persist test, alive-restore + PC tile-snap from staging + arena origin, then the resume CALL). Validated:
interactive forest MASHA victory → `returnprobe: f1100 explore=1 placed=2 placed_enemies=0` (deterministic
×2, pinned as `field-battle-forest-return`); GATO persists (alive-restore path, zero id checks).

**Refinement (found by the GATO scene, not covered by AD.5): op-0xD9 re-claims a follower the resume
already CALLed.** The GATO scene's obj0 runs its own post-battle `op-0xD9` party-walk (@0041) AFTER the
op-0xD8 completes — in the ROM this is harmless because C0/32F4 REPOINTS each PC's IP onto the D9 walk
block, implicitly cancelling whatever the PC was running (the $7F3700 regroup included; last-repoint-wins).
Our port drives the D9 mover directly (no IP repoint), so without the equivalent the two walkers fought and
obj0's D9 never completed (points-persist never ran — the `field-battletest-gato` red). Two derived fixes,
both in ct_vm.c: (1) our op-0xD9 CANCELS a follower's in-flight regroup CALL (prio 0, addr in the rgBase
region) — the C0/32F4 semantics; (2) the conga controller yields to `d9Active` followers (the ROM's
repointed PC is script-owned, invisible to the party-trail controller). Both scenes then emerge correct:
GATO = obj0's own D9 regroup + Silver Points; forest = the engine-installed regroup restores control.

---

## CORRECTION — `$29C6` is not an ALIVE byte, and the loop is EIGHT slots

Two errors in AD.3c above. (1) `C0/076B` unrolls **eight** entries (`$A3` through `$AA`, `LDX #00/0C/18/24/30/3C/48/54`), not three. (2) `$29C6 + 12*slot` does not say whether the enemy lived: at battle
end `C1/B3BB` masks it down to **`&= 0x40`** for all eight slots, and bit 6 was set at STAGING by
`C0/05B2-05C2` (`LDA $1100,Y / CMP #06`) = the field object's **move-mode is 6**. So the epilogue's
test is "was this staged mode-6", independent of death, dissolve or flight. Measured: GATO `$29EA
0x42 -> 0x40` and he survives; the forest ambush `$29F6/$2A0E 01 -> 00` and both objects are
dead-marked by `C0:07FB`. Full derivation: [battle_end_teardown_survivor.md](battle_end_teardown_survivor.md).
