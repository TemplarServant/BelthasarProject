# BATTLE END — victory celebration, party regroup, PC fallen/dead pose (GATO fight)

Derives the three user-reported battle-**END** behaviors our `src/battle.c` is missing:
1. **Victory celebration** (*"at the end of the battle there should be a celebration animation if you win"*)
2. **Party regroup walk** (*"marle ... should walk to the same position as crono, she is ending up a little to the side"*)
3. **PC fallen/dead pose** (*"when a character dies, he should 'lie fallen on the ground' and he is vanishing"*)

Picks up from `battle_loop.md` S5 (death→victory→`$7F01EC`, field resume ≈f1156) and reuses the
OAM/pose conventions of `battle_stance.md` / `battle_actionfx.md` (OBJ 16×16 4bpp, CGRAM `128+pal*16+v`,
**a pose OBJ pixel at OAM (x,y) → oracle fb true-pixel (x, y+8)**, verified). Party = Crono seat0 +
Marle seat1 vs GATO enemy-slot3.

> **METHOD (peek vs monitor, decisive here).** The celebration/regroup are RENDERED animations, so
> **`--peek` is authoritative** (monitor does no render → it fast-forwards the celebration; that is why
> `battle_loop.md` S5 saw the whole kill→resume in one 206-frame monitor window f950→f1156). All OAM/pose
> data below is from `--peek` of `gato_lowhp.state` (in-battle, GATO 19HP). Two clean input regimes:
> - **kill+IDLE** (mash-A to f980 to land the killing blow, then release): the celebration pose then
>   **holds indefinitely** (the reform waits for an A-press, §T2) → an A-noise-free victory pose.
> - **kill+MASH** (keep pressing A): the A-press dismisses the results → the field-side reform+regroup fires.
> RAM writer PCs are from `--monitor --writer` (timeline-independent). Frame numbers are the peek timeline
> unless tagged `(mon)`; peek runs ~in step with monitor here (kill f960 peek vs f950 mon).

Repro base: `ct_probe --peek gato_lowhp.state OUT --btnscript "<mash>[,0:<idle>]"` (mash = `100:16,0:16`…).
Decoders in scratch: `end_trace.py` (per-frame HP + turn-state + Crono/Marle anchor&assembly + pal1 count),
`end_sweep.py`, `vdiff.py` (tile stream), `an2.py`. Generator `tools/_gen_battle_end.py` (gitignored).

---

## Timeline overview (peek, gato_lowhp + mash-A, one representative run)

| frame | event | key RAM |
|---|---|---|
| f960 | **killing blow** GATO curHP `$6130`→0 (`C1:ED12`), dead flag `$614A`=0x80 (`C1:ED0C`), alive-list `$AF02`[slot3] 92→FF (`C1:B2C3`) | victory scan `C1/817A`: `$AF02` all-FF ⇒ `$7F01EC` stays 0 = win |
| f960→f1018 | **celebration settle** (~58f): both PCs' battle-anim engine settles from the last combat pose into the held victory pose | `$95D5`=FF, `$95DB`=0, `$AFAB` c04/m66/g06 |
| f1018→(A) | **VICTORY POSE HOLD** — Crono @(124,111), Marle @(168,68), fully static; results text *"Got N experience/tech point(s)!"* shows; holds until the player presses A | unchanged |
| f1151-1156 (mon) / f1171-1177 (peek) | **A-press → FIELD handoff**: `$AFAB`→0x80 (`C0:A689`), `$95D5`=0x80 + `$95DB`=0x80 (`C0:A730`); PCs revert to field-standing 4-cell column | `$95D5`=0x80 |
| f1177→f1216 | **REGROUP WALK** — Marle walks from (168,68) to the leader's tile (124,99) and **stacks exactly on Crono**; ~40f, ~1.35 px/f | `$95D5`=0x80 |
| f1216+ | both settled at (124,99); field would resume (peek holds at the results/handoff, the A-edge caveat) | — |

---

