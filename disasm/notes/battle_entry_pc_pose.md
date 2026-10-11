# BATTLE ENTRY — the PC's first pose and the WEAPON-DRAW ladder are FACING-DISPATCHED ROM windows (and the strike facing must aim at the victim's LIVE FOOT)

User report (translated from Portuguese; Guardia Forest, loc 19, storyline 0x03, SOLO Crono vs 3x Hetake — the
`saves/battle_crono_solo_lake.state` fight, walk LEFT to trigger):

> *"crono should start looking down, and then the sword animation to the left;
> in our version he starts looking up, and then the sword animation downwards."*

Both halves reproduce, and both are **facing-0 (UP) instances substituted for the canonical ones**.
They are TWO INDEPENDENT DEFECTS in our render, derived below from the ROM + the oracle:

| # | moment | ORACLE (this fight) | OURS | root cause (C site) |
|---|---|---|---|---|
| **A** | battle entry, before the draw | pose **`0x00`** (front / DOWN standing) | pose **`0x03`** (facing-0 = back to camera) | `battleEntranceRender` replays a BAKED GATO capture (`src/data/battle_entrance.h`) instead of the ROM's staged pose |
| **A'** | weapon-draw ladder | **`30 31 32 33`** (facing-2 window `+0x30`) | **`38 39 3A`** (facing-**0** window `+0x30`) | same bake |
| **B** | the attack SWING | **`5A 5B 5C`** (facing-2 STRIKE window `+0xC4`) | **`50 51 52`** (facing-**0** STRIKE window) | `src/field.c:4274-4278` aims at the victim's render **ANCHOR**, not its live foot |

The FACING ENGINE ITSELF IS CORRECT in our port: `$96DB[0] = 02` (LEFT) in the oracle and
`pcFacing[0] = 2` in ours (`CT_BATTLE_FACETRACE`: `pcface ENTRY f0 fac=2,1,1 crono=(160,70) e0=(120,63)`),
and the settled idle stance renders `0xC8` = the facing-2 idle window in both. Nothing in
`battle_facing_engine.md` §2 needs revisiting.

Facing encoding CONFIRMED identical to the field's: the battle's `$96DB`/`$96DE` decode through
`C0/F700` is **0=UP 1=DOWN 2=LEFT 3=RIGHT** (battle_facing_engine.md §1; re-verified here — `$96DB[0]=02`
while the published pose is `C8`/`C9`, and `C8` is literally `ptrA + 2*stride + 0x0C`).

---

## 0. Method / repro

```
P=<scratch>/ct_probe.exe            # copy of build/ct.exe   (NEVER probe with build/ct.exe)
F=<scratch>/ct_field.exe            # copy of build/ct_field.exe
# ORACLE (walk LEFT into the ambush, then two A presses = Crono's physical on Hetake0):
$P --monitor "9907:1,96DB:1,1D0C:1,1D23:1,9877:1" --from saves/battle_crono_solo_lake.state \
   --input "40:120,0:280,100:3,0:20,100:3,0:400" --diff --frames 820
$P --monitor "9245:8,922D:8,5DC1:3,5DBD:3" --from saves/battle_crono_solo_lake.state \
   --input "40:400" --diff --writer --frames 300
$P --peek saves/battle_crono_solo_lake.state out.raw --btnscript "40:120,0:<N-120>"     # entry frames
$P --peek saves/battle_crono_solo_lake.state out.raw --btnscript "40:120,0:280,100:3,0:20,100:3,0:236"   # f662 strike
python tools/ct.py fb2png out.raw out.png
# OURS
CT_BATTLE_FACETRACE=1 CT_ANIMENG_PCTRACE=1 CT_BATTLESHOT=<out>.raw CT_BATTLESHOT_AT=<f> \
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $F --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest o.raw --frames 500 --walkscript "l:120,."
#   (+ CT_BATTLE_MASHA=1 to make Crono attack)
```

> **TRAP (cost me an hour — do not repeat).** Use `--walkscript "l:120,."`, **not** `"l:400,."`.
> With `l:400` the walkscript keeps pushing left after the fight resolves and the ambush re-triggers:
> `CT_BATTLE_FACETRACE` prints **five** `pcface ENTRY` lines, `CT_BATTLESHOT_AT` fires in **every**
> battle, and the .raw file keeps the LAST one. The later battles re-enter with the enemies already
> flagged dead, so `battlePcAimFacing` finds no candidate and returns its default **1**, and the shot
> shows Crono in the facing-1 idle window (`43 / 2F / 44`). That is a HARNESS artefact, not an engine
> defect — the first battle renders `0xC8` correctly.

**Pose identification** (how every "ours draws pose 0xNN" claim below is established, not eyeballed):
a scratch Python renderer builds the pixel MASK of every Crono E3 record `0x00..0xFF` straight from the
ROM (E3 base `E4:2300[0] = $DC27B0`, stride `0x28`; sheet `E4:2000[0] = $D272C0`, raw 4bpp, `src*0x20`;
raster `word i -> vt 0x08 + (i%8) + 0x10*(i//8)`, 4 cells at `vt0+{0,2,4,6}` with the record's signed
(dx,dy)) and IoU-matches it against the composited `CT_BATTLESHOT` frame over a +-6px shift search.
Every match reported below is **IoU = 1.000 at shift (0,0)** and unique over all 256 indices.

---

## 1. THE ORACLE'S ENTRY TIMELINE (this fight)

`--monitor "96DB:6,9907:1,1C99:3,966E:1" --from saves/battle_crono_solo_lake.state --input "40:400"`:

| frame | event | value |
|---|---|---|
| f113 | a script takes Crono (field) | — |
| f128 | battle staged | `$2989 = 02` |
| f190 | `CF:FB9A` clears the battle RAM | `$96DB=00...`, `$9245=00...` |
| f197 | `CC:E366` parks the published pose | `$9907[0] = FF` |
| f199 | PC staging `CC/E940` | `$1C99[0] = 00 30 E4` (ptrA = **$E43000**), `$966E` stride = **0x128**; `$96A4..A8 = 0A 04 00 06 00` |
| **f204** | `C1/34A7` publishes each combatant's staged pose | **`$9907[0] = 00`** <- *"looking down"* |
| **f217** | `C1/350F` entry-facing law | **`$96DB[0] = 02`** (LEFT); `$5DF1 = 0x0030`; `$9245 = 30 31 32 33 FF 00 00 00`; `$922D = 05 05 05 05 00...` |
| f219/229/239/249 | `C1/3702` steps the ladder (10 render frames each) | `$9907[0] = 30 -> 31 -> 32 -> 33` <- *"sword to the left"* |
| f291+ | `C1/2EF2` idle stepper | `C9 33 C8 33 C9 ...` = the facing-2 IDLE window, dwell 7/13 |

Pixel proof (`--peek` + `fb2png`, cropped around the live foot (160,70)):
* **f210** (`$9907=00`): Crono drawn **front-facing, his FACE visible** — the DOWN standing sprite.
* **f245** (`$9907=32`): katana out, blade sweeping **LEFT**.
* **f300** (`$9907=C9`): the side/LEFT idle stance, sword held low-left.

Our engine at the matching module frames: **f5/f60 = pose `0x03`** (back of the head, no sword),
**f95/f105/f115 = `0x38` / `0x39` / `0x3A`** (a back-view draw), **f130+ = `0xC8`** (correct facing-2 idle).

---

## 2. THE CANONICAL MECHANISM

### 2.1 The pre-ladder pose = the PC's LIVE FIELD ANIM FRAME (`$0F01,X`)

Field->battle PC transfer **C0/041A** (writer of `$29B0`, oracle f131 reports `(C0:045A)`):

```
C0/041D  $DB = $1D0A>>1 , $DD = $1D0E>>1      ; camera tile origin
C0/042D  $7E29B0 = $1801,X - $DB              ; tile X   (X = $97 = the lead PC's field object)
C0/043C  $7E29B2 = $0A00,X & 0x0F             ; fine X
C0/0446  $7E29B1 = $1881,X - $DD              ; tile Y
C0/044F  $7E29B3 = $0A80,X & 0x0F             ; fine Y
C0/0453  LDA $0F01,X : STA $7E29B4            ; +4 = THE FIELD ANIMATION FRAME  <-- the battle pose
```

Battle staging **CC/E940** (PC pass) copies it into the publish register:
```
CC/E965  LDA $96A8,X : STA $A4DC,Y            ; $A4DC[cmb] = record+4  ($96A4+5*seat mirrors $29B0+5*seat)
CC/E96B  TDC / STA $96DB,Y                    ; facing zeroed here (the C1/350F law stamps it at f217)
```
and **C1/34A7** publishes it once for every present combatant:
```
C1/34AF  LDA $96F5,X : BEQ skip               ; present?
C1/34B7  STA $9679                            ; combatant index
C1/34BA  LDA $A4DC,X : STA $967B              ; the pose
C1/34C0  JSR C1/1C55                          ; -> C1/1C66 STA $9907,X (the published assembly index)
```

Measured here: `$29B0..B4 = 0A 04 00 06 `**`00`** -> `$A4DC[0] = 0x00` -> `$9907[0] = 0x00`.
E3 record `0x00` is Crono's **front/DOWN standing assembly** (it is also `ptrA[facing1] + 0x00`; the
`+0x00` window is `03 / 00 / 01 / 02` for facings 0/1/2/3 — §2.2). **This byte is DATA, not a facing
computation**: it is whatever field frame the sprite happened to be on. `saves/battle_re/forest_pre.raw.wram`
carries `$29B4 = 0x00` too. Our own stager already writes it (`src/field.c:5290`,
`r[4] = s[0x0F01 + po*2]`, and `:5313` for the enemy pass) — **nothing in our engine ever reads it back.**

### 2.2 The weapon-draw ladder = ROM window `+0x30`, indexed by `$96DB`

