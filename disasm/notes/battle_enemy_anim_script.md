# The battle ANIM SCRIPT is a live machine — enemy sound, enemy lunge, enemy poses

The user report this closes (translated from Portuguese):

> the hetake enemies, battle in the forest, are not making the attack sounds, damage sound, and animations
> properly. They are not walking either, just like GATO. GATO's attacks are not making any noise either

**All of those are one defect.** `src/battle.c` had `battleAnimParse`, a one-shot STATIC walk of the
dispatched anim script that collected structure (has-a-strike, has-a-ShowDamage, the pause sum) and threw the
script away. **Nothing ever executed it.** Every event the script owns — the approach/return lunge, the sound
opcodes, the inter-track sync barriers — therefore never happened. `battle_sfx_ops_inputs.md` §6 named this
exactly ("there is NO live per-frame opcode execution point in `src/battle.c`") and stopped there.

This file derives the machine and records the port. Everything below carries the command that produced it.

---

## 0. TL;DR

| | |
|---|---|
| the machine | `C1/4CA2` — 16 tracks, **one opcode per track per frame** unless the handler sets `$A00F` |
| the frame cost | **READ FROM THE ROM**: an opcode is free iff its handler contains `INC $A00F` (`EE 0F A0`) |
| the track order | `C1/4562` walks the header mask **MSB-first**: bit 15 → track 0 … bit 0 → track 15 |
| the track's role | the track index **is** the `C1/762E` resolver mode: `$A014,X` (0 = actor, 3 = primary victim) |
| enemy scripts | resolve through **`CD/5FF0[$9877*2]`, bank CD** (`C1/4535`) — the PC physical uses `CD/5EC6`→CE |
| the Hetake's sound | `78 67` in its track 3 — cmd `$18`, id = the **script operand**, pan `#80` (`C1/619E`) |
| GATO's sound | `78 46`, same opcode, behind the script's own `20 14` pause |
| the Hetake's lunge | `98 00 31` onto a point a **scratch track** computes, then `98 00 19` + `1B 19` home |
| the pose engine | `C1/423A` + `C1/424C` -- one publish every OTHER frame, globally, round-robin over 11 |
| the terminator | a **zero DWELL** byte in ptrB, not a 0xFF pose in ptrA |
| the render foot | the E3 positions are offsets from the **live** foot `($1D0C,X / $1D23,X)` — SOLVED by subtracting one E3 record from the oracle's OAM (§7.6) |
| result | the sound reproduces at the oracle's **rel 51 exactly**, **every one of the 8 animation landmarks** lands on the oracle's own frame, and **GATO and the Hetake both draw the engine's publish** -- no tuned constant anywhere |

---

## 1. The ground truth — the forest fight with the PCs idle

The whole prior SFX dossier drove A-mash, so the PCs acted and the enemies never did. Let them act:

```
build/ct.exe --from saves/battle_re/forest_holdleft_pre.state --btnscript "40:240,0:1400" \
             --frames 1650 --monitor "7E1E01:2" --diff --writer
```

```
  f21     7E1E01=6F80*(C0:3C21)      the encounter
  f244    7E1E01=0880*(CD:3B7D)
  f327    7E1E01=4280*(C1:1B50)
  f502    7E1E01=6780*(C1:61B5)   <- ENEMY ATTACK
  f623    7E1E01=FF80*(C1:40D8)
  f717    7E1E01=6780*(C1:61B5)   <- ENEMY ATTACK
  f847    7E1E01=FF80*(C1:40D8)
  … 67 / FF, alternating, once per Hetake turn
```

`C1:61B5` is the `JSL $C7:0004` inside **`C1/619E`** — the op-`0x78`/`0x79` handler, whose id comes from the
script operand and whose pan is the literal `#80`. So the missing enemy attack sound is an **anim-script
opcode**, not battle logic. (`C1:40D8` = `C1/40D3`'s `LDA #FF`, not an anim op; still unexplained
semantically, same as in `battle_sfx_ops_inputs.md` §7.4.)

### 1.1 Which script — and it is NOT the PC's table

```
build/ct.exe --from … --btnscript "40:240,0:700" --frames 950 \
             --monitor "7E9877:1,7E9878:2,7EAE91:1,7E9884:3" --diff --writer
  f448    7EAE91=03*(C1:ACFC)                              ; the acting combatant = 3 (a Hetake)
  f451    7E9877=14*(C1:44E7)  7E9878=B972*(C1:453C)       ; selector 0x14 -> script ptr 0x72B9
```

`C1:453C` is the PC after `C1/4535 LDA $CD5FF0,X` — the table `ROM_ATK_ANIM_TBL` already cites, and the
header is then read `LDA $CD0000,X`, i.e. **bank CD**. `CD/5FF0[0x14] = 0x72B9` ✓. This closes
`battle_sfx_ops_inputs.md` §7.5: **an ENEMY action resolves through `CD/5FF0` (bank CD); the PC physical
resolves through `CD/5EC6` (bank CE).** Both tables are real; they serve different actors.

### 1.2 The script

`CD/72B9`, walked with the ROM's own opcode lengths (mask `0x9001` → three tracks):

```
  t0  72C9: 39 | 24 00 | 72 0D | 02 05 | 0B | D9 30 | D0 | 98 00 31 | 72 0D | 36 | 03 07 | 20 0F |
             02 00 | 0C | 98 00 19 | 1B 19 | D1 | DA | 24 02 | 72 0D | 06 03 | 20 0F | 50 | 2E | 01
  t3  72F4: 24 01 | 78 67  <<< THE SOUND | 1E 44 | 36 | 06 03 | 00
  t15 96B2: 24 FF | 02 00 | 73 03 | 1B 03 | 76 03 00 | 0C | A8 04 | 33 23 | 41 1E 0E | 41 1F 0F | 36 | 00
```

and GATO's attack-A, `CD/5FF0[0x31] = CD/7A42` (mask `0x9000` → two tracks):

```
  t0  95D3: 72 0D | 36 | 03 08 | 24 02 | 06 03 | 20 0F | 50 | 2E | 01
  t3  7A4E: 24 01 | 20 14 | 78 46  <<< THE SOUND | 72 0A | 06 09 | 20 02 | 69 00 | 20 02 | 6A |
             76 00 03 | 0C | A9 04 | 20 0A | 36 | 72 0A | 06 03 | 00
```

GATO's flurry `CD/5FF0[0x34]` carries **seven** `78 46` in one track — the multi-hit.

---

## 2. The machine — `python tools/dis.py 14CA2`

```
  14CB5  LDA $5DFD,X / BEQ +          ; the per-track PAUSE counter
  14CBA    DEC $5DFD,X / BRA done     ;   still pausing -> this track does nothing this frame
  14CBF  STZ $A00F                    ; the CONTINUE flag
  14CC4  LDA [$E6],Y -> $9895         ; fetch the opcode
  14CD4  JSR ($7A6B,X)                ; run the handler (it advances $E6 by its own length via C1/75BB)
  14CD7  LDA $A00F / BNE 14CBF        ; the handler set it -> run ANOTHER opcode THIS SAME FRAME
```

⇒ **an opcode costs ONE FRAME iff its handler does not `INC $A00F`.** That is a fact in the ROM image, so the
port READS it (`battleAscOpFree` scans the handler body for `EE 0F A0` up to its own terminator) exactly the
way `battleAnimOpLenRom` reads the length. **No opcode's frame cost is typed into the C.**

Scan result for the opcodes these scripts use — free (zero-frame): `1B 24 36 39 41 49 60 76 78 79 7A 7B 7C 7D`.
Costing: `00 01 02 03 06 08-0F 12 1E 20 26 2E 33 50 69 6A 72 73 98 99 A8 A9 D0 D1 D9 DA`.
(`24` is conditional and is decided in the switch: satisfied → free + advance, else re-run next frame.)

### 2.1 The tracks — `python tools/dis.py 1455E 0x50 0 0`

```
  Y = 0 ; REP #20
  loop: ASL $80              ; 16-bit, MSB out to carry
        BCC skip
          $5DAC[Y] = 1                  ; track Y active
          $5DBD[Y*4] = *ptr++ ; +2 = #CD
        skip: INY ; CPY #0010 ; BNE loop
```

**MSB-first**: header bit 15 → track **0**, bit 0 → track **15**. The Hetake's `0x9001` therefore loads
`72C9` as track 0, `72F4` as track 3, `96B2` as track 15.

### 2.2 The roles — `python tools/dis.py CF150 0xB0`

```
  $A014 = $AE91                        ; the ACTOR
  $A015 = $AE97 / $A016 = $AE98        ; co-actors
  $A017 = the first set bit of $AE95   ; the PRIMARY target      <- this is $A014[3]
  $A018.. = the set bits of $AE99      ; the rest of the targets
  for X in 0..2: $99AF[X] = $1D0C[$A014[X]] ; $99B2[X] = $1D23[$A014[X]]
```

Measured, at the Hetake's dispatch:

```
--monitor "7E5D80:16,7E99AF:6,7EA014:4" --diff --writer
  f451   7E99AF=7800003F0000*(CC:F1E0)   7EA014=03FFFF00*(CC:F1A7)
```

`$A014 = 03 FF FF 00` → role 0 = combatant 3 (the acting Hetake), role 3 = combatant 0 (Crono, the victim).
`$99AF[0]/$99B2[0] = 78/3F` = that Hetake's position at dispatch — i.e. **resolver modes 0x19-0x1B are the
actor's HOME**, which is what the script's `98 00 19` / `1B 19` return to.