## T1 — VICTORY CELEBRATION : a per-PC static held pose, settled ~58f after the kill, held until A

**It is a HELD pose, not a loop.** Across the whole hold (f1020→f1160, clean idle) the victory pose is
**fully static**: OAM assembly unchanged, **OBJ tiles do not stream** (`vdiff tr_1020 vs tr_1160`:
Crono/Marle tiles-changed=none), and **the palette does not cycle** (pal5/pal6 CGRAM identical f1030 vs
f1090 — no sword-gleam). So the "celebration" the user sees = each surviving PC assumes ONE distinct
victory pose and holds it while the results window is up. Baked byte-exact:

- **Crono (pal5, anchor 124,111):** `008@(0,0) 00A@(0,13) 00C@(16,14) 00E@(0,29)` — 4 cells, ~17×31.
  Distinct from his idle stance (109,108 `00E@16,0 00A@0,7 008@16,13 00C@17,28`) — a real victory pose.
- **Marle (pal6, anchor 168,68):** `022@(2,0) 020@(18,4) 026@(1,16) 024@(0,30)` — 4 cells, ~19×44.
  Distinct from her idle (164,63). She stays on the RIGHT (x≈168), NOT beside Crono yet.
- **GATO (pal1):** still rendered (pal1=12) through the whole hold (§ note on enemy vanish below).

**Settle-in (kill→held pose, ~58f, f960→f1018).** The battle-anim engine plays a short settle through a
few streamed sub-poses (clean idle capture): f958-970 (116,116) `00A@0,0 008@16,0 00E@0,16 00C@16,16`
→ f976 (106,116) `00A@16,0 008@0,8 00C@32,8 00E@16,16` → f994-1000 (106,110) → a 1-frame field-column
transient f1006-1012 (124,99) → **settled (124,111) at f1018**. Marle settles to (168,68) by f1024.
These sub-poses stream tile content (the C1/CC anim engine, `battle_stance.md` S4 boundary) so the
**per-frame settle is OPEN**; the **settled held pose is the byte-exact implementable minimum** (baked).

**When it starts / how it ends.** Starts the moment combat has no more actions to run after the kill
(`$95D5`=FF idle, GATO dead); ends when the player **presses A** (dismisses the results) → the field
handoff (§T2). No fade — the pose is simply replaced by the field-standing sprite at the handoff.

## T2 — REGROUP WALK : the FIELD gathers followers onto the leader's tile (Marle → Crono, stacked)

**The regroup is FIELD-side, not the battle module.** The reform latch is written by **bank C0 (field)**,
not C1: at the handoff `$AFAB`→0x80 (`C0:A689`, an `MVN $7E,$7E` block move rebuilding the `$AF80` region)
then `$95D5`=0x80 + `$95DB`=0x80 (`C0:A730`) — matches `battle_loop.md` S5 "control returned to the field
≈f1156, first bank-C0 write C0:A730". So: the **battle module runs the celebration (C1)**, the player's A
dismisses the results, **control hands to the field (C0/A730)**, and the field's party-gather brings the
followers to the leader — the field conga reform (memory `ct-field-party-follower`), applied on battle-return.

**Measured path (peek, all-mash, `tr_1168..1237` step 3):** Marle holds the victory pose until the latch
(`$95D5`→0x80 at f1171), reverts to the field-standing 4-cell column, and **walks** from her battle anchor
**(168,68)** diagonally down-left, reaching the leader **(124,99) at f1216** — i.e. **Crono's EXACT
position** — then holds there stacked. ~40 render frames, ~1.35 px/frame (field walk speed). Sample path
x/y: 168,68 → (f1180) 164,76 → (f1186) 145,92 → (f1198) 141,89 → (f1204) 133,96 → (f1210) 129,99 →
(f1213) 126,99 → **(f1216) 124,99**.

