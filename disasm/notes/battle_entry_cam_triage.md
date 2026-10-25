# GATO battle-entry camera JUMP triage — "the camera moves UP abruptly and then moves DOWN again slowly"

User report (2026-07-16): at GATO battle entry our camera JUMPS up abruptly, then slowly glides back
down. Triage of the regression introduced by commit **8670d77** (op-0xE7 blocking seek + the field
`arenaCamState` render latch). ROM+oracle only, zero invention. Prereq reading: `battle_entry_camera.md`
(the camera MECHANISM) — this file is the follow-up that measures the SPECIFIC visible defect 8670d77 shipped
and specs the structural fix.

## VERDICT UP FRONT

The symptom is **real and reproduced byte-for-byte**. It is the visible face of the project's **two-camera
divergence** (`battle_entry_camera.md` A.5): our field has TWO camera models — the RENDER camera (a
player-follow placeholder `camy = py−112`, clamped) and the VM field camera `$1D0A/$1D0E` (what op-0xE7 seeks
and staging reads). The **oracle has ONE** (`$1D0A/$1D0E` IS the rendered scroll). Commit 8670d77 added a
3-state `arenaCamState` latch whose state 1 RENDERS the op-0xE7 seek by reading `$1D0E·8`. At the frame the
seek arms, the render camera switches from the player-follow value (**31** for GATO) to the seek's coarse
start `$1D0E·8` (**24**) — a **−7 px instant JUMP (camera pans UP = "moves up")** — then the seek's fine
accumulator glides `24→32` over 8 frames (**+8 px, ~1 px/frame = "moves down slowly"**). The net correct move
was a **1 px** nudge (31→32); we instead do a −7/+8 detour. Before 8670d77 the render stayed player-follow
until D8 then snapped, so the divergence was a hidden static offset, not a moving jump — 8670d77 turned the
static offset into a visible bounce.

**The seek/staging side (VM `$1D0A/$1D0E`, the arena origin) is byte-correct and must not change.** The bug is
purely the RENDER camera's discontinuous ENTRY into the glide. Fix = enter the glide CONTINUOUSLY (from the
current render camera), or unify the two cameras (the standing field-camera milestone). Details + spec below.

---

## PART A — THE SYMPTOM, REPRODUCED (our render camera, frame-by-frame)

`CT_FIELDSHOT` (field.c:6252-6268) prints the LIVE render camera each dumped frame: `fieldshot: fN camx=.. camy=..`.
Driving our engine through the whole GATO E7→D9→D8 chain from a pre-D8 seed (minted below), sweeping
`CT_FIELDSHOT_AT`:

```
build/Belthasar.exe --assets ct_assets.dat --scene 6 --battletest <SCRATCH>/gato_seek_pre.raw.wram
   env: CT_ROM=Chrono_Trigger_USA_SNES.sfc  CT_FIELDSHOT=<out>  CT_FIELDSHOT_AT=<N>
```
seed = f180 (2 frames before the E7 arms at f182). Frame N below is RELATIVE to the seed.

