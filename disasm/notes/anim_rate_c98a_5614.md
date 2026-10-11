# The field ANIMATION RATE — `C0/C98A` reload + `C0/5614` auto-select, and the two clocks in our engine

Derived 2026-08-22 from the ROM (`tools/dis.py`) + the LakeSnes oracle (`build/ct.exe`), against the
user report *"the blue imps in scene 113 seem to have their animation too fast"*.

Companions: [`anim_drain_cad9.md`](anim_drain_cad9.md) (the assembler FIFO the advance rides on),
[`frame_wiring.md`](frame_wiring.md) (where `C0/C98A` sits in the frame),
[`player_anim_9e84.md`](player_anim_9e84.md) (the PLAYER's own `$1680,Y` writers — a different path).

---

## 0. WHICH objects the user can be looking at — measured, not assumed

| scene | imp objects | sprite | how loaded |
|---|---|---|---|
| **112** Truce Canyon | obj15, obj16, obj23, obj24 | **268** | `83 0C xx` LOADMON |
| 112 | obj19 | 269 (same anim-set 33) | `83 0D 08` |
| 112 | obj18 | 273 | `83 11 03` |
| **113** gate clearing | obj12, obj13, obj14 | **268** | `83 0C 03/04/05` |

`<probe> --datfield ct_assets.dat --scene 113` lists only obj1 (spr 104) and obj11 (spr 158) because
that summary prints *statically placed* actors; `python tools/ct.py script 113 --obj 12` shows
obj12/13/14 do `83 0C 0x LOADMON spr=268` in fn0 followed by `91 HIDE`.

**At storyline 0x0C neither host shows a scene-113 imp.** Oracle, teleported into 113:

```
PEEK_SAVE=1 <probe> --teleport 113 8 10 0 --from saves/jogo_trava_lake.state --frames 120 \
    --out $TMP/ctwf/anim/orc113.state
<probe> --read "7E1100+32,7E1580+32,7E1680+32,7E1780+32" --from $TMP/ctwf/anim/orc113.state
  ->  obj12/13/14 (X = 0x18/0x1A/0x1C):  $1100 = 80 80 80   (bit7 = destroyed/inactive)
<probe> --peek $TMP/ctwf/anim/orc113.state out.raw --btnscript "0:90"   -> the clearing, NO imps
```

Our engine at `--scene 113` likewise never activates them (`CT_POSALL=1 ./build/ct_field.exe
--assets ct_assets.dat --scene 113 --selftest x.raw --frames 600` — obj12/13/14 stay
`mode=05 moving=0 anim=0/0` for 600 frames; their fn3 is `CALL/REQ`'d from obj1 fn4, which needs the
post-gate storyline).

**So the Blue Imps visible at this story point are scene 112's**, and every measurement below is taken
there (`saves/jogo_trava_lake.state`, loc 112, storyline 0x0C) where they animate for real.
`<probe> --peek saves/jogo_trava_lake.state out.raw --btnscript "0:120"` shows two of them on screen.

⚠ I could **not** reproduce the reported symptom in a side-by-side: the scene-113 imps are unreachable
from any state in `saves/`, and the scene-112 imps' anim *mode/index* timeline in our engine matches the
oracle's shape. Report this as "not reproduced under the available states", not "absent"
(memory: `verify-negatives-on-proper-state`).

---

## 1. `C0/C98A` — the cadence, byte for byte

Called once per frame per object from `C0/A810` (`A832` on the state>=3 arm, `A878` on the state<3 arm),
**and only when `C0/A88D` returns carry** — the camera-window test. States 7 and 8 skip the whole leg
(`A822`/`A826`), so they never animate.

```
C98A  LDX $6D
C98C  LDA $1601,X          the anim TIMER
C98F  BEQ ->C997           already 0 -> try to advance
C991  DEC $1601,X
C994  BEQ ->C997           reached 0 -> advance
C996  RTS                  still counting -> nothing happens

C997  LDA $1100,X          the FIFO end this object joins
C99A  BEQ ->C9D0             state 0        -> push at the HEAD
C99C  BMI ->C996             bit7           -> NEVER enqueues, NEVER advances
C99E/C9A2  CMP #01/#02 BEQ ->C9E2   states 1/2 -> tail, unless the $78 one-shot jumps the queue
C9A6  CPX $77 / BEQ ->C996   ALREADY THE TAIL -> RTS, timer stays 0, NO ADVANCE
C9AE  LDA $1081,X / BPL ->C996  ALREADY LINKED -> RTS, NO ADVANCE
      ... link in ...
C9C0/C9CB/C9DD/CA06  INC $1681,X    THE PHASE, only on an ACCEPTED enqueue

CA09  LDX $6D
CA0B  LDA $1780,X / BEQ ->CA18 / DEC A / BEQ ->CA18   modes 0 and 1 -> $1680,X
CA13  LDA $1781,X                                     modes >= 2    -> $1781,X
CA1B  REP #20 / AND #00FF / ASL A / ASL A             mod * 4
CA23  CLC / ADC $1580,X / STA $C1                     + the per-object DURATION-TABLE BASE (16-bit)
CA28  LDA $1681,X / AND #00FF / ADC $C1               + the phase   (NOTE: no CLC -> the first ADC's
CA32  TAX / SEP #20                                    carry lands in the index; ported in move.c)
CA35  LDA $E40000,X
CA39  BNE ->CA6E                                      non-zero  = THE STEP'S DURATION IN FRAMES
CA3B  LDX $C1 / LDA $E40000,X / STA $1601,X           zero      = END OF STRIP: reload dur[0]
CA48  LDA $1780,X / CMP #02 / BNE ->CA6A
CA6A  STZ $1681,X                                     ... and rewind the phase (modes != 2)
CA6E  SEP #10 / LDX $6D / STA $1601,X / RTS
```

**A step whose duration byte is `d` is drawn for exactly `d` frames** (verified below, both directions).
`$1580,X`/`$1581,X` is a **16-bit pointer into bank `$E4`**, written by the object loader from the
sprite header's anim-set byte; the strip for anim `k` starts at `+k*4` and ends at the first **zero**
byte — rows freely overrun their 4-byte slot.

### Every writer of the four bytes (exhaustive ROM byte scan, banks C0-C7)

| byte | writers |
|---|---|
| `DEC $1601,X` | **`C0/C991` only** — the timer decrements exactly once per `C98A` call |
| `STA $1601,X` | `2EBA`, `2F21`, `CA45`, `CA72` |
| `STZ $1601,X` | `2E77`, `2EE6`, `2F4E`, `2F7F`, `4336`, `43E9`, `4CB0`, `4CCE`, **`5550`**, **`55F8`**, `565A`, `566D`, `5687`, `56A2`, `A8FA`, `B17D` |
| `INC $1681,X` | `C9C0`, `C9CB`, `C9DD`, `CA06` — all inside `C98A`'s enqueue |
| `DEC $1681,X` | `CA66` (the mode-2 ping-pong) |
| `STA/STZ $1680,X` | `2E6F`, `2E93`, `2ECB`, `2EF0`, `2F32`, `2F58`, `2F89` (VM anim ops), `4545`, `4585` (the LOADER, `:= 0`), **`5654`, `5667`, `5681`, `569C`** (`C0/5614`/`C0/568B`) |
| `STA $1680,Y` | `A17E`, `A1AD`, `A1C4`, `A1DB`, `A21C`, `A246`, `A25D` — **the PLAYER only**, `C0/9E84`'s tails |

**There is no mover among them.** Which raises the obvious question, and answers it:

---

## 2. `C0/5614` — THE canonical idle / walk / run selector

Called from the **13 move-op sites** `C0/4EC0 4FA5 4FE3 5037 507D 50E4 5122 527B 52B9 53C4 5409 54B0
54EE` (byte scan for `20 14 56`). Its sibling `C0/568B` is called from `4E85 4F9C 502D 50DB 5273 53BB
54A8` — the move-op ENTRIES.

```
5614  LDX $6D
5616  LDA $1780,X / BEQ ->5630      mode 0  -> THE AUTO SELECTOR
561B  CMP #03 / BEQ ->5620          mode 3  -> restore ($1680==FF ? STZ $1780 : $1780 := 1)
561F  RTS                           modes 1 and 2 -> NOTHING: the SCRIPT owns the anim

5630  LDA $1900,X / BPL + / EOR #FF / INC A / STA $D9     |velX|
563A  LDA $1980,X / BPL + / EOR #FF / INC A
5642  CLC / ADC $D9                                       speed = |velX| + |velY|
5645  BEQ ->565E                                          speed == 0            -> IDLE
5647  CMP #20 / BCS ->5671                                speed >= 0x20         -> the RUN arm
564B  LDA $1680,X / CMP #01 / BEQ ->5670                  already walk -> NOTHING (the CMP GUARD)
5652  LDA #01 / STA $1680,X / STZ $1681,X / STZ $1601,X   WALK  (anim 1) + phase/timer RESET
565E  LDA $1680,X / CMP #00 / BEQ ->5670
5665  LDA #00 / STA $1680,X / STZ $1681,X / STZ $1601,X   IDLE  (anim 0) + reset
5671  LDA $1100,X / CMP #04 / BCS ->564B                  state >= 4 -> WALK, never runs
5678  LDA $1680,X / CMP #06 / BEQ ->5670
567F  LDA #06 / STA $1680,X / STZ $1681,X / STZ $1601,X   RUN   (anim 6) + reset

568B  LDX $6D / LDA $1780,X / BNE ->RTS                   the move-op ENTRY: force IDLE
5693  LDA $1680,X / CMP #00 / BEQ ->RTS
569A  LDA #00 / STA $1680,X / STZ $1681,X / STZ $1601,X
```

Three facts a paraphrase gets wrong:

1. **The gate is the object's own VELOCITY BYTES, `$1900,X` / `$1980,X`, summed as absolute values** —
   not a boolean "has a move task".
2. **The RUN arm is `speed >= 0x20` AND `$1100,X < 4`** — a per-object RAM branch, *not* "is this the
   player". Sprite 268's anim 6 is `[5,5]` — a **10-frame** cycle against the walk's
   `[15,10,15,10]` = **50 frames**. Getting this branch wrong is a **5x** rate error on this sprite.
3. **The `CMP` guard is load-bearing**: when the id is unchanged, phase and timer are NOT reset. When
   it changes, both are zeroed and the very next `C98A` steps to phase 1.

### `C0/5614` confirmed in the oracle, byte for byte

`<probe> --monitor "7E1918:1,7E1998:1,7E1698:1,7E1798:1,7E1619:1,7E1699:1,7E0F19:1,7E1118:1"
--from saves/jogo_trava_lake.state --frames 120 --diff` (obj12, X = 0x18, `$1580 = 0xB888` = sprite 277):

```
f0..f55   $1798=00 (mode 0)   $1918=00 $1998=E9  ->  |v| = 0x17 < 0x20  ->  $1698 = 01  WALK
          phase 2 f0..f9 / phase 3 f10..f24 / phase 0 f25..f39 / phase 1 f40..f54 / phase 2 f55
          -- fifteen frames each, and sprite 277's anim-1 strip is 0F 0F 0F 0F.  EXACT.
f56       $1698: 01 -> 00      the velocity contribution went to 0 -> the 5665 IDLE arm fired
          $1699: 02 -> 01      STZ $1681 then C98A's INC  ->  phase 1
          $1619: 0F            = sprite 277's anim-0 dur[1] = 15.  EXACT.
```

### `C0/A88D`'s camera gate, measured

Same state, obj15 (X = 0x1E), `--writer`:

```
f0..f67   $161F=00  $0F1F=FF          the anim NEVER ticks -- the object is outside the window
f68       $0F1E: 00 -> 80 (C0:A8F5)   the A88D RISING EDGE latches $0F00,X
f68       $169E: 00 -> 04 (C0:2E72)   op 0xAA ANIMESET 4      $179E: 00 -> 01
f68       $161F: 00 -> 0F (C0:CA48)   anim 4 = one step, dur 0x0F
f69..f82  0E 0D ... 01                                          15 frames
f83       0F (C0:CA48) with $169F(phase) held at 00 (C0:CA6D)   the zero terminator wraps it
f100      $169E: 04 -> 01 (C0:2E72)   op 0xB4 -> ANIMESET 1 (walk)
          $169F: 00 -> 01 (C0:C9C3)   $161F: -> 0A (C0:CA75)    phase 1, dur 10
f110      phase 2, dur 0F   f120  phase 3, dur 0A   f130  phase 0 (wrap), dur 0F
```

That matches scene 112 obj16's script verbatim
(`AA 04 / 8B / E8 4F / 96 MOVETO(14,10) / 89 30 / AA 01 / 96 MOVETO(14,8)`): **the imp does its first
MOVETO on a single static pose (anim 4) and only then switches to the walk cycle.**

### The FACE ops restart the timer without touching the phase

`C0/5545` (op 0xA6 FACE) and `C0/55F0` (op 0xA9 FACETOWARD):

```
5545  INY / TYX / LDA $7F2001,X / LDX $6D / STA $1600,X / STZ $1601,X / CLC / RTS
55F0  LDA $F700,X / LDX $6D / STA $1600,X / STZ $1601,X / CLC / RTS
```

`STZ $1601,X` with **no** `STZ $1681,X`. So a facing change makes the next `C98A` step the animation
forward *immediately*, keeping the phase. This is what produced the one measurement I could not
otherwise explain — obj16 at f106 armed phase 2 with `0x0F` and advanced to phase 3 at f116 with the
timer still reading `06` at f115 (`--writer` shows `C0:CA75` / `C0:C9CE`, i.e. `C98A` really did run
the advance). ⚠ I did **not** pin *which* of the `STZ $1601,X` sites fired on that frame — `--writer`
keeps only the last writer per frame. Open question §7.

---

## 3. Sprite 268 (Blue Imp) — the actual numbers

`_anim_set_index(268)` = `rom[0x24F600 + (268-256)*10 + 3]` = **33**; `dp[33] = 0x24B8D8`. Confirmed
in oracle WRAM: `$1580/$1581` = `D8 B8` for scene-112 obj15/16/23/24 and for scene-113 obj12/13/14.

`$E4:B8D8` duration strip (raw ROM):

| anim | `+k*4` | bytes | cycle |
|---|---|---|---|
| 0 idle | +0 | `05 00 00 00` | 1 step, static, re-committed every **5** frames |
| 1 walk | +4 | `0F 0A 0F 0A` | 4 steps, **50** frames |
| 3 | +12 | `0F 0A 00 00` | 2 steps, 25 frames |
| 4 | +16 | `0F 00 00 00` | 1 step, static, 15 frames |
| 5 | +20 | `0A 00 00 00` | 1 step, 10 frames |
| **6 run** | +24 | `05 05 00 00` | 2 steps, **10** frames |
| 7 / 8 | +28 / +32 | `0F 05 1F 00` | 3 steps |

Scene 113's imps: `90 SHOW / AA 03 ANIMESET 3 / E8 xx / AD xx WAIT / AE AUTOANIME / 96 MOVETO / 71 06`
— so 25-frame flap, then **AUTOANIME → `$1780 = 0`, `$1680 = 0`**, and from that point on `C0/5614`
owns the anim id every move visit.

---

## 4. OUR ENGINE HAS TWO ANIMATION CLOCKS, AND THE WRONG ONE IS DRAWN

### Clock A — the ROM's, ported and graded, but fed garbage

`CtAnimTimerTick` (`src/move.c:847`) is a byte-exact port of `C0/C98A` (`--sweepc98a` 116640/116640)
and it **is live**: `ct_vm.c:519` makes `CT_FRAMELEGS` default ON, and `ct_vm.c:4013` calls it under
the same `C0/A88D` edge as the ROM. It reads `$1680,X` / `$1780,X` / `$1781,X` / `$1580,X` out of the
`$7E` image.

**But nothing in `src/ct_vm.c` ever WRITES `$1680,X`, `$1780,X` or `$1781,X`.** The VM's anim ops
(`ct_vm.c:907-911`, `ct_vm.c:2388-2410`) write a C struct — `CtVmActor.animMode` / `.animIndex` —
and the three `w[0x1680 + x]` occurrences (`ct_vm.c:3169/3174/3188`) are **reads**, in the WRAM seeder.
`C0/5614` and `C0/568B` are not ported at all.

Measured, our engine, loc 112, f100, seeded from the oracle's own WRAM:

```
CT_DUMPWRAM_AT=100 CT_DUMPWRAM=out.wram ./build/ct_field.exe --assets ct_assets.dat --scene 112 \
    --loadwram saves/jogo_trava_loadwram.state --selftest x.raw --frames 101

           ours (f100)                              oracle (f100)
obj15   $1780=00 $1680=00 $1681=00 $1601=02      $1780=01 $1680=01 $1681=01 $1601=0A
obj16   $1780=00 $1680=00 $1681=00 $1601=03      $1780=01 $1680=01 $1681=01 $1601=06
```

`$1680 = 0` -> the ported clock indexes `$E4:B8D8 + 0` = **`05`**, so it reloads every **5** frames
where the ROM reloads every **15** (anim 4) or 15/10 (anim 1). **That is the ROM-faithful clock in our
engine running 3x too fast, for exactly the reason the mandate warns about: the port is right and the
state feeding it is never written.** (`CT_POSALL` confirms the VM struct *does* know: obj15 is
`anim=1/4` from f47 and `anim=1/1` from f79, matching the oracle's f68 / f100 — the values simply never
reach `$7E`.)

### Clock B — a second, ungated implementation, and it is what you see

`animTickActor` (`src/field.c:1643`), called once per actor per rendered frame at `src/field.c:8591`,
keeps its own `AnimPlay {frame, delay}` and is what `drawSprite` renders. Its per-step arithmetic *is*
equivalent to `C0/C98A`'s (I traced both: a step with duration `d` occupies `d` frames in each), so the
DURATIONS are right when the anim id is right. What is not the ROM:

```c
// src/field.c:1662
int want = (mode == 2) ? il : (mode == 1) ? index : (walking ? (running ? 6 : 1) : 0);
// src/field.c:8591-8592
actorAf[i] = animTickActor(&animPlay[i], an, al, am, ai, ail, alp, ast, agen, mv,
                           0, CtSceneActorMoveAnim(f.vm, i), fa, &done);   // NPCs never run
// src/ct_vm.c:4570-4576
int CtSceneActorMoving(const CtScene* sc, int obj) { return sc->hasTask[obj]; }
```

- the mode-0 arm is `C0/5614` replaced by `walking ? (running ? 6 : 1) : 0`, where `walking` is a
  **boolean move ticket** (`sc->hasTask`) and not `|$1900,X| + |$1980,X|`;
- `running` is the literal constant **`0`** with the comment *"NPCs never run"* — the forbidden
  "player vs NPC" heuristic standing in for the ROM's `$1100,X < 4 && speed >= 0x20`;
- there is **no** `C0/A88D` camera gate, **no** `$1100,X` state 7/8 skip, and **no** FIFO-enqueue gate.
  Every one of those three can only make the ROM *slower* than a free-running clock — none of them can
  make it faster. `frame_wiring.md` measures the FIFO one: the drain still has work at the frame
  boundary on 0/90 (bedslide), 1/90 (party3_field) and **24/90** (DEMO8_gtu) frames.
- the FACE ops' `STZ $1601,X` (`C0/5550`, `C0/55F8`) is not modelled either.

### And clock B overwrites clock A's output

`src/field.c:8598` — `CtSceneMem7E(f.vm)[0x0F01 + i*2] = (uint8_t)actorAf[i];` (and `:8694` for the
lead). `$0F01,X` is the byte `C0/CAD9` commits and that `C0/CBDC`'s equality guard
(`CC04 CMP $0F01,X / BNE`) reads back, so the renderer's write also corrupts the ported drain's
commit decision.

---

## 5. The bug, stated once

Our engine draws from a second animation clock that is not the ROM's, and starves the ROM's clock of
the two bytes it selects on. Concretely:

1. `src/ct_vm.c` never writes `$1680,X` / `$1780,X` / `$1781,X`, so `CtAnimTimerTick` (`move.c:847`)
   indexes `$E4:$1580,X + 0` for every object — for a Blue Imp that is `05`, a 5-frame reload where the
   ROM uses 15 or 15/10.
2. `C0/5614` / `C0/568B` are unported, so nothing produces `$1680,X` from `$1900,X`/`$1980,X`.
3. `src/field.c:1662` substitutes `walking ? (running ? 6 : 1) : 0` for `C0/5614`, with `running`
   hard-wired to 0 at `src/field.c:8592` and `walking` = a boolean ticket (`ct_vm.c:4570`).
4. `animTickActor` has none of `C0/C98A`'s three gates, all of which only slow the ROM down.

## 6. Minimal mandate-safe change

Nothing scene-scoped; the whole point is that this is one general machine.

1. **Make the VM anim ops write the ROM's RAM.** In `src/ct_vm.c`'s op handlers (`:907-911`,
   `:2388-2410`) mirror `C0/2E6F` / `C0/2E74` / `C0/2E77` / `C0/2E7C` / `C0/2E80` (op 0xAA/0xB3/0xB4),
   `C0/2E8F`-`C0/2E98` (0xAE), `C0/2EF0`-`C0/2EFA` (0xAB), `C0/2F48`-`C0/2F6A` (0xB7),
   `C0/2F79`-`C0/2F8E` (0xAC) into `$1680,X` / `$1780,X` / `$1781,X` / `$7F0B01,X` / `$1681,X` /
   `$1601,X`. This alone puts the already-graded `CtAnimTimerTick` on the right strip.
2. **Port `C0/5614` and `C0/568B` verbatim** (`|$1900,X| + |$1980,X|`, `#$20`, `$1100,X < 4`, the CMP
   guard, the paired `STZ $1681,X` / `STZ $1601,X`) and call them at the 13 / 7 move-op sites listed
   in §2. Delete `running` and `walking` from the anim path.
3. **Port the FACE ops' `STZ $1601,X`** (`C0/5550`, `C0/55F8`).
4. **Delete the second clock.** Have the renderer take the assembly frame from `$0F01,X` as committed
   by `CtAnimDrainPass` + `CtFieldFrameLookup` (`C0/CB3A`, already ported in `move.c`), and remove
   `animTickActor` + `AnimPlay` + the `$0F01` overwrite at `field.c:8598`/`:8694`. Steps 1-3 are
   prerequisites; step 4 is what actually makes the drawn cadence the ROM's, gates included.

Grade with `--vmdiff saves/jogo_trava_lake.state --frames 200 VMDIFF_WMASK=1` claiming
`$1680`/`$1780`/`$1681`/`$1601`/`$0F01`, plus a `--sweep5614` in the family style of `--sweepc98a`
(seed `$1900`/`$1980`/`$1100`/`$1680`/`$1780`, micro-execute `C0/5614`, compare all four written bytes;
the seed grid must reach all three arms *and* both sides of every CMP guard).

## 7. Open questions

- **The symptom was not reproduced.** The scene-113 imps are unreachable from every state in `saves/`;
  a capture at the storyline where obj1 fn4 runs is needed before "too fast" can be measured against
  the oracle rather than argued from the mechanism. (Direction is derivable: all four missing gates
  slow the ROM down, none speeds it up.)
- **Which `STZ $1601,X` fired on obj16's f116** (phase 2 -> 3 with the timer at `06`). `C0/5550`/`C0/55F8`
  is the only site whose shape matches (timer zeroed, phase kept), but `--monitor --writer` keeps one
  writer per frame and did not name it. A per-write log or `--callrom` would settle it.
- **Is `C0/5614`'s RUN arm reachable for a scripted actor?** No object in any state I read had
  `$1780 == 0` with `$1680 == 6`; the two `$1680 = 06` objects in loc 112 (obj18, obj25) got there from
  a script `AA 06` and carry `$1780 = 01`. The likely live case is a party FOLLOWER (`$1100` 1/2) at
  speed >= 0x20 — unmeasured here.
- **`C0/C98A`'s SEC/enqueue-refusal rate in a live scene** is unmeasured for this scenario;
  `frame_wiring.md`'s 0/1/24-per-90 figures come from other states.
- **obj21 in loc 112 carries `$1780 = 00` with `$1680 = 0C`** — a combination `C0/5614` would
  overwrite on its next move visit. Its `$1100` is 07 (never moved), so it is consistent, but the path
  that left `0C` there was not traced.

---

# Adversarial review (2026-08-22, second agent, read-only on `src/`)

Verdict: the **ROM half (§1-§3) survives** — I re-disassembled every cited address and re-ran every
byte scan and it is right, tables included. The **engine half (§4-§6) does not**: three of its
load-bearing statements are false against the current tree, its headline "3x too fast" is a
mischaracterisation of a static pose, and its one oracle confirmation of `C0/5614` is a
misattribution. **Confidence should be HYPOTHESIS, not DERIVED.**

Probe binary: `cp build/ct.exe "$TMP/ctwf/anim-adv/ct_probe.exe"`; all commands run from the repo root.

## A. What I re-verified and CONFIRM

| claim | how | result |
|---|---|---|
| `C0/C98A`-`CA75` listing | `python tools/dis.py C98A 0xF0` | matches line for line, incl. `CA2E ADC $C1` with no `CLC` and `CA6A STZ $1681` |
| `C0/5614`/`C0/568B` listing | `python tools/dis.py 5610 0xA0` | matches |
| `C0/5545`/`C0/55F0` = `STA $1600,X / STZ $1601,X`, no `STZ $1681,X` | `dis.py 5540 0x20`, `dis.py 55F0 0x20` | matches |
| the four writer tables | my own scan, **whole ROM**, widened to `8D/9C/9F` abs+long and to a 16-bit spill through `$167F,X`/`$177F,X` | reproduces the dossier EXACTLY. `DEC $1601,X` = `C0/C991` only. `STA $1680,X` = 2E6F 2E93 2EF0 2F58 2F89 4545 4585 5654 5667 5681 569C; `STZ $1680,X` = 2ECB 2F32; `STA $1680,Y` = A17E A1AD A1C4 A1DB A21C A246 A25D. `JSR $5614` at the 13 sites, `JSR $568B` at the 7. The only hits outside bank C0 are `FD/1BB7` and `E8/BBB8` (other banks/contexts) |
| sprite 268 strip at `0x24B8D8` | raw ROM read | `05 00 00 00 / 0F 0A 0F 0A / 00 00 00 00 / 0F 0A 00 00 / 0F 00 00 00 / 0A 00 00 00 / 05 05 00 00 / 0F 05 1F 00 / 0F 05 1F 00` — exact |
| `C0/A810` is a FLAT per-frame loop over all objects (NOT scheduler-gated) | `dis.py A810 0xA0` + a `JSR $A810` scan -> only `C0/00D3` and `C2/2D01` | confirmed; state 7 -> `A947` only, state 8 -> nothing, bit7 filtered inside `A88D` by `A892 BMI ->A910` |
| `CC04 CMP $0F01,X / BNE` | `dis.py CBDC 0x50` | confirmed |
| oracle obj15 timeline | `<probe> --monitor "7E169E:1,7E169F:1,7E161F:1,7E0F1E:1,7E0F1F:1,7E179E:1,7E191E:1,7E199E:1,7E111E:1" --from saves/jogo_trava_lake.state --frames 150 --diff --writer` | **exact**: f68 `$169E` 00->04 (`C0:2E72`), `$161F`=0F (`C0:CA48`), `$0F1E` 00->80 (`C0:A8F5`); 15f steps; f83/f98 wrap; f100 ->01 (`C0:2E72`) phase 1 dur 0A; f110/f120/f130 |
| oracle obj12 = mode 0, `$1918/$1998` = 00/E9, 15f steps | `--monitor "7E1698:1,7E1699:1,7E1619:1,7E1798:1,7E1918:1,7E1998:1"` | confirmed |
| OURS at loc112 f100 | `CT_DUMPWRAM_AT=100 CT_DUMPWRAM=... ./build/ct_field.exe --assets ct_assets.dat --scene 112 --loadwram saves/jogo_trava_loadwram.state --selftest x.raw --frames 101` | obj15 `$1780/$1680/$1681/$1601` = **00/00/00/02** vs oracle 01/01/01/0A — reproduced |
| `src/ct_vm.c` anim ops write only the C struct | read `:907-911`, `:2405-2410` | true |
| `CT_FRAMELEGS` defaults **ON** | `src/ct_vm.c:517-521` (`gLegs = (e && *e=='0') ? 0 : 1`) | true (the "DEFAULT OFF" line inside `vmdiff-framelegs-party3`'s note is stale; the pair is still honest because `vmdiff-wmask-party3` sets `CT_FRAMELEGS=0` explicitly) |
| line citations `field.c:1662 / :8592 / :8598 / :8694`, `ct_vm.c:4570` | grep | all exact |

## B. REFUTED

**R1 — "`C0/5614` and `C0/568B` are not ported at all" is FALSE.**
`src/move.c:279` `ctMoveStepAnim5614()` is a byte-exact port *including* the 8-bit `ADC` wrap, the
`5638 STA $D9` side effect into `$01D9`, the `$1100,X >= 4 -> walk` branch, the CMP guard and the
`562D` fall-through. `moveArriveAnim()` (`C0/568B`) follows at `src/move.c:303`, with a note that the
`56A2` store had been the missing one. Both are CALLED from the move-op sites:
`move.c:1480, 1502, 3636, 3657, 3762, 3801, 3833, 3836, 3858`, each tagged with its ROM address
(`4EC0`, `4FA5`, `4FE3`, `54EE`, `4E85`, `4F9C`, `54A8`, ...). **Fix step 2 is already done**;
executing it literally would create a second, duplicate port.

**R2 — "the FACE ops' `STZ $1601,X` is not modelled either" is FALSE.**
`src/move.c:2975` (`// 55F8 STZ $1601,X`), `:3878` (`// 5550`), `:4599`, `:4609`, `:4621` all write
`r[0x1601 + idx] = 0`. **Fix step 3 is already done.** What is missing is only the effect on
`animTickActor` — the two-clock problem, not an unported routine.

**R3 — "`C0/5614` confirmed byte-exact" against the oracle is a MISATTRIBUTION.**
The dossier's single oracle event for `5614` is obj12 at f56. Re-measured per byte with `--writer`:

```
f55  7E1698=01  7E1699=02*(C0:C9C3)  7E1619=0F*(C0:CA75)  7E1798=00  7E1918=00  7E1998=E9
f56  7E1698=00*(C0:569F)  7E1699=01*(C0:C9CE)  7E1619=0F(C0:CA75)  7E1798=00  7E1918=00  7E1998=E9
f57  7E1698=00  7E1699=01  7E1619=0E*(C0:C994)  7E1798=00  7E1918=00  7E1998=E9
```

* the writer is **`C0:569F`** = the store at **`569C`, inside `C0/568B`** — not `5667` inside `5614`;
* **the velocity did NOT go to zero**: `$1918=00`, `$1998=E9` across f55/f56/f57/f58, i.e.
  `|vx|+|vy| = 0x17`, which is `5614`'s WALK arm, not its idle arm.

Widening to every object
(`--monitor "7E1680:64,7E1780:64" --from saves/jogo_trava_lake.state --frames 400 --diff --writer`),
across the whole 200 field frames of that state the only `$1680` writers that ever appear are
`C0:2E83`/`C0:2E77` (the VM anim ops), `C0:569F` (`568B`) and `C0:C9C3`/`C9CE`/`CA6D` (`C98A`);
at f203/f204 `C1:CE04` wipes both rows for the battle.
**No store from `5654`, `5667` or `5681` was observed at all.** `C0/5614` is therefore derived from
the disassembly ONLY — by this project's own two-directions rule that makes it a HYPOTHESIS, and the
dossier's "C0/5614 confirmed byte-exact" citation line must be struck. (Its *silent* path is
consistent with observation: obj12 was already `$1680 = 01` so the CMP guard suppressed the store.)

**R4 — "the ROM-faithful clock in our engine running 3x too fast" is a MISCHARACTERISATION.**
Sprite 268's anim 0 is `05 00 00 00` — **one step**. On expiry `C98A` INCs the phase to 1, `CA35`
reads the zero terminator, `CA3B` reloads `dur[0]`, `CA6A` `STZ $1681,X` puts the phase back to 0.
The drawn assembly frame therefore **never changes**; a 5-frame reload of a one-step strip is visually
identical to a 15-frame one. The oracle shows exactly this for anim 4 (also one step): obj15 holds
`$0F1F = 0D` unchanged from f68 to f99 while `$161F` runs `0F -> 01` twice.
Clock A's real defect is a **WRONG POSE, not a wrong rate**, and `--vmdiff` says so:

```
VMDIFF_WMASK=1 <probe> --vmdiff saves/jogo_trava_lake.state --frames 200
  [wmask] f24  $7E1617 m=1F ours=06 oracle=05     obj11 timer
  [wmask] f56  $7E0F19 ours=02 oracle=01 / $7E1619 ours=0E oracle=0F / $7E1699 ours=02 oracle=01
  [wmask] f64  $7E0F21 ours=04 oracle=0D          obj16 COMMITTED FRAME  <- the pose
  [wmask] f64  $7E1621 ours=05 oracle=0F          obj16 timer (anim 0 vs anim 4 reload)
  [wmask] f96  $7E16A1 ours=00 oracle=01
  [wmask] f136 $7E1603 ours=04 oracle=05
  [wmask] first masked divergence at frame 24; 87 address(es) over 200 frames
```

**R5 — fix step 1 "closes the measured 3x" contradicts the dossier's own §4.**
§4 establishes that clock B is what is drawn and that `field.c:8598` overwrites `$0F01`. Step 1
therefore changes nothing visible; it changes `--vmdiff` / `--verifyframe` / battle-staging state only.

**R6 — "matching the oracle's f68/f100" is not a match.**
`CT_POSALL=1 ./build/ct_field.exe --assets ct_assets.dat --scene 112 --loadwram saves/jogo_trava_loadwram.state --selftest x.raw --frames 140`
gives obj15 `anim=1/4` at **f47** and `anim=1/1` at **f79**; obj16 at f63/f95. The oracle arms the same
two ops at f68/f100. The *interval* is 32 frames on both sides, but our VM is **21 frames EARLY**.
That is a real, measured, unexplained divergence which the dossier reports as agreement.

**R7 — the §2 paraphrase of the mode-3 arm loses the fall-through.**
`5620 LDA $1680,X / CMP #FF / BEQ ->562D / LDA #01 / STA $1780,X / RTS`; `562D STZ $1780,X` is
followed at `5630` by the auto-selector with **no RTS between them**. `src/move.c:287` gets this right
(`// 562D STZ $1780,X -- then FALLS THROUGH`); the dossier's `($1680==FF ? STZ $1780 : $1780 := 1)`
would lead a fresh implementer to return there.

**R8 — §3's table omits anim 2 (`00 00 00 00`).** With `$1680 = 2`, `CA35` and `CA3B` both read 0, so
`$1601` is loaded with **0** — a timer that re-attempts the advance every frame. Worth stating
explicitly next to the other rows.

**R9 — the closest prior art is not cited.** `tools/regress_checks.json` -> `field-cat-anime-once-duration`
pins *the same class of user report*: "the cat's animation is too fast or incomplete" (scene 2's
cat, obj9). Its root cause was **`CtSceneSeedFromWram` restoring only `$1780,X == 2`, so modes 3 and 1
came back as `animMode 0`** — the actor fell into `field.c:1662`'s idle/walk selector and played the
4-step WALK instead of its held pose. That is a *simpler, already-demonstrated* mechanism for
"too fast", and it is not mentioned anywhere in the dossier.

## C. Causes the derivation missed

**S1 (most likely, precedent-backed).** Any path that leaves an actor at `animMode == 0` when the ROM
has `$1780,X` = 1/2/3 drops it onto `field.c:1662`'s `walking ? 1 : 0`. A one-step held pose becomes a
4-step 50-frame walk; a 25-frame `AA 03` becomes a 5-frame idle re-commit. This is the `cat_anim`
defect's shape and it is a genuine *rate* change, unlike R4's.

**S2 (the only mechanism found that makes OURS structurally FASTER).** `C0/C98A`'s **enqueue refusal**:
`C9A6 CPX $77 / BEQ ->C996` (already the tail) and `C9AE LDA $1081,X / BPL ->C996` (already linked)
RTS with the timer at **0**, so the advance is retried the next frame. `sweepc98a-anim-timer`'s own
note states it: *"The animation therefore advances at the rate the assembler drains, not at the rate
the timer fires."* `animTickActor` (`field.c:1643`) has no such stall, so in a crowded scene our drawn
animation advances strictly faster than the ROM's. The dossier lists this as a bullet but never
measures it for loc 112 / scene 113 (`frame_wiring.md`'s 0/1/24-per-90 figures are from other states).

**S3.** The 21-frame VM lead of R6: an actor that arms its anim op early *is* ahead of the oracle.

## D. Is the proposed fix mandate-safe?

* Steps 1-3: ROM-cited, no scene gate, no eyeballed constant — **safe in kind**, but 2 and 3 are
  already implemented (R1/R2), so the genuinely-minimal change is much smaller than stated.
* `running = 0 // NPCs never run` (`field.c:8592`) is correctly identified as the forbidden
  player-vs-NPC heuristic. But the ROM branch it should be **already exists** at `move.c:290-299`;
  the mandate-safe repair is to make the renderer read the one clock, NOT to re-derive `5614`.
* **Step 4 carries an invention risk.** `$0F01,X` is only committed by `C0/CC14` for objects that pass
  `A88D` + the FIFO + `C0/CAD9`'s four gates; every other object keeps the `A902` sentinel **`0xFF`**.
  A renderer that takes its frame from `$0F01` needs an answer for `0xFF` that has no ROM source
  (the ROM's OBJ pipeline simply emits no piece for such an object — which our field renderer does not
  model). Do not delete `animTickActor` until that path is derived.
* The dossier's own §7 says the symptom was never reproduced; **I could not reproduce it either.**
  For the two visible Blue Imps in loc 112 both hosts run the same strips at the same 15/10-frame
  steps in the DRAWN clock (`$0F01` sampled at f5/10/15/20/25/30/40/50/56/60/70 via `CT_DUMPWRAM_AT`
  — `field.c:8598` makes that byte clock B's own output).

## E. Regression checks at risk (read only; the suite was NOT run)

Anything that changes `$1680`/`$1780`/`$1681`/`$1601`/`$0F01` in the `$7E` image moves clock A, and
the legs are ON by default:

* `vmdiff-framelegs-party3` — pins `21 address(es)`, `mask 273 addr`, and two exact `f74` rows.
* `vmdiff-oam-lockstep` — an explicit **known-bad ratchet** ("IT MUST ONLY EVER GO DOWN"):
  `2108 main + 1 high`, `X=1256 Y=328 tile=4 attr=520`.
* `verifyframe-oam-lockstep`, `verifyframe-oam-ourlist` — `0 NON-attr mismatches`, `606 objects`.
* `portrun-field-runner` — `6988/6989 owned-actor-frames`; it compares IP, facing, **anim timer/phase
  and `$0F01`**.
* `sweepcad9-drain-visit`, `sweepdrain-b701-ca76`, `sweepcb3a-frame-lookup`, `sweepc98a-anim-timer`.
* `sweepops-player-script-opcodes` — `12600/12600` over the **whole 128 KB image**; step 1 adds stores
  inside the very handlers it micro-executes, so it grades that change directly.

Step 4 (deleting `animTickActor`) additionally moves every pixel canary and every pose check:
`render-scene1-snapshot` (`fnv32=38684D4D`), `render-scene5-snapshot` (`837BCC29`),
`render-scene6-gato` (`E24AA52E`), `camera-reenter-scene5`, `field-cat-anime-once-duration`
(its `[andbg]` lines read the STRUCT mode — and its subject *is* a too-fast anim),
`field-battletest-gato` / `field-menutest-gato` and the ~80 `field-battle-*` pose checks
(`src/field.c:5087` copies `$0F01` into `$1301`, and the battle staging reads it),
`vm-op-a0-pace-arrival`, `sweeplist-y-sorted-sprites`.

## F. Corrected minimal change

1. Mirror the ROM stores of the anim ops into the `$7E` image inside `src/ct_vm.c`'s handlers
   (`C0/2E6F/2E74/2E77/2E7C/2E80` for 0xAA/0xB3/0xB4, `2E8F-2E98` for 0xAE, `2EE0-2EFA` for 0xAB,
   `2F48-2F6A` for 0xB7, `2F79-2F8E` for 0xAC). This is the only *provably missing* port. It fixes a
   **pose**, not a rate.
2. Do **not** re-port `C0/5614` / `C0/568B` or the FACE `STZ $1601` — they are in `src/move.c`.
3. Before touching the renderer, **capture a state in which the symptom is measurable** and grade the
   drawn `$0F01` per frame against the oracle. Without that number, "too fast" is not a measurement.

## G. Open questions added by this review

* Why is our VM 21 frames ahead of the oracle on obj15/obj16's `AA 04` / `B4` in loc 112 (f47/f79 vs
  f68/f100)? The `AD` waits and the `E8` sound op are the suspects; unmeasured.
* Is `C0/5614`'s STORE path reachable at all in a live field scene? Zero observed stores over 200
  frames of `saves/jogo_trava_lake.state`. Needs a state with a mode-0 actor whose speed crosses 0 or
  `0x20` mid-move.
* What is `C0/C98A`'s enqueue-refusal rate in loc 112 / scene 113? S2 is the only "ours is faster"
  mechanism found and it is unmeasured for these scenes.
* What does the ROM draw for an object whose `$0F01,X` is still the `A902` sentinel `0xFF`? Required
  before `animTickActor` can be deleted.
* Scene 113's imps carry `89 20` (move speed `0x20`) before their mode-0 `96 MOVETO`
  (`python tools/ct.py script 113 --obj 12`). If their `$1100,X` is < 4 there, `C0/5614` picks anim 6
  (`05 05` = 10 frames) where ours picks anim 1 (`0F 0A 0F 0A` = 50) — that would make ours **5x too
  SLOW**, the opposite of the report. Their `$1100,X` in scene 113 is unmeasured (at storyline 0x0C
  the oracle reads `80 80 80` and no imp is on screen).

---

# Repro pass (session 2) — the scene-113 ambush REPRODUCED on both sides, and the rate MEASURED

Third agent, read-only on `src/`, 2026-08-22. Probe binaries snapshotted before anything else
(`build/ct.exe`, `build/ct_field.exe`, `build/SDL2.dll` -> `$TMP/ctwf2/imp/`), every command run from
the repo root, every scratch file under `$TMP/ctwf2/imp`.

**Verdict: NOT REPRODUCED — and this time on a proper state.** The scene-113 Blue Imps were put on
screen, animating, in BOTH hosts, and their drawn assembly frame (`$0F01,X` / `CT_DRAWDBG af=`) was
compared interval by interval: **26 of 26 dwell intervals agree frame-for-frame across obj12, obj13
and obj14, with identical frame ids.** Section 7's and section G's remaining scene-113 worries are
answered below.

## R0. Why section 0 and the adversarial review both failed: the ambush has a DONE-flag, and it was SET

`saves/jogo_trava_lake.state` has `$7F0146 = 0x03`. Bit 0 of that byte is the ROM's
"scene-113 imp ambush already played" flag. With it set the entry script *kills* the imps and the
director object — which is exactly the `80 80 80` both earlier passes measured.

### The script region ct.py never printed

`python tools/ct.py script 113 --obj 0` stops at obj0 fn0's first `00 RETURN` (body `0x007A`).
**obj0 fn0 continues to body `0x018A`**, reached through the else-jumps of two op-`0x16` tests, and
*that* is where the ambush lives. Disassembled with `assets/script.py`'s own `op_len` over
`0x007B..0x018C`:

```
0x0086  16 DB 05 00 46   if $7F00DB == 5  -> 0x008B (the storyline-211 obj15/obj16 arm, imps fn4)
                          else            -> 0x00D0
0x00D0  16 46 01 86 67   if $7F0146 & 01  -> 0x00D5   0A 18 / 0A 1A / 0A 1C  KILL obj12,13,14
                          else            -> 0x013B   THE GATE-ARRIVAL CUTSCENE
0x0116  18 21 05         (reached from 0x00DB when $7F005B != 1) storyline<33 -> 0x0119 0A 02 KILL obj1
0x013B  E3 00 / 02 14 13 / 04 12 13 / E8 70 / FF 90 87 67 20 / AD 08 / 77 0F
0x014E  04 04 17         REQ obj2 fn7   (Crono: AA 24, MOVETO tile(8,11))
0x0154  04 02 13         REQ obj1 fn3
0x0157  4F 08 07         loc[7] = 8
0x015A  12 07 02 02 07   while loc[7] > 2 :        <- THE TRIGGER, and it is POSITIONAL
0x015F  22 00 07 14         loc[7],loc[20] = tile of $0197+0
0x0163  11 0A               loop
0x0165  16 46 01 86 03   if $7F0146 & 01  -> 0x016A  10 1F -> 0x018A (skip)
                          else            -> 0x016C  THE AMBUSH:
0x016C  E3 00 / EB 40 00 / 56 00 ED 01
0x0175  04 02 14         REQ obj1 fn4     (activate + REQ obj12/13/14 fn3, wait loc[6]==3)
0x0178  EB 08 FF
0x017B  D8 88 10         BATTLE
0x017E  EA 21
0x0180  65 80 46         SET $7F0146 bit0   <- the done-flag, written right after the battle
0x0183  E3 01 / 4A 10 01 00 00
0x018A  B2               HANDTOPLAYER
```

`04 02 14` at `0x0175` is the **only** caller of obj1 fn4 in the whole script — a raw byte scan of the
1926-byte body for `02|03|04 , 02 , 4x` finds it and nothing else, which is why the fn-table walk
(`grep "CALL/REQ obj1 fn"`) reported obj1 fn4 unreachable.

### op `0x16` semantics, derived (jump table `C0/5D6E`, entry 0x16 -> `C0/63E6`)

```
63E6 INY/TYX/ LDA $7F2001,X -> $D9(offset)  ; next -> $DB(value) ; next -> arg3
63FA BPL ->6418                              arg3 bit7 CLEAR -> address = $7F0000 + offset
63FF AND #7F / ...  640A ORA #0100           arg3 bit7 SET    -> address = $7F0100 + offset
6410/6427 LDA $7F0000,X / STA $D9            $D9 := the memory byte
642D LDA $DD / ASL A / TAX / 6433 JSR ($6467,X)   comparator = arg3 & 0x7F
6436 BCC ->643B   carry CLEAR -> 643B LDX $C3 / INX / SEC / RTS   = CONTINUE (do not jump)
6438 BRL ->6349   carry SET   -> take the jump
```

Comparator table `C0/6467`, 16 entries of 2 bytes (`87 64 8F 64 97 64 A1 64 A9 64 B5 64 BF 64 ...`);
each body is `LDA $D9 / CMP $DB / <branch to 64B3 = CLC> / SEC / RTS`, so **CLC (= execute the guarded
code) means the condition HOLDS**: 0 `==`, 1 `!=`, 2 `>`, 3 `<`, 4 `>=`, 5 `<=`, **6 = `LDA $D9 / AND
$DB / BNE ->64B3`** (bitwise-AND non-zero). The jump target is `operand-byte-address + operand`
(validated against `18 48 0C` at body `0x024C` -> `0x025A`).

So `16 46 01 86 dd` = *"if `$7F0146 & 0x01` then fall through, else jump `dd`"*. The setter is body
`0x0180` `65 80 46`: op `0x65`'s `0x80` means bit index 0 **and** the `+0x100` address bit, i.e. it
sets bit 0 of **`$7F0146`** — the same byte both tests read. Two directions, closed.

### op `0x12`: "loc[N]" is `$7F0200 + 2N`, not `$7F0200 + N`

`C0/62DE`: `LDA $7F2001,X / REP #20 / AND #00FF / **ASL A** / TAX / SEP #20 / LDA $7F0200,X`.
So ct.py's `loc[7]` is **`$7F020E`** and `loc[6]` (the imps' `71 06` counter) is **`$7F020C`**.
This matters: probing `$7F0206`/`$7F0207` reads the wrong bytes and makes the ambush look inert.

### op `0x22` (`C0/65EB` -> `C0/65B2`) — the positional read

`LDA $97,X` (field DP = `$0100`, so `$0197 + arg1`) picks the object slot, then
`65B3 LDA $1801,X -> loc[arg2]` (tile X) and `65B8 LDA $1881,X -> loc[arg3]` (tile Y).
`22 00 07 14` therefore writes **the lead PC's tile X into loc[7]** every pass of the `0x015A` loop.

**=> the scene-113 ambush is a POSITIONAL trigger: it fires when the party leader's tile X reaches
`<= 2`, i.e. when the player walks LEFT across the clearing.** Standing still after arrival spins
`0x015A -> 0x015F -> 0x0163` forever; the oracle was observed doing exactly that
(`--monitor 7E1180:2` cycling `7A03 / 8303 / 7F03` = body `0x0159 / 0x0162 / 0x015E`, the script-image
IP being `body + 0x221`). **Both earlier passes drove no input, so even with the flag clear they would
have seen nothing.**

## R1. The reproduction recipe (both hosts)

```
# 1. one scratch copy of the save with the ROM's own first-visit condition restored
python -c "import sys;sys.path.insert(0,'tools');from ctlib import lake
d=open('saves/jogo_trava_lake.state','rb').read(); p,_=lake.unwrap(d); p=bytearray(p)
p[lake.WRAM_OFF+0x10146] &= ~0x01                       # $7F0146 bit0 := 0
open(r'%TMP%\ctwf2\imp\jt_noambushflag.state','wb').write(lake.wrap(bytes(p)))"

# 2. the ORACLE, through the ROM's own loader
<probe> --teleport 113 8 7 0 --from $TMP/ctwf2/imp/jt_noambushflag.state --frames 240 \
        --out $TMP/ctwf2/imp/o113_ambush.state
    ->  "19 active, 19 placed" (vs 16 with the flag set)
        $1102 obj1  80 -> 04        $1118/$111A/$111C obj12/13/14  80 80 80 -> 05 05 05

# 3. drive LEFT to satisfy the 0x015A loop
<probe> --peek  $TMP/ctwf2/imp/o113_ambush.state out.raw --btnscript "0:200,40:400"
    ->  loc[7] $7F020E = 02, loc[6] $7F020C = 01, and the framebuffer shows THREE BLUE IMPS
        around Crono in the clearing (tools/ct.py fb2png, looked at).
<probe> --monitor "7E0F1B:1,7E169A:2,7E161B:1,7E179A:1" --from $TMP/ctwf2/imp/o113_ambush.state \
        --btnscript "0:200,40:900" --frames 1100 --diff

# 4. OURS -- --scene 113 already starts the imps ACTIVE (mode=05, same as the oracle); it only
#    needs the same LEFT walk to satisfy the same loop
SDL_VIDEODRIVER=dummy CT_DRAWDBG=1 <ct_field> --assets ct_assets.dat --scene 113 \
        --walkscript "l:900" --selftest x.raw --frames 1000
```

`$1100,X = 0x05` for obj12/13/14 on **both** sides (`CT_POSALL` prints `mode=05`), and their
`$1580,X` is `D8 B8` = `$E4:B8D8`, sprite 268's strip. Positions agree at load, too:
oracle `idx1A px=(184,207)`, `idx1C px=(40,159)` vs ours `obj13 at(184,207)`, `obj14 at(40,159)`.

## R2. The measurement — drawn assembly frame, interval by interval

Oracle = `$0F01,X` per frame (`--monitor --diff`). Ours = `CT_DRAWDBG`'s `af=` (which `field.c:8598`
also stores into `$0F01,X`, so it is the same quantity). The two hosts reach the trigger at different
absolute frames (ours walks from f0, the oracle idles 200 frames first), so the constant offset is
listed and the **dwells** are what is compared.