**Crono (the lead) does NOT move** — he snaps to the field column at (124,99) at the latch (f1177) and
holds while Marle comes to him. So the **target position = the LEADER's formation tile**; the follower
**collapses onto it (offset 0, stacked)** because the leader is stationary (no path-history gap). This is
exactly the user's fix: Marle must end at Crono's **same** tile (stacked), not "a little to the side". (A few-px
settle jitter — Marle 118 vs 124 — appears only in noisier inconsistent-mash captures; the clean monotonic
step-3 trace lands 124=Crono.)

Both PCs' field-standing column pose = `008@(0,0) 00A@(0,16) 00E@(0,32) 00C@(0,48)` (the ordinary field
sprite; rendered by the field engine, not baked here).

## T3 — PC FALLEN/DEAD POSE : a collapsed on-ground sprite that PERSISTS (dead PCs do NOT vanish)

Capture: `crono_low.state` = `gato_lowhp` with Crono curHP `$5E30` patched to 10 (`state[0x45+0x5E30]`,
the `battle_loop.md` ADDENDUM patch base), then **IDLE** — GATO's queued physical kills the 10HP Crono.

**The collapse is ~instant (≤1 render frame) — no multi-frame fall observed at 1-frame sampling.**
Fine sweep `tr_1..16` (idle): f1-**f8** Crono STANDS at his idle stance **(109,108)** `00E@16,0 00A@0,7
008@16,13 00C@17,28` (alive); at **f9** he snaps to the FALLEN pose and holds it:

- **Crono fallen (pal5, anchor 114,114):** `00A@(11,0) 00E@(0,16) 00C@(16,16) 008@(12,32)`.
  Opaque bbox = **31 px wide × 13 px tall** (a prone/lying shape — vs the 17×31 standing bbox), anchor
  **+6 y** below the idle stance = "fallen on the ground". Tiles are **static** (`tr_22` vs `tr_98` identical) —
  a frozen dead sprite (same tile NUMBERS 0x08/0A/0C/0E, dead CONTENT streamed in by the death handler).

**It PERSISTS to battle end.** Dead Crono keeps **4 pal5 sprites** at (114,114) every frame f9→f1400+
(idle sweep) — he never vanishes. Trigger = the **PC dead flag `$5E4A` bit7** (block `$5E2D`+0x1D,
`battle_loop.md` S3 death path `C1/ED04`); confirmed set (0x80) the frame the fallen pose appears.

