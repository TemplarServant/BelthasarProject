# BATTLE PRESENTATION POLISH II — four presentation truths DERIVED (GATO hurt / flurry cadence+facing / song-rise gate / Crono crit ladder)

Derives, from the ROM disasm + LakeSnes oracle **only**, four battle-presentation truths behind the user's
reports, at HEAD **cdeb11d** (anim-engine stages 1-3 live — GATO + PCs render through the E3/E4 engine).
`src/`+git untouched — this notes file is the only write. Method: oracle `ct_ae.exe` (= HEAD `build/ct.exe`,
copied to scratch) `--monitor` (per-frame RAM) + `--peek` (rendered fb/OAM/VRAM); our engine read from
`src/battle.c` / `src/field.c` / `src/animeng_e3_*.h`. The pose engine is monitored via **`$9907[cmb]`** = the
builder's per-combatant assembly-index cache (`battle_animeng_builder.md` §2: `C1/1C4A` `LDA $967B / CMP
$9907,X / STA $9907,X`): **GATO = cmb 6 → `$990D`**, **Crono = cmb 0 → `$9907`**, Marle = cmb 1 → `$9908`;
`$9912` is the rebuild counter, not a cmb. Every assembly index below is that live builder value (the same
E3/DC index our stage-1/3 engine consumes), cross-checked against `--peek` OAM where noted.

## ⭐ HEADLINE

| # | user report | root truth (oracle) | our defect |
|---|---|---|---|
| **1** | GATO "when taking damage it is not right" | GATO HAS a distinct HURT pose = **E3 assembly `0x0C`** (arms-up recoil, anchor raised ~12px, its own tiles 0x100-0x126) — held ~15f at the incoming strike + pal4 flash; **live foot CONSTANT (no knockback)** | we NEVER select 0x0C (idle claw-swap keeps cycling) + add an invented foot-knockback offset |
| **2** | flurry "fast and cut short" / directional | flurry = **fist-out↔retract, 8f each, 6 cycles** (rel +26→+117); FACING-DISPATCHED: front `0x16↔0x17`, right `0x1E↔0x1F` | our front flurry is `0x16↔`**`0x15`** at **3+4f × 7** (49f, too fast/short); directional attack-B shows rear→strike, **no flurry** |
| **3** | song "he always moves UP so the spotlight does not cut" | enemy action-dispatch is **GATED on the mover being IDLE** → GATO **always sings at Y=85** (the fixed beam aligns) | our enemy dispatch has NO mover-idle guard → the song CAN dispatch **mid-return** → wrong height → fixed beam clips |
| **4** | crit "cut short; two actions with the sword" | crit `0x01` has its OWN 8-pose ladder incl. a **second slash (0x53/0x54)**; flashes land on 0x51 (rel+50) & 0x54 (rel+91) | flash timing already fixed (STRIKE=50/91 shipped); the **POSE** ladder still reuses the normal 6-pose swing → the 2nd slash (0x52/0x53/0x54) is never shown |

---

# ITEM 1 — GATO's HURT pose: E3 assembly `0x0C` (DERIVED — prior "no hurt pose" was a mis-sample)

## 1.1 CANONICAL — a distinct hurt/recoil assembly, byte-exact
Clean single normal Crono hit on an IDLE GATO (`crono_normal_ready.state`, crit rate 0, drive
`100:5,0:6 ×4,0:120`; Crono dispatched f26):

```
$990D (GATO assembly) : ...idle 01 held (frozen during the PC action)... -> 0x0C @f80 -> 0x04 @f95
$1D12/$1D29 (GATO live foot) : (136,111) CONSTANT the whole hit (NO knockback)
$6130 (GATO curHP) : 76 -> 59 (-17) at span-end f159 (HP applies at span-end, NOT the strike)
```

- **The hurt pose = `$990D`(=`$967B`) = `0x0C`**, held **f80→f94 (~15f)**, onset = the incoming PC strike beat
  (Crono's strike pose 0x51/0x52 lands f75-86), then reverts to idle 0x04.
- **Byte-exact OAM confirmation** (`--peek` at the flinch): GATO's 12 cells are **pal4 (white flash)** with the
  layout `080@(24,0) 08E@(0,16) 06E@(16,16) 06C@(32,16) 088@(47,16) 08C@(0,32) 06A@(16,32) 068@(32,32)
  08A@(47,32) 086@(8,48) 084@(24,48) 082@(39,48)` at anchor (106,45). Decoding `src/data/animeng_e3_gato.h` idx
  **0x0C** (dx/dy `{2,-34}{-14,-34}{2,-50}{-14,-50}{-6,-66}{9,-18}{-6,-18}{-22,-18}{17,-50}{17,-34}{-30,-34}
  {-30,-50}`, tile order 0x68…0x8E) reproduces that anchor-relative layout **cell-for-cell (12/12)**. So the
  flinch IS E3 idx 0x0C — a DISTINCT pose: cell `080@dy-66` reaches 66px above the foot (idle rest reaches
  only -56), and its cell-words reference the **0x100-0x126 tile band** (vs idle's 0x00-0x30) = its own graphics.
- **No live-foot knockback.** `$1D12/$1D29` = (136,111) never changes. The visible "recoil" is entirely the
  0x0C pose's own raised anchor (idle anchor y57 → hurt anchor y45 = the body jerks up ~12px). There is NO
  translation of the foot away from the attacker.
- **pal4 white flash** (~3-4f) overlaps the onset (`--peek` f80/f82 = pal4 on the idle layout, f84 = pal4 on the
  0x0C layout, f88+ = pal1 with 0x0C continuing to ~f94). So the flash is the first ~4f; the pose 0x0C outlasts
  it (~15f). Corpus §4.2 pinned the pal4 swap at 3f.
- **Idle is HELD during the PC action.** While the PC action freezes ($A013=1), GATO's idle claw-swap does NOT
  cycle — `$990D` sits at one frame (01) from dispatch (f26) until the 0x0C flinch (f80), then holds 0x04 to
  span-end. Only the strike-flinch interrupts the held idle.
- **Facing-invariant.** A Marle (right, facing 3) hit on GATO (`gato_crono_dead.state`) also produced
  `$990D=0x0C` — the hurt pose is the same `0x0C` regardless of facing (unlike the directional attack/flurry).
  In the canonical GATO fight GATO idles facing 1 (front), so hits land facing-front → 0x0C.
- **Mid-walk hits flinch too.** In a mash, a PC strike landing while GATO was mode-6 (return-walking) also drove
  `$990D=0x0C` at the strike (mash f689), then back to his walk frame. So the flinch fires at rest AND mid-walk.

## 1.2 OURS + FIX SPEC
`battleGatoRender` (`field.c` ~2472): during hurt our engine draws `ctAnimengPoseIdx(fac, h->frames)` — the
**idle claw-swap with an ADVANCING clock** (so the swap keeps cycling) — plus `palw = ctBattleFxFlashPal`
(pal4, correct) and a foot offset `if (h->fxKnockTarget==6) { gdx += fxKnockDX; gdy += fxKnockDY; }` (an
**invented** knockback — the oracle foot is constant). It NEVER selects 0x0C (which IS baked in
`animeng_e3_gato.h`, unused).

**FIX:** while GATO is the struck target (the same window that arms the pal4 flash — the strike beat for ~15f):
1. Render **E3 assembly `0x0C`** (not `ctAnimengPoseIdx`) at the STATIONARY live foot — the pose supplies the
   ~12px recoil; hold it ~15f then revert to the idle tick.
2. **Drop the `fxKnockDX/DY` foot offset for GATO** (fxKnockTarget==6) — the oracle does not translate his foot;
   the pose is the recoil.
3. Keep the pal4 flash (~3f) as today. ⚠ verify `0x0C`'s cell-word tiles (0x100-0x126) are present in the
   baked sheet `animeng_sheet_gato.h` (they are outside idle/attack's band) — else the pose renders with wrong
   pixels. The dx/dy are already byte-exact in `animeng_e3_gato.h` idx 0x0C.

---

# ITEM 2 — the FLURRY (attack-B `$9877=0x34`): cadence + FACING law (DERIVED)

## 2.1 CANONICAL front cadence — 0x16↔0x17, 8f each, 6 cycles (NOT 0x16/0x15 at 3+4f×7)
Clean attack-B ladder (mash on `gato_press_action`, dispatch f772, facing 1). `$990D` timeline, rel to dispatch:

```
windup (idle rest 0x04) : rel 0 .. +25   (~26f)
FLURRY  0x16 (fist-out) : +26  +42  +58  +74  +90  +106     ] 6x, 8f each,
        0x17 (retract)  : +34  +50  +66  +82  +98  +114      ] alternating (rel +26 .. +117 ~= 91f)