```
relf   our render camy   state / event
 0     31                arenaCamState 0 (player-follow); $1D00=0, seek not armed
 1     31                player-follow, steady
 2     24   <<< JUMP     E7 arms ($1D00 1) -> arenaCamState 1 -> CtSceneCameraSeekRender enters at $1D0E·8 = 3·8 = 24
 3     25                seek fine accumulator +1 px/frame
 4     26
 5     27
 6     28
 7     29
 8     30
10     32                fine hits 8 -> coarse $1D0E 3->4 -> 32 (= target $1D03·8); seek COMPLETE
12..40 32                held through D9 walk + D8 into battlePhase
```
camx = 0 the whole time (GATO's X target is 0, already 0 — no X motion).

**Screen-direction mapping (the render loop `fb[y] = base[camy+y]`, field.c:5847-5848):** a SMALLER camy shows
higher map rows = content shifts DOWN = **camera pans UP ("moves up")**; a LARGER camy = **camera pans DOWN
("moves down")**. So:
- f1→f2: camy **31→24** = pan **UP −7 px, in ONE frame** = *"the camera moves UP abruptly"* ✓
- f2→f10: camy **24→32** = pan **DOWN +8 px over 8 frames (~1 px/f)** = *"and then moves DOWN again slowly"* ✓

An exact match to the report. Visual confirmation: `gato_fs_1.png` (camy 31) vs `gato_fs_2.png` (camy 24) — the
whole frame sits ~7 px lower in f2 (fnv 651FB3BC vs 4FA6399B).

**Why our pre-E7 render camy = 31 (not 24):** `CT_CAMINFO` reports the GATO map `W=H=256`, `mask[0,256]x[0,256]`
(`my0=0, my1=256`), lead obj1 at `py=165`. Player-follow `camy = py−112 = 53`, CLAMPED to `my1−CAMH−1 =
256−224−1 = 31` (CAMW=256, CAMH=224, field.c:68-69; clamp field.c:5837). So the render camera is pinned at the
map's bottom camera bound (31). Note the settled battle origin (**32**) is 1 px BELOW that bound — the
`arenaCamState`/battlePhase path (field.c:5800-5816) bypasses the `my1` clamp, so the fight deliberately frames
1 px past the normal field limit.

### A.2 SCENARIO-GENERAL — the forest shows the same jump, larger

Same recipe, `--scene 19 --battletest saves/battle_re/forest_seek_pre.raw.wram` (seed f50, E7 arms rel f2):
```
relf   our render (camx,camy)   event
 0     (15, 503)                player-follow, steady
 1     (15, 503)
 2     ( 8, 480)  <<< JUMP      E7 arms -> render enters at ($1D0A·8, $1D0E·8) = (seek coarse start); $1D0E pre-E7 = 0x3C = 60 -> 480
 6     ( 4, 484)                glide
10     ( 0, 488)                (X reached the 0 bound)
34     ( 0, 512)                (matches the pinned check field-battle-forest-seek-glide: f34 camy=512)
60     ( 0, 538)                gliding toward camY target 0x44 = 68 -> 544
```
Forest jump at arm: camy **503→480 = −23 px UP** (+ camx −7), then glide **480→544 = +64 px DOWN**. Same
structural defect, much bigger — confirming this is NOT GATO-specific but the general two-camera divergence.

---

## PART B — ORACLE COMPARISON (one camera, no jump)

`ct.exe --monitor "0100:2,1D00:1,1D02:2,1D0A:2,1D0E:2,0117:2" --from gato_press_action.state --input "100:8,0:290" --diff --writer`
(⚠ monitor byte-order = address order, `battle_entry_camera.md` header):
```
f0     loc=0600  $1D00=0  $1D0A=00 $1D0E=03   <- camera ALREADY at camY 3 (24px), 182 frames before the E7
f182   $1D00=1 (C0:3E57)  target $1D02=00,$1D03=04    ; op-0xE7 'E7 00 02' ARMS
f185   $1D0E 03->04 (C0:94C1)                          ; camera Y 24 -> 32, a SINGLE 8px step
f186   $1D00 1->0 (C0:3E3B)                            ; op advances
f218   $0117=2100 (C0:3AA8)                            ; D8 fires
f290+  bytes repurposed by the battle module (CC:E308/E952)
```
`--diff` prints a row only on CHANGE: `$1D0E` is **silent from f0 to f184**, i.e. the oracle camera sits at
**24 px steady** the entire pre-battle window, then makes ONE 8 px step to 32 at f185, and holds. There is
exactly ONE camera (`$1D0E` = the field scroll = what renders), it is already at the arena framing (24), and
it moves **8 px total, continuously**. No jump. (Forest oracle, `battle_entry_camera.md` A.3: `$1D0E` 0x3C→0x44
= 480→544, one 8 px step / 8 f from f60 — same, one continuous camera.)

**The crux, side by side:**

| | pre-E7 RENDER cam | seek coarse start | settled origin | JUMP at arm |
|---|---|---|---|---|
| GATO **oracle** | 24 (`$1D0E`) | 24 (same var) | 32 | **none** — glide 24→32 |
| GATO **ours** | 31 (`py−112` clamped) | 24 (`$1D0E·8`) | 32 | **−7 px** then glide 24→32 |
| FOREST **oracle** | 480 (`$1D0E`) | 480 (same var) | 544 | **none** — glide 480→544 |
| FOREST **ours** | 503 (`py−112`) | 480 (`$1D0E·8`) | 544 | **−23 px** then glide 480→544 |

The oracle has NO jump because its pre-E7 render camera and the seek's start are **the same variable**
(`$1D0E`). Ours jumps because they are **different variables** that disagree by 7 px (GATO) / 23 px (forest) —
the un-RE'd field-Y-camera placeholder (`camy=py−112`, field.c:5836; the memory `ct-field-camera-scroll-model`
flags "Y center (oracle 136)" as the standing TODO) diverges from the real field scroll `$1D0E`.

---

## PART C — ROOT CAUSE

