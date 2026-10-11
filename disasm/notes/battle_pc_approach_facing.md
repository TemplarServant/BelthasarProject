# BATTLE — PC ATTACK-APPROACH is a REAL MOVER (Crono RUNS to the victim, not teleport) · GATO has FOUR directional facings (up/down/left/right)

Derives, from the ROM disasm + LakeSnes oracle **only**, two user-reported movement/pose truths:

1. *"in the battle against the Hetake, Crono teleports to do the attack animation"* — our Crono attack
   JUMPS to a baked (victim-relative) strike offset instead of TRAVELLING there. **The oracle physically
   translates Crono's live foot to the victim** — a mover, distance-dependent, byte-derived below.
2. *"animations are missing in the battle against GATO, he should be able to turn left/right"* —
   GATO should turn left/right. **He does: FOUR directional poses (up/down/left/right) selected by his walk
   bearing.** Our render draws one (front) pose regardless of the computed bearing.

Both were the documented boundaries `battle_forest_return.md` D3 (PC approach law un-pinned — no capture of a PC
hitting a non-GATO enemy existed) and `battle_enemy_move_target.md` E.6 (walk-pose bake). This dossier CLOSES
the mechanism for both. **Nothing implemented; `src/` + git untouched (this notes file is the only write).**

**Method.** Oracle `ct_probe2.exe` (= `build/ct.exe`) copied to scratch. Base state `bat3_lake.state`
(the user's forest fight — Crono+Marle vs 3× Hetake, `battle_states_catalog.md`). Drove Crono's physical against
individual Hetakes via `--monitor`/`--btnscript` (target-select navigation derived below), reading the live-
position rows (X=`$1D0C+cmb`, Y=`$1D23+cmb`, `battle_enemy_move_target.md` §A.2) + the mover state + writer PCs
(`--writer`). GATO facing from `gato_press_action.state` (`--monitor` the facing var, `--peek` the OAM/VRAM).
`tools/dis.py` for every routine (file offset = `(bank−0xC0)<<16 | addr`). Button grammar (LakeSnes serial):
A=0x100, Up=0x10 Down=0x20 Left=0x40 Right=0x80. FB decode 512×480 2×-doubled `[pad,B,G,R]` R@+3, fb-row = OAM y+8.

---

## PART 1 — THE PC ATTACK-APPROACH LAW  (Crono RUNS to the victim)

### 1.1 HEADLINE — it is a MOVER, not a teleport, and it is distance-dependent
When Crono's physical dispatches, the ROM **translates his live foot** (`$1D0C+cmb` / `$1D23+cmb`) toward the
victim at **~2 px/frame**, in **8-frame segments**, for **as many segments as the distance requires**, stops
**adjacent to the victim**, plays the strike, then **springs back to his staged home** at **~4 px/frame** via a
*different* routine. It is **NOT** the enemy's atan2/trig mover (§A of the enemy dossier) — PCs use a **separate,
quantised-direction, table-driven mover** with **no `$974A`/`$5E0D` mode machine** at all. Two clean captures
(Hetake0 at 40 px → **2 segments**; Hetake1 at 72 px → **4 segments**) prove the shape adapts to distance.

### 1.2 The live-position system + the confirmed combatant mapping (bat3)
Per-combatant live foot: **X = `$1D0C+cmb`, Y = `$1D23+cmb`**. Read directly from `bat3_lake.state` and matched
to the staging records (`$29B0+5·seat` PCs, `$29BF+12·slot` enemies):

| cmb | who | staged foot (16·rel+fine) | live `$1D0C`/`$1D23` |
|---|---|---|---|
| 0 | **Crono** (id0, seat0) | (160,71) | A0 / 47 ✓ |
| 1 | Marle (id1, seat1) | (127,144) | 7F / 90 ✓ |
| 3 | **Hetake0** (slot0) | (120,63) | 78 / 3F ✓ |
| 4 | Hetake1 (slot1) | (88,95) | 58 / 5F ✓ |
| 5 | Hetake2 (slot2) | (168,127) | A8 / 7F ✓ |

**Target-select navigation (multi-enemy — closes `battle_actionfx.md` P5.4):** the cursor index is **`$9614`**
(0/1/2), Right increments / Left decrements (writer **C1:176F**); `$960D` only resolves the object on *confirm*
(A). So a PC attack on Hetake k = A (Att→target-select) → Right×k → A (confirm). ⚠ Both PCs' ATBs fill; act
promptly (before ~f106) or an autonomous enemy turn preempts the dispatch (the enemy mover is a distinct
subsystem — it never touches `$A517`, so enemy interference is filterable by the cmb row + `$A517==0`).

### 1.3 The mover (executor): CF/EC7A — per-frame (dx,dy) from the ROM track table CC/F494/F594
Per-combatant loop `CF/EC7A` (gated on the move flag `$A15B,Y`, runs while `$A500,Y ≥ 0`):
```
CF/EC87  phase = 7 − $A500,Y            ; phase 0..7 within the segment
CF/EC8D  X = ($A517,Y << 3) + phase     ; $A517 = the movement TRACK; ×8 stride
CF/EC9D  LDA $CCF494,X : ADC $1D0C,Y : STA $1D0C,Y   ; live X += CC/F494[track*8+phase]   (writer CF:ECA7)
CF/ECA8  LDA $CCF594,X : ADC $1D23,Y : STA $1D23,Y   ; live Y += CC/F594[track*8+phase]   (writer CF:ECB2)
CF/ECB3  $A500,Y --                     ; segment duration counter (7→0, then re-armed)  (writer CF:ECBB)
```
**One segment = exactly 8 frames.** The track table (24 tracks × 8 (dx,dy) each; `CC/F494`=X `CC/F594`=Y,
file 0xCF494/0xCF594) is a **quantised-direction primitive set** at 2 px/frame:

| track | (dx,dy)/f | dir | | track | (dx,dy)/f | dir |
|---|---|---|---|---|---|---|
| **0** | (0,−2) | UP | | **8** | (0,+2) | DOWN |
| 1 | (−2,−2) | up-left | | 9 | (−2,+2) | down-left |
| 2 | (+2,−2) | up-right | | 10 | (+2,+2) | down-right |
| 3 | curve | up-left arc | | 11..15 | curve/hop | |
| 4 | curve | up-right arc | | **16** | **(−2,0)** | **LEFT** |
| 5..7 | hop/knockback | | | 17/18 | (−2,∓2) | left diag |

(Tracks 5–7, 13–15, 21–23 are the recoil/knockback/hop primitives — larger, curved deltas.) The `$A517` values
observed were all **0x10 = pure LEFT** (both Hetakes sit left of Crono; the dominant axis is X). GATO-fight Crono
(enemy above) is track **0 = UP** — see §1.7.

### 1.4 The setup + direction selection: C1/5480 — a GRID PATHFINDER (writers C1:55E9 track / C1:54D0 duration)
The track + duration are chosen by **C1/5480**, a **battle-grid pathfinder**: it reads an occupancy grid
`$7E8AFD` with neighbour-offset tables (**CC/F454/F455/F456/F476** grid strides, **CC/F67C** per-direction row
stride), walks toward the target checking walkability (`LDA $8AFD,Y : AND #E0 : CMP #E0` = cell blocked), and
builds the track index `$85` = `direction·8` nudged by obstacle `INC $85`s. Finalize (C1/55E4):
```
C1/55E6  STA $A517,Y            ; track                                (writer C1:55E9)
C1/55E9  LDA $CCF414,X : STA $A548,Y   ; $A548 = CC/F414[track] = the direction code
C1/55F0  LDA $CCF434,X : STA $96DB,Y   ; $96DB = CC/F434[track] = the PC WALK FACING
```
- `$A15B,Y ← 1` (moving), `$A500,Y ← 7` reloaded each segment (C1:54D0), `$A548` = chosen direction (writer C1:55F0).
- **`CC/F434` (track→PC facing) has only THREE facing groups: tracks 0–4 → 0(up), 8–12 → 1(down), 16–20 → 2(left)
  — NO right group. PC RIGHT = h-flip of the LEFT facing.** (Contrast GATO, §2.4, who has a distinct 4th facing.)
  This is the PC's own directional walk-pose selector during the approach (relevant to rendering Crono's run).

