# BATTLE — the ENTRANCE, the VICTORY and the DEFEAT are ONE per-charId pose-window ladder

User report (translated from Portuguese): *"[lucca] invisible in the opening and closing animations"* — Lucca is not drawn at all during the
battle entrance or during the victory.

She was not drawn because **both sequences were BAKED keyframe fixtures of one Crono+Marle GATO fight**,
with seat gates that read `!(mem[0x2980+seat] & 0x80) && (mem[0x2980+seat] & 7) == seat`. Any other
character fell through to a layer that had no keyframes for it. The ROM has **one machine** for all seven
characters, and it also drives the defeat pose.

---

## 1. Three stagers, one machine, one difference: the WINDOW OFFSET in `$5DF1`

| stager | window `$5DF1,Y` | `$5DDD/$5DDE` | reached from | sequence |
|---|---|---|---|---|
| `C1/34DB` | `0x0030` | 1 / 0 | `C1/0042 JMP C1/34DB`, the battle-init tail (dispatch idx 0) | ENTRANCE walk-in |
| `C1/356D` | `0x0028` | 0 / 0 | `C1/8186 LDA #08 / JSR C1/0003` → `C1/0051[8]` | **VICTORY** |
| `C1/354D` | `0x0020` | 0 / 1 | `C1/815F LDA #09 / JSR C1/0003` → `C1/0051[9]` | DEFEAT (party wipe) |

Each also does, per seat: `cursor = 0xFF`, `dwell = 1 + seat` (the `C1/3665` stagger 1/2/3), and — for the
entrance and victory arms only — `$5DF1,Y = 0x0000` instead when `$A09B,seat != 0`. The outcome router:

```
18133 LDA $24 / BEQ ->18167          ; $24 != 0 = party wiped
1815F   LDA #09 / JSR C1/0003        ;   LOSE  -> C1/354D, window +0x20
18167 (victory scan over $AF02, 8 slots)
18176   JSR C1/B3BB                  ;   the staging teardown
18186   LDA #08 / JSR C1/0003        ;   WIN   -> C1/356D, window +0x28
```

`C1/356D` also early-outs on `LDA $2989 / AND #01` — a fight with *no* celebration at all. Measured 0 in
every fixture; unported, and the byte's writer is not traced.

## 2. The shared copier `C1/35DD` and what it reads

```
135E9  LDX $80 / LDA $96F5,X / BEQ ->1365D          ; ABSENT seat -> skip entirely
135F0  $A5 = $96DB,X                                 ; FACING, resolved ONCE here
135F5  $A7 = $966E,X , $A8 = $A2C8,X / JSR C1/00A7   ; $A9 = facing * stride
13617  $A1FE = $5DF1,Y + $1CBA,X                     ; DWELL src = ptrB + win        (NO facing term)
13621  $A1E8 = $5DF1,Y + $1C99,X + $A9               ; POSE  src = ptrA + win + facing*stride
13639  8x { LDA $E40000,X : LSR A : STA $922D,Y }    ; dwells = ptrB[...] >> 1
13650  8x { LDA $E40000,X :         STA $9245,Y }    ; poses  = ptrA[...]
13665  $5DBD/BE/BF = 1/2/3 ; $5DC1/C2/C3 = FF
```

`ptrA = word E4:2600[cid*2]`, `ptrB = word E4:2800[cid*2]`, `stride = (ptrA[cid+1] − ptrA[cid]) >> 2`.
Read straight out of the ROM, the **+0x28 victory** rows:

| cid | ptrA | ptrB | stride | poses (facing 0) | dwells (raw, `>>1` at copy) |
|---|---|---|---|---|---|
| 0 Crono | E4/3000 | E4/A800 | 0128 | `4F 3A 39 38 6C 6D FF` | `0A 0A 0A 0A 0F 14 00` |
| 1 Marle | E4/34A0 | E4/A928 | 00F8 | `2F 2E 2D 2C 61 62 63 FF` | `0A 0A 0A 0A 0F 05 0A 00` |
| **2 Lucca** | **E4/3880** | **E4/AA20** | **011C** | **`49 46 47 48 01 87 4A FF`** | **`05 05 05 05 0A 05 0F 00`** |
| 3 Robo | E4/3CF0 | E4/AB3C | 011C | `7B 7D 7E 7C 2D 2E 5E FF` | `05 05 05 05 05 0F 0F 00` |
| 4 Frog | E4/4160 | E4/AC58 | 0118 | `2F 2E 2D 2C 61 62 FF` | `0A 0A 0A 0A 0F 14 00` |
| 5 Ayla | E4/45C0 | E4/AD70 | 0150 | `04 01 03 02 2D 30 3B FF` | `05 05 05 05 07 07 14 00` |
| 6 Magus | E4/4B00 | E4/AEC0 | 00F4 | `01 03 02 00 5B 5C 00 FF` | `05 05 05 0F 14 0F 14 00` |

## 3. The stepper `C1/369A` / `C1/36A1` — one entry every OTHER render frame

```
1369A  LDA $E5 / BNE ->136A1 / INC $E5 / RTS       ; a tick every OTHER frame
136AC  LDA $5DDE / BNE ->136BF                     ; the DEFEAT arm BYPASSES the dead check
136B4  LDA $5E4A,X / BMI ->13705                   ; a DEAD PC has no ladder
136BF  LDA $96F5,X / BEQ ->13705                   ; an ABSENT seat has none either
136C7  DEC $5DBD,X / BNE ->13705                   ; the per-seat dwell countdown
136CC  INC $5DC1,X / CMP #08 / BEQ ->              ; the cursor caps at 8
136E7  LDA $922D,Y / STA $5DBD,X                   ; reload; 0 = terminator -> publish NOTHING, pose HOLDS
136F6  $9679 = seat / $967B = $9245,Y / JSR C1/1C4A -> C1/1C66 STA $9907,X
```

`C1/367F` wraps it in a 64-frame blocking driver (`LDA #40 / STA $5DC9`), which is why the celebration is
bounded even though the ladder itself ends on a dwell-0 terminator.

`$5DDE` is the canonical reason a **dead PC does not celebrate but does play the defeat pose**: the victory
and entrance arms skip a dead seat at `C1/36B4`; the defeat arm does not.

## 4. The FALLEN pose — `D1:5F42 + charId*4 + facing`

One-shot, published on the death and held. Byte-identical to each charId's own `+0x20` window:

| cid | 0 Crono | 1 Marle | **2 Lucca** | 3 Robo | 4 Frog | 5 Ayla | 6 Magus |
|---|---|---|---|---|---|---|---|
| pose | `45 46` | `4E 4F` | **`40 41`** | `4D 4E` | `4E 4F` | `C3 C8` | `4D 4E` |

## 5. Verified against the oracle

`bat8_lake` with the four Naga-ette `curHP` patched to 1, mashed to victory
(`--monitor "7E5DF1:6,7E9245:16,7E922D:16" --diff --writer`):

```
f1334  7E5DF1=280028002800*(C1:35D2)
       7E9245=373635346C6DFF00 4946474801874AFF*(C1:3657)
       7E922D=05050505070A0000 0202020205020700*(C1:3641)
```

Seat 0 is Crono at facing 3, seat 1 is **Lucca**. Our arm prints, for the same seed:

```
pcladder f400 seat=0 cid=0 win=$28 face=0 poses=4F,3A,39,38,6C,6D,FF,00 dwells=5,5,5,5,7,10,0,0
pcladder f400 seat=1 cid=2 win=$28 face=0 poses=49,46,47,48,01,87,4A,FF dwells=2,2,2,2,5,2,7,0
```

Lucca's row and dwells are **byte-identical** to the oracle's (her victory row is the same on all four
facings); Crono's dwells are identical and his row is the facing-0 one because our seed's `$96DB` differs
from the oracle's at that moment. The oracle's own rendered victory (peek f1424) shows Crono **and** Lucca
side by side in their poses; ours now does too, where it used to draw Crono with an empty bracket beside him.

## 6. What the port does now

* `CtBattlePcLadderArm/Tick/Fallen/Pose` (`src/battle.c`) = §1's stagers, §3's stepper, §4's one-shot. The
  per-seat state is the ROM's: `win/cursor/dwell/facing` == `$5DF1,Y / $5DC1,X / $5DBD,X`, and `pose` is
  what the ROM publishes into `$9907`.