**The asymmetry (pinned):**
- **Dead ENEMY** → removed from the alive-list `$AF02[slot]`=0xFF (`C1:B2C3` via `FD/ABA2`, `battle_loop.md`
  S5); the OBJ builder drops it → the enemy sprite goes away. (Our engine's GATO-vanish is correct.)
- **Dead PC** → only its dead flag `$5E4A` bit7 is set; the PC is NOT removed from any render list, so the
  OBJ builder KEEPS drawing it, selecting the **collapsed/dead pose** instead of the idle stance. → stays
  fallen on the ground. **This is the bug the user reports: our engine makes the dead PC "vanish"; it must
  render the fallen pose instead.**

A hurt-flash (pal4 white, `battle_actionfx.md` P3) likely precedes the fall on the lethal hit, but it was
not isolated here (the lethal hit landed between the f8/f9 idle samples); the flash→fall micro-timing is a
minor OPEN.

## ROM / RAM anchors (cited)

- **Kill + outcome:** curHP `$6130` (`C1:ED12`), dead flags block+0x1D bit7 (`C1:ED04`; GATO `$614A`, Crono
  `$5E4A`), enemy alive-clear `$AF02` (`C1:B2C3`/`FD:ABA2`), victory scan **`C1/817A`** → `$7F01EC`=0 win
  (`battle_loop.md` S5). All re-confirmed by `--monitor --writer` this session.
- **Celebration state:** `$95D5`(current actor)=0xFF, `$95DB`=0, ATB `$AFAB` c04/m66/g06 — i.e. "no actor,
  no action, combat drained" = the module's post-victory idle that plays the victory pose.
- **Field handoff (celebration→regroup):** `$AFAB`→0x80 **`C0:A689`**, `$95D5`/`$95DB`→0x80 **`C0:A730`**
  (f1151-1156 mon). C0/A660+ is the field re-entry (rebuilds the `$7E7080` solid plane from a bank-E5 table).
- **Pose PIXELS = BOUNDARY** (same as stance/entrance/fx): the battle-anim engine (C1/CC OBJ builder +
  anim-slots `$0520+n*0xC`, `FD:E3A9` + ROM per-character battle-anim scripts, `battle_stance.md` S4)
  streams the victory/dead pose tiles; not a compact constant → **baked from the oracle VRAM**, mechanism cited.

---

# PART 2 — REPLAY SPEC (what to bake + the render/tick rule + the trigger state)

**Baked — `src/data/battle_end.h`** (emitted by `tools/_gen_battle_end.py`, deterministic from the oracle):
- `ctBattleEndObj[12]` — the settled OAM lists for the 3 pose keyframes (victory Crono, victory Marle,
  fallen Crono), OAM-index order: `{oidx, x(int16), y(uint8), tile(9b), pal, prio, hf, vf}`.
- `ctBattleEndKf[3]` — `{name, pose, pal, kind(0=victory/1=fallen), objOff, nObj, tileKf, anchorX, anchorY}`.
- `ctBattleEndTiles[3][0x40][32]` — per-keyframe OBJ 8×8 tiles 0x00..0x3F, 4bpp planar (streamed per pose).
- `ctBattleEndPal[8][16]` — OBJ CGRAM palettes (pal5 Crono, pal6 Marle used), BGR555 LE.
- Regroup constants: `CT_END_REGROUP_LEADER_X/Y = 124,99`, `_MARLE_X0/Y0 = 168,68`, `_WALK_FRAMES = 40`.
- Timing: `CT_END_SETTLE_FRAMES = 60`, `CT_END_HOLD_MIN = 140`.

**Render rule — a pose keyframe** (composite as OBJ over the field, same as the stance bake):
```
for each sprite S in the keyframe's slice, drawn HIGHEST oidx FIRST:
  for the 16x16 cell sub-tiles (T, T+1, T+0x10, T+0x11) of ctBattleEndTiles[tileKf]:
     decode 4bpp v; v==0 transparent; apply hf/vf; plot rgb=bitrep(ctBattleEndPal[S.pal][v])
     at screen (S.x+sx, S.y+sy)          // fb row = OAM y + 8 (feed drawSprite y-1 if it adds SNES +1)
```

**Frame selection per battle-END STATE:**
- **Victory (win detected, `$7F01EC`==0 after all enemies dead, `battle_loop.md` C1/817A):** for each
  ALIVE PC (dead-flag clear) render its **victory keyframe** (`kind==0`), held. Start ~`CT_END_SETTLE_FRAMES`
  after the kill (the settle animation is OPEN → snap to the held pose is the safe minimum); hold until the
  player dismisses the results, then hand to the field. (Dead enemies already vanish — do not draw GATO.)
- **A PC is DEAD (its `$5E2D+seat*0x80 +0x1D` bit7 set):** render its **fallen keyframe** (`kind==1`)
  INSTEAD of any stance/victory pose, at ALL times from death onward (persists). Do NOT clear/vanish the PC.
  (Enemies: keep the existing alive-list vanish.)
- **Regroup (post-victory, after the field handoff `C0/A730` fires on A-dismiss):** this is the FIELD's
  party-gather — walk each follower from its battle anchor to the **leader's tile**
  (`CT_END_REGROUP_LEADER_X/Y`) and **stack it exactly on the leader** (offset 0, leader stationary), over
  ~`CT_END_REGROUP_WALK_FRAMES` at field walk speed, using the field walk sprite. The follower must NOT
  stop offset ("a little to the side") — it collapses onto the leader. (Field-side; battle_end only supplies
  the endpoint + rate.)

**State that triggers each element** (our engine already computes these):
- victory pose ← the win end-check (`$7F01EC`=0, all enemies dead);
- fallen pose ← the PC dead flag (block+0x1D bit7) our damage/clamp already sets (`battle_loop.md` S3);
- regroup ← the battle→field return (our `battle.c`→`field.c` handoff), driven field-side.

**NOT baked / boundaries (documented, mandate):**
- **Settle-in victory animation (~60f)** — streamed sub-poses (C1/CC anim engine); only endpoints/anchors
  measured. Snap to the held pose is the byte-exact minimum.
- **Regroup pose pixels** — the field walk sprite (field engine); only target+rate baked.
- **Death hurt-flash→fall micro-sequence** — collapse ≤1 frame; the preceding pal4 flash not isolated.
- **Other parties/enemies, ≥3-member conga, victory poses of other PCs, looping/gleam victory flourishes** —
  only Crono+Marle vs GATO captured; a looping/palette-gleam flourish was **absent** in this fight (static).

---

# Verify

`CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/_gen_battle_end.py --verify --emit`
regenerates the victory frame (`gato_lowhp` + mash-to-980 + idle-to-1160) and the fallen frame
(`crono_low` idle-to-100), reconstructs each pose from the baked arrays, and pixel-diffs vs the oracle fb
over each pose's bbox (fb row = OAM y + 8):
```
[battle-end verify] per pose keyframe, over its bbox (d>24 = mismatch):
  Crono victory  pal5  bbox x[124..141] y[116..147]  exact=400/400  mismatch=0
  Marle victory  pal6  bbox x[169..188] y[68..112]   exact=397/397  mismatch=0
  Crono fallen   pal5  bbox x[114..145] y[132..145]  exact=321/321  mismatch=0
  TOTAL exact=1118/1118 (100.0%)
```

## Open questions
- **Settle-in victory animation (T1, f960→f1018):** the ~5 streamed sub-poses (anchors measured) → the
  C1/CC anim engine (`$0520+n*0xC`, `FD:E3A9`); per-frame content unbaked (the stance-dossier boundary).
- **Regroup exact mechanism (T2):** the field party-gather that maps battle-formation → leader tile
  (seeded from `$29B0`?) and the follower stack rule are field-side (`ct-field-party-follower`), pinned here
  only as endpoint (leader tile) + rate; the ≥3-member conga ordering is un-captured.
- **Death micro-sequence (T3):** hurt-flash (pal4) → fall isolation; whether a >1-frame collapse exists at a
  finer-than-render sampling (unlikely — snap observed).
- **Enemy vanish vs celebration (side note):** under the held peek the GATO sprite lingers (pal1=12) the
  whole celebration; the real enemy-death dissolve (`FD/ABA2`) is skipped in the stuck-peek tail. Our
  vanish rule is correct per the task; whether the enemy should linger a beat during the PC celebration is
  UNVERIFIED (needs the completing field-resume capture, blocked by the peek A-edge caveat).
- **Other combatants:** only Crono+Marle vs GATO; other parties/enemies need their own capture or a full
  port of the C1/CC OBJ builder + battle-anim scripts.

## Scratchpad artifacts (paths for the caller; not committed)
Dir: `%TEMP%/claude/…/e5608df4-…/scratchpad/`
- States: `gato_lowhp.state` (GATO 19HP in-battle), `crono_low.state` (Crono curHP=10, for the death).
- `_gen_battle_end.py`→`src/data/battle_end.h` regen; decoders `end_trace.py`, `end_sweep.py`, `vdiff.py`, `an2.py`.
- Frames: `be_VIC.raw*` (victory f1160), `be_FALL.raw*` (fallen f100); trace dumps `tr_*.raw*`; renders
  `vic_pose.png`, `fallen.png`, `view_1000/1200.png`.
- `ct_probe.exe` (oracle copy).

---

# ADDENDUM — T4: MARLE's fallen/dead pose DERIVED + baked (closes T3's "only Crono captured")

User report *"marle vanishes when she dies"* — T3 baked only Crono's fallen pose (GATO fight; the Hetake
retarget made Marle's death unreachable then). Now reachable in the **forest fight** (3× Hetake,
`saves/battle_re/forest_battle_3hetake.state`). Marle's collapse is captured + baked exactly like Crono's (T3).