### 1.5 TWO-DISTANCE EVIDENCE (the shape adapts to distance) — clean captures, cmb0 = Crono
**Attack A — Hetake0 (default target, X=120):**
```
f116  $A517=10 (LEFT, C1:55E9)  $A500=07  $A15B=01           ; approach armed
f117-132  $1D0C 160→128  (−2/f, writer CF:ECA7)  Y=71 fixed  ; 2 segments = 16 f = 32 px
f132  $A15B→0  ... hold (strike) ...
f181-188  $1D0C 128→160  (+4/f, writer CF:F13F)              ; return, 2× speed
```
Stop **X=128 = Hetake0 120 + 8** (adjacent). **Attack B — Hetake1 (nav Right×1, X=88, 32 px farther):**
```
f110  $A517=10  $A548=03(LEFT)  $A500=07  $A15B=01
f111-142  $1D0C 160→96  (−2/f, CF:ECA7)  Y=71 fixed          ; 4 SEGMENTS = 32 f = 64 px
f190-205  $1D0C 96→160  (+4/f, CF:F13F)                      ; return
```
Stop **X=96 = Hetake1 88 + 8**. **Same track (0x10 LEFT), 2× the distance → 2× the segments (2→4).** Direction is
**axis-quantised**: both went pure-LEFT along the dominant axis (Hetake1 is 24 px lower, but Y stayed 71 — the
approach does not chase the minor axis; it stops adjacent in the dominant axis and the strike lands from there).