A track index **>= 8** does not address a combatant at all: every handler branches `LDA $A010 / CMP #08 /
BCS` into the effect-object space (`$A2B6`, `$AB5F`). Those are the scratch objects a script uses to COMPUTE
a point.

### 2.3 The resolver `C1/762E`, jump table `C1/79D3`

| mode | handler | meaning |
|---|---|---|
| `00-08` | C1/7639 | the live position of combatant `$A014[mode]` |
| `09-11` | C1/764A | same + the per-combatant offset `$9708`/`$9713` |
| `19-1B` | C1/77DA | `$99AF[mode-0x19]` / `$99B2[…]` — the actor's dispatch HOME |
| `1C-23` | C1/77EC | `$A260[k]+$A240[k]` — EFFECT-object k's position |
| `2A-31` | C1/7901 | `$5D80[(mode-0x2A)*2]` — a script-computed STORED point |
| `12` | C1/767D | `$A2B0`/`$A2B2` as they stand (RTS) |
| `13-15` | C1/767E-7686-768E | the midpoint of the actor `$A014` and role 3 / 1 / 2 (`$84`) |
| `16` | C1/76D8 | the three roles' X sum / 3, into both bytes |
| `17` | C1/774E | 0x18 px ahead along the track's own heading |
| `18` | C1/77D1 | (0x80, 0x80) |
| `24-29`, `32-3F` | C1/780B | `(wA*A + wB*role3) >> 3`, weights CC/FB7F[2k]/[2k+1]; 32-38 A = role 1; 39-3F with the cursor offsets |
| `40` | C1/7914 | `LDY #2 / STY $84 / LDA $A015 / TAX / JMP C1/7697`: the midpoint of role 1 and role 2 |
| `41-47` | C1/7920 | `(wA*actor + wB*effect object 0) >> 3` per axis, weights CC/FB7F[2k]/[2k+1] (k = mode-41), object 0 = the low bytes of `$A240`/`$A250` |

