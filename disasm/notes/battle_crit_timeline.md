# CRONO'S CRIT — the full canonical timeline (LEAP → slash 1 → slash 2 → return), byte-derived

User defect (translated from Portuguese): *"when landing a critical blow, he delivers the second attack after already having returned to his
position."* — on a critical hit Crono delivers the second strike **after** he has already returned home.

Mandate note: the user's words are a POINTER. What the ROM actually says is derived below from the
**CE choreography scripts** (bank-CE bytes + the live per-role interpreter IP `$5DBD`) and from two
independent oracle crit captures at **different leap distances**. Every number is an oracle frame delta
or a ROM byte at a cited address.

---

## HEADLINE

Crono's crit ($9877=0x01) is **not** driven by the CD table entry `CD/5FF0[1] = CD/6E4A` — the live
role-IP array `$5DBD` proves the whole 176-frame span runs the **bank-CE crit base script `CE/03CC`**
(role-0 track `CE/03DC`). That track is: `1E 2B` (super → `CE/D795` = **the LEAP block**, `98 02 28`)
· `03 31` (**PLAY slash-1 anim 0x31**) · `36` · `03 32` (**PLAY slash-2 anim 0x32**) · `20 0A` ·
`1E 2A` (super → `CE/D776` = the SHARED strike/return block: `98 02 19` home slide + `1B 19` snap).
The **double slash is TWO separate play-anim ops**, and — decisively — **everything after the leap is
anchored to the LEAP ARRIVAL**, not to dispatch: with a 3-frame-longer leap every subsequent op shifts
by exactly +3 (two captures). Writing `A` for the arrival frame (rel-to-dispatch), the canon is
**slash-2 STRIKE = A+60, return travel = A+94, snap home = R+8, span = A+146**. Our engine keys the
whole crit off *dispatch* with fixed constants (`CT_CRONO_CRIT_RETURN=113`, `CT_SPAN_PC_CRIT=177`), so
the return fires 8-14 f early and the snap 15-19 f early — **the slash-2 STRIKE pose is still on screen
while Crono slides home, and the recover pose only plays once he is back**. That is the user's report,
and the error grows linearly with the leap distance.

---

## 1 — SETUP / REPRO

State: `saves/battle_re/bat3crit.state` (= `bat3_lake` + Crono crit rate `$5E72`=0xFF → deterministic
crit; forest formation, Crono staged (160,71), enemies cmb3 (120,63) / cmb4 (88,95) / cmb5 (168,127),
each 14 HP). Probe copy of `build/ct.exe` (never probe with the build binary).

```
# CAPTURE 1 (near victim cmb3):   dispatch f26, leap arrival rel 30
ct_probe --monitor "1D0C:11,1D23:11,9907:3,967B:3,968C:3,9689:3,96DB:3,A013:1,A3D1:2,5FB0:2,A15B:1,A517:1,9877:1" \
         --from saves/battle_re/bat3crit.state --input "(100:8,0:8)x16" --frames 260 --diff
ct_probe --monitor "5DBD:4,A013:1"  --from ... --input "(100:8,0:8)x16" --frames 230 --diff   # role-0 IP
ct_probe --monitor "5DC9:4,5DBD:4,A013:1,A3D1:1" --from ... --frames 215 --diff               # role-3 IP

# CAPTURE 2 (far victim cmb5, one cursor step LEFT):  dispatch f47, leap arrival rel 33
ct_probe --monitor "1D0C:6,1D23:6,9907:1,A013:1,5FB0:2,6030:2,60B0:2,5DBD:4,A3D1:2" \
         --from saves/battle_re/bat3crit.state --input "100:8,0:8,40:8,0:8,100:8,0:400" --frames 260 --diff

# WHOLE-SCREEN WASH sweep (fb mean brightness, capture frame = btnscript total):
ct_probe --peek saves/battle_re/bat3crit.state sw.raw --btnscript "100:8,0:8,100:8,0:<f-24>"
```

---

## 2 — THE SCRIPT (ROM bytes; the CD table is a decoy)

### 2.1 `$5DBD` = the per-ROLE interpreter IP, stride 4 (`battle_ai_interp.md` §11)

`--monitor "5DBD:48"` at the crit dispatch (f26) returns
`DC03CE00 00000000 00000000 F203CE00 …(+0x20)… 0004CE00` → **role 0 = CE/03DC, role 3 = CE/03F2,
role 8 = CE/0400**. The IP stays inside bank **CE** for the entire span (f26 → f202). `CD/6E4A` is
**never loaded** — the `$9877`-indexed CD table is the enemy-side/legacy table (§11's law confirmed for
the crit too). *This corrects `battle_ai_interp.md` §13b, which decoded CD/6E4A as "the crit
choreography".*

### 2.2 The crit BASE script `CE/03CC` (`--read rom:CE03C0+112`)

