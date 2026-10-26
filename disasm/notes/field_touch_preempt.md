# THE TOUCH PREEMPT — how a pacing NPC starts an encounter, and the three things the port is missing

Derives the mechanism behind a question that turned out to be **malformed**. The investigation began
as "why does the port loop forever on two op-0xA0 pace steps where the ROM runs through them into the
encounter". The answer is that **nothing runs through them**: the pace is a closed infinite loop in
both engines, and the ROM leaves it only by *preemption from outside the script*.

Worked example throughout: loc `$082` (Manoria corridor), scene 130, script 178, seeded from
`saves/battle_re/party3_field.state`; walk LEFT and the Diablos encounter fires at frame 123.

---

## 1. THE PACE IS AN INFINITE LOOP BY CONSTRUCTION

Script bytes at `$7F2000+0x0AB0` (`--read "wram:7F2AB0+48"`):

```
0AB0: 83 48        op 0x83 LoadEnemy, id 0x48 = Diablos   (ROM stats $CC:4700+id*23 -> $CC:4D78, HP 50)
0AB2: 0A
0AB3: 8B 1F 2B
0AB6: 1B
0AB7: 00
0AB8: A0 23 2B     pace to tile (0x23,0x2B)
0ABB: A0 1F 2B     pace to tile (0x1F,0x2B)
0ABE: 11 07        JUMPBACK  ->  0x0ABE - 7 = 0x0AB7
0AC0: 00           <- fn slot 2 (the TOUCH function) starts here
0AC1: 87 02
0AC3: E3 00        the control lock ($011F = 0, writer C0:3C0D)
0AC5: 0C 42
0AC7: 02 44 34
0ACA: 04 ...
```

**op 0x11 = C0/62CB = a 2-byte BACKWARD relative jump**, `newIP = (opcodeIP + 1) − N`. So
`A0 / A0 / 11 07` closes on itself and `0x0AC0` is **never reached by fallthrough**. The port looping
here is *correct*.

(Decode correction: `0AC5` is `0C 42` — op 0x0C is 2 bytes, C0/6282 — and `0AC7` is `02 44 34` — op
0x02 is 3 bytes, C0/5FB6. The split `0C 42 02 44` + `0AC9: 34 04` is wrong; only the split above lands
the IP on the measured `0x0AC9`.)

**Field event-VM dispatch table: `C0/5D6E`, 256 × 16-bit.** Confirmed entries:
`0x00→C0/5F74  0x02→C0/5FB6  0x0C→C0/6282  0x0E→C0/62A4  0x10→C0/62B5  0x11→C0/62CB
0x87→C0/4876  0xA0→C0/5129  0xAF→C0/2FFD  0xB0→C0/3002  0xE3→C0/3C05`

---

## 2. op 0xA0 (C0/5129) — arrival is TILE-INDEX EQUALITY

```
5129: LDX $6D          ; X = obj*2
512B: LDA $1A01,X      ; step ticket
512E: BEQ ->5133
5130: TYX / CLC / RTS  ;   busy -> YIELD, IP untouched (Y = entry IP)
5133: TYX / INX / LDA $7F2001,X -> $F2    ; target tile X
513B: INX / LDA $7F2001,X -> $F3          ; target tile Y
5142: LDX $6D
5144: LDA $1801,X -> $F0                  ; CURRENT tile X
5149: LDA $1881,X -> $F1                  ; CURRENT tile Y
514E: CMP $F3 / BNE ->516D
5152: LDA $F0 / CMP $F2 / BNE ->516D
5158: LDA $1C81,X / BIT #01 / BEQ ->5164
515F: JSR C0/305D / BCS ->516A            ; snap still running -> yield
5164: TYX / INX / INX / INX / SEC / RTS   ; COMPLETE, IP += 3
516A: TYX / CLC / RTS
516D: JSR C0/ABA2                         ; atan2 -> heading $EE
5170: JSR C0/5B95 / BCC ->517C / $EE += 0x30    ; object-collision sidestep
517C: LDA $1000,X / STA $1A01,X / STA $7F0B00,X ; re-arm ticket = cadence
518A: JSR C0/ACFD                         ; velocity $1900/$1980 + prio cell
518F: CLC / RTS                           ; yield, IP untouched
```

