# BATTLE — THE PC ATTACK IS ARRIVAL-ANCHORED: op-0x18 PARKS the script thread until the walk ends

User defect (translated from Portuguese): *"in battle, crono starts the attack animation while he still has not finished
moving to the enemy"* — the strike animation starts while Crono is still travelling to the enemy.

Derived from the ROM disasm (`tools/dis.py`, flat offset = `(bank−0xC0)<<16|addr`) + the LakeSnes oracle
(`build/ct.exe` copied to scratch as `ct_probe.exe`) ONLY. Every number below carries either a ROM address
with its disasm line, or an oracle frame with the exact command that produced it. **Zero invention.**
Extends `battle_ai_interp.md` §11/§13/§14, `battle_pc_approach_facing.md` §1.5, `battle_span_emergence.md`.
**Nothing under `src/` was touched** — this dossier is the only write.

---

## 0 — HEADLINE (the canonical mechanism)

The whole PC physical is ONE bank-CE command script running on the multi-track interpreter (`C1/4CA2`), and
**op `18 tt` (walk-to-target) does not advance the track cursor while the walker is alive** — the handler
falls through to `C1/560C: LDA #00 / JMP C1/75BB` (advance ZERO), so the interpreter re-fetches the SAME op
next frame. Only on the arrival frame does it take `C1/5603: LDA $A4F0,X / JMP C1/75BB` (advance by the op's
own stored length) and let the thread continue. Therefore **everything after the walk — the face-victim
stamp, the swing play-once, the victim hurt-flash, the ShowDamage popup, the `1E 2A` strike super, the
op-98 home slide, and the `01` EndTech that ends the `$A013` freeze — is ARRIVAL-anchored, not
dispatch-anchored**, and the frozen span simply grows by the walk: `span = 125 + walkFrames`
(oracle: zero-walk 125, 8-frame walk 133, 16-frame 141, 32-frame 156). During the walk the pose is the
RUN window played in LOOP mode (op `02 06` → `$A124 = 2`), so it cycles for however long the walk lasts.
Our engine instead indexes a FIXED dispatch-relative pose table, so on any walk longer than ~10 frames the
strike-raise pose fires while Crono is still sliding — exactly the reported defect.

---

## 1 — THE SEQUENCING LAW (ROM, byte-exact)

### 1.1 The opcode jump table and op 0x18's handler chain

`C1/7A6B[op*2]` (ROM dump, `Chrono_Trigger_USA_SNES.sfc` @ file `0x17A6B`):

```
op 16 -> C1/5409   op 17 -> C1/541B   op 18 -> C1/542D   op 1B -> C1/567D
op 02 -> C1/4D04   op 03 -> C1/4E22   op 06 -> C1/4EA2   op 1E -> C1/56CC
op 1F -> C1/56FB   op 20 -> C1/5714   op 24 -> C1/5756   op 36 -> C1/594B
op 50 -> C1/5ADB   op 01 -> C1/4CF7   op 72 -> C1/5FDE   op 98 -> C1/6299   op 7C -> C1/61EB
```

`python tools/dis.py 0x15409 0x40`:
```
1542D: INY                 ; op 0x18 = walk-to-TARGET
1542E: LDA  [$E6],Y        ;   the tt operand (resolver mode, C1/762E)
15430: JSR  C1/762E        ;   -> $A2B0/$A2B2 = the goal point
15433: LDA  #02            ;   ** $8E = the op's own LENGTH (2) **
15435: STA  $8E
15437: LDA  $A010          ; --- the SHARED tail (op 16/17/18 all land here) ---
1543A: CMP  #08 / BCC ; JMP C1/5603     ; role >= 8 (effect object) -> just advance
15441: CMP  #04 / BNE ; JMP C1/5603     ; role == 4 -> just advance
15448: TAX / LDA $A014,X / TAX          ; role -> COMBATANT
1544F: LDA  $A15B,X
15452: BNE  ->154C1                     ; ** already walking -> the per-segment controller **
```

### 1.2 The ARM frame parks (C1/54BE)

`python tools/dis.py 0x15448 0x60` + `0x15490 0x35 1 0`:
```
15454: LDA $1D0C,X / STA $80 / STA $A553,X      ; base = the live foot
1545C: LDA $1D23,X / STA $A55E,X / STA $81
15464: $A2B0 -> $82 ; $A2B2 -> $83               ; goal
1546E: INC  $A166,X
15471: LDX  $A010 / LDA $8E / STA $A4F0,X       ; ** $A4F0[role] = the op's LENGTH, stashed **
15479: JSR  C1/4A71                              ; BFS flood + path backtrace on the walk grid $7CFD
1549B: (MVN) $7CFD -> $7FFD  (256 bytes)         ; the grid snapshot for this walk
154A8: LDY $80 / TDC / STA $A517,Y / STA $A522,Y ; track = 0
154B1: LDA #FF / STA $A548,Y / STA $A500,Y      ; forbid-dir = FF, segment counter = FF
154B9: LDA #01 / STA $A15B,Y                    ; ** WALKING **
154BE: JMP  C1/560C                             ; ** -> LDA #00 / JMP C1/75BB = ADVANCE ZERO **
```

### 1.3 The per-segment controller parks; only the STOP probe releases (C1/55FC vs C1/55FE)

`python tools/dis.py 0x154C1 0x50` and `0x155C0 0x50`:
```
154C1: LDX $80 / LDA $A500,X / BMI ->154CB ; JMP C1/55FC   ; mid-segment -> C1/55FC
154CB: LDA #07 / STA $A500,X                                ; new segment: reload 7
...    probe the 4 neighbours of $A53D on the 0xE0-marked path, choose dir+variant ...
155E4: LDA $85 / STA $A517,Y                                ; the TRACK
155E9: LDA $CCF414,X / STA $A548,Y                          ; no-backtrack dir
155F0: LDA $CCF434,X / STA $96DB,Y                          ; ** the PC WALK FACING stamp **
155F7: LDA $84 / STA $A53D,Y                                ; the new cell
155FC: BRA  ->1560C                                         ; ** ALSO advance ZERO = PARK **
155FE: LDX $80 / STZ $A15B,X                                ; ** the STOP probe: walking OFF **
15603: LDX $A010 / LDA $A4F0,X / JMP C1/75BB                ; ** advance by the op's LENGTH = RESUME **
1560C: LDA #00 / JMP C1/75BB                                ; the PARK primitive
```

`C1/75BB` is the shared cursor advance (`$E6 += A`), so `A=0` literally re-executes the same op next frame.
**This is the sequencing law.** The same primitive gates the swing:

`python tools/dis.py 0x14E22 0x30` (op 0x03 PLAY-ONCE) + `0x14D5C 0x2A` (the shared play body tail):
```
14E32: LDA $A145,X / BNE ->14E41    ; first visit?
14E37: INC $A145,X / LDA #01 / STA $8E / JMP C1/4D08   ; arm: $A124[cmb] = 1 (mode ONCE)
14D6F: LDA $8E / CMP #01 / BEQ ->14D78 (RTS)           ; ** mode 1 -> RTS, cursor NOT advanced = PARK **
14D75: JMP C1/4E19                                      ; mode 2 (op 02, LOOP) -> advance, non-blocking
14E41: LDA $A124,X / BNE ->14E4E (RTS)                  ; still playing -> PARK
14E46: STZ $A145,X / LDA #02 / JMP C1/75BB              ; anim done -> advance 2
```

So the PC melee thread has **two** parking ops in series: `18 03` (the walk) then `03 31` (the swing).

### 1.4 The scripts (ROM bytes)

`CE/0000[ss*2]` = the `1E ss` super table (`C1/56CC`): **0x0A → CE/D4C3**, **0x2A → CE/D776**,
**0x00 → CE/D428**.

**Base script CE/0380** (bytes `90 80 90 00 | 8E 03 9E 03 A6 03 B6 03 C5 03 | …`): mask word-1 = `90 80`
→ `script[0]` bit7 = Y0, bit4 = Y3; `script[1]` bit7 = Y8 → tracks **role 0 = CE/038E, role 3 = CE/039E,
role 8 = CE/03A6** (the §11a bit→role law). Oracle-confirmed: `$5DBD = 0x038E`, `$5DC9 = 0x039E` the frame
`$A013` goes 1, in BOTH the GATO fight and the bat3 forest fight.

