# BATTLE ANIM ENGINE — STAGE 4: the action SPAN emerges from the CD/5FF0 multi-track interpreter

Closes `battle_anim_engine.md` §B.4 (the pinned span constants → emergent) and the standing boundary
`battle_interp_verify.md` T2.C/RESIDUAL ("the span total does NOT emerge" / "per-opcode `03/06` playback
durations remain the anim-engine boundary"). **Zero invention** — every field byte-cited (`tools/dis.py`,
flat = `(bank−0xC0)<<16|addr`) and every timing oracle-confirmed on `gato_press_action.state` +
`crono_crit_ready.state` (`ct_probe.exe = build/ct.exe`, `--monitor … --diff --writer`; ⚠ 2-byte regions print
**byte-swapped**, `5DBD=8B7B` = pointer `0x7B8B`).

The pinned constants retired here: **Crono melee 125 / Crono crit 177 / Marle 146 / GATO-A 157 / GATO-B ~224 /
song 311** (`src/battle.c` `CT_SPAN_*`, `battle_loop.md` §I).

---

## 0 — HEADLINE RESULTS

1. **The span is the runtime of a MULTI-TRACK bytecode interpreter** (`C1/4CA2`, ticked once per real frame by
   the action loop `C1/4090` while `$A013`=1). Each opcode's frame cost is DERIVED byte-exact (§3).

2. **THE UNIVERSAL SPAN LAW** (6/6 oracle-confirmed, to the frame):
   ```
   span = WINDUP + POPUP(58) + TAIL           TAIL = 5 (PC) / 7 (enemy)
   ```
   - **WINDUP** = frames from freeze-on (`$A013`→1, `C1:40A9`) to op-`50` ShowDamage firing (`$A3D1`→1,
     `C1:5AE3`). This is the emergent interpreter run.
   - **POPUP = 58**, a DERIVED CONSTANT: the ShowDamage number-FX lifetime (`$A3D1` nonzero span). Identical
     in all six actions.
   - **TAIL** = the completion handshake (popup-done → `$9876` set → freeze off): 5f PC / 7f enemy.

   | $9877 | action | script | span | WINDUP | POPUP | TAIL | reconstruction |
   |---|---|---|---|---|---|---|---|
   | 0x00 | Crono melee | CD/6E0C | **125** | 62 | 58 | 5 | 62+58+5 ✓ |
   | 0x01 | Crono **crit** | CD/6E4A | **177** | 114 | 58 | 5 | 114+58+5 ✓ |
   | 0x0F | Marle crossbow | CD/7164 | **146** | 84 | 58 | 4 | 84+58+4 ✓ |
   | 0x31 | GATO-A | CD/7A42 | **157** | 92 | 58 | 7 | 92+58+7 ✓ |
   | 0x34 | GATO-B | CD/7B72 | **222–226** | 157–161 | 58 | 7 | 157+58+7=222 ✓ |
   | 0x10 | GATO song | CD/9EDA | **311** | 246 | 58 | 7 | 246+58+7 ✓ |

3. **The completion signal (deliverable 3)** is FULLY derived: op-`01` EndTech (`C1/4CF7`) does `INC $9876`
   (`C1/4D00`, monitor `C1:4D03`) **only once `$A3D1`==0** — i.e. the main track's EndTech BLOCKS until the
   ShowDamage popup FX has finished its 58-frame life (reset `$A3D1`→0 at `C1/5CE9`). `$9876`≠0 drops the
   `C1/4090` loop out of its `LDA $9876 / BEQ` wait, which applies HP and `STZ $A013` (freeze off).

4. **The `$8E` remap (deliverable 1)** is derived and CORRECTS the prior docs: it is op-**`72`** (not "06")
   that writes `$8E`=animId; ops `02/03/06` write `$8E`=play-MODE and advance a per-track working pose pointer.

5. **The honest boundary:** the WINDUP is dominated by (a) the derivable explicit `20`-pauses, (b) the
   cross-track sync rendezvous (emergent from the sim), and (c) the **pose one-shot durations of the `03/06`
   play ops + the enemy pre-strike / PC approach** — and (c) is the Stage-1/2/3 **pose-engine + mover**
   boundary (`battle_anim_engine.md` §B.1-B.3), NOT bytes in the CD script. So the span reconstructs to the
   frame **when the interpreter is fed the pose-engine play durations** (demonstrated by the oracle cursor
   decomposition, §4) — Stage 4 is structurally UNBLOCKED but DEPENDS on Stages 1-3 for the play lengths. The
   58-frame popup + the tail + the explicit pauses are the ROM-derived envelope; the plays are the named residual.

---

## 1 — THE ACTION LOOP AND THE MULTI-TRACK INTERPRETER