`$1801,X` / `$1881,X` are the **high bytes** of the 1/16-px position (`px*16 >> 8`) = the **tile
index**. So arrival is tile equality — not a distance, not a tile centre, and not the ticket reaching
zero. `$1A01` only decides *when* the test runs (once per `$1000,X` frames).

The handler **blocks with the IP parked** — but note *how*: it restores the entry IP itself with `TYX`
before its `CLC` (5130, 516A, 518F). **The carry does not mean "did the handler advance".**
`C0/5ABC-5ABF` writes X back into `$1180,X` on **both** paths; the carry only says whether the runner
may keep spending this visit's op budget (`5AB4 BCC` ends the visit, SEC does `DEC $67 / BNE`).
Handlers that advance **and** yield are common — SHOW/HIDE/REMOVE (`C0/4CD9`: `TYX / INX / CLC / RTS`)
are the clearest case, and reading the carry the other way mis-ports every one of them.

This corrected two errors in `field_tick.md` — see the CORRECTED blocks there: the `$1C81` branch was
written inverted (bit0 clear → *complete*, not "idle at target, yield"), and the per-actor stride was
given as `0x80` when it is **`obj*2`** (`0x80` is a table's SPAN). That stride rests on MEASUREMENT —
obj32's IP at `$7E11C0` = `$1180 + 32*2` — **not** on "X is 8-bit", an argument I made and which is
false: the runner does `5A9E: REP #10` before `5AB1: JSR ($5D6E,X)`, so the index is 16-bit inside
every opcode handler.

---

## 3. THE PREEMPT — op 0xB0 on the LEAD, via `$01EB`

At f122 obj32's IP is **overwritten**, not advanced:

```
f122  7E11C0 = 0x0AC0 * (C0:319D)    <- IP := fn slot 2
      7E1C40 = 0x02   * (C0:31A4)    <- priority := 2
      7E1A41 = 0x00   * (C0:31AA)    <- mover ticket killed
      7E01EB = 0x40   * (C0:9963)    <- hit index = obj32*2
```

`C0:319D` is inside **C0/3154**, the touch invoke: skip if `$1C01,X != 0`, `$1100,X` bit7, `$1000,X`
bit7, or `$1C00,X < 3`; else push `$1180,X` onto the return stack `$7F0580,X`, load slot 2 from
`$7F2001 + ($EB<<4) + 4` (= `$7F2001 + obj*32 + slot*2`; obj32 → `$7F2401`, which reads
`AF0A BF0A C00A` → slot2 = `0x0AC0` ✓), then prio = 2 and zero `$1A80/$1A01/$1001`.

**The only caller of C0/3154 in the ROM is C0/3054**, inside the *state-0 (lead PC)* branch of
**op 0xB0 (C0/3002)**:

```
3002: LDA $38 / BEQ ->3009 / TYX / CLC / RTS
300F: LDA $1100,X / BEQ ->3040 (lead) / DEC BEQ ->3030 / DEC BEQ ->3020 (followers)
3040: LDA #01 / STA $1A01,X / JSR C0/9E29
304C: LDA $1600,X / JSR C0/5B8D      ; carry set iff $01EB bit7 clear
3052: BCC ->3057
3054: JSR C0/3154                    ; <-- THE touch invoke
```

`C0/5B8D` is only a sign test: `LDA $EB / BPL ->5B93 / CLC / RTS / 5B93: SEC / RTS`.

`$01EB` is the **hit latch**: reset to `0x80` ("nothing") every frame by the player controller
(`C0/881E: LDA #80 / STA $01EB`) and written with the hit actor's index by the proximity scan
**C0/9923** (`STX $01EB` at 9960 solid, 99A1 non-solid).

Frame order (C0/0097 main loop): `00A7 JSR $881E` — player move + the C0/9923 scan — runs **before**
`00B7 → 00C4 JSR $5A46`, the event-VM. So the VM consumes an index produced earlier in the same frame.

**The probe point is not the player's foot.** From the LEFT block C0/8BC4 (RIGHT block C0/8AB5 is the
mirror): `$1D66 = leadX($1800,$0197) − 0x70 + Xvel($1D2E)`, `$1D68 = leadY($1880) − 0x40`. The 7.0-px
forward offset is **unconditional** — there is no "only when blocked" variant in the ROM. Box test is
`|dx| < $00E0 && |dy| < $00E0` (14.0 px) in 1/16-px units.