```
role 0 (attacker) CE/038E :  1E 0A | 36 | 7C | 03 31 | 36 | 1E 2A | 23 01 | 24 03 | 2E | 01 | 00
role 3 (victim)   CE/039E :  24 01 | 20 0F | 1E 00 | 35 00 | 60 00 | 73 03 | 24 01 | 72 00 …
role 8 (effect)   CE/03A6 :  60 00 | 73 03 | 24 01 | 72 00 | 1B 00 | 20 0D | 03 00 | 36 00 …

ENGAGE  CE/D4C3 :  07 | 02 06 | 18 03 | 72 0D | 1F
STRIKE  CE/D776 :  76 03 19 | 08 | A8 01 | 26 0E 02 | 0C | D0 | 98 02 19 | 26 0E 01 | 20 03 | D1 |
                   1B 19 | 06 0E | 20 05 | 72 0D | 06 03 | 1F
HURT    CE/D428 :  72 0A | 69 00 | 20 02 | 06 05 | 20 02 | 6A | 03 05 | 06 03 | 50 | 1F
```

New op facts derived this session:
- **`07`** = load the combatant's default mover speed (`C1/4F1A`: `$98BF,X → $A324,X`, `$973F,X → $A2FE,X`),
  length 1, costs 1 frame (no `INC $A00F`).
- **`02 aa`** = PLAY-**LOOP** (`C1/4D04`): working ptrA/ptrB (`$A1E8`/`$A1FE`) := the combatant's own
  `$1C99/$1CBA` pose pointers + `aa*4`; `$A124[cmb] = 2`; **advances** (non-blocking).
- **`7C`** = SFX/pan (`C1/61EB`: `$1E00 = 0x18`, `$1E02 = $1D0C[attacker]`, `JSL C7/0004`, `INC $A00F`),
  length 1, 0-cost chain.
- **`2E`** length 1 (base track `2E | 01` — the `01` EndTech sits at CE/039C, oracle cursor `0x039C`).

---

## 2 — THE POSE ENGINE DURING AND AFTER THE WALK (ROM, byte-exact)

`python tools/dis.py 0x1423A 0x80` + `0x142B0 0x3A` + `0x142EC 0x40` — the per-frame anim advance the action
loop calls (`C1/4090` → `JSR C1/423A`):

```
1423C: LDA $A12F,X / BEQ / DEC $A12F,X    ; ** the per-combatant DWELL TIMER, one DEC per REAL frame **
14250: LDA $A124,X / BEQ ; LDA $A12F,X / BEQ ->14269   ; advance only when the timer hits 0
14283: LDA $96DB,X / CMP $A214,X / …      ; facing changed? re-latch
14293: STA $A5 / LDA $966E,X -> $A7 / JSR C1/00A7 / LDX $A9 / STX $82   ; ** $82 = facing * stride **
142B0: LDA $A13A,X / ADC #01 / STA $A13A,X                 ; pose CURSOR ++
142C9: ADC $A1FE,Y / TAX                                   ; ptrB + cursor   (NO $82 -> facing-INDEPENDENT)
142D0: LDA $E40000,X / BNE ->142EC                         ; the RAW ptrB byte; 0 = terminator
142D8:   LDA $A124,X / CMP #02 / BEQ ->142E4                ;   mode 2 -> ** LOOP ** (cursor := 0)
142DF:   STZ $A124,X                                        ;   mode 1 -> ONE-SHOT DONE (unblocks op 03/06)
142EE: STA $A12F,X                                         ; ** dwell timer = the RAW ptrB byte, in FRAMES **
142F6: ADC $82 / ADC $A1E8,Y / TAX                         ; ptrA + cursor + facing*stride
14300: LDA $E40000,X / STA $967B / JSR C1/1C4A              ; -> the published assembly index ($9907,seat)
```

**Two laws fall out and are both oracle-confirmed below:**
1. `ptrA` is indexed **with** the facing (`+$82`), `ptrB` **without** — pose IDs are per-facing, dwells are
   shared (this is why a facing-1 swing uses the facing-0 dwell row).
2. `$A124 == 2` (op `02`) makes the window **LOOP** forever — the RUN plays for the entire walk, whatever
   its length; `$A124 == 1` (op `03`/`06`) runs it once and unblocks the parked op.

### 2.1 Crono's pose windows, straight from ROM (`E4/2600[0*2]` = ptrA `0x3000`, stride `0x128`; ptrB `0xA800`)

| window | facing 0 (up) | facing 1 (down) | facing 2 (left) | facing 3 (right) | ptrB dwells (shared) |
|---|---|---|---|---|---|
| RUN `+0x18` | 20 41 21 22 42 23 | 1C 3F 1D 1E 40 1F | **24 3B 25 26 3C 27** | 28 3D 29 2A 3E 2B | **06 06 06 06 06 06 00** |
| STRIKE `+0xC4` | **50 51 52** | **55 56 57** | 5A 5B 5C | 5F 60 61 | **0F 0A 0A 00** |
| RECOVER `+0x38` | 49 8D 8E 49 | **47 8B 8C 47** | 4D 91 92 4D | **4B 8F 90 4B** | 08 08 08 08 00 |
| IDLE `+0x0C` | **C6** 4F C7 4F | 43 2F 44 2F | **C8** 33 C9 33 | CA 37 CB 37 | 05 05 05 05 00 |

(Bold = values the oracle actually published in the captures of §3. `CtBattlePcWinPose(0, win, ent, fac)`
in `src/battle.c:287` already reads exactly this address arithmetic.)

---

## 3 — THE ORACLE FRAME TIMELINE (4 distances, 2 arenas)

Probe copy: `cp build/ct.exe $SCRATCH/lane_A/ct_probe.exe`. All commands run from the repo root.

**Forest / 3× Hetake (`saves/battle_re/bat3_lake.state`)** — target-select nav = A → Right×k → A
(`$9614`, `battle_pc_approach_facing.md` §1.2):
```
ct_probe --monitor "5DBD:2,5DC9:2,5DFD:16,A013:1,A15B:1,1D0C:1,A3D1:1,5D9D:1" \
         --from saves/battle_re/bat3_lake.state --input "100:6,0:6,100:6,0:200" --diff        # attack A
ct_probe --monitor "9907:1,A124:1,A15B:1,A013:1,1D0C:1,1D23:1,A3D1:1,96DB:1,5DBD:2" \
         --from saves/battle_re/bat3_lake.state --input "100:6,0:6,80:4,0:4,100:6,0:220" --diff  # attack B
         …  "100:6,0:6,80:4,0:4,80:4,0:4,100:6,0:220"                                            # attack C
```
**GATO (`gato_press_action.state`)**, mash `INP=$(python -c "print(','.join(['100:8,0:8']*40))")` (fight 1)
and `['100:8,0:40']*45` (fight 2, which contains BOTH a zero-walk and an 8-frame-walk Crono melee):
```
ct_probe --monitor "5DBD:2,5DC9:2,A15B:1,A013:1,A3D1:1,5D9D:1,1D23:1" --from gato_press_action.state \
         --input "$INP" --diff
```

### 3.1 The measured table (every row is a monitor row, `*` = the frame the byte changed)

| capture | victim | dispatch `$A013`1 | op-18 arm `$A15B`1 | ARRIVAL `$A15B`0 | walk W | popup `$A3D1`1 | span end `$A013`0 | span |
|---|---|---|---|---|---|---|---|---|
| GATO fight-1 | GATO | **f414** | f436 (+22) | **f445** (+31) | 8 | f484 (arr+39) | f547 (arr+102) | **133** |
| GATO fight-2 #1 | GATO | **f444** | f466 (+22) | **f467** (+23) | **0** | f506 (arr+39) | f569 (arr+102) | **125** |
| GATO fight-2 #2 | GATO | **f1591** | f1613 (+22) | **f1622** (+31) | 8 | f1661 (arr+39) | f1724 (arr+102) | **133** |
| bat3 A | Hetake0 | **f27** | f49 (+22) | **f66** (+39) | 16 | f105 (arr+39) | f168 (arr+102) | **141** |
| bat3 B | Hetake1 | **f36** | f58 (+22) | **f91** (+55) | 32 | f129 (arr+38) | f192 (arr+101) | **156** |
| bat3 C | Hetake2 | **f36** | f59 (+23) | **f68** (+32) | 8 | f106 (arr+38) | f169 (arr+101) | **133** |