### 1.6 The RETURN: CF/F13F — spring back to the staged home (a DIFFERENT routine)
`$A15B` is **0** during the return (not the outbound track mover). CF/F13F:
```
FF136  live X = $A8D5,Y (a decreasing offset, SBC'd toward 0) + $A35D,X (HOME base)   (writer CF:F13F)
FF140  live Y = $A8FB,Y + $A370,X                                                     (writer CF:F149)
```
= interpolate the current (adjacent) position back to the **staged home** `$A35D/$A370` at ~4 px/f. So the round
trip is **asymmetric**: out at 2 px/f (track table), home at 4 px/f (spring).

### 1.7 GATO-BAKE RECONCILIATION — the baked `ctCronoAttackTraj` is the UP-direction instance of this law
`battle_actionfx.md` A2.1 baked Crono's GATO-fight swing as the OAM-cluster trajectory whose **A1 "approach"
phase drifts dy −11→−21 at −2 px/frame**. That −2 px/f up-drift **is this mover's UP track (track 0 = (0,−2))**
translating Crono's live foot toward GATO (who sits ABOVE him) — the OAM cluster = live foot + pose, so the baked
traj already contains the up-translation, fused with the strike pose. Because GATO is close and directly above,
that instance is a short (1–2 segment) UP approach, which is why baking it as a **fixed** offset "worked" for
GATO but is **target-independent** and fails for a Hetake that sits left/right/far (`battle_forest_return.md`
B.2). **The general law: approach = the CC/F494 track mover, direction = the pathfinder's dominant direction to
the victim, length = distance; the GATO bake is its up-instance.** (A live GATO-fight `$A517` recapture was not
obtained — that state boots with Crono already move-flagged — but the −2 px/f up rate is the table's own
signature; no other subsystem produces it.)

### 1.8 MARLE verified IN-PLACE (ranged, no mover)
Marle's live position `$1D0D`=176 / `$1D24`=111 is **constant through her entire attack** (selector
`$9877=0x0F`, her crossbow) — **zero live translation**. The ≤10 px anchor step `battle_actionfx.md` MA.2
measured is **pose-only** (her sprite assembly), not a foot move; the bolt is the separate pal0 spark (MA.3).
So the melee/ranged split is crisp: **Crono's physical arms the approach mover; Marle's ranged physical does
not.** Our engine's planted-Marle render is canonical — **no change**.

### 1.9 IMPLEMENTATION SPEC (attack render/mover)
Our `battleAttackRender` (`field.c:2348`) currently draws the baked pose re-homed to `victimLiveFoot − GATO_baked`
(a fixed hop, no travel). Replace the APPROACH half with the real mover; keep the strike-pose bake:

- **Bake the track table** (mandate-clean, ROM verbatim): `CC/F494`/`CC/F594` (24×8 signed (dx,dy), file
  0xCF494/0xCF594) → a `ctPcMoveTrack[24][8]` header (same provenance as `battle_trig.h`).