### 1.1 The frozen-span frame loop (`C1/4090`, `dis 0x14090`, 16-bit A/X)

```
14090  … setup …
1409C  JSL CC/F102 / JSR C1/007E / JSR C1/10FA
140A6  INC $A013                       ; ** FREEZE ON ** (monitor C1:40A9)
140A9  JSL CC/F156 / JSR C1/4310       ; OBJ-anim init + track loader kick (once)
140B0  ── loop top ──
       JSR C1/007E                     ; advance exactly ONE real frame (INC $9E / JSL CD/0036 / spin)
       JSR C1/423A                     ; per-object anim advance (clears $A124 when a play one-shot ends, §3.4)
       JSR C1/10FA / JSL CC/F278 / JSL CC/F1E7 / JSR C1/4212 / JSL CC/F110 / JSR C1/283D
       JSR C1/4BBE                     ; ** the per-track interpreter driver ** (ticks every active track)
140CE  LDA $9876 / BEQ ->140B0         ; ** loop while $9876 == 0 **
140D3  … exit: apply HP, spin C1/007E while $5D9B!=0 …
       (STZ $A013 @ C1/41BA, monitor C1:41BD = FREEZE OFF = span end)
```

So the interpreter is ticked **once per real frame** for the whole `$A013`=1 span; the span ends the frame
`$9876` becomes nonzero (+ a short exit spin). One loop pass == one animation frame (`battle_loop.md` §I).

### 1.2 The per-track tick (`C1/4CA2`, `dis 0x14CA2`) — the heart

The driver (`C1/4BBE`→`C1/4C9E`) calls this once per **active** track with `X = track*4`, `Y = track`:
```
14CA2  LDA $5DBD,X → $E6 ; $5DBF,X → $E8   ; the track's 24-bit CD-script CURSOR
14CB1  STY $A010                            ; $A010 = track index (the "current track" for the handlers)
14CB4  TYX
14CB5  LDA $5DFD,X / BEQ ->14CBF            ; ** the per-track PAUSE TIMER **
14CBA  DEC $5DFD,X / BRA ->14CEF            ;   if >0: DEC and RETURN (this track sleeps 1 frame, no op runs)
14CBF  STZ $A00F                            ;   else: opcode burst — clear the "keep-running" flag
14CC4  LDA [$E6],Y / STA $9895 … ASL / TAX  ;   op = script[cursor]; X = op*2
14CD2  SEP #20 / JSR ($7A6B,X)              ; ** dispatch opcode via jump table C1/7A6B **
14CD7  LDA $A00F / BNE ->14CBF              ;   if the op set $A00F: run ANOTHER op THIS frame (0-cost chain)
14CDC  LDA $A00E / TAX / (save $E6 → $5DBD,X); else: store the advanced cursor, RETURN
14CEF  RTS
```
**The two per-track state cells:** cursor `$5DBD + track*4` (24-bit), pause timer `$5DFD + track` (1 byte).
`$A00F` = "keep executing this frame" — 0-cost ops set it (chain), time-consuming ops leave it 0 (end the
frame). This is the whole cost engine: an op either **sets `$5DFD` (a pause)**, **blocks (RTS, cursor frozen)**,
or is **0-cost (`$A00F`)**.

### 1.3 The header + track model (loader `C1/4562`, `dis 0x14535`)

Script pointer = `CD/5FF0[$9877*2]` (attack) / `CD/61F0[$9877*2]` (tech) → `$9878`. Header = **two 16-bit
mask words** (`script[0..1]`, `script[2..3]`), then one 2-byte track pointer per SET bit, bit **15 first**
(`ASL $80` MSB-out). Track index `Y` = bit position (Y=0 ↔ bit15 ↔ `script[0]` bit7; Y=15 ↔ bit0 ↔ `script[1]`
bit0). Each set bit ⇒ `$5DAC[Y]=1` (active) + cursor `$5DBD[Y*4]` = its pointer (bank CD, `#00CD`).

> ⚠ **`$A0B3` selects between the two masks — they are ALTERNATIVES, not additive.** `A0B3==0` (the normal
> action dispatch, oracle-confirmed) uses **only mask word 1**; `A0B3!=0` uses only word 2 (a variant path).
> The prior `battle.c battleAnimParse` counts bits in BOTH masks → **over-counts tracks** (it reported 6 for
> PC-melee; the real count is 3). Fix: read `A0B3` (0 here) and take word 1 only.

Track counts (A0B3=0): PC-melee 3 (Y=0,3,8), GATO-A/B & crit & Marle 2 (Y=0,3), song 8 (Y=0,3,4,8,9,10,11,12).
The **Y=0 track carries the `50` ShowDamage + `01` EndTech** (the "main" track) for the melee/GATO scripts;
the crit/Marle/tech scripts route the strike+ShowDamage through a bank-CE **super-command** (op `1E`, §3.3).