```
CE/03CC  90 80 90 80                      ; two role masks (bits 15,12,7 -> roles 0,3,8), §11 bit->role law
CE/03D0  DC03 F203 0004  DC03 F203 0004   ; 6 track pointers
CE/03DC  ---- ROLE 0 (attacker) ----
   03DC  1E 2B      SUPER 0x2B  -> CE/0000[0x2B*2] = CE/D795   (the LEAP block)
   03DE  36         sync
   03DF  7C
   03E0  72 0D      face target
   03E2  03 31      PLAY anim 0x31            <-- SLASH 1
   03E4  36         sync (wait out anim 0x31)
   03E5  35 7D
   03E7  03 32      PLAY anim 0x32            <-- SLASH 2
   03E9  20 0A      pause 10
   03EB  1E 2A      SUPER 0x2A  -> CE/D776    (the SHARED strike/RETURN block)
   03ED  24 04      wait
   03EF  2E
   03F0  01         END (parks here until the span closes)
CE/03F2  ---- ROLE 3 (victim) ----
   03F2  24 01 | 20 0F | 1E 05 (-> CE/D47D, BEAT-A block) | 23 01 | 20 0F |
         1E 00 (-> CE/D428, BEAT-B block) | 36 | 00
```
Op boundaries are not guessed — every one is pinned by an observed `$5DBD` sample
(03DC f26 · 03E2 f58 · 03E4 f99 · 03E7 f100 · 03E9 f130 · 03EB f131 · 03ED f176 · 03F0 f199).

### 2.3 The super table `CE/0000[ss*2]` (`--read rom:CE0000+24`, `rom:CE0050+16`)

| ss | target | role | what |
|---|---|---|---|
| 0x00 | **CE/D428** | 3 | BEAT-B victim block (flash + hurt pose + **`50` ShowDamage**) |
| 0x05 | **CE/D47D** | 3 | BEAT-A victim block (flash + hurt pose, **no `50`**) |
| 0x0A | CE/D4C3 | 0 | the NORMAL melee engage block (`18 03` grid walk) — *not* used by the crit |
| 0x2A | **CE/D776** | 0 | strike/RETURN block — **shared with the normal attack** |
| 0x2B | **CE/D795** | 0 | the CRIT LEAP block |

### 2.4 `CE/D795` — the LEAP block (`--read rom:CED776+48`)

```
CE/D795  0C            speed
CE/D796  02 26         PLAY anim 0x26   (the windup / leap pose)     <- IP here at rel 21 (BOTH captures)
CE/D798  D0
CE/D799  98 02 28      MOVE (op-98 trig slide) to resolver mode 0x28  <- PARKS for the whole leap
CE/D79C  D1                                                          <- reached on the ARRIVAL frame
CE/D79D  1F            POP -> back to CE/03E2
```

### 2.5 `CE/D776` — the strike/RETURN block (identical to the normal attack's)

```
CE/D776  76 03 19 | 08 | A8 01 | 26 0E 02 | 0C | D0 |
CE/D781  98 02 19      MOVE to resolver mode 0x19 = PC seat-0 STAGED HOME   <-- THE RETURN
CE/D784  26 0E 01 | 20 03 | D1 |
CE/D78A  1B 19         TELEPORT/SNAP to the staged home                     <-- THE SNAP
CE/D78C  06 0E | 20 05 | 72 0D | 06 03 |
CE/D794  1F            POP -> CE/03ED
```

### 2.6 The victim blocks — ONE damage, TWO visual beats

```
CE/D47D (BEAT A)   69 00 | 20 02 | 06 05 | 20 02 | 6A | 03 05 | 06 03 | 1F
CE/D428 (BEAT B)   72 0A | 69 00 | 20 02 | 06 05 | 20 02 | 6A | 03 05 | 06 03 | 50 | 1F
```
Byte-identical except beat B adds the opening `72 0A` face and the closing **`50` ShowDamage**.
`69`/`6A` = the victim pal-flash on/off pair; `06 05` = the hurt pose; `06 03` = back to idle.

---

## 3 — THE AUTHORITATIVE FRAME TABLE (two captures, different leap distances)

`A` = leap-arrival frame (rel to the `$A013` 0→1 dispatch). `R` = the last frame of the return travel.