- **Per PC attack, host state:** `pcMvActive`, `pcMvTrack` (`$A517`), `pcMvPhase`(0..7)/`pcMvDur`(`$A500`),
  live foot (init = the PC's staged foot), and the home foot (for return).
- **Arm on dispatch:** direction = the **dominant cardinal** from the PC's foot to the victim's live foot
  (`battleEnemyComputedAnchor(target)+battleEnemyLiveDelta`): |dx|≥|dy| → LEFT(track16)/RIGHT / else UP(0)/DOWN(8)
  — the derived-minimal stand-in for the C1/5480 grid pathfinder (both captures took the dominant axis; the exact
  grid path incl. diagonals/obstacle nudges is OPEN, §3). Segment count = `ceil((dist − 8)/16)` (2 px/f × 8 f =
  16 px/segment; stop 8 px = one step from the victim foot).
- **Tick (per render frame, while active, gated like the enemy mover — `$A013==0`):** every frame add
  `ctPcMoveTrack[track][phase]` to the live foot; `phase++`; at phase 8 decrement the remaining segments; when
  0, stop, mark ARRIVED. **Render Crono's swing pose at the LIVE foot** (translate the pose with the foot), so
  the sword-arc plays adjacent to the victim — where he actually arrived.
- **Return:** interpolate the live foot back to the staged home at 4 px/f (mirror CF/F13F) after the strike/recoil.
- **Keep** the baked strike/recoil/settle pose keyframes (`ctBattleAnimKf` A3–A6) for the adjacent portion; only
  the A1 "approach" translation becomes the real distance-scaled mover. In the GATO fight this reproduces the
  baked up-approach (dominant axis = UP, ~2 segments) → **the GATO pins stay byte-faithful** (the shift is (0,0)
  there), while the forest strike now lands ON the Hetake. No pinned SHOT captures Crono mid-swing → no fnv moves.
- **(fidelity) render Crono's WALK facing** during the run from `CC/F434[track]` (up/down/left, right=h-flip) — the
  PC directional walk-pose bake, a follow-on (parallels the GATO facing bake, §2.6).

---

## PART 2 — GATO'S FACING TRUTH  (four directional poses)

### 2.1 HEADLINE — GATO turns to face his walk direction; there are FOUR facings
GATO's OAM shows **four distinct directional poses** — up / down / left / right — selected by a per-frame
**facing byte** derived from his walk bearing. Our engine computes the bearing (`mvAngle`) but never selects a
pose from it, so it draws the front idle pose in every direction (`battleGatoRender` E.6). The four poses use the
**same tile NUMBERS** (0x68–0x8E) with **no h-flip/v-flip attributes** — the direction is carried by **per-facing
streamed tile CONTENT + per-facing OAM assembly**.

### 2.2 The facing var: `$96DE+slot` = ctBattleFaceTab(C0/F700)[bearing]
Computed every frame in the enemy approach handler **C1/379A**:
```
C1/37C9  JSR C1/0222              ; bearing $DB = atan2(target − self)
C1/37E9  LDA $C0F700,X (X=$DB)    ; facing = ctBattleFaceTab[bearing]   (already baked, battle_trig.h)
C1/37EF  STA $96DE,X  (X=slot)    ; store per-enemy facing               (writer C1:37EF)
```
Decoding `ctBattleFaceTab[0..255]` against the mover's angle convention (0x00=+X right, 0x40=+Y down, 0x80=−X
left, 0xC0=−Y up): bearing 0xA0–0xDF→**0**, 0x20–0x5F→**1**, 0x60–0x9F→**2**, 0xE0–0x1F→**3**. i.e.:

| `$96DE` | facing | bearing centre |
|---|---|---|
| **0** | UP | 0xC0 (−Y) |
| **1** | DOWN (front) | 0x40 (+Y) |
| **2** | LEFT | 0x80 (−X) |
| **3** | RIGHT | 0x00 (+X) |

### 2.3 The four facings — OBSERVED (gato_press_action, HOLD → GATO acts un-hit and walks)
Monitoring `$96DE+3` (GATO = enemy slot 3) + his live X/Y through a full HOLD fight:

| window | `$96DE` | motion | facing |
|---|---|---|---|
| f421–692 | **1** | Y 6F→7D (walks DOWN toward Crono) | DOWN / front |
| f739–771 | **2** | X 88→80 (walks LEFT) | **LEFT** |
| f1018–1050 | **3** | X 80→87 (walks RIGHT) | **RIGHT** |
| (return, up) | **0** | Y decreasing (walks UP) | UP / back |