idle rest 0x04          : +117 .. span-end +226
```

So the flurry proper = **6 cycles of `0x16`(8f) ↔ `0x17`(8f)** over rel **+26 → +117** (~91f), bracketed by
idle-rest 0x04. `0x16` = fist-out (E3 cell-words carry the claw-thrust tiles 0x49-0x52), `0x17` = retract
(claw-back tiles 0x53-0x5A); both share the FRONT rear-up OAM (idx 0x15/0x16/0x17 have identical dx/dy, differ
only in streamed tile content — the "front strike = tile-stream on a fixed OAM", corpus §1.2). This CONTRADICTS
the CD/7B8F pause-sum (`06 05` pause3 + `06 03` pause4 = 7f/cycle ×7): the live dwell is **8f** per pose (the
`06 aa` playback adds ~4-5f to each pause) and the live count is **6** cycles, not 7. Our 49f/7× flurry is the
user's "fast and cut short".

## 2.2 CANONICAL facing law — the flurry pose PAIR is FACING-DISPATCHED
attack-B (`01 01 06 00`) has NO movement program → GATO does NOT walk, but he faces his current target, and the
flurry pose pair follows that facing:
- **FRONT (facing 1, Crono target):** `0x16 ↔ 0x17` (§2.1).
- **RIGHT (facing 3, Marle target):** forced via `gato_crono_dead.state` (Crono dead → attack-B targets Marle,
  facing 3). `$990D` timeline: windup `0x0A` (right idle) → **`0x1E`(fist) ↔ `0x1F`(retract), 8f each, 6
  cycles** (rel +26 → +117), identical cadence to the front. So the E3 **RIGHT block `0x1C-0x1F`** DOES contain
  the flurry pair: `0x1E` = right fist-out, `0x1F` = right retract (`0x1C/0x1D` are the right rear-up windup for
  the walked directional attack-A). Confirming: a patch of `$96DE[3]=3` on `gato_press_action` was overwritten
  back to 1 by GATO's rest re-facing (he always settles facing the party), so the ONLY way to a right flurry is
  a target to his right — exactly the Marle case.

So the facing law: **flurry pose pair = {fist-out, retract} selected by GATO's facing** (front 0x16/0x17, right
0x1E/0x1F; left/up pairs exist in the block-table by symmetry but are non-canonical for the GATO formation — no
PC sits left/above). GATO faces his target for attack-B even though he does not walk to it.

## 2.3 OURS + FIX SPEC
`battleGatoRender` (`field.c` ~2439): the FRONT branch (fac==1, `$9877==0x34`) renders `FIST=0x16` /
`RETRACT=`**`0x15`** (wrong — 0x15 is the rear-up WINDUP used by attack-A, not the retract; the retract is
**0x17**) on a `cyc = rel/7; flashOn = (rel-cyc*7)<3` schedule = **3+4f × 7 cycles / 49f** (wrong cadence). The
DIRECTIONAL branch (fac!=1) does a single rear→strike (`ctAnimengGatoAtk[fac]`) with **no `$9877==0x34` check**,
so a right-facing attack-B renders attack-A's rear→strike, **not a flurry**.

**FIX:**
1. FRONT flurry: `0x16 ↔ 0x17` (fix the retract from 0x15 → **0x17**), **8f per pose, 6 cycles**, over rel +26
   → +117 (windup idle 0x04 rel 0→+26). Keep the pal4 fist-flash on the 0x16/0x1E frames (the 69/6A CD op — the
   sanctioned white-swap representation).
2. DIRECTIONAL flurry: in the `fac!=1` branch, when `$9877==0x34`, use the facing's flurry pair (RIGHT →
   **`0x1E ↔ 0x1F`**) at the same 8f×6 cadence, NOT the rear→strike. (Front/right are the in-fight facings.)
Damage staging is UNCHANGED (single strike + single popup fire from `battle.c`; the flurry is cosmetic).

---

# ITEM 3 — the SONG rise is a DISPATCH GATE on the mover (DERIVED)

## 3.1 CANONICAL — the song never dispatches mid-rise; enemy dispatch requires the mover IDLE
`gato_moving_lake.state` (GATO mid-return from attack-A, song queued). Watching GATO's ATB (`$AFB1` = `$AFAB`
seat 6), mover Y (`$1D29`), mode (`$974A[3]`), and the action gate (`$9877`/`$A013`):

```
f0    ATB=0x2C  Y=125  mode=06  (returning; ATB charging)
f109  ATB=0x00  Y=102  mode=06  <-- ATB EXPIRES (action-ready) but GATO is STILL RETURNING (mover active)
f114  ATB=0x66  Y=101           <-- ATB RELOADS: the action did NOT dispatch; the ready turn is deferred
f193  ATB~0x47  Y=85            <-- mover reaches rest height (Y=85) and holds (still mode 06, stationary)
f365  ATB=0x00  Y=85            <-- ATB expires AGAIN, now mover idle
f373  $9877=0x10 $A013=1 Y=85   <-- SONG dispatches (at rest height)
```

**The song did NOT dispatch when the ATB first expired (f109) because GATO's return mover was still active
(Y=102, mid-rise).** The ATB reloaded and drained a second time; the song dispatched only at the next expiry
(f365), by which point the mover had long settled at Y=85 (since f193). So the enemy action-dispatch is
effectively **gated on the mover being idle** — the rise (mode 6, `$A013=0`, ATB ticking) always completes
before the song's `$A013=1` fires. (The return runs at `$A013=0`, `battle_timing_audit.md` §1.A: "enemy walk
NOT frozen".)

## 3.2 The Y=85 invariant + the fixed beam (why "they do not cut")
GATO's Y at song-dispatch, measured across songs (X varies with the preceding attack, **Y is invariant ≈85**):

| song | GATO pos at dispatch | source |
|---|---|---|
| gato_moving_lake | (128, **85**) | this session |
| HOLD gato_press_action | (135, **85**) | this session (song f1151) |
| (polish I) | (135,85) / (123,**87**) | `battle_gato_polish.md` §2.1 |

The **spotlight beam is a FIXED SCREEN graphic** — BG3 scrolled to (992,123), apex ~screen y113, base ~y8,
centred x127 (`battle_song_spotlight.md` §B.1/A.3) — **independent of GATO's Y**. GATO's song pose reaches
-56px above the foot (head at y85-56 ≈ y29), sitting inside the fixed cone (y8..y113). If GATO sang mid-rise
(e.g. Y=118, his approach height) his body would be ~33px BELOW the calibrated cone → the fixed beam would not
cover him / the rising notes → the "cut" the user describes. The mover-idle dispatch gate (§3.1) is what keeps
GATO at the calibrated Y=85 so the fixed beam always aligns. **"he always moves UP" = the return-rise always
completes before the song, enforced by the dispatch gate.**

## 3.3 OURS + FIX SPEC
`battle.c` L2131-2134 (enemy turn dispatch):
```c
for (int i = 3; i < 11 && !h->actActive; i++)
  if (battleAlive(s, i) && s[0xAFAB + i] == 0 && h->mvPendAtk != i)   // <-- ONLY a mid-APPROACH guard
    battleOpenAction(h, i);