`C1/34DB` is the entry sequence, in order:
```
C1/34DB  JSR C1/350F                          ; THE ENTRY FACING LAW (battle_facing_engine.md §2)
C1/34E4  LDA $A09B,X                          ; per-seat flag; 0 for all 3 seats in this fight
C1/34E9    != 0 -> $5DF1,Y = 0x0000           ;   window offset 0x00  (single pose, no draw)
C1/34F5    == 0 -> $5DF1,Y = 0x0030           ;   window offset 0x30  <-- the WEAPON-DRAW ladder
C1/350C  JMP C1/35DD                          ; -> the per-seat copier below
```
The copier **C1/35E9-3663** (per seat, X = seat):
```
C1/35F0  $A5 = $96DB,X                        ; FACING
C1/35F5  $A7/$A8 = $966E,X / $A2C8,X          ; STRIDE (16-bit; Crono 0x128)
C1/35FF  JSR C1/00A7                          ; $A9 = facing * stride
C1/3617  $A1FE = $1CBA,X (ptrB) + $5DF1,Y                 ; DWELL source  (NO facing term)
C1/3621  $A1E8 = $1C99,X (ptrA) + $5DF1,Y + $A9           ; POSE  source  (facing*stride)
C1/3639  8x: LDA $E40000,X : LSR A : STA $922D,Y          ; dwells = ptrB[...] >> 1
C1/3650  8x: LDA $E40000,X :         STA $9245,Y          ; poses  = ptrA[...]
C1/3665  $5DBD/BE/BF = 1/2/3 (per-seat stagger); $5DC1..C3 = FF (step cursor = -1)
```
and the stepper **C1/36A1** runs it (one tick every OTHER render frame — `$5DBD` counts 5->0 across 10 frames):
```
C1/36C7  DEC $5DBD,X : BNE next               ; per-entry dwell
C1/36CC  INC $5DC1,X                          ; step cursor 0..7
C1/36E7  LDA $922D,Y : STA $5DBD,X            ; reload dwell; 0 = TERMINATOR -> ladder over
C1/36FC  LDA $9245,Y : STA $967B              ; the pose
C1/3702  JSR C1/1C4A                          ; publish -> $9907,X
```

**The ROM windows** (direct ROM read: `E4:2600[cid*2]` = ptrA, `E4:2800[cid*2]` = ptrB,
stride = `(ptrA[cid+1]-ptrA[cid])>>2`; Crono ptrA `E4:3000`, ptrB `E4:A800`, stride `0x128`):

| Crono window | facing 0 (UP) | facing 1 (DOWN) | facing 2 (LEFT) | facing 3 (RIGHT) |
|---|---|---|---|---|
| `+0x00` standing (1 entry) | `03 FF` | `00 FF` | `01 FF` | `02 FF` |
| `+0x30` **weapon draw** | `38 39 3A 4F FF` | `2C 2D 2E 2F FF` | **`30 31 32 33 FF`** | `34 35 36 37 FF` |
| `+0x0C` idle | `C6 4F C7 4F` | `43 2F 44 2F` | `C8 33 C9 33` | `CA 37 CB 37` |
| `+0x18` run | `20 41 21 22 42 23 FF` | `1C 3F 1D 1E 40 1F FF` | `24 3B 25 26 3C 27 FF` | `28 3D 29 2A 3E 2B FF` |
| `+0xC4` **strike** | `50 51 52 FF 53 54` | `55 56 57 FF 58 59` | **`5A 5B 5C FF 5D 5E`** | `5F 60 61 FF 62 63` |
| `+0x38` recover | `49 8D 8E 49 FF` | `47 8B 8C 47 FF` | `4D 91 92 4D FF` | `4B 8F 90 4B FF` |
| ptrB `+0x30` dwells | `0A 0A 0A 0A 00` -> `>>1` = `05 05 05 05 00` (facing-independent; 10 render frames each) | | | |

`$9245 = 30 31 32 33 FF 00 00 00` and `$922D = 05 05 05 05 00 00 00 00`, read out of the oracle at f217,
are **byte-identical** to `ptrA + 2*0x128 + 0x30` and `(ptrB + 0x30) >> 1`. Marle/Lucca/Frog have the same
`+0x30` shape (`2C 2D 2E 2F` / `30 31 32 33` / `34 35 36 37` / `38 39 3A 3B` — Crono's four rows are permuted
relative to theirs, which is exactly why a facing bug is invisible on Marle and loud on Crono).

### 2.3 What our engine does instead (DEFECT A)

`src/field.c:4677 battleEntranceRender` + `src/data/battle_entrance.h` replay a **captured GATO fight**:
4 Crono keyframes (`P0_field` t290-314, `P1_gatherA` 315-324, `P2_gatherB` 325-334, `P3_spread` 335-344)
against `CT_ENT_BOOT_F 222` (`src/field.c:4660`), i.e. module frames 0-92 / 93-102 / 103-112 / 113-122.
Measured against the ROM records: **P0 == E3 `0x03`, P1 == `0x38`, P2 == `0x39`, P3 == `0x3A`**
(IoU 1.000 each, at the observed frames f5/f60, f95, f105, f115). The bake is a verbatim capture of
**`ptrA[facing 0] + 0x00`** followed by **`ptrA[facing 0] + 0x30`** entries 0..2 — the right MECHANISM
frozen at the wrong FACING and carrying the capture's own `$29B4`, so every fight replays the GATO fight's
entry. (The 4th ladder entry `4F` was never captured; it coincides with the facing-0 settle pose.)

---

## 3. THE ATTACK SWING (DEFECT B) — measured on both sides, same fight

ORACLE, Crono's physical on Hetake0 (`--monitor "9907:1,96DB:1,1D0C:1,1D23:1"`, A -> A):

```
f627-645  $1D0C 160 -> 128 at -2/f   poses 24 -> 3B -> 25 -> 26  = facing-2 RUN window (+0x18)
f647-672  planted at (128,70)        poses 5A -> 5B -> 5C        = facing-2 STRIKE window (+0xC4); $96DB stays 02
f685      $96DB[0] <- 03 (home aim)  f688 pose 8F, X 128->160 at +4/f = facing-3 RECOVER (+0x38)
f701      pose 4B (settle)   f716 $96DB[0] <- 02 (nearest-enemy re-aim)   f718 pose C8 (facing-2 idle)
```

OURS (`CT_ANIMENG_PCTRACE=1`, same fight, `CT_BATTLE_MASHA=1`):
```
pcatk f225 pose=0 idx=0x3B foot=(146,70) arr=(160,70) fac(a2,s2,r1)
pcatk f235 pose=2 idx=0x21 foot=(128,70) arr=(128,70) fac(a2,s0,r3)
pcatk f245 pose=3 idx=0x50 foot=(128,70) arr=(128,70) fac(a2,s0,r3)     <-- facing-0 strike window
pcatk f255 pose=3 idx=0x51 foot=(128,70) arr=(128,70) fac(a2,s0,r3)     <--   "
pcatk f265 pose=3 idx=0x52 foot=(128,70) arr=(128,70) fac(a2,s0,r3)     <--   "
pcatk f290 pose=4 idx=0x8F foot=(160,70) arr=(128,70) fac(a2,s2,r3)     <-- recover, facing 3  OK
pcatk f305 pose=5 idx=0x4B foot=(160,70) arr=(128,70) fac(a2,s2,r3)     OK
```
Approach (`a2`, poses `3B`/`24`) and recover (`r3`, poses `8F`/`4B`) match the oracle. **The strike facing
`s0` is wrong; the oracle's is 2.** The arithmetic (ROM `C1/0222` atan2 over `C0/F300`, then `C0/F700`
faceTab — both ported verbatim as `battleAtan2` / `ctBattleFaceTab`):

| aim operands | bearing | faceTab | window played |
|---|---|---|---|
| ROM law: self live foot `(128,70)` -> victim **LIVE FOOT** `(120,63)` | `0x80` | **2** | `5A 5B 5C` (correct) |
| ours: self `(128,70)` -> victim **RENDER ANCHOR** `(120-16, 63-28) = (104,35)` | `0xA6` | **0** | `50 51 52` (wrong) |

The ROM re-stamps `$96DB` at the arrival from the victim's `$1D0C`/`$1D23` (the engage block's `72 0D`
face-target op; the same operands `C1/353B` feeds the entry law) — and **`src/battle.c:3060-3062` already
does exactly that**, into `h->pcFacing[0]`, using `h->mvLiveX/Y[h->actTarget]`. The render simply does not
use it: `src/field.c:4274-4278` re-derives its own aim from `battleEnemyComputedAnchor()`
(`src/field.c:2707`), which returns `stagedFoot + CT_EXT_HETAKE_{X,Y} = (-16,-28)` — a **sprite min-corner
extent**, not a position. `C0/F300` is indexed `(|dy|>>3)*32 + (|dx|>>3)`, so at this geometry even +-1px
of Y crosses a quadrant boundary (`(128,70)->(120,63)` = facing 2 but `(128,71)->(120,63)` = facing 0);
28px of bogus Y is far past it.

---

## 4. THE MINIMAL MANDATE-SAFE CHANGES (derived; NOT applied — read-only session)

**B (small, do this first — it is a pure "use the byte the ROM uses" fix).**
`src/field.c:4274-4278`: the strike aim must be the victim's LIVE FOOT, i.e. the same
`h->mvLiveX[h->actTarget] / h->mvLiveY[h->actTarget]` operands `src/battle.c:3062` already aims with —
or, more canonically, read `h->pcFacing[0]`, which IS `$96DB` and already carries the arrival re-stamp.
Delete the `battleEnemyComputedAnchor + battleEnemyLiveDelta` aim origin: an OAM extent is not a
combatant position and has no counterpart in `C1/0222`'s operands.
Validation note: `field-battle-pc-approach-early/-late` and the `-crit-*` checks run the forest fight from
`saves/battle_re/forest_pre.raw.wram`, whose `$29B3 = 07` puts Crono's foot at Y **71** — where the ROM law
itself yields facing 0 for the strike. Those pins may not move at all; check before re-pinning.

**A (the real port).** Retire `src/data/battle_entrance.h` + `battleEntranceRender`'s keyframe replay and
drive the entry from the ROM, exactly as §2:
1. at battle begin, publish `pose = mem[0x29B0 + 5*seat + 4]` (= `$A4DC`, `CC/E965` -> `C1/34BA`) and hold it;
2. stage the ladder per seat: `poses[k] = CtBattlePcWinPose(charId, 0x30, k, pcFacing[seat])` and
   `dwellTicks[k] = CtBattlePcWinDwell(charId, 0x30, k) >> 1` (the `LSR A` at `C1/363D`). Both accessors
   already exist in `src/battle.c` (`CtBattlePcWinPose` / `CtBattlePcWinDwell`), and both are already
   ROM-verified by the `animengcheck-*` selfchecks;
3. step one entry per `dwellTicks[k]` **logic ticks of 2 render frames** (`C1/36C7`), terminating on the
   `0xFF` pose / `0x00` dwell, then hand over to the idle stepper (`C1/2EF2`) — which our `pcStanceIdx`
   path already renders correctly;
4. keep the alternate arm: `$A09B,seat != 0` selects window offset `0x00` instead of `0x30`
   (`C1/34E4-34F3`) — a one-pose entry with no draw. It reads `00 00 00` in this fight; gate on the byte,
   do not guess its source.
Everything needed is already in RAM/ROM — no new capture. The retired keyframes become fixtures and must
re-emerge as the facing-0 instances of the same windows (P0 `0x03`, P1..P3 `38 39 3A`).