So GATO's facing tracks his walk bearing across all four directions — the horizontal legs (facing 2/3) are the
user's "left/right". (His *natural* GATO fight is vertical-only because Crono sits directly below; the
horizontal legs appear when he targets Marle, who sits to his right at his height, or via a patched horizontal
target.) **Visually confirmed** (peek renders `facing/{down,left,right}_gato.png`): down = front view; **left =
body clearly turned to the side**; right = a distinct turned 3/4 pose.

### 2.4 The tile / attr deltas per facing (what actually differs)
Decoding GATO's 12 body cells (pal1, tiles 0x68–0x8E) at a down/left/right frame:
- **Tile NUMBERS are identical** (0x68,6A,6C,6E + 0x80,82,84,86,88,8A,8C,8E) in every facing.
- **`hf = vf = 0` in every facing** — the direction is **NOT** an OAM h-flip.
- **The tile CONTENT differs** per facing (VRAM md5 of the body block: down `7731…` / left `9a03…` / right
  `143f…`) — GATO **re-streams** the directional sprite pixels into the same tile slots.
- **The OAM ASSEMBLY differs** per facing (each cell's x,y offset within the cluster is re-laid-out).
- **LEFT and RIGHT are NOT h-flips of each other** (23/24 body tiles distinct, right ≠ hflip(left)); likewise
  left≠down. GATO has **four fully-distinct directional sprite sets**, not a 3-pose + flip scheme (unlike the PC,
  §1.4). (The comparison also spans walk-cycle phases; the within-facing per-frame tile stream is the C1/CC
  anim-engine boundary — a rep-frame per facing, as with every other GATO bake.)

### 2.5 The selector — `$96DE` is a per-frame RENDER PARAMETER, not an anim-script switch
GATO's anim-script pointers `$1CCC`/`$1CAB` (slot 3) are **constant** (`09BFE4`/`C072E4`) across all four
facings — the facing does **not** reload the anim script. So `$96DE` is read **per frame by the OBJ-frame render
/ assembly engine** (the C1/CC interpreter), which picks the directional cell layout and streams the directional
tile content. The mover's facing output (ctBattleFaceTab, already in `battle_trig.h`) → `$96DE` → the pose the
render draws. Our engine has the table baked and computes the bearing; it just never uses it to pick a pose.

### 2.6 BAKE / RENDER SPEC (GATO facing)
- **Compute the facing** in the render from the mover: `facing = ctBattleFaceTab[mvAngle[slot]]` while walking
  (mvAngle is the same atan2 bearing the ROM indexes); **hold** the last facing while idle/attacking; default
  idle = **1 (front/down)** (GATO faces the party — our current baked idle pose).
- **Bake four directional walk poses** for GATO (same provenance as the idle/attack bakes, `battle_anim.h`):
  each facing = its OAM assembly (12-cell x,y layout) + its streamed OBJ tiles (0x68–0x8E content) + pal1.
  Capture the rep frame per facing from the oracle (the peeks in `facing/` cover down/left/right; up = the
  return-walk `gm_*` sidecars). **Four distinct sets** — do NOT synthesise right from left by h-flip (§2.4).
- **Select the pose bake by `$96DE`/computed facing** in `battleGatoRender` instead of always drawing the front
  idle pose. This closes E.6 (walk-pose bake) — and it is **four facings**, the specific "turn left/right".
- Gate exactly as today (enemy-id/slot keyed; no scene literal): only an enemy whose mover actually turns uses it.
- **MASH-safe:** GATO's when-hit counter (block2, mode byte 0) never walks (`battle_enemy_move_target.md` ADDENDUM),
  so the facing stays front (1) and every pinned MASH fight is byte-identical; only the un-hit HOLD walk turns.

---

## PART 3 — OPEN questions / boundaries
- **P1 The exact PC approach PATH (grid pathfinder C1/5480).** Both captures took the dominant cardinal axis and
  stopped adjacent; the full ROM path uses the battle occupancy grid `$7E8AFD` + neighbour tables
  CC/F454/F455/F456/F476/F67C and can choose diagonal/curve tracks and route around occupied cells. The
  observable law (dominant-direction, 2 px/f, N=distance, stop adjacent) is derived and sufficient for the fix;
  the exact grid routing is the remaining boundary (port C1/5480 + the grid for byte-exactness).
- **P2 The exact adjacency/stop test.** Crono stopped 8 px from the victim foot in both captures (victim foot +
  Crono extent 8). Whether the stop is the C1/28B0 box-overlap (as enemies) or a grid-cell-adjacency test in
  C1/5480 is not separated — 8 px is the measured datum.
- **P3 GATO-fight PC `$A517` recapture.** The gato_press_action donor boots with Crono already move-flagged, so a
  fresh in-fight track read was not obtained; the up-track reconciliation rests on the baked −2 px/f up rate
  (§1.7). A pristine pre-Crono-turn GATO state would pin it directly.
- **P4 The per-facing walk-cycle tile stream (GATO).** Only rep-frame content per facing is characterised; the
  within-facing per-frame pixel stream is the standing C1/CC anim-engine boundary (as for the idle/attack bakes).
- **P5 The full C1/5480 grammar** (the `CC/F414` direction code `$A548` and `CC/F434` PC-facing `$96DB` uses in
  the render; the curve/hop tracks 3–7/11–15/21–23 and when the pathfinder selects them).
- **P6 Other PCs / enemies.** Only Crono's melee approach + Marle's ranged-planted + GATO's 4 facings captured;
  the track mover (`$A15B`/`$A517`) and the facing var (`$96DE`) are generic — other melee PCs approach the same
  way, other walking enemies turn the same way (their per-id directional bakes are the same oracle-read datum).

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/e5608df4-…/scratchpad/`
- `ct_probe2.exe` (oracle copy = build/ct.exe). Base states `batalha_state/bat3_lake.state` (forest, PC approach),
  `gato_press_action.state` (GATO facing), `gato_moving_lake.state` (GATO up/return walk).
- `facing/` : `render.py` (pure-Python PNG cropper, no PIL), `{down,left,right}.raw*` peeks + `{down,left,right}_gato.png`
  renders (the four-facing visual proof), the VRAM/OAM decode.
- Existing sidecars reused: `gm_30/60/90.raw.oam` (GATO up-return walk = facing 0), `gm_400.raw.oam` (idle).
- Repro (all commands inlined per §): target-select nav = `$9614` (Right/Left); PC-attack = `A, Right×k, A`;
  GATO horizontal facing = HOLD on gato_press_action (GATO targets Marle → walks left/right f739/f1018).
- Key ROM: PC mover `CF/EC7A` (writers CF:ECA7/ECB2), tracks `CC/F494`/`CC/F594` (0xCF494/0xCF594), pathfinder
  `C1/5480` (writers C1:55E9/54D0, tables CC/F414/F434/F454/F455/F456/F476/F67C), return `CF/F13F` (CF:F13F/F149);
  enemy facing `C1/379A` @37E9 (`$96DE` ← C0/F700[bearing]).

---

# IMPLEMENTATION ADDENDUM 2026-07-12 — both parts SHIPPED; conventions pinned in-engine

**PART 1 (PC approach)** — src/battle.c `battlePcAtkArm`/`battlePcAtkTick` + `src/data/battle_tracks.h`
(tools/_gen_battle_tracks.py: CC/F494/F594 baked verbatim, 24×8 signed pairs; cardinals 0=UP/8=DOWN/16=LEFT
assert-checked; RIGHT = negated LEFT per §1.4 — no (+2,0) track exists in the ROM table). Modelled as an
OFFSET (home foot − arrival foot) added to the swing pose: full at dispatch → 0 at arrival (adjacent =
victim foot ± 8 on the dominant axis) → back at 4px/f from rel 60 (inside the 79f pose window). The A1
approach pose's baked trajectory drift is DROPPED at render (the mover now owns the translation — §1.9's
"only the TRAVEL changes"). Validated: forest Hetake0 32px LEFT = 16f = 2 segments (== §1.5 Attack A
byte-for-byte), a lower victim = (0,±48) = 3 segments, GATO fight = (0,37) UP ≈ §1.7's up-instance →
every pinned fight is fnv-identical (empty re-pin table); pinned `field-battle-pc-approach-early/-late`.
P1 (the C1/5480 grid pathfinder), P2 (exact stop test) stay OPEN — dominant-cardinal form shipped.

**PART 2 (GATO facing)** — `src/data/battle_gato_faces.h` (tools/_gen_gato_faces.py: UP=gm_60, LEFT/RIGHT=the
facing/ captures; 12 pal1 cells each, tiles 0x68–0x8E, hf=vf=0, DISTINCT body md5s — all §2.4 asserts) +
`h->mvFacing[slot] = ctBattleFaceTab[bearing]` stamped at both mover aim sites (approach C1/37E9 + return)
and held at rest; battleGatoRender selects the bake for facing 0/2/3 (facing 1 = the committed idle bob,
per §2.6). **Coordinate-convention pin (new datum):** the capture fb proves oracle-fbRow = OAM y + 8, and
our render rows = oracle − 8 ⇒ OUR fb space == OAM space (exactly the equivalence the committed idle's
OAM-space GATO_REF (104,62) already uses); cells are stored relative to the capture's live foot
($1D0C+6/$1D23+6) and drawn at the live foot (136+gdx, 111+gdy). NB the at-rest idle capture's min-corner
sits at foot+(−32,−53) — 5px above the A8.1 stance-frame extent (−32,−48): an idle/settle PHASE datum, not
a convention error (the facing bakes anchor to their own capture feet, so no cross-frame extent is
assumed). Facing distribution validated on the un-hit HOLD fight (bearings 0x06-0x0F→3, 0x32-0x40→1,
0x8D-0x92→2, 0xC0→0 = the §2.2 decode); shots f2830/f3080 match the oracle right/left references; pinned
`field-battle-gato-facing-left/-right`. MASH fights never walk → all older pins byte-identical (proved:
full suite green with zero re-pins). P4 (within-facing walk cycle) stays OPEN; attacks keep the front
rear-up bake (no directional attack capture — documented at the render).

---

## ADDENDUM 2026-07-12 — the "teleports to a different spot" TRIAGE + single-base fix (session-inline)

User report post-mover-port: Crono still teleported when attacking. Root cause BY INSPECTION (the planned
oracle triage lane died on the account spend limit; the seam is provable from the bake semantics alone):
`ctCronoAttackTraj.dx/dy = capture anchor − (109,108)` (battle_anim.h) — i.e. the baked trajectory AND each
pose's rep-frame cells EMBED the capture's own approach/return travel. The render then added the live
approach-mover offset (pcAtkMvOff, home-relative) + the victim shift ON TOP → the capture travel and the
live travel double-counted, time-shifted: at swing start the position jumped by mvOff(full) plus the
capture-travel misalignment, again at arrival and at the A1→A2 pose switch.

FIX (single base — the live mover owns ALL travel when armed): re-base every pose on the STRIKE rep
(`rebase = traj[rep(A4)] − traj[rep(pose)]`), drop the drift for the TRAVEL poses (A1 approach / A6 settle,
whose drift IS capture travel), keep it for the SHAPE poses (A2..A5 arcs). Rendered position =
`cell + drift + rebase + victimShift + mvOff` — algebraically continuous at every seam: rel0 =
home_live + shape (matches the idle sprite), strike = the unchanged oracle-faithful victim-adjacent capture
(rebase=0, mvOff=0), settle end = home_live again. Unarmed (already-adjacent) keeps the old capture-faithful
law. Implemented in battleSkeleton (battle.c) + battleAttackRender (field.c, drift now added unconditionally).

EVIDENCE: forest attack shot sweep f160..196 — frame-diff motion is ONE contiguous cluster progressing
smoothly (centroid y 56→76→109→94→settle; overlapping x-spans 104-167); a teleport would show two disjoint
clusters. GATO fight: no mid-swing shot is pinned; all GATO checks green unchanged. Re-pins (justified):
field-battle-pc-approach-early f172 99BE5EBB→2EA7859B, -late f181 D169A15B→A24EAAF8 (the pinned mid-run
frames legitimately moved — the run now STARTS at the idle spot, which was the fix's whole point).
Suite: quick 58/0, FULL 80/0/2.

OPEN (unchanged): the exact C1/5480 grid-pathfinder routing; the capture-travel vs live-travel residual
wobble within the run (bounded ~2px/f schedule desync, invisible in the sweep) resolves for free when the
real anim engine port lands (the poses will then be positioned by the engine, not by capture-absolute cells).