```
The only guard is `mvPendAtk != i` (skip an enemy mid-**approach**, whose dispatch is pending on arrival). There
is **NO guard for an active RETURN mover** (`h->mvActive[i-3]` / `mvMode==6`). The song (`02 10 05 00`, mode 0)
dispatches immediately in `battleOpenAction` (it has no approach to defer). So when GATO's ATB reaches 0 while
his return mover is still active, our engine dispatches the song **mid-return** — GATO sings at the wrong height
and the fixed beam clips. (Structural defect; whether it manifests depends on our return-mover duration vs the
ATB phase, but the gate is missing.)

**FIX:** add the mover-idle condition to the enemy dispatch — do NOT dispatch while the enemy's own mover is
active:
```c
if (... && s[0xAFAB+i]==0 && h->mvPendAtk != i && !h->mvActive[i-3])   // defer until the return settles
```
To match the oracle's pacing (§3.1), a blocked dispatch should **reload the ATB** (re-arm), so the action fires
on the next ATB-expiry once the mover is idle — not immediately at mover-settle. (Minimal correctness: just the
`!mvActive` guard; full fidelity: the re-arm.) This ties to `battle_gato_polish.md` items 1-2 (the return-settle
re-face, already shipped 3954c02) — that fixed WHERE the return ends; THIS fixes WHEN the song may fire relative
to it.

---

# ITEM 4 — Crono's CRIT pose ladder (`$9877=0x01`, span 177f) — the DOUBLE slash (DERIVED)

## 4.1 CANONICAL — the crit's own 8-pose ladder (2 slashes)
`crono_crit_ready.state` (Crono crit rate 0xFF → deterministic crit), rapid-A drive; crit anim dispatched f26
(`$9877=0x01`, `$A013=1`, `$AE9B=0x80`), span-end f203 (**177f**). `$9907` (Crono cmb0) ladder, rel to dispatch:

| pose (`$9907[0]`) | rel window | role |
|---|---|---|
| 0xD5 | +23 .. +32 | leap / windup |
| 0x50 | +33 .. +47 | **first slash** raise |
| **0x51** | +48 .. +61 | **first slash STRIKE**  ← **Flash A rel+50** |
| 0x52 | +62 .. +73 | first slash follow-through |
| 0x53 | +74 .. +88 | **second slash** raise |
| **0x54** | +89 .. +120 | **second slash STRIKE** ← **Flash B rel+91** (held ~32f) |
| 0x8B | +121 .. +131 | recover |
| 0x47 | +132 .. +148 | recover |
| 0xC6 | +149 .. +177 | idle bob (return) |

- **Two slashes**: FIRST = 0x50→0x51→0x52, SECOND = 0x53→0x54. The `0x52/0x53/0x54` are the double-slash's poses
  the NORMAL swing lacks. (Normal 0x00 swing ladder, measured on `crono_normal_ready`: `0x20 0x41 0x50 0x51
  0x52 0x8B 0x47` — the crit REPLACES the 0x20/0x41 windup with `0xD5` and INSERTS the second slash `0x53/0x54`.)
- **Flash sync (already shipped, confirmed)**: Flash A (rel+50) lands on pose **0x51** onset (+48) = the first
  slash contact; Flash B (rel+91) lands on pose **0x54** onset (+89) = the second slash contact
  (`battle_timing_audit.md` §2). `--peek` fb-mean confirms two washes: ~f78 (Flash A, mean 120 over base 80,
  Crono pose = assembly **0x51** byte-exact `08@(12,0) 0E@(0,14) 0C@(12,16) 0A@(12,32)`) and ~f128 (Flash B,
  Crono a DISTINCT higher pose anchor(126,79) vs the first slash's anchor(115,87) — "Crono has leapt beside
  GATO", §2.B). Fade A ~7f, B ~15f (§2.E).

## 4.2 OURS + FIX SPEC
The crit **FLASH** is already correct at HEAD: `CT_CRONO_CRIT_STRIKE=50` (Flash A) + `CT_CRONO_CRIT_STRIKE2=91`
(Flash B) + `critFlash2Done` (battle.c:916/920 — the `battle_timing_audit.md` §2 fix landed). But the crit
**POSE** ladder is still WRONG: `battleAttackRender` (`field.c` ~2732) always blits
`ctAnimengCronoAtkByPose[h->fxAtkPose]` = the normal 6-pose swing `{0x20,0x41,0x50,0x51,0x8B,0x47}` (driven by
`ctCronoAttackTraj`, a 91f table), for BOTH the normal AND the crit. So the crit renders the normal swing
stretched over 177f and NEVER shows `0x52/0x53/0x54` (the second slash) or the `0xD5` windup → "cut short", the
"two actions with the sword" collapse to one. (The flashes fire at rel+50/+91 but land on the WRONG poses because
the second-slash pose isn't rendered.)

**FIX:** give the crit (`h->actCrit` / `$9877=0x01`) its OWN pose ladder driven by rel-to-dispatch, per §4.1:
```
rel<23:idle 0xC7 | +23:0xD5 | +33:0x50 | +48:0x51 | +62:0x52 | +74:0x53 | +89:0x54 | +121:0x8B | +132:0x47 | +149:idle 0xC6
```
Then Flash A (rel+50) automatically lands on 0x51 (first slash) and Flash B (rel+91) on 0x54 (second slash) —
byte-exact sync. **Bake the 3 missing assemblies** into `ctAnimengE3Crono` (`animeng_e3_pc.h`): **`0xD5`,
`0x53`, `0x54`** — `0x50/0x51/0x52/0x8B/0x47/0xC6/0xC7` are ALREADY baked (the generator `tools/_gen_animeng_pc.py`
reads them from Crono base `DC/27D0 + idx×0x28`; add 0xD5/0x53/0x54 to its index list). The PC engine (stage 3)
then renders the pinned ladder directly.
(Minor, out of scope: the NORMAL swing also omits 0x52 from `ctAnimengCronoAtkByPose` — the live normal ladder
is `0x20 0x41 0x50 0x51 0x52 0x8B 0x47`, 7 poses.)

---

# OPEN / refinements
- **Item 1 hurt-window duration** measured ~15f (one clean capture); whether it is a fixed 15f or hit-derived
  not swept. The pal4-flash sub-window (~3-4f) vs the 0x0C pose window (~15f) micro-timing is peek-offset-blurred
  (`--peek` ≠ `--monitor`); the pose window from `--monitor` (f80→94) is authoritative.
- **Item 1 hurt tiles** — 0x0C's cell-words reference the 0x100-0x126 tile band; confirm `animeng_sheet_gato.h`
  carries them (outside the idle/attack band) before trusting the pose's PIXELS (its dx/dy are already exact).
- **Item 2 left/up flurry pairs** — front (0x16/0x17) + right (0x1E/0x1F) captured; left/up pairs live in the
  block-table (LEFT ~0x18-0x1B, UP ~0x20-0x22) by symmetry but are non-canonical for the GATO formation
  (no PC left/above), un-captured.
- **Item 3 dispatch-gate mechanism** — the OBSERVABLE (no dispatch while the mover is active; the ready ATB
  re-arms) is proven; the exact ROM site that defers the ready enemy turn on an active mover is not byte-traced
  (the AI-interpreter / `$A013` arm boundary). The user-visible invariant (Y=85, beam aligned) is fully pinned.
- **Item 4 second-slash dx/dy** — 0x53/0x54 confirmed distinct via `--peek` (2nd-slash anchor 126,79) but their
  exact dx/dy come from ROM `DC/27D0` (the generator extracts them); not hand-transcribed here.

# Scratchpad artifacts (NOT committed)
Dir `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`: oracle `ct_ae.exe` (+SDL2.dll), ROM, states
(`gato_press_action`, `crono_normal_ready`, `crono_crit_ready`, `gato_moving_lake`, `gato_crono_dead`,
`gato_faceR` = a $96DE[3]=3 patch). Helpers: `parse_mon.py` (monitor→columns), `decode_gato2.py` (OAM cell
decode via `tools/ctlib.oam`), `patch_state.py` (WRAM patch via `tools/ctlib.lake`), `fbmean.py` (fb brightness).
Captures: `hit2.txt`/`hit1.txt` (item1 clean hit), `gflinch/gidle/gpost.raw.oam` (item1 OAM),
`flurry_ladder.txt`/`flurry_marle.txt` (item2 front/right), `song_gate.txt`/`hold_song.txt` (item3),
`crit_raw.txt`/`cf*.raw` (item4). Repro (all from the scratch dir, ROM in CWD):
```
# item1: ct_ae --monitor "990D:1,1D12:1,1D29:1,6130:2,A013:1,9877:1" --from crono_normal_ready.state --btnscript "100:5,0:6,100:5,0:6,100:5,0:6,100:5,0:6,0:120" --diff
# item2 front: ct_ae --monitor "990D:1,9877:1,A013:1,96DE:8" --from gato_press_action.state --input <mash 100:20,0:40 xN> --diff  (attack-B @f772)
# item2 right: same --from gato_crono_dead.state  (attack-B on Marle @f374 -> 0x1E/0x1F)
# item3: ct_ae --monitor "AFB1:1,9877:1,A013:1,1D29:1,974A:8" --from gato_moving_lake.state --btnscript "0:400" --diff
# item4: ct_ae --monitor "9907:12,9877:1,A013:1,AE9B:1" --from crono_crit_ready.state --btnscript "100:5,0:6,100:5,0:6,100:5,0:6,100:5,0:6,0:230" --diff  (dispatch f26, span 177)
```
