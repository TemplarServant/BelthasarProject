# Field collision — reverse-engineering map (verified)

The CT field-collision subsystem, decoded + **adversarially verified** (workflow
`ct-field-collision-re`, 16 decode/verify agents over the real ROM, the Python reference
`assets/engine.py`/`maps.py`, and the field execution trace `disasm/boot_trace_play.txt`).
Per-routine instruction-level pseudocode lives in `disasm/notes/_collision_specs/*.json`
(the verifier's `finalCPseudocode` — the corrected, authoritative version). This file is the
synthesis + the port contract.

> **The execution trace `boot_trace_play.txt` is an IDLE capture.** The player never moved in
> it (all 34 sampled frames early-out at `$1D2E==$1D2D==0`), so every *moving* collision path
> has **zero** trace hits. The decode below is verified by static re-derivation + ROM-table math;
> the *moving* behaviour must be validated against the LakeSnes oracle by actually walking Crono
> into a wall (see the Port Plan, STEP 1).

All player/collision routines run with **Direct Page `D = $1D00`** (`LDA #$1D00; TCD`, set in the
controller entries C0/881E and C0/885A). A direct-page operand `$XX` is therefore WRAM `$7E:1DXX`.
The actor-collision routine **C0/5B95 runs with `D = $0100`** (`$XX` = `$7E:01XX`). Absolute
operands (`$0197`, `$0A00,X`, `$0C00,X`, `$0155`) are literal `$7E` addresses, not DP-relative.

## 1. The per-frame player controller — C0/881E (call order)

```
881E  LDA #$80; STA $01EB                 ; (already in src/player.c)
8823  PHD; D = $1D00
882C  JSR $88E5   seed:  $1D2E=$1D2A, $1D30=$1D2B     ; carry-over velocity (the "momentum")
882F  STZ $1D2C; STZ $1D2D                            ; clear d-pad facing intent
8833  if ($0162==0 && $011F!=0): JSR $88EE  d-pad dispatch (gated also by $0138==0)
8840  if ($0120 != 0):  SEP#$10; JSR $8A6D ; *** the player TILE-COLLISION + position path ***
884C  JSR $9175   screen/map edge clamp -> APPLIED velocity $1D32/$1D33   ; (in src/player.c)
884F  JSR $99DE   finer map-RECT clamp: zero $1D2E/$1D30 at a scroll bound ; (PORTED: ct_vm.c camGate99DE)
8852  JSR $91AC   accel / auto-target integrator + subpixel->pixel split ($1D41..$1D4C)
8855  JSR $93E1   camera scroll + VRAM tilemap-strip redraw scheduler (NOT collision)
8858  PLD; RTS
```
`91AC`+`93E1` are called **three times** (8852/8855, 88B9/88BC, 88DD/88E0 — the two extra are
camera passes). After the controller, the player position `$1800/$1880` is integrated by **C0/AAFD**
from the applied velocity `$1900/$1980 = $1D32/$1D33` (the `--playerframe` model). The collision
work is entirely **velocity-clamping** ($1D2E/$1D30 -> 0 when blocked); nothing here writes the
16-bit world position — that is AAFD's job.

### What was WRONG in the first pass (caught by the adversarial verify)
- `93E1` is the **camera / VRAM-strip redraw scheduler**, *not* position-integration+collision
  (no `$62/$64/$155/$2E/$30`, no `9C5C/9AA1` references anywhere in `93E1-9922`).
- `91AC` is the **acceleration + subpixel→integer-pixel converter** (builds the per-axis,
  per-BG-layer whole-pixel step counts `$1D41..$1D4C` for the camera; blocks 1&2 = auto-target/
  forced-vector, both dormant in free-roam).
- The real player tile-collision is the **8A6D → 8AB5/8D11/… → 9C5C** path (below).

## 2. The player tile-collision path — C0/8A6D (gated by `$0120 != 0`)

`8A6D` dispatches on the sign of the velocity accumulators `$1D2E`(X)/`$1D30`(Y) to an 8-way
directional handler (it runs with A/X **8-bit**, so it tests only the low byte):

| `$1D30` (Y) | `$1D2E` (X) | dir | handler |
|---|---|---|---|
| 0 | 0 | — | RTS (free-roam idle path; **0 handler hits in the trace**) |
| 0 | >0 | E | `8AB5` |
| 0 | <0 | W | `8BC4` (reuses the E mid-section via `BRL 8AE4`) |
| <0 | 0 | N | `8BF9` |
(ALL EIGHT arms ported + exhaustively graded 2026-08-14 -- `--sweepclamp`, 16562/16562 byte-exact
over a position grid x 49 velocity vectors x both entry carries, every arm reached >= 1014 times.)

| <0 | <0 | NW | `8EF1` (corner box; fallback up `8BF9` / left `8BC4`) |
| <0 | >0 | NE | `8E21` (fallback up `8BF9` / right `8AB5`) |
| >0 | 0 | S | `8D11` |
| >0 | <0 | SW | `8FC1` (fallback down `8D11` / left `8BC4`) |
| >0 | >0 | SE | `909C` (fallback down `8D11` / right `8AB5`) |

Each handler: load base `$1D62=$1800,X` / `$1D64=$1880,X` (X=`$0197` player index); build a
tentative point `$1D66/$1D68 = base ± feeler($70=112) + velocity`; probe (a) **actor occupancy**
`9923` and (b) **tile solidity** `8A9E`; on a corner/edge block try a **±$10 slide** on the cross
axis and re-probe; finally `BRL 9C5C` (commit) on the all-clear leaf, or `STZ $2E`/`STZ $30` on a
hard block.

**`8A9E probe_tile_solid()` → A=1 off-map, A=0 passable; carry from the z-gate:**
```
A16 = $1D68 (tentative Y); SEP#$20; A8 = $1D67 (tentative X hi)
JSR $9AA1  ; prop fetch; if $7E70C0[cell] bit7 clear -> SEC (see the note below: TREASURE plane)
  BCC ->   LDA #$01; RTS        ; off-map => A=1, carry SET
JSR $9AD3  ; shape decode -> $1D58/$5A/$5C, $1D6A z, $1D6C ignore_z, prio
JSR $9C37  ; z-gate -> carry = SOLID
LDA #$00; RTS                   ; A=0, carry = z-gate verdict
```
Handlers branch on **both** A (off-map) and carry (z/shape-solid).

**`9923 probe_actor_block()` → carry SET = blocked:** walk the actor list `$7F2000` (ASL→X),
skip dead (`$0F00,X==0`), inactive (`$1100,X` bit7), and self/party (`X==$0197/$0199/$019B`); a
hit when `|$1880,X-$1D68| < $E0 && |$1800,X-$1D66| < $E0`; `$1B01,X` bit0 selects SEC-on-hit vs
CLC-on-hit polarity.

## 3. The collision core (shared by player 8A6D and the per-axis commit 9C5C)

### 3a. Tile-property fetch — C0/9AA1 / 9AC8 / 9AD3 (+ tail 9AB2)
Cell index is a **16-bit de-interleave**: with `REP #$20`, `A = (cellY:cellX) AND ($1D1F:$1D1E)`
→ `idx = ((cellY & $1F)<<8) | (cellX & $1E)`. Then:
```
LDA $7E70C0,X ; if bit7 CLEAR -> SEC, RTS         ; the TREASURE plane (NOT an off-map sentinel)
$1D58 = $7E30C0,X   (byte0 — shape)
$1D5A = $7E7000,X   (byte1 — prio + plane-update flag bit4)
$1D5C = $7E7040,X   (byte2 — z-plane / IGNORE_Z / sprite-front)
CLC, RTS
```
Entry variants: `9AA1` ANDs the live A (caller's pos); `9AC8`/`9AD3` AND a fresh value; `9AB2` is
the shared body. (`9AD3` also pre-clears the shape outputs at `9ADB`.)

### 3b. Sub-tile SHAPE decode — C0/9AD3-9BF4 (verified byte-exact)
```
$1D56 = $1D68 >> 2                  ; Y proxy
shape = ($1D58 & 0xFC) >> 2         ; 6-bit shape id (== maps.py (b0>>2)&0x1F)
shape==0 -> walkable tail (9B41)
shape==1 -> solid tail   (9B62)
shape 2..0x17 (orig) -> SLOPE eval via tables; shape>=0x18 -> one-way special (9B86)
```
**Slope eval** (X index = `orig_shape - 2`, 22 entries), using sub-tile X = `$1D66`:
```
sel = C09BF5[X]                     ; bit7 = negate-after-shift; bits0-6 = LSR count (1..3)
a   = $1D66 >> (sel & 0x7F)
if (sel & 0x80) a ^= 0xFF
a   = a + C09C0B[X]                 ; 8-bit
a   = a - $1D56
a  ^= C09C21[X]                     ; 0x00 keep / 0xFF invert
(a & 0x80)==0 -> WALK  ;  else -> SOLID
```
Tables (read from ROM `$C0:9BF5/9C0B/9C21`, verified):
```
C09BF5 = 82 02 02 82 81 01 01 81 83 03 03 83 81 01 01 81 83 03 03 83 82 02
C09C0B = 3D 03 03 3D 7D 03 C3 3D 3E 23 03 1D 3D C3 03 7D 1E 03 23 3D 3D 03
C09C21 = 00 00 FF FF 00 00 FF FF 00 00 FF FF 00 00 FF FF 00 00 FF FF 00 00
```
**One-way special shapes** (9B86, `shape-2 ≥ 0x16`, i.e. orig 0x18..0x1E) gate on the **sign of
the tentative position low byte** `$1D66`(X)/`$1D68`(Y), not velocity:
`0x18`→solid iff X≥0; `0x19`→solid iff Y≥0; `0x1A/0x1B/0x1C/0x1D` = directional corners;
`0x1E` = **conveyor**: sets `$1D5E=0x04` and forces `$1D30` to ±$10 by its current sign.
Anything ≥0x1F falls through to walkable.

**Output tails** (the z/prio bytes the z-gate then reads):
```
WALK (9B41):  $1D6A = byte2 & 3        $1D6C = byte2 & 4
SOLID(9B62):  $1D6A = (byte2 & 0x18)>>3 $1D6C = byte2 & 0x20
both:         $1D6E = ((byte1>>2)&0x10)|0x20    $1D6F = ((byte2>>2)&0x10)|0x20
```

### 3c. Z-plane gate — C0/9C37 (carry SET = SOLID; verified, matches engine.py `_zsolid`)
```
if ($1D6C != 0)              return ($0155 == 3);   ; IGNORE_Z tile: solid iff player on plane 3
z = $1D6A
if (z == 0)                 return SOLID;           ; transition-solid
if (z == 3)                 return WALK;            ; transition-walkable
if (z == $0155)             return WALK;            ; same plane as player
if ($0155 == 3)             return WALK;            ; player on transition plane
return SOLID;                                       ; the other plane
```

### 3d. Per-axis commit — C0/9C5C (clamp velocity; update plane/prio/conveyor)
```
REP#$20
$1D66 = $1D70 = $1D62 + $1D2E       ; tentative X
$1D68 =          $1D64 + $1D30      ; tentative Y
$1D72 = $1D68 -/+ 0x100             ; wrap-neighbour (sign from $1D30 bit7)
SEP#$20
prop_fetch(9AC8); shape_decode(9AD3); if (zgate(9C37) == SOLID) { $1D2E=$1D30=0; return; }
trigger_lookup_and_door (9CA1)      ; touch-trigger ($7E7080 8-bit idx -> $E50000 table) + door bit
plane_update (9D54): if ($1D6C==0) { $0155=$1D6A; $0C00[$0197]=$1D6E; $0C01[$0197]=$1D6F; }
conveyor (9D6E): see below
```
`9C5C` **does not** write the world position. It only clamps the velocity accumulators and updates
side state. Position integration stays in **C0/AAFD**.

**Conveyor / auto-move** (9D6E-9DBD) — the source of the `--playerframe` "momentum" residual:
```
sel = $1D5A & 0x0C
if (sel==0) { $1D2A = $1D2B = 0; return; }
speed = sel==0x04 ? 0x08 : sel==0x08 ? 0x10 : 0x20
switch ($1D5A & 0x03):
  0: $1D2B = -speed; $1D2A = 0    ; up
  1: $1D2B = +speed; $1D2A = 0    ; down
  2: $1D2A = -speed; $1D2B = 0    ; left
  3: $1D2A = +speed; $1D2B = 0    ; right
```
`$1D2A/$1D2B` are the **seed** for next frame's `$1D2E/$1D30` (C0/88E5), so a conveyor tile injects
a persistent velocity = "momentum."

### 3e. Scene-load init — C0/9DC3 (sets the player's initial plane `$0155`)
Fetches the spawn tile's props, `$0155 = $1D6A` (or 1 if 0); player gets `($1D6E,$1D6F)` priority,
party 2 & 3 get **`$1D6F` in both** of their `$0C00/$0C01` bytes (a quirk, verified).

## 4. The player edge/bound clamp — C0/99DE (PORTED 2026-07: `ct_vm.c` camGate99DE, inside CtSceneCameraTick)

`99DE` clamps the velocity accumulators `$1D2E`(X)/`$1D30`(Y) against the **scroll/map rectangle**
(NOT tile collision — that is the 8A6D/9C5C path). It uses the half-tile foot positions
`$1D0A/$1D0C` (X) `$1D0E/$1D10` (Y), the tile bounds `$1D1A`=left/`$1D1B`=right/`$1D1C`=up/`$1D1D`=down
(from scene header bytes h[10]/h[12]/h[11]/h[13]; `0x80`=no-clamp), the sub-tiles `$1D93/$1D96`, and
the on-screen coords `$0A00,X/$0A80,X`. `mag = abs(vel) >> 4`. **Must-preserve asymmetry:** the X
dispatcher has no fall-through, but the Y dispatcher **falls through** from down-blocked into the up
branch with a stale A. Full code: `_collision_specs/10_VERDICT_*99DE*.json`. (Leaves never fired in
the idle trace — validate against a clamped map edge.)

## 5. Actor-vs-actor collision — C0/5B95 — DONE + VALIDATED (the NPC mover's collision)
The NPC move-step `C0/5129` flows: arrival-check -> (not arrived) `JSR $ABA2` (heading `$EE`) ->
**`5170: JSR $5B95; BCC $517C; else $EE += 0x30`** (the actor-collision heading nudge) -> reload counter
`$1A01` -> `ACFD` velocity build. So `5B95` is the NPC's actor-vs-actor collision; on a block it nudges
the heading by `+0x30` (which re-aims the velocity).

`5B95` (`src/collision.c:CtCollisionActorMovable`, returns 1=blocked): `STZ $EA`; if the mover
(`$6D`) is inactive (`$0F00`==0) -> movable; **GATE: if `$1C80[mover] & 0x02` is clear -> movable**
(no scan); else scan the actor list (`X = $7F2000 * 2`, step -2, **idx*2 stride**) skipping inactive
(`$0F00`==0 / `$1100` bit7), self (`CPX $6D`), and non-solid (`$1B01` bit0 clear); for each solid
candidate `JSR ($5BF2 + facing*2)` -> a box-handler (up `5BFA` / down `5C20` / left `5C47` / right
`5C6D`): **a solid actor within a 1-tile (`0x100`) box ahead in the facing dir + within `0x100`
laterally -> blocked**. ROM carry quirks (byte-exact): the primary-axis SBC inherits carry from
`CPX $6D` (`candIdx >= moverIdx`), the lateral SBC from `CMP #$0100` (clear), and abs uses
one's-complement (`EOR #$FFFF`, no INC).

**The `$1C80 & 2` gate is CLEAR for every field NPC in the whole trace** (5B95 runs 7×, all bail at the
gate `5BA5`; 0 hits on the box-handlers/nudge). So the box-handlers are unreachable via live play and
were validated by **micro-execution** (`--sweepactor`: synthetic 2-actor table, gate ON, sweep
facing × candidate-offset × `candIdx≷moverIdx`): **1352/1352 byte-exact, 256 blocked**. Wired into
`CtActorMoveStep` (the `5170` nudge) with **no `--portrun` regression (1585/1592 = 99.6%)** since the
gate is off in field play. (The `5B63-5B8C` edge clamps in this region are the player's 9175 clamps,
fixed in §4.)

**Player-vs-NPC collision is already done** via the player path: the 8A6D handlers call `9923`
(`CtCollisionActorBlock`, `--checkactor` 243/243) so the player avoids solid actors. So both directions
of actor collision are byte-exact: player->actor (9923, in 8A6D) and NPC->actor (5B95, in the move-step).

## 6. Collision DATA in WRAM (what the port reads)
The active location's per-tile properties live de-interleaved in WRAM, indexed by the 16-bit cell
`((tileY & $1D1F)<<8) | (tileX & $1D1E)`:
- `$7E30C0` byte0 (shape),  `$7E7000` byte1 (prio + bit4 door-on-step),  `$7E7040` byte2
  (z-plane bits0-1 / IGNORE_Z bit2 / sprite-front bit6),  `$7E70C0` off-map sentinel (bit7),
  `$7E7080` 8-bit touch-trigger index plane.
- Player z-plane `$7E0155`; player index `$7E0197`; per-actor OBJ prio `$7E0C00/$0C01`.
- Byte layout cross-checked against `assets/maps.py:scene_props` (`shape=(b0>>2)&0x1F`, `z=b2&3`,
  `IGNORE_Z=b2&4`, door=`b1&0x10`, sprite-front=`b2&0x40`). **The Python `solid_at` is a SIMPLIFIED
  model** (0..15px, slopes treated fully solid, no one-way/conveyor) — the C port must use the
  ROM-table slope evaluator above, NOT `solid_at`.

Because these planes already live in the oracle's WRAM, the validation harness can read them
straight from the copied RAM (as `--playerframe` already copies `s->ram`).

## 7. Incremental port plan (each step byte-exact-validatable vs the oracle)

**STEP 1 — Empirical probe `--watchcollide` — DONE (`src/main.c:runWatchCollide`).**
Drives Crono in his room holding each direction, per-opcode-tallies the collision PCs, and samples
`$0120`/velocity/plane at each `C0/881E` entry. **RESULT (oracle, in Crono's room):**
- `$0120 == 0x01` while walking in **all four** directions ⇒ **the `8A6D` path IS the normal
  free-roam tile-collision path** (it is gated by `$0120 != 0`, and `$0120` is set in free-roam).
  The earlier "forced/auto-walk ALT path" framing is too narrow — it is *the* player mover-collision.
- Per ~90 walked frames: `8A6D`=89, the matching directional handler fires (E/W/S=88, N=15), `9C5C`
  commit fires every moving frame (15–88), and `9AA1`/`9AD3`/`9C37` run **several times per frame**
  (the handler's multiple feeler probes: prop≈2×, shape/zgate≈3×/frame). `99DE`+`91AC`=89 (every frame).
- `accVel` sampled at `881E` entry is `(0,0)` (correct: that is before the d-pad dispatch adds to the
  seed). Crono moved freely (no wall hit within 90 frames) — a **wall-stop (BLOCKED) capture is still
  needed** (walk longer / start adjacent to a wall) to pin the clamp's exact stop pixel for STEP 3.
- TODO: dump a multi-plane scene (scene 7, `zplane.md`) for the z-gate.

**STEP 2 — Shape decoder `CtCollisionShapeDecode` (port of C0/9AD3) — DONE + VALIDATED.**
`src/collision.c` ports 9AD3 byte-exact (clears + `$1D56=$1D68>>2`, `shape=(byte0&0xFC)>>2`, the two
tails, the slope-table evaluator over `C0/9BF5/9C0B/9C21` (embedded as verified consts), the one-way
specials, and the conveyor `$1D30` force). Harness `--checkcollide` (`src/main.c:runCheckCollide`):
hooks the oracle at each `C0/9AD3` entry, snapshots its inputs, runs the C port on the snapshot, steps
the oracle to the routine's RTS, and diffs the outputs `$1D5E/$60/$6A/$6C/$6E/$6F` + conveyor `$1D30`.
**Result: 975/975 byte-exact, 0 mismatch**, over shapes 0 (walk, ×740), 1 (full-solid, ×28), 0x18/0x19
(one-way, ×166/×41) in Crono's room.
The prop-fetch (9AA1) + z-gate (9C37) are trivial and will be folded into the end-to-end STEP 3.

**STEP 2b — exhaustive validation by MICRO-EXECUTION — DONE.** `--sweepcollide`
(`src/main.c:runSweepCollide`) calls the real ROM routine `C0/9AD3` in the oracle on synthetic inputs:
for each combo it writes the inputs to WRAM, sets up a 65816 call (DP=$1D00, A/X 8-bit, sentinel return
addr, PC=C0/9AD3), runs the CPU to the RTS via `CtOracleRunToPC` (`cpu_runOpcode` only — no PPU/IRQ),
and diffs the oracle outputs against the C port. Sweep = all 64 shape ids × 3 byte1 × 4 byte2 × 7 subX ×
4 subY. **Result: 21504/21504 byte-exact, 0 mismatch, 0 no-return.** This runtime-validates the slope
evaluator (shapes 2..0x17, the slope tables + shift/add/sub/eor), every one-way shape (0x18..0x1D), and
the conveyor (0x1E, all three `$30` branches) against the real ROM — closing the STEP-2 coverage gap.
(Reusable technique: micro-execute any pure ROM leaf with swept inputs to validate a port exhaustively.)

**STEP 3a — collision DECISION primitive `CtCollisionSolidAt` — DONE + VALIDATED.**
`src/collision.c`: `CtCollisionSolidAt` composes prop-fetch (9AA1: cell = `(($1D69<<8|$1D67)) &
($1D1F<<8|$1D1E)`, off-map iff `$7E70C0[X]` bit7 clear) + the validated shape-decode (9AD3) + z-gate
(9C37 -> `CtCollisionZGate`). Harness `--checksolidat` (`src/main.c:runCheckSolidAt`) hooks the oracle's
real `C0/8A9E` player probe while walking, runs the C port on the snapshot, and diffs the solid verdict
(vs oracle carry), the off-map flag (vs oracle A), and the decoded props. **Result: verdict 630/630,
props 630/630 byte-exact** (630 in-situ probes). This is the primitive the 8A6D handlers call.

**STEP 3b — the collision core routines — DONE + VALIDATED.**
- `CtCollisionCommit` (9C5C) — `--checkcommit` **190/190** byte-exact (the velocity-zeroing wall-stop +
  plane/prio/conveyor update; the 9CA1 trigger/door side-state is intentionally not modelled).
- `CtCollisionActorBlock` (9923) — `--checkactor` **243/243** byte-exact (clear path runtime-validated;
  the blocked path stays static-verified — no solid actor entered the box during the room walk).

**STEP 3c — the 8A6D dispatch + all 8 directional handlers — DONE + VALIDATED.**
`CtPlayerCollide` (8A6D) + the 4 cardinals (EAST 8AB5 / WEST 8BC4 share the 8AE4 mid; NORTH 8BF9 /
SOUTH 8D11 share a body) + the 4 diagonals (NE 8E21 / NW 8EF1 / SW 8FC1 / SE 909C — corner-box probes
with cardinal fallbacks). Transcribed with carry-tracked 16-bit ALU helpers (`adc16`/`sbc16`) so the
ROM's carry-chained feeler math is byte-exact per-site. `--checkclamp` in-situ: **cardinal 143/143 +
diagonal 137/137** byte-exact vs the oracle's 8A6D.

**STEP 3 (wiring) — DONE.** `src/player.c:CtPlayerControl` calls `CtPlayerCollide` between the d-pad
dispatch and the 9175 edge clamp, gated by `$0120 != 0` (the C0/881E order at 8847).

**STEP 4 — the "momentum residual" was a real bug + a harness artifact (both fixed). DONE.**
The investigation (`--tracemove` dump of the oracle's per-frame chain, then `--checkctrl` hooking 881E
with correct mid-frame timing) showed it was NOT a missing accel subsystem:
1. **Bug:** the C0/9175 edge clamps in `player.c` were provisional and WRONG — UP/DOWN were swapped
   (9175 calls 5B71 for up / 5B63 for down) and 5B71/5B86 used a flipped compare. Fixed byte-exact from
   C0/5B63-5B8C. `--checkctrl` 310/400 -> **400/400** velocity byte-exact (full CtPlayerControl).
2. **Harness artifact:** the control gate `$011F` flips 0->1 mid-frame, so feeding 881E a post-frame
   snapshot mis-phased it. `--playerframe` now feeds the previous frame's gates.

**RESULT: `--playerframe` position 73/270 -> 139 -> 264 -> 270/270 BYTE-EXACT** over the full d-pad sweep
(all 8 directions + wall-stops). The player field mover — input -> velocity -> tile collision (8A6D) ->
edge clamp (9175) -> AAFD position integration — is byte-exact end-to-end. **M2 field collision +
player mover: COMPLETE.** (The `--playerframe` applied-velocity field still reads 196/270 only because it
diffs the oracle's `$1900` register, which holds a stale value when a move is blocked; position is the
definitive end-to-end check and is 270/270.)

**STEP 4 — 99DE map-rect clamp + the remaining momentum.** Port 99DE (the four leaves) and confirm
the non-conveyor momentum source (the `$1D2A/$1D2B` seed carry-over) to drive `--playerframe` to a
clean byte-exact position over the full d-pad sweep.

## 8. Open risks / lowest-confidence items
- **`$0120` semantics** (STEP 1) — the whole player-collision path hinges on when 8A6D runs. Resolve
  empirically before STEP 3.
- ~~The slope/one-way/conveyor paths are static-only.~~ **CLOSED by STEP 2b** (`--sweepcollide`):
  the shape decoder C0/9AD3 is now exhaustively byte-exact vs the micro-executed real routine over all
  64 shapes. The **99DE leaves** remain static-only (0 trace hits) — validate against a clamped map edge.
- `9CA1` trigger-lookup / door-on-step writes side state (`$0112-$0116`, `$015B`, `$0117`) — not
  needed for the position byte-exactness, but port it before relying on door/touch triggers.
- `5B95` actor collision + the `5B63-5B86` edge clamps already in `src/player.c` — re-verify the
  four edge clamps match exactly.

---

## CORRECTION — `$7E:70C0` is the TREASURE plane, not an "off-map / hard-solid sentinel"

This dossier (and `src/collision.h` / `src/collision.c`, which quote it) labelled `$7E:70C0` an off-map
sentinel. It is not. Its **only two ROM writers** are inside `C0/A712`, the routine that builds the
location's treasure map at every load: it fills the whole 64x64 plane (stride `$100`) with `$80` and then
stores each treasure record's SLOT at `(record.tile & $1D1E)`. A `--peek` of the entire plane in loc 26 has
**exactly three** non-`$80` cells -- `(30,37)=00 (26,39)=01 (27,39)=02`, its three records in slot order.

The *behaviour* the old label described is real -- `C0/9AA1` returns SEC (solid, A=1) for any cell whose
plane byte has bit7 clear, so **the plane alone makes every chest tile impassable**, independent of the
tile's own props -- which is exactly why the mislabel survived. Off-map is done by the `$1D1E/$1D1F` mask,
not by this plane. Full derivation: [field_treasure_chests.md](field_treasure_chests.md).
