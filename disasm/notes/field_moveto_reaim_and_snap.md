# FIELD op 0x96 MOVETO — the heading is RE-AIMED every step, and the arrival snap is DIAGONAL

User report (translated from Portuguese): *"taban crosses the teleport (blocked tiles) when running in scene 8"* — Taban crosses
the Telepod's blocked tiles in scene 8. The user added: *"actually I think it also happens in the original
game, but check it anyway"* and supplied two snes9x savestates of the moment
(`scene_8_taban_moving_1/2.state`).

**It does not happen in the original.** Two port inventions in the scripted mover made Taban walk a
different path from the ROM's — a straight diagonal that crosses the pod instead of the ROM's re-aimed
walk. Both are now removed; the user's own captures run byte-exact for 600 frames.

---

## 0. What was checked FIRST, and came back clean

Before touching the mover, the player-collision reading of the report was measured out (this is worth
recording because it is a whole class of "it must be the collision" hypotheses that is now closed for
scene 8):

- **The player's collision routine**: `--stairtest` micro-executes the REAL `C0/8A6D` on an injected WRAM
  and diffs the post-collision velocity against `CtPlayerCollide` per frame. Generalised with
  `CT_STAIRDIR` (the C0/88EE d-pad nibble) + `CT_RUN` (the 0x20 run magnitude) and swept over **108 start
  tiles × 8 directions × walk/run = 1728 runs — ZERO divergences.**
- **The collision DATA**: `CT_COLLDUMP=1` prints our per-tile `(b0,b1,b2)` in the ROM's own spelling; all
  **256 scene-8 tiles are identical** to the oracle's live planes (`$7E30C0,X` / `$7E7000,X` / `$7E7040,X`,
  the `collFetchEngine` contract) in every bit the decode reads. (`b0` bits 0-1 differ and are never read:
  `C0/9AD3` takes the shape as `(b0 & 0xFC) >> 2`.)
- **RUN is canonical**: `--monitor "00F8:1,1D2E:1"` on a field state, LEFT vs LEFT+B → `$00F8 = 0x0A` and
  the accumulator goes `0xF0` (-1px) → `0xE0` (-2px), which is exactly `C0/8944`'s `BIT #02` branch. Our
  port's `strong` flag is the same byte.
- **The follower** (`--followtest`, 240 frames of running around scene 8): `maxerr=0px -> BYTE-EXACT`.

So the report was not the player, not the map, not the follower. It was **Taban himself** — obj10, which
`ct_vm.c` already names at the `$1C81` seed, running op 0x96.

## 1. C0/4F61 has NO cached heading (`tools/dis.py 0x004F61 0x90 1 0`)

```
4F6B  TYX / INX / LDA $7F2001,X -> $F2      ; operand 1 = target tile X
4F73  INX / LDA $7F2001,X -> $F3            ; operand 2 = target tile Y
4F7A  LDX $6D
4F7C  LDA $1801,X -> $F0                    ; ** the actor's CURRENT tile ** (position HIGH byte)
4F81  LDA $1881,X -> $F1
4F86  CMP $F3 / BNE ->4FAB                  ; tile Y differs -> MOVE
4F8A  LDA $F0 / CMP $F2 / BNE ->4FAB        ; tile X differs -> MOVE
4F90  LDA $1C81,X / BIT #01 / BEQ ->4F9C    ; arrived: bit0 -> the SNAP (§2), else finish
4F97  JSR C0/305D / BCS ->4FA5              ;   carry set = still snapping -> re-anim + yield
4F9C  JSR C0/568B / TYX/INX×3 / SEC / RTS   ;   done -> advance past the op
4FAB  JSR C0/ABA2                           ; ** HEADING from ($F2-$F0, $F3-$F1) -- recomputed HERE **
4FB5  LDA $F700,X / STA $1600,X             ; facing from the heading
4FBD  JSR C0/5B95 / (+0x30 sidestep 4FC2)   ; an actor in the way -> sidestep the heading
4FD2  LDA $1000,X / STA $1A01,X / STA $7F0B00,X   ; the cadence ticket
4FE0  JSR C0/ACFD                           ; ** ISSUE the velocity from that heading **
4FE3  JSR C0/5614 / 4FE8 CLC / RTS          ; re-anim (walk), yield with the IP parked
```

Every visit whose ticket has expired re-reads `$1801/$1881` and recomputes. There is no state that could
hold a heading: the only per-object move state the handler writes is the ticket and the velocity.