Measured at the firing frame:

```
f122  7E1D66 = 0x22E0   7E1D68 = 0x2BC0     ; probe
      7E1802 = 0x2360 (lead X)   7E1840 = 0x2204   7E18C0 = 0x2BFF  (obj32)
      |dx| = 0xDC = 220 < 224      |dy| = 0x3F = 63 < 224
```

A **4/16-px margin**. Without the 0x70 the separation is 332 units (20.75 px) and the box never
closes — so the forward offset is load-bearing, not decoration.

At f123 the touch function runs four ops in one visit — `87 02`, `E3 00` (→ `$011F = 0`, writer
C0:3C0D ✓), `0C 42`, `02 44 34` — landing the IP on `0x0AC9`.

---

## 4. WHAT THE PORT IS MISSING — three divergences, ranked

**(a) `$0F00` is not the ROM's flag. THIS is why no touch can ever fire here.**
The seeded WRAM gives obj32 `$7E0F40 = 0x00`; `src/ct_vm.c:1993` does
`actors[o].draw = (w[0x0F00 + x] == 0) ? 1 : 0` → draw = 1, and `CtSceneActorTouchable`
(`src/ct_vm.c:2832`) rejects `draw != 0`. Nothing in the port ever clears it.
In the ROM `$0F00,X` is **not** a script hide flag — it is a **per-frame camera-window activation
latch** maintained by C0/A810's object pass:

```
A88D: LDA $1100,X / BMI ->A910 ; LDA $1A81,X / BEQ ->A910 / BMI ->A910
A89B: window = [$1D0A-4 .. $1D0C+4] x [$1D0E-2 .. $1D10+5]   (tile*2 compares vs $1801/$1881)
A8E5: LDA $0F00,X / BMI ->A90E                  ; already live
A8EA: LDA #80 / STA $0E80,X / STA $0E81,X / STA $0F00,X
A8F5: STZ $1681,X / STZ $1601,X / STZ $1B00,X / LDA #FF / STA $0F01,X
A905: JSR C0/7170 / JSR C0/6F9A / JSR C0/A9CD
A910: LDA $0F00,X / BPL ->A934 / STZ $0F00,X    ; off-screen -> DEACTIVATE
```

That is the `$0F00 = 0x80` write observed on the oracle at f73, when the camera scrolled obj32 into
range.

**(b) op 0xB0 is a bare park.** `src/ct_vm.c:1198` is
`if (op == 0xB0 || op == 0xB2) return 1;  // forever-yield: park.` — no `$1100` dispatch, no touch
probe. The preempt has no port at all.

**(c) The touch is hand-rolled, and its feeler is INVENTION.** `src/field.c:6712-6735` runs its own
box with `int fwd = moved ? step : (7 + step);` — 1 px on a free walk, the 7 px only when blocked —
plus an edge-inclusive `(!moved && dxp <= 14 && dyp <= 14)` variant and a `touchLatch[]` array. The
ROM's offset is unconditional. Relatedly, `CtCollisionActorBlock` (`src/collision.c:403`) is the port
of C0/9923 but returns only its carry and **discards the hit index**: a grep for `01EB` / `hitObj`
across `src/` returns nothing.

Separately, the port's op-0xA0 arrival test (`src/ct_vm.c:974-984`) is *tile centre* —
`cx = dtx*16+8; if (fabs(dpx) < 1 …)` — where the ROM tests tile equality. Measured consequence: the
oracle finishes obj32's first pace leg at f41, the port at f66.

---

## 5. THE FIX, AND WHY IT IS NOT WRITTEN YET

Rejected outright: any `sceneNo == 130` branch, any per-object allowlist, any tuning of the 7-px
feeler or the 14-px box to make this one encounter fire.

- **Fix 1 (op 0xA0 arrival):** replace the tile-centre float test with tile-index equality per
  C0/5129, `$1C81` bit0 clear → complete. Blast radius is real — obj25/obj28 in this same scene and
  Mom's cooking pace also run `A0`, and the current centre test carries a comment saying it exists
  *for* Mom. `--checkstep` / `--porttick` must be green before and after.