| event | evidence (RAM / IP) | cap1 rel (A=30) | cap2 rel (A=33) | **canonical** |
|---|---|---|---|---|
| dispatch `$A013`0→1, `$9877`=01 | f26 / f47 | 0 | 0 | 0 |
| IP → `CE/D796` (`02 26` windup anim) | `$5DBD` | 21 | 21 | **rel 21** (dispatch-anchored) |
| IP → `CE/D799` (leap op armed) | `$5DBD` | 22 | 22 | **rel 22** |
| windup POSE published (D7 / D6) | `$9907[0]` | 23 | 23 | **rel 23** |
| leap FIRST travel frame | `$1D0C/$1D23` | 24 | 24 | **rel 24** |
| leap travel, 4 px/frame | (160,71)→(132,65) 7 f · (160,71)→(165,111) 10 f | 24-30 | 24-33 | 4 px/f |
| **leap ARRIVAL** (IP → `CE/D79C`) | `$5DBD` f56 / f80 | **30** | **33** | **A = 23 + dominantDist/4** |
| IP → `CE/D79D` (`1F` pop) | | 31 | 34 | A+1 |
| IP → `CE/03E2` = `03 31` PLAY slash-1 | `$5DBD` f58 / f82 | 32 | 35 | **A+2** |
| slash-1 pose 0 (5A / 55) | `$9907` f61 / f84 | 35 | 37 | **A+5** (±1 publish lattice) |
| **slash-1 STRIKE pose** (5B / 56) | `$9907` f76 / f99 | **50** | **52** | **A+20** |
| slash-1 pose 2 (5C / 57) | `$9907` f89 / f112 | 63 | 65 | **A+33** |
| IP → `CE/03E4` `36` (anim 0x31 done) | f99 / f122 | 73 | 75 | A+43 |
| IP → `CE/03E7` = `03 32` PLAY slash-2 | f100 / f123 | 74 | 76 | **A+44** |
| slash-2 pose 0 (5D / 58) | `$9907` f101 / f124 | 75 | 77 | **A+45** |
| **slash-2 STRIKE pose** (5E / 59) | `$9907` f116 / f139 | **90** | **92** | **A+60** |
| IP → `CE/03E9` `20 0A` (anim 0x32 done)| f130 / f153 | 104 | 106 | A+74 |
| victim BEAT-B block entered (`69 00`) | role-3 IP `CE/D42A` f117 | 91 | — | **A+61** |
| `50` ShowDamage → popup `$A3D1` | f138/139 · f162/163 | 113 | 116 | **A+83** |
| IP → `CE/D77A` (super 0x2A taken) | f142 / f166 | 116 | 119 | A+86 |
| `$96DB` 2→3 (HOME re-aim) | f143 | 117 | — | A+87 |
| recover POSE (8F / 8D) | `$9907` f146 / f170 | 120 | 123 | **A+90** |
| IP → `CE/D781` (`98 02 19` armed) | f148 / f172 | 122 | 125 | **A+92** |
| **RETURN travel FIRST frame** | `$1D0C/$1D23` f150 / f174 | **124** | **127** | **A+94** |
| return travel LAST frame `R` | f156 / f183 | 130 (7 f) | 136 (10 f) | R = A+93+dom/4 |
| settle POSE (4B / 49) | `$9907` f158 / f185 | 132 | 138 | **R+2** |
| **`1B 19` SNAP to exact home** | IP D78A→D78C f163→164 / f190→191 | **138** | **144** | **R+8** |
| `$96DB` 3→2 (re-aim nearest) | IP `CE/D792` f173 / f200 | 147 | 153 | **R+17** |
| idle POSE (C8 / 43) | `$9907` f175 / f202 | 149 | 155 | **R+19** |
| popup dies (`$A3D1`hi→00) | f197 | 171 | — | A+141 (= A+83+58) |
| **span end `$A013`→0 + HP APPLY** | f202 / f226; `$5FB0` 14→0 / `$60B0` 14→0 | **176** | **179** | **A+146** |

**The +3 proof (question 4).** cap2's leap is 3 frames longer than cap1's (arrival rel 33 vs 30) and
**every** op-level event after the leap shifts by exactly **+3** (`03 31` 32→35, `03 32` 74→76… popup
113→116, super-2A 116→119, return 124→127, span 176→179). Pose publications shift +2 because the PC
anim tick publishes on a 2-frame lattice (`battle_ai_interp.md` §14). ⇒ **the post-leap ladder is
ARRIVAL-anchored**, and the span is `A+146`, not a constant.

### 3.1 The two whole-screen white washes (fb mean, bat3crit, baseline 62.3)

`--peek … --btnscript "100:8,0:8,100:8,0:<f-24>"` frame sweep:

| flash | onset | peak | fade | rel | vs A |
|---|---|---|---|---|---|
| **A** | f77 (63.9) | **f78 = 105.8** | 99.1 · 92.7 · 86.7 · 79.8 · 73.8 (f79-83) → base ≈ f86 (~8 f) | 52 | **A+22** |
| **B** | f117 (62.2) | **f118 = 107.4** | 104.4 … 73.7 (f119-129) → base ≈ f133 (~15 f) | 92 | **A+62** |

Both peak **exactly 2 frames after their slash's STRIKE pose** (A+20 / A+60), consistent with
`battle_timing_audit.md` §2's rel+50 / rel+91 pins (that capture's A was ~30). Peak lerp-to-white
(mean-based) 22.6 % / 23.4 % here vs the shipped `CT_CRIT_FLASH_PEAK`=28 — **not** part of this defect.

### 3.2 Damage: ONE application, two beats (question 2)

`$5FB0` (cmb3 curHP, block `$5E2D+cmb*0x80 +3`) = 14 flat through the whole span and → **0 at f202**
(span end, `C1:ED1A`); cap2 `$60B0` 14 → 0 at f226. Neither beat writes HP. Beat A = victim flash +
hurt pose only (`CE/D47D`, no `50`); beat B = the same + the single `50` ShowDamage (`CE/D428`) whose
popup lives 58 f. So: **the crit is ONE damage application (×2 value) presented as two visual beats.**

⚠ **Correction to `battle_facing_engine.md` ADDENDUM-CRIT / `src/battle.c:3583`:** the claim "Beat B:
NOTHING here — the 14-HP Hetake died at slash A" is **wrong on this state**. The Hetake is alive until
f202, the role-3 IP runs the whole beat-B block (`CE/D42A` f117 → `6A` f125), and the beat-B wash
**does** fire (peak 107.4 @f118). The shipped `battleAlive()` gate on the beat-B effects is unfounded —
harmless today (our HP also lands at span end, so the gate never trips), but it should be dropped.

---

## 4 — WHAT OUR ENGINE DOES INSTEAD (and why the user sees the defect)

Measured, not assumed — `CT_BATTLE_CRITRATE=255 CT_BATTLE_ROUTETRACE=1 CT_BATTLE_TRACE=1`:

* GATO `build/Belthasar.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram`
  (`CT_BATTLE_MASHA=1`): dispatch f195; leap f219-f223 (rel 24-28, 20 px) → **A = 28**.