**ORACLE** (`--monitor "1814:2,1894:2,1914:1,1994:1,1A15:1" --from scene8_taban_moving.state --diff`),
obj10 = Taban, speed `$1A14 = 0x40`, ticket `$1A15` = 3,2,1,0 (a 4-frame step):

| frame | `$1900/$1980` velocity |
|---|---|
| f0-f1 | `16 EA` = (+22, -22) |
| **f2** | `1A EF` = (+26, -17) |
| **f6** | `16 EA` = (+22, -22) |
| f10 | (unchanged — the recompute returned the same angle) |
| **f14** | `1C F2` = (+28, -14) |

The port held the f2 heading, so `--vmdiff --withpos` reported **first divergence at frame 6**
(`obj10 posX ours=0676 oracle=0672`, posY `0758` vs `0753` — ours had applied the previous step's
velocity). The held-heading branch was introduced to stop a candy-move jitter and its own comment says the
ROM "holds the velocity constant across the whole diagonal"; the disassembly above and this trace both say
otherwise. (The jitter it was hiding is not reachable from here: the whole suite is green with the branch
gone, the fair/candy checks included.)

## 2. C0/305D — the arrival snap corrects BOTH axes in ONE call

```
305D  LDX $6D
305F  STZ $1900,X / STZ $1980,X             ; clear both velocities
3065  LDA $1800,X / AND #F0 / CMP #80       ; X sub-tile at the CENTRE (0x80)?
306E    (yes) LDA $1880,X / AND #F0 / CMP #F0 -> equal: 3077 CLC / RTS   = SNAP COMPLETE
3079    (no)  BCS -> 3082 LDA #F0 (-1px) : 307B LDA #10 (+1px)  -> $1900,X
3089          THEN FALLS INTO THE SAME Y TEST -- LDA $1880,X / AND #F0 / CMP #F0
3099            BCS -> 30A7 LDA #F0 : 309B LDA #10 -> $1980,X
3092/30A2     LDA #01 / STA $1A01,X / SEC / RTS      ; ticket 1, carry SET = keep snapping
```

X target = sub-tile **0x80** (the tile centre, px 8), Y target = **0xF0** (the tile bottom, px 15), 1 px per
axis per visit, **both at once**. The port corrected X to completion and only then Y, which walks an L and,
on a 4-frame cadence, takes twice as long. Measured residual after fixing §1: at f30 the oracle's velocity
becomes `10 10` (+1,+1) and it lands at `(2130,1499)`; ours moved `(+1,0)` and stopped at `(2128,1456)`.

Gate: `4F90 LDA $1C81,X / BIT #01` — **bit 0**, not "!= 0" (Taban's `$1C95` reads 0x01; other objects in the
same scene read 0x03).

## 3. Result

`--vmdiff <state> --frames 600 --withpos` on **both** of the user's captures: `CLEAN: no scheduler
divergence over 600 frames`. Pinned as `vmdiff-scene8-taban-moveto` (+ `-moveto2`, the control that was
already clean before the fix), each mutation killed.

Fallout, both re-pinned with reasons in the JSON: `field-scene434-decision-box` (its NPC walk re-timed the
conversation — **re-anchored from f500 to f470**, where the decision box it exists to grade is up again;
a straight re-pin would have kept it green while grading a page with no box in it) and
`field-battle-ambush-generic-dissolve` (the pre-battle field walk stages the formation a little
differently; the fight itself is unchanged — same victory frame, same pcAttacks, same HP).

## 4. OPEN

- The **candy-move jitter** the held-heading branch was added for is not reproduced by anything in the
  suite today. If it returns, the cause is upstream of this handler (the note it left behind blamed
  "recomputing every frame"; the ROM recomputes every STEP, which is not the same thing).
- `C0/5B95`'s +0x30 sidestep is modelled by `npcMoveAngle` but was not re-derived in this pass.
- The user's own reading — "I think the original does it too" — is **refuted for these two captures**;
  nothing here says a scripted NPC never crosses a solid tile (op 0x96 is not tile-collided at all: the ROM
  walks the heading and only `C0/ACFD`'s own wall test can bounce it).

---

## 5. The SIBLING snap: `C0/30B3`, the walk-ONTO-the-target the party JOIN uses (2026-09-06)

User report (translated from Portuguese): *"when marle joins the party with crono, she walks to crono without animation (idle)"*.

Same shape as §0-§4 above and the same two port inventions, in the OTHER snap. Where op 0x96's
`$1C81,X` **bit 0** selects `C0/305D` (snap to the tile's centre/bottom), the move-toward-an-**actor**
body selects `C0/30B3` on **bit 1** — walk onto the target's exact pixel instead of stopping a tile short.
Scene 439's Marle fn1 arms it: `0D 00` (collision off) `0E 02` (`$1C81` := 2) `95 00` (move toward party
slot 0 = Crono) `D3 01` `5C`, at ip `$05F1`.

### 5.1 The arm, in the shared body `C0/5228`

```
  5237: STX  $E9            ; the target's object offset -- 30B3 reads it, SIXTEEN bit
  ...   ±1 adjacency compares (5245-5263), wrap-exact, per axis
  5265: LDX  $6D
  5267: LDA  $1C81,X
  526A: BIT  #02            ; <-- BIT ONE. op 0x96/0xA1 test bit ZERO and pick C0/305D
  526C: BEQ  ->5273
  526E: JSR  C0/30B3
  5271: BCS  ->527B
  5273: JSR  C0/568B        ; arrive: force idle
  5276: TYX / INX / INX / SEC / RTS
  527B: JSR  C0/5614        ; <-- the idle/walk/run selector, on the step 30B3 just published
  527E: TYX / CLC / RTS     ; yield, IP unchanged