**Suggested new checks** (`add-regress-check`): (i) `CT_BATTLESHOT_AT` on the solo-forest entry at a
pre-ladder frame — must render E3 `0x00`, not `0x03`; (ii) one mid-ladder frame — must render `0x31`/`0x32`,
not `0x39`/`0x3A`; (iii) a unit line asserting `CtBattlePcWinPose(0,0x30,k,2) == {30,31,32,33}` and
`CtBattlePcWinDwell(0,0x30,k)>>1 == 5` (same shape as the existing `animengcheck-marlemelee`);
(iv) a `CT_ANIMENG_PCTRACE` pin on `fac(a2,s2,r3)` for the solo-forest swing — an EVENT-anchored line, not
a frame number.

---

## 5. OPEN QUESTIONS

1. **The 42-frame gap f249 -> f291** between the ladder's last entry (`33`, dwell ends ~f259) and the
   first idle publish (`C9`). `$5DC1` parks at 04 (the `FF` entry) and `$5DBD` free-runs negative; the
   handover is gated on `$5DDD`/`$5DDE` (`C1/36AC`, `C1/36EF`) plus the 1/2/3 per-seat stagger at
   `C1/3665`. The exact release condition is NOT derived.
2. **`$A09B`** — who writes it, and what the offset-0 "no draw" entry is for. Zero in every state read
   here, so that arm is unexercised (a negative on a state that may simply not exercise it — see
   memory `verify-negatives-on-proper-state`).
3. **The idle cadence.** The oracle cycles `C8(7f) 33(13f) C9(7f) 33(13f)`; our stance layer draws `win[0]`
   statically and the bob layer cycles on the pinned `ctCronoIdleBob` fixture. Same standing residual as
   `battle_facing_engine.md` §7 (the ptrB dwell decode for the PC tick `C1/2E00`) — the pose SET is right,
   only the timing is fixture-driven. Out of scope for this report.
4. **Is the arrival re-stamp observable here?** `$96DB` holds `02` across f627-f685, so the arrival stamp
   recomputes the same value and is unobservable in THIS fight. `battle_facing_engine.md` §4 saw a real
   `$96DB[0] <- 0` at bat3 f108 — same law, foot Y 71 instead of 70.
5. `CT_ENT_BOOT_F 222` was calibrated on the GATO capture; once the ladder is ROM-driven the anchor should
   be the `C1/34FF` staging frame (the `$5DF1` write), not a baked constant.

## 6. Scratch artefacts (not committed)

`<scratch>/facing/`: `ct_probe.exe`, `ct_field.exe`, `crop.py`, `ours2png.py`, `match.py` (the E3-mask IoU
matcher — the tool that makes every pose claim above falsifiable), oracle peeks
`or_{210,225,245,265,300,662}.raw(+.png)`, ours `w_{5,60,95,105,115,130,190}.raw`, `a_{215..320}.raw`,
and `fs_*` (a `$2980`-patched SOLO copy of `forest_pre.raw.wram`, used to rule solo-party-ness out as a
cause before the re-triggered-battle harness trap was found).

---

# Adversarial review (independent re-derivation, read-only session)

Method: every cited ROM address re-disassembled with `python tools/dis.py` from the ROM file offset;
every oracle value re-measured with my own `--monitor`/`--peek` runs from
`saves/battle_crono_solo_lake.state`; our side re-measured with my own snapshot of `build/ct_field.exe`.
Scratch: `%TMP%/ctwf3/facing-adv/` (`ct_probe.exe`, `ct_field.exe`, `crop.py`, `z.py`, oracle peeks
`or210/or245(.png,_z.png)`, ours `ours60/ours105(_z.png)`, WRAM dumps `s140/s170/s300.wram`,
`solo_staged.wram`).

## AR.0 Verdict

The two MECHANISMS are **confirmed**. Both PROPOSED FIXES are **not proven sufficient**, one of them
for a reason that is fatal in exactly the scenario the user reported.

## AR.1 What re-verified CLEAN

**Every ROM citation holds.** Re-disassembled, byte for byte:

| cited | my dis.py | verdict |
|---|---|---|
| `C0/0453 LDA $0F01,X : STA $7E29B4` | `0453: LDA $0F01,X` / `0456: STA $7E29B4` | OK |
| `CC/E965 LDA $96A8,X : STA $A4DC,Y` + `CC/E96B TDC : STA $96DB,Y` | identical | OK |
| `C1/34BA LDA $A4DC,X : STA $967B : JSR C1/1C55` then `C1/1C66 STA $9907,X` | identical | OK |
| `C1/34DB JSR C1/350F`; `$A09B,X` gate to `$5DF1 = 0x0000 / 0x0030` | identical (`LDX #0002 / LDY #0004` at 134DE, 3-seat loop, `DEY DEY DEX BPL`) | OK |
| `C1/350F` reads self `$1D0C,X/$1D23,X` and target `$9873`'s `$1D0C/$1D23`, `JSR C1/0222`, `LDA $C0F700,X`, `STA $96DB,X` | identical | OK |
| `C1/35F0 $A5=$96DB,X`; `C1/3617 $A1FE = $1CBA,X + $5DF1,Y` (no facing); `C1/3621 $A1E8 = $1C99,X + $5DF1,Y + $A9`; `C1/3639 LSR to $922D,Y`; `C1/3650 to $9245,Y` | identical | OK |
| `C1/3665 $5DBD = 1/2/3`, `$5DC1 = FF` | identical | OK |
| `C1/36C7 DEC $5DBD,X` / `36CC INC $5DC1,X` / `36E7 reload` / `36FC LDA $9245,Y` / `3702 JSR C1/1C4A` | identical (+ `136D2 CMP #08` cursor cap) | OK |
| `C1/0222` atan2 index `(abs dy>>3)*32 + (abs dx>>3)` | identical to `battleAtan2`, `src/battle.c:3895` | OK |

**Every ROM window table reproduces.** Independent read of `E4:2600[0]=0x3000` (ptrA),
`E4:2800[0]=0xA800` (ptrB), `ptrA[1]-ptrA[0] = 0x4A0`, `>>2 = 0x128` stride: all six rows of the
dossier's table come out byte-identical, including `+0x30 f2 = 30 31 32 33 FF`, `+0x00 = 03/00/01/02`,
`+0xC4 f2 = 5A 5B 5C FF 5D 5E`, `ptrB+0x30 = 0A 0A 0A 0A 00`.

**Every oracle value reproduces**, on a state I loaded myself:

```
ct_probe --monitor "9907:1,96DB:1,9245:8,922D:8,5DF1:6,29B0:5,96A4:5,A4DC:1,966E:2" \
   --from saves/battle_crono_solo_lake.state --input "40:400" --diff --writer --frames 330
```

f131 `$29B0 = 0A 04 00 06 00` (writer **C0:045A**); f199 `$96A4 = 0A 04 00 06 00` (CC:E93B),
`$A4DC=00` (CC:E96B); **f204 `$9907[0] = 00`** (C1:1C69); **f217** `$96DB[0]=02` (C1:3548),
`$9245 = 30 31 32 33 FF 00 00 00` (C1:3657), `$922D = 05 05 05 05 00..` (C1:3641),
`$5DF1 = 30 00 30 00 30 00` (C1:34FF); f219/229/239/249 `$9907 = 30/31/32/33`; f291+ `C9 33 C8 33`.
The attack run (`--input "40:120,0:280,100:3,0:20,100:3,0:400"`) reproduces f627-645 `24/3B/25/26`
with `$1D0C 0xA0 -> 0x80`, f647/662/672 `5A/5B/5C` planted at `$1D0C=0x80`, f685 `$96DB<-03`, f688
`8F`, f701 `4B`, f716 `$96DB<-02`, f718 `C8`. `$1D0C/$1D23` for the three Hetakes read
`(120,63) (88,95) (166,119)` and `$9873 = 03`, so the entry law's target is Hetake0's LIVE foot.

**Pixel proof, re-taken and looked at.** `--peek ... --btnscript "40:120,0:90"` then `fb2png` then an
8x crop at (140,40): oracle f210 is Crono **front-facing, face and both hands visible**. f245 is the
drawn katana in a side stance. Ours, `--scene 19 --battletest saves/battle_re/forest_pre.raw.wram`
with `CT_BATTLESHOT_AT=60` / `=105`: **back of the head, no face**, then a back-view weapon draw.
Defect A and A' are real and user-visible exactly as reported.

**Two dossier gaps I closed while checking:**

* **OQ1 first half is answered.** `C1/36E7-36F4`: on the terminator entry the reload stores dwell `0`
  into `$5DBD,X`, `BEQ` falls through to `LDA $5DDD / BEQ` and **nothing is published** -- `$9907`
  HOLDS the last pose. Measured (`--monitor "A2C8:3,A09B:3,5DBD:3,5DC1:3,5DDD:2" --diff --writer`):
  `$5DC1[0]` parks at `04` at f259 and `$5DBD[0]` free-runs `00,FF,FE,FD...` from f259 on, `$5DDD =
  01`. The ladder is permanently dead after entry 3; the 42-frame gap belongs entirely to the idle
  stepper's own start gate, not to the ladder.
* **"a tick = 2 render frames" is now DERIVED, not inferred.** Same monitor: `$5DBD[0]` decrements on
  f235, f237, f239, f241, ... (writer C1:36CA) -- exactly every other render frame; reload to `05` at
  f239/f249 (C1:36ED) with `$5DC1` `01->02->03->04` (C1:36CF). `$A09B = 00 00 00` (the `+0x30` arm).

## AR.2 REFUTED / NOT PROVEN

### AR.2.1 (FATAL to fix A as written) our `$29B4` is **0x01**, not `0x00`

The dossier says: *"Our own stager already writes it (`src/field.c:5290`) -- nothing in our engine ever
reads it back."* The second half is true (grep: `$29B0 + 5*seat` is read only by `src/battle.c:4267`,
for the FOOT). **The first half implies the datum is correct. It is not.**

```
CT_DUMPWRAM=<f>.wram CT_DUMPWRAM_AT=200 SDL_VIDEODRIVER=dummy ct_field.exe --assets ct_assets.dat \
  --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest o.raw --frames 260 \
  --walkscript "l:120,."
```

| | `$29B0..B4` |
|---|---|
| ORACLE (f131, C0:045A) | `0A 04 00 06 `**`00`** |
| OURS (dumps at f140, f170, f200, f300 -- stable) | `0A 04 00 06 `**`01`** |

The first four bytes are byte-exact. The **fifth byte -- the one fix A step 1 proposes to publish -- is
wrong**. By the ROM's own `+0x00` window (`03/00/01/02` for facings 0/1/2/3), E3 `0x01` is the
**facing-2 (LEFT) standing** record. Implementing fix A step 1 verbatim would replace today's
back-facing `0x03` with a LEFT-facing `0x01`, **not** the DOWN-facing `0x00` the user asked for.