## T4.1 — reaching Marle's death (the enemy targets the LEAD, so Crono must fall first)
With Crono alive, the Hetakes **only ever target the lead (Crono)** — an 900-f idle left Marle untouched at
65→3 HP while Crono took every hit. So (the state-patch trick, `WB=0x45`): **Marle `$5EB0`=3** (dies to one
4-dmg Hetake hit) + **Crono `$5E30`=1** (dies first → the Hetakes retarget Marle). IDLE (`100:0,0:N`).
Timeline (monitor `$5E30/$5EB0/$5E4A/$5ECA` + pal6 cluster): Crono dies **f228** (`$5E4A`→0x80, HP→0);
**Marle dies ~f435** — her cluster drops from (108,112)→**(94,128)** [+16 y = collapse], `$5EB0`→0,
`$5ECA`→0x80; **held stable f435-520** before the party-wipe defeat transition (flags clear ~f528, but her
fallen SPRITE persists). **Both deaths run IN-ENGINE** so the death handler streams Marle's fallen tiles.

## T4.2 — the fallen assembly (a flat prone row) + it is a FROZEN dead sprite
Peek f470 (Marle fallen, stable). **Marle fallen (pal6, anchor 94,128):**
`022@(0,0) 026@(16,0) 024@(32,0) 020@(48,0)` — **4 cells in a horizontal row = a 64×16 prone/lying-flat shape**
(rendered: ponytail right, body flat, legs left — a genuine "fallen on the ground", not a relocated standing pose).
Opaque ink bbox **x[108..149] y[130..143] = 41×13** (vs her ~19×44 standing). **Tiles are FROZEN**: Marle's
0x20-0x37 block is byte-identical f460 vs f508 (`vdiff` = NONE) — a frozen dead sprite (same asymmetry as Crono
T3: dead PCs are NOT removed from the render list, so the OBJ builder keeps drawing the collapsed pose; they do
NOT vanish, unlike enemies). Trigger = the PC dead flag `$5ECA` bit7 (Marle block `$5EAD`+0x1D; Crono was
`$5E4A`, stride 0x80).

