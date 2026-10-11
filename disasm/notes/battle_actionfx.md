# BATTLE ACTION FEEDBACK — damage numbers, attack sequence, hurt flash (GATO fight)

Derives the battle **action feedback** the module renders on a hit — the user report *"there is no
attack or damage animation at all"* (`src/battle.c` draws the HUD strip + the static entry stance only, no
action feedback). Picks up from `battle_stance.md` S5 (the enumerated-but-not-derived attack/hurt
boundary) and `battle_loop.md` S3 (the damage pipeline `$AD89`→`$B328`→`$5E30`, whose value is what the
popup shows). Priority order **P1 damage numbers ▸ P2 attack sequence ▸ P3 hurt flash ▸ P4 GATO attack.**

Everything is measured from `ct_probe.exe --peek` dumps of the canonical GATO fight (party Crono seat0 +
Marle seat1 vs GATO enemy-slot3), base state `gato_press_action.state`, driven by a deterministic **A-mash**
(`100:16,0:16` repeated — the `battle_loop.md` mash; boots the fight, Crono→GATO then GATO→Crono physicals).
`--peek` is self-consistent per dump and byte-reproducible for a fixed btnscript (proven: re-peeking f505/f540
reproduces the swept y). FB decode (`battle_stance.md`): fb 512×480 doubled, true px `(x,y)=fb(2x,2y)` bytes
`[pad,B,G,R]`; **an OBJ pixel at OAM (x,y) lands at fb true-pixel (x, y+8)**. OBJ = 4bpp, CGRAM `128+pal*16+v`
(v=0 transparent), 5→8-bit `(v<<3)|(v>>2)`. Decoders in scratch: `mash.py` (exact-frame mash + HP/digit
report), `digverify.py` (digit pixel-diff vs fb), `akin.py` (attack kinematics + flash + slash), `an2.py`
(pose clusters). All frame numbers are the **mash peek-timeline** (see the timeline caveat at the end).

---

## P1 — DAMAGE NUMBERS (the rising popup digits) — BYTE-EXACT, baked + verified

### P1.1 The digit font: OBJ tiles `0x190+d` = '0'..'9', palette 0, 8×8, prio 3
Rendering the OBJ VRAM strip `0x190..0x199` (any hit frame, `digtest.py`) yields the glyphs **'0'..'9' in
order** — so **digit `d` → OBJ tile `0x190 + d`**. Each is an **8×8 sprite (OAM size bit 0 = small), palette
0, prio 3, no flip**. Confirmed on two independent hits by the tile numbers alone:
- **Crono → GATO "17"** → tiles `0x191`('1'),`0x197`('7')
- **GATO → Crono "14"** → tiles `0x191`('1'),`0x194`('4')

`0x190` = OBJ VRAM byte `0x3200`, inside the **battle-boot OBJ upload region 0x3000-0x3C00** (`battle_stance.md`
S1) — the digit strip is uploaded by the boot like the pose tiles; **baked from VRAM** (provenance identical to
the stance bake). The digit tiles use only palette indices **0(clear)/1(white fill `pal0[1]=0x7FFF`)/2/3(outline)**;
pal0 is **stable across the whole fight** (`dmg_st17`==`dmg_st14`==`dmg_pk17` pal0, verified).

### P1.2 Value = the damage dealt (source: the `battle_loop.md` S3 pipeline)
The number shown **equals the damage applied**, proven by the curHP delta at the frame the popup clears:
| popup | digits | target curHP | delta |
|---|---|---|---|
| "17" | 0x191,0x197 | GATO `$6130` 76→59 (`cnt` f558) | −17 |
| "14" | 0x191,0x194 | Crono `$5E30` 70→56 (`cnt` f788) | −14 |

So the displayed value is the **staged/applied physical damage** `$AD89 → $B328,Y → curHP $5E30,X`
(`battle_loop.md` S3, compute `C1/DCED`, stage `C1/EC29`, apply `C1/EC99`). **Multi-digit = natural decimal,
no leading zeros** (a 1-damage-digit hit shows a single tile — observed a lone `d9` sprite in a partial run).

### P1.3 Layout + position
- **Layout:** one 8×8 OBJ per digit, columns **8 px apart** (`"17"`: `0x191@x128`, `0x197@x136`), common y,
  leftmost = highest place.
- **Horizontal position = centered on the target's sprite center X.** GATO sprite bbox centerX = **135**;
  the "17" spans x128..144, centerX **136** (exact). (For a PC target the number sits at a stored per-combatant
  anchor: Crono's "14" centerX 134 vs his idle-sprite centerX 125 — the exact PC anchor formula is a BOUNDARY,
  the popup routine; measured constants below suffice for replay.)
- **Measured per-target origin** (left-digit x, rest row y): **GATO leftX 128, restY 96 · Crono leftX 126, restY 129.**

### P1.4 Motion — a parabolic hop, MEASURED byte-exact, target-independent
Step-1 dense sweep of the "17" popup (`arc` sweep). **Vertical offset from the rest row, per render frame,
index 0 = birth** (x is constant the whole life — a pure vertical animation):

