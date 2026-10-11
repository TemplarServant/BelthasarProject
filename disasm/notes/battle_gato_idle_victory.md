# BATTLE — GATO idle · dead-standing freeze · victory celebration (three user-reported defects)

Re-derives, from the ROM + LakeSnes oracle **only**, three battle-animation truths the user reports broken
in our engine (GATO fight, party Crono seat0 + Marle seat1):

1. **GATO idle** *"he just keeps turning up, down, up, down"* (he just keeps
   turning up/down repeatedly) — mission 1.
2. **Victory celebration** *"the players' celebration animation is stuck"* (the players' celebration
   animation is frozen/stuck) — mission 2.
3. **Dead-standing final enemy** must NOT keep animating — mission 3.

**HEADLINE FINDING (mandate: report the verified truth, not the assumed problem).**
- The GATO **idle bake `ctGatoIdleTrajV2` is BYTE-CORRECT** — the window (f67-186), the tile/column mapping,
  and the period (120) are all right; it matches the oracle frame-for-frame (§A). There is **no divergence
  in the idle data.**
- Missions **1 and 3 are the SAME bug**: our `battleGatoRender` advances the idle clock
  `gi=(h->frames-COMBAT_LIVE_F)%120` **unconditionally**, so after the final kill the *dead-standing* GATO
  keeps swapping his claws up↔down through the whole victory screen (§B). Because everything else is frozen
  during victory, that perpetual swap is the *only* motion on screen → the user reads it as *"he just keeps
  turning up, down"*. The oracle's dead GATO is **frozen byte-exact** on his death-frame pose.
- The victory **held pose is genuinely static** (oracle-confirmed) and our `battle_end.h` bake of it is
  **correct**; what our engine is *missing* is the **~72-frame animated settle-in** that plays *into* that
  pose (§C). Our `battleEndPoseRender` snaps straight to the static held pose → the *"stuck"* look.

> **Method / decode conventions** (as `battle_stance.md` / `battle_end.md`): OBJ 16×16 4bpp, CGRAM
> `128+pal*16+v`, idx0 transparent, **a pose OBJ pixel at OAM (x,y) → oracle fb true-pixel (x, y+8)**.
> GATO = pal1, 12 body cells (tiles 0x68/0x6A/0x6C/0x6E + 0x80..0x8E); tile **0x006 = the active-PC
> selection BRACKET, EXCLUDED** everywhere (its own asset, `battle_stance.md` ADD.6). Flip bits read from
> the OAM attr byte. **Idle OAM = `--monitor "0700:512,0900:32"`** (OAM shadow is CPU-maintained → per-frame
> exact). **Rendered timelines (victory/death) = `--peek`** (monitor FAST-FORWARDS the $A013-paced post-kill
> phase; `battle_end.md` METHOD note) — the `.wram`/`.oam`/`.vram` sidecars give per-frame RAM + OAM + tiles.
> Bases: `gato_press_action.state` (A-boots battle ~f222; combat-live ~f288); `gato_mid.state` = peek to
> f400 (`--btnscript "100:8,0:392"` + `PEEK_SAVE`); `gato_kill.state` = `gato_mid` with GATO curHP `$6130`=6
> (`state[WB=0x45 + 0x6130]=6`). Scratch scripts: `scratchpad/gato_idle/{cap_idle,analyze2,transition,
> death_mon,peek_death,fine_celebration,settle_full,ours_capture}.py`.

---

## A — GATO IDLE : the bake is CORRECT (period 120, up87/down33, ±1px body bob, NO flips)

### A.1 The two idle poses (byte-exact, from both a natural fight and a no-attack window)
GATO's idle alternates exactly **two OAM assemblies** (min-corner anchor + `tile@(dx,dy)`; **all cells
`hf0 vf0` in every frame of every window — GATO NEVER flips**):