**obj13** (`AA 03` -> `AE` -> `96 0B 09 MOVETO(11,9)` -> `A9 00`), oracle offset **+122**:

| af | oracle f | ours f | oracle dwell | ours dwell |
|---|---|---|---|---|
| 0D/13 | 553 | 675 | 10 | 10 |
| 0C/12 | 563 | 685 | 14 | 14 |
| 01 | 577 | 699 | 10 | 10 |
| 02 | 587 | 709 | 15 | 15 |
| 01 | 602 | 724 | 10 | 10 |
| 00 | 612 | 734 | 15 | 15 |
| 01 | 627 | 749 | 10 | 10 |
| 02 | 637 | 759 | 4 | 4 |
| 07 | 641 | 763 | (static) | (static) |

**obj12** (`96 0A 05 MOVETO(10,5)`), oracle offset **+126** — dwells
`10, 15, 10, 15, 10, 15, 9, 10, 14` on **both** sides
(oracle f461/471/486/496/511/521/536/545/555/569 af `0D 0C 0D 0C 0D 0C 0D 04 05 04`;
ours f587/597/612/622/637/647/662/671/681/695 af `13 12 13 12 13 12 13 4 5 4`).

**obj14**, oracle offset **+122** — dwells `10, 15, 10, 15, 10, 8, 10, 15` on **both** sides
(oracle f509/519/534/544/559/569/577/587/602 af `0D 0C 0D 0C 0D 0C 0A 0B 0A`;
ours f631/641/656/666/681/691/699/709/724 af `13 12 13 12 13 12 10 11 10`).