Measured 2026-10-01 (the rule's oracle, tools/battle_ab.py `combos 69`): Final Kick's track 15 opens `0E 1B 40` -- the
effect object teleports to mode 40, (9F,87) = the midpoint of Crono and Marle -- and its `A8 04` / `A8 0C` forward moves
and the `44` stores that follow make the homing goal `$5D82` = (9F,57) every effect object of the kick steers to (op `15 FF
2B`). The port had no mode 40-47: the object sat at (0,0), the forward moves stopped at the edge at once, and every homing
flight ran to (0,0) -- the tech 16 frames long (Gatling Kick 15).

---

## 3. Reading the Hetake's attack frame by frame

```
build/ct.exe --from saves/battle_re/forest_holdleft_pre.state --btnscript "40:240,0:500" --frames 700 \
             --monitor "7EA013:1,7E1D0F:1,7E1D26:1,7E1E01:1,7E5D9D:1" --diff
```

```
  f451  A013=1                                dispatch (span opens; span = 628-451 = 177)
  f472  $5D9D = FF                            t0's `39` -- the script's FIRST opcode  => PRE-ROLL = 21
  f481  $5D9D = 00                            t15's closing `36`
  f487..f500  X 78->94 (+2/f), Y 3F->43       t0's `98 00 31` -- THE APPROACH, 14 steps
  f502  id 67 posted, $5D9D = 01              t0's `36` releases t3's `24 01` -> `78 67`  => rel 51
  f528  $5D9D = 02                            t3's `36` after the `1E 44` super
  f534..f540  X 94->78 (-4/f)                 t0's `98 00 19` -- THE RETURN, 7 steps
  f623  id FF   f625 $5D9D=00   f628 A013=0   span end
```

Every one of those falls out of the machine with **nothing tuned**:

| frame (script-relative) | what runs |
|---|---|
| 0 | t0 `39`(free)→`24 00` waits. t15 `24 FF`(satisfied, free)→`02 00` costs. |
| 1 | t15 `73 03` |
| 2 | t15 `1B 03`(free) `76 03 00`(free) `0C` costs |
| 3 | t15 `A8 04` — arms the move-forward, parks 4 frames |
| 8 | t15 `33 23` |
| 9 | t15 `41 1E 0E`(free) `41 1F 0F`(free) `36`(free) → sync 0, `00` ends the track ✓ **f481** |
| 10 | t0 `24 00`(free) → `72 0D` costs |
| 11-14 | t0 `02 05`, `0B`, `D9 30`, `D0` |
| 15 | t0 `98 00 31` — arms AND takes its first step ✓ **f487** |
| 15..28 | the 14 slide steps ✓ **f500** |
| 29 | t0 `72 0D` |
| 30 | t0 `36`(free) → sync 1 → t3 `24 01`(free) → **`78 67`** ✓ **f502 = rel 51** |

### 3.1 The approach point is COMPUTED, by a scratch track

The oracle stores it:

```
  f481   7E5D80=00000000000000000000000000009444*(C1:59F2)
```

`C1:59F2` is op `0x41`'s `INC $A00F` — `41 xx yy` copies `$5D80[xx]` → `$5D80[yy]`. Slot `0x0E/0x0F` is
exactly the pair resolver mode `0x31` reads (`(0x31-0x2A)*2 = 0x0E`), and it holds **(0x94, 0x44)**. Where it
came from, in t15: `1B 03` teleports the scratch object **onto the victim**; `76 03 00` (C1/60E6 → C1/0222)
stores the bearing `atan2(victim, actor)` in `$A5AA`; `0C` sets speed 4; `A8 04` walks it forward along that
bearing; `33 23` stores where it ended up into `$5D9E/$5D9F`; the two `41`s copy that into the mode-0x31 slot.

With Crono at (0xA0,0x47) and the Hetake at (0x78,0x3F) the bearing is **0x88**, and

```
trigMul(0x88+0x40, M), trigMul(0x88, M)  ==  (-12, -3)  at  M = 12   ->  (0x94, 0x44)   EXACT
```

`A8 04` at speed 4 is 4 passes reaching magnitude 12, i.e. `k*speed` for k = 0..3 — and 4 passes is exactly
the 4-frame park the timeline above needs. **⚠ NOT SEPARATED:** the single measured pair (dd, speed) = (4, 4)
cannot distinguish `k*speed` from `(k+1)*(speed-1)`; both reach 12. A second script with a different
(dd, speed) would settle it.

### 3.2 The slide law is the one already ported

`0B` = speed 2 (CC/F4BC[3]) and the approach is 28px in **14** frames at 2px/f; `0C` = speed 4 and the return
is 28px in **7** frames. That is `battleSlideArmSteps` (count = dominantDist/speed, per-axis step =
speed*delta/dominant) unchanged, the same law the PC tech route derived.

---

## 4. GATO — the same opcode, a different offset, and why that is the proof

`saves/gato_inbattle.state`, `--btnscript "0:1600" --monitor "7EA013:1,7E5D9D:1,7E1E01:2,7E9877:1"
--diff --writer`, second attack:

```
  f1141  A013=1  $9877=31*(C1:44BB)     dispatch
  f1166  $5D9D=01*(C1:594E)             t0's `36`
  f1189  7E1E01=4680*(C1:61B5)          the sound            => rel 48
  f1213  $5D9D=02   f1294 id FF   f1299 A013=0               => span 158 (pin CT_SPAN_GATO_A 157 ✓)
```

GATO's t0 has **no approach** (`72 0D | 36 | …`) — his walk is the AI mover, which runs *outside* the span —
so the sound's offset is the script's `20 14` pause alone and is position-independent. The port lands it at
**rel 43**, 5 frames early; see §7.

---

## 5. What was ported — `src/battle.c`, `battleAsc*`

`battleAscArm` (header read + MSB-first mask walk + the role/home filler) and `battleAscTick` (one frame of
`C1/4CA2`), armed from `battleAdvanceAction` at `rel == CT_PC_ACT_PREROLL` for a **non-PC** attacker and
ticked every render frame after. State lives in `CtBattleHost` (`ascIp/ascPause/ascSpeed/ascSync/ascStore/…`).

Opcodes: terminators `00 01`; `20` pause; `24` wait; `36`/`39` sync; `08-0F` speed; `33` store; `41` copy;
`49` store-add; `1B` teleport; `12`/`98`/`9A` slide; `76` heading; `A8`/`A9` move-forward; `78-7B` **SOUND**;
`02/03/04/05/06/26` playAnim. Everything else falls through as an inert pass — and still costs its frame,
because the cost comes from the ROM scan, not from whether we implemented the effect.

### 5.1 Scope lines, stated rather than hidden

- **PC actions still use the old path.** `battlePcAtkTick` / `battleTechRouteTick` are separately derived and
  pinned; folding them onto this interpreter is the next step, not a silent rewrite.
- **The window LENGTH is still the oracle-pinned span.** The interpreter drives the EVENTS inside it. This is
  the split `battleAnimParse`'s own header already describes.
- **One writer per foot.** A track bound to a VICTIM also carries motion (GATO's `76 00 03 | 0C | A9 04` is
  the recoil that shoves Crono 3px), but that displacement is already ported — `battleAdvanceAction`'s LIVE
  KNOCK block applies it at the strike from the same script's operand (`battle_ai_interp.md` §10d-e). Note
  `A9`'s handler `C1/6AF0` is a **different** mover from `A8`'s `C1/69C3` (it carries its own `$AE=10`
  scale). Publishing it from the interpreter as well would apply it twice, so the interpreter repositions the
  ACTOR and its scratch objects only; a non-actor track still runs (its frames, parks and sound ops are the
  script's timing) but does not move that foot.
- **`03 aa` (one-shot playAnim) parks for a duration the ROM does not state in the script.** It is charged the
  enemy pose window's own ptrB dwell sum (`CtBattleEnemyAnimWin`) — a ROM datum, not a typed-in number. For
  the Hetake's `03 07` that is 10 frames against the oracle's 13, so the RETURN walk starts 3 frames early.
  This is the project's standing sprite-anim-playback boundary (`battle_span_emergence.md`) and the runtime
  flags it (`ascStall`, `CT_BATTLE_ASCTRACE`).

---

## 6. Grading

- `field-battle-hetake-attack-sfx` — the Hetake's `78 67` at **rel 51**, the oracle's own number, produced by
  the interpreter rather than pinned. Mutations killed: discarding the ROM-read `$A00F` flag; reversing the
  MSB-first mask walk; removing the slide arm's first step.
- `field-battle-gato-attack-sfx` — `78 46` × 3 at rel 43, with `cmd=18` and `pan=80` in the line. Mutations
  killed: the `$9895 - #78 + #18` command arithmetic; the script's `20 14` pause.
- `field-battle-hetake-return-pose` — re-derived TWICE and now EVENT-anchored (`enemy-arrive+110`), because
  a fixed frame fell one frame outside the live `02 00` window once the pose engine landed; its own mutation
  still kills.
- `field-battle-hetake-pose-ladder` — the whole animation ladder as the engine produces it, arm frames,
  cursor, published index and ROM dwell, with the victim's competing arms in the pin. Mutations killed: the
  `anim*4` window law; the `$A012` publish gate; arming PCs. One derived fact it CANNOT grade is stated in
  its own notes (the zero-dwell terminator, §7).
- Section 6's TL;DR line "the sound reproduces at rel 51" still holds after all of this.
- Suite green.

---


---

## 7. THE POSE ENGINE — `C1/423A` + `C1/424C`, and why `03 aa` parks for exactly as long as it does

The playAnim opcodes do not animate anything. They ARM per-COMBATANT state and a separate machine walks it.
`python tools/dis.py 1423A 0xE0 1 0`:

```
  C1/423A  for cmb in 0..10: if $A12F[cmb] != 0: DEC $A12F[cmb]     ; every entry's frame countdown
  C1/424C  X = $A21F                                                 ; the ROUND-ROBIN cursor
           scan 11 entries for the first with $A124[X] != 0 && $A12F[X] == 0 ; none -> RTS
           if $A028 != 0 or $A012 != 0 -> RTS                        ; the publish gate
           $A21F = X+1                                               ; ONE combatant advances per pass
           $A13A[X] += 1                                             ; the cursor
           if $A150[X] != 0 { $A13A[X] = $A150[X]-1 ; $A124[X] = 0 } ; the HOLD ops land here
     loop: d = E4:[ptrB + cursor]
           if d == 0 {                                               ; a ZERO DWELL is the terminator,
              if $A124[X] == 2 { cursor = 0; goto loop }             ;   NOT a 0xFF pose
              $A124[X] = 0 ; RTS }                                   ;   a one-shot ENDS here
           $A12F[X] = d
           $967B = E4:[ptrA + facing*stride + cursor] ; JSR C1/1C4A  ; PUBLISH
           $A012 = 2                                                 ; block further publishes
```

⚠ **The terminator is the ZERO DWELL, not the 0xFF pose.** They coincide in the Hetake's windows, which is
why a mutation swapping them stays green there — but across all 104 distinct enemy `animId`s x 32 anims x 4
entries the two DISAGREE in **6102 of 13056** entries (pose `0x00` with dwell 0 is the common case), where
terminating on the pose would republish pose 0 for ever.

Two things this pins down that the port had only assumed. **The tick runs BEFORE the script machine, once
per rendered frame**: `C1/423A` has exactly one call site in the 4 MB image, `C1/40B3`, inside the action
loop `140B0 JSR C1/007E (the NMI barrier) … 140B3 JSR C1/423A … 140CB JSR C1/4BBE (the script) … 140D1 BEQ
140B0`. So an arm written by the script at frame N is first serviced at N+1, and a mode-clear written by
the tick at N is seen by `C1/4E22` in that same frame. And **the IDLE pose is a different stepper**:
`C1/2E89`, cursor `$968C,X` — which is why `$A124/$A12F/$A13A` read 0 for a combatant that is visibly
animating between actions, and why the port is right to keep the two apart.

`$A012` **is** a countdown (`LDA #02 / STA $A012` at C1/430C, `DEC` once per frame at CF/E775): measured, a
publish at T, blocked at T+1, free at T+2. So the law is **at most one pose publish every other frame,
globally, round-robin over all eleven combatants** — and that is not bookkeeping trivia, see §7.2.

### 7.1 The four playAnim opcodes, and which of them park

All four share the C1/4D08 body, whose tail (C1/4D5C-4D6C) is `$A124 = $8E`, `$A12F = 0`,
`$A13A = $A214 = 0xFF`, and then `CMP #01 / BEQ -> RTS`:

| op | handler | mode | latch | parks? |
|---|---|---|---|---|
| `02 aa` | C1/4D04 | 2 = LOOP | — | no (falls to C1/4E19 and advances) |
| `03 aa` | C1/4E22 | 1 = ONE-SHOT | `$A145` | YES — re-visits until `$A124` reads 0 (C1/4E41) |
| `04/05/06 aa` | C1/4EA2 | 1 + `$A150 = 1` | `$A150` | YES, but the tick's HOLD path clears the mode on the very first pass, so it is one pose and release |
| `26 aa nn` | C1/577D | 1 + `$A150 = nn` | `$A150` | same, holding entry `nn-1` |

That is what makes the Hetake's `03 07` park EXACTLY 14 frames — arm, publish entry 0, five frames, publish
entry 1, five frames, terminator, re-visit — and the previous commit's dwell-sum stand-in gave 10. The
oracle, `--monitor "7EA127:1,7EA132:1,7EA13D:1" --diff --writer` (mode/dwell/cursor for combatant 3):

```
  f502  A124=01*(C1:4D64) A12F=00 A13A=FF     ; `03 07` arms
  f503  A12F=05*(C1:42F1) A13A=00*(C1:42B8)   ; entry 0 -> pose 04
  f504..f508  05 04 03 02 01 00
  f510  A12F=05           A13A=01             ; entry 1 -> pose 05   <- TWO frames late, see 8.2
  f511..f515  04 03 02 01 00
  f515  A124=00*(C1:42E2) A13A=02             ; the terminator; the track releases the next frame
  f532  A124=02*(C1:4D64)                     ; `02 00` -- 516 + 16, i.e. `20 0F`'s own frame + 15
  f545  A124=01 / f546 A124=00*(C1:42C4) A12F=0F A13A=00*(C1:42C1)   ; `06 03` -- the $A150 HOLD path
```

### 7.2 The victim STEALS a publish slot — and skipping it costs 2 frames

At f507 combatant **0** (Crono, the victim) arms a window of its own, and the f508 pass — the very frame the
attacker's dwell expires — serves **cmb0 instead of cmb3** (`$A21F` goes `04 -> 01`, i.e. it serviced
combatant 0). `$A012 = 2` then blocks f509, and cmb3 is finally served at f510.

Where does the victim's window come from? Not from the victim's own track: t3's `06 03` runs much later
(f528). It is the STRIKE SUPER. `1E 44` -> `CE/0000[0x44*2]` = **CE/DB10**:

```
  69 00 | 20 04 | 06 05 | 20 08 | 6A | 20 08 | 1F
```

`69 00` is the palette set, and `06 05` is the victim's hurt window. So the super had to become a real
1-deep call for the attacker's own animation to land on the right frames — `1E ss` (C1/56CC) pushes
`$E6 + 2` into `$A569`, points `$E6` at bank CE and `INC $A00F` (it is FREE), `1F` (C1/56FB) pops.

### 7.2b ⚠ The free-opcode walk was wrong in BOTH directions, and the two errors were cancelling

The walk stopped at the first `RTS`/`RTL`/`JMP C1/75BB` byte. Three ways that lies, all now handled:

| | |
|---|---|
| **an OPERAND byte reading as a terminator** | `C1/56CC` (op `1E`) and `C1/56FB` (op `1F`) contain `99 6B A5` / `B9 6B A5` — that `6B` reads as `RTL` and stops the walk ~19 bytes before the real `INC $A00F`. These two stay in a cited correction table in the C; there is no way to see past them without decoding instructions. |
| **a TAIL CALL** | op `02` ends `JMP C1/4E19`, and the `INC $A00F` is *there*, not in C1/4D04. op `1B` ends `JMP C1/561F`, and the `INC` the walk was finding belonged to the NEXT handler entirely. **The walk now FOLLOWS a non-canonical `JMP` (up to 4 hops)**, which reads op `02` as FREE and op `1B` as COSTING — the opposite of before, in both cases. |
| **the window being too short** | op `69`'s `INC` is 172 bytes in; the window is 0x100 now and finds it unaided. Its sibling `6A` (C1/5E87) was checked the same way and is NOT free (`LDA $8E / JMP C1/75BB` at C1/5EDA, no INC). |

The two tail-call errors had been **cancelling**: op `02` counted a frame it should not, and the port paid
it back by having the slide op step on its own arm frame. Both are gone, and the result is §7.3.

The one thing the walk still cannot decide is a CONDITIONAL continue: ops `03`/`04`/`05`/`06`/`26` all reach
C1/4D08 whose tail branches on `$8E`, taking `JMP C1/4E19` (free) for a LOOP and `RTS` (park) for a
one-shot. The walk follows the JMP and reads them free; the C forces the flag from the mode instead, which
is the branch the ROM actually takes. `24` is the same shape and is decided by the rendezvous test.

### 7.3 The result — the ladder EMERGES

Ours against the oracle, as script-frames (ours `f - 1023`, oracle `f - 472`), forest action #3, victim Crono:

| what | oracle | ours |
|---|---|---|
| `02 05` arms / publishes pose **03** | 11 / 12 | 11 / 12 ✓ |
| `98 00 31` arms the approach slide | 14 | 14 ✓ |
| `03 07` arms / publishes pose **04** | 30 / 31 | 30 / 31 ✓ |
| the super's `06 05` arms the victim / takes the slot | 35 / 36 | 35 / 36 ✓ |
| the attacker's second cell, pose **05** | 38 | 38 ✓ |
| `02 00` arms / publishes pose **06** | 60 / 61 | 60 / 61 ✓ |
| the return slide's first step | 62 | 62 ✓ |
| `06 03` settles on pose **01**, dwell 15 | 73 / 74 | 73 / 74 ✓ |

**Every landmark exact, and the sound is still at rel 51.** No tuned constant anywhere in the chain: the
frames come out of the ROM's opcode costs, the ROM's dwell bytes, the ROM's slide law and the ROM's
publish gate.

`src/field.c`'s Hetake ladder — four `0x5E` literals, four anim numbers, four `facing 0`s and the rel
thresholds 3/8/29 — is now the FALLBACK; the pose is `h->ascPmPose[cmb]`. `battleEnemyGenRender` reads the
same field, so **every enemy without a dedicated renderer animates its attack too**, where before it stood
in its idle window for the whole action.

### 7.3b The VICTIM's publishes are DRAWN — the flinch (the second half of "improper animations")

§7.2 derived that the strike super arms the VICTIM's own pose window and that its publish steals the
engine's slot. What that publish IS, visually: **the hurt flinch**. Oracle, the forest bite
(`build/ct.exe --peek saves/battle_re/forest_holdleft_pre.state out.raw --btnscript "40:240,0:300"`,
peeks at f540/f560, `build/or_f540/or_f560` this session): from the strike until the window's end,
Crono is drawn THROWN BACK, sword out — the anim-5 window's pose for his facing (facing 0 → `0x64`,
facing 2 → `0x66`; the window row `E4:[ptrA + anim*4 + facing*stride]` reads `64/65/66/67 FF` across
the four facings). It HOLDS because the pose tick publishes into `$967B` and nothing re-publishes until
the idle stepper (C1/2E89) takes the sprite back outside the action.