This is the same one-frame `$0F01` drain lead that `--battletest` already declares an ACCEPTED SCOPE
LIMIT and EXCLUDES from the staging compare (`src/field.c:8324-8331`; its own printout for this scene
reads `anim-frame bytes (+4 = $0F01, EXCLUDED ...): got 00 00 00, oracle 05 05 04`). The dossier never
mentions it, yet fix A rests on that byte.

A **second, independent** field-side divergence sits next to it. Oracle
`--monitor "0F05:1,1604:1,0197:1,29B4:1" --from saves/battle_crono_solo_lake.state --input "40:400"`:

```
f0    $0F05=01  $1604=02                          (identical to ours)
f113  $0F05=11  $1604<-03  (C0:4FBD)      <- the ambush SCRIPT turns Crono
f123  $0F05<-00 $1604<-01  (C0:5550)      <- and this is where $0F01 becomes 00
f195            $1604<-00  (C1:CE04)
f198  $0F05=29  (CC:E4E6)
```

Ours holds `$1604(obj2) = 02` for the whole ambush (dumps f140/f170/f300). Our live `$0F01(obj2)`
does reach `00`, so the staged `01` is a sampling-moment miss rather than a wrong animation -- but the
ambush's scripted turns are not reproduced either. **One of these two must be fixed before fix A can
produce `0x00`.** Until then fix A is a real improvement (facing-2 ladder instead of facing-0) whose
FIRST pose is still wrong.

### AR.2.2 the "OURS" pcatk table in section 3 was NOT taken on the user's scenario

The stated repro (`--loadwram saves/forest_re/battle_crono_solo.dumpwram ... CT_BATTLE_MASHA=1`)
emits **no `pcatk` line at all**: `CT_BATTLE_MASHA` runs the whole battle synchronously inside one
`CtBattleRun` (`src/field.c:5108`), so there is no per-frame compose and `battleAttackRender` is never
called. I confirmed this (only `pcface` logic lines appear). What DOES reproduce the dossier's exact
frame numbers is the forest_pre `--battletest` fixture:

```
for f in 225 235 245 255 265 275 290 305; do
  CT_ANIMENG_PCTRACE=1 CT_BATTLE_MASHA=1 CT_BATTLESHOT=e.raw CT_BATTLESHOT_AT=$f SDL_VIDEODRIVER=dummy \
  ct_field.exe --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram; done
->  pcatk f225 pose=0 idx=0x3B foot=(146,71) arr=(160,71) fac(a2,s2,r1)
    pcatk f235 pose=2 idx=0x21 foot=(128,71) arr=(128,71) fac(a2,s0,r3)
    pcatk f245 pose=3 idx=0x50 foot=(128,71) arr=(128,71) fac(a2,s0,r3)   ... f255 0x51, f265/275 0x52
    pcatk f290 pose=4 idx=0x8F foot=(160,71) arr=(128,71) fac(a2,s2,r3)   ... f305 pose=5 0x4B
```

Foot Y is **71**, not 70. The dossier prints `foot=(128,70)` / `arr=(160,70)`. The frame numbers are
forest_pre's; the coordinates are the solo state's. The `s0` finding survives, but that evidence line
must be relabelled -- it is the forest_pre fixture, not the user's fight.

Also: `saves/forest_re/battle_crono_solo.raw.wram` has `$29B0..B4` and `$29BF..` **all zeros**
(pre-staging), so `--battletest saves/forest_re/battle_crono_solo.raw.wram` cannot stage this fight
(`battletest: FAIL (staging 1/7 ...)`). Only the `--loadwram <dumpwram>` interactive route works for
this scenario.

### AR.2.3 fix B variant 1 is still a stand-in for a LATCH

The dossier offers two variants as if interchangeable: (1) aim at `h->mvLiveX/Y[actTarget]`, or (2)
read `h->pcFacing[0]`. They are not equivalent, and (1) is not mandate-clean.

`$96DB` is a **latch**: in the ROM it is written only by `C1/3545` (entry law) and by the engage
script's face-target op. Measured, this fight: `$96DB[0] = 02` unchanged from **f217 through f685**,
across approach, plant and all three strike poses. Our `strkFac` is recomputed EVERY FRAME from the
moving foot and actually CHANGES mid-action: `s2` at f225, `s0` at f235..f275, `s2` at f290. Swapping
the aim TARGET from the render anchor to the live foot leaves that per-frame recomputation in place; it
merely happens to agree at the frames we look at. Only variant (2) branches on the byte the ROM
branches on. (Variant (2) is not a drop-in either: `retFac`/`apFac` would then also have to come from
the same latch, and `src/battle.c` writes `pcFacing[0]` at 2961/3062/3266/3345/4283/4706 -- the home
re-aim `$96DB<-03` at f685 and the nearest-enemy re-aim at f716 are not among them.)

### AR.2.4 "the ROM re-stamps `$96DB` at arrival" is asserted, not observed here

The mechanism table and the citation list state the arrival re-stamp as fact ("engage `72 0D`, same
operands as C1/353B"). In THIS fight it is unobservable -- `$96DB` never changes between f217 and
f685, and the entry law from HOME `(160,70) -> (120,63)` already yields bearing `0x80` -> facing 2, the
same answer an arrival re-stamp from `(128,70)` would give. Section 5 OQ4 concedes this; the body does
not. The supporting evidence is `battle_facing_engine.md:116` (`bat3 f108 $96DB[0] <- 0`, geometry
`(128,71) -> (120,63)`), which I read but did **not** re-run. Treat the re-stamp as inherited, not
re-derived here. It matters: at foot Y=71 the two hypotheses DISAGREE (entry-law-from-home = 2,
arrival-re-stamp = 0), and Y=71 is exactly the pinned fixture's geometry.

### AR.2.5 the validation note is right but too weak, and the check list is incomplete

I measured the aim on EVERY pinned fixture (`saves/battle_re/*.raw.wram`), with our real traced feet:

| fixture | self foot | victim live foot | render anchor | facing (live) | facing (anchor) |
|---|---|---|---|---|---|
| forest_pre (approach f225) | (146,71) | (120,63) | (104,35) | 2 | 2 |
| forest_pre (strike f235-275) | (128,71) | (120,63) | (104,35) | **0** | **0** |
| forest_pre (recover f290) | (160,71) | (120,63) | (104,35) | 2 | 2 |
| gato_t0 (all traced frames) | (134,128/130/136/144) | (136,127) | (104,79) | **0** | **0** |
| **solo state (the user's fight)** | **(128,70)** | **(120,63)** | **(104,35)** | **2** | **0** |

So fix B moves **nothing** pinned -- not "may not move": **will not move**.
`field-battle-pc-approach-early/-late`, `-crit-flash1` (f235 = pose 3, strkFac), `-crit-flash2`,
`-crit-leap-mid`, `-crit-beatB-dead-suppressed`, `-hetake-*`, `-ambush-generic-dissolve` all keep
their fnv. The corollary the dossier does not draw: **the fix would be entirely unguarded**, and a new
check MUST use a `$29B3 = 06` (foot Y = 70) fixture -- the solo state -- or nothing in the suite can
ever regress it. This is a 1-pixel knife edge (`(128,70) -> (120,63)` = bearing `0x80` -> 2;
`(128,71) -> (120,63)` = `0xA0` -> 0).

For fix A the dossier names no check at all. Reading `tools/regress_checks.json`, **two move**:

* `field-battle-solo-entrance-no-marle` -- `CT_BATTLESHOT_AT=120` on `gato_solo.raw.wram`, i.e. inside
  Crono's `P3_spread` keyframe (t=342). Its pinned MUTATION targets the exact presence-gate line inside
  `battleEntranceRender` and would have to be re-authored against the new mechanism.
* `field-battle-forest-camera-composite` -- `CT_BATTLESHOT_FIELD_AT=90` is compared against
  `liveHost.frames` (`src/field.c:8965`), i.e. a BATTLE-HOST frame, so f90 sits inside the entrance
  window (P0 held) and the shot contains the baked Crono.

Unaffected: `field-battle-bat3-stance-facing` (the `CT_BATTLE_RENDERWRAM` path forces `hh.frames=400`,
past every `endF`, so the entrance layer is inert there).

### AR.2.6 minor factual slip

"`$966E` stride = 0x128" -- `$966E,X` is only the LOW byte. `C1/35F5-35FD` composes
`$A7 = $966E,X` / `$A8 = $A2C8,X`. Measured: `$966E = 28 28`, `$A2C8 = 01 01 01`, giving `0x0128`.
The oracle line quoted as `$966E stride = 0x128` in section 1 is really `$966E = 0x28`.

## AR.3 Forbidden-pattern audit of the proposed fixes

* Fix A (retire `src/data/battle_entrance.h` + the `CT_ENT_BOOT_F 222` anchor, drive from `$5DF1` /
  `CtBattlePcWinPose` / `CtBattlePcWinDwell >> 1` / 2-frame ticks): **structural, not a spot fix, and
  every constant is ROM-read.** Clean -- except that step 1 reads a byte our engine gets wrong
  (AR.2.1) and step 4's `$A09B` source is still underived (their OQ2; it reads `00 00 00` in every
  state I looked at, so that arm stays unexercised -- implementing it gated on the byte is fine,
  guessing its writer is not).
* Fix B variant 1: **a heuristic standing in for a ROM latch** (AR.2.3). Variant 2 is the
  mandate-clean one. Neither is a scene/enemy gate, and deleting
  `battleEnemyComputedAnchor`/`battleEnemyLiveDelta` from the aim is correct in both -- an OAM
  min-corner extent (`CT_EXT_HETAKE_{X,Y} = (-16,-28)`, `src/field.c:2693-2694`) has no counterpart
  among `C1/0222`'s operands (`$D3/$D4` = self `$1D0C/$1D23`, `$D5/$D6` = target `$1D0C/$1D23`,
  verified at `C1/3523-3539`).
* **The same anchor-as-position mistake is repeated twice more and the dossier names neither:**
  `src/field.c:4391` (Marle close-melee `mfac`) and `src/field.c:4406` (crossbow `cfac`) both do
  `battleEnemyComputedAnchor + battleEnemyLiveDelta -> CtBattleAimFacing`. Fixing only 4274-4278
  leaves the shared mistake alive in two more call sites -- by the mandate's own "fix the MECHANISM"
  rule, all three must go together.

## AR.4 Repro commands used in this review