- **Fix 2 (the preempt) — fully derived:** have `CtCollisionActorBlock` write `ram[0x01EB] = 0x80` on
  entry and the hit index on an in-box hit; make op 0xB0 dispatch on `$1100` and, on the lead branch,
  invoke the touch for object `$01EB / 2`; add C0/3156-3163's three missing guards to `CtSceneTouch`;
  and **delete** the hand-rolled box in `field.c`.
- **Fix 3 (`$0F00` activation) — STOP.** Without it obj32 is untouchable no matter how correct 1 and
  2 are, and it **cannot be specified byte-exact yet**. Must be disassembled first: C0/A810 in full
  (the A81B entry filter and every branch between A819 and A88C); **`$1A81,X`**, the unknown gate at
  C0/A894; the activation tail C0/7170 (a `$1100`-indexed dispatcher `JSR ($7181,X)`), C0/6F9A,
  C0/A9CD; the deactivation tail C0/EA42, C0/734C, C0/7056, C0/A98A; and confirmation that the port's
  unified camera exposes `$1D0A/$1D0C/$1D0E/$1D10` in the same units (the compares are `ASL`'d, i.e.
  tile*2).

**Do not ship a stand-in for Fix 3** ("treat off-screen objects as touchable", "activate everything at
load"). That is precisely the heuristic-for-a-mechanism the mandate forbids, and it would silently
change every scene's touch set.

---

## 6. STILL NOT DERIVED

- **`$1A81,X`** (C0/A894) and C0/A810's object-selection filter — blocks Fix 3.
- C0/7170 / C0/6F9A / C0/A9CD / C0/EA42 / C0/734C / C0/7056 / C0/A98A — the activate/deactivate tails.
- **`LDA $38` at C0/3002** — the op-0xB0 global early-out, and which DP is in force there.
- **`$1D2E`** — the lead's X-velocity generator (C0/88EE / C0/9175 not disassembled). Only
  back-computed as −0x10 at f122 from `0x2360 + v − 0x70 = 0x22E0`.
- Which of the eight C0/9923 callers ran at f122 (C0:8AD8/8BEA/8C1B/8D2D/8E49/8F1A/8FE6/90BF). No
  call-site hook exists in `ct.exe`; `--writer` reports only the last writer per region per frame.
- **Whether a NON-SOLID in-box actor's `$01EB` survives to op 0xB0.** `STX $01EB` at 99A1 is
  confirmed, but on the non-solid path the caller continues the movement chain and a later probe in
  the same frame can reset the latch to 0x80 (writer C0:9941 seen on non-firing frames). Untested —
  **do not port the non-solid case as "it still fires".**
- C0/3020 / C0/3030 (the follower branches of op 0xB0) beyond their entries; C0/305D beyond its first
  three instructions; C0/AAA3, which force-zeroes the mover ticket at f44 and f118.
- Whether any indirect/computed path other than C0/3054 reaches C0/3154. Every static JSR/JMP in bank
  C0 and every JSL bank-wide were ruled out; JML and indirect-table entries were not scanned.

---

## 7. THE $0F00 WINDOW, MEASURED — 97%, and the residual is NAMED

Before writing the activation pass, the window predicate was tested against the oracle's own `$0F00`
rather than assumed. Dumped `$0F00 / $1800 / $1880 / $1A81 / $1100` for all 64 slots plus the camera
`$1D0A/$1D0C/$1D0E/$1D10`, every frame of the 130-frame leftward walk at loc `$082`
(`--monitor "7E0F00:128,7E1800:128,7E1880:128,7E1A81:128,7E1100:128,7E1D0A:8" --btnscript "40:130"`),
then swept the margins and the inclusive/exclusive choices.

**Model tested:** `live = !($1100 & 0x80) && $1A81 != 0 && !($1A81 & 0x80) && in-window`, where the
window uses the object's TILE (`$1801`/`$1881`, the position high bytes) against the camera edges
`>>1`.

**Result: the best window is the PLAIN camera rect — `[lt, rt) x [tt, bt)`, all margins ZERO, right
and bottom EXCLUSIVE.** 138 of 4680 samples wrong = **2.95%**.

That **refutes the `[-4 .. +4] x [-2 .. +5]` margins** as tile offsets: the sweep covered 0..6 tiles
on each edge and 0 won on every edge. (Those figures came from the A89B listing in raw ASL'd units;
as ±2 / −1 / +2.5 tiles they also lose to 0.)

**First lesson, recorded because it nearly shipped as a wrong negative:** the same sweep over all 64
slots scores 12% wrong. Scene 130 has **36** objects; slots 36-63 are unused and their `$0F00` holds
stale non-zero bytes. Six phantom objects wrong on all 130 frames were four fifths of the "error".
**Always bound a per-object sweep by the scene's object count.**

**The residual, by (object, `$1100`):**

| object | `$1100` | frames wrong | reading |
|---|---|---|---|
| obj0 | 7 | 130 / 130 | class 7 takes A810's **SPECIAL branch** (`CMP #8 BEQ / CMP #7 BEQ -> A86B`), not the window test at all — so applying the window to it is simply wrong |
| obj32 | 5 | 8 | the pacing NPC, at a window edge: consistent with the A8E5 "already live" short-circuit (activation is STICKY, and the deactivate test at A910 is a separate path) |

Every residual is "predicted OFF / oracle LIVE" — never the reverse.

**NOT IMPLEMENTED, deliberately.** 97% is not a mechanism, and a 3%-wrong activation pass would
silently change every scene's touch and activate sets. What must be derived first is the thing the
residual points at: **C0/A810's entry filter and the class-7/8 branch at C0/A86B**, plus the exact
sticky/deactivate asymmetry (A8E5 vs A910). Both were already on the open list; this measurement
turns them from "unknown" into the two specific things standing between here and a byte-exact pass.

---

## 8. THE ENCOUNTER EPILOGUE — and the one gap that is left

**The ROM's dead-mark is TWO bytes**, from the enemy epilogue `C0/07ED-080C` (m=1/x=0):

```
07ED: LDA $7E29C6,X / BNE ->080D          ; survivor -> restore field position instead
07F3: LDA $1000,Y / ORA #80 / STA $1000,Y ; the cadence pause  (store PC reports as C0:07FB)
07FB: LDA $1A81,Y / BPL ->0807 / (JSR C0/734C)
0807: LDA #00 / STA $1A81,Y               ;                     (reports as C0:080C)
```

`$1A81 := 0x00` — **not 0x80**. That matters beyond taste: `C0/0623-0635`, the next battle's enemy
eligibility scan, does `LDA $1A81,X / BMI ->063F`, so **bit7 makes a defeated enemy ELIGIBLE AGAIN**.
The port had been writing 0x80 (my invention); it now writes the ROM's two bytes.

**Why `$1A81 = 0` is what makes an encounter final**, end to end:
`$1A81 := 0` → `A894 BEQ` → `A910 STZ $0F00,X` → `C0/9933` skips the object → `$01EB` never latches it
→ op 0xB0's lead branch never re-enters `C0/3154` → the fn2's `E3 00` cannot re-fire.

> **Retracted attribution.** An earlier comment of mine said this encounter is finalised by "another
> object hiding the trigger NPC with `7D`". Unsupported — the only measured writers of obj32's
> post-battle `$1A81` are `C0:07FB` / `C0:080C`, the epilogue above. A `7D` would report a writer
> inside `C0/4CD9`. The op-0x7C/0x7D → `$1A81` mirror is still correct as an op semantic; it is just
> not this mechanism.

### The staging gap — RESOLVED (one term)

**The slot table is 8 entries, the candidate list is 6 — they are different arrays.** The "max 6" in
`battle_entry.md` describes `$019D..$01A2`, which `C0/052E-0551` consumes with six unrolled
`LDA $9D..$A2 / BMI / JSR C0/0557` pairs. The slot index `C0/0560` computes indexes a *different*
array, `$01A3..$01AA` (`0574: STY $A3,X`, DP = $0100), and its range is 0..8. **Slot 7 was never a
contradiction.**

```
0558: LDA $0D00,Y / CMP #C0 / BCC ->0560 / RTS   ; eligible iff $0D00,Y < 0xC0
0560: LSR/LSR/LSR  PHA  LSR  SEC SBC #04  STA $D9   ; (v>>4) - 4
056A: PLA  AND #01  CLC ADC $D9                     ;  + ((v>>3)&1)
0574: STY $A3,X                                     ; $01A3+slot = obj*2
```

Against the op-0x83 writer (`C0/47AC`, ported as `loadMonMirror`), `$0D00 = ((slot&0xFE)<<4) |
((slot&1)<<3)` inverts exactly: **the staged slot index is `slot - 4`**, where `slot` is
`(operand2 & 0x7F) + 1`. obj32 `$0D00 = 0xA8` → 7; obj33 `$0D00 = 0x48` → 1.

So neither `C0/0557` nor the port's fill was ever wrong. The defect was **one term in `C0/0617`**, the
list build. Measured at the encounter frame, with every predicate side by side:

```
oracle --monitor 7E0D40:4,7E0F40:4,7E1140:4,7E1841:4,7E18C1:4,7E1AC1:4,7E1D0A:8  (f388)
  obj32  $0D00=A8  $0F00=80  $1100=05  tile=(22,2B)  rel=(9,7)
  obj33  $0D00=48  $0F00=80  $1100=05  tile=(1C,2B)  rel=(3,7)
port   CT_STAGE_LISTDBG=1
  obj33  pl=1  0F00=80  1A81=01  1100=05  0D00=48  rel=(3,7)
  obj32  pl=0  0F00=80  1A81=01  1100=05  0D00=A8  rel=(9,7)
         ^^^^
```

Every ROM byte agrees on both sides. The port dropped obj32 because its port of `C0/0630`
`LDA $0F00,X / BNE` read **`placed`** — `CtSceneActor`'s render-side predicate (`defined && draw==0 &&
!dead && hasPos && !colorGated-closed`) — instead of `$0F00,X`. That stand-in was legal when it was
written: the port had no `$0F00` model. It has one now (the `C0/A810` activation pass, 0/3770), so the
stand-in is simply the wrong byte. Reading `$0F00` makes both arrays byte-identical to the oracle:

| | `$019D` (list) | `$01A3` (slot back-map) |
|---|---|---|
| oracle f388 | `42 40 80 80 80 80` | `FF 42 FF FF FF FF FF 40` |
| port | `42 40 80 80 80 80` | `FF 42 FF FF FF FF FF 40` |

> This is the shape the mandate predicts: a heuristic standing in for a real RAM gate does not fail
> where it was introduced (the render), it fails somewhere unrelated (a battle never ending). The fix
> is the mechanism, and it is one line.

#### What the fixed staging then exposed: the battle side had **six** enemy blocks, and there are eight

Staging obj32 correctly made the encounter what it is on the oracle — a **two**-Diablos fight — and the
battle boot could not hold the second one. It went `GAME OVER`, with the mash hitting
`tgt=10 tgtHP=4112`: combatant 10 = 3 PCs + enemy slot 7, and `4112` = `0x1010`, the *stale WRAM* the
oracle also shows at `$632D` on frame 0. The block was simply never written.

The port's loop stopped at 6 under a comment I had written and never measured — *"only 6 battle enemy
blocks exist — staged slots 6/7 have no block, a data-layout fact of the ROM, not a cap we chose."*
It is exactly backwards. Three independent confirmations that there are **eight**:

- `FD/B438` imposes no bound of its own — `LDA $02 / XBA / LSR A / TAY` is just `slot * 0x80`.
- `C1/8357`, the debug instant-end, sweeps enemies with `CPY #0400 / BCC` → `0x400 / 0x80` = **8**.
- The oracle, in this very battle: `$62AD` (slot 6) = `FF 00 00 01 00 01` (empty: id FF, HP 1/1) and
  `$632D` (slot 7) = `48 00 00 32 00 32` (Diablos, HP 50) — populated exactly like slot 1's `$602D`.

And `FD/B2A9` copies by slot with **no compaction** (`INY` every pass), so `$AF02[7]` legitimately holds
an id; the id half was already right, only the stats were missing.

The same monitor row corrected a second stale gloss. `$AEC6` is **not** the enemy count: `FD/B2A3`
zeroes it and `FD/B2C9 INC $AEC6` is the branch target of *both* the empty (`B2AC BMI`) and hidden
(`B2BA BPL`) tests, inside a loop that always makes 8 passes. It is the slot count, constant 8 — the
oracle reads `$AEC5/$AEC6 = 03 08` while exactly two enemies are alive.

### (historical) The gap as first recorded

| | slot 1 | slot 7 |
|---|---|---|
| **oracle** `$01A3` from f388 | `0x42` = obj33 | **`0x40` = obj32** |
| **port** (instrumented epilogue loop) | `0x42` = obj33 | `0xFF` — empty |

The oracle stages **both** the combatant and the trigger NPC, and at f2309 clears **both** their
`$1A81`. Ours stages only obj33, so obj32 is never removed: it keeps `$1A81 = 1`, the activation pass
keeps `$0F00 = 0x80`, `C0/9923` keeps latching it, and the encounter re-locks control forever. That is
the whole reason the C0/9923 touch consolidation — which already fires at **f122, the oracle's exact
frame** — still ends `explore=0`.

So the next step is `C0/0557`'s eligibility filter and slot-index formula, not the epilogue.
`battle_entry.md` (b) says C0/0557 fills "one per eligible object (`$9D..$A2`, max 6)" with the slot
index from `C0/0560` — obj32 landing in **slot 7** contradicts that "max 6" and is worth checking first.

> Both guesses in that paragraph were wrong, and it is worth keeping them visible. `C0/0557` was
> correct in the port all along, and the "max 6 vs slot 7" contradiction did not exist — it was two
> arrays being read as one. The defect was in the routine I had *not* suspected (`C0/0617`) and in the
> one term of it that was not a ROM byte. Naming a suspect from a doc's phrasing is not a derivation;
> the measurement above is.

---

## 9. THE CONSOLIDATION — landed on the third attempt

field.c no longer has a touch scan of its own. `CtPlayerControl` runs the ROM's `C0/9923` over the live
NPCs, `CtCollisionActorBlock` records the overlapped object in `$01EB`, and `fieldPlayerCollide` hands
that to the VM, where op 0xB0's lead branch (`C0/3054` → `C0/3154`) consumes it. One scan, the ROM's.

What was deleted, and what it cost:

- **The invented forward feeler** — `fwd = moved ? step : (7 + step)`. The ROM's `0x70` projection is
  unconditional; making it conditional on being *blocked* is what put our touch four frames late.
- **The per-actor re-arm latch** (`touchLatch[]`). The ROM re-arms `$01EB` to `0x80` on *every* probe
  entry (`C0/9925`), across all eight directional callers — "last scan wins" is the mechanism, not a
  bug to be worked around with a latch.
- **The `pl`/`placed` gate on the collide's actor mirror** → `$0F00`, the same correction as §8. A
  script-visible NPC merely off-camera at the seed has `draw = 1` and was invisible to the scan.

Kept: the FADE suppression (`if (f.vm && fade) CtSceneSetTouchHit(f.vm, -1)`). That one is **ours**, not
a ROM mechanism — the player may walk during a transition but should not spawn a battle — and it is
labelled as such in the code.

**Measured** (`CT_VMTRACE=32`, loc $082, walking left into the pacer):

```
t123  obj32 addr=0637 prio=7 op=A0     still pacing
t124  obj32 addr=0640 prio=2 op=87     PREEMPTED onto fn2
      f124 obj32 0642: E3 00           the control lock
```

| | preempt frame |
|---|---|
| hand-rolled box | f126 |
| **ROM scan (now)** | **f124** |
| oracle | f123 |

The residual is **one** frame, and it is a read-lag of the same kind `vm-op-a0-pace-arrival` documents.

### Why it failed twice, and what actually unblocked it

`a9561b1` recorded the blocker as: *"obj32 is the encounter's TRIGGER object, not a staged combatant, so
`C0/07C3`'s enemy epilogue never removes it."* **That was wrong.** obj32 **is** a staged combatant — the
oracle puts it in slot 7 with enemy id `0x48` and HP 50 — and the epilogue removes it perfectly well.
Our *staging* dropped it (§8), so it stayed live, `C0/9923` kept latching it, and its fn2's `E3 00`
re-locked control forever. The more-correct scan didn't cause that; it exposed it.

> Worth keeping as a pattern: the blocker was recorded as a fact about the ROM ("obj32 is not a
> combatant") when it was only a fact about our port's output. A blocker phrased as canon is expensive —
> it makes the retry look like it needs new derivation when it needs a bug fixed. Phrase them as what
> was measured, not as what the ROM must therefore be.

## 10. THE CASTLE HALL'S GUARDS (2026-10-26, playthrough leg 36) -- four mechanisms the port had wrong

Loc 015 after the Dragon Tank: the guards of the hall chase Crono (`84 03` solid and pushable, `95 00` in a loop), he
walks into one, and the scene that follows calls functions on Crono and Lucca. Measured on the rule's cartridge with
`--monitor --writer` against the port's memmon (the playthrough's `36_castle_hall`); the leg is SAME after all four.

1. **The push, C0/31B1** (`vmTouchPush`). C0/3154's tail `31AD JSR C0/31B1` runs whether or not the touch installed
   fn2 (only the three refusals at 3156-3163 skip it). A touched object with `$1B01` bit 1 is shoved one tile the
   way the LEAD faces (`$1600,$97`: 0 up 3202 / 1 down 3217 / 2 left 31ED / else right 31DA; up from row 0 and left
   from column 0 refuse), unless that tile's `$7E7040` has bit 7 (the word is not masked). 3228 writes the tile to
   `$7F3781` -- the operands of C0/5D68's program at `$7F3780` (`96 tx ty / FF 9D / 00`, C0/5D26's MVN) --, saves
   the running IP at its own priority (`$7F0580[$1C00 * 80 + $EB]`), sets `$1180,X` = 177F at priority 0, zeroes
   `$1A80/$1A01/$1001` and `TSB $54` #02. `FF 9D` (C0/41DC) clears that latch. Cartridge: `$01EB` = 34 at f809,
   `$7F3780` = `96 0F 28`, obj26 shoved 0ED4 -> 0F31.