**26/26 intervals identical, and every drawn frame id identical.** The `AA 03` phase runs the
`0F 0A` of anim 3 and the `AE`+`MOVETO` phase runs the `0F 0A 0F 0A` of anim 1, on both sides.

## R3. What this answers in section 7 / section G

* **"Is `C0/5614`'s RUN arm reachable for the scene-113 imps?" — NO, and it is measured.**
  `5671 LDA $1100,X / CMP #04 / BCS ->564B` takes the WALK branch for anything with `$1100 >= 4`, and
  the scene-113 imps carry **`$1100,X = 0x05`**. The feared "ours 5x too SLOW" (anim 6 `05 05` vs
  anim 1 `0F 0A 0F 0A`) **cannot happen here**: the ROM picks anim 1 too. Same for the loc-112 imps —
  `--read 7E1100+52 --from saves/jogo_trava_lake.state` gives `$1100 = 05` for obj15 (`X=1E`),
  obj16 (`X=20`), obj23 (`X=2E`) and obj24 (`X=30`), all with `$1580 = D8 B8`.
  `89 20` in obj12 fn0 sets the **move speed**, not `$1100`; it never reaches `5647 CMP #20`'s arm
  because the `$1100` gate is tested first.
* **"the symptom was not reproduced" (section 7 bullet 1) is now closed as
  NOT-REPRODUCED-ON-A-PROPER-STATE**, not as "unreachable". The state exists, the imps animate, and
  the cadence matches.