## T4.3 — bake + verify (additive to `src/data/battle_end.h`)
Appended a 4th keyframe (`ctBattleEndKf[3]` = Marle fallen, kind=1, pal6, anchor 94,128) + her 4 OBJ cells +
her fallen tile-set. **Additive-only**: `CT_END_NOBJ` 12→16, `CT_END_NKF` 3→4; the 3 existing keyframes' OBJ
cells/tiles and `ctBattleEndPal` stay BYTE-IDENTICAL (palette pinned to the GATO-fight frame). Generator
`tools/_gen_battle_end.py` (`make_marle_fall` patches the forest state, peeks f470).
**Verify: Marle fallen pal6 391/391 mismatch=0** (fully unoccluded — she lies at the screen bottom, below the
forest FG foliage); the 3 prior poses unchanged (400/400, 397/397, 321/321) → **TOTAL 1509/1509 byte-exact.**

## T4.4 — Open questions (T4)
- **Marle-death requires a party-wipe** (Crono dead first): enemy PC-targeting favors the lead, so an
  isolated Marle death (Crono alive) was not reachable by idling. Her fallen pose is captured cleanly in the
  f435-520 window (her death handler runs regardless of Crono's state); the defeat/game-over sequence after
  f528 is out of scope. The fallen ANCHOR (94,128) is forest-formation-dependent (a boundary, as Crono's).
- **Collapse micro-sequence:** the drop to the prone pose is ≤ the sample step (snap, as Crono T3); a hurt-flash
  (pal4) likely precedes it, not isolated here.