**span − W = 125, 125, 125, 125, 124, 125.** `arrival = dispatch + 23 + W`; `spanEnd = arrival + 102`
(≡ popup + 58 + 5 = the `battle_span_emergence.md` §0.2 universal law, now arrival-anchored).
`W = 8 × segments` (the op-18 walker's 8-frame segments, `$A500` 7→0).

### 3.2 The attacker track cursor `$5DBD` — the PARK, frame by frame (bat3 attack A, arrival f66)

Printed byte-swapped; `C6D4` = `0x D4C6`.
```
f27  5DBD=038E   A013=1               ; tracks loaded (CE/038E role-0, CE/039E role-3)
f27..f47  5DBD=038E                   ; the 21-frame PRE-ROLL (no op advances) — see §6 OPEN-1
f48  5DBD=D4C4                        ; `1E 0A` super -> CE/D4C3, then `07` (1 frame)
f49  5DBD=D4C6  A15B=1  A124=2        ; `02 06` (RUN, LOOP) + the `18 03` ARM, SAME frame
f50  A517=0x10 (LEFT) A500=7          ; segment controller picks the track (C1/55E6)
f51..f65  5DBD=D4C6 (UNCHANGED)       ; ** THE PARK ** — live X 0xA0 -> 0x82, 2 px/f
f66  5DBD=D4C8  A15B=0  1D0C=0x80     ; ** ARRIVAL ** — op-18 returned $A4F0[0]=2, thread RESUMES
f67  5DBD=D4CA  96DB: 2 -> 0          ; `72 0D` = face the victim + animId 0x0D
f68  5DBD=0392  A124=1  5D9D=1        ; `1F` POP + `36` sync + `7C` + `03 31` ARM  (all one frame)
f68..f105 5DBD=0392 (UNCHANGED)       ; ** THE SECOND PARK ** — the swing play-once
f105 A3D1=1                           ; the VICTIM track's `50` ShowDamage  (arr+39)
f106 5DBD=0394  A124=0                ; the play-once terminator cleared $A124 -> `36`  (arr+40)
f107 5DBD=D77A  5D9D=3                ; `1E 2A` -> CE/D776, `76 03 19` aim, `08` speed  (arr+41)
f109 5DBD=D77C                        ; `A8 01` contact nudge                            (arr+43)
f111 5DBD=D77F / f112 D780 / f113 D781; `26 0E 02`, `0C`, `D0`                            (arr+47)
f113..f121 5DBD=D781 (UNCHANGED)      ; ** THE THIRD PARK ** — `98 02 19` home slide
f115..f122 1D0C 0x80 -> 0xA0          ; the return, +4 px/f, 8 steps                      (arr+49..+56)
f122 5DBD=D784 / f124 D787 / f125 D789 (5DFD[0]=03 = `20 03` PAUSE, ROM-visible)
f129 D78A / f130 D78C (`1B 19` SNAP-HOME-EXACT) / f132 D78E / f133 D790 (5DFD[0]=05 = `20 05`)
```

### 3.3 The victim track `$5DC9` (CE/039E → CE/D428) — the hurt flash

bat3 A (arrival f66) / GATO fight-1 (arrival f445), identical arrival-relative:
```
arr+2   03A2  ; `24 01` released by the attacker's `36` ($5D9D=1) -> `20 0F` PAUSE 15 ($5DFD[3]=0F)
arr+18  D42A  ; `1E 00` -> CE/D428, `72 0A`
arr+19  D42E  ; ** `69 00` = VICTIM HURT-FLASH ON ** + `20 02`
arr+23  D430  ; `06 05`
arr+24  D432  ; `20 02`
arr+27/28 D433 ; ** `6A` = FLASH OFF **      (window = 8..9 frames; our CT_FX_FLASH_DUR 9 stands)
arr+36  D435  ; `03 05`
arr+38  D437  ; `06 03`
arr+39  ----  ; ** `50` ShowDamage -> $A3D1 = 1 (the damage number is born) **
arr+97  ----  ; $A3D1 = 0 (the 58f popup life)
arr+102 ----  ; `01` EndTech (base CE/039C) fires -> $9876 -> $A013 = 0
```

### 3.4 The published pose `$9907` — every value is `ptrA[facing*0x128 + win + entry]`

| capture | facing during walk | RUN entries observed | facing at strike | STRIKE entries | RECOVER | IDLE |
|---|---|---|---|---|---|---|
| GATO f-1 (arr f445) | 0 | `20`@arm+1, `41`@arm+7 | 0 | `50`@arr+3, `51`@arr+18, `52`@arr+29 | `8B`@arr+45, `47`@retEnd+2 | `C6`@retEnd+19 |
| bat3 A (arr f66) | 2 | `24`@arm+1, `3B`@+7, `25`@+13, (`22`=fac0 e3 @arr+2) | 0 | `50`@arr+4, `51`@arr+19, `52`@arr+29 | `8F`@arr+45, `4B`@retEnd+2 | `C8`@retEnd+19 |
| bat3 B (arr f91) | 2 | `24 3B 25 26 3C 27` at arm+1,+7,+13,+19,+25,+31 | 1 | `55`@arr+3, `56`@arr+18, `57`@arr+28 | `8F`@arr+44 | — |

**bat3 B publishes all six RUN entries of facing 2 at exactly 6-frame intervals — byte-identical to the ROM
row `24 3B 25 26 3C 27` with ptrB dwells `06 06 06 06 06 06`.** The STRIKE dwells observed (15, 10/11) are
the **facing-0** ptrB row `0F 0A 0A` even when the pose IDs come from facing 1 (bat3 B) — the §2 facing-
independent-ptrB law, confirmed in the oracle.

`retEnd` = the frame the op-98 slide completes = `arrival + 48 + R`, `R = returnDist/4` (GATO R=4 → arr+52;
A R=8 → arr+56; B R=16 → arr+63). Return distance always equals the walk distance (the `1B 19` snap makes
the rest exact).

---

## 4 — THE CANONICAL TIMELINE (all constants, one table)

Everything below is **relative to ARRIVAL** except the first three rows.

| symbol | value | meaning | evidence |
|---|---|---|---|
| `PREROLL` | **21** | dispatch (`$A013`←1) → the first CE-script op advance | 5/5 captures: 414→435, 444→465, 27→48, 36→57, 36→58(+1) |
| `ARM` | **22** | dispatch → `02 06` RUN armed + `18 03` walk armed (`$A15B`=1) | f436/f466/f1613/f49/f58 (f59 = the +1 jitter) |
| `TRACK` | **23** | dispatch → the segment controller stamps `$A517`/`$96DB` | f437/f50/f59/f60 |
| — | — | **`arrival = dispatch + 23 + W`, `W = 8 × segments`** (W=0 when the stop probe fires on the first controller pass) | §3.1 all six rows |
| `SWING_ARM` | +2 | `1F`+`36`+`7C`+`03 31` (`$A124`←1) | f68 (arr 66), f447 (arr 445), f93 (arr 91) |
| `SWING_P0` | +3 | STRIKE window entry 0 published | f448/f94 (+3), f70 (+4, jitter) |
| `SWING_P1` | +18 | STRIKE entry 1 (dwell `ptrB[0xC4]`=0x0F=15) | f463/f109 (+18), f85 (+19) |
| `SWING_P2` | +28..29 | STRIKE entry 2 (dwell `ptrB[0xC5]`=0x0A=10) | f474/f95 (+29), f119 (+28) |
| `FLASH_ON` | **+19** | victim `69 00` hurt-flash ON | f464 (arr 445), f85 (arr 66) |
| `FLASH_OFF` | +27..28 | victim `6A` | f473, f93 |
| `POPUP` | **+39** | victim `50` ShowDamage → `$A3D1`=1 | f484, f1661, f506, f105 (B/C = +38) |
| `SWING_END` | +40 | `$A124`←0, cursor → `36` | f485, f106 |
| `STRIKE_SUP` | +41 | `1E 2A` → CE/D776 (`76 03 19` aim, `08` speed) | f486, f107 |
| `NUDGE` | +43 | `A8 01` (move-forward 1 → 0 px net) | f488, f109 |
| `POSE_RECOV` | **+45** | RECOVER window entry 1 published | f490 (`8B`), f111 (`8F`), f135 (`8F`, +44) |
| `RET_ARM` | +47..48 | `98 02 19` first visited (parks) | f492→493, f113→114, f137→138 |
| `RET_STEP0` | **+49** | first return travel frame, +4 px/f | f494, f115 (B: f139 = +48) |
| `RET_END` | +48+R | slide completes, cursor advances | f497(+52), f122(+56), f154(+63) |
| `POSE_SETTLE` | RET_END+2 | RECOVER entry 0 | f499, f124 |
| `POSE_IDLE` | RET_END+19 | IDLE entry 0 (the bob resumes) | f516, f141 |
| `SPAN_END` | **+102** | `$A013`←0 (`= POPUP + 58 + 5`) | f547, f1724, f569, f168 (B/C = +101) |

Identity to keep in the code: **`span = 125 + W`** — 125 is exactly the ZERO-WALK span, measured directly
(GATO fight-2 #1: dispatch f444, arrival f467 with `$A15B` 1→0 in one frame, span end f569 = **125**). So
the existing `CT_SPAN_PC_NORMAL = 125` is not wrong — it is the **W = 0** instance of the real law, and its
`battle_span_emergence.md` §4.3 provenance (ledger f643→768) was an already-adjacent attack.

---

## 5 — WHAT OUR ENGINE DOES INSTEAD (and why the user sees it)

Repro: `CT_BATTLE_MASHA=1 CT_BATTLE_ROUTETRACE=1 build/ct_field.exe --assets ct_assets.dat --scene 19 \
--battletest saves/battle_re/forest_pre.raw.wram 2>&1 | grep pcroute`

| stage | ROM (arrival-anchored) | our engine | src |
|---|---|---|---|
| walk arm | dispatch **+22** | **rel 1** (`battlePcAtkArm` at dispatch, ticked from rel 0) | `battle.c:2366`, `battle.c:3313`, `battle.c:3483` |
| arrival (forest, 2 segments) | dispatch **+39** | **rel 18** | route trace `pcroute f213 rel=18 … walk=0 arr=1` |
| pose ladder | RUN loop for the WHOLE walk; STRIKE window from **arrival+3** | `idx = rel + CT_CRONO_ATK_IDX0(7)` into `ctCronoAttackTraj` → pose0 rel 0-5, pose1 rel 6-10, **pose2 (win 0xC4 = STRIKE) rel 11-25**, pose3 rel 26-52, pose4 rel 53-61, pose5 rel 62-78, idle rel 79+ | `battle.c:2436-2440` (`battleSkeleton`), consts `battle.c:1971-1975`; window map `field.c:3371-3378` |
| victim flash / knock / hurt-pose | **arrival+19** | `strikeRel = CT_CRONO_STRIKE = 29` from dispatch | `battle.c:1972`, `battle.c:3476-3478`, `3486-3551` |
| damage popup | **arrival+39** | `spanRel − (58+5)` = rel 62 | `battle.c:3595` |
| home slide | first step **arrival+49** | `rel >= CT_PC_ATK_RETURN (60)` | `battle.c:2331`, `battle.c:2410-2418` |
| span | **125 + W** | fixed `CT_SPAN_PC_NORMAL = 125` | `battle.c:2025`, `battle.c:2673` |

**The defect, numerically (forest, W = 16):** our walk occupies rel 3‥18, but the pose table hands
`fxAtkPose = 2` (window `0xC4` = the STRIKE-raise) at **rel 11** — *seven frames before Crono arrives* —
and `fxAtkPose = 3` (the full STRIKE) at rel 26. `field.c:3376` then picks the STRIKE facing (`strkFac`) for
those poses, so from rel 11 the sprite is a sword-raise pose **sliding sideways at 2 px/f**. That is
precisely *"starts the attack animation while he still has not finished moving"*.

**Why GATO hid it:** there the walk is one segment (route trace `gato_t0`: arm rel 1, arrival **rel 10**),
so pose0+pose1 (rel 0‥10) happen to cover the whole walk and pose2 lands 1 frame after arrival. The bug is
invisible at 1 segment and grows linearly with distance — 4 segments (bat3 B) would show 21 frames of
strike-pose sliding.

**Root cause in one line:** `src/battle.c:2436` — `int idx = rel + CT_CRONO_ATK_IDX0;` — the pose ladder
(and with it every downstream constant `CT_CRONO_STRIKE`, `CT_PC_ATK_RETURN`, `CT_SPAN_PC_NORMAL`) is
indexed from **dispatch**, while the ROM's script thread is *parked* on op `18` (`C1/55FC → C1/560C`,
advance 0) for the entire walk and only starts the swing at **arrival**.
The one-segment GATO capture that produced `ctCronoAttackTraj` (`CT_CRONO_ATK_F0 = 440`, "A1 start f447")
was in fact **arrival-relative data mislabelled as dispatch-relative**: the oracle's f447 in that same fight
is `arrival(f445) + 2` — the `03 31` swing arm, not the run start.

---

## 6 — IMPLEMENTATION SPEC (mandate-safe; every constant cited)

No scene gate, no attacker-identity gate: this is the generic CE base-script law for any PC physical whose
role-0 track carries op `18` (Crono's melee today; the same block is loaded for every melee PC — the base
script CE/0380 is char-independent and was observed identically in two arenas).

### 6.1 New host state (`src/battle.h`, `CtBattleHost`)

```
int pcAtkArrRel;      // rel of the ARRIVAL frame; -1 until the walker stops  (the single new anchor)
int pcAtkRetEndRel;   // rel of the frame the op-98 slide completes;    -1 until then
int pcAtkRunCur;      // RUN-window cursor (E4 ptrA entry index)   [$A13A]
int pcAtkRunDwell;    // frames left on the current RUN entry       [$A12F]
int fxAtkWin, fxAtkEnt;   // published pose = CtBattlePcWinPose(0, fxAtkWin, fxAtkEnt, facing)
```
`fxAtkPose` keeps its existing role as the "a swing is rendering" flag / facing-class selector; the
(window, entry) pair replaces the 6-value enum so the RUN window can play all six of its entries.

### 6.2 Constants (replace the dispatch-relative ones)

```c
/* battle_pc_attack_sequencing.md §4 — ALL relative to the ARRIVAL frame except the first three.  */
#define CT_PC_ACT_PREROLL     21  /* dispatch -> first CE-script op advance (5/5 captures; the C1/4310
                                     track-loader/sheet staging = the Stage-1 streamer boundary, §6 OPEN-1).
                                     Corroborated independently: the CRIT's leap starts rel 23 = PREROLL+2
                                     (CT_CRONO_CRIT_LEAP0, battle_facing_engine.md ADDENDUM-CRIT). */
#define CT_PC_ATK_ARM         22  /* dispatch -> `02 06` RUN + `18 03` arm  ($A15B=1)  [= PREROLL+1] */
#define CT_PC_ATK_TRACK       23  /* dispatch -> the C1/55E6 track/facing stamp; first step at +24       */
#define CT_PC_ATK_SWING_ARM    2  /* arrival  -> `03 31` play-once armed ($A124=1)                       */
#define CT_PC_ATK_SWING_P0     3  /* arrival  -> STRIKE window entry 0 published                          */
#define CT_PC_ATK_FLASH       19  /* arrival  -> victim `69 00` hurt-flash ON (CE/D428)                   */
#define CT_PC_ATK_POPUP       39  /* arrival  -> `50` ShowDamage ($A3D1=1)                                */
#define CT_PC_ATK_STRIKE_SUP  41  /* arrival  -> `1E 2A` (CE/D776)                                        */
#define CT_PC_ATK_POSE_RECOV  45  /* arrival  -> RECOVER window entry 1                                   */
#define CT_PC_ATK_RET_STEP0   49  /* arrival  -> first op-98 travel frame (armed +47, applies from +49)   */
#define CT_PC_ATK_SETTLE       2  /* retEnd   -> RECOVER window entry 0                                   */
#define CT_PC_ATK_IDLE        19  /* retEnd   -> IDLE window entry 0 (fxAtkPose = -1, the bob resumes)    */
#define CT_PC_ATK_SPANEND    102  /* arrival  -> $A013=0   ( == CT_PC_ATK_POPUP + CT_FX_POPUP_LIFE(58)
                                     + CT_ACT_TAIL_PC(5) — the derived universal span law, §3.1 )        */
/* window offsets: already in field.c — RUN 0x18, STRIKE 0xC4, RECOVER 0x38, IDLE 0x0C
   (battle_facing_engine.md §4; the entry lists + dwells are read from ROM, §6.4).                       */
```
`CT_CRONO_ATK_IDX0`, `CT_CRONO_STRIKE`, `CT_CRONO_WINDOW` and `CT_PC_ATK_RETURN` are **retired** for the
PC physical path (they stay only for the crit ladder / Marle / enemy paths that still use them).
`ctCronoAttackTraj`'s `poseId` column is retired with them (its `dx/dy` column was already retired in §11d).

### 6.3 `battlePcAtkArm` / `battlePcAtkTick` — delay the walk to `CT_PC_ATK_ARM`

1. `battlePcAtkArm` (`battle.c:2333`) keeps doing the BFS/backtrace, but sets `pcAtkMvActive = 1` **without**
   letting the walker step: add a gate in `battlePcAtkTick` (`battle.c:2370`)
   `if (rel < CT_PC_ATK_ARM) return;` and only call `battlePcWalkArm` on the frame `rel == CT_PC_ATK_ARM`
   (mirrors `C1/54BE`: the arm frame parks, the controller runs the NEXT frame → `rel == CT_PC_ATK_TRACK`
   picks the track, the stepper starts at `rel == CT_PC_ATK_TRACK+1`). The existing walker already produces
   the ROM's `arm → track → 8·n steps` shape (route traces reproduce the oracle step-for-step, §11d/§13b) —
   only its start offset moves.
   *Zero-walk case:* if the very first controller pass stops (`battlePcWalkTick` returns 0 at
   `rel == CT_PC_ATK_TRACK`), `pcAtkArrRel = CT_PC_ATK_TRACK` → span 125, byte-identical to the oracle's
   measured zero-walk instance.
2. On the frame the walker stops, record `h->pcAtkArrRel = rel` (in addition to the existing
   `pcAtkArrX/Y` + the `72 0D` face-victim stamp, which already happens there — that stamp is canonical and
   lands at arrival+1 in the ROM; keeping it on the arrival frame is within the ±1 jitter).
3. Return trigger: replace `rel >= CT_PC_ATK_RETURN` with
   `rel >= h->pcAtkArrRel + CT_PC_ATK_RET_STEP0` (arrival-relative). Keep the existing accumulator arm
   (`dom/4` steps, seed 0x80) — validated byte-exact in §11c. When `pcAtkRetCount` reaches 0 and the
   `1B 19` snap fires, record `h->pcAtkRetEndRel = rel`.
   (The crit branch keeps `CT_CRONO_CRIT_RETURN`; the crit uses `98 00 03`, not op-18 — out of scope, OPEN-5.)

### 6.4 `battleSkeleton` — the arrival-anchored pose state machine

Replace the `ctCronoAttackTraj[rel+7].poseId` lookup with (pseudo-code; `rel` = `frames − actStart`,
`arr` = `h->pcAtkArrRel`, `ret` = `h->pcAtkRetEndRel`):

```
if (h->actAttacker != 0 /* PC physical */) return;
h->fxAtkPal = 5;  h->fxAtkWin = -1;
if (arr < 0) {                                    /* still walking (or pre-roll) */
    if (rel < CT_PC_ATK_ARM + 1) { h->fxAtkPose = -1; return; }   /* pre-roll: the IDLE bob still runs */
    /* RUN window, LOOPING (op `02 06` -> $A124 = 2, C1/42DB-42E7) */
    if (--h->pcAtkRunDwell <= 0) {
        h->pcAtkRunCur++;
        if (CtBattlePcWinPose(0, 0x18, h->pcAtkRunCur, apFac) < 0) h->pcAtkRunCur = 0;   /* 0xFF -> LOOP */
        h->pcAtkRunDwell = ctPcPoseDwell(0x18, h->pcAtkRunCur);   /* the RAW ptrB byte, C1:42EE */
    }
    h->fxAtkWin = 0x18; h->fxAtkEnt = h->pcAtkRunCur; h->fxAtkPose = 0 /* facing class = approach */;
    return;
}
int a = rel - arr;
if (a < CT_PC_ATK_SWING_P0) { keep the RUN entry (the loop keeps ticking for 2 more frames) }
else if (a < CT_PC_ATK_POSE_RECOV) {            /* STRIKE window, 3 entries, ROM dwells 15/10/10 */
    int e = (a < CT_PC_ATK_SWING_P0 + 15) ? 0 : (a < CT_PC_ATK_SWING_P0 + 25) ? 1 : 2;
    h->fxAtkWin = 0xC4; h->fxAtkEnt = e; h->fxAtkPose = 3 /* facing class = strike */;
} else if (ret < 0 || rel < ret + CT_PC_ATK_SETTLE) {
    h->fxAtkWin = 0x38; h->fxAtkEnt = 1; h->fxAtkPose = 4 /* facing class = return */;
} else if (rel < ret + CT_PC_ATK_IDLE) {
    h->fxAtkWin = 0x38; h->fxAtkEnt = 0; h->fxAtkPose = 5;
} else h->fxAtkPose = -1;                        /* IDLE window 0x0C -> battleCronoIdleRender resumes */
```
`ctPcPoseDwell(win, ent)` = one ROM byte: `E4/2800[charId*2] + (win + ent)` (**no** facing term — the
`C1/42C9` vs `C1/42F6` split). Reading it makes the 6 / 15 / 10 / 10 dwells ROM-sourced rather than pinned.
`field.c:3371-3378`'s `P[6]` table is replaced by the published `(fxAtkWin, fxAtkEnt)` plus the existing
facing-class pick (`fk` 0=`apFac` / 1=`strkFac` / 2=`retFac`) — `CtBattlePcWinPose` is already the exact
ROM address arithmetic, so no new ROM reader is needed beyond `ctPcPoseDwell`.

### 6.5 `battleAdvanceAction` — the strike / popup / span

```
strikeRel  = arr + CT_PC_ATK_FLASH        (PC physical with a walk; enemy paths keep CT_GATO_STRIKE)
popupRel   = arr + CT_PC_ATK_POPUP        (replaces the `spanRel − 63` back-computation for this path;
                                           it is the SAME law, just anchored where the ROM anchors it)
spanRel    = arr + CT_PC_ATK_SPANEND      (== 125 + W; assign h->actSpan on the ARRIVAL frame)
```
Everything the strike branch does today (hurt-flash `CT_FX_FLASH_DUR`=9 ✓ matches the measured 8-9-frame
`69 00`→`6A` window, enemy hurt-pose, knock, crit flash) keeps its current gating — only `strikeRel` moves.
`animRel` (the `fxKnockTarget` snap-back / `fxEnemyRearup` window) for a PC physical becomes
`arr + CT_PC_ATK_IDLE + (retEnd − arr)` i.e. `ret + CT_PC_ATK_IDLE`; until arrival is known it must not fire.
`battleActionSpan` still runs at dispatch (it must write `mem[$9877]` and resolve tech/item/ranged/crit
spans); for the **melee-with-walk** case it returns the provisional `CT_SPAN_PC_NORMAL` (125) and
`battlePcAtkTick` **overwrites `h->actSpan = pcAtkArrRel + CT_PC_ATK_SPANEND`** on the arrival frame.
(Equivalently: pre-simulate the walker at dispatch and set `125 + 8·segments` — the walker is deterministic.
Setting it at arrival is closer to the ROM, where the span is emergent, and is 1 line.)

### 6.6 Validation

- `CT_BATTLE_ROUTETRACE` + a new `CT_ANIMENG_PCTRACE` line must reproduce, for `bat3_lake`-equivalent
  distances, the §3.1 table: arm at rel 22, arrival at rel 23+W, RUN entries every 6 frames cycling the
  full 6-entry window, STRIKE entry 0 at arr+3, flash at arr+19, popup at arr+39, first return step at
  arr+49, span 125+W.
- `--vmdiff` is not applicable (battle is not the VM); the oracle check is a `--monitor` re-run of the four
  commands in §3 against our trace.

### 6.7 Can the GATO fight stay byte-identical? **No — and it must not.**

Our GATO battletest walks 1 segment (route trace `gato_t0`: arm rel 1, steps rel 3‥10, arrival rel 10). Under
the canonical law that fight becomes: arm rel 22, arrival rel 31, flash rel 50, popup rel 70, first return
step rel 80, **span 133**. The oracle's own GATO fight measures exactly that (§3.1 rows 1 and 3: dispatch
f414/f1591 → span end f547/f1724 = **133**), and measures **125 only when Crono is already adjacent**
(row 2: f444 → f569, `$A15B` 1→0 in a single frame). So today's 125-with-a-walk is simply wrong by the walk
length, and every pinned GATO render canary inside an action window — plus the `field-battleloop-gato`
victory frame, which shifts by +8 per Crono physical that walks a segment — legitimately moves. Per the
mandate this is a MECHANISM fix, not a re-pin excuse: the re-pinned values are the oracle's own.
The only GATO-fight behaviour that stays bit-identical is an already-adjacent second attack (W = 0 → 125).