```
idx :  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15 16 17 18 19 20 21 22 23 24
dy  : +7  -1  -7 -10 -12 -13 -14 -15 -16 -15 -14 -13 -12 -11 -10  -9 -8 -7 -6 -5 -4 -3 -2 -1  0
```
Born **+7 below** rest, rises **decelerating** to the peak **−16 (idx 8)**, then falls **linear +1/frame** back
to rest (idx 24), then **holds at rest (dy=0) ~31 frames**, then vanishes. The **"14"/Crono popup samples the
IDENTICAL table** (step-3 offsets −10,−14,−15,−12,−9,−6,−3,0 match the "17" at the same relative frames) ⇒ the
arc is **target-independent** (applied over each target's own rest row). Baked verbatim as `ctBattleFxRiseDY`.

### P1.5 Lifetime + event timeline (mash peek-timeline)
**Lifetime ≈ 56 render frames** = 25 (rise, birth→rest) + ~31 (hold at rest). Crono→GATO sequence:
`strike/flash f476-484` → `number born f497` → `rest f521` → `held …f552` → `gone f553` → **`curHP applied f558`**.
The number thus **leads the HP write** and clears just as HP drains.

### P1.6 VERIFY — byte-exact vs the oracle fb
`tools/_gen_battlefx.py --verify` regenerates the settled "17"(f540) + "14"(f755), reconstructs the digits from
the baked tiles+pal0 at the oracle's own digit OAM positions, pixel-diffs vs fb:
```
f540 "17" over GATO  digits @(128,96)(136,96)  digit opaque px exact=73/73  mismatch=0
f755 "14" over Crono digits @(126,129)(134,129) digit opaque px exact=79/79  mismatch=0
```
(At the peak frame 5/73 px are GATO-body occlusion — GATO sprites at lower OAM index draw over the digit — a
verify artifact, not a digit error; the settled rows are clean 100 %.)

---

## P2 — Crono's ATTACK sequence (skeleton pinned; fine trajectory OPEN)
`akin.py` (Crono pal5 cluster anchor + flash + slash per frame) + `an2.py` poses. Crono's physical on GATO:

| phase | frames (mash) | evidence |
|---|---|---|
| wind-up (raise) | ~f450→474 | anchor climbs idle (109,108) → **(127,83)**; pose `008@(12,0) 00E@(0,14) 00C@(12,16) 00A@(12,32)` (act_f470) |
| **lunge + STRIKE** | **f476** | anchor **(115,95)**; GATO flashes white + slash appears (P3); pose `00E@(16,0) 008@(16,16) 00A@(0,20) 00C@(16,32)` (act_f600) |
| recoil / return | f486→532 | anchor returns to idle **(109,108)** by f532 |

**Skeleton:** idle → **wind-up hop up/toward GATO (~24 f)** → **lunge-strike (~f476, ~8 f at the strike pose)** →
**return to idle (~by f532)**; total attack window ≈ **f450→558** (~108 f incl. the number/HP tail), the swing
motion itself ≈ f450→532 (~82 f). Same 4 tile slots (0x08/0A/0C/0E) throughout; content streams.

**Correction to `battle_stance.md` S5:** it read "Crono translates off-screen (no pal5 at f640) — he walked to
GATO". FALSE — at f640 Crono is rendered in **pal4 (the hurt flash, GATO's counter landing)**, not absent; and
across the whole attack his cluster stays in his column **x109-127, y83-108** — **he does NOT walk off-screen**,
he wind-up-hops and lunges roughly in place. The fine per-frame body trajectory is **OPEN** (the cluster
min-corner is pose-contaminated and the tiles stream; needs the C1/CC anim-slot `$0520+n*0xC` correlation,
`battle_stance.md` S4 boundary).

---

## P3 — HURT / HIT FLASH (mechanism pinned + baked; knockback OPEN)
On impact the **target's own pose sprites are palette-swapped 1/5/6 → 4**; **OBJ palette 4 is all-white**
(`ctBattleFxFlashPal`: idx 1..15 = `0x7FFF`/`0xFFFF`, idx 0 transparent) ⇒ the target renders as a **solid-white
silhouette**. It is a **swap, not an overlay**: at flash frames the target has **only pal4 sprites, none in its
normal palette** (f480: GATO = 12 pal4 tiles `0x68..0x8E`, **zero pal1**; f640: Crono = 4 pal4 tiles `0x08..0x0E`,
zero pal5). No new tiles needed — the flash reuses the combatant's baked pose tiles (`battle_stance.h`) in pal4.

- **Duration ≈ 9-10 frames** (GATO flash present f476-484, gone f486; `akin` step-2).
- **Slash / cut effect:** alongside the flash, **pal0 OBJ tiles `0x0C0..0x0DF`** (a diagonal cut, ~2 sprites)
  appear at the target for **~15 frames** (f476-490) — slightly outlasting the flash.
- **Knockback: none clearly measured** — the target holds position through the flash (GATO idle anchor stays
  ~(104,63)); marked OPEN.
- Crono-as-target flash confirmed independently (GATO's counter, act_f640: Crono all-white pal4).

---

## P4 — GATO's own attack (fell out partially)
GATO's counter on Crono uses the **same feedback path**: Crono flashes pal4 white (act_f640), then the **"14"
damage number** rises over Crono (P1, same font/motion/lifetime; curHP `$5E30` 70→56 = −14 at f788). GATO's own
attacker **animation poses** (his wind-up/lunge sprites) were not densely captured and are **OPEN** (his body
tiles `0x68..0x8E` stream during idle already, `battle_stance.md` S2); the *feedback* half (flash + number on the
victim) is fully covered by P1/P3.

---

## ATB freeze ↔ action-window relation (for the caller's cadence work)
Through the whole action span `$95D5` (current actor) reads **0xFF (none)** — the turn scheduler does **not open a
new turn while an action animates**. Crono's action occupies ≈ **f476(strike)→f558(HP apply) ≈ 82 frames**; then a
long **inter-action gap f558→f725 (~167 f)** before GATO's counter number — that gap is **GATO's ATB recharge +
bank-CC AI first-turn delay** (`battle_loop.md` S1 §B / ADDENDUM T2: `$B158` recharge + ~72-tick AI wait), **not** a
freeze. So the "post-attack freeze" the caller observed = the **~82 f action-animation window** (scheduler paused)
*plus* the recharge gap. All spans are in the mash **peek-timeline**; the loop runs at the render cadence, and the
peek↔logic-tick ratio (~2.5 f/tick, global `$993A`) is the `battle_loop.md` ADDENDUM-2 boundary — the action
window and the recharge are render-frame counts here, convert via `$993A` for logic ticks.

---

# PART 2 — REPLAY SPEC (what to bake + the render/tick rule)

**Baked — `src/data/battle_fx.h`** (emitted by `tools/_gen_battlefx.py`, deterministic from the oracle):
- `ctBattleFxDigitTile[10][32]` — OBJ 8×8 tiles '0'..'9' (tile `0x190+d`), 4bpp planar, from in-battle VRAM.
- `ctBattleFxDigitPal[16]` — OBJ palette 0 (BGR555 LE); digit = white idx1 + idx2/3 outline; idx0 clear.
- `ctBattleFxFlashPal[16]` — OBJ palette 4, all-white (the hurt/hit flash), from flash frame f480.
- `ctBattleFxRiseDY[25]` — the per-frame vertical offset from rest (P1.4), then hold at 0 for `CT_FX_HOLD`.
- Constants: `CT_FX_DIGIT_BASE 0x190`, `CT_FX_DIGIT_W 8`, `CT_FX_DIGIT_PAL 0`, `CT_FX_FLASH_PAL 4`,
  `CT_FX_RISE_N 25`, `CT_FX_HOLD 31`, `CT_FX_LIFETIME 56`.

**Render rule — the DAMAGE NUMBER** (composite as OBJ, prio 3, over the combatants, once a hit's value is known):
```
value  = the damage applied to the target (battle_loop.md: $AD89 / $B328,Y / curHP delta)
s      = decimal(value)                      // no leading zeros
leftX  = targetCenterX - (len(s)*8)/2        // centered on target sprite center X (GATO exact)
                                             //   PC target: use the stored per-combatant anchor (measured
                                             //   Crono leftX 126); exact formula = BOUNDARY
for k,ch in enumerate(s):
    dy = ctBattleFxRiseDY[min(age, CT_FX_RISE_N-1)]     // age = frames since the number was born
    draw 8x8 OBJ tile (CT_FX_DIGIT_BASE+ch), pal CT_FX_DIGIT_PAL,
         at OAM ( leftX + 8*k , restY + dy )            // fb row = OAM y + 8 (as the stance bake)
kill the number when age >= CT_FX_LIFETIME (it clears ~as curHP drains)
```
Per-target rest row measured: **GATO restY 96, Crono restY 129** (a stored per-combatant display Y + offset;
formula BOUNDARY). Verified **73/73 + 79/79** opaque px byte-exact.

**Render rule — the HURT FLASH:** for ~`9` frames after a hit lands, draw the **target's own stance sprites**
(`battle_stance.h`) using `ctBattleFxFlashPal` instead of its normal palette (a full-white silhouette). Optionally
draw the slash effect (OBJ tiles `0x0C0..0x0DF`, pal0) at the target for ~15 f — **NOT baked** here (out of P1
scope; enumerate/​bake later if wanted).

**Attack sequence (P2):** if replaying Crono's swing, key the pose on phase — idle → wind-up pose (act_f470
assembly, anchor raised to ~(127,83)) → lunge/strike pose (act_f600 assembly, anchor ~(115,95)) → idle — over
~82 render frames, strike at the flash frame. The pose assemblies are the same tile slots as the stance; the
fine per-frame trajectory + tile-stream is OPEN (the C1/CC anim engine, `battle_stance.md` S4/S5 boundary).

**State that drives each element:** the number needs `{value, targetCenterX, restY, birthFrame}` captured when the
hit's damage is computed; the flash needs `{targetSeat/slot, hitFrame}`. Both are emitted by the ROM's damage
pipeline (`battle_loop.md` S3) — our engine already computes `value` and knows the target, so the feedback layer
is a pure render addition keyed on "a hit just landed on target T for value V at frame F".

---

## Verify
`CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/_gen_battlefx.py --verify --emit`
→ `73/73` (GATO "17") + `79/79` (Crono "14") opaque digit px, 0 mismatch; writes `src/data/battle_fx.h`.

## Open questions
- **Per-target number anchor formula** (target screen pos → number origin X/Y): measured constants pinned
  (GATO 128/96, Crono 126/129); the exact ROM rule (the popup routine's read of the combatant display coord) is a
  BOUNDARY. Horizontal centering-on-sprite-center holds exactly for the enemy target (GATO).
- **Fine attack trajectory / pose per-frame** (P2) — the C1/CC OBJ anim engine (`$0520+n*0xC`, `FD:E3A9`) +
  streamed tiles; only the skeleton (wind-up/lunge/return poses + timings) is pinned.
- **Knockback on hurt** (P3) — none measured; confirm on a cleaner capture before asserting zero.
- **Slash/impact effect** (P3) — tiles `0x0C0..0x0DF` pal0 identified + timed, not baked (out of P1 scope).
- **GATO's attacker poses** (P4) — his own swing animation not captured; the victim-side feedback is covered.
- **crit / miss / multi-digit≥3 / heal (green) numbers, victory poses, techs** — not in this fight; the digit
  font (0x190-0x199) + motion generalize, but crit-color / "Miss"/​"0" glyphs (tiles `0x19A/0x19B` seen unused
  near the strip) need their own capture.

## Scratchpad artifacts (paths for the caller; not committed)
Dir: `%TEMP%/claude/…/e5608df4-…/scratchpad/`
- `dmg_st17.raw*` (settled "17"/GATO f540), `dmg_st14.raw*` (settled "14"/Crono f755), `dmg_pk17.raw*` (peak),
  `fx_flash480.raw*` (GATO hit-flash) — verify/bake frames.
- `mash.py` (exact-frame A-mash driver), `digverify.py`, `akin.py`, `digtest.py`, `scan_novel.py`, `palcmp.py`.
- `ct_probe.exe` (oracle copy). Task logs: `tasks/*.output` (dense arc f494-524, popup f472-566, counter
  f704-800, kinematics f460-576, flash f474-496).

---

# ADDENDUM — P5: the TARGET-SELECT state (Att → cursor → confirm/cancel → dispatch)

User report (translated from Portuguese): *"after choosing Att. it should be possible [to select the target]"* — after choosing Att there is a
target-select step our engine skips. Derives the full command→dispatch state machine. Method = `--monitor`
(authoritative for input — the `--peek` A-edge misregisters, `battle_hud_render` ADDENDUM 2 §B) to pin the RAM
+ transitions, then `--peek` at a `$9609`==1 frame for the cursor OAM, + `dis.py` on the handler. Base
`gato_press_action.state`; single-A-on-Att input `100:8,0:412,100:8,0:…` (menu opens f383, cursor `$95DC`=0=Att).

## P5.1 — there IS a target-select state, and it REQUIRES a confirm A even with ONE enemy

Pressing A **once** on Att sets **`$9609`=1 at f422 and it STAYS 1** (monitor `100:8,0:412,100:8,0:170`) with
Crono's ATB `$AFAB`[0] held at 0 — **the attack does NOT dispatch**. So `$9609`=1 is the **target-select state**,
awaiting a second input. It is exited three ways (all monitored):
- **A (confirm target):** `$9609`→0, `$95D5`→**0xFF** (Crono dequeues / commits his action); the attack then
  executes and his timer **reloads `$AFAB`[0]=0x48 by f578** (`100:8,0:412,100:8,0:20,100:8,0:180`). = DISPATCH.
- **B (cancel):** `$9609`→0 but `$95D5` **stays 0** — back to the command menu, Crono still choosing
  (`…,1:8,0:50`; f449 `$9609`=00 `$95D5`=00 `$95DC`=00). = CANCEL.
So **confirm is mandatory even for a single enemy** — the "mash-A always attacks" (`battle_loop`/P2) works
because each A **edge** advances one step: A(Att)→target-select, A(target)→dispatch. Two edges, not one.

## P5.2 — the cursor: a 16×16 hand OBJ, tile 0x000 pal1 prio3, over the target (reuses the foot-quad OAM slots)

At a target-select frame (`tgt_f450`, `$9609`=1), the OAM gains a sprite NOT present in the menu frame
(`tgt_analyze.py` OAM-diff vs `idle_f440`): the module **repurposes GATO's foot-quad OAM slots #0-3** — in the
menu they are tile 0x006 (foot); in target-select they become **tile 0x000, pal1, prio3, size1 (16×16)**, with
**#1-3 parked off-screen (x = −126/−134)** so only **#0 shows on-screen**. Rendering tile 0x000 pal1 from the
target VRAM = a **pointing-hand cursor** (the "☞"/mano, distinct from the HUD command hand which is BG3 tiles
0x160-3). Position tracks the target: `#0` x = **120** (constant), y = **65→70** as GATO idle-settles (f436 y65,
f450 y68, f468/f488 y70) — the cursor sits at a fixed offset above the target sprite and inherits the target's
small idle motion. **Measured cursor OAM for GATO: (120, ~68)** (fb row = y+8). The exact per-target anchor
formula (target screen pos → cursor origin) is a **BOUNDARY** (same class as the damage-number anchor, P1.3);
the measured constant suffices for the single-enemy replay. Cursor-sprite builder = **C1/1F79** (called on entry).

## P5.3 — the state machine + gate bytes (cited from ROM)

Command-confirm handler entry **C1/1290** dispatches by cursor row `$95DC`: **Att(0)→C1/129C**, Tech(1)→C1/12BC,
Item(2)→C1/12ED. The **Att→target-select** path (`dis.py 11290`):
```
1129C LDA #FF ; JSL CD002D            ; (cursor gfx setup)
112A2 INC $A43F ; INC $A43F
112A8 LDA #07 ; STA $960D             ; $960D = 07  = the TARGET object index (GATO = battle-obj 7); a CONSTANT
112AD STZ $9615                       ;   here (default/first enemy) — multi-enemy nav would rewrite $960D
112B0 JSR C1/1F79                     ; build the target cursor OBJ (tile 0x000)
112B6 INC $9609                       ; $9609 = 1  → ENTER target-select
112B9 JMP C1/179C
```
Writers confirmed by `--monitor --writer` at f422: `$9609`=1 ← **C1:12B9**; `$960D`=07 ← **C1:12AD**;
`$960A`=01 ← **C1:20B4**. Per-state gate bytes for the engine to replay:

| state | gate bytes | render | input → next |
|---|---|---|---|
| **command menu** | `$95D5`=seat, `$9609`=0, `$95DB`=0 | box + hand at `$95DC` | A → confirm row `$95DC` (Att→below) · Up/Down nav `$95DC` (ADD 2 §B) |
| **target-select** | `$95D5`=seat, **`$9609`=1**, `$95DB`=0, target=`$960D` | hand OBJ tile 0x000 pal1 over target[`$960D`] | **A → DISPATCH** (`$9609`→0, `$95D5`→0xFF, exec, reload `$AFAB`=0x48) · **B → CANCEL** (`$9609`→0, `$95D5` stays) |
| (Tech/Item) | `$95DB`=1 / 2 | submenu replaces strip | ADDENDUM 2 §B (submenu = boundary) |

## P5.4 — Open questions (target-select)
- **Multi-enemy navigation:** only one enemy (GATO) in this fight; `$960D` is loaded with the constant #07 on
  Att-confirm, so target nav (Left/Right/Up/Down cycling `$960D` among live enemies) is **unobserved = BOUNDARY**.
  The single-target cursor + confirm/cancel is fully pinned.
- **Cursor anchor formula:** measured (120, ~68) for GATO, tracking the target sprite; the ROM read that maps
  target obj → cursor origin (via C1/1F79) is not traced — boundary (like the P1.3 number anchor).
- **Whole-menu B (from the command level, not target-select):** not tested; §P5 covers B **within** target-select
  (→ back to command menu).

---

# ADDENDUM A2 — Crono's ATTACK swing: the FULL per-frame trajectory (supersedes the P2 skeleton)

User report *"crono, when attacking, does 'jump' animations; in ours he 'teleports'"* — our engine jumps between
3 waypoints, the oracle shows a rich swing. This pins **every frame** of Crono's physical on GATO.

**Method.** `--monitor "0700:512,0900:32"` of the **OAM shadow** `$7E0700` low + `$7E0900` high (== the live
`.oam`, proven byte-exact `battle_stance.md` S4) streamed **per frame in ONE oracle run** (fast path; validated
`monitor fN == peek fN` + the shadow decode == the `.oam` decode at f460/f480). **MASH** driver
(`100:16,0:16` repeated) from `gato_press_action.state` → exactly **one** clean Crono physical: dispatch f447 →
return-to-idle f526. Per frame the Crono cluster (pal5) = anchor (min-corner) + assembly `tile@(dx,dy)[flip]`.
Scratch: `mon.py`/`anz.py`/`ctable.py` (monitor parse + cluster + poseId table); the `*`-change-marker on
changed monitor lines must be stripped or the changing frames are silently dropped.

**Correction to P2.** P2 labelled `008@(12,0) 00E@(0,14) 00C@(12,16) 00A@(12,32)` the "act_f470 wind-up" — that
assembly is actually the **STRIKE/lunge** pose (measured f473-499). P2's `act_f600` "lunge" pose
`00E@(16,0) 008@(16,16) 00A@(0,20) 00C@(16,32)` is not in the swing at all — it is Crono's **hurt/flinch** pose as
GATO's target (measured f623+, ADDENDUM A3.3). P2's frame labels were from a shifted capture; the ladder below is
the byte-exact per-frame truth.

## A2.1 — the 7-pose ladder (mash timeline; ref = idle anchor (109,108); dx,dy = anchor − ref)

| pose | frames | dwell | anchor (dx,dy) | assembly (pal5) | motion |
|---|---|---|---|---|---|
| **P0 idle** | ≤f446 | — | (0,0) | `008@(16,13) 00A@(0,7) 00C@(17,28) 00E@(16,0)` | settled stance (`battle_stance.h`) |
| **A1 approach** | f447-452 | 6 | (9,−11)→(9,−21) | `008@(8,0) 00A@(8,16) 00C@(16,32) 00E@(0,32)` | steps in + rises **−2 px/frame** (SMOOTH) |
| **A2 gather** | f453-457 | 5 | (17,−16→−18) | `008@(0,0) 00A@(16,15) 00C@(0,15) 00E@(0,31)` | coils to strike |
| **A3 raise** | f458-472 | 15 | (18,−25) | `008@(0,0) 00A@(1,16) 00C@(1,48) 00E@(1,32)` | sword raised HIGH, held |
| **A4 strike** | f473-499 | 27 | (6,−13) | `008@(12,0) 00A@(12,32) 00C@(12,16) 00E@(0,14)` | lunge fwd+down; **GATO flashes f476** (P3); sword-arc streams in tiles |
| **A5 recoil** | f500-508 | 9 | (17,−26)→(17,−10) | `008@(16,8) 00A@(0,0) 00C@(0,32) 00E@(0,16)` | bounces up to −26 then **falls −26→−10** (SMOOTH, f500-506) |
| **A6 settle** | f509-525 | 17 | (17,−10) | `008@(0,0) 00A@(0,16) 00C@(0,48) 00E@(0,32)` | upright post-strike, held |
| **P0 idle** | f526+ | — | (0,0) | (stance) | 1-frame snap back to idle |

Total swing **f447→526 ≈ 79 render frames**. The anchor **SNAPS** pose→pose (the oracle itself does not tween
between poses) with **two smooth drifts**: A1 (dy −11→−21, −2/f) and A5 (dy −26→−10). The "teleport" our engine
shows = only P0/A3/A4/P0 (3 waypoints) — the 4 intermediate poses (A1 approach, A2 gather, A5 recoil, A6 settle)
+ the two smooth drifts are MISSING. The full 91-frame `(dx,dy,poseId)` table is baked as `ctCronoAttackTraj`
(f440-530).

## A2.2 — tiles stream per-frame within every pose (the anim-engine boundary)
The tile NUMBERS never change (always 0x08/0A/0C/0E) but the **8×8 CONTENT streams every frame** — `vdiff` of the
Crono OBJ region (tiles 0x08,0x09,0x18,0x19,0x0A,0x0B,0x1A,0x1B) differs between *any* two attack frames, even
within one pose's dwell (f448 vs f451 in A1; f459 vs f472 in A3; f474 vs f499 in A4; f501 vs f505 in A5) — the
sword-arc/arm drawn frame-by-frame by the **C1/CC OBJ anim engine + FD:E170 tile streamer** (`battle_stance.md`
S4 boundary). So each keyframe's OBJ tiles are **baked per pose** (like the entrance, `battle_entrance.h`): the
bake is **byte-exact at each pose's rep frame** (verify below), the within-pose per-frame pixel stream is the
boundary. 6 keyframes A1-A6 baked (OAM assembly + own OBJ tiles 0x00-0x9F + pal5) in `src/data/battle_anim.h`.

# ADDENDUM A3 — GATO's OWN attack (his swing) — DERIVED (closes P4)

User report *"gato shows no animations when attacking"*. P4 had only the victim-side feedback; this pins GATO's
attacker animation.

**Method.** **HOLD** driver (`100:8,0:N-8` = one boot-A then hold) → Crono sits ACTIVE in the command menu and
**never attacks**, so GATO's ATB fills and it strikes Crono in **ISOLATION** (no confounding Crono action).
Cross-checked against the MASH GATO counter.

## A3.1 — GATO attacks by a REAR-UP + claw-swipe (not a walk to the target)
- **Idle→lunge (f505):** GATO's anchor jumps **(104,62)→(110,56)** [dx +6, dy −6] and the 12 body cells reshuffle
  to the attack pose `068@(18,0) 06A@(33,16) 06C@(18,16) 06E@(2,16) 080@(18,64) 082@(34,32) 084@(18,32)
  086@(2,32) 088@(25,48) 08A@(10,48) 08C@(35,4) 08E@(0,4)`. The **feet stay planted** (foot-quad tile-0x06 y
  still ~132) while the **body top rises 6 px** ⇒ GATO **rears up**. Held **f505-553 (~49 f)**.
- **Claw-swipe:** during the hold the OBJ tiles **0x80-0x9F** (VRAM `0x1000-0x1400`) **STREAM** (`vdiff` f510 vs
  f535 — tiles 0x80-0x9F changed; tiles 0x68-0x6E did NOT) — the claw/fist attack drawn frame-by-frame on the
  fixed rear-up OAM (same anim-engine boundary as Crono's A4). Note `vdiff.py`'s GATO tile list only checks
  0x68/0x78, so it reports "none" — the stream is in the 0x80+ range.
- **Feedback on Crono:** Crono flashes **pal4 white f532-534 (3 f)**; the **"9"** damage number rises over Crono
  **f572+** (born (130,139), the P1 arc); **Crono curHP `$5E30` 70→61 (−9) at f640**.
- **Return:** GATO anchor back to **(104,62) at f554**; idle resumes. Foot-quad drops +3 px at f540 (minor).

## A3.2 — canonical (timeline-independent)
The **MASH GATO counter** shows the **byte-identical** lunge assembly at **(110,49)** — the **same (+6,−6)** delta
from its idle (104,55) — f596-680, flash on Crono f620-650, "14" number f740. So GATO's rear-up attack pose is the
**same in both timelines** (only the absolute idle-y differs by the idle bob). Baked: keyframe **G1_rearup**
(byte-exact, verify below).

## A3.3 — bonus: target KNOCKBACK (refines P3's "knockback OPEN")
GATO's strike **displaces the target**: in HOLD, Crono moves from idle (109,108) to **~(118,111)** then
**(118,114)** during f528-551 (~+9,+3..+6 px, ~24 f), returns to (109,111) at f552. In MASH, Crono-as-target
instead **oscillates (109,108)↔(111,101) every 16 f** (a low-HP flinch, pose `008@(16,16) 00A@(0,20) 00C@(16,32)
00E@(16,0)`). So a hit **does** move the target ~9 px — measured, but the exact profile is timeline/HP-dependent;
not a single pinned curve.

# ADDENDUM A2/A3 — VERIFY (byte-exact vs oracle fb)
`tools/_gen_battleanim.py --verify` reconstructs each baked keyframe from its baked OBJ tiles, composites ALL
combatants by OAM index for correct cross-combatant occlusion, and pixel-diffs the acting combatant's top pixels
vs the oracle fb (fb row = OAM y + 8; d>24 = mismatch):
```
Crono A1_approach f450  exact=370/370    Crono A4_strike  f485  exact=347/347
Crono A2_gather   f455  exact=350/350    Crono A5_recoil  f503  exact=394/394
Crono A3_raise    f465  exact=425/425    Crono A6_settle  f515  exact=345/345
GATO  I0_armsRest f395  exact=1860/1860  GATO  I1_armsSwap f460  exact=1911/1911
GATO  G1_rearup   f520  exact=1938/1938    TOTAL 7940/7940 (100.00%)
```
(I0/I1 = the GATO idle keyframes, `battle_stance.md` ADDENDUM 4.) Baked → `src/data/battle_anim.h`
(`ctBattleAnimKf`/`ctBattleAnimTiles`/`ctBattleAnimPal` + trajectory tables `ctCronoAttackTraj`,
`ctGatoIdleTraj`, `ctCronoIdleBob`). Generator `tools/_gen_battleanim.py` (self-contained: peeks the 9 rep
frames + runs 2 `--monitor` streams for the trajectory tables).

## Open questions (A2/A3)
- **Within-pose per-frame tile stream** (the sword-arc / claw-swipe pixels) — the C1/CC anim engine
  (`$0520+n*0xC`, `FD:E3A9`/`FD:E170`); only each pose's rep-frame tiles are baked. Same boundary as stance/entrance.
- **Target knockback exact profile** (A3.3) — measured ~9 px but timeline/HP-dependent; not a single pinned curve.
- **Other attackers / techs / multi-hit** — only Crono's sword physical + GATO's claw captured; the ladder shape
  (approach→gather→raise→strike→recoil→settle) + the rear-up mechanism generalize but need per-character captures.

---

# ADDENDUM A4 — GATO's claw-SWIPE tile stream DERIVED (closes A3.1's "the swipe is the anim-engine boundary")

User report *"GATO's attack animation is not complete"* — we render only the held rear-up (kf8
`G1_rearup`, one rep frame). A3.1 pinned that during the rear-up hold **f505-553** the OBJ tiles **0x80-0x9F
STREAM** (the claw/fist attack drawn frame-by-frame) but left it as the anim-engine BOUNDARY. This DERIVES the
distinct swipe frames. Method: HOLD driver (isolated GATO attack), `--peek` f505-553 step-3, hash the OBJ tile
block **0x80-0x9F**; `an2.py` for the OAM assembly. Bake is ADDITIVE in `src/data/battle_anim_add.h` (generator
`tools/_gen_battleanim_add.py`).

## A4.1 — the rear-up OAM is FIXED; the claw streams **3 distinct swipe frames** over 0x80-0x9F
The GATO cluster (pal1, 12 body cells) is **byte-identical at anchor (110,56) every frame** f508-553 (verified
`an2.py` f508/f529/f535 = same 16-cell assembly `068@(18,0) 08E@(0,4) 08C@(35,4) 06E@(2,16) 06C@(18,16)
06A@(33,16) 086@(2,32) 084@(18,32) 082@(34,32) 08A@(10,48) 088@(25,48) 080@(18,64)` + the bracket/parked slots).
So the swipe is **pure tile-content stream in 0x80-0x9F** over the fixed rear-up OAM. Hashing 0x80-0x9F gives
**3 distinct swipe frames** (after the idle→rearup transition V0 f505):

| swipe frame | tiles | render window | rel to dispatch (f505) | note |
|---|---|---|---|---|
| **GS1 windup** | V1 (`0x1a62…`) | **f508-526** (~18 f) | rel 3-23 | == the shipped `G1_rearup` content (kf8) |
| **GS2 strike** | V2 (`0x67da…`) | **f529-532** (~4 f) | rel 24-29 | the **flash-on-Crono lands @rel27** (`CT_GATO_STRIKE`), inside GS2 |
| **GS3 follow** | V3 (`0x1a62…f93c`) | **f535-553** (~18 f) | rel 30-48 | follow-through / recover |

Most sub-tiles change (0x80-0x9F almost all; 0x84/85/88/8B/94/95/98/9B change 3-4×). The body block 0x68-0x6F
changes ONCE (idle→rearup at f508), then holds. So the replay: while GATO's rear-up window animates
(`fxEnemyRearup`), pick the swipe frame by `rel=(frame − rearup-dispatch)`: `a<=rel<b`. Baked
`ctGatoSwipeKf[3]` (fixed rear-up OAM + each swipe frame's own OBJ tiles) + the `[a,b)` schedule.
**Verify: GS1 1782/1782 · GS2 1707/1707 · GS3 1771/1771 = 5260/5260 byte-exact.**

## A4.2 — Open questions (A4)
- **Within-swipe-frame stream:** step-3 gave 3 clean run-lengths; a finer per-frame stream INSIDE each swipe
  frame is the S4 anim-engine boundary (`$0520+n*0xC`, `FD:E170`) — rep-frame tiles baked, as elsewhere.
- **Enemy-generic:** only GATO's claw captured; the Hetake spore-lunge (battle_scenario2 T1.4) streams its own
  0x40-0x47 block by the same mechanism (rep-frame only). Other enemies need their own capture.

---

# ADDENDUM MA — MARLE's crossbow ATTACK (PC_RANGED) — DERIVED + baked (closes "marle's attack animation is not working")

Our engine renders Marle **only as her static waiting stance** (`src/field.c:1763-1770`, explicit comment
*"there is NO Marle idle-variant layer… no Marle attack layer"*) — she never animates an attack. This pins her
full physical. **Method** = the same OAM-shadow monitor as A2 (`--monitor "0700:512,0900:32"`, strip `*`) + the
canonical **42× A-MASH** from `gato_press_action.state`; in that mash Marle takes her turn at **f869** and her
attack anim runs to ~f1020. FB renders (`ct.py fb2png`) + per-frame OAM confirm the visuals. Bake +
pixel-verify: `tools/_gen_battlemarleatk.py` → `src/data/battle_marleatk.h` (NOT wired).

## MA.1 — dispatch + the selector $9877 = 0x0F (was unpinned)
Marle's turn (`--monitor "95D5,9877,AEEB,AEE3,AEE5,AE93"`, 42× mash):
```
f821  $95D5 = 01           Marle becomes the current actor (her seat)
f867  $95D5 -> FF          she dequeues / commits (target confirmed)
f869  $AEEB = 02           PC_RANGED damage formula loaded (C1/E184, battle_interp_verify T3.B) -> = Marle
f872  $9877 = 0x0F         her ATTACK ANIM selector   <-- the value battle_interp_verify left unpinned
```
**$9877 = 0x0F.** Anim-script pointer `CD/5FF0[0x0F] = CD/7164` (read from ROM; table per battle_interp_verify
T2.A). Her script is **multi-track**: header `90 00 90 00` + track pointers **CD/7170, CD/719C, CD/7170,
CD/960E** — the last (`CD/960E`, a distinct 0x96xx region) is a separate **effect track** (the bolt/impact),
distinguishing her from Crono's basic `CD/5FF0[0x00]=CD/6E0C`. (Per-opcode decode = the anim-engine boundary.)

## MA.2 — the animation: a CROSSBOW aim-and-fire, NO lunge (she stays planted on the right)
Rendered f908/925/942 (fb2png) show Marle **raising a crossbow side-on** and firing a **bolt** leftward at GATO;
GATO becomes a white silhouette (flash) at f942. Her pal6 cluster (4 cells 0x020/022/024/026, home anchor
**164,63**) walks a **4-pose OAM ladder** (`--monitor`, anchor = min-corner; dx,dy rel home):

| pose | frames | anchor (dx,dy) | note |
|---|---|---|---|
| **MA1 aim** | f901-913 | (−10,+16) | steps ~10 px toward GATO, crossbow lowering |
| **MA2 raise** | f914-923 | (−9,0) | crossbow leveled |
| **MA3 fire** | f924-940 | (−10,+16) | release; bolt + impact spark appear |
| **MA4 recover** | f941+ | (−1,+16) | returns toward home |

The anchor moves **≤10 px left** and back — a small step, **NOT a lunge** (contrast Crono's A2 approach→strike
translation). The tile NUMBERS stay 0x20/22/24/26; the **content STREAMS per pose** (`vram` diff f908↔f930,
f925↔f930: cells 0x20-0x27,0x30-0x37 change) — the crossbow + the leaving bolt are drawn frame-by-frame **in
Marle's own streamed sprite** (same mechanism/boundary as Crono's sword-arc A2.2), so **the bolt is NOT a
separate flying OBJ**.

## MA.3 — the dedicated effect OBJ = a pal0 spark in TWO roles (muzzle + impact ring)
Beyond Marle's streamed sprite, ONE **pal0** OBJ appears (OAM-shadow, tiles 0x100-0x12F, prio3):
- **MUZZLE spark** tile **0x12E** — f922-927, flickering (142,55)↔(168,87) at the bow/impact line.
- **IMPACT RING** tiles **0x108 → 0x10C → 0x10E** — f930-935, fixed at **(142,55)** = GATO's upper-right body
  (the hit point). A small tile-animated expanding ring, **not** a screen-crossing arrow.

So the "projectile" render layer = {Marle's streamed crossbow-fire sprite} + {this one pal0 spark, muzzle then
impact}. Its OAM position is essentially **stationary at the impact point** (no long trajectory to pin).

## MA.4 — strike + damage (standard P1/P3 feedback)
- **Strike / hurt flash:** GATO's body (tiles 0x68-0x8E) swaps to **pal4 white** (P3) **f939-947 (~9 f)** — strike
  lands **f939** = **+67 f after dispatch** (f872) / **+34 f after aim-start** (f905).
- **Damage number "40"** (tiles 0x194='4', 0x190='0', P1 font) rises over GATO **f959+** (the P1 arc); confirms
  the PC_RANGED formula output.

## MA.5 — bake + VERIFY (byte-exact)
`tools/_gen_battlemarleatk.py --verify --emit` (self-contained: peeks the rep frames via the mash-prefix
btnscript; **peek timeline = monitor+2**, the `battle_hud_render` skew, handled per keyframe). Bakes: 4 pose
keyframes (`ctMarleAtkKf` = pal6 assembly + streamed OBJ tiles 0x20-0x37 + pal6), the 79-row trajectory
`ctMarleAtkTraj` (f872-950, `(dx,dy,poseId)`, strike f939), and the spark (`ctMarleSparkKf` 4 tile-variants +
positions). Reconstructs each pose from the baked tiles, composites all combatants by OAM index, pixel-diffs
Marle's pixels vs the oracle fb (fb row = OAM y + 8):
```
MA1_aim f905 439/439 · MA2_raise f918 458/458 · MA3_fire f930 443/443 · MA4_recover f948 419/419
  -> MARLE 1759/1759 byte-exact (100%)
spark E0 0x12E 15/15 · E1 0x108 148/148 · E2 0x10C 132/132 · E3 0x10E 60/60
  -> SPARK 355/355 byte-exact (100%)
```

## MA.6 — Open questions (Marle attack)
- **Within-pose per-frame tile stream** (the crossbow-draw / bolt-leave pixels) — the C1/CC anim engine
  (`$0520+n*0xC`, `FD:E170`); rep-frame pose tiles baked only, same boundary as Crono A2.2 / GATO A4.
- **Bolt travel:** the OAM effect layer is only the muzzle spark + the impact ring (both near the target); any
  visible mid-flight "beam" is Marle's streamed sprite, not a tracked OBJ — no separate trajectory to pin.
- **Multi-enemy targeting / other PCs' ranged attacks** — only Marle-vs-GATO captured; the aim→fire→spark
  ladder + the pal0-spark mechanism generalize but need per-target captures.
- **Anim-script CD/7164 per-opcode decode** (the effect track CD/960E timing) — the standing anim-engine boundary.