* Armed at the victory transition (`CtBattlePcLadderArm(h, 0x28, 0)` beside `victoryHold = 1`) and on a PC
  death; ticked once per render frame (the function halves it itself).
* `battleStanceRender`'s existing per-seat loop draws the published pose through the same
  `ctAnimengPcBlit` / foot / palette it already used — **a published pose OWNS the seat**, so the
  active-PC bob and the attack hand-offs are skipped while it is up (without that guard the lead was drawn
  by a renderer with nothing to draw: an empty bracket, measured on bat8 at hold 90).
* `battleEndPoseRender` is retired.
* A dead seat's fallen pose is also RECOVERED (not only published at the death event), because the ROM's
  hold is a value sitting in `$9907` and a state entered with a PC already down has it.

Test surface: `CT_BATTLE_RENDER_VICTORYHOLD=<n>` arms the ladder and runs `n` frames of its stepper on a
`CT_BATTLE_RENDERWRAM` seed — without it the victory arm is unreachable from a mid-battle capture and no
check can render Lucca's or Frog's celebration at all. Traces: `pcladder` (the armed rows and dwells) and
`pcfallen`.

Checks: `field-battle-lucca-victory-pose` (+2 mutations), `field-battle-solo-victory-no-marle`
(mutations re-authored), plus the re-pinned `field-battle-victory-hold` /
`field-battle-hud-name-after-victory`.

## 7. OPEN

> 2026-09-10: the first bullet (the ENTRANCE) and the last (`CC/F06B` wiping the ladder) are CLOSED -- see the
> addendum at the end. `$A09B` is staged and gated (its meaning is still open). §3's "every other render frame"
> is corrected there too: the stepper's only caller is the copier's own 64-frame loop.

* **The ENTRANCE (+0x30) is NOT done.** `battleEntranceRender` + `battleEntranceMask` + `battle_entrance.h`
  are still the Crono+Marle bake, and `battleEntranceMask` derives its suppression mask **from the bake**
  with no reference to the party — so pal6 (seat 1) is suppressed for module frames 0..176 of *every*
  battle and pal5 (seat 0) for 0..122. The undershoot the verifier proved: **seat 0 also vanishes whenever
  seat 0 is not Crono** — a Crono-less party loses its lead too. Doing it needs the `$0F01 → $29B4`
  staging drain first (`battle_entry_pc_pose.md` AR.2.1) and moves two pinned checks
  (`field-battle-solo-entrance-no-marle` at `CT_BATTLESHOT_AT=120`, whose pinned MUTATION targets a line
  that goes away, and `field-battle-forest-camera-composite` at field-frame 90).
* **The DEFEAT arm (+0x20, `$5DDE = 1`)** is implemented in the tick but never armed — our loss path does
  not call it. Arm it on the byte, do not guess.
* **`$A09B,seat`** — the `+0x00` alternate window (a single standing pose). Reads `00 00 00` in every state
  loaded. Probe: a ROM-wide writer scan plus `--monitor "A09B:3" --writer` on a scripted/boss/ambush battle
  whose walk-in is skipped.
* **`$2989` bit0** — the flag that suppresses the celebration entirely (`C1/35A9`). Measured clear
  everywhere; needs a boss fight with no victory dance.
* **`$5DDD`** (entrance 1, victory/defeat 0) decodes to a no-op at `C1/36EF`; unported, no observable.
* ⚠ **`CC/F06B` also clears `$5D80..$5E0C`**, which contains `$5DBD/$5DC1/$5DF1` — the per-action teardown
  (see `battle_action_state_leak.md`) wipes the ladder state too. Our ladder is host-side and is not wiped
  by the action machine; that matters only if an action can open while a ladder is armed, which the victory
  arm cannot. The entrance arm can, and this must be modelled when §7's first item is done.

---

## THE ENTRANCE ARM IS LANDED (2026-09-08) — and the warning above is exactly what it cost

The note that closes the section above says the entrance arm can open an action while a ladder is
armed and "must be modelled". It is, and here is what it took.