---

## 7 — OPEN

1. **The 21-frame pre-roll's ROM source.** `$A013`←1 at `C1:40A9`, then `JSL CC/F156` + `JSR C1/4310`
   (the track-loader kick) — but the first cursor advance is 21 frames later, 5/5 PC-physical captures
   (and 25 frames for GATO's own attack, `battle_span_emergence.md` §4.1). It is the sprite-sheet/pose
   staging = the standing Stage-1 streamer boundary. Pinned as an OBSERVED constant (the copyFreeze
   precedent), independently corroborated by the crit leap starting at rel 23. **To close:** `--writer` on
   `$5DBD` across the pre-roll, or trace `C1/4310`'s DMA/queue drain.
2. **The ±1 jitter** on `FLASH/POPUP/RET_STEP0/SPANEND` (`+19/+39/+49/+102` vs `+19/+38/+48/+101` in bat3
   B and C). It is the PC anim-tick phase (`battle_ai_interp.md` §14: the 6.5 f/unit lattice, whose
   `C1/106E`-vs-`C1/10E3` per-frame dispatch is still unlocated). The spec uses the 4-of-6 majority values.
3. **The RUN window's LOOP is ROM-proven** (`C1/42DB-42E7`, mode 2 restarts the cursor) but **never observed
   wrapping** — the longest capture is bat3 B's 33-frame walk vs a 36-frame cycle (6 entries × 6 f). Needs a
   ≥5-segment walk (≥ 80 px) on some arena to see the wrap directly.
4. **op `26 ee vv`** (`C1/577D`, len 3) drives the post-strike RECOVER pose changes (`26 0E 02` at arr+46,
   `26 0E 01` at retEnd+2). Its operand semantics are undecoded; the spec uses the measured pose frames.
5. **The CRIT path** (`$9877`=0x01, `CD/6E4A`) uses `98 00 03` (a trig leap), not op-18, and its
   `CT_CRONO_CRIT_*` constants stay dispatch-anchored. Its leap is 4 px/f so its span should be
   distance-dependent too — only the `bat3crit` (single-distance) capture exists. Not re-derived here.
6. **Marle / other PCs.** Marle plants (`battle_pc_approach_facing.md` §1.8) — no op-18, spans unaffected.
   Her CLOSE-melee variant `$9877`=0x03 (which today borrows `CT_SPAN_PC_NORMAL`) was not captured; if it
   walks, it inherits this law. The base script CE/0380 is char-independent, so Lucca/Robo/Frog/Ayla/Magus
   melees are expected to follow it — uncaptured.
7. **The PC-melee knock is ZERO by mechanism**: the victim block CE/D428 carries no `A9`/`A8` move op (only
   `69/6A` palette + plays + `50`), and the attacker's `A8 01` moves the attacker 1 phase (0 px net). Our
   `animKnockDist` gate reads the **CD** script, which §11a proved never loads for a PC physical — so the
   gate is accidentally right for the wrong reason. Re-basing it on the CE blocks is a separate item.

---

## 8 — SCRATCHPAD / REPRO INDEX (not committed)

`$SCRATCH = %TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/e956ecca-…/scratchpad/lane_A`
- `ct_probe.exe` = `cp build/ct.exe` (never probe with `build/ct.exe` — build lock).
- States: `saves/battle_re/bat3_lake.state` (3× Hetake, 3 distances), `gato_press_action.state` (GATO;
  mash `['100:8,0:8']*40` = fight 1, `['100:8,0:40']*45` = fight 2 with a zero-walk AND an 8-frame-walk melee).
- Monitors used (all with `--diff`):
  `"5DBD:2,5DC9:2,5DED:2,5DFD:16,A013:1,A15B:1,1D0C:1,A3D1:1,5D9D:1"` (track cursors + pauses + the parks),
  `"9907:1,967B:1,968C:1,9689:1,A124:1,008E:1,96DB:1,A15B:1,A013:1,1D0C:1,A3D1:1"` (poses),
  `"1D0C:1,1D23:1,A15B:1,A517:1,A500:1,A013:1,9614:1"` (the walk + target cursor).
- ROM: `tools/dis.py 0x15409 0x40` / `0x15448 0x60` / `0x15490 0x35 1 0` / `0x154C1 0x50` / `0x155C0 0x50`
  / `0x15603 0x20` / `0x14D04 0x60` / `0x14D5C 0x2A` / `0x14E22 0x30` / `0x14F1A 0x3A` / `0x1423A 0x80`
  / `0x142B0 0x3A` / `0x142EC 0x40` / `0x161EB 0x18`.
  Tables: `C1/7A6B` (op JT), `CE/0000` (super JT), `CE/0380` (melee base), `CE/D4C3` / `CE/D776` / `CE/D428`
  (engage / strike / hurt blocks), `E4/2600`+`E4/2800` (Crono ptrA `0x3000` stride `0x128` / ptrB `0xA800`),
  `CC/F414` / `CC/F434` / `CC/F494` / `CC/F594` (the walker's forbid / facing / dx / dy tables).

---

## VERIFICATION (adversarial pass)

Independent re-run of every ROM citation (`tools/dis.py`) and every oracle number
(`$SCRATCH/verify_A/ct_probe.exe` = `cp build/ct.exe`), plus **three states the derivation never
touched** (`bat2_lake`, `bat4_lake`, `bat5_lake`). Nothing under `src/` was modified.

### V.1 — CONFIRMED (ROM, byte-for-byte)

Every disassembly quoted in §1/§2 is real and means what §1/§2 says:

| citation | verified |
|---|---|
| `C1/7A6B[op*2]` jump table | 01->4CF7 02->4D04 03->4E22 06->4EA2 16->5409 17->541B **18->542D** 1B->567D 1E->56CC 1F->56FB 20->5714 24->5756 26->577D 36->594B 50->5ADB 72->5FDE 7C->61EB 98->6299 — all 18 exact |
| `C1/5433` `LDA #02 / STA $8E` (op-18 length 2) | exact |
| `C1/5471-5476` `LDX $A010 / LDA $8E / STA $A4F0,X` | exact |
| `C1/54B9-54BE` `LDA #01 / STA $A15B,Y / JMP C1/560C` | exact |
| `C1/55FC` `BRA ->1560C`; `C1/55FE` `LDX $80 / STZ $A15B,X`; `C1/5603` `LDX $A010 / LDA $A4F0,X / JMP C1/75BB`; `C1/560C` `LDA #00 / JMP C1/75BB` | exact |
| `C1/75BB` `REP #21 / ADC $E6 / STA $E6` (the REP clears carry) = `$E6 += A` | exact — **A=0 really is a park** |
| stop-probe entry `C1/5555 CMP #FF / BNE / JMP C1/55FE` (+ 3 more `#FF` exits at 5569/5581/55B7) | exact — the walker stops on the `FF` path-grid marker |
| `C1/4D04` op-02 `LDA #02 / STA $8E` -> `C1/4D61 STA $A124,Y`; `C1/4D6F LDA $8E / CMP #01 / BEQ RTS` | exact |
| `C1/4E32-4E4E` op-03 first-visit arm / re-visit `LDA $A124,X / BNE RTS` / `LDA #02 / JMP C1/75BB` | exact |
| `C1/42C9 ADC $A1FE,Y` (ptrB, **no** `$82`) vs `C1/42F6 ADC $82 / CLC / ADC $A1E8,Y` | exact — the facing split is real |
| `C1/42D8-42EA` `LDA $A124,X / CMP #02 / BEQ ->142E4` -> `TDC / STA $A13A,X / BRA ->142C4` | exact — mode-2 LOOP |
| `C1/42EE STA $A12F,X` (dwell = raw ptrB byte) | exact |
| `C1/56CC` op-1E `LDA [$E6],Y / ASL / TAX / ... / LDA $CE0000,X -> $E6 / $E8=00CE / INC $A00F` | exact — `1E ss` = CALL `CE/0000[ss*2]` |
| `C1/4F1A` op-07 `$98BF,X->$A324,X`, `$973F,X->$A2FE,X` | exact |
| `CE/0000[00/0A/2A]` -> **D428 / D4C3 / D776** | exact |
| `CE/0380` `90 80 90 00 / 8E 03 9E 03 A6 03 ...` | exact |
| `CE/038E` `1E 0A 36 7C 03 31 36 1E 2A 23 01 24 03 2E 01 00` (the `01` EndTech is at **CE/039C**) | exact |
| `CE/039E`, `CE/03A6`, `CE/D4C3`, `CE/D776`, `CE/D428` | all byte-identical to §1.4 |

`E4/2600[0]=0x3000`, `E4/2600[1]=0x34A0` -> **stride 0x128**, `E4/2800[0]=0xA800`. The §2.1 pose table is
byte-exact, and the **ptrA `FF` terminator lands on the same index as the ptrB `00` terminator** in all four
windows (so §6.4's `CtBattlePcWinPose(...) < 0` wrap is equivalent to the ROM's ptrB `BNE` test):

```
RUN     +0x18  f0: 20 41 21 22 42 23 FF | f1: 1C 3F 1D 1E 40 1F FF | f2: 24 3B 25 26 3C 27 FF | f3: 28 3D 29 2A 3E 2B FF   ptrB: 06 06 06 06 06 06 00
STRIKE  +0xC4  f0: 50 51 52 FF | f1: 55 56 57 FF | f2: 5A 5B 5C FF | f3: 5F 60 61 FF                                        ptrB: 0F 0A 0A 00
RECOVER +0x38  f0: 49 8D 8E 49 FF | f1: 47 8B 8C 47 FF | f2: 4D 91 92 4D FF | f3: 4B 8F 90 4B FF                            ptrB: 08 08 08 08 00
IDLE    +0x0C  f0: C6 4F C7 4F FF | f1: 43 2F 44 2F FF | f2: C8 33 C9 33 FF | f3: CA 37 CB 37 FF                            ptrB: 05 05 05 05 00
```

### V.2 — CONFIRMED (oracle, re-run verbatim)

The four §3 commands run exactly as written and print exactly the quoted frames. Re-verified end to end:
GATO f-1 (`['100:8,0:8']*40`), GATO f-2 #1 and #2 (`['100:8,0:40']*45`), bat3 A/B/C.

- **The park is real**: bat3 A `$5DBD=D4C6` frozen f49..f65 while `$1D0C` A0->82 (2 px/f), advancing to
  `D4C8` on f66 with `$A15B` 1->0. Third park (`98 02 19`, `$5DBD=D781`) frozen f113..f121 while
  `$1D0C` 80->A0 at 4 px/f.
- **`W = 8 x segments` is real**: bat3 C `$A500` FF(f59) -> 07(f60) -> 00(f67) -> re-probe f68 = stop.
  One segment = 8 movement frames. bat3 A shows two reloads (f50 07..f57 00, f58 07..f65 00) = W 16.
- **FLASH = arrival + 19** — now **8/8** captures (adding bat2 f86 / arr f67, bat4 f76 / arr f57).
- **`CT_FX_FLASH_DUR` 9** — `69 00` -> `6A` = 8-9 f in every capture.
- **`POPUP_LIFE` 58 and `TAIL_PC` 5** — `$A3D1` 1->0 = 58 f, span end = +5, **8/8** (incl. bat2
  110->168->173, bat4 101->159->164). `spanEnd = popup + 58 + 5` is the one span law that survives
  every state.
- **Pose IDs carry the facing, dwells do not** — bat3 B publishes `55 56 57` (facing 1) at arr+3/+18/+28,
  spaced by the only dwell row `0F 0A 0A` = 15/10. Reproduced independently.
- **RUN plays 6-frame entries from arm+1** — bat3 B `24 3B 25 26 3C 27` at f59/65/71/77/83/89 (facing 2);
  GATO `20`@f437 `41`@f443 (facing 0); bat3 A `24`@f50 `3B`@f56 `25`@f62. Never observed wrapping
  (OPEN-3 stands).
- **Our engine's defect reproduces**: `CT_BATTLE_MASHA=1 CT_BATTLE_ROUTETRACE=1 build/ct_field.exe --assets
  ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram` prints
  `pcroute f196 rel=1 ... walk=1` ... `pcroute f213 rel=18 live=(128,71) walk=0 ... arr=1`; the extracted
  `ctCronoAttackTraj` poseId ladder is exactly `pose0 rel 0..5 / pose1 6..10 / pose2 11..25 / pose3 26..52 /
  pose4 53..61 / pose5 62..78 / -1 79+`, and `field.c` `P[2] = {0xC4, 0, 1}`. **7 frames of STRIKE pose while
  still sliding — confirmed.** Our arm/track/step shape (rel 1 / 2 / 3..18) is the ROM's shape offset by 21.
- Every cited `src/` line number is right except `battlePcAtkArm(h)`, which is **battle.c:3312**, not 3313.

### V.3 — REFUTED

**R1 — `CT_PC_ATK_POPUP = 39` is NOT invariant; it is VICTIM-dependent.**
Two untouched states, same base script (`$5DBD=038E`, `$5DC9=039E`), same selector `$9877 = 0x00`, same
attacker (Crono — the published poses are his `50 51 52` / `8B 47` / `C6`; bat4's party word
`$2980 = 00 80 80` is Crono solo):

```
ct_probe --monitor "A013:1,A15B:1,A3D1:1,5DBD:2,5DC9:2,5DFD:16" \
         --from saves/battle_re/bat4_lake.state --input "100:6,0:6,100:6,0:260" --diff
  f17 A013=1 5DBD=8E03 | f39 D4C4 | f40 D4C6 A15B=1 | f57 D4C8 A15B=0   (arrival, W=16)
  f75 5DC9=D42A(+18) f76 D42E(+19 FLASH) f84 D433(+27 `6A`) f98 D435(+41) f100 D437(+43)
  f101 A3D1=1  ->  popup = arrival + 44        f159 A3D1=0        f164 A013=0
```
```
... --from saves/battle_re/bat2_lake.state --input "100:6,0:6,100:6,0:300" --diff
  f20 dispatch | f41 D4C4 | f42 arm | f67 arrival (W=24) | f94 5DC9=D433(+27) | f107 D435(+40) |
  f109 D437(+42) | f110 A3D1=1 -> popup = arrival + 43 | f168 A3D1=0 | f173 A013=0
```
The variance is entirely the victim block's `03 05` PLAY-ONCE at `CE/D433`, whose length is the **victim's
own** pose-window play: `D433 -> D435` = 8 f (GATO), 9 f (Hetake), **13 f (bat2 victim), 14 f (bat4
victim)**. Everything before it is fixed (`D433` at arr+27/28 in all 8 captures) and everything after it is
fixed (`+2` for `06 03`, `+1` for the `50`). The real law is
**`popup = arrival + 30 + play05(victim)`**; `39` is simply the `play05 = 9` instance.

**R2 — `CT_PC_ATK_SPANEND = 102` and the identity `span = 125 + W` are NOT invariant** (they follow R1):

| state | victim | dispatch | preroll | arrival | W | popup | span end | span | §4 predicts |
|---|---|---|---|---|---|---|---|---|---|
| GATO f-1 | GATO | 414 | 21 | 445 (+31) | 8 | arr+39 | arr+102 | 133 | 133 ok |
| GATO f-2 #1 | GATO | 444 | 21 | 467 (+23) | 0 | arr+39 | arr+102 | 125 | 125 ok |
| GATO f-2 #2 | GATO | 1591 | 21 | 1622 (+31) | 8 | arr+39 | arr+102 | 133 | 133 ok |
| bat3 A | Hetake0 | 27 | 21 | 66 (+39) | 16 | arr+39 | arr+102 | 141 | 141 ok |
| bat3 B | Hetake1 | 36 | 21 | 91 (+55) | 32 | arr+38 | arr+101 | 156 | 156 ok |
| bat3 C | Hetake2 | 36 | **22** | 68 (+32) | 8 | arr+38 | arr+101 | 133 | 133 ok |
| **bat2_lake** | — | 20 | 21 | 67 (+47) | 24 | **arr+43** | **arr+106** | **153** | 149 WRONG |
| **bat4_lake** | — | 17 | **22** | 57 (+40) | 16 | **arr+44** | **arr+107** | **147** | 141 WRONG |

`span = 125 + W + (preroll − 21) + (play05 − 9)` fits **8/8** exactly. `span = 125 + W` fits only the two
enemy species the derivation sampled. "In all six captures" is literally true and materially misleading:
all six are Hetake or GATO.

**R3 — `arrival = dispatch + 23 + W` is off by one whenever the pre-roll is 22** (bat3 C: 36+23+8 = 67,
measured 68; bat4: 17+23+16 = 56, measured 57). The jitter-free form is
**`arrival = armFrame + 1 + 8*segments`**, which holds 8/8 with zero exceptions. The pre-roll is 21 in 6/8
and 22 in 2/8, so the proposed comment `CT_PC_ACT_PREROLL 21 /* 5/5 captures */` states a falsehood; it is
6/8, and bat3 C (already in §4's own evidence column as "36->58(+1)") is a counter-example inside the
dossier's own data set.

### V.4 — corrections the implementer must apply before writing code

1. Anchor the walk on the **arm frame**, not on dispatch: `arrival = armRel + 1 + 8*segments`. Do not
   encode `dispatch + 23 + W`.
2. Do **not** write `CT_PC_ATK_POPUP 39` / `CT_PC_ATK_SPANEND 102` as universal laws. They are the
   `play05 = 9` (Hetake/GATO) instance. Either derive `play05` from the victim's own ptrB window (the same
   `E4/2800` read `ctPcPoseDwell` already needs, applied to the victim's animId) or keep them explicitly
   pinned per victim with the ROM law written down: `popup = arr + 30 + play05`,
   `spanEnd = popup + 58 + 5`. `spanEnd = popup + 58 + 5` IS universal (8/8) and should be the code's
   actual span expression.
3. `CT_PC_ACT_PREROLL 21` — keep as an OBSERVED constant but comment it **6/8, 22 in bat3 C and
   bat4_lake**.
4. §6.4's RUN branch must init the cursor the way `C1/4D64-4D69` does (`$A12F = 0`, `$A13A = 0xFF`) so the
   first tick publishes entry 0 at arm+1: `pcAtkRunCur = -1`, `pcAtkRunDwell = 0` on the arm frame.
5. §6.4's `a < SWING_P0` tail must use the **strike** facing, not `apFac`: the `72 0D` stamps `$96DB` at
   arrival+1 and the anim engine re-latches at arrival+2, where bat3 A publishes `22` = RUN entry 3 of
   **facing 0** while the walk facing was 2.
6. `battlePcAtkArm(h)` is at **src/battle.c:3312** (§5's "3313" is off by one).
7. §6's "the base script CE/0380 is char-independent" is not established by any capture — every capture is
   Crono. It is arena-independent (5 arenas) and victim-independent (4 victims). State it that way.


---

# IMPLEMENTATION ADDENDUM (session 24) — SHIPPED

`src/battle.h` gained the ARRIVAL ANCHOR (`pcAtkAnchored` / `pcAtkArrRel` / `pcAtkRetEndRel` /
`pcAtkRunCur` / `pcAtkRunDwell`) and the published pose pair (`fxAtkWin`, `fxAtkEnt`); `fxAtkPose` is now
only the "a swing renders" flag + the facing class.

* **`battlePcAtkArm`** no longer arms the walker at dispatch. **`battlePcAtkTick`** holds until
  `rel == CT_PC_ATK_ARM (22)`, arms the BFS+backtrace there (§1.2's park frame), lets the segment
  controller pick the track at rel 23, and latches `pcAtkArrRel` on the stop probe.
* **`battleSkeleton`** replaces the fixed `ctCronoAttackTraj[rel+7].poseId` ladder with the state machine of
  §6.4: pre-roll idle → the RUN window **looping** on its own ptrB dwells for the whole walk → STRIKE window
  from `arrival+3` at dwells 15/10/10 → RECOVER 1 → RECOVER 0 → idle. The dwells are READ from ROM
  (`CtBattlePcWinDwell`, new: `E4:ptrB[cid] + winOff + entry`, no facing term — C1/42C9 vs C1/42F6),
  ROM-verified as RUN 6,6,6,6,6,6,0 / STRIKE 15,10,10,0 / RECOVER 8,8,8,8,0.
* **`battleAdvanceAction`** anchors the victim flash (`arrival+19`), the span (`arrival+102`) and the
  knock/idle window on the arrival; while the arrival is unknown NOTHING downstream may fire.
* `battleActionSpan` returns the provisional 125 and `battlePcAtkTick` overwrites `h->actSpan` on the
  arrival frame — the span EMERGES from the walk (§6.5).

**Validated** on the forest (2 segments): arm rel 22, track rel 23, steps rel 24-39, **ARRIVAL rel 39**
(= `arm + 1 + 8*segments`, the verifier's corrected form), first return step rel 88 (`arr+49`), span
**141 = 125 + 16**. GATO (1 segment) becomes span 133 — the oracle's own measured value.

**Corrections from the adversarial pass that were applied:** the arrival is latched from the walker, never
computed as `dispatch + 23 + W`; the RUN cursor is seeded `-1 / dwell 0` (C1/4D64-4D69); `CT_PC_ACT_PREROLL`
is documented 6/8 not 5/5; and `CT_PC_ATK_SPANEND 102` carries the victim-dependence of `play05` in its
comment (the universal law is `popup + 58 + 5`, `popup = arrival + 30 + play05(victim)`; 8/9/13/14 measured
across four victims — reading it from the victim's pose script stays OPEN, the enemy ptrA/ptrB indexing is
the standing `battle_animeng_script.md` boundary, so the constant is the play05=9 instance).

**Re-pins (all oracle-explained, every shot verified visually first):** `field-battleloop-gato` f2402→f2418
(+8 per walking Crono physical), `-forest` f1237→f1261, `field-battle-miss-forced` span 125→**133**,
`field-battle-status-inflict` +8, `field-battle-forest-return` +24 with the field fnv **identical**, and the
swing/flurry/dissolve shot canaries. Three shot checks were additionally **RE-ANCHORED**: their
`CT_BATTLESHOT_AT` had drifted off the moment their own notes describe (victory-hold sat mid-fight,
lastkill-dissolve sat before the kill, ambush-dissolve ~200f past the dither) — they now shoot
victory+90 / kill+18 / kill+18 and were visually confirmed to show the held victory pose, the fight-ending
Hetake dithering, and the second Beetle dithering.