2. **ACFD tests the landing tile on every issue** (`mvCrossesSolid`). C0/AD81-ADB4 reads `$7E7040` under
   `$1800 + $DD`, `$1880 + $DB` -- not only when the step changes tiles; a mover standing on a bit-7 tile bounces at
   once (obj26 on (15,39) = C1, f739). A zero step skips the test (AD5F-AD73).
3. **The retry's stale `$DE`.** The random angle's retry sign-extends dx only when it is nonzero (AE97-AEA5); a zero dx
   goes AEA7 -> AEAB with `$DE` as the first test left it, so after a first heading with dx < 0 the probe of a
   straight up/down retry lands one tile (0x100) to the LEFT. obj22 at (1E04,3022), f1399: the retry (0,E1) is clear
   on its own tile (30,48) = 01 but probes (29,48) = 81 and stalls (`$1900` = 0 by C0:AF1F).
4. **The chase reads the target's tile bytes from WRAM** (522D `LDA $1801,X / $1881,X`; 52D8, 5370, 5444 the same).
   The lead moves in A810, after the pass; the port's host moves him before the tick, so the C position showed the
   chasers his new tile a frame early (obj25 re-issued at f801 against the cartridge's f803).

And two for the lead and the party, from the scene's calls (obj18's `04 02 4C` / `04 06 4A`):

5. **A busy lead's step** (`CtSceneLeadDeferStep`). C0/881E runs every frame; the lead moves when his op B0 runs in the
   pass (3040 -> 9E29), and a function another object called on him may RETURN onto it within the pass. f1334: Crono
   on fn12 (`AE / 00`) with Right held, back on B0 (04FB) at the frame's end and `$1802` 6019 -> 7019. The host now
   hands such a step to the VM, which takes it at his B0 (with 9E84's arm on the published intent) or drops it.
6. **A follower whose B0 ran before a later call** (`CtSceneUpdateFollowers`, `ranThenCalled`). A26B ran in her visit
   (the cursor, the ring's velocity, the 9ED1 arm); the call's `STZ $1A80 / $1A01` (C0/6031-6034) only leaves A810
   nothing to integrate. Lucca at f1337: `$01AC` 14 -> 15, `$1806` held at 3018, the step from f1338.