* FOREST `--scene 19 --battletest saves/battle_re/forest_pre.raw.wram`
  (`CT_BATTLE_SCRIPT="n:20,a:1,n:3,a:1,n:400"`): dispatch f198; leap f222-f228 (rel 24-30) → **A = 30**.

**The crit leap DOES arm on every dispatch path** — `battleOpenAction` (`src/battle.c:3291` reset, `:3292`
`battleComputeDamage`, `:3312` `battlePcAtkArm`), `battleDispatchScheduled` (`:3370` reset, `:3400`
compute, `:3404` arm) and `battleArrive` (`:2918`/`:2919`/`:2930`, enemy-only so the arm is a no-op) all
set `h->actCrit` **before** `battlePcAtkArm`. The hypothesised "arm bails →
`pcAtkMvCritLeap`=0 → return at rel 60" is **REFUTED for both pinned fights**: `pcroute` shows `arr=1`
and a live leap in each (GATO f223, forest f228), and the second GATO crit (f1171, GATO having walked
closer) still arms with a 1-step 4 px leap. The bail-out `src/battle.c:2360`
(`if (!arrived || (ax==hx && ay==hy)) return;`) never fired here — the sim advances a constant 4 px per
phase so it cannot step over the box, and the 64-guard covers 256 px. (It is *not* provably dead in
general: the ROM's 8 px-quantized atan2 bearing could drift laterally past the box on a very long leap.
§5.2 removes the bail-out anyway.) **The defect is a timing-anchor bug, not a missing leap.**

| our constant / site | our value | canonical | delta (GATO A=28) | delta (forest A=30) |
|---|---|---|---|---|
| `CT_CRONO_CRIT_LEAP0`=23 (`battle.c:1982`, first travel rel 24) | 24 | rel 24 | ✓ | ✓ |
| `ctAnimengCronoCrit` {23,0xD5} (`animeng_e4_pc.h:37`) | 23 | rel 23 | ✓ | ✓ |
| {33,0x50} | 33 | **A+5** | +2 late | ✓ |
| `CT_CRONO_CRIT_STRIKE`=50 (`battle.c:1978`) / {48,0x51} | 48/50 | **A+20** (wash A+22) | +2 late | ✓ |
| {62,0x52} | 62 | **A+33** | +1 | ✓ |
| {74,0x53} | 74 | **A+45** | +1 | ✓ |
| `CT_CRONO_CRIT_STRIKE2`=91 (`battle.c:1987`) / {89,0x54} | 89/91 | **A+60** (wash A+62) | +1 | ✓ |
| {121,0x8B} recover | 121 | **A+90** | **+3 late** | **+1 late** |
| **`CT_CRONO_CRIT_RETURN`=113** (`battle.c:1985`, used `:2410`) | arm 113, travel 114-118 | **arm A+92, travel A+94** | **8 f EARLY** | **10 f EARLY** |
| SNAP `1B 19` (`battle.c:2428`, fires the frame after the last step) | rel 119 | **R+8** (GATO 134, forest 138) | **15 f EARLY** | **19 f EARLY** |
| {132,0x47} settle | 132 | **R+2** (GATO 128, forest 132) | +4 | ✓ |
| {149,0xC6} idle | 149 | **R+19** (GATO 145, forest 149) | +4 | ✓ |
| **`CT_SPAN_PC_CRIT`=177** (`battle.c:2034`) | 177 | **A+146** (GATO 174, forest 176, cap2 179) | +3 | +1 |

### 4.1 The user-visible failure, proved from our own render

```
CT_BATTLE_CRITRATE=255 CT_BATTLE_MASHA=1 CT_ANIMENG_PCTRACE=1 CT_BATTLESHOT=our_crit.raw \
CT_BATTLESHOT_AT=310 build/Belthasar.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
  -> pcatk f310 pose=-1 idx=0x54 foot=(134,136) arr=(134,124) fac(a0,s0,r1) CRIT
```
f310 = rel 115 = **mid-return** (home 144, arrival 124, foot 136) and the drawn assembly is **0x54 = the
SECOND-SLASH STRIKE pose**. Because `ctAnimengCronoCritPose()` is dispatch-keyed (`src/field.c:3353`,
`h->frames - h->actStart`) the 0x54 window runs rel 89→121, while `battlePcAtkTick` starts the home
slide at rel 114 (`src/battle.c:2410-2411`). So Crono **slides home holding the second-slash strike
pose**, reaches home at rel 119, and only at rel 121 switches to the recover pose 0x8B — i.e. the second
slash visibly *finishes after he is back in position*. Canonically the recover pose is published at
A+90 = rel 118 **before** the travel begins at A+94 = rel 122; the slash pose is never on screen during
the slide.

### 4.2 Why it gets worse (the structural half)

Our return is a **dispatch**-relative constant; the canon is **arrival**-relative. The error is
`113 − (A+94) = 19 − A` frames early. It happens to be small for the two pinned fights (A=28/30), but:

* cap2 (A=33, a 40 px leap on the *same* arena) → 14 f early;
* any leap with **A ≥ 54** (≈124 px, reachable in a wide formation) puts our return start (113) **before**
  the canonical slash-2 STRIKE pose (A+60 ≥ 114) — the second attack then genuinely lands *after* Crono
  is home, exactly as reported.

Same root cause for `CT_SPAN_PC_CRIT`=177: the true span is `A+146`, so the popup/HP/act-end drift with
the leap length too (the popup already emerges from the span at `span − 63`, so fixing the span fixes it).

---

## 5 — IMPLEMENTATION SPEC (mandate-safe; every number cited above)