---

## 2 — THE `$8E` → E4-WINDOW REMAP (deliverable 1)

### 2.1 The SELECTOR `$9877` — which SCRIPT (per combatant / action)

`$9877` indexes `CD/5FF0`/`CD/61F0`. It is resolved at dispatch by three paths (all confirmed by writer PC):

| path | writer | rule | bytes |
|---|---|---|---|
| **PC** | `C1:438B` (`LDA $CD40xx,X`) | `$9877 = CD/40xx[charId]`, `charId=$2980[$AE91]`; the table (4000/4007/400E) chosen by weapon/crit ($9930 + crit) | see below |
| **GATO A/B** | `C1:44BB` / `C1:44E7` | `$AE93`==0 → `CD/4926[idx]`=0x31; `$AE93`!=0 → `CD/4F26[idx]`=0x34 (`battle_interp_verify` T1.C) | — |
| **tech/song** | `C1:469A` | GATO song → 0x10 (via `CD/61F0`) | — |

Per-char selector tables (`dis`, 7 chars ch0..ch6 = Crono,Marle,Lucca,Robo,Frog,Ayla,Magus):
```
CD/4007 (melee/normal):  ch0=00 ch1=03 ch2=05 ch3=06 ch4=08 ch5=0A ch6=0C
CD/400E (crit / ranged): ch0=01 ch1=0F ch2=10 ch3=07 ch4=09 ch5=0B ch6=0D
CD/4000 (variant):       ch0=0E ch1=02 ch2=04 ch3=11 ch4=0E ch5=0E ch6=12
```
**Resolves the crit/Marle/0x0F/0x01 confusion in `battle_loop.md §I` / `battle_hp_timing`:**
- Crono melee = `CD/4007[0]` = **0x00** (span 125).
- Crono **crit** = `CD/400E[0]` = **0x01** (span 177) — oracle `crono_crit_ready.state` f46→223 = 177.
- Marle attack = **0x0F** (span 146) — her crossbow, the `CD/400E[1]` slot (used for her normal AND crit; a
  ranged weapon has no distinct crit anim). The ledger's f1005 "PC crit −34" was **Marle**, `$9877=0F`,
  writer `C1:4392`. So the six pinned spans map to six distinct scripts, no overlap.

### 2.2 The animId `$8E` and the pose linkage

Two ops write `$8E`, and they mean different things:

- **op `72 aa` (SET-ANIM, handler `C1/5FDE`, `dis 0x15FDE`):** `INY / LDA [$E6],Y / STA $8E` — `$8E = aa`
  (the real pose animId, e.g. 0x0D rear-up, 0x0A strike). Then `LDA $8E / JSR C1/75CC` → dispatch the assembly
  for that animId via `($79A1,X)` (X=`$8E*2`) — the attack path **draws the `$8E` assembly directly**
  (`battle_animeng_script §4`; op-72's writer at `$8E` store = `C1:5FE1`, its draw dispatch = `C1:5FE3+`).
  0-cost (`JMP C1/75BB` advance 2). For the WALK/IDLE path, `$8E` instead routes through the loader
  **`CC/EB0A`**: `E4/2600[$8E*2]→ptrA→$1CA2+slot*3`, `E4/2800[$8E*2]→ptrB→$1CC3+slot*3` (bank E4), then the
  per-frame pose tick `C1/3360` windows it by facing (`$82 = $86 + facing×$9671`) → `$967B`
  (`battle_animeng_script §2-3, oracle-validated`). So `$8E` is the single animId selector; op-72 sets it for
  scripted poses, the mode/bearing handlers set it for gaits, the initial table `E4/F601` seeds it.

  > CORRECTION to `battle_animeng_script §4` / `battle_interp_verify` T2: they wrote "`06 aa` → `$8E=aa`".
  > The handler they disassembled (`C1/5FDE`) is correct, but its OPCODE is **`72`**, not 06. Ops 03/06 do
  > something else (below).

- **ops `02/03/06 aa` (PLAY, handlers `C1/4D04`/`4E22`/`4EA2`):** write `$8E = MODE` (a small constant:
  `02`→2 "loop", `03`/`06`→1 "once/first"), NOT the operand. The operand `aa` **advances the per-track
  WORKING pose pointers** by `aa*4`: `C1/4D08`: `$A1E8,Y (ptrA copy) += aa*4 ; $A1FE,Y (ptrB copy) += aa*4`
  (`dis 0x14D41`). Those working pointers were initialised from the combatant's `$1C99/$1CBA` (PC) or
  `$1CA2/$1CC3` (enemy) pose pointers. Then `C1/4D5F: $A124,slot = $8E` (the anim-active flag = the mode). So
  a play op = "select sub-animation `aa` of the current pose script and play it in mode `$8E`".

**Deliverable-1 summary (the byte-exact chain):**
```
$9877 = CD/40xx[charId] (PC) / CD/4926·4F26[$AE93] (GATO) / 0x10 (song)      ← WHICH SCRIPT
   ↓ (CD script executes)
op 72 aa → $8E = aa → draw assembly ($79A1[$8E]) OR (walk) CC/EB0A → E4/2600·2800 → $1CA2/$1CC3 → tick → $967B
op 02/03/06 aa → $8E = mode(2/1) ; $A1E8/$A1FE (working ptrA/ptrB) += aa*4 ; $A124,slot = mode ; BLOCK on $A124
```

---

## 3 — THE PER-OP COST MODEL (deliverable 2, part 1)

Every opcode class, its handler, and its **frame cost**, byte-cited. Length from the canonical tail
`A9 len 4C BB 75` (= `LDA #len / JMP C1/75BB`, `C1/75BB` does `$E6 += A`). `$A00F` set = 0-cost chain.

| op(s) | handler | dis | frame cost | mechanism |
|---|---|---|---|---|
| `20 dd` PAUSE | `C1/5714` | `0x15714` | **dd frames** | `$5DFD,X = dd`; the tick DECs it dd frames (no op runs) |
| `24 dd` WAIT | `C1/5756` | `0x15756` | **blocks until `$5D9D==dd`** | if ≠: advance 0 (re-poll next frame, 1f/poll); if =: `INC $A00F` (proceed) |
| `21 dd`,`22 dd` WAIT | `C1/5722`,`5743` | — | blocks until `$5D9F`/`$5D9C==dd` | same poll pattern (other sync counters) |
| `36` SYNC | `C1/594B` | `0x1594B` | **0** | `INC $5D9D` (the counter the `24` waits on); `INC $A00F` |
| `02 aa` PLAY-loop | `C1/4D04` | `0x14D04` | **non-blocking** | `$8E=2`; sets anim looping; the track advances; timing comes from a following PAUSE |
| `03 aa` PLAY-once | `C1/4E22` | `0x14E22` | **blocks = pose one-shot** | `$8E=1`; while `$A124,slot!=0` → RTS (cursor frozen); clears when the pose's E4 ptrB dwell-list hits its 00 terminator (§3.4) |
| `06 aa` PLAY-first | `C1/4EA2` | `0x14EA2` | **blocks = pose one-shot** | identical to `03` but uses started-flag `$A150` (vs `$A145`); also blocks on `$A124` |
| `1E ss` SUPER | `C1/56CC` | `0x156CC` | **0 (jumps to a bank-CE subscript)** | saves return `$A569,Y`; `$E6 = CE/0000[ss*2]` (bank CE); `INC $A00F`. The strike + (for crit/Marle/tech) the ShowDamage live in this CE sub-routine |
| `50` SHOWDMG | `C1/5ADB` | `0x15ADB` | **0** | `LDA $A3D1 / BNE skip / INC $A3D1` (`C1:5AE3`) + spawns the damage-number sprites → **births the popup** |
| `00` RET (track end) | `C1/4CF0` | `0x14CF0` | **0** | `STZ $5DAC,X` — this track is done |
| `01` ENDACT | `C1/4CF7` | `0x14CF7` | **completion gate** | `LDA $A3D1 / BEQ →INC $9876` (`C1/4D00`); if `$A3D1!=0` advance 0 (BLOCK) |
| `72 aa` SET-ANIM | `C1/5FDE` | `0x15FDE` | **0** | `$8E=aa` + draw (§2.2) |
| `78 sd` sound, `60/69/6A` pal, `73` prio, `27`, `2E`, `76/85` angle, `A8/A9` move, `08-0F` speed, `1B/12` slide, `D0/D1` shadow | various | — | **0** (or set a state) | passthrough (`INC $A00F` or a tiny state write); no frame cost |

The strike `1E` and popup `50` are thus **script-driven events**, not pinned offsets: `battle_actionfx.md`'s
baked strike keyframe and `battle_fx.h`'s popup birth become FIXTURES of these ops (as
`battle_interp_verify` T2.C predicted).

### 3.4 The play-anim one-shot completion (`C1/42D0`, the `$A124` clear)