### The trigger is the BOOT'S LAST INSTRUCTION

```
C1/0023  JSL CC/E2E4 / JSR C1/0299 / JSR C1/283D / JSL CC/ED87 / JSR C1/34A7 / JSL CD/0003
C1/0038  JSL CD/000C            seed the entry band ($CA1F = 0x17, $CA1E = 0xFF)
C1/003C  JSL CD/0012 / BNE ->1003C    BLOCK until the band's steps are done
C1/0042  JMP C1/34DB            <-- and the fall-through ARMS THE LADDER, window 0x30
```

`C1/34DB` and `C1/35AE` are one shape with one difference — the window byte (`#30` vs `#28`) — and both
gate per seat on `$A09B,X`: zero arms the window, non-zero arms window 0.

### Two things the completion plan had wrong, both measured before porting

* **`$5DF1` has exactly FOUR writers in the 4 MB image**: `C1/34EB`/`C1/34F7` (`#00`/`#30`) and
  `C1/35BE`/`C1/35CA` (`#00`/`#28`). The plan lists the gap as "0x30/0x20 are never armed" — **window
  `0x20` is armed nowhere at all** and is not a gap.
* **`C1/34DB` looks unreachable and is not.** Nothing in the image targets `C1/0042` with a JSR, JMP or
  JSL, because that address is CODE — the boot's tail — not a vector. Searching for callers finds none.
  `--monitor "7E5DF1:8" --diff --writer` settles it: window `0x30` is live in bat3, bat9 and
  `forest_wait_ambush_active`, and on the ambush state it is armed at **f360 by C1:34FF**.

### ⚠ THE DISARM IS THE WHOLE PROBLEM, and the warning above named it

Arming alone broke **thirty-one checks**. The ladder caps at step 8 (`136CC`) but stays ARMED, so every
PC held an entrance pose for the rest of every fight, and every battle screenshot moved.

The oracle has the clear: on the ambush state `$5DF1` goes `30 30 30` at f360 and back to `00` at
**f531, by `CC:F071`**. This engine modelled that wipe only as a battle-BEGIN reset — i.e. *before* the
arm. Moving a ladder disarm to `battleCombatStart` (the `CC/F071` point) took the suite from 31 fail to
0 fail, with the ladder still playing its rows in the intro window.

### What is pinned, and the residual

`field-battle-entrance-pose-ladder` pins the arm: frame, window, per-seat facing, and the ROM pose/dwell
rows the ladder will play (Crono `30,31,32,33`, Marle `38,39,3A,3B`, dwells `5,5,5,5`). Two mutations,
both killed — arming `0x28` at the entrance, and not arming at all.

⚠ **The arm FRAME is not oracle-compared.** It follows this engine's own band-completion clock (f81 on
`forest_pre`), and the oracle's arm-to-clear span on the ambush state is **171 frames against ours of
about 78**. The ladder completes inside both, so nothing pinned sees the difference — but the span is a
named residual, not a match.

---

## ADDENDUM 2026-09-09 — the entrance ladder is armed but does not STEP pre-combat in this engine