**No scene gates, no new heuristics.** The single change is: *re-anchor the crit ladder, the return, the
snap and the span to the LEAP ARRIVAL frame that `battlePcAtkArm/Tick` already computes.*

### 5.1 Host state (`src/battle.h`)
Add two ints next to the existing `pcAtkMvCritLeap` block:
* `int pcAtkArrRel;`  — the arrival frame **relative to `actStart`** (`A`). Set at the arm (5.2), and
  re-asserted by the tick on the frame `pcAtkMvArrived` flips to 1.
* `int pcAtkRetEndRel;` — `R`, set when the return step counter reaches 0 (the last travel frame).
  Until the return arms, `R = INT_MAX` (the R-keyed steps simply do not fire).

### 5.2 `battlePcAtkArm` (`src/battle.c` ~L2344-2367) — compute `A` deterministically at dispatch
The crit branch already simulates the trig leap and yields the arrival `(ax,ay)`. After it succeeds:
```
int dom = max(abs(ax - hx), abs(ay - hy));           // dominant-axis leap distance
h->pcAtkArrRel = CT_CRONO_CRIT_LEAP0 + dom / 4;      // = 23 + dom/4   (cap1 28px->30, cap2 40px->33)
```
Cited: cap1 dominant 28 px → 7 travel frames → A=30 (f26→f56); cap2 dominant 40 px → 10 frames → A=33
(f47→f80). Our own leap tick already spends exactly `dom/4` frames (GATO 20 px → 5 f → A=28, forest
28 px → 7 f → A=30), so the predicted `A` and the ticked arrival coincide.

**Zero-travel / already-adjacent crit (explicitly requested).** The ROM has **no bail-out**: the base
script always executes `1E 2B` → `98 02 28`, and an op-98 with zero distance retires with `$ABFE`=0
steps, so the block pops immediately and `A` collapses to its floor. Therefore replace
`src/battle.c:2360`'s `if (!arrived || (ax == hx && ay == hy)) return;` (which today leaves
`pcAtkMvActive=0`, so **no return op ever runs**) with: keep `pcAtkMvActive = 1` and
`pcAtkMvCritLeap = 1` with `arrX/arrY = hx/hy` and `pcAtkArrRel = CT_CRONO_CRIT_LEAP0` (= 23, i.e.
`dom/4 = 0`). The ladder, the `98 02 19` home slide (a 0-step no-op) and the `1B 19` snap all still run,
byte-consistent with the script. ⚠ The exact floor for `dom == 0` (23 vs 24) is **UNMEASURED** (see OPEN).

### 5.3 The pose ladder (`src/data/animeng_e4_pc.h` `ctAnimengCronoCrit`, `src/field.c:3292/3353`)
Replace the single dispatch-relative table with a **three-anchor** table (same E3 ids, same window/entry
mapping in `battleAttackRender` — that part is already correct and is re-confirmed by cap1's facing-2
instances 5A/5B/5C = win 0xC4 ent 0/1/2, 5D/5E = win 0xC8 ent 0/1, 8F = win 0x38 ent 1, 4B = win 0x38
ent 0):

| step | E3 fixture | anchor | offset | evidence |
|---|---|---|---|---|
| idle hold | 0xC7 | dispatch | 0 | — |
| leap / windup | 0xD5 | dispatch | **+23** | cap1 f49, cap2 f70 |
| slash-1 raise | 0x50 | **arrival** | **A+5** | cap1 rel 35, cap2 37 |
| slash-1 STRIKE | 0x51 | **arrival** | **A+20** | cap1 rel 50, cap2 52 |
| slash-1 follow | 0x52 | **arrival** | **A+33** | cap1 rel 63, cap2 65 |
| slash-2 raise | 0x53 | **arrival** | **A+45** | cap1 rel 75, cap2 77 |
| slash-2 STRIKE | 0x54 | **arrival** | **A+60** | cap1 rel 90, cap2 92 |
| recover | 0x8B | **arrival** | **A+90** | cap1 rel 120, cap2 123 |
| settle | 0x47 | **return end** | **R+2** | cap1 rel 132, cap2 138 |
| idle bob | 0xC6 | **return end** | **R+19** | cap1 rel 149, cap2 155 |

`ctAnimengCronoCritPose` becomes `(rel, A, R)` → pick the last step whose resolved absolute rel ≤ `rel`
(the three anchors are monotone by construction because `A+90 < A+94 ≤ R+2`).

### 5.4 The strike / flash events (`battleAdvanceAction`, `src/battle.c:3477` and `:3574`)
* `CT_CRONO_CRIT_STRIKE` 50 → **`A + 20`** (pose) — keep the existing single "strike" event (hurt-flash +
  knock + wash 1) on it; the wash's measured peak is A+22, i.e. the existing 2-frame render lag is
  already right.
* `CT_CRONO_CRIT_STRIKE2` 91 → **`A + 60`**; the victim beat-B pal4 flash canonically opens at **A+61**
  (`CE/D42A` `69 00`, role-3 IP f117) and closes at **A+69** (`6A`, f125). Beat A's victim flash opens at
  **A+17** and closes at **A+25** (role-3 IP `CE/D47D` region, f74-f81) — matches the already-pinned
  "pal4 pulse A at rel 47-55".
* Drop the `battleAlive()` gate on the beat-B effects (`src/battle.c:3582-3585`) — §3.2.