Our render used to drop every PC publish on the floor ("tracked for contention, never drawn"), so the
victim stood in his idle stance through every enemy hit — across GATO's 224-frame flurry, most of what
the player watches. Now `battleStanceRender` draws `ascPmPose[seat]` whenever the engine holds a publish
for a PC seat during a script-owned action (`ascOn`), through the same `ctAnimengPcBlit` path as the
stance; the active-Crono bob layer yields for the same condition. GATO's flurry pulses it 7 times (the
flurry track carries `06 05` per hit); the Hetake's `1E 44` holds it once. The victim's OWN track (its
`06 03`) publishes late in the window and is drawn by the same rule — Marle's settle pose at the
Hetake's +110, which is what moved `field-battle-hetake-return-pose`'s pixels.

Pinned: `field-battle-gato-flurry` (mutation: the stance-layer ascPmPose branch deleted → KILLED) plus
the two re-derived Hetake shots.

### 7.3c The pre-publish gap is IDLE — the retired rel ladder must not fill it

The pose tick writes `$967B` only on a playAnim publish; from the dispatch to the script's first `02 05`
publish (~33 frames: the 21f staging pre-roll + the walk to the opcode) the sprite simply KEEPS the last
idle publish. `battleHetakeRender`'s fallback ladder (rel 3/8/29 thresholds from the pre-interpreter
model) was still filling that gap — so the whole bite played EARLY, then the live poses played it again:
the user's "the hetake's attack animation looks strange". The acting branch now falls through to the
idle cells whenever the script owns the action (`animParsed`) and nothing is published yet. ⚠ The gate
is `animParsed`, NOT `ascOn`: `ascOn` only rises at rel 21, so rel 0-20 would still ladder.

`field-battle-hetake-attack-pose` was re-derived off its `+13` anchor (which caught exactly the ladder's
early bite — the ROM is idle there) onto `+75`, inside the live `03 07` window (publishes at +72/+79).

### 7.4 Scope

- **A PC combatant IS armed** in the engine (its window comes from the PC pose tables, same `anim*4` law) —
  because the slot contention above is real — but its published index is **never drawn**: a PC's pixels stay
  the separately-derived PC bake's business. Skip the arm and the attacker's ladder runs 2 frames early;
  the `field-battle-hetake-pose-ladder` mutation proves it.