| pose | claws | assembly (rel. anchor) |
|---|---|---|
| **UP / rest** (kf6 I0_armsRest) | 068/06A top-right | `068@(47,10) 06A@(47,25) 06C@(0,25) 06E@(0,10) 080@(31,48) 082@(16,48) 084@(32,32) 086@(16,32) 088@(32,16) 08A@(16,16) 08C@(32,0) 08E@(16,0)` |
| **DOWN / swap** (kf7 I1_armsSwap) | 068/06A drop to bottom; 084/086 rise | `068@(31,48) 06A@(15,48) 06C@(0,26) 06E@(0,11) 080@(32,32) 082@(16,32) 084@(47,11) 086@(47,26) 088@(32,16) 08A@(16,16) 08C@(32,0) 08E@(16,0)` |

The "turning up/down" the user names = cells **068/06A dropping ~38px** (y10→48) and 084/086 rising, i.e.
GATO alternately raising each claw. It is a genuine idle motion, and the **UP↔DOWN change is a hard
1-frame SNAP** (natural: f433 UP → f434 DOWN, no tween; DOWN→UP f472→473 likewise) — so our two-keyframe
snap is faithful. These match `battle_anim.h` kf6 (baked f395) / kf7 (baked f460) **cell-for-cell**.

### A.2 Period + cadence — the no-attack window (`cap_idle.py`/`transition.py`)
No-attack state = `gato_idle400` (peek f400) with countdowns `$AFAC=$AFB1=0xFF` (Marle+GATO never act) →
a clean 450-frame idle. Claw-DOWN **onsets at monitor f34, 154, 274, 394 → Δ = 120, 120, 120 (period 120,
dead exact)**; **up-run = 87f, DOWN-run = 33f**. Body bob (from cell 08E / anchor Y) = **62↔63, ±1px**;
within one 120f period the anchor Y is `62(27f) / 63(33f) / 62(27f) / 63(33f)` with the claw-DOWN swap
landing only in the *last* 63-block. Cross-checked against the **natural** fight (`gato_press_action`
hold): the first claw-DOWN onset is battle **f434** in BOTH the natural and no-attack windows (the
no-attack monitor's f0 = battle f400) — so the swap is real idle, not a windup artifact of the patch.

### A.3 The committed `ctGatoIdleTrajV2` matches the oracle frame-for-frame
`src/data/battle_anim_add.h` (`CT_GATO_IDLE2_N 120`, `CT_GATO_IDLE2_F0 67`), per-row `{dx,dy,arm,footDY}`:

```
gi   0..26  (f67-93 )  {0,0,0,0}  UP,   body 62   |  oracle no-attack f67-93 : y62 dy0 arm0  ✓
gi  27..59  (f94-126)  {0,1,0,0}  UP,   body 63   |  oracle           f94-126: y63 dy1 arm0  ✓
gi  60..86  (f127-153) {0,0,0,0}  UP,   body 62   |  oracle          f127-153: y62 dy0 arm0  ✓
gi  87..119 (f154-186) {0,1,1,0}  DOWN, body 63   |  oracle          f154-186: y63 dy1 arm1  ✓
```
= up87 / down33, body bob 62↔63, footDY 0 (no feet — the old "foot quad" was the bracket, already fixed).
**Every column and every frame matches.** `battleGatoRender`'s render math (`field.c:1957-1965`) applies it
correctly: `gi=(h->frames-82)%120`; `kf = arm ? kf7 : kf6`; `by = s->y + (62+dy) - kf->anchorY` normalizes
both keyframes' top cell to `62+dy` (kf6 anchorY 58 / kf7 anchorY 63 baked-relative) — verified correct.

### A.4 Our engine renders the live idle correctly (visual)
`ours_150.png` (our engine, h->frames=150, `CT_BATTLESHOT_AT`): GATO is a well-formed robot with claws
up, identical in shape to the oracle idle (`view_100.png`). **The live idle is not the defect.**

### A.5 VERDICT (mission 1)
**The idle bake is CORRECT — no wrong window, no wrong column mapping, no wrong period.** The reported
*"he just keeps turning up, down"* is the **dead-standing GATO of §B**, not a defect in the idle
data. (One minor, pre-existing boundary remains: GATO's **entrance-settle** — his anchor sinks y55→63 over
the first ~150 combat-live frames in the oracle — is unbaked, so our idle starts at the steady y62-63 and
its first claw-swap lands ~59f earlier than the oracle's during the entrance only; cosmetic, see OPEN.)

---

## B — DEAD-STANDING GATO : the oracle FREEZES him byte-exact; our engine keeps the idle clock running

### B.1 Oracle truth (peek, authoritative rendered timeline — `peek_death.py`/`fine_celebration.py`)
Drive: `gato_kill` + mash-A. GATO dies between peek **N=160 and N=175**: curHP `$6130` 6→0, dead flag
`$614A` 0→0x80, alive-list `$AF05` 0x92→**FF** (removed). From the kill onward GATO's 12-cell OAM is
**byte-STATIC through N=520 (345 frames, zero changes)**: anchor (104,59), claws-UP. The frozen assembly
(`pk_310`, deep in victory) is **byte-identical to his pre-kill idle pose** (`pk_160`) — same anchor, same
12 cells, all `hf0 vf0`. So:

> **The dead-standing GATO freezes on whatever idle pose he held at the killing blow, and holds it exactly
> — no dissolve, no special death pose, no animation, no bob, no swap.** (In a mash-kill he is usually in
> the UP/rest pose, as captured.) He stays rendered (12 pal1 cells) the entire victory — correct, since the
> FINAL kill does not dissolve. `view_310.png` shows him standing upright through the results window.

### B.2 Our engine's bug (confirmed in code AND visually)
`battleGatoRender` (`field.c:1934-1965`): the early-out `if (mem[0xAF05]==0xFF && !h->deadStanding[3])
return;` keeps drawing GATO when `deadStanding[3]` is set (the final-kill-stands latch, `battle.c:698`),
**but the idle index is still `gi=(h->frames-CT_BATTLE_COMBAT_LIVE_F)%120` off the LIVE, still-advancing
`h->frames`.** During victoryHold (`battle.c:1381` `h->frames`/victoryHold advance every render frame) GATO
therefore keeps cycling. Timeline of our fight (`CT_BATTLE_TRACE`): GATO dies **f1956**, victoryHold
**f1957-2160 (204f)**, handoff f2171. Over that 204f `gi` sweeps two full swaps.
**Confirmed visually:** `ours_2000.png` GATO claws-DOWN (gi=118), `ours_2030.png` GATO claws-UP (gi=28) —
he swaps up/down while dead. This IS *"he just keeps turning up, down, up, down"*.

---

## C — VICTORY CELEBRATION : an animated ~72f SETTLE-IN into a STATIC held pose

### C.1 The held pose is STATIC and our bake of it is correct (`fine_celebration.py`/`settle_full.py`)
After the settle (below), the surviving PCs **hold one victory pose, fully static, until A**: OAM position
+ tile content unchanged, and **OBJ pal5/pal6 do not cycle** (every-frame N=252-270: the *only* CGRAM entry
that changes is index **158 = OBJ pal1 idx14 = the bracket pulse**, a separate asset — pal5/pal6 identical).
Held assemblies:
- **Crono (126,108):** `008@(0,0) 00A@(0,13) 00C@(16,14) 00E@(0,29)` — == `battle_end.h` kf0 assembly exactly.
- **Marle (168,68):** `020@(18,4) 022@(2,0) 024@(0,30) 026@(1,16)` — == `battle_end.h` kf1 assembly exactly.
(Anchors differ ≤2-3px from `battle_end.md`'s (124,111)/(168,68) — capture-state variance; the assemblies
are identical.) **So `battle_end.h`'s victory poses are right — the held pose is not the bug.**

### C.2 The MISSING piece — the animated settle-in (the "stuck" fix)
Immediately after the kill (peek N~170 → ~242, **~72 render frames**) BOTH PCs play a pose ladder *into* the
held pose, with **stable ~5-10f dwells** per pose and streaming tiles (a discrete keyframe ladder — bakeable
like `battle_entrance.h`, NOT a continuous tween). Measured (rep frame / anchor / pal5|6 assembly):

**Crono** (idle-stance held through the kill, then):
| # | ~frames | anchor | assembly | note |
|---|---|---|---|---|
| C0 | ≤N178 | (109,108) | `008@(16,13) 00A@(0,7) 00C@(17,28) 00E@(16,0)` | idle stance (held at kill) |
| C1 | N190-198 | (112,106) | `008@(14,14) 00A@(0,8) 00C@(14,30) 00E@(14,0)` | steps in/compresses |
| C2 | N202-206 | (126,106) | `008@(0,14) 00A@(0,46) 00C@(0,30) 00E@(0,0)` | jumps right, rearranges |
| C3 | N210-218 | (126,107) | `008@(0,13) 00A@(0,0) 00C@(0,29) 00E@(0,45)` | rearrange |
| C4 | N222-230 | (126,96) | `008@(0,0) 00A@(0,16) 00C@(0,48) 00E@(0,32)` | **hops up (y96)** = field-column pose |
| C5 | N234→ | (126,108) | `008@(0,0) 00A@(0,13) 00C@(16,14) 00E@(0,29)` | **settled held victory pose** (= kf0) |

**Marle** (waiting; still celebrates — a horizontal shuffle at y79 then settle):
| # | ~frames | anchor | assembly |
|---|---|---|---|
| M0 | ≤N178 | (164,63) | `020@(3,0) 022@(0,16) 024@(16,15) 026@(0,32)` (idle) |
| M1 | N182-190 | (154,79) | `020@(16,0) 022@(16,32) 024@(16,16) 026@(0,8)` |
| M2 | N194-198 | (159,79) | `020@(16,0) 022@(0,0) 024@(16,16) 026@(0,16)` |
| M3 | N202-218 | (171,79) | `020@(16,0) 022@(0,0) 024@(16,16) 026@(0,16)` (x→171) |
| M4 | N222-234 | (157,79) | `020@(16,0) 022@(0,0) 024@(16,16) 026@(0,16)` (x→157) |
| M5 | N238→ | (168,68) | `020@(18,4) 022@(2,0) 024@(0,30) 026@(1,16)` (**settled** = kf1) |

Tiles STREAM per pose (each keyframe has its own OBJ tile content, like the entrance/end bakes). The
per-frame within-pose stream is the C1/CC anim-engine boundary (`battle_stance.md` S4), but the **pose
ladder itself is discrete/bakeable** — this REFINES `battle_end.md`'s "settle-in is OPEN/unbaked".
Rendered proof: `view_190.png` (Crono mid-hop), `view_234.png` (settling), `view_310.png` (held + results
window). **Our engine (`battleEndPoseRender`, `field.c:2244-2255`) draws only the static kf0/kf1 from
victoryHold onward — it skips this entire ~72f ladder → the celebration looks frozen.**

---

## D — IMPLEMENTATION SPECS (for the re-implement lane; NOT applied here)

**D.1 GATO idle (mission 1): NO CHANGE.** `ctGatoIdleTrajV2` + kf6/kf7 + `battleGatoRender`'s idle math are
byte-correct. Do not "fix" the bake.

**D.2 Dead-standing freeze (mission 3) — the actual fix for the "GATO turning" report.** In
`battleGatoRender` (and, for generality, `battleHetakeRender`) FREEZE the idle clock when the enemy is
dead-standing: instead of the live `gi=(h->frames-COMBAT_LIVE_F)%CT_GATO_IDLE2_N`, use a **latched frame**
captured at the `deadStanding[slot]` transition. Concretely: add `uint32_t deadStandFrame[8]` to the host,
set `deadStandFrame[tgt-3]=h->frames` where `deadStanding[tgt-3]=1` is set (`battle.c:698`), and in the
render use `int clk = h->deadStanding[slot] ? h->deadStandFrame[slot] : h->frames;
gi=(clk-COMBAT_LIVE_F)%CT_GATO_IDLE2_N;`. This holds GATO on his exact death-frame pose (byte-exact vs the
oracle for ANY kill phase), matching §B.1. (The GATO/Hetake anchor delta `battleEnemyAnchorDelta` stays as
is — position is unaffected.)

**D.3 Victory settle-in (mission 2) — the "stuck" fix.** Keep the held poses (kf0/kf1 are correct).
BEFORE holding, replay the ~72f settle-in ladder (§C.2), timed off `victoryHold` (plays over victoryHold
1..~72, then hold): bake a per-PC keyframe ladder (Crono C1-C4, Marle M1-M4; each = OAM assembly + its own
streamed OBJ tiles, ~8-10f dwell — same generator shape as `battle_entrance.h` / `battle_end.h`) then land
on the existing held keyframe. Endpoints (C0/C5, M0/M5) are already the idle-stance and the baked held pose.
This is a discrete keyframe bake (NOT the unbakeable-tween boundary `battle_end.md` assumed).

---

## E — OPEN questions
- **Settle-in per-frame tile stream:** the ladder poses are baked-able at their rep frames, but the
  within-pose sub-frame tile stream (C1/CC anim engine, `$0520+n*0xC`, `FD:E3A9`) is the standing boundary
  — a byte-exact settle needs each keyframe's tiles captured (like the entrance), which I measured only at
  ~step-4 resolution here (assemblies pinned; full per-keyframe tile bake is the implementer's capture).
- **GATO idle ROM driver:** the exact per-enemy idle SCRIPT that emits up87/down33 (period 120) lives behind
  the battle-anim interpreter (`CD/5FF0` script tables → `C1/7A6B` handlers → OBJ-slot tick `FD:E3A9`,
  `battle_stance.md`/`battle_interp_verify.md`). The OAM-derived sequence (period 120) is the deliverable;
  the script bytes stay OPEN.
- **Freeze MECHANISM (ROM):** the oracle stops ticking the dead enemy's OBJ anim slot (observable: OAM
  frozen from the kill frame). The exact gate byte is the anim-engine boundary; the OBSERVABLE freeze
  (latch the clock) is what D.2 reproduces.
- **GATO entrance-settle (minor, pre-existing):** anchor y55→63 over the first ~150 combat-live frames is
  unbaked (`battle_stance.md` S6.1) → our idle starts steady at y62-63 and its first claw-swap lands ~59f
  early during the entrance only. Cosmetic; not the reported defect.
- **Victory held-pose anchor:** oracle (126,108)/(168,68) here vs `battle_end.h` (124,111)/(168,68) — ≤3px,
  capture-state variance; assemblies identical. Whether the victory anchor should be formation-computed
  (like the idle anchor, A8) rather than baked-absolute is a minor follow-up.
- **Marle-only / other parties / Hetake final-kill:** only Crono+Marle vs GATO captured; the dead-standing
  freeze (D.2) is written per-slot so it generalizes, but a standing final Hetake is unverified.

## Scratchpad artifacts (paths for the caller; not committed)
`scratchpad/gato_idle/`: monitor dumps `mon_natural.txt` / `mon_noatk.txt`; states `gato_mid.state`,
`gato_kill.state`, `gato_noatk.state`; peek renders `view_{100,190,234,310}.png`; our-engine renders
`ours_{150,2000,2030,2060,2090}.png`; capture/analysis scripts listed in the Method note. `ct_probe.exe`
(oracle copy) + `ct_field_probe.exe` (our engine copy) in `scratchpad/`.