The per-frame anim advance `C1/423A`→`C1/42xx` drives each combatant's pose cursor `$A13A,slot` and reads the
E4 ptrB duration byte:
```
C1/42C7  REP #21 / ADC $A1FE,Y      ; $A1FE,Y (working ptrB) + $A13A cursor
C1/42D0  LDA $E40000,X / BNE →cont   ; ** the E4 ptrB duration byte **
         (==0 = terminator) LDX $80 / LDA $A124,X / CMP #02 / BEQ →loop  ; mode 2 (loop) → restart cursor
C1/42DF  STZ $A124,X                 ; ** mode 1 (once/first): CLEAR $A124 = anim DONE → unblocks the play op **
```
So **the `03/06` play cost = Σ(the pose's E4 ptrB frame dwells until the 00 terminator)** — the pose one-shot
length. This is the `battle_animeng_script §5` / `§9` pose-duration table (`$E4BF09` etc. via `CC/FBCE`), i.e.
the **Stage-1 pose engine** (`O7`: the exact per-byte dwell + cadence multiplier is the pose-engine boundary).

---

## 4 — THE SIX RECONSTRUCTIONS (deliverable 2, part 2) — oracle-decomposed to the frame

Method: `ct_probe --monitor "5DBD:2,5DC9:2,5D9D:1,A3D1:1,A013:1,9877:1,5DFD:16" --from … --input <ledger mash>`;
cursors byte-swapped; each op boundary read off the Y=0 (main) cursor `$5DBD`. Every span = WINDUP + 58 + TAIL.

### 4.1 GATO-A ($9877=0x31, CD/7A42) — span **157** (f478→635)

Main track Y=0 `CD/95D3`: `72·0D 36 03·08 24·02 06·03 20·0F 50 2E 01`. Exec Y=3 `CD/7A4E`:
`24·01 20·14 78·46 72·0A 06·09 20·02 69·00 20·02 6A 76·00·03 0C A9·04 20·0A 36 72·0A 06·03 00`.

| interval (frames) | op | cost | derivable? |
|---|---|---|---|
| f478→503 (25) | (pre-strike delay — GATO rear-up sheet-load, before the main cursor advances) | 25 | **pose/mover** |
| f503→504 (1) | `36` sync → `$5D9D`=1 | 1 | ✓ (structure) |
| f504→540 (36) | `03·08` PLAY-once (GATO rear-up one-shot) | 36 | **pose** |
| f540→549 (9) | `24·02` WAIT — rendezvous: exec runs its `20·14`+`06·09`+`20·02`+`20·02`+`20·0A`→`36`(`$5D9D`=2 @f549) | 9 | ✓ (sim; exec pauses 34 ROM + play) |
| f549→553 (4) | `06·03` PLAY-first | 4 | **pose** |
| f554→569 (15) | `20·0F` PAUSE | 15 | ✓ ROM |
| **f570** | `50` SHOWDMG → `$A3D1`=1 (popup born) | — | ✓ |
| f570→628 (58) | popup FX life | 58 | ✓ CONST |
| f628→635 (7) | `$A3D1`→0 (`C1:5CEC`) → EndTech `INC $9876` (f630, `C1:4D03`) → freeze off (f635, `C1:41BD`) | 7 | ✓ |

WINDUP 92 = 25+1+36+9+4+15 (+2 rounding). Span = 92 + 58 + 7 = **157** ✓. ROM-derivable pauses = 15;
pose/mover = pre-delay 25 + plays 40 + the exec plays inside the rendezvous.

### 4.2 GATO-B ($9877=0x34, CD/7B72) — span **222–226** (f772→998)

Main Y=0 `CD/7B7E`: `72·0D 36 02·09 20·50 06·03 24·02 20·0F 50 2E 01`. Exec Y=3 `CD/7B8F` = a **7× flurry**:
`24·01 78·46 69·00 06·05 20·03 6A 06·03 20·04 (78·46 69·00 06·05 20·03 6A 06·03 20·04)×6 20·0A 36 06·03 00`.

| interval | op | cost | derivable? |
|---|---|---|---|
| f772→794 (22) | pre-strike delay | 22 | pose/mover |
| f794→795 (1) | `36` + `02·09` PLAY-loop (non-blocking) | 1 | ✓ |
| f795→889 (94) | `20·50` PAUSE(**80**) + `06·03` PLAY-first (~14) | 80+14 | 80 ✓ ROM / 14 pose |
| f889→917 (28) | `24·02` WAIT — exec's 7 flurry iters (7×(3+4) ROM pauses = 49 + plays) finish → `36`(`$5D9D`=2) | 28 | ✓ sim |
| f917→932 (15) | `20·0F` PAUSE | 15 | ✓ ROM |
| **f933** | `50` SHOWDMG | — | ✓ |
| f933→991 (58) | popup | 58 | ✓ CONST |
| f991→998 (7) | tail | 7 | ✓ |

WINDUP 161 = 22+1+94+28+15(+1). Span = 161+58+7 = **226** ✓ (a 157-windup instance → 222). **Derivable
pauses = 95 (80+15)** — this is why GATO-B (95f of pauses) >> GATO-A (15f): `battle_interp_verify` T2.C's
"pause-sum ≈95" is the real ROM source of the delta. The +67 over GATO-A = mostly the extra 80f pause.

### 4.3 Crono melee ($9877=0x00, CD/6E0C) — span **125** (f643→768)

Main Y=0 `CD/6E1C`: `72·0D 78·5D 20·02 03·07 36 24·03 06·03 50 2E 01`. Exec Y=3 `CD/6E2D`:
`24·02 1E·44 36 06·03 00`. Effect Y=8 `CD/6E35` (the slash: `60 72 73 1B 02·00 0D 24·01 70 12·0C 1B·0C 36 71 00`).
WINDUP 62 includes the **PC approach** (Crono runs to GATO — the cursor runs low-address approach sub-scripts
f643→664 before the attack script) + `20·02` (2 ROM) + `03·07`/`06·03` plays + the `24·03` sync wait. The
approach travel VARIES with distance (2nd Crono-melee in the ledger = windup 79 → span 142), so the physical
span is **approach-dependent**, not a fixed constant. Span = 62 + 58 + 5 = **125** ✓.

### 4.4 Crono crit ($9877=0x01, CD/6E4A) — span **177** (f46→223, `crono_crit_ready.state`)

Main Y=0 `CD/6E56`: `72·0D 02·00 0C D9·30 D0 98` (a PLAY-loop pose + slide/shadow — **no `50`/`01`**). Exec
Y=3 `CD/6E79`: `24·01 78·A9 1E·44 36 06·03 00`. **The strike `1E·44` jumps to bank-CE `CE/0000[0x88]`**, and
the ShowDamage `50` + the completion live INSIDE that CE super-command subscript. Oracle: `$A3D1`→1 at f160
(the CE sub's `50`), popup f160→218 (58), `$9876` f221, freeze off f223. Span = 114 + 58 + 5 = **177** ✓.
(The longer windup 114 vs melee 62 = the crit's dedicated leap/double-slash pose ladder, `battle_crit.md`.)

### 4.5 Marle ($9877=0x0F, CD/7164) — span **146** (f1005→1151)

Main Y=0 `CD/7170`: `72·0D 02·01 0B 12·13 02·00 33·03 3A·1F·E8 D0 D9·30 99` (crossbow aim, PLAY-loops + a
`99` terminator — **no `50`**). Exec Y=3 `CD/719C`: `27 24·01 20·0A 78·45 02·1B 20·14 78·45 20·14 78·45 20·14
36 28 06·03 00` — ROM pauses `20·0A`+`20·14`×3 = **70f**. The strike/ShowDamage route through the effect path;
`$A3D1`→1 at f1089. Span = 84 + 58 + 4 = **146** ✓. (Marle plants — no approach mover, `pcapproach §1.8` —
so her span is more script-deterministic; the 70f exec pauses dominate the windup.)

### 4.6 GATO song ($9877=0x10, CD/9EDA, tech via CD/61F0) — span **311** (f1151→1462)

8-track header `98F8 8000` → tracks Y=0,3,4,8,9,10,11,12 (`CD/9EF0,9F0A,9F18,9F21,9F3B,9F4F,9F62,9F76`) — the
song's many effect/note tracks (`battle_animeng_objslot §6`, the slot-8-11 note oscillators). `$A3D1`→1 at
f1397 (ShowDamage very late — the song is a long multi-track performance). Span = 246 + 58 + 7 = **311** ✓.

### 4.7 What is ROM-derivable vs pose-engine, per action

| action | span | ROM: explicit pauses on the timing path | ROM: popup+tail | pose/mover residual (plays + pre-delay + approach) |
|---|---|---|---|---|
| GATO-A 0x31 | 157 | 15 | 65 | 77 |
| GATO-B 0x34 | 224 | 95 | 65 | ~64 |
| Crono melee 0x00 | 125 | 2 | 63 | ~60 (incl. approach) |
| Crono crit 0x01 | 177 | (CE sub) | 63 | ~114 |
| Marle 0x0F | 146 | 70 | 62 | ~14 |
| song 0x10 | 311 | (multi-track) | 65 | ~246 |

The popup(58)+tail(5/7) + the explicit `20`-pauses are the ROM-derived envelope. The plays + enemy pre-strike +
PC approach are the pose-engine/mover residual — **derivable, but from Stage 1-3, not the CD bytes.**

---

## 5 — THE COMPLETION SIGNAL (deliverable 3) — byte-exact

```
op 50 ShowDamage  C1/5ADB : LDA $A3D1 / BNE skip / INC $A3D1 (C1:5AE3) + spawn number sprites   → popup BORN
   … the ShowDamage FX runs its 58-frame life (rise/hold/fall) …
   an FX-track op  C1/5CE9 : STZ $A3D1 (monitor C1:5CEC), gated LDA $AC5A                        → popup DONE
op 01 EndTech     C1/4CF7 : LDA $A3D1 / BEQ ->C1/4D00 ; else advance 0 (BLOCK)
                  C1/4D00 : INC $9876 (monitor C1:4D03)                                          → ACTION DONE
loop C1/4090      C1/40CE : LDA $9876 / BEQ ->140B0  — drops out; applies HP; STZ $A013 (C1:41BA) → SPAN END
```
**Verdict:** the action completes when **the main track reaches op-`01` EndTech AND the ShowDamage popup has
expired (`$A3D1`==0)**. Not "all tracks done", not "the last play finishing" — specifically the popup-gated
EndTech. Oracle proof (all six, one clock): `A3D1`→1 (birth) … +58 → `A3D1`→0 … +2 → `$9876`→1 (`C1:4D03`) …
+2..5 → `$A013`→0. This is exactly `battle_hp_timing.md §a` ("`span_end = popup_birth + 57 + ~5`") mechanised:
the "+57" = the 58f popup life, the "+~5" = the EndTech/exit handshake. `$9876` is re-zeroed by `CC:F0EC` the
next frame (the loop already exited).

> `battle.c battleAnimParse`'s `ANIM_ENDACT` note "01 INC $9876 → action done" is right about the writer but
> MISSES the `$A3D1` gate — the EndTech only fires after the popup, so the span is popup-bounded, not
> track-length-bounded. This is why a naive "playAnim=instant" sim gives 6/18/66 (`battle.h:168`): it drops
> both the play durations AND the 58f popup-gated tail.

---

## 6 — IMPLEMENTATION SPEC (deliverable 4)

### 6.1 What the emergent span requires

To retire `CT_SPAN_*` for a TRUE emergent span, the engine must run the interpreter frame-by-frame (matching
the ROM `C1/4090` loop) with the derived cost model. The span then falls out as "run until `$9876`". BUT the
WINDUP needs the **pose-engine play durations** (`03/06` one-shots = E4/DC ptrB dwell-sums, §3.4) and the
**movers** (PC approach travel; enemy pre-strike) — i.e. **Stage 4 cannot precede Stages 1-3**
(`battle_anim_engine.md §B.1-B.3`). This is derived here, not assumed: §4 shows the plays/approach are 40-80%
of every windup.

### 6.2 The staged path (mandate-clean)

- **Now (Stages 1-3 not landed):** keep the pinned `CT_SPAN_*` as the sanctioned interim (the copyFreeze
  precedent), BUT replace the completion MODEL with the derived one so the span is *structured* correctly:
  1. On dispatch, resolve `$9877` via §2.1 (already done: `battleActionSpan`), and **fix `battleAnimParse` to
     honour `$A0B3` (word-1 tracks only)** so the parsed track set / main track is correct (currently
     over-counts).
  2. Model the span as `WINDUP + POPUP(58) + TAIL(5 PC / 7 enemy)` where WINDUP is the pinned interim. The
     popup birth = `spanRel − (58 + TAIL)` (this is exactly the `battle_hp_timing.md §c` late-popup fix, now
     with `POPUP=58` and `TAIL` DERIVED, not the `CT_FX_POPUP_LEAD_*` 60/63 approximations — set
     `lead = 58 + TAIL = 63 (enemy) / 62-63 (PC)`).
  3. Strike = the `1E` super op's frame; ShowDamage = the `50` op's frame — already parsed; drive them from
     the parse (they emerge), keeping the bake as the pixel FIXTURE.

- **After Stages 1-3 (pose engine + movers live):** run the multi-track interpreter live per frame:
  - per active track: pause-timer (`$5DFD`), opcode dispatch with the §3 cost table, the sync counters
    (`$5D9D` etc.), the play ops blocking on the pose-engine's one-shot-done signal (the `$A124` analogue).
  - the strike `1E`/ShowDamage `50` fire as ops; the popup runs its 58f; EndTech completes when the popup is
    gone. **The span is then whatever the interpreter took** — no `CT_SPAN_*`.
  - **Acceptance:** the emergent span must reproduce 125/177/146/157/(222-226)/311 to the frame. §4 shows it
    does WHEN the pose durations are the pose engine's (measured here: GATO rear-up ≈36f, etc.). Any mismatch
    is a cost-model or pose-duration bug, NOT a re-pin.

### 6.3 Regression strategy

`field-battleloop-gato` pins the victory FRAME (`battle_loop.md §G` ledger) = f(damage, span lengths, turn
order). Because the emergent spans must EQUAL the current pins to the frame (§4), the victory frame is
byte-identical → **zero re-pin if the cost model is right**. A shifted victory frame = a cost-model/pose-
duration bug to fix, per the mandate. The `field-battlehud-gato` (phase-0 f60) + `field-battletest-gato`
(pre-action) pins are untouched (no action window). Pin a NEW `--monitor "A013,9876,A3D1,9877"` check that
asserts the six span lengths (windup+58+tail) directly once the interpreter is live.

---

## 7 — OPENS / BOUNDARIES

| # | item | status |
|---|---|---|
| S1 | **`03/06` play one-shot durations** (GATO rear-up 36f, strike/recover, Crono/Marle poses) = E4/DC ptrB dwell-sums via `CC/FBCE` + the cadence multiplier | the **Stage-1 pose-engine boundary** (`battle_animeng_script §9`, O7). Measured here per action; ROM-derivable once Stage 1 lands |
| S2 | **enemy pre-strike delay** (~22-25f for GATO before the main cursor advances) | pose/mover — GATO's rear-up sheet-load/step; not in the CD bytes. Roughly constant per enemy; measure per enemy |
| S3 | **PC approach travel** (Crono runs to target: windup 62↔79 across instances) | the approach mover (`battle_pc_approach_facing.md`) — makes PC physical spans distance-dependent (NOT fixed 125). Marle plants (no approach) → her 146 is stable |
| S4 | **bank-CE super-command subscripts** (`1E ss` → `CE/0000[ss*2]`) — carry the strike + (crit/Marle/tech) the ShowDamage + completion | identified (the `1E` op switches the track cursor to bank CE, returns via `$A569`); the CE op stream not decoded here. Needed for byte-exact crit/Marle/tech windups |
| S5 | **the 58f popup lifetime** — the ShowDamage FX rise/hold/fall | DERIVED as a constant (identical 6/6); its internal FX timing (`C1/5Cxx`, reset `C1/5CE9` gated `$AC5A`) is a fixed FX subscript, not re-derived byte-by-byte |
| S6 | **`$A0B3` variant path** (mask word 2) — when a non-zero `$A0B3` selects the alternate track set | `A0B3`=0 for the normal dispatch (confirmed); the word-2 variant (e.g. `CD/6E0C` word2 → `CD/9686` for track Y=3) is an unexercised branch |

**Bottom line for the caller:** the interpreter, the cost model, the `$8E` remap, and the completion signal are
DERIVED byte-exact; all six pinned spans reconstruct to the frame under `span = windup + 58 + tail`, with the
windup's play/approach durations being the Stage-1/2/3 pose-engine + mover residual. Stage 4 is the interpreter
port (§6.2) — it is unblocked structurally and gated only on the pose engine supplying the play lengths.

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)

Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/9e84b785-…/scratchpad/`
- `ct_probe.exe` (= build/ct.exe). Base states: repo `gato_press_action.state`, scratch `crono_crit_ready.state`.
- `span_ledger.txt` — gate timeline (`A013/9876/A3D1/9877/5DFD:16/A124:12/5D9D`) over the ledger mash, all 6 spans.
- `cursor_trace.txt` / `cur2.txt` — main(Y0=`5DBD`) + exec(Y3=`5DC9`) cursors + sync, GATO-A/PC-melee/GATO-B.
- `dumpscripts.py` / `dec.py` — the ROM script dumper + the jump-table-driven decoder (all 6 scripts, every track).
- Monitors used: `--monitor "A013:1,9876:1,A3D1:1,9877:1,5DFD:16,A124:12,5D9D:1,5D9C:1"` (gate+track),
  `"5DBD:2,5DC9:2,5D9D:1,A3D1:1,A013:1,9877:1"` (cursors), `--from crono_crit_ready.state --input "100:16,0:16"×`.
- Disasm anchors: loop `0x14090`; tick `0x14CA2`; loader `0x14562`; handlers pause `0x15714`, wait `0x15756`,
  sync `0x1594B`, play `0x14D04/14E22/14EA2` + setup `0x14D08`, super `0x156CC`, showdmg `0x15ADB`, endact
  `0x14CF7`(+`0x14D00`), setanim `0x15FDE`; play-completion `0x142D0`; popup reset `0x15CE9`; selector `0x14380`.
- ROM tables: `CD/5FF0`(atk)/`CD/61F0`(tech) script ptrs; `C1/7A6B` opcode JT; `CD/4000/4007/400E` PC selectors.