```

`C0/30B3` zeroes `$1900`/`$1980`, compares the WHOLE-PIXEL parts (`AND #FFF0`) of self's and the
target's `$1800`/`$1880`, and on a mismatch publishes **1 px on BOTH axes in the same call**
(30EB/30F8 store `$1900` and then FALL INTO the same Y test at 3103, whose 312A/3140 arms store
`$1980`) plus `$1A01` = `$7F0B00` = 1, returning SEC. Fully aligned -> CLC (30E7).

### 5.2 What the port had

`ct_vm.c`'s copy of that arm was a paraphrase on three counts, and each is a defect the ROM does not have:

1. **one axis per visit** (X to completion, then Y) — an L instead of the ROM's diagonal;
2. **`$7F0B00` = #80** instead of 30B3's 1 (both free-integrate, but 1 is the byte the ROM writes);
3. **no `JSR C0/5614` at all** — so the actor kept whatever `$1680,X` the previous op left (0, idle, after
   `C0/568B`). *That* is the user's report: Marle slid onto Crono in her idle pose.

The arm now calls the graded `move.c` routines (`CtVmMoveSnap30B3` = `C0/30B3`, `CtVmMoveStepAnim` =
`C0/5614`), which is why this is a MECHANISM fix: the same `5265` arm serves ops 0x94/0x95/0xB5/0xB6.

### 5.3 The measurement

`saves/field_re/s439_marle_join_lake.state` — the oracle parked 2 opcodes before the join, so the ROM
runs the whole walk with NO input (recipe in `tools/regress_inputs.json`). obj3, X = 6:

| frame | `$1686` (`$1680,X`) | `$1906`/`$1986` | `$0F07` (drawn assembly) |
|---|---|---|---|
| 0  | 00 | — | 03 |
| 6  | 01 | 0E / F9 (the ordinary 5281 issue) | 17 |
| 14 | **06** | 10 / F0 — `|vx|+|vy|` = $20 and `$1100,X` = 3 < 4, so 5614 picks the fast index | 29 |
| 20 | 06 | 10 / F0 | 9E |
| 26 | 06 | 10 / F0 | 2A |
| 30 | 01 | 10 / 00 — only X still moves | 17 |
| 66 | 00 | 00 / 00 — `C0/568B` | 03 |

Position: the oracle walks (+1,-1) per visit from (396,241) to (416,234); the paraphrase held Y at 238
until X arrived, first diverging at frame 14 and finishing the op 16 frames late.