```
P=%TMP%/ctwf3/facing-adv/ct_probe.exe ; F=%TMP%/ctwf3/facing-adv/ct_field.exe   # snapshots of build/
# entry ladder + staging (all values in AR.1)
$P --monitor "9907:1,96DB:1,9245:8,922D:8,5DF1:6,29B0:5,96A4:5,A4DC:1,966E:2" \
   --from saves/battle_crono_solo_lake.state --input "40:400" --diff --writer --frames 330
# tick cadence, stride hi byte, the $A09B arm, the terminator hold
$P --monitor "A2C8:3,A09B:3,9873:1,5DBD:3,5DC1:3,5DDD:2" \
   --from saves/battle_crono_solo_lake.state --input "40:400" --diff --writer --frames 300
# the field->battle source bytes
$P --monitor "0F05:1,1604:1,0197:1,29B4:1" --from saves/battle_crono_solo_lake.state \
   --input "40:400" --diff --writer --frames 220
# the swing
$P --monitor "9907:1,96DB:1,1D0C:8,1D23:8,9873:1,29BF:12" --from saves/battle_crono_solo_lake.state \
   --input "40:120,0:280,100:3,0:20,100:3,0:400" --diff --frames 800
# pixels
$P --peek saves/battle_crono_solo_lake.state or210.raw --btnscript "40:120,0:90"   # -> f210
$P --peek saves/battle_crono_solo_lake.state or245.raw --btnscript "40:120,0:125"  # -> f245
python tools/ct.py fb2png or210.raw or210.png    # then crop (140,40,44,44) x8
# OURS: staged record (the AR.2.1 measurement)
CT_DUMPWRAM=s.wram CT_DUMPWRAM_AT=200 CT_BATTLE_STAGEDBG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  $F --assets ct_assets.dat --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram \
     --selftest o.raw --frames 260 --walkscript "l:120,."
# OURS: the swing trace (one run per frame -- the shot callback composes only at CT_BATTLESHOT_AT)
CT_ANIMENG_PCTRACE=1 CT_BATTLE_MASHA=1 CT_BATTLESHOT=e.raw CT_BATTLESHOT_AT=245 SDL_VIDEODRIVER=dummy \
  $F --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
# OURS: the entrance pixels
CT_BATTLE_MASHA=1 CT_BATTLESHOT=ours60.raw CT_BATTLESHOT_AT=60 SDL_VIDEODRIVER=dummy \
  $F --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
```

## AR.5 Open questions added by this review

1. Why is our staged `$29B4 = 01` while our live `$0F01(obj2)` reaches `00`? The `--battletest`
   comment (`src/field.c:8324-8331`) calls it a one-frame drain lead and declares it out of scope --
   but fix A depends on that byte, so the scope limit has to be revisited or the fix bounded.
2. Our `$1604(obj2)` stays `02` through the whole ambush; the oracle's script drives it
   `02 -> 03 -> 01 -> 00` (writers C0:4FBD / C0:5550 / C1:CE04). Which field-VM op is missing, and does
   it also move `$0F01`?
3. Is the arrival `$96DB` re-stamp real? Re-run bat3 (`battle_facing_engine.md` section 4, f108) before
   any fix that depends on it -- it is the only discriminator between "entry law from home" and
   "re-stamp at arrival", and they disagree at foot Y = 71.