### 5.5 The return + the snap (`battlePcAtkTick`, `src/battle.c:2408-2431`)
* `CT_CRONO_CRIT_RETURN` 113 → **`A + 92`** = the frame the op-98 home slide ARMS (IP `CE/D781`, cap1
  f148, cap2 f172).
* Insert the canonical **2-frame arm→move gap**: the first position change is **`A + 94`** (cap1 f150,
  cap2 f174). Today the arm and the first 4 px step happen in the same tick.
* Step count stays `dominant/4` at 4 px/frame (unchanged; cap1 7 steps 28 px, cap2 10 steps 40 px).
  `R = A + 93 + dominant/4` — record it in `pcAtkRetEndRel` on the last step.
* The `1B 19` snap must be **held 8 frames after `R`** (cap1 R=130 → snap 138; cap2 R=136 → snap 144;
  IP `CE/D78A`→`CE/D78C` at f163→164 / f190→191). Today it fires on the frame after the last step.
* Facing: the HOME re-aim (`$96DB`→3) lands at **A+87**, the re-aim to nearest (`$96DB`→2) at **R+17**
  (IP `CE/D792`, cap1 f173 = 147, cap2 f200 = 153). `battleAttackRender`'s `retFac`/idle facing already
  models this; only the R-anchored switch frame moves.

### 5.6 The span (`battleActionSpan`, `src/battle.c:2034/2671`)
`CT_SPAN_PC_CRIT` 177 → **`h->pcAtkArrRel + 146`** for `$9877==0x01`. Evidence: cap1 A=30 → 176
(f26→f202), cap2 A=33 → 179 (f47→f226). Keep the existing universal decomposition — it stays exact:
`span = WINDUP + 58 + 5` with `WINDUP = A + 83` (the `50` ShowDamage / popup birth: cap1 rel 113, cap2
rel 116), so the late-popup spawn at `spanRel − 63` (`src/battle.c:3595`) needs **no** change.
Ordering note: `battleActionSpan` runs *after* `battlePcAtkArm` on every dispatch path
(`src/battle.c:3312`/`:3314`, `:3404`/`:3409`, `:2930`/`:2932`), so `pcAtkArrRel` is available.

### 5.7 Re-pin
`field-battle-crit-x2`, `crit-leap-mid`, `crit-beatB-dead-suppressed` and every crit render canary shift:
GATO span 177→174, return 114→122, snap 119→134, recover pose 121→118, settle 132→128, idle 149→145.
Forest span 177→176, return 114→124, snap 119→138.

---

## 6 — OPEN

* **Resolver mode 0x28** (the leap goal of `98 02 28`, `CE/D799`) is not decoded — §11b's table stops at
  0x1C+. Our shipped strict-AABB leap sim reproduces both captures' arrivals ((132,65) and (165,111))
  byte-exactly, so the *arrival* is pinned empirically; the *formula* is not. Closing it needs the
  `C1/762E`/`C1/79D3` mode table read at index 0x28.
* **Zero-distance crit** (`dom == 0`): the ROM's `A` floor (23 or 24) is unmeasured — no capture puts
  Crono inside the victim's box before a crit. Closing it: stage an adjacent Crono (the
  `bat3marle.state` technique — poke `$1D0C/$1D23` seat 0 to a cell adjacent to a victim) with
  `$5E72`=0xFF and monitor `$5DBD:4` + `$1D0C`.
* **How the crit selects base script `CE/03CC`**: `C1/435B` branches on `$AE9B` bit 7 and loads
  `CD/401F` params + `CD/400E` anim id; which of those bytes becomes the CE base pointer is not traced
  (our engine gates on `h->actCrit`, so this is documentation-only).
* **`CE/03DF 7C` and `CE/03E5 35 7D`** — operand lengths inferred from the IP samples (they retire
  inside one frame); their semantics are undecoded. They carry no observed side effect.
* **Wash peak intensity**: bat3crit gives 22.6 % / 23.4 % mean-lerp vs the shipped
  `CT_CRIT_FLASH_PEAK`=28 (pinned on a different capture). Not part of this defect; a dedicated
  BG-palette (CGRAM) diff, not an fb mean, would settle it.
* **Other PCs' crits** (Lucca 0x10 / Robo 0x07 / Frog 0x09 / Ayla 0x0B / Magus 0x0D) — untouched;
  Marle's 0x0F stays the no-flash crossbow (`battle_crit.md` §C.3).

---

## VERIFICATION (adversarial pass)

Independent re-derivation. Probe = a copy of `build/ct.exe` (never the build binary). **A third capture at a
third leap distance** was added, plus an independent fb sweep of both washes on **all three** captures.

### V.0 — the §1 cap1 command does NOT run as written (finding)

`--input "(100:8,0:8)x16"` is not a supported grammar. `runMonitor` (`src/main.c:4559-4571`) parses with
`strtol(p,&p,16)`; on the leading `(` strtol consumes nothing, so `p` never advances → **infinite loop
running button 0x0000 forever**. Run verbatim it never dispatches the crit (first `$A013` 0→1 at f106 with
`$9877`=0x14, IP in bank **CD**, and the run does not terminate). Everything below uses the explicit
expansion `100:8,0:8` ×16, which reproduces §3 cap1 exactly. **The dossier's headline command must be
rewritten before anyone re-runs it.**

### V.1 — the three captures