Found while porting the idle-tick PC walk (`battle_facing_engine.md`): `CtBattlePcLadderTick` is called from
`battleTickBody` after the intro gate (`h->frames < CT_BATTLE_INTRO -> return 0`), and `battleCombatStart`
disarms the ladder at the intro frame. So the 0x30 window armed at the band-completion edge never publishes a
step — the entrance is drawn by the baked morph layer (`battleEntranceRender`), and the ladder machine only
steps for the victory and fallen arms. The ROM's `$9907,X` at the idle loop's start is the ladder's fourth
publish (entry 3 of window 0x30: holdleft 33/3B, gato_wait 4F/37 — each equal to the seat's idle entry [1]),
which the PC walk needs to decide its first upload; `battleCombatStart` now seeds `pcPub` from that window
entry. Stepping the ladder pre-combat (and retiring the morph bake) is the remaining half of this port.


---

## ADDENDUM 2026-09-10 — the entrance ladder STEPS pre-combat, on the copier's OWN clock; the bake is retired

This closes §7's first item and its last bullet, and corrects three things §3 and the 09-09 addendum said.

### 1. Where the first pose comes from: `C1/34A7` publishes `$A4DC,X`, the loaders' copy of the staging record's byte +4

The boot tail (`C1/34A7-34DA`) loops X = 0..10: `LDA $96F5,X / BEQ skip / $9679 = X / $967B = $A4DC,X / JSR C1/1C55`
— i.e. `$9907,X := $A4DC,X` for every PRESENT combatant — then `$9689/A/B := 1/2/3`. `$A4DC` is written by the
loaders and by nothing else afterwards (monitored through f200 on gato_t0):

```
CC/E93B   copies $29B0 (15 bytes, the three PC staging records) to $96A4
CC/E965   LDA $96A8,X / STA $A4DC,Y        PC seat: record byte +4
CC/EA9E   LDA $29C3,X / STA $A4DC,Y        enemy slot: record byte +4
```

and byte +4 is what the FIELD staged: `C0/0495 LDA $0F01,X / STA $7E29B9` — the object's live assembly frame
(field.c stages the same byte into `r[4]`). Oracle gato_t0: f147 `$A4DC = 03 02 D3 .. 04` (CC:EAA4), f152
`$9907 = 03 02 FF FF FF FF 04` (C1:1C69). Crono's 03 and Marle's 02 are the FIELD poses the OAM shows until the
first step — peeks f166-f169: pal5 `(126,96)(126,112)(126,144)(126,128)` = E3 record 03 at foot (134,144), pal6
`(169,63)(169,79)(169,111)(169,95)` = record 02 at (176,111). The seat-2 D3 is never published (`$96F5[2] = 0`).

`CF/FC2D` (`$9907,X -> $29B4,Y`, `$1D0F,X -> $29BF,Y`, `$990A,X -> $29C3,Y`) is NOT a per-frame OAM stager: `$29B4`
stays 03 through the whole entrance and beyond. It is a write-back of the live state into the staging records
(the battle end / re-stage path), which is why the port's staging comment "+4 prio" was reading the same byte.

### 2. The stepper `C1/369A` has ONE caller — the copier's own 64-frame loop

§3 said "one entry every OTHER render frame", implying the main loop calls it. It does not. The copier's tail:

```
C1/3665  LDA #01 / STA $5DBD / INC A / STA $5DBE / INC A / STA $5DBF     dwells 1/2/3 (the stagger)
C1/3672  LDA #FF / STA $5DC1 / STA $5DC2 / STA $5DC3                     cursors
C1/367D  STZ $E5 / STZ $A4
C1/3681  LDA #40 / STA $5DC9
C1/3686  loop: JSR C1/007E      ; INC $9E, spin on JSL CD/0036 until $9E clears = ONE FRAME
C1/3689        JSL CD/0009      ; the frame service
C1/368D        JSR C1/369A      ; the stepper
C1/3690        DEC $5DC9 / BNE loop
C1/3695  STZ $E5 / STZ $A4 / RTS
```

A whole-image search for `JSR/JSL C1/369A` finds exactly `C1/368D`. So every arm (entrance, victory, defeat)
runs the ladder for 64 frames inside the stager itself, blocking whatever called it — the boot, for the entrance —
and never again. Oracle gato_t0 (`--monitor "7E9907:11,7E5DBD:5,7E5DC1:4,7E5DF1:6,7ECA1E:2" --writer`):

```
f137  CF:FB9A   the wipe                       f165  C1:34FF  $5DF1 = 30 30 30 (arm); $5DBD = 01 02 03; $5DC1 = FF FF FF
f152  C1:1C69   $9907 = 03 02 .. 04 (boot)     f166            (INC $E5 only; $CA1E := 0 by CD:05FE)
f153  CD:0512   $CA1E/1F = FF 17 (band)         f167  C1:1C69  Crono 38  (dwell reload 5, cursor 0)
f154-f165       $CA1F -2 per frame              f169  C1:1C69  Marle 34
                                                f177/179 39/35   f187/189 3A/36   f197/199 4F/37
                                                f207/f209       cursor 4, dwell[4] = 0 -> terminator, $5DBD := 00
                                                f211..f229      $5DBD DECs wrap FF, FE, ... F5 (Crono) / F6 (Marle), every other frame
                                                f229            the LAST DEC = arm + 64; nothing touches $5DBD after
                                                f232  C1:2E23   $9689[1] := 01 -- the main loop's first PC-walk visit
                                                f238  C1:1C69   Crono C7 -- the walk's first change publish
                                                f331  CC:F071   $5DBD/$5DC1/$5DF1 := 0 -- the first action's open
                                                f487  CC:F071   again, at its close
```

The port: `CtBattlePcLadderArm` sets `pcLadderLoop = 0x40` and `pcLadderTick = 0`; `CtBattlePcLadderTick` returns
unless the loop count is positive, decrements it, and steps on alternate calls; the call sits at the top of the
frame, BEFORE the band-edge arm block, so the arm frame does not tick (oracle: arm f165, INC f166, step f167 =
arm+2, last DEC arm+64). Port gato_t0: arm f16, steps f18/f28/f38/f48 (Crono) and f20/f30/f40/f50 (Marle),
terminator f58, loop over at f80 — the same offsets. The "dead seat recovery" loop that lived in the tick is gone
(pcPub carries the fallen byte; the mid-battle loader reads the seed's `$9907`).

### 3. The disarm is `CC/F06B` at EVERY action open (C1/405B) and close (C1/4119) — on all three port openers

`CC/F06B` zeroes `$5D80..$5E0C` (dwells, cursors, `$5DC9`, `$5DDD/E`, windows). `CtBattlePcLadderWipe` is called
from `battleOpenAction`, its enemy think-drain twin, AND the PC dispatch path — the third one was missing at
first, and the crit shots moved because the entrance's last publish (4F) kept OWNING Crono's seat through his
leap: `pcblit f235 seat0 pose=4F foot=(135,120)` appeared on top of the leap render. With the wipe on that path
the crit/ambush shots returned to their pins. `$9907` is not in the wiped range: the byte stays; only the port's
hand-off arbiter (`pcLadderPose`) is dropped. The walk's own publish (`C1/1C55 STA $9907,X`) also drops it — the
byte has been replaced.

### 4. The draw is `$9907`, always

`battleStanceRender` draws `pcPub` whenever it is not -1 (the seed's `$9907 != FF` for a synthetic host). The
`$A103 != 0` gate ("the walk has latched") and the "dead and nothing published -> draw nothing" branch are gone:
the ROM's OAM builder reads `$9907` every frame and a present seat holds SOMETHING there from the boot's first
publish on. Retired with them: `battleEntranceRender`, `battleEntranceMask`, `CT_ENT_BOOT_F`, the `suppressPal`
parameter, and `src/data/battle_entrance.h` (475 lines of Crono/Marle keyframes aligned to oracle-absolute
frames — its Crono P3 ran to f344 and Marle's P2 to f398, and on a fight whose first action opened inside that
window it painted the settled stance at the staged seat on top of the attack render).

OAM cells, port vs oracle peeks, exact: f8 03/02 (the field poses), f30 39/35, f52 4F/37 — `(126,120)(126,152)
(126,136)(126,106)`, `(187,79)(171,79)(187,95)(171,95)`, `(126,120)(110,116)(126,136)(126,107)`,
`(170,79)(170,111)(170,95)(154,87)`.

### 5. `$A09B,seat` — staged and gated, meaning still open

`CF/FBB8-FBE1` (bytes `[31 ROM bytes at 0xFFBB8: python tools/dis.py 0xFFBB8 31]
BA 00 7F 29 02 49 02 8D A7 A0 5C 00 80 C1`): `$A0A6 = $7F00BA & 1`; if set, `$A09B,seat = $7F00B4[$2980,seat & 7]`
for seats 2..0 (no presence test) and `$A0A7 = ($7F00BA & 2) ^ 2`; then `JML C1/8000`. Readers: the two arms
(C1/34E4, C1/35B7 — non-zero -> window 0), the status scan C1/2F99 (non-zero -> pose class 0, no icon), C1/3037,
C1/FB13 and the command menu (C1/205F, 20EA, 2145, 237E, 2502, 2688, 2748). Writers of `$7F00B4`: the menu's
`C2/9D11` (zeroes a character < 6 while `$0077 == 0`); what SETS it is not located (the bank-F4 `TSB $A09B`
hit is data). Every fixture has `$7F00B4..BA = 0`. The port stages the bytes between the wipe and the loaders
and gates the arm on them (`(!defeatArm && $A09B) ? 0 : win`); a mutation ignoring the flag survives on every
fixture, so it is NOT added — the gate is ungraded until a save with the flag exists.

### 5.1 `$A09B` IDENTIFIED (2026-09-10, item 7d): the Blackbird's "equipment stolen" flag, per character

The writers a ROM-address search cannot find are event-VM ops. Scanning every scene script for writes into
`$7F00B4..$7F00BA` (op `56` SetGlobalByte, ops `65/66` set/clear-bit on the `$7F00xx` block):

```
scene 371 @044A..0468   56 01 B5 00 / 56 01 B6 00 / 56 01 B7 00 / 56 01 B8 00   $7F00B5..B8 := 1  (Marle, Lucca, Robo, Frog)
scene 371 @071E         65 00 BA                                              $7F00BA bit0 := 1  (the CF/FBB8 gate)
scene 373 @0231..0255,  56 00 B5..B9 00                                       cleared (and B9)
scene 443 @01C9..01ED
scenes 367-377, 442-443 65/66 bit 1..7 BA                                    the other bits of $7F00BA
```

Scene 371's own dialogue table (`B8 -> F7:4DAA`) is the BLACKBIRD: "The Blackbird's a symbol of how advanced
Zeal's culture is", (Dalton's taunt), "Marle: Our equipment's gone!",
"Even the money...", and [4]-[6] "Got {pt}'s equipment back!". So `$7F00B4 + charId` = this character's equipment
is stolen, raised for the four possible Blackbird party members (Crono is dead at that point), and in battle:
`$A09B,seat` non-zero -> the entrance and victory arms take WINDOW 0 (the unarmed standing pose instead of the
weapon-draw), the status scan reports class 0 (the unarmed pose set) with no icon, and the command-menu readers
C1/205F..2748 see it. `$7F00BA` bit0 gates the staging (CF/FBC1); bit1 lands inverted in `$A0A7`. The port stages
and gates exactly that; no fixture in the tree is a Blackbird fight, so the gate stays ungraded until one exists.

### 6. What is measured but not modelled

* **Publish -> PPU lag is 2 frames** (`$9907` written in f167, the PPU OAM shows the new cells after f169;
  Marle f169 -> after f171). No WRAM byte among `$A4DC/$A2C8/$966E/$9922/$A103` changes in between: it is the
  tile-upload packet (C1/1C69-1D4A builds it into the `$7E0000,X` DMA buffer) plus the OAM build/DMA. The port
  draws the byte on the frame it is written, as it does for every other pose. Global render-pipeline residue.
* **The absolute clock.** On gato_t0.state the field's staging is f76 and the arm f165 (89 frames later: ~60 of
  field-side transition, the wipe at f137, 15 frames of loaders, 12 of band). The port's `--battletest` arms at
  f16 (f81 on `--scene 19` forest, where the field engine spends its own transition) — the ladder's offsets from
  the arm are exact, its absolute position is the recorded intro-clock residue (the field's C0/EC60 frame-waits
  and the loaders' packet costs are not spent).

### 7. Checks

New: `field-battle-entrance-initial-pose` (arm line, publishes to f30, cells at f8 = 03/02 and f30 = 39/35; 3
mutations killed: no boot publish, `$E5` not zeroed, no per-seat stagger) and `field-battle-entrance-ladder-hold`
(publishes to f50, cells at f52 = 4F/37 held and f190 = the walk's C6/DA; 3 killed: a 16-frame loop, the walk not
ending the hold, a step every frame). `field-battle-crit-flash1` gained the dispatch-path wipe mutation (killed).
Eleven re-pinned with the attribution in their notes (intro-window shots; synthetic renders that now draw the
seed's `$9907`); two anchors re-anchored (the victory-arm one to the `$9907` branch, killed); one retired with
its reason (the solo check's presence gate is enforced upstream by the draw-order list).