- ~~**GATO's dedicated renderer is NOT on the live pose.**~~ — **RESOLVED, §7.6.** It reads `ascPmPose[6]`
  first now. The facing-model worry that kept him out (`mvFacing`, default 1, vs the engine's `enFacing`)
  turned out to be moot: the pose engine resolves the window COLUMN from `$96DB` inside its own tick, so the
  renderer never has to pick a facing at all — it just draws the published index. His script had been arming
  the engine correctly the whole time (`03 08` -> 0x15/0x16/0x17 at dwells 20/5/10; the song -> 0x0D/0x0E/
  0x0F; `anim=03` -> 0x0A when he faces right), which is exactly the set `battle_animeng_corpus.md` had
  pinned from captures — the renderer was simply throwing it away.
- `$A028`, the pose tick's other gate, is NOT derived — modelled as 0, which is what every capture shows.
  It is mirrored from `$CD29` every frame (`CD/09B2 LDA $CD29 / STA $A028`) and `$CD29` has exactly one
  writer, `CD/1918` (`LDA [$40] / STA $CD29`), i.e. an operand-driven bank-CD battle command.
- Ops `04`/`05`/`06` are **byte-for-byte indistinguishable**: one handler, no `$9895` read, so `06 aa` is
  identical to `26 aa 01`. Why the ROM spends three opcodes on it is not derived.
- Op `26`'s EFFECT-OBJECT path (track index >= 8) is an anomaly recorded rather than smoothed: it stores
  mode **2**, not 1 (`C1/57EE LDA #02 / STA $8E`). Our scratch objects have no pose window, so nothing
  here depends on it.
- An **empty looped window** (`dwells[0] == 0` with mode 2) is an infinite loop in the ROM (C1/42E4 sets
  cursor 0 and BRAs back to the same zero read). Hetake `animId 0x30` anim 2 is exactly such a window. The
  port bounds that retry, so it degrades to "this combatant is permanently due" instead of hanging; no
  shipped script reaches it, which is presumably why the ROM can afford the loop.

## 7.5 The FACING opcodes, the box EXTENTS, and the crit's approach

Three more mechanisms read straight out of the ROM in this pass.

**`72 tt` — SET FACING** (`C1/5FDE`, whose mode table is `C1/75CC` dispatching through `C1/79A1`):

| mode | handler | facing |
|---|---|---|
| `00-09` | C1/75D7 | copy role `tt`'s own facing, `$96DB[$A014[tt]]` |
| `0A-13` | C1/75E3 | `faceTab[atan2(self, role[tt-0x0A])]` — `72 0D` faces the VICTIM, `72 0A` the ACTOR |
| `14-17` | C1/760A | the literal facing `tt - 0x14` |
| `18` | C1/7612 | `faceTab[atan2(self, (0x80,0x70))]` — the screen centre |
| `19-1F` | C1/7639 | the POSITION resolver: writes `$A2B0`, not `$A2B4`, so the store re-uses a stale value — inert |

Ops `A8`/`A9` stamp it too, from the heading op `76` left in `$A5AA` (`C1/69F1` / `C1/6B1E`:
`LDA $A5AA,Y / TAX / LDA $C0F700,X / STA $96DB,Y`). Oracle: GATO's `72 0D` writes `$96DE[3] = 3` at f592
(`C1:600C`) when his victim is Marle to his right; her own recoil turns her to 3 at f1200 (`C1:6B25`) and
the victim track's `72 0A` puts her back to 2 at f1213. This matters beyond cosmetics — `$96DB,X` is the
window COLUMN the pose engine reads at `C1/4283`, so the facing picks which attack/flinch sprite draws.
Pinned by `field-battle-script-facing-ops`.

**The box half-extents are per-enemy ROM data.** `CC/EBBE` (`LDA $E4F607,X / STA $9721,Y`) and `CC/EBC5`
(`LDA $E4F608,X / STA $972C,Y`) stage `$971E`/`$9729` from the enemy's `E4/F600` record bytes **+7 / +8**;
a PC gets 8/8. Hetake 16/16, GATO 16/16, Beetle 8/16, Avian Chaos 2/16, Diablos 8/8, Imp 14/20. The port
had `GATO ? 16 : 8`, which put the HETAKES at 8 — the oracle's forest boot reads
`$971E = 08 08 08 10 10 10` (`--monitor "7E971E:11,7E9729:11" --diff`, f240). The box itself (`C1/285A`)
is `x ± $971E`, `y - $9729`, and `y + 8` **unless `$2989` bit 2 is set**, in which case the bottom is the
foot exactly (`C1/2897-28AC`); bit 2 is clear in every capture here (`$2989` = 0x02 forest, 0x12 bat9).

**The crit's approach is a SLIDE, and the old model only looked right because of the extent bug.** The
crit script's `1E 2B` super is `CE/D795` = `0C | 02 26 | D0 | 98 02 28 | D1 | 1F`: speed 4, windup loop,
hide the shadow, slide to resolver mode `0x28`, show it, pop. Mode `0x28` is `C1/780B`'s **weighted point**

```
  X = (wA * A.x + wB * B.x) >> 3      (C1/0089 hardware multiply, C1/011B = LSR x3)
  (wA, wB) = CC/FB7F[idx*2]           idx = mode - 0x24 : 07 01 / 06 02 / 05 03 / 03 05 / 02 06 / 01 07 / 04 04
  A = the ACTOR ($A014), B = the PRIMARY TARGET ($A017)
```

so `0x28` is `(2*self + 6*victim) >> 3`, and the travel is the ordinary `C1/6299` → `CF/F113` slide.
**There is no box test anywhere in it** — `CF/F130-F193` reads the `mm` operand only to pick the Z-hop
height `$A4AF`. bat3crit: Crono (160,71), Hetake (120,63) → lerp (130,65) → 30/4 = **7** steps × 4px →
(132,65), and the oracle's `$1D0C/$1D23` at f49 read `84/41` — exactly that; the slashes post at rel 32
and 73, pan 84, which the forest crit now reproduces. The retired model halted a polar hop on a STRICT
AABB overlap and coincided with the oracle **only while the Hetake's extent was the mis-staged 8**.

⚠ One consequence, recorded rather than hidden: `field-battle-bat9-return` moved f2205 → f2208. That
MASHA fight rolls natural crits, so a corrected crit arrival changes the fight's length. Its subject
(control restored, enemies delisted, the 3-PC regroup) is unchanged.

## 7.6 GATO joins the engine — and the OAM solves his FOOT

The Hetake's rel ladder went in §7.3; GATO's stayed, because `battleGatoRender` is a much bigger tower:
a 4-phase song sway on a uniform 15-frame dwell inside a `rel 42..236` lit window, a flurry of
`windup + 6 x (8f fist / 8f retract)`, three rel windows for the front swing, a `strikeAt` flip for the
directional attacks. **Every one of them is a reconstruction of what the ported pose engine already
emits.** Measured — the oracle's `$967B` writer stream against our `ascpose` trace, over the song:

| | 0D | 0E | 0F | 0E | 0E | 0D |
|---|---|---|---|---|---|---|
| oracle `C1:4307` | f595 | f611 | f627 | f643 | f649 | f654 |
| ours `ascpose` | f992 | f1007 | f1022 | f1037 | f1042 | f1047 |
| gap | | 16 / 15 | 16 / 15 | 16 / 15 | **6 / 5** | **5 / 5** |

Same cycle, same ROM dwells — **including the two short beats** that the baked `SWAY_DWELL 15` flattened.
And the engine resolves the facing itself, which the ladder could not: at f1137 `anim=03` publishes **0A**
(the oracle publishes 0A at f796, the song's end) and `anim=08` publishes **1D / 1E / 1F**, the RIGHT-facing
attack — where the ladder had to pick `ctAnimengGatoAtk[3]` from `mvFacing[3]` and then guess the flip
frame. So the live pose now runs FIRST and the ladders are demoted to the pre-publish gap, exactly as
`battleHetakeRender` already had it.

### The foot: solved from the oracle's OAM, not chosen

The renderer carried **two** foot bases — 111 for the song and the directional attacks, 118 for the idle,
the hurt pose and the front attack. They cannot both be right: E3 idx `04` (front idle-rest) and idx `0E`
(song sway) have the **identical** y-extent `[-56..-8]`. The oracle answers it outright. `ct.exe --peek
saves/gato_inbattle.state <out> --btnscript 0:N` dumps its OAM; subtract one E3 record's twelve `(dx,dy)`
from GATO's twelve OAM `(x,y)` and at four frames the twelve differences **collapse to a single point**:

```
  f300 -> idx 01  foot (128,101)        f450 -> idx 0A  foot (134, 85)
  f510 -> idx 04  foot (135, 85)        f620 -> idx 0E  foot (135, 85)
```

and `--monitor "7E1D0C:11,7E1D23:11"` reads `128/101`, `134/85`, `135/85`, `135/85` at exactly those
frames. **The E3 positions are offsets from the live foot `($1D0C,X / $1D23,X)`** — one foot for the walk
poses, the front idle-rest and the song sway alike. 118 was the wrong base: it drew GATO **seven pixels
below** where the ROM draws him, in the idle, the hurt pose and the front attack. `CT_GATO_FOOT_X/Y` is now
`136/111` = `16*rec[0]+rec[2]` / `16*rec[1]+rec[3]` off the staged record at `$29BF+slot*12` — the same
staged-foot expression `battleEnemyPoseBlit` already uses for every enemy without a dedicated renderer —
and the anchor chain carries it to the live `(135,85)`.

⚠ **Twenty-one GATO-fight shot pins moved by exactly that seven pixels, and NOT ONE forest / bat8 / bat9
pin moved.** That asymmetry is the scope proof: the change is in GATO's renderer and the Hetake path was
already on the live pose. Pinned by `field-battle-gato-live-pose-and-foot` (mutations: the 118 base
returns -> KILLED; the renderer stops reading `ascPmPose` -> KILLED).

⚠ **OPEN, recorded rather than invented:** the staged Y (111) and the live Y (85) differ by a constant 26
whose ROM origin is NOT derived here. Our anchor chain reproduces it algebraically and tracks a re-staged
GATO 1:1, so nothing is wrong on screen — but the staging -> `$1D23` seed itself is unported. The oracle's
`$1D23[6]` also *starts* at 118 and settles to 85 across the fight, so the pair is a live mover output,
not a constant.

## 7.7 The song's NOTES move — and the residual was never the effect opcodes

The whole of `CD/9EDA` reads out cleanly once the opcode lengths come from the port's own table (`1A` is
length **1**, not 2 — read it as 2 and `1A 36` looks like one opcode instead of the teleport followed by
the sync INC, which is what hid the chain):

```
 t0  9EF0: 24 05 | 3D 00 | 30 1B 01 | 32 0A | 72 0D | 02 06 | 24 0C | 30 1A 01 | 20 20 | 06 03 | 50 | 2E | 01
 t3  9F0A: 24 0A | 02 09 | 78 AA | 24 0B | 20 0F | 36 | 06 03 | 00
 t4  9F18: 24 0A | 02 09 | 24 0C | 06 03 | 00
 t8  9F21: 60 00 | 73 00 | 33 00 | 3A 1E 09 | 3A 1F D7 | 1A | 36 | 24 0A | 70 | 02 00 | 09 | 85 00 | A8 30 | 71 | 00
 t9  9F3B: 73 00 | 24 01 | 1A | 36 | 24 0A | 20 1E | 70 | 02 00 | 09 | 85 E0 | A8 30 | 71 | 00
 t10 9F4F: 73 00 | 24 02 | 1A | 36 |         20 3C | 70 | 02 00 | 09 | 85 20 | A8 30 | 71 | 36 | 00
 t11 9F62: 73 00 | 24 03 | 1A | 36 | 24 0A | 20 0F | 70 | 02 00 | 09 | 85 10 | A8 30 | 71 | 00
 t12 9F76: 73 00 | 24 04 | 1A | 36 | 24 0A | 20 3C | 70 | 02 00 | 09 | 85 F0 | A8 30 | 71 | 36 | 00
```

**The sync ladder is a CHAIN**, not a countdown: t8 `36`→1, then t9 waits `24 01` and `36`→2, t10 →3,
t11 →4, t12 →5, and only then does t0's `24 05` release into `32 0A`. Five INCs, one per frame — the
oracle's f587, f588, f589, f590, f591, then `32 0A` at f592.

**The note motion is three opcodes**, the same three on each effect track, and every step is confirmed on
the oracle to the byte (`--monitor "7E5D9E:2" --diff --writer`):

```
  f585  7E5D9E=8755*(C1:592C)     `33 00`  resolve mode 0 = the ACTOR -> (0x87,0x55) = (135,85) = GATO's
                                            live foot, the very point the OAM solve of section 7.6 gives
  f586  7E5D9E=902C*(C1:5989)     `3A 1E 09` then `3A 1F D7`: 0x87+0x09 = 0x90, 0x55+0xD7 = 0x12C -> 0x2C
```

so the notes are born at **(144,44)**, and `--monitor "7EA240:10,7EA250:10"` shows the oracle spawn five
effect objects on exactly `0x0090`/`0x002C`, one per frame f586-f590, fanned out on the five headings
`00 E0 20 10 F0` — precisely the five `85` operands. Ported: `3A xx yy` (`$5D80[xx] += yy`, an 8-bit ADC
that WRAPS, unlike `49`'s saturating add), `1A` (place on `$5D9E/$5D9F`, which ARE `$5D80[0x1E]/[0x1F]`),
`85 hh` (the heading `A8`/`A9` read, `$A5AA,X` for a combatant / `$A5B5,$A2B6` for an effect object).

**The landings are the proof.** Oracle finals at f754 vs ours:

| obj | head | oracle | ours | |
|---|---|---|---|---|
| 0 (t8) | 00 | `BE 2C` | (190,44) | ✓ |
| 1 (t9) | E0 | `AC 0F` | (177,10) | ✗ — see below |
| 2 (t10) | 20 | `B1 4D` | (177,77) | ✓ |
| 3 (t11) | 10 | `BB 3D` | (187,61) | ✓ |
| 4 (t12) | F0 | `BB 1A` | (187,26) | ✓ |

Four of five byte-identical over a 48-step drift from a script-computed spawn point. The fifth is
explained, not shrugged at: `(177,10)` is the exact mirror of t10's `(177,77)` about the spawn row, so OUR
note runs its full 48 while the ROM's stops ~5 steps short — `CF/F61F` tests both coordinates through
`CF/FADD` before every step (`BEQ done` / `CMP #0E` / `CMP #0F`) and ends the move once the object leaves
the visible area. obj1 is the one heading up-and-right into the top edge. That bounds test is **not
ported**. Pinned by `field-battle-song-note-motion` (4 mutations, all killed).

⚠ Still a FIXTURE: `battleSongNotesRender` draws the notes from a per-frame OAM ledger keyed on `rel`, so
the motion above is not yet on screen. Wiring it needs the effect object's GLYPH, which comes from `73 00`
(the bind) + `60 00` (palette) + `02 00` (play anim 0) — none of them derived yet.

### The song's timing residual is the PRE-ROLL, and the effect opcodes were never the cause

The previous entry blamed the sound landing at rel 29 instead of the oracle's 41 on tracks 8-12 running
"faster than the ROM's" because their opcodes are inert passes. **That was wrong, and the port's own design
is why:** an unimplemented opcode still falls through `battleAscTick`'s switch with the ROM-read `isFree`
and the ROM-read length, so it costs exactly the frames the ROM charges. Timing was never at risk.

The real number, measured:

```
  --monitor "7E9877:1,7E5DBD:48" --diff --writer
    f551   7E9877=10*(C1:469A)                    <- the DISPATCH (and C1:473D loads all 16 track IPs, and
                                                     C1:472B sets all 16 live flags, on this same frame)
    f584   7E5DBD=...259FCD...*(C1:4CEF)          <- the FIRST time any IP advances
```

Every IP is loaded at f551 and **not one of them moves until f584**: the interpreter is frozen for **33
frames**, and `$5DFD` (the per-track pause) is all zero throughout, so it is not a pause — `C1/4BBE`, whose
only caller is `C1/40CB` inside the action frame loop at `C1/40B0`, simply is not reached yet. Our port
arms at `CT_PC_ACT_PREROLL` = 21. **33 - 21 = 12, which is exactly the rel 29 vs 41 gap.** Three casts
measure 33 / 32 / 32.

That also re-frames OPEN item 1 (GATO's physical measured 24, the Hetake's 21): the pre-roll is not one
constant, it is however long the one-time prologue at `C1/40A0` takes — `JSL CC/F156` then
`JSR C1/4310`, the action-type dispatcher (`($7A63,X)` on `$AE92`) that does the staging. Its length is
work-dependent, so this is the `copyFreeze` situation again: cycle-emergent, to be oracle-anchored per
action KIND, never scene-gated. **Not changed here** — a single number tuned to 33 would fix the song and
break the Hetake's exact rel 51.

## 7.8 The SPEED opcode has two halves, and the port was reading one

§7.7 ended on an honest contradiction. `A8`/`A9` arm mover mode 2 (`CF/F61F`) in both cases, and mode 2
holds no divider — `$A1E0` is the move-FINISHED flag written at `CF/F687`, not a phase — yet:

```
  GATO's song note (mode 2)      $A644  0x30 f596 -> 0x2F f597 -> 0x2E f599 -> ... -> 0x00 f691
  the forest's effect obj (mode 2)  $A644  0x04 f475 -> 0x03 f476 -> 0x02 f477 -> 0x01 f478 -> 0x00 f479
```

every OTHER frame against every frame. A 2-frame rate was tried as a universal law, moved five pinned
checks including the flagship `field-battle-hetake-attack-sfx`, and was reverted. The rule is real and it
is **one loop above** the mover mode, in the two mover LOOPS themselves:

```
  CF/EFBD  LDX #000A / STX $C6            ; the 11 COMBATANTS
  CF/EFC2  LDX $C6
           LDA $A337,X / BEQ skip         ;   not moving
           DEC $A2EB,X / BNE skip         ;   <<< THE PER-OBJECT RATE COUNTDOWN
           LDA $A2FE,X / STA $A2EB,X      ;   reload from the RATE
           ... LDA $A86D,X / JSR ($F01E,X)

  CF/EFED  LDX #0007 / STX $C6            ; the 8 EFFECT OBJECTS
  CF/EFF2  ... $A342 / DEC $A2F6,X / BNE / LDA $A309,X / STA $A2F6,X ... LDA $A878,X / JSR ($F02E,X)
```

Both loops run **every frame over their whole list** and skip an object whose countdown has not expired,
so the divider gates the **mode dispatch itself** — every kind of travel, slides included, not just
move-forward.

And the rate comes from the **speed opcode**, which sets TWO halves. `C1/4F54` indexes `X = $9895 - 8`
into two tables and stores both, on either arm of its track split:

| | rate | step |
|---|---|---|
| track < 8 (combatant, `Y = $A014[track]`) | `$A2FE,Y = CC/F5BC[X]` | `$A324,Y = CC/F4BC[X]` |
| track >= 8 (effect object, `Y = $A2B6`) | `$A309,Y = CC/F5BC[X]` | `$A32F,Y = CC/F4BC[X]` |

(`C1/4F6B`-`4F76` and `C1/4FAC`-`4FB7`; the `== 4` arm loops the same pair over every victim.) The port
read only `CC/F4BC`. Together the two tables are a clean geometric ladder:

```
  op     08    09    0A    0B    0C    0D    0E    0F
  step   01    01    01    02    04    08    10    20      CC/F4BC
  rate   04    02    01    01    01    01    01    01      CC/F5BC
  px/f   0.25  0.5   1     2     4     8     16    32
```

Oracle, on both sides of the old contradiction: `--monitor "7EA309:8,7EA2F6:8" --diff` reads
`$A309[0] = 02` in the GATO capture with `$A2F6` oscillating 1,2,1,2, and `$A309[7] = 01` in the forest
one; `--writer` attributes both stores to `C1:4FB3` / `C1:4FBA`, that very handler. **The note runs `09`
(rate 2) and the forest object `0C` (rate 1) — neither rate was ever universal.**

**Blast radius, measured rather than guessed.** Walking every script in both anim tables (113 ATK + 117
TEC distinct scripts) with the port's own length table finds **89 sites** using op `08` or `09` — almost
all of them effect tracks 8-15 of techs, i.e. essentially every slow-drifting FX sprite in the game was
running 2x or 4x too fast. (Ops `0E`/`0F` are never used at all.) After the fix `A8 30` at rate 2 spans
**95 frames**, and the oracle's own `$A644` runs f596 -> f691 — 95. The landings are unchanged, because
the accumulator law was already right; only the duration was wrong.

⚠ It also shortened a residual §7.7 recorded: the song's victim flinch used to run past its window with
the effect tracks finishing around rel 140; they now finish at rel 185-190 and the flinch ends at rel
200 (oracle 236, of which 12 frames are the known pre-roll deficit). Pinned by
`field-battle-song-note-motion` (`rate=2` is in the expected trace; the mutation that re-ignores
`CC/F5BC` is KILLED).

## 8. OPEN / NOT DERIVED (loudly)

0. **Two user reports NOT REPRODUCED, stated per the verify-negatives rule** (the conditions are named so a
   future repro can falsify them, and "I did not reproduce it under X" is all this claims):
   - *"crono usually stands idle facing the wrong direction"* — measured: our `pcFacing` equals the
     oracle's `$96DB` byte-for-byte in BOTH arenas (GATO `00 02` = Crono UP / Marle LEFT; forest `02 03`),
     and the post-entrance idle PIXELS match the oracle's peek (sprite-masked auto-aligned diff, the
     residue being the bob state). The moments that LOOK wrong and are now fixed: the missing victim
     flinch (§7.3b — during every enemy hit the ROM's Crono is thrown into the hurt pose and ours stood
     straight), and the entrance keyframes (correct, hold to module f122/f176). If the report persists
     after the flinch, a savestate/frame of it is needed.
   - *"in the battle against GATO, he is not attacking anymore" (solo Crono)* — not reproduced in ANY driveable
     path: battletest and interactive, MASHA and manual-menu (CT_BATTLE_SCRIPT `n:240,a:1,n:3,a:1`), duo
     and solo party. Crono attacks (4 attacks to victory in the full interactive fight, f195/f647/f1131/
     f1575), GATO attacks (both selectors, 0x31 and 0x34), the swing/strike/return RENDER frame-checked
     against the window (`build/strip.png` this session). The interactive-vs-headless module/field frame
     anchor is now printable (`battlephase: begins at field fN`, under CT_BATTLE_TRACE).

1. **THE PRE-ROLL IS NOT A CONSTANT** — *the blocker below is CLOSED, and the big half of the spread
   is now derived and shipped (2026-09-07).* What follows first is what this entry used to ask for,
   then what answered it.

   ### The blocker this entry named: a real DISPATCH MARKER

   The old sweeps keyed "the action starts" on two specific `$5DBD` writers (`C1:4583` / `C1:473D`) and
   this entry recorded, correctly, that **different fights load through different routines** (bat2's only
   loader is `C1:447A`; bat4/8/9 mix four), so the P/T labels in the table below were "which loader
   happened to fire", not "physical vs tech". It asked for a marker that does not depend on the loader.

   ⭐ **It is `$A013`, and it was already in the port.** The action frame loop `C1/4090` runs a ONE-TIME
   prologue before its per-frame body:

   ```
   C1/409C  JSL CC/F102
   C1/40A0  JSR C1/007E          <- one frame (C1/007E is INC $9E / JSL CD0036 / BNE, the frame wait)
   C1/40A3  JSR C1/10FA
   C1/40A6  INC $A013            <- THE MARKER: once per action, ahead of every loader and handler
   C1/40A9  JSL CC/F156          -- setup only ($AE91 -> $A014, defaults from CC/FBC8); no frame waits
   C1/40AD  JSR C1/4310          -- the action-type dispatcher, JSR ($7A63,X) on $AE92
   C1/40B0  JSR C1/007E          <- the per-frame body starts HERE
   ...
   C1/40CB  JSR C1/4BBE          <- the script advance, i.e. the end of the pre-roll
   C1/40CE  LDA $9876 / BEQ C1/40B0
   ```

   `--monitor "7EA013:1" --diff --writer` shows the 0->1 edge attributed to `C1:40A9` (the PC after the
   INC) exactly once per action in **every** capture, and the 1->0 at `C1:41BD` at span end. Measuring
   dispatch->first `$5DBD` advance (`C1:4CEF`) against it reproduces this entry's own numbers on bat3
   (f106 -> f127 = 21) with no loader in the key at all.

   ### Where the frames actually go

   Both script loaders converge on `JMP C1/4833`, and that is the only place the prologue spends frames:

   ```
   C1/4833  LDA $987A / JSL CD0015   -> CD/1323: $CACE=A, $CACF=0, $CAD0=4  \_ same loop body, CD/132E
   C1/483A  LDA $987B / JSL CD002A   -> CD/1314: $CACE=A, $CACF=4, $CAD0=6  /  (INC $CACE per iteration)
   C1/4841  JSR C1/007E                                                        1 frame
   C1/4844  LDA $AE93 / CMP #37 / BEQ ; LDA $AE94 / BNE ->C1/485B
   C1/4850  LDA $987C / CMP #FF / BEQ ->C1/485B          <-- THE THIRD UPLOAD IS GATED
   C1/4857  JSL CD0018
   C1/485B  JSR C1/007E                                                        1 frame
   C1/4861  JSR C1/48EC
   C1/4864  JSR C1/007E                                                        1 frame
   ```

   `$987C` is `#FF` from the dispatcher's own `LDA #FF / STA $987C` (`C1/431B`) unless a config record
   fills it in. So the pre-roll is **the cost of the staging uploads**, and one of the three is switchable.

   ### The staging arguments come out of ROM config records

   ```
   ENEMY PHYSICAL  C1/4497  X = $9853[(actor-3)*2] * 6      -- indexed by the ENEMY'S OWN ID
                            $AE93 == 0 -> CD/4926 + X       (attack 1)
                            $AE93 != 0 -> CD/4F26 + X       (attack 2)
                            6-byte record, +1 -> $987A, +2 -> $987B; $987C is NEVER written (stays FF)
   ENEMY TECH      C1/4693  CD/5526 + $AE93*7               +1 -> $987A, +2 -> $987B, +6 -> $987C
   PC              C1/45BB  CD/45A6 + id*7                  same layout (the port already reads +0)
   PC ITEM         C1/476F  CD/5C26 + sub*7                 same layout (ditto)
   ```

   Predicting ($987A,$987B,$987C) from those records and comparing against the oracle's own RAM at the
   dispatch frame: **72 actions across 12 captures, 72 ok, 0 mismatch**, with enemy ids 0C/10/15/1E/48/
   5E/92 resolving through the right table each time.

   ### ⭐ WHAT IS NOW DERIVED AND SHIPPED: the third upload's step

   ```
   $987C == FF  (the third upload is SKIPPED)   65 actions   ->  21..24 frames
   $987C != FF  (C1/4857 JSL CD0018 runs)        7 actions   ->  32..33 frames
   ```

   **No overlap.** That is the step `battleActStage3` / `battleActPreroll` in `src/battle.c` now models,
   and it is exactly what this entry recorded as unresolvable with one constant — *"one constant moved to
   33 fixes the song and breaks the Hetake's exact rel 51"*. It does not have to: the Hetake's physical
   never writes `$987C` (21), GATO's song config carries `0x5B` (33). Both are right at once.

   ORACLE-VERIFIED, not inferred from this dossier's prose: on `gato_inbattle`, `--monitor
   "7EA013:1,7E1E01:2" --writer` gives song dispatch f551 -> its op-0x78 (`id AA`) at f592 = **rel 41**
   (and f1813 -> f1853 = 40). The port posted rel 29 and now posts **41**. Five checks moved with it and
   were re-pinned through the runner; GATO's physical stayed at its pinned rel 43 throughout, because its
   `$987C` is FF.

   ### ⚠ WHAT IS STILL NOT DERIVED

   - **The 21..24 spread INSIDE the `$987C == FF` group** — now TAKEN APART rather than left as
     "cycle-emergent". See the next section.
   - **A PC PHYSICAL's `$987C` is not derived.** It stages the equipped WEAPON's sheet
     (`C1/432C` -> `C1/49FF` -> `$CCF83F[$AE91]` -> `$1C48`), a path this has not followed. The port
     reports FF for it, which matched all 10 PC physicals measured (gato_inbattle + bat9) — a measurement
     over those captures' weapons, not a proof.
   - **The enemy-side `+6` offset is not separately graded.** GATO's song record is
     `10 C8 00 0D 0F 0F 5B`, whose byte +5 (`0F`) is also non-FF, so a check on the song cannot tell +5
     from +6. The PC-tech offset IS graded (Cyclone's record `01 DA EF 20 01 01 FF` separates them, and
     the mutation is pinned on `field-battle-pc-tech-sfx`). Closing the enemy side needs a live-fight
     fixture for a fight whose enemy tech has `$987C == FF` — bat4/5/6 (id 07), bat9 (06), bat7 (03),
     bat8 (05), bat2 (1E); the existing `bat9.raw.wram` is a RENDERWRAM fixture and does not run a fight.

   ### ⭐ THE 21..24 SPREAD: `pre-roll = 13 + U`

   **THE 13 IS COUNTED, NOT FITTED.** Every frame the ROM spends in the prologue goes through
   `JSR C1/007E`, because `JSL CD/0036` — the one-frame primitive it wraps — occurs at **exactly one site
   in the whole 4 MB**, inside `C1/007E` itself. Bank CD therefore cannot wait on it at all. The sites in
   the prologue's tail are:

   ```
   C1/4841  JSR C1/007E                                    1
   C1/485B  JSR C1/007E                                    1
   C1/4864  JSR C1/007E                                    1
   C1/488D  JSR C1/007E                                    1
   C1/489B..C1/48B0   JSR C1/4943  x8                      8   (C1/4943 opens with JSR C1/007E)
   C1/40B0  JSR C1/007E   (the frame loop's own entry)     1
                                                          --
                                                          13
   ```

   MEASURED CONFIRMATION: split the window at `C1/4880` (the `JSL C3/0002` whose `$0300..$0305` setup
   marks it — `--monitor "7E0300:6" --writer` reports the writer as `C1:4880`). The second half,
   `C1/4880 -> the first $5DBD advance`, is **exactly 10 frames in all 102 samples, every key, every
   capture**: `1 + 8 + 1`. The three staging waits make the other 3.

   **U IS A PACKET COUNT, NOT A BYTE RATE.** `JSL CD0015` / `JSL CD002A` land in `CD/132E`, which loops a
   FIXED count (`$CACF` 0..3 then 4..5) and per slot calls `CD/3ED2`. `CD/3ED2` SPINS
   `while $CA26 != 0` on `CD/3E75` -> `CD/3E7D` (`INC $CA20 / JSR CD/3E44 / LDA $CA20 / BNE`) — one frame
   per service — and each service performs one VRAM DMA (`CD/3E8E`: `STY $2116`, `$4370 <- 01`,
   `$4371 <- 18`, length from `$45`). `$CA26` is filled by the sheet walk at `CD/3F1A / CD/3F64 /
   CD/3FAA`. So **U counts the DMA packets the action's six sheets need**, with `$CACE` starting at
   `$987A` then `$987B`.

   MEASURED U, 102 actions across 12 captures, free-running and driven:

   ```
   $987A/B/C     n     pre-roll seen                U
   00/00/FF     22     21 x19  22 x3                8
   20/24/FF     10     21 x7   22 x3                8
   36/3A/FF      2     21 x2                        8
   2C/30/FF      1     22 x1                        9
   07/0B/FF      1     23 x1                       10
   0A/0E/FF      5     24 x5                       11
   C0/C4/FF     54     24 x52  25 x1  26 x1        11
   C8/00/5B      7     33 x6   32 x1               20   (the third upload runs)
   ```

   ⚠ **DELIBERATELY NOT SHIPPED AS A TABLE.** U is a packet count that belongs *derived* from the sheet
   data — the same species as the scene loader's packet cost, which this project already prices from the
   ROM (`field-load-cost-vs-rom`) — and tabulating oracle modes would store the answer without the
   mechanism, where three of the eight keys have n <= 2 and the well-sampled ones still carry a +1/+2 tail
   (the queue is not always empty when an upload starts). WHAT WOULD CLOSE IT: the per-sheet DMA packet
   count, out of the walk at `CD/3EFD..CD/3FAA`.

   ⚠ **THE NAMED RESIDUAL THAT LEAVES:** `CT_PC_ACT_PREROLL` = 21 is right for `00/00`, `20/24` and
   `36/3A`, and **3 frames early for `C0/C4`** — GATO's physical and most enemy physicals, the most common
   key here (n=54, 96% at 24). End to end on `gato_inbattle`: the oracle's physical dispatches f1141 and
   posts its op-0x78 (`id 46`) at f1189 = **rel 48**, where ours posts **rel 43**. Three of those five
   frames are this; the other two are not yet accounted for.

   The `$987C` step is a different animal, and that is why it IS shipped: it separates two groups with no
   overlap at all (21..26 against 32..33), because it switches a whole upload on or off rather than
   stretching one.

   ### The original table, kept for the record

   Measured the old way (dispatch = the `$9877` write at `C1:469A`; start = the first `$5DBD` advance):

   ```
     bat3 (3x Hetake)     P21 P21 P21 P21 P21 P21 P21
     bat2 (Beetle/Avian)  P24 P21 P24 T24 P21 P24        <-- BOTH values, in ONE battle
     bat4 (Imp)           P24 P24 P24 T24 T24 T24 P24
     bat8 (Naga-ette)     P24 P24 P24 P24 T24 P24 T24
     bat9 (Diablos)       P24 P24 P24 P24 P24 P24 T24 P24
     bat1                 T33 P24 T33 P24
     GATO                 P24 P24 P24 P24  /  T33 T33 T33 T33
   ```

   Every simple key failed against it, and the reason is now plain: the split is not tech-vs-physical
   and not the enemy — it is whether the action's config asks for the third staging upload.

2. ~~**`03 aa` park duration**~~ — CLOSED by §7: the pose engine is ported, so the park emerges (14 frames
   for the Hetake's `03 07`, the oracle's own) and the whole ladder lands on the oracle's frames, §7.3.
3. ~~**`A8 dd`**~~ — **CLOSED, both halves.** The MAGNITUDE law: `CF/F61F` (the mode-2 mover that both
   A8 and A9 arm) runs `DEC $A644,X / BEQ done` **then** `$A31C,X += $A32F,X` and only then the two
   `CF/F9FB` components, so the accumulator is `k*speed` with k advanced FIRST — it runs `speed` ..
   `(n-1)*speed`, never 0, and the tick that zeroes the distance draws nothing.
   The CADENCE, which §7.7 recorded as a measured CONTRADICTION (the song's note decrementing every
   OTHER frame and the forest's effect object every frame, on the same opcode and the same mode), is
   **the missing half of the speed opcode** — see §7.8.
4. **Resolver modes `1C-23`** are implemented as *the executing track's own object*. The real index is
   allocated at runtime through `$A2B6`; every ported use is a track reading the object it just moved, and
   the oracle agrees to the byte (the Hetake's `33 23` → (0x94,0x44)). A script where the two differ would
   expose it.
5. ~~**The "damage sound" is the enemy DEATH sound, and it is DERIVED but NOT PORTED.**~~ — **PORTED**
   (`battleDeathSfxTick`, src/battle.c): armed at the death event, posts `#95` two frames later at the dying
   combatant's live X; the three forest deaths print the oracle's own pans `78/58/A8`. `field-battle-enemy-death-sfx`
   pins them (mutations: the +2 wait, the pan source). Still NOT ported: the two sibling arms (`#40`/`#38` via
   CD/0E05, a different poster) — they trace `NOT PORTED` instead of guessing; every measured death takes the
   `#95` arm (`$99D2=00, $2A21=00, $A08C/$A01D=00` at all three, oracle-read). The original derivation follows. There is no separate
   damage sound in this fight at all: the idle-PC capture's enemy turns post only `67` (the attack) and `FF`,
   and no hit sound accompanies a PC losing HP. What the user is missing is `id 95`. Tying it to HP:

   ```
   --btnscript "40:240," + "100:4,0:4" x150   --monitor "7E1E01:2,7E5FB0:1,7E6030:1,7E60B0:1" --diff --writer
     f483   7E5FB0=00*(C1:ED12)      f485   7E1E01=9578*(CD:3B7D)
     f746   7E6030=00*(C1:ED12)      f748   7E1E01=9558*(CD:3B7D)
     f1020  7E60B0=00*(C1:ED12)      f1022  7E1E01=95A8*(CD:3B7D)
   ```

   Each post lands **2 frames after that Hetake's curHP hits 0**, panned at its own X. It does NOT come
   through the `$CD23` staging flush (that slot never moves all fight, confirming `battle_sfx_ops_inputs.md`
   §3): a ROM-wide scan for `20 71 3B` finds the direct caller, `python tools/dis.py D0060 0x50`:

   ```
     D0093: LDA $CD00 / TAX / LDA $1D0C,X / TAX / LDA #95 / JSR CD/3B71
   ```

   i.e. **id = the literal `#95`, pan = `$1D0C[$CD00]` = the dying combatant's screen X**, `$CD00` being the
   combatant index the death routine runs for. Two SIBLING variants share that branch and are NOT derived:
   `$A08C,X != 0` takes `CD/0E05` with `#40` and no `95`, and `$A01D,X & 1` takes `CD/0E05` with `#38`.
   Porting it needs those two flags plus the call site of `CD/0050`; our engine already owns the death event
   (the dissolve queue), so the wiring is short once they are read.
6. **GATO's walk was NOT broken.** Measured, not assumed: `CT_BATTLE_MVTRACE=1 … --battletest
   saves/battle_re/gato_t0.raw.wram` gives 288 `battlemove` steps, y 111 → 144 toward his victim — the AI
   mover (`battle_enemy_move_target.md` §A) at its ROM rate of 1px per 4 frames. Slow, but the ROM's own.
   What GATO does NOT do is lunge during the action window, and that is correct: his script's main track is
   `72 0D | 36 | …` with no move op in it. The Hetakes, by contrast, had NO motion of any kind before this
   port — their AI mover mode byte is 0, so that machine is inert for them and their whole movement is the
   anim script's.
7. ~~**The enemy ATTACK ANIMATION itself**~~ — CLOSED by §7 for every enemy the generic renderer draws, for
   the Hetake, and now **for GATO too** (§7.6): his renderer reads `ascPmPose[6]` first, so the song sway,
   the flurry, the front swing and the directional strikes all come off the engine with the ROM's own dwells
   and its own facing resolution. The ladders survive only as the pre-publish-gap fallback. The same pass
   collapsed his TWO foot bases into the one the oracle's OAM solves for, fixing a 7px vertical error in the
   idle / hurt / front-attack poses. (The "facing model disagrees" worry that kept GATO out was moot: the
   engine resolves the window column from `$96DB` itself — it never needed `mvFacing`.)
8. ~~**A tech's tracks.**~~ — **CLOSED**: not stalled, GATED. `CD/61F0[0x10] = CD/9EDA` opens track 0 with
   `24 05 | 3D 00 | 30 1B 01 | 32 0A`, and tracks 3/4 wait on `24 0A` — where the song's SOUND (`78 AA`, the
   oracle's only song SFX, `7E1E01=AA80*(C1:61B5)` at f592) and the victim's own `02 09` FLINCH window live.
   **Nothing in the script COUNTS to 0x0A**: op `32` (C1/590C) SETS `$5D9D` to a literal, and the port had it
   inert, so both tracks waited for ever. Ported with its siblings `30 xx yy` → `$5D80[xx] := yy` (C1/58EC)
   and `3D nn` → `$5D9C := nn` (C1/59FE). Pinned by `field-battle-song-sfx-and-flinch`.
   ⚠ Residual: ours posts at rel 29, the oracle at rel 41. **The earlier diagnosis here was WRONG and is
   corrected in §7.7.** It blamed the `24 05` release chain on tracks 8-12 for "running faster than the
   ROM's" because several of its opcodes were inert passes — but an unimplemented opcode still falls
   through `battleAscTick`'s switch carrying the ROM-read `isFree` and the ROM-read length, so it costs
   exactly the frames the ROM charges and the chain's timing was never at risk. The gap is entirely the
   **PRE-ROLL**: the oracle loads all 16 track IPs at the dispatch frame f551 and not one of them advances
   until **f584**, a 33-frame freeze (`$5DFD` is all zero throughout, so it is not a pause), while our port
   arms at `CT_PC_ACT_PREROLL` = 21. 33 - 21 = 12 = the rel 29 vs 41 gap, exactly. Three casts measure
   33/32/32. Same cause for the flinch overrun. Not tuned: one constant set to 33 would break the Hetake's
   exact rel 51 (§7.7 and OPEN 1). The effect opcodes were ported anyway, on their own merits — they are
   what makes the NOTES move (§7.7).
9. ~~**Crono's song-flinch assembly index un-derived**~~ (was `battle_gato_polish.md` ITEM 3) — **CLOSED by
   the pose engine**: the flinch is simply the ROM's **anim-9** row, facing-resolved — Crono `6E 6E 6F 70`,
   Marle `5A 5A 5B 5C`. The hardcoded `CT_ANIMENG_MARLE_FLINCH = 0x5B` was just Marle's facing-2 column, and
   `battleSongFlinchPose`'s Marle-only special case is retired: the engine publishes both, for any facing.
10. **Multi-actor actions.** Roles 1/2 (`$AE97`/`$AE98`) are never bound — this engine dispatches one actor per
   window. A co-actor script would read them as unresolved (inert), not wrong-but-plausible.