| | cap1 | cap2 | cap3 (NEW) |
|---|---|---|---|
| input | `100:8,0:8` ×16 | `100:8,0:8,40:8,0:8,100:8,0:210` | `100:8,0:8,80:8,0:8,100:8,0:210` |
| dispatch (`$A013`0→1, `$9877`=01) | f26 | f47 | f47 |
| leap travel | f50-f56 | f71-f80 | f71-f83 |
| arrival `(1D0C,1D23)` | (132,65) | (165,111) | (108,88) |
| dominant dist | 28 | 40 | **52** |
| **A** (IP→`CE/D79C`) | 30 | 33 | **36** |

`A = 23 + dom/4` → 30 / 33 / 36. **CONFIRMED on three distances.** (The leap advances exactly 4 px per
frame on the dominant axis, so the simulated `dom` is structurally a multiple of 4.)

### V.2 — CONFIRMED, arrival/return-anchored on all three

| law | cap1 | cap2 | cap3 |
|---|---|---|---|
| `50` popup `$A3D1` = **A+83** | rel 113 | 116 | 119 |
| super 0x2A `CE/D77A` = **A+86** | 116 | 119 | 122 |
| `$96DB` HOME re-aim = **A+87** | 117 | 120 (f167) | — |
| recover pose = **A+90** | 120 | 123 | 126 |
| `98 02 19` arm `CE/D781` = **A+92** | 122 | 125 | 128 |
| first return travel frame = **A+94** | 124 | 127 | 130 |
| `R = A+93+dom/4` | 130 | 136 | 142 |
| settle pose = **R+2** | 132 | 138 | 144 |
| `1B 19` snap = **R+8** | 138 | 144 | 150 |
| `$96DB` re-aim `CE/D792` = **R+17** | 147 | 153 | 159 |
| idle pose = **R+19** | 149 | 155 | 161 |
| **span end = A+146** | 176 (f202) | 179 (f226) | **182 (f229)** |

ROM bytes re-read and CONFIRMED verbatim: `rom:CE03C0+112` (role-0 track `1E 2B|36|7C|72 0D|03 31|36|
35 7D|03 32|20 0A|1E 2A|24 04|2E|01`, role-3 track), `rom:CE0000+24` (ss 0x00=D428, 0x05=D47D, 0x0A=D4C3),
`rom:CE0050+16` (ss 0x2A=D776, 0x2B=D795), `rom:CED776+48`, `rom:CED428+96`, `rom:CED47D+16`
(`69 00 20 02 06 05 20 02 6A 03 05 06 03 1F` — beat A = beat B minus `72 0A` and minus `50`).
`$5FB0` = 14 flat → 0 at f202; cap2 `$60B0` → 0 at f226; cap3 `$6030` → 0 at f229 — **one application at
span end**, and the victim is alive (14 HP) throughout beat B (role-3 IP `CE/D42A` f117 → `6A` f125).
All cited `src/` line numbers verified (2360 bail-out, 2334 `pcAtkMvActive=0`, 2410, 2427-2429, 3291/3292/
3312/3314, 3370/3400/3404/3409, 2918/2919/2930/2932, 3582-3585, 3595, `animeng_e4_pc.h:35-46`,
`field.c:3292/3353` dispatch-relative).

### V.3 — REFUTED

**(a) "every op after the leap shifts by exactly +3".** There are **two clocks**. A-clock events shift +3
(cap1→cap3 Δ=27); anim-clock events shift +2 (Δ=26) and re-sync only at `CE/03EB`:

| | cap1 | cap2 | cap3 | Δ(1→3) |
|---|---|---|---|---|
| `CE/03E4` / `03E7` / `03E9` | 73/74/104 | 75/76/106 | 78/79/109 | **+26** |
| poses 5A·5B·5C·5D·5E | 35·50·63·75·90 | 37·52·65·77·92 | 40·55·68·80·95 | **+26** |
| `CE/03EB`, popup, D781, span | 105·113·122·176 | 108·116·125·179 | 111·119·128·182 | +27 |

The dossier lists `03 32` "74→76" *inside* its +3 proof; that is +2.

**(b) the pose ladder A+5/A+20/A+33/A+45/A+60 is fitted to cap1.** 2 of 3 captures give **A+4/A+19/A+32/
A+44/A+59** (cap1 A=30 → 35/50/63/75/90; cap2 A=33 → 37/52/65/77/92; cap3 A=36 → 40/55/68/80/95). §5.3's
own evidence cell "cap1 rel 35, cap2 37" is self-contradictory with A+5 (A+5 = 38 for cap2). The *shape* is
rigid and confirmed: relative to the first published pose P1 the ladder is exactly **+0/+15/+28/+40/+55**
in all three; only P1 jitters (`03 31` entry at A+2, publish latency 3 in cap1, **2** in cap2 and cap3).
Recover A+90, settle R+2 and idle R+19 are exact in all three and stand.

**(c) "both washes peak exactly 2 frames after their slash's STRIKE pose".** Independent fb-mean sweep
(`--peek … --btnscript`, mean of B+G+R over the 512×480 dump):

| capture | baseline | wash A onset / peak | rel(peak) | wash B peak | rel(peak) |
|---|---|---|---|---|---|
| cap1 (A=30) | 63.46 | 65.16 @78 / **106.86 @78** | 52 = **A+22** | **108.38 @118** | 92 = A+62 |
| cap2 (A=33) | 63.56 | 65.05 @101 / **106.80 @102** | 55 = **A+22** | **108.29 @141** | 94 = A+61 |
| cap3 (A=36) | 63.55 | 65.20 @104 / **106.91 @105** | 58 = **A+22** | **108.33 @144** | 97 = A+61 |