4. `$5DDD` / `$5DDE` (`C1/36AC`, `C1/36EF`) read `01 00` here; their writers and the idle stepper's
   start gate are still underived (this dossier's OQ1 second half).
5. `$A09B` writer: still unknown (this dossier's OQ2), reads `00 00 00` in every state examined.

---

# Field-turn pass (session 3)

**Verdict: AR.2.1's cause (ii) is REFUTED as stated, and cause (i) is now ROOT-CAUSED and is not a
"one-frame drain lead".** The ambush script's turns ARE reproduced by our field engine, at the same
frames, with the correct DOWN facing -- but only in the C struct `sc->actors[].facing`. The ROM
routine that turns the field facing into the staged pose (`C0/CAD9` -> `C0/CC12`, the animation
drain) **is ported and does run every frame in `ct_field.exe`** (`src/ct_vm.c:4090` ->
`CtAnimDrainVisit`, `src/move.c:5144`), and it reads WRAM `$1600,X` / `$1680,X` / `$1681,X`.
`src/ct_vm.c` writes **none** of those three -- `$1600,X` appears in it exactly once, as a READ at
seed time (`src/ct_vm.c:3127`). So the drain publishes `$0F01` for a facing that never changed, and
the staging (which runs BEFORE the render mirror, exactly as the ROM's mainloop does) reads it.

Everything below was re-measured this session from my own snapshots
(`%TMP%\ctwf4\turns\ct_probe.exe`, `ct_field.exe`), on `saves/battle_crono_solo_lake.state` and
`saves/forest_re/battle_crono_solo.dumpwram`.

## S3.1 WHICH script grabs Crono at oracle f113: **op 0xD9**, handler **C0/326C**

`--monitor "1184:2,1C04:1,1604:1,0F04:1,011F:1" --from saves/battle_crono_solo_lake.state
--input "40:400" --diff --writer` (IP words print in byte order, i.e. `1F17` = `0x171F`):

| f | obj2 IP `$1184` | obj2 prio `$1C04` | `$1604` | writer |
|---|---|---|---|---|
| f0 | `0x062F` | 07 | 02 | -- |
| **f112** | **`0x171F`** | **00** | 02 | `C0:32F4` / `C0:32FB`; `$011F` by `C0:32C8` |
| f113 | `0x1726` | 00 | **03** | `C0:4FBD` (op-0x96's `4FB5 LDA $F700,X / 4FBA STA $1600,X`) |
| f123 | `0x172B` | 00 | **01** | `C0:5550` (op-0xA6's `554D STA $1600,X`) |
| f124 | `0x062F` | 07 | 01 | `C0:5FA3` (`5FA0 INC $1C00,X`, the prio-stack POP) |

`$1180,X = 0x171F` is a **literal immediate in the ROM**: `C0/32EE LDA #171F / 32F1 STA $1180,X`.
It is not in scene 19's script body -- it points at a **canned per-PC block in WRAM**.

**The op.** Jump table `C0/5D6E` (`handler = tbl[op*2]`): **op `0xD9` -> `C0/326C`** (neighbours:
op `0xB0` -> `C0/3002`, op `0xDA` -> `C0/345A`). `C0/326C` is:

```
326C: STY $C7                        ; the op's own IP
326E: LDA $7F03FC / 3272 BEQ ->32C8  ; sequencer: 0 = FIRST visit -> install
3274: CMP #04 / 3276 BCS ->327B      ; >=4 -> the completion arm
3278: TYX / CLC / RTS                ; else PARK on this op (still walking)
32C8: STZ $1F                        ; <== $011F: player control OFF
32CA: LDX $97                        ; $97 = the LEAD PC's object index
32CC-32E8: save $1180,X into $7F0580 + (prio*0x80 + obj)      ; the priority stack
32EE: LDA #171F / 32F1 STA $1180,X   ; <== REPOINT the PC at the canned block
32F6: LDA #00 / 32F8 STA $1C00,X     ; prio 0
32FB: STZ $1A80,X / 32FE STZ $1A01,X / 3301 STZ $1001,X       ; drop any in-flight move
3306: LDX $C7 / INX
3309: LDA $7F2001,X / 330D STA $DB / 330F AND #7F / 3311 STA $7F3728   ; operand1 -> MOVETO x
3315: INX / 3316 LDA $7F2001,X / 331A STA $DD / 331C AND #7F / 331E STA $7F3729  ; operand2 -> MOVETO y
3322: LDA $DB / 3324 BMI ->3332
3326:   LDA $DD / 3328 BMI ->332E
332A:     LDA #00 / 332C BRA ->333C   ; x bit7=0, y bit7=0 -> facing 0 (UP)
332E:     LDA #01 / 3330 BRA ->333C   ; x bit7=0, y bit7=1 -> facing 1 (DOWN)
3332: LDA $DD / 3334 BMI ->333A
3336:     LDA #02 / 3338 BRA ->333C   ; x bit7=1, y bit7=0 -> facing 2 (LEFT)
333A:     LDA #03                     ; x bit7=1, y bit7=1 -> facing 3 (RIGHT)
333C: STA $7F372B                     ; <== the FACE op's operand
3342-3376: the SAME for $99 (PC1) -> block #173F, operands $7F3748/$7F3749/$7F374B
3346: LDA #03 / STA $7F03FC          ; PC1 absent (solo) -> sequencer := 3, park
       (3rd PC -> #175F by symmetry; C0/3222-3269 installs #177F on $EB -- a different, touch-side path)
32A2/32AC/32B6: LDA $1C00,X / BEQ ->3278   ; any PC still at prio 0 -> PARK
32B8: STA #00 -> $7F03FC ; 32BE INY x7 ; 32C5 TYX / CLC / RTS   ; IP += 7 = D9 + 6 operands
```

**The canned block** (`--read "7F36E0+192"` on the oracle; IP = addr - `$7F2001`):

```
ip=171F  E3 00 89 40 0D 00 AE 96 XX YY A6 FF 71 FE 12 FE 04 04 04 E3 00 00 11 09 96 00 00 FF 9D 00 74 5F
ip=173F  (identical, stride 0x20)      ip=175F  (identical)      ip=177F  96 00 00 FF 9D 00
```
i.e. `E3 00` lock, `89 40` speed 0x40, `0D 00` collision gates, `AE`, **`96 x y` MOVETO**,
**`A6 f` FACE**, then the tail that returns. The 16 bytes `E3 00 89 40 0D 00 AE 96 00 00 A6 00 71 FE
12 FE` are **byte-identical to ROM `C0/5D58`** (ROM file offset `0x005D58`, immediately below the op
jump table at `C0/5D6E`); the routine that copies them into `$7F371F/$7F373F/$7F375F` was NOT located
(open question S3.5.1).

**This fight's operands.** Scene 19 obj1's poll loop, loc[11] branch (`loc[11] = $7F0216`, set to
`0x0001` at f1 by `C0:6CD2`) -- obj1 IP `$1182` walks `0x0096 -> 0x0099 -> 0x009C -> 0x009F ->
0x00A2 -> 0x00A9` at f4/f20/f32/f44/**f112**/**f124**. Body `0x00A2` is
**`D9 26 A8 23 A8 28 A7`** (7 bytes -> `0x00A9` = `D8 02 00`, the BATTLE op -- exactly where
`field_move_stale_heading.md` section 2 places it).

* Crono = operand pair `(0x26, 0xA8)`: MOVETO tile `(0x26, 0x28)` = (38,40); x bit7 = 0, y bit7 = 1
  -> **FACE = 1 = DOWN**. That is the user's *"crono should start looking down"*, derived.
* Sequencer measured: `$7F03FC` 00 -> **03** at f112 (writer `C0:334C`) -> **00** at f124 (`C0:32BE`).

## S3.2 Our engine DOES run it -- the turns are reproduced, in the C struct only

`CT_VMTRACE=1 ct_field --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest
o.raw --frames 260 --walkscript "l:200,."`, obj1 address transitions:

| | 0x0096 | 0x0099 | 0x009C | 0x009F (E7 pan) | **0x00A2 (D9)** | **0x00A9 (D8)** |
|---|---|---|---|---|---|---|
| ORACLE | f4 | f20 | f32 | f44 | **f112** | **f124** |
| OURS | t5 | t9 | t21 | t33 | **t113** | **t125** |

`CT_CATTRACE=2`, same run: obj2 walks x 141 -> 160 over f111-f119 with `face=3(R)`, **arrives f120
with `face=1(D)`**, and holds DOWN through f123. Our op-0xD9 (`src/ct_vm.c:2455`) decodes the
operands exactly as `C0/3322-333C` does (`faceB = ((bx>>6)&2) | ((by>>7)&1)`, line 2480; `0x26,0xA8`
-> 1) and stamps `sc->actors[po].facing = faceB` on arrival (line 2487).

**So AR.2.1's cause (ii) -- "our field `$1604` never follows the ambush script's turns" -- is a
MEASUREMENT ARTEFACT.** `$1600,X` is a WRAM mirror our field VM does not maintain: every facing write
in `src/ct_vm.c` targets `a->facing` (lines 870-874, 1137-1138, 1223, 1324-1328, 1472, 1837, 2487,
3864, 4361) and `$1600,X` is read exactly once, at seed (`src/ct_vm.c:3127`). Ditto `$1804`
"oscillating": that is the D9 walk RIGHT (141->160) followed by the post-battle walk LEFT (the
walkscript is still holding LEFT), not a failure to reach the trigger.

## S3.3 THE ACTUAL ROOT CAUSE of `$29B4 = 01`: the ported drain reads WRAM, the VM writes a struct

`$0F01,X` is committed by **`C0/CAD9` -> `C0/CC12-CC14`**, and that routine **is ported and live**:
`src/ct_vm.c:4090 CtAnimDrainPass(...)` (ROM `C0/CA76`, called from the mainloop at `C0/00D6`) ->
`src/move.c:5144 CtAnimDrainVisit` -> `src/move.c:4991-4995` (`if (fb == r[0x0F01+idx]) return 0;` /
`r[0x0F01 + idx] = fb;` = `CC04/CC07` + `CC12-CC14`). Its frame resolution reads the **facing byte
`$1600,X`** (`src/move.c:902 int facing = r[0x1600 + idx];`) and the anim cells `$1680/$1681/$1601`
(`C0/CB3A`: `CB7A LDA $1681,X`, `CBB1 LDA $1680,X`).

Per-frame inputs of that drain, obj2, both engines:

| f | ORACLE `$1604 $1684 $1685 $1605` -> `$0F05` | OURS `$1604 $1684 $1685 $1605` -> `$0F05` |
|---|---|---|
| 118-119 | `03 01 01 02/01` -> `11` | `02 00 00 04/03` -> `12` |
| 120-122 | `03 01 02 07..05` -> `12` | `02 00 00 03..01` -> `12`, `02`, `00` |
| **123** | **`01 00 00 05` -> `00`** (writers `C0:5550`, `C0:569F`, `C0:CA6D`, `C0:CC17`) | **`02 00 00 05` -> `01`** |
| 124-195 | `01 00 00` -> **`00` held to f195** | `02 00 00` -> `00`, `0B`, ... (oscillates, then the walk resumes) |

The oracle's f123 row is the whole mechanism in one line: the FACE op writes `$1600,X = 01`
(`C0/554D`) **and** `STZ $1601,X` (`C0/5550`); the anim-stop helper `C0/5690-56A2` clears the walk
anim (`5693 LDA $1680,X` / `569C STA $1680,X = 0` / `569F STZ $1681,X` / `56A2 STZ $1601,X`); the
drain then publishes the facing-1 standing record **`00`** and holds it. Ours never moves
`$1604`/`$1684`, so the drain keeps resolving the **facing-2 (LEFT)** set and lands on **`01`** --
and `01` is precisely the facing-2 standing record of the ROM's own `+0x00` window (`03/00/01/02`),
which is why AR.2.1 saw a LEFT-facing byte.

**Why the staging sees the drain's byte and not our render's.** Our render mirror
(`src/field.c:8724`, `CtSceneMem7E[0x0F01 + pObj*2] = af`) computes the *visually correct* value --
`CT_PDBG` prints `[p f121..f123] pfa=1 af=0`, i.e. our composited Crono really does face DOWN -- but
it runs LATE in the frame, and the ROM's own mainloop order is:

```
C0/0097 (loop top)
  00A1 JSR C0/18D9   <== the $0117 transfer consumer -> 1890 JSR C0/024C = THE BATTLE STAGING
  00A7 JSR C0/881E ... 00B7 JSR C0/00BF -> 00C1 JSR C0/59D9  (event VM)
                                           00D3 JSR C0/A810  (activation)
                                           00D6 JSR C0/CA76  (THE ANIM DRAIN -> CC17 commits $0F01)
```
so the staging at frame N canonically reads the `$0F01` the drain committed at frame N-1. We keep
that order (`src/field.c:8206` VM+drain, `:8212` consumer, `:8600+` compose), which is correct -- the
byte it reads is simply produced by an anim state the field VM never updates. Measured, ONE run,
`CT_PDBG=1 CT_BATTLE_STAGEDBG=1 CT_DUMPWRAM_AT=123`: `[p f122] af=0`, then `[dumpwram@f123]` reads
`$0F05 = 01` (the drain overwrote it at the top of f123), then `[stage] PC seat0 obj2` -> `$29B4 = 01`.

So **AR.2.1's "one-frame `$0F01` drain lead" diagnosis is wrong**: a one-frame lead would have staged
`00` (our compose wrote `0` at both f121 and f122). The staged byte is not late -- it is produced by
a **second, un-synced anim engine**. `--battletest`'s scope-limit comment (`src/field.c:8324-8331`)
excludes byte +4 for a reason that does not hold here.

## S3.4 Answers to the four questions asked

1. **Which script / what op.** op `0xD9` at scene-19 obj1 body `0x00A2` (`D9 26 A8 23 A8 28 A7`),
   handler `C0/326C`. It pushes each PC's IP onto the `$7F0580` prio stack and repoints it at the
   canned WRAM block `0x171F` (`96 x y` MOVETO -> `A6 f` FACE), patching the MOVETO operands from
   `operand & 0x7F` and the FACE operand from the operand SIGN BITS. Crono -> tile(38,40), FACE 1 =
   DOWN. The `C0:4FBD` writer at f113 is that block's MOVETO; the `C0:5550` writer at f123 is its FACE.
2. **Why ours does not run it.** It DOES run it, at the same frames, with the same facing. Nothing in
   the op-0x04 CALL-park path (R6 of `field_move_stale_heading.md`), the touch-preempt path, or the
   walk-to-trigger path diverges in this scenario -- the obj1 timeline matches op-for-op (S3.2). The
   FIRST divergent RAM byte is `$1604` (`$1600,X`) at oracle f113 (`C0:4FBD`) -- ours never writes it
   -- with `$1684` (`$1680,X`, `C0:569C`) right behind it at f123.
3. **Cause (i) vs (ii).** Neither, as framed. The field turns ARE reproduced (so (ii) is refuted as
   stated), and the residue is not a drain LEAD (so (i) is refuted as stated). The live defect is a
   **WRAM-mirror gap**: `src/ct_vm.c` keeps facing/anim in C fields while the ported ROM drain
   (`src/move.c`) reads `$1600,X` / `$1680,X` / `$1681,X` / `$1601,X`.
4. **Can fix A produce `0x00` now?** Not yet -- with today's tree step 1 publishes `0x01`. It will,
   with **no change to fix A itself**, once the field VM writes the facing/anim bytes the ROM writes.
   The change is structural, not scene-scoped, and every store is already cited AND already ported in
   the sibling engine:
   * op-0x96 MOVETO re-issue: `C0/4FB5 LDA $F700,X / 4FBA STA $1600,X` (plus `4FCA/4FCF`, the second
     write on the object-solid sidestep arm) -- `src/move.c:3843` / `:3847`.
   * op-0xA6 FACE and the `0x0F/0x17/0x1B/0x1D` family: `C0/554D STA $1600,X` **and `C0/5550 STZ
     $1601,X`** -- `src/move.c:4598` / `:4608` / `:4620`.
   * the anim-stop helper `C0/5690-56A2`: `$1680,X = 0`, `STZ $1681,X`, `STZ $1601,X`.
   `src/move.c` carries all of them; `src/ct_vm.c` carries none. Closing that gap is what the
   mandate's "fix the MECHANISM" clause asks for -- it is ONE shared mirror, not a battle-entry patch,
   and it will move every drain-fed byte (`$0F01`, the OAM assembly, `$1B00`) for every scripted
   actor. Expect the `--vmdiff` set and every `$0F01`-sensitive pin to move; grade before and after.
5. **The SWING half is untouched by this session.** Nothing here bears on defect B. AR.2.3 stands
   (branch on the `$96DB` latch -- `h->pcFacing[0]` -- not on a per-frame recompute), and AR.3's
   corollary stands: `src/field.c:4274-4278`, `:4391` (Marle close-melee) and `:4406` (crossbow) all
   feed `battleEnemyComputedAnchor + battleEnemyLiveDelta` into the aim and must go together.
   AR.2.5's warning is unchanged: nothing pinned guards it (`$29B3 = 06`, foot Y = 70, is the solo
   state only).

## S3.5 Open questions added by this pass

1. **The copier of the canned blocks.** The 16 bytes at WRAM ip `0x171F`/`0x173F`/`0x175F` are
   byte-identical to ROM `C0/5D58`, but the routine that writes them into `$7F371F+` was not found
   (there is no `JSR/JMP/JSL $32C8` anywhere in the ROM to search back from -- `C0/326C` reaches it
   by `BEQ`). The blocks read as shown in a state that has never run a D9, so they are laid down at
   scene/boot time, not by the op.
2. **`C0/3222-3269` installs `#177F`** on `$EB` (a 6-byte `96 00 00 FF 9D 00` block) from the
   `C0/3054 -> C0/3154` path -- the touch/bump side. Not exercised here; not derived.
3. **Our idle drain oscillates where the oracle's is constant.** After the settle the oracle holds
   `$0F05 = 00` from f123 to f195; ours cycles `00/01`. Once `$1600,X` is mirrored this may resolve
   by itself (same anim set, right facing), but it was NOT verified and may be a second
   `$1601`/`$1681` reset gap (`C0/5550`, `C0/569F`, `C0/56A2`).
4. **Two anim engines for the lead.** With the drain fed correctly, `src/field.c`'s `animTickActor`
   render path and the ported ROM drain will both be producing a pose for the lead PC. Which one the
   ROM's renderer actually consumes -- and whether the render mirror at `field.c:8724` should exist
   at all -- is not derived here.

## S3.6 Repro commands used in this pass

```sh
P=%TMP%/ctwf4/turns/ct_probe.exe ; F=%TMP%/ctwf4/turns/ct_field.exe     # snapshots of build/
# WHO repoints Crono, and to what
$P --monitor "1184:2,1C04:1,1604:1,0F04:1,011F:1" --from saves/battle_crono_solo_lake.state \
   --input "40:400" --diff --writer --frames 200
# which ambush + the D9/D8 pair + the sequencer
$P --monitor "7F0214:2,7F0216:2,7F0218:2,1182:2,11B0:2,7F03FC:1" --from saves/battle_crono_solo_lake.state \
   --input "40:400" --diff --writer --frames 210
# the canned blocks
$P --read "7F36E0+192" --from saves/battle_crono_solo_lake.state
# the drain's inputs and its output
$P --monitor "1604:1,1684:1,1605:1,1685:1,0F05:1,1B04:1" --from saves/battle_crono_solo_lake.state \
   --input "40:400" --diff --writer --frames 130
# ROM
python tools/dis.py 326C 0x120 ; python tools/dis.py 5545 0x30 ; python tools/dis.py 5690 0x20
python tools/dis.py 00A0 0x50  ; python tools/dis.py CB3A 0x50 ; python tools/dis.py 5F70 0x40
#   op table: handler = word at (C0/5D6E + 2*op)  ->  0xD9 = C0/326C, 0xA6 = C0/5545, 0xD8 = C0/3A83
# OURS: the timeline, the turn, the anim inputs, the staged byte
CT_VMTRACE=1  ... $F --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest o.raw --frames 260 --walkscript "l:200,."
CT_CATTRACE=2 ... (same)          # -> f120 face=1(D)
CT_PDBG=1 CT_BATTLE_STAGEDBG=1 CT_DUMPWRAM=q123.wram CT_DUMPWRAM_AT=123 ... --frames 128
#   -> [p f122] af=0  /  [dumpwram@f123] $0F05=01  /  [stage] PC seat0 obj2 -> $29B4=01
```

---

# Adversarial review (session 3)

Read-only, independent re-run of every probe and re-disassembly of every cited address, on my own
snapshots (`%TMP%\ctwf4\turns-adv\ct_probe.exe` / `ct_field.exe`, copies of `build/`).

**VERDICT: the MECHANISM is CONFIRMED (DERIVED) and strengthened by a discriminator the derivation
did not have. The PROPOSED FIX IS NOT LANDABLE AS WRITTEN: its enumerated site list does not contain
the site that sets the facing in this fight, so its own acceptance numbers are unreachable from the
edits it lists.**

## S3A.1 What re-verified CLEAN (ROM)

Every instruction cited in section S3 holds at the cited address:

| claim | re-measured |
|---|---|
| op table `C0/5D6E`, handler = word at `+2*op` | `0xD9 -> C0/326C`, `0xA6 -> C0/5545`, `0xD8 -> C0/3A83`, `0x96 -> C0/4F61`, `0x97 -> C0/5084`, `0x0F/0x17/0x1B/0x1D -> C0/5535/5539/553D/5541` |
| `C0/326C` shape | `326C STY $C7` / `326E LDA $7F03FC` / `3272 BEQ ->32C8` / `3274 CMP #04` / `3278 TYX,CLC,RTS` (park) |
| the install arm | `32C8 STZ $1F` / `32CA LDX $97` / `32E3 LDA $1180,X` -> `32E8 STA $7F0580,X` / **`32EE LDA #171F` / `32F1 STA $1180,X`** / `32F6 LDA #00` / `32F8 STA $1C00,X` / **`32FB STZ $1A80,X` / `32FE STZ $1A01,X` / `3301 STZ $1001,X`** |
| operand -> canned MOVETO | `3309/330D/330F/3311` -> `$7F3728`; `3316/331A/331C/331E` -> `$7F3729` |
| facing from the SIGN BITS | `3322 LDA $DB / BMI`, `3326 LDA $DD / BMI` -> `#00 / #01 / #02 / #03` -> `333C STA $7F372B`. x0y0=UP x0y1=DOWN x1y0=LEFT x1y1=RIGHT |
| solo park | `3342 LDA $99 / 3344 BPL` -> `3346 LDA #03 / 3348 STA $7F03FC` (writer PC `334C`) |
| op 0xA6 + the immediate family | `5545 INY/TYX/LDA $7F2001,X` and `5535/5539/553D/5541 LDA #imm` all fall into `554B LDX $6D` / **`554D STA $1600,X`** / **`5550 STZ $1601,X`** |
| the anim-stop helper | `5693 LDA $1680,X` / `5698 BEQ` / `569C STA $1680,X` (=0) / **`569F STZ $1681,X`** / **`56A2 STZ $1601,X`** |
| op-0x96 facing store | `4FB5 LDA $F700,X` / `4FB8 LDX $6D` / **`4FBA STA $1600,X`**; and after `4FBD JSR C0/5B95 / 4FC0 BCC`, the SECOND write `4FC5 ADC #30` / `4FCA LDA $F700,X` / **`4FCF STA $1600,X`** |
| prio-stack pop | `5FA0 INC $1C00,X` (writer PC `5FA3`) |
| the drain commit | `CC04 CMP $0F01,X` / `CC07 BNE` / `CC0D JSR C0/E952` / `CC10 BCC` / `CC12 LDA $EE` / **`CC14 STA $0F01,X`** (writer PC `CC17`) |
| field->battle transfer | `C0/041A`: `042D $1801,X-$DB -> $7E29B0`, `043C $0A00,X&0F -> $29B2`, `0446 $1881,X-$DD -> $29B1`, `044F $0A80,X&0F -> $29B3`, **`0453 LDA $0F01,X` / `0456 STA $7E29B4`** |
| battle staging / publish | `CC/E965 LDA $96A8,X` / `E968 STA $A4DC,Y` / `E96B TDC` / `E96C STA $96DB,Y`; `C1/34AF LDA $96F5,X / BEQ`, `34B4/34B7 -> $9679`, **`34BA LDA $A4DC,X` / `34BD STA $967B`**, `34C0 JSR C1/1C55` |

## S3A.2 What re-verified CLEAN (oracle)

`--from saves/battle_crono_solo_lake.state --input "40:400" --diff --writer`, my own snapshot:

```
--monitor "1184:2,1C04:1,1604:1,0F04:1,011F:1"
  f0    $1184=062F $1C04=07 $1604=02
  f112  $1184=171F (C0:32F4)  $1C04=00 (C0:32FB)          <- the D9 install
  f113  $1184=1726            $1604=03 (C0:4FBD)          <- the canned MOVETO's F700 facing
  f123  $1184=172B            $1604=01 (C0:5550)          <- the canned FACE
  f124  $1184=062F (C0:5AC2)  $1C04=07 (C0:5FA3)          <- the prio-stack pop
--monitor "1604:1,1684:1,1605:1,1685:1,0F05:1,1B04:1"
  f94..f112   $1604=02 $1684=00  ->  $0F05 = 01   (constant, 19 frames)
  f113..f119  $1604=03 $1684=01 (C0:5657) $1685=01  ->  $0F05 = 11 (C0:CC17)
  f120..f122  $1604=03 $1684=01 $1685=02            ->  $0F05 = 12
  f123        $1604=01 (C0:5550) $1684=00 (C0:569F) ->  $0F05 = 00 (C0:CC17), HELD to f195
--monitor "29B0:5,2989:1,9907:1"
  f128 $2989=02 (C0:3A99)   f131 $29B0 = 0A 04 00 06 **00** (C0:045A)   f204 $9907=00 (C1:1C69)
--read "7F371F+32"  ->  AE **E3 00 89 40 0D 00 AE 96 00 00 A6 00 71 FE 12 FE** 04 04 04 E3 00 00 11 09 ...
```

So the whole S3 timeline, the writers, the canned block and the staged `00` reproduce exactly.

## S3A.3 THREE NEW MEASUREMENTS the derivation did not have

**(a) A causal discriminator that isolates the drain.** `CT_FRAMELEGS` toggles the ported
`C0/CA76` drain (`src/ct_vm.c:518-521` `legsOn()`; note the default is **ON**, not "Default OFF" as
`docs/DEBUG_SURFACE.md:314` still says). Same run, same frame, the only difference being the drain:

```
CT_DUMPWRAM=q123.wram CT_DUMPWRAM_AT=123 ct_field --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest o.raw --frames 130 --walkscript "l:120,."
  CT_FRAMELEGS unset (drain ON) :  $0F05 = 01   $1604=02 $1684=00 $1605=05 $1685=00
  CT_FRAMELEGS=0   (drain OFF) :  $0F05 = 00   $1604=02 $1684=00 $1605=04 $1685=00
```
With the drain off, the only surviving writer of `$0F05` is the compose mirror (`src/field.c:8724`),
and it stages the oracle's `00`. With the drain on it stages `01`. **The ported drain, fed an
un-mirrored `$1600,X`, IS the writer of the wrong byte** -- measured, not inferred. S3.3 is right.

**(b) The acceptance number is reachable, and it is reachable on the ORACLE's own drain.** The
oracle publishes `$0F05 = 01` for `($1604=02,$1684=00)` for nineteen consecutive frames (f94-f112)
and `$0F05 = 00` for `($1604=01,$1684=00)` from f123. **`01` is not our bug's private value -- it is
the ROM's own answer for facing 2.** The only wrong input is `$1600,X`; the drain itself agrees.

**(c) Our engine's staged record is already byte-exact except for +4.** `CT_BATTLE_STAGEDBG`:
`[stage] PC seat0 obj2 rec=0A 04 00 06` vs the oracle's `0A 04 00 06 00`.

## S3A.4 REFUTED / NOT PROVEN

### AR3.1 (LOAD-BEARING) the fix's site list misses the site that turns Crono in this fight
`ct_field.exe`'s op-0xD9 is a **REIMPLEMENTATION, not a port**, and says so in its own comment
(`src/ct_vm.c:2455-2500`): *"The ROM saves each PC's IP to the $7F0580 stack and forces the global
walk routine 0x171F ... our port drives each PC's mover from this visit instead (no repoint)"*.
Measured on our f123 WRAM dump: **`$1184 = 062F` and `$1C04 = 07`** -- our obj2's IP is never
repointed and its prio never drops, where the oracle holds `171F` / `00` from f112 to f124.
Consequently **our engine never executes op 0x96 or op 0xA6 for Crono in this fight at all**; the
facing is stamped directly at `src/ct_vm.c:2487` (the op's own arrival arm) and `:3864` (the
per-frame `d9Active` arrival in `run_tasks`), with the walk facing coming from `mvStartStep`'s
`T[0x400+angle]` at `:1472`.

Therefore fix items 1-3 (op-0x96 re-issue, op-0xA6 FACE, the anim-stop helper) **leave `$1604 = 02`
in this scenario**, and the stated acceptance criterion ("ours must show `$1604 = 03` at f113-f122,
`01` at f123") is **unreachable from the edits the fix enumerates**. Two honest ways out, and the
dossier picks neither:

* **(i)** also mirror at `ct_vm.c:1472 / :2487 / :3864`. These have **no ROM counterpart at those
  sites** -- the ROM writes `$1600,X` from `C0/4FBA` inside op 0x96 and from `C0/554D` inside op
  0xA6, both reached *through* the canned block. A mirror bolted onto a reimplemented D9 is a
  stand-in for the ROM's mechanism, i.e. exactly the pattern the mandate forbids, even though the
  byte it produces is right.
* **(ii)** port `C0/326C` for real: `$1180,X := 0x171F`, `$1C00,X := 0`, the `$7F0580` prio stack,
  `STZ $1A80/$1A01/$1001,X`, the operand patch into `$7F3728/29/2B`, the `$7F03FC` sequencer, and
  let the already-correct op-0x96/op-0xA6 handlers do the work. That requires the field VM to
  execute script bytes out of the `$7F` WRAM image instead of the scene body -- a real architectural
  lift, and the only mandate-clean option. **Not derived here whether `ct_vm.c` can do that.**

### AR3.2 the ROM frame-order citation is WRONG, and ours is the OPPOSITE of what is claimed
S3.3 states `C0/0097` -> `00A1 JSR C0/18D9` *"(-> 1890 JSR C0/024C = THE BATTLE STAGING)"*.
Measured: `C0/18D9` never reaches `C0/1890`. A byte scan of bank `$C0` finds

* `JSR C0/024C` at exactly **`C0/1890`** and **`C0/2628`** (nowhere else);
* `C0/1890` is entered by **`BRL` from `C0/1776`**, and `C0/1770` is `LDA $17 / BIT #20 / BNE` --
  **the `$0117` transfer consumer**, entered from `C0/16E2` (BRL) / `C0/16FE` (BRA), not from `C0/00A1`;
* `JSR C0/041A` has exactly ONE caller, **`C0/0392`**, inside the block at `C0/038F`, which is
  called from **`C0/0265`** -- inside `C0/024C`.

So the staging hangs off the `$0117` consumer, whose position in the mainloop is **not established
in this dossier**. Worse, the dossier says *"We keep that order (`src/field.c:8206` VM+drain,
`:8212` consumer ...), which is correct"* while describing the ROM as staging BEFORE the VM: 8206
before 8212 is **drain-then-stage**, the opposite of the stage-then-drain it claims to be matching.
**Not fatal to this scenario** -- measured, the oracle holds `$0F05 = 00` from f123 to f195, so any
offset of a few frames stages the same byte -- but "the ordering is not the bug" currently rests on a
citation that does not hold. Re-derive the consumer's mainloop position before relying on it.

### AR3.3 "src/ct_vm.c writes none of those three" is false
`src/ct_vm.c:4030-4031` writes `$1681,X` and `$1601,X` (`C0/A8F7` / `C0/A8FA`), and `:4033` writes
`$0F01,X = 0xFF` (`C0/A902`). Only **`$1600,X` and `$1680,X`** are genuinely never written. The
conclusion survives; the sentence does not.

### AR3.4 the canned block's ROM address is C0/5D50, not C0/5D58
`rom[0x5D50:0x5D60] = E3 00 89 40 0D 00 AE 96 00 00 A6 00 71 FE 12 FE` -- byte-identical to WRAM
`0x7F3720` (ip `0x171F`). `0x5D58` starts **eight bytes into** that block.

### AR3.5 the grader named for the fix is BLIND to the fix
`--vmdiff` does run `CtSceneTick` (`src/main.c:519`) so it can move -- but it grades through a WRITE
MASK, and a byte our engine has **never written** is not in our mask. `$1600,X` / `$1680,X` are
precisely such bytes, which is the structural reason this gap survived every green run (the
`ct-sweepops-blindness-classes` failure mode). "Expect the whole `--vmdiff` set to move" is
therefore not a criterion: it may move for unrelated reasons and it may not move at all where it
matters. What the fix needs is a check that pins **`$1604` / `$1684` / `$0F05` / `$29B4` at the
op-0xD9 completion EVENT**, not at a frame number -- the more so because our own counters disagree:
`[stage]` prints at field `f123` while `CT_VMTRACE` shows obj1 taking op 0xD8 at `t128` and
`[vm f132]` numbering a third way. Frame-pinned acceptance numbers here are fragile by construction.

### AR3.6 minor mis-mappings
* op 0xA6 is mapped to `src/move.c:4598`; `:4598` is the op `0x1E/0x1F/0x25/...` family that writes
  **another object's** facing (`r[0x1600 + tgt]`). Self-targeting 0xA6/0xA7 is `:4608`, the
  immediate family `0x0F/0x17/0x1B/0x1D` is `:4620`.
* `C0/4FCA/4FCF` is called "the object-solid sidestep arm"; the branch is `4FBD JSR C0/5B95 / 4FC0
  BCC` (`CtCollisionActorMovable`) plus `ADC #30`. The label is plausible, not derived here.
* `docs/DEBUG_SURFACE.md:314` ("`CT_FRAMELEGS` ... **Default OFF**") and `CMakeLists.txt:63-73`
  ("nothing in field.c calls it yet ... none of it runs live") are both **stale**: `legsOn()`
  defaults ON and `src/ct_vm.c:4090` calls `CtAnimDrainPass` every tick. Those two stale notes are
  why a live second anim engine went unnoticed.

### AR3.7 the double-writer is not closed by the fix
`src/field.c:8628` mirrors `$0F01,X` for **every** non-lead actor and `:8724` for the lead, both
AFTER the drain, every frame. After the fix there will still be two writers of a ROM address, with
the drain's value visible only to the mid-frame readers (`field.c:5096`, `:5299`, `:5322`). The
claim that the `--battletest` `+4` exclusion "should become removable" also covers the **enemy**
records (`field.c:5322`, `kFRec` byte +4), whose drain inputs were never measured. Unproven.

## S3A.5 Forbidden-pattern audit

* No scene/loc/enemy/id gate anywhere in the proposal. CLEAN.
* Items 1-3 are verbatim ROM stores at the ROM's own sites, already carried in `src/move.c`. CLEAN.
* **WARNING -- the unstated item 4** (whatever makes `$1604` move in THIS fight) is, as written, a
  mirror bolted onto a reimplemented op-0xD9 -- a stand-in for `C0/326C`, not a port of it (AR3.1).
* **WARNING -- mirroring in `mvStartStep` (`ct_vm.c:1472`) would over-write**: the ROM stores
  `$1600,X` from the `F700` table only inside op 0x96's `anim` arm, not on every mover step. A
  blanket mirror there is broader than the ROM.
* No eyeballed constants. CLEAN. No scene-scoped spot fix. CLEAN.

## S3A.6 Acceptance criterion -- measurable, but restate it

The dossier's criterion is measurable in FORM (four named bytes) but is pinned to frame numbers our
own traces number three different ways, and it is unreachable from the edits listed. Restated so it
is both reachable and re-measurable:

1. **Event-anchored, not frame-anchored**: at the tick on which our op-0xD9 completes (obj1 advances
   past `0x00A2`), assert `$1604 = 01`, `$1684 = 00`, `$0F05 = 00`, and on the next battle staging
   `$29B4 = 00` and the full record `0A 04 00 06 00`.
2. **During the walk**: `$1604 = 03` while obj2 is moving -- this is the half that item 4 must
   deliver and items 1-3 cannot.
3. **The discriminator must collapse**: `CT_FRAMELEGS=0` and the default must produce the SAME
   `$0F05` at staging. Today they differ (`00` vs `01`); after the fix they must agree. This is the
   single cheapest before/after and it needs no oracle.
4. **Grade it with a check that does not depend on the write mask** (AR3.5).

## S3A.7 Open questions carried out of this review

1. Where in the mainloop is the `$0117` consumer (`C0/1770`) reached from? Until that is derived,
   the "staging reads the drain of frame N-1" claim is unsupported (AR3.2).
2. Can `src/ct_vm.c` execute script bytes from the `$7F` WRAM image? If not, `C0/326C` cannot be
   ported faithfully and the mandate-clean route (AR3.1 option ii) is blocked -- say so explicitly
   rather than shipping the mirror silently.
3. Does our ported drain, given `($1604=01,$1684=00,$1685=00,$1605=05)`, actually publish `00`? The
   ORACLE does (measured, f123). Ours was not measured on those inputs -- no read-only harness path
   was found to feed a patched WRAM to `CtAnimDrainPass`.
4. The enemy staging path (`field.c:5322`) and the `+4` exclusion for enemy records: never measured.
5. Whether the compose mirrors (`field.c:8628` / `:8724`) should exist once the drain is fed
   correctly (this dossier's S3.5.4, unchanged).

## S3A.8 Commands used in this review

```sh
P=%TMP%/ctwf4/turns-adv/ct_probe.exe ; F=%TMP%/ctwf4/turns-adv/ct_field.exe
$P --monitor "1184:2,1C04:1,1604:1,0F04:1,011F:1" --from saves/battle_crono_solo_lake.state --input "40:400" --diff --writer --frames 200
$P --monitor "1604:1,1684:1,1605:1,1685:1,0F05:1,1B04:1" --from saves/battle_crono_solo_lake.state --input "40:400" --diff --writer --frames 200
$P --monitor "29B0:5,2989:1,9907:1" --from saves/battle_crono_solo_lake.state --input "40:400" --diff --writer --frames 260
$P --read "7F371F+32" --from saves/battle_crono_solo_lake.state
python tools/dis.py 326C 0xE0 ; 3315 0x60 ; 5535 0x30 ; 5690 0x20 ; 4FA0 0x50 ; 5F90 0x24
python tools/dis.py 0097 0x50 ; CC00 0x20 ; 041A 0x50 ; 0CE955 0x25 ; 0134A7 0x25 ; 18D9 0x20 ; 1770 0x14 ; 0380 0x20
# byte scans (python): jump table C0/5D6E+2*op ; canned block at 0x5D50 ; callers of C0/024C, C0/041A, C0/1770
# OURS -- the discriminator
CT_FRAMELEGS=0 CT_DUMPWRAM=q1230.wram CT_DUMPWRAM_AT=123 $F --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest o.raw --frames 130 --walkscript "l:120,."
CT_PDBG=1 CT_BATTLE_STAGEDBG=1 (same)     CT_VMTRACE=1 (same, --frames 135)
```