1. **Two cameras (the standing divergence, `battle_entry_camera.md` A.5).** Our RENDER camera is the
   player-follow placeholder (`camX` stateful X + `camy=py−112`, clamped, field.c:5817-5838). The VM field
   camera `$1D0A/$1D0E` is a SEPARATE variable. Proof our VM never maintains `$1D0A/$1D0E` during field play:
   the ONLY writes to them in the whole codebase are the op-0xE7 seek stepper (`ct_vm.c:1919/1926`) — nothing
   else. In the battletest they hold 24/480 only because the SEED carried the oracle's live value; in a purely
   interactive session our `$1D0E` is whatever scene-load left it (not player-tracked). The oracle's one
   camera is `$1D0E` itself, maintained every frame by the ROM field camera-follow (C0/8833→9175/944B, DP=$1D00).

2. **8670d77 made the seek RENDER, exposing the divergence as motion.** `git log -S` confirms both
   `arenaCamState` (field.c) and `CtSceneCameraSeekRender` (ct_vm.c) were introduced by 8670d77. The latch
   (field.c:5793-5816): state 0 = player-follow, **state 1 = pre-battle seek/hold**, state 2 = battlePhase snap.
   The instant the seek arms (`$1D00 != 0`), the latch flips 0→1 and the render camera is taken from
   `CtSceneCameraSeekRender` (ct_vm.c:1935-1946), which returns `curY*8 + dirY*fineY` — i.e. it **enters at the
   seek's coarse `$1D0E·8` (24), discarding the player-follow render value (31) it replaces**. That
   substitution IS the −7 px jump; the following fine-accumulator ramp is the (correctly-paced) glide. Before
   8670d77, state 1 did not exist: the render stayed player-follow (static 31) until D8, then battlePhase
   snapped 31→32 = a 1 px change (invisible) — which is why GATO "looked acceptable for a static-camera engine"
   (`battle_entry_camera.md` A.4). Adding the glide surfaced the pre-existing 7 px camera gap as a visible bounce.

**So: the bug is not the seek and not the staging (both byte-correct). It is that the render camera's glide
STARTS at a point discontinuous from the render camera it replaces.**

---

## PART D — FIX SPEC (structural; pace stays derived, only the START point is at issue)

Two tiers. (b) is the mandate-correct end state ("one general machine = one camera"); (a) is the minimal
interim that removes the reported symptom without the full field-camera RE.

### D.1 — TIER (b): UNIFY the field camera (the real fix, oracle-exact, scenario-general)

RE the ROM field camera-follow (C0/8833 → 88EE/9175/99DE/9238/944B, DP=$1D00; the memory
`ct-field-camera-scroll-model` TODO: "Y center (oracle 136), scripted cam-seek, persistence") so our VM
**maintains `$1D0A/$1D0E` as the live field scroll every frame**, and make the RENDER camera READ `$1D0A/$1D0E`
directly (retire the `camy=py−112` placeholder, field.c:5836). Then:
- Pre-E7 the render camera already equals `$1D0E·8` (24 GATO / 480 forest) = the oracle. The op-0xE7 seek
  `24→32` / `480→544` renders **continuously with zero special-casing** — the jump cannot exist because there
  is only one camera, exactly like the oracle.
- The `arenaCamState` states 1 and 2 collapse into "render = `$1D0A/$1D0E·8`", which is what they already do —
  they just stop being a SWITCH away from a divergent player-follow camera.
- **This simultaneously fixes the static "enemies to the right"** (`battle_entry_camera.md` A.5): terrain and
  combatants have shared one camera all along.
Cost: it is the deferred field-camera milestone (ct_vm.c:1400 calls it out). Larger, but it is the only fix
that reproduces the oracle's intermediate frames and is correct in every scene, not just battle entry.

### D.2 — TIER (a): minimal — enter the glide from the CURRENT render camera (interim, no field-camera RE)