Wash **A** is rigidly **arrival**-anchored (onset A+21, peak A+22 in all three) — in cap3 that is *three*
frames after the 5B pose, not two. Wash **B** is rigidly **pose-5E + 2** — i.e. **anim**-anchored — giving
A+62 / **A+61** / **A+61**. This matches the ROM: role-3 enters beat A off its own `20 0F` right after the
arrival sync (`CE/03F6` at A+2), but enters beat B off a `23 01` sync to role-0's second `03 32`.
⇒ `CT_CRONO_CRIT_STRIKE` → **A+20 stands** (it lands the A+22 wash with the existing 2-f render lag), but
`CT_CRONO_CRIT_STRIKE2` → **A+60 is a one-capture fit; the 2-of-3 value is A+59** (wash B at A+61).
(My absolute means run ~1.1 above §3.1's — a different mean definition. The *frames* reproduce exactly, so
§3.1's mean-lerp percentages are method-dependent and should not be re-used as intensities.)

**(d) §4's engine-side delta row is wrong about our own code.** `battlePcAtkTick` arms **and** takes the
first 4 px step in the same tick (`src/battle.c:2410` arm, `:2419` step, no gap) — §5.5 says this
correctly, but §4's table says "arm 113, travel 114-118". Our first travel frame is rel **113**, so the
"8 f / 15 f EARLY" deltas are each one frame off.

### V.4 — corrections the implementer must apply

1. Pose ladder (§5.3): **A+4 / A+19 / A+32 / A+44 / A+59** (not +5/+20/+33/+45/+60). Better: anchor P1 and
   use the rigid +0/+15/+28/+40/+55, and record that P1 = A+4..A+5 is **not** invariant (undecoded lattice).
2. `CT_CRONO_CRIT_STRIKE2` (§5.4): **A+59**, not A+60. `CT_CRONO_CRIT_STRIKE` = A+20 confirmed.
3. Rewrite §1's cap1 command (`(100:8,0:8)x16` → the explicit ×16 expansion) — as printed it hangs.
4. Delete the "+3 for every subsequent op" claim; state the two-clock structure instead.
5. Delete "both washes peak 2 f after the STRIKE pose"; wash A = A+22 (arrival), wash B = pose-5E+2 (anim).
6. Fix §4's "arm 113 / travel 114-118" row (our first travel frame is 113) and the two delta figures.

Everything else in §5 — `A = 23+dom/4`, arm A+92, first move A+94, `R = A+93+dom/4`, snap **R+8**, settle
R+2, idle R+19, span **A+146**, popup A+83, the dropped `battleAlive()` beat-B gate — is **CONFIRMED on
three independent leap distances (28 / 40 / 52 px)**. Still OPEN and untested here: `dom == 0`, and whether
our sim's arrival matches the oracle's on a leap our engine has not been run against.


---

# IMPLEMENTATION ADDENDUM (session 24) — SHIPPED

The crit now rides the SAME arrival anchor as the melee (`pcAtkArrRel`, computed at the arm from the trig
leap: `A = CT_CRONO_CRIT_LEAP0 + dom/4`).

* `CT_CRONO_CRIT_STRIKE` 50 → **A+20** (kept at 20 per the verifier: wash A peaks at A+22 in all three
  captures, so the beat-A EVENT is arrival-rigid even though the beat-A POSE rides the other clock).
* `CT_CRONO_CRIT_STRIKE2` 91 → **A+59** (2 of 3 captures; A+62 is cap1-only).
* The return: `CT_CRONO_CRIT_RETURN 113` (dispatch-relative) → **arm A+92, first move A+94**
  (`CT_CRONO_CRIT_RET_ARM` + `CT_CRONO_CRIT_RET_GAP`), snap held **R+8** (`CT_CRONO_CRIT_SNAP_HOLD`).
* Span 177 → **A + 146** (`CT_CRONO_CRIT_SPANEND`).
* The pose ladder in `src/field.c` became a THREE-ANCHOR table (`ctCronoCritLadder`: dispatch / A / R) with
  the verifier's corrected offsets **A+4 / A+19 / A+32 / A+44 / A+59**, `0x8B` A+90, `0x47` R+2, `0xC6` R+19.
* The zero-travel bail-out at the old `battle.c:2360` is GONE: the ROM has no bail (the base script always
  runs `1E 2B`), so an already-adjacent crit now keeps the route armed with `A = LEAP0`. That bail was the
  mechanism of the user's report — with it the crit fell back to the melee's rel-60 return.

**Validated** (forest, A=30): dispatch f198 → beat B at rel **89** → return first travel rel **124** → last
travel R=130 → snap R+8 → span end rel **176 = A+146**. The second slash now lands 35 frames BEFORE he
starts home. GATO: A=28 → span 174, and a second, closer crit A=24 → span 170. Damage/HP/attack counts are
byte-identical (`field-battle-crit-x2` re-pinned on the frame axis only).

**Not applied (deliberate):** §5.4's "drop the `battleAlive()` gate on the beat-B victim flash". Every crit
capture in this dossier has the victim ALIVE at beat B — which the existing gate already passes. The case it
would change is the DEAD victim, and that is the one bat3crit actually measured (f202-212 all pal1, no
pulse) and that `field-battle-crit-beatB-dead-suppressed` pins. Removing it would contradict the only
oracle evidence that exists for it.