* Sections 4/5's engine critique is untouched by this: it is about `$1680`/`$1780` never reaching the
  `$7E` image (a **pose**/`--vmdiff` defect, R4 of the review), and clock B happens to reproduce the
  ROM's *durations* exactly in this scenario because `animTickActor`'s mode-0 arm (`walking ? 1 : 0`)
  and `C0/5614`'s arm agree whenever `$1100 >= 4` — which is every scripted imp measured so far.

## R4. Open questions this pass leaves

* **Where the user's "too fast" comes from is still unlocated.** It is not scene 113's imps at
  the drawn clock. Candidates not measured here: the LEAD PC in the same room (`$1100 = 0` — the one
  object for which `C0/5614`'s RUN arm *is* reachable, and for which `field.c:8592` hard-wires
  `running = 0`); an actor whose `$1780` mode is lost across a state reload (review section C, S1 —
  the `field-cat-anime-once-duration` shape); or a different room entirely.
* **The two hosts' scene-113 ENTRY differs and was not graded.** Ours starts the player at tile (7,7)
  free to walk; the oracle (teleport + flag clear) plays the `0x013B` arrival — `FF 90 87 67 20` iris
  (`C0/4036`, `$39 := 1`), obj9/obj10 gate glow, obj2 fn7 `MOVETO(8,11)` — and locks `$011F = 00`
  until obj1 fn3's `E3 01`. Whether ours runs that arm at all is unmeasured; it is a *sequencing*
  question, not a rate one. (In ours the run ended with `changeloc: scene 113 -> 112 at f885` — Crono
  walked out the left exit; in the oracle the `D8 88 10` at body `0x017B` fired at f727-730. Both
  hosts did run obj1 fn4 first: loc[6] `$7F020C` climbed 0->1->2->3 in the oracle, and ours played
  all three imps' `AA 03` + MOVETO.)
* `$7F00DB == 5` (body `0x0086`) selects a *different* scene-113 arm that REQs obj12/13/14 **fn4**
  plus obj15/obj16 (`83 A6 09` = monster 422) and ends in `DF AC 01 07 0A`. Never exercised here.
* Whether the `0x0165` test can ever be reached with `$7F0146` bit0 SET (its `10 1F` skip arm) is dead
  code in practice, since `0x00D0` already diverts that case — unverified.
* `op 0xFF` sub-ops (`C0/3FAF`, dispatch `4009 JSR ($400E,X)` for `nn >= 0x90`): `FF 90` = `C0/4036`
  (`$3A/$3B/$3D` := x,y,radius, `$39 := 1`), `FF 91` = `C0/4055` (`$39 := 2`), `FF 92` = `C0/405D`
  (the WAIT — `LDA $39 / CMP #03 / BNE -> TYX / RTS`, i.e. park without advancing the IP). Only the
  entry points were disassembled; the iris state machine itself was not re-derived here.

## Ported into the LIVE VM (2026-09-03; defect #7, 'Marle moved without animating')

Everything above was already byte-exact in the harness runner (move.c). The live VM (ct_vm.c) kept the
same state in C fields and the host rendered from its own ticker, so three things diverged from the
oracle on scene 8's pod cutscene (marle_getting_in_the_left_pod, obj14 = Marle), all measured with
`--monitor 7E169C:2,7E0F1C:2,7E161C:2` against `CT_DUMPWRAM_AT` dumps of ours:

1. **The host rendered NPCs from its own `animTickActor` and wrote its result over `$0F01,X`.** The drain
   (C0/CA76 -> CAD9, ported in the tick's frame legs) writes each object's OAM assembly frame there, and
   oam.c's `CtDecodeSpriteAssembly` -- the OAM the VMDIFF_OAM lockstep grades -- reads it. The host now
   reads `$0F01,X` for every NPC (field.c 'RENDER FROM $0F01,X'); the ticker stays as the fallback
   without a ROM image and for the lead PC's own walk/idle machine.
2. **C0/568B was missing.** At the seven move-op exits (4E85 4F9C 502D 50DB 5273 53BB 54A8) the ROM forces
   the auto anim IDLE (`$1780 == 0 && $1680 != 0 -> $1680 := 0, $1681 := 0, $1601 := 0`). The oracle's
   Marle goes `$1680/$1681` 01 01 -> 00 00 and `$0F01` 05 -> 01 on the frame after her ticket expires
   (f28); ours stayed in walk. Ported as `mvForceIdle` (ct_vm.c). Op 0xA0's arrival (C0/5184) and the
   port-only missing-target guard are NOT 568B sites and do not call it.
3. **The registers live in WRAM now.** `$1600,X` is written by every facing writer (the 0F/17/1B/1D/A6 ops,
   the move family's `LDA $F700,X / STA $1600,X` at issue, the face-object ops, the D9 walk, the followers,
   the host setter): Marle's `17` (face down) on the pod left `$1600 = 0` before, so the drain looked up
   the idle-UP frame (0) where the oracle shows 1. At the step issue `$1900/$1980` carry the velocity ACFD
   publishes (zero on a walled stall, C0/AF27) and C0/5614 (`CtVmMoveStepAnim`, the harness's own
   function) runs ONCE on that final velocity; op 0xA0's issue path has no 5614 (party3's pacers keep
   `$1680 = 0`, measured). Committing per attempt had left the $FE00 bounce's velocity in the image for a
   walled mover -- party3's obj27 `96 37 1D`.

Graded: the pod cutscene's write mask is clean apart from the harness's own $00F0/$00F2 injection;
`vmdiff-wmask-party3` claims 318 addresses (was 269) with no masked divergence; `vmdiff-framelegs-party3`
20 divergences (was 21). `$01D9` is excluded from the mask: a direct-page scratch with many writers per
frame (5614's |velX|, the PC loader's 44A9, ...), so its boundary value is an intra-frame order the port
does not reproduce. Nine frame-hash checks were re-pinned (positions, counts and states verified identical
token by token).

Verified on the same fixture at f40: Marle's world position ($1800/$1880 = D603/CE05), camera (0,0),
assembly frame (1) and her four unit offsets (-48/-32/-16/0 from the record at DC:9AD8 + 40) draw her
on the same rows as the oracle's OAM; an apparent 8-px offset in a first pixel comparison was the
oracle framebuffer's 8-row top border (240 lines against our 224 -- the pod's water shifts by the
same 8 rows). Still open there: the Gate vortex is visible in ours from f12 while the oracle shows it
later (op 0x88 colour gating / palette).