Keep everything byte-exact on the VM/staging side. Change only the state-1 RENDER source (field.c:5808-5816):
instead of taking the render camera from the seek's coarse `$1D0A/$1D0E` (`CtSceneCameraSeekRender`), **carry
the render camera continuously**:
```
on arenaCamState 0->1 (seek just armed):
    seed a render-glide accumulator from the CURRENT render camera (the player-follow camx,camy of the last state-0 frame)
each state-1 frame:
    step (rcamx,rcamy) toward the E7 TARGET ($1D02·8, $1D03·8) by the derived 1 px/frame, clamp on arrival
    render at (rcamx,rcamy)
battlePhase (state 2): snaps to $1D0A/$1D0E·8 (= the target) -> continuous (the glide already arrived there)
```
Result: GATO glides **31→32** (1 px, effectively static — correct: GATO's arena is already framed); forest
glides **(15,503)→(0,544)** monotonically (a real reframe — satisfies the user's other note *"the camera should
move"*). **No −7/−23 jump.** The VM seek (`$1D0A/$1D0E`, ct_vm.c:1907-1930) and staging are untouched,
so the arena origin the combatants anchor to stays byte-exact.
- **Mandate note:** the PACE is the derived 1 px/frame (unchanged); the only thing changed is the glide's
  START (from the seek coarse `$1D0E·8` to the live render camera). Choosing the current render camera is the
  physically-continuous initial condition, not an invented easing.
- **Target source** = the E7 target `$1D02/$1D03` (set from the script operands each arm, field.c op-0xE7 /
  ct_vm.c seek) — reliable even in interactive play where `$1D0A/$1D0E` are not player-maintained. Do NOT read
  the seek's coarse `$1D0A/$1D0E` for the render START.
- **Caveat (a):** during the glide the render camera (31→32 / 503→544) and the VM origin `$1D0E·8` (24→32 /
  480→544) differ by up to the 7/23 px gap. Pre-D8 this is cosmetic-only — no combatants are staged yet, and
  the D9-walking PCs are field sprites drawn at the render camera (consistent with the terrain). At D8 both are
  the arena origin (32/544), so staging is consistent. (a) does NOT reproduce the oracle's intermediate frames
  (oracle starts the glide from 24/480; we start from 31/503) because the pre-existing Y-camera-follow error
  remains — (a) simply stops ADDING a teleport on top of it. Only (b) removes that residual.

**Recommendation:** (b) is the correct structural fix and retires this whole class (battle-entry jump + static
"to the right") at once; schedule it as the field-camera milestone. If this ticket must land without that RE, (a)
removes the reported symptom mandate-safely and is forward-compatible (it is a no-op once (b) makes the render
camera already continuous). Do NOT "fix" it by reverting to a snap — the camera SHOULD move (the forest needs
the 64 px reframe; the user explicitly wants motion).

---

## PART E — BOUNDARIES / OPEN

- **Interactive vs seeded `$1D0A/$1D0E`.** The measured traces seed `$1D0E` from the oracle WRAM (so the seek
  starts at the oracle-correct 24/480). In a from-scratch interactive Belthasar session our VM does not maintain
  `$1D0A/$1D0E` (Part C proof), so the seek could start from a stale value and the jump/glide would differ
  (likely worse). This is the same root (no field-camera) and is exactly what tier (b) fixes; tier (a) is
  robust to it because it glides the render camera to the script TARGET, not from the stale coarse scroll.
- **Exact oracle field-Y-camera law un-RE'd.** Why the oracle sits at `$1D0E`=24 (GATO) / 60 (forest) pre-E7
  (its follow model + clamp, DP=$1D00) is the standing camera milestone; only the SETTLED origin is needed for
  staging, and it is byte-exact. Our 31/503 vs oracle 24/480 is the placeholder error, not introduced here.
- **`CT_CAMINFO` prints camx but not camy** (field.c:5841) — camy was recovered from `CT_FIELDSHOT`'s
  `fieldshot:` line (which prints both). No src change was made (mandate: this triage wrote only this file).
- Screenshot artifacts confirm the numeric trace; the numbers (printed render camx/camy) are authoritative.

## Repro / artifacts (scratchpad, NOT committed)
- exes: `<SCRATCH>/ct.exe`, `<SCRATCH>/Belthasar.exe` (+ SDL2.dll) — copies of build/ (HEAD cdeb11d).
- **GATO pre-D8 seed** (minted here, mirrors `forest_seek_pre.raw.wram`):
  `ct.exe --peek gato_press_action.state <SCRATCH>/gato_seek_pre.raw --btnscript "100:8,0:172"` -> `.wram`
  (f180: `$1D00=0`, `$1D0A=00 $1D0E=03`, loc=0006).
- our render trace: the `--scene 6 --battletest gato_seek_pre.raw.wram` + `CT_FIELDSHOT_AT` sweep (Part A).
- oracle trace: the `--monitor … --from gato_press_action.state --input "100:8,0:290" --diff --writer` (Part B).
- PNGs: `gato_fs_1.png` (camy 31, player-follow), `gato_fs_2.png` (camy 24, jumped), `gato_fs_10.png` (camy 32,
  settled) via `<SCRATCH>/fs2png.py` (fieldshot `[u32 w][u32 h][RGB]` -> PNG).
- forest cross-check: `--scene 19 --battletest saves/battle_re/forest_seek_pre.raw.wram` + `CT_FIELDSHOT_AT` sweep.