**Pinned twice, because one check cannot see both halves.** `vmdiff-marle-join-walkup`
(`--vmdiff <state> --frames 90 --withpos`) is CLEAN over the whole walk and grades the DIAGONAL — and is
BLIND to the anim: dropping the `JSR C0/5614` leaves it green (verified). `field-marle-join-walk-anim`
runs the live host from the same moment (`--loadwram` on that state's WRAM) and grades `$1680` and the
drawn assembly frame against the table above, frame for frame. Three mutations, all killed.

### 5.4 Still open

- Our final idle-blink turns one frame after the oracle's (f71 vs f70), the same 1-frame render phase
  already noted for the follower fixture ("draw fN = oracle fN+1").

---

## 6. Op 0x94 was on the WRONG ARRIVAL RULE entirely (2026-09-06, follow-up to §5)

§5.4 listed this as open. It is now closed, and it was not a guess — three independent measurements say
the same thing.

### 6.1 Which routine op 0x94 is

The event-VM jump table is at `C0/5D6E`, indexed by `opcode*2`. Read out of the ROM:

| op | handler |
|---|---|
| 0x94 | **C0/5215** |
| 0x95 | C0/5410 → `5426 BRL ->5228` |
| 0xB5 | C0/3546 = `STY $E3 / JSR C0/5215 / LDX $E3 / CLC / RTS` |
| 0xB6 | C0/354F = `STY $E3 / JSR C0/5410 / LDX $E3 / RTS` |

`C0/5215` resolves the operand as an object OFFSET (`5223 LDA $7F2001,X / TAX`) and then falls **straight
into 5228**. So all four ops execute the same arrival code; 0xB5/0xB6 differ only in that `LDX $E3`
discards whatever IP the body returned (and 0xB5 forces CLC), which is what makes them perpetual.

### 6.2 The arrival rule, and what the port had

```
  5245: CMP $F3 / BEQ ->5254 / INC A / CMP $F3 / BEQ ->5254 / DEC A DEC A / CMP $F3 / BNE ->5281   ; Y
  5254: LDA $F0 / CMP $F2 / BEQ ->5265 / INC A / CMP $F2 / BEQ ->5265 / DEC A DEC A / CMP $F2 / BNE ->5281
  5265: ... the $1C81 bit-1 / C0/30B3 arm of §5 ...
  5273: JSR C0/568B / TYX / INX / INX / SEC / RTS
```

ADJACENT on both axes — a wrap-exact 8-bit compare on the tile bytes `$1801`/`$1881`, not a signed
`|d| <= 1`. `ct_vm.c` routed op 0x94 to `dxr == 0 && dyr == 0` instead: the EXACT-tile rule, which is what
`C0/4F61` / `C0/5084` / `C0/5129` (ops 0x96/0x97/0xA0) use and which this body never uses.

### 6.3 The three measurements

1. **The oracle, live.** At the Marle join (`saves/field_re/s439_marle_join_lake.state`, §5.3) the ROM
   enters the 5265 arm at frame 13 with Marle's tile byte `$1807` = 0x19 (25) against the lead's `$1803` =
   0x1A (26), `$1887` = `$1883` = 0x0E — a delta of **(-1, 0)**: adjacent, and not equal. That is this
   body's arrival test executing on the real machine.
2. **`--sweepops`.** `move.c`'s `ctVmMoveActorOp`, which implements the ±1 rule for 0x94, is byte-exact
   against a micro-execution of the real `C0/5215` — 60 cases, the whole 128 KB image plus the returned
   IP and carry. The live VM was the outlier, not the reference.
3. **The port, before and after.** Location 44 (script 425) obj3 fn1 is `89 40 / 94 04 / AC 28 / ...` with
   no storyline or party gate in front of it, and its target obj2 is itself script-driven (op 0x9C), so
   `Belthasar --scene 44 --talk 3` chases a MOVING actor. With the exact-tile rule obj3 finished the op at
   **f288 standing on tile (5,9) — obj2's own tile**; with the ROM's rule it stops at **f264 on tile (6,8)**
   against obj2's (5,9). A tile too far and 24 frames late, and it applies to all **117** op-0x94 sites in
   the shipped scenes (30 locations; `assets/script.py` walk of every object's every slot).

### 6.4 What changed, and what is pinned

The adjacency test is now written once, wrap-exact, and shared by 0x94/0x95 and 0xB5/0xB6 — including the
`$1C81` bit-1 / `C0/30B3` arm of §5, which 0x94 also reaches in the ROM. Check
`field-op94-stops-adjacent` pins the completion frame's two POS lines (obj3 tile (6,8), obj2 tile (5,9)); its
mutation puts 0x94 back on the exact-tile arm and is killed. Suite 389 pass / 0 fail.

⚠ **What that check can and cannot say.** No oracle state exists for any of the 30 locations that use op
0x94, so it is not a lockstep: the frame numbers are our own trajectory and a later mover fix may move them.
The ROM-derived part — the part the mutation grades — is the RELATION between the two pinned lines: the op
completes while the tiles differ by one on each axis and match on neither. Re-pin only after checking that
still holds.
