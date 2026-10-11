# CYCLONE's CHOREOGRAPHY — Crono LEAVES his seat: the bank-CE tech script, op-by-op, frame-by-frame

Closes the user report *"crono's cyclone animation is incomplete, he is not even leaving his position.
only the effects are happening"*. Derives, from the ROM disasm + the LakeSnes oracle **only**, the FULL
Cyclone presentation for the attacker track: the CE base script that loads for a PC TECH, every op of role 0
(and the supers it calls), the exact travel law (slide → 8-leg diamond orbit → slide home → snap), the pose
ladder (bank-E4 windows + dwells), the facing stamps, and where the whirl FX sits in that timeline.

**Zero invention.** Every op is cited to its handler in the C1 dispatch table `C1/7A6B[op*2]` with `tools/dis.py`
output; every frame number is an oracle `--monitor` row. Probe = `ct_probe.exe` (= `build/ct.exe`).

> **Seeds (canonical injections, the tech-slice `$2837|=0x80` method).**
> `gato_cyclone.state = gato_press_action.state + $2837|=0x80` — btnscript
> `"100:8,0:372,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:340"`.
> `forest_cyclone.state = saves/battle_re/forest_holdleft_pre.state + $2837|=0x80` — btnscript
> `"40:60,0:275,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:400"` (the `battle_multitarget.md` recipe).
> A `20:2,0:12` inserted before the LAST `100:2` moves the target-select cursor (the role-3 experiment, §4.2).

---

## 0 — HEADLINE (the canonical mechanism, 6 lines)

1. A PC tech loads a **bank-CE base script** exactly like a PC physical: `$AE93 = techId` → `$9877 =
   CD/45A6[techId*7][0]` → `$9878 = CD/5EF0[$9877*2]`; for Cyclone that is **CE/0909**, and the loader stamps
   the per-role track cursors at `$5DBD + 4*role` (oracle f388: role0=`CE/0917`, role3=`CE/094D`,
   role4=`CE/0960`, role8=`CE/0970`). **The tech-base-script open of `battle_ai_interp.md` §13 is CLOSED for
   the TECH case: the base IS `CD/5EF0[$9877]`, the same table the tech slice already pinned.**
2. Role 0 (the attacker) = **CE/0917**, 54 bytes, decoded op-for-op below and verified against a live
   per-frame walk of `$5DBD` (the cursor advances match every op length).
3. **THE TRAVEL.** `98 02 03` = a **trig-slide onto resolver-mode 3 = role 3 = the CONFIRMED PRIMARY VICTIM's
   live position** (speed 4, step count = dominantDist/4). Not the op-18 BFS walk of a physical — the tech
   lands ON the victim, not one cell short.
4. **THE SPIN.** The super `1E 1F` (→ `CE/D61C`) sets speed 2 (`0B`), captures the STORED POINT
   `($5D9E,$5D9F) := resolver-mode 0 = Crono's OWN live position` (`33 00`), then runs **8 legs**: each leg is
   `49 1E ±0x1E` / `49 1F ±0x1E` (storedX/storedY += ±30, `C1/5A70`) followed by a **move-to-stored** op
   (`11`, `99 01`, `9B 01`) — a 30px, 15-frame, 2px/frame edge. The 8 legs trace `up → SE → SW → NW → NE →
   SE → SW → NW`, i.e. **two full 30px-radius diamond orbits around his arrival point**, with the facing
   ($96DB) re-stamped from the leg bearing every leg.
5. **THE RETURN.** `0C` (speed 4) + `98 02 19` (trig-slide to resolver-mode 0x19 = the seat's STAGED HOME)
   + `1B 19` (**exact snap** home), then a last `03 33` spin cycle, `06 03` (idle bob), `24 03` (wait for the
   cross-track rendezvous `$5D9D == 3`), `20 14`, END.
6. **The poses** all come from the shared PC pose engine (`C1/424C`): `$967B = E4[ptrA + facing*stride +
   animId*4 + cursor]`, `dwell = E4[ptrB + animId*4 + cursor]` (0 = end of run). Cyclone's spin pose = animId
   **0x33** → Crono window `E4/30CC` = `7F 80 B7 81`, dwells `E4/A8CC` = `04 04 04 04` — **oracle-measured
   identical**. Our engine ALREADY has this reader: `CtBattlePcWinPose()` (`src/battle.c:287`).

---

## 1 — THE SCRIPT (bank CE), byte-cited

### 1.1 Which script loads (the tech-base open, CLOSED)

`battle_tech_slice.md` §3.2: `$AE93=1` → `$9877 = CD/45A6[1*7][0] = 1` → `$9878 = CD/5EF0[1*2] = 0x0909`.
Oracle (`--monitor "5DBD:60" --from forest_cyclone.state`), **f388** (the frame `$A013` goes 1):

```
$5DBD+4*0 = 17 09 CE 00   role 0  -> CE/0917    (the ATTACKER track)
$5DBD+4*3 = 4D 09 CE 00   role 3  -> CE/094D    (the PRIMARY VICTIM track)
$5DBD+4*4 = 60 09 CE 00   role 4  -> CE/0960    (the 2nd victim / broadcast track)
$5DBD+4*8 = 70 09 CE 00   role 8  -> CE/0970    (the attachment/effect object track)
```
Header at `CE/0909` (`ct_probe --read "rom:CE/0909+160"`):
```
CE/0909: 98 80        word1 = 0x8098
CE/090B: 80 00        word2 = 0x0080
CE/090D: 17 09 4D 09 60 09 70 09 85 09   -> FIVE track pointers 0917/094D/0960/0970/0985
```
Five mask bits (word1 bits 3,4,7,15 + word2 bit 7) = five pointers; four are instantiated in this fight
(the 5th, `CE/0985`, needs a role that this encounter does not create). **The bit→role law is NOT
`role = 15-bit`** (that would give 0/8/11/12; the oracle gives 0/3/4/8) — see OPEN **C1**.

### 1.2 Role 0 = `CE/0917` (54 bytes; `0x094D − 0x0917 = 0x36` ✓)

```
0917: 0C            speed := 4                        C1/4F54  (ops 08-0F: speed = CC/F4BC[op-8])
0918: 72 0D         FACE resolver-mode 0x0D           C1/5FDE  ($96DB,X = $A2B4)
091A: 06 1D         play anim 0x1D, WAIT              C1/4EA2  (latch $A150, parks while $A124!=0)
091C: 20 05         pause 5                           C1/5714  ($5DFD,role = 5)
091E: 06 26         play anim 0x26, WAIT              C1/4EA2
0920: 1E 1D         SUPER 0x1D -> CE/0000[0x3A]=D612  C1/56CC
0922: D0            (set $A5D8 pair)                  C1/6FFC
0923: 27            —                                 C1/5806
0924: D9 30         $99B5[cmb] := 0x30 (draw mode)    C1/753A
0926: 98 02 03      ** TRIG-SLIDE to mode 3 (role 3 = the primary VICTIM), speed 4 **   C1/6299
0929: D1            (clear $A5D8)                     C1/702B
092A: 36            INC $5D9D  (rendezvous -> 1)      C1/594B
092B: 1E 1F         SUPER 0x1F -> CE/D61C  ** THE SPIN **                               C1/56CC
092D: 30 00 00      $5D80+0 := 0                      C1/58EC
0930: 78 FF         sfx 0xFF (stop)                   C1/619E
0932: 36            INC $5D9D  (rendezvous -> 2)      C1/594B
0933: 1E 1E         SUPER 0x1E -> CE/D617  ** FX TEARDOWN **                            C1/56CC
0935: 26 0E 02      HOLD frame 1 of anim 0x0E         C1/577D  (recovery pose)
0938: 0C            speed := 4
0939: D0
093A: 98 02 19      ** TRIG-SLIDE to mode 0x19 = the seat's STAGED HOME **              C1/6299
093D: D1
093E: 28            —                                 C1/5840
093F: DA            $99B5[cmb] := $9F3B[cmb] (restore draw mode)   C1/7579  ** LEN 1 **
0940: 1B 19         ** TELEPORT/SNAP to mode 0x19 (home, exact) **                      C1/567D
0942: 03 33         play anim 0x33, WAIT              C1/4E22
0944: 06 03         play anim 0x03 (idle bob), WAIT   C1/4EA2
0946: 24 03         ** WAIT UNTIL $5D9D == 3 **       C1/5756
0948: 20 14         pause 20
094A: 2E            $CFFF := 0                        C1/58E4
094B: 01            —                                 C1/4CF7
094C: 00            END                               C1/4CF0
```

### 1.3 The supers it calls (`1E ss` = `JMP CE/0000[ss*2]`, `1F` = POP — `C1/56CC` / `C1/56FB`)

`CE/0000` table (read `rom:CE/0000+96`): `…[0x1D]=D612, [0x1E]=D617, [0x1F]=D61C`.

```
CE/D612: 2D            $CFFF := 1                         C1/58DC
CE/D613: 80 02 F0      ** FX SPAWN ** (n=2, copies 02 F0 to DP $53.., JSL CD/001B)     C1/621B
CE/D616: 1F            POP
CE/D617: 2D            $CFFF := 1
CE/D618: 80 12 F0      ** FX TEARDOWN ** (JSL CD/001B with 12 F0)
CE/D61B: 1F            POP
CE/D61C: 0B            speed := 2                          (CC/F4BC[3] = 0x02)
CE/D61D: 78 91         sfx 0x91
CE/D61F: 03 33         play anim 0x33 (spin), WAIT
CE/D621: 02 33         play anim 0x33 LOOPING ($A124 := 2)
CE/D623: 33 00         ** STORED POINT := resolver-mode 0 (role 0 = the ATTACKER's live pos) **  C1/591A
CE/D625: 49 1F E2      storedY += -30                      C1/5A70
CE/D628: 11            move-to-STORED                      C1/4FD1        <- LEG 1  (0,-30)
CE/D629: 49 1E 1E      storedX += +30
CE/D62C: 49 1F 1E      storedY += +30
CE/D62F: 99 01         move-to-STORED ($8F=0)              C1/672E        <- LEG 2  (+30,+30)
CE/D631: 49 1E E2 / 49 1F 1E / 9B 01   ($8F=1)             C1/674A        <- LEG 3  (-30,+30)
CE/D639: 49 1E E2 / 49 1F E2 / 9B 01                                      <- LEG 4  (-30,-30)
CE/D641: 49 1E 1E / 49 1F E2 / 99 01                                      <- LEG 5  (+30,-30)
CE/D649: 49 1E 1E / 49 1F 1E / 99 01                                      <- LEG 6  (+30,+30)
CE/D651: 49 1E E2 / 49 1F 1E / 9B 01                                      <- LEG 7  (-30,+30)
CE/D659: 49 1E E2 / 49 1F E2 / 9B 01                                      <- LEG 8  (-30,-30)
CE/D661: 1F            POP
```

### 1.4 The other tracks (decoded with the same op lens; not measured per-frame)

```
role 3  CE/094D (19B): 34 00 | 24 01 | 20 14 | 6C 00 04 | 24 02 | 20 02 | 1E 00 | 06 03 | 36 | 00
role 4  CE/0960 (16B): 24 01 | 20 14 | 6C 00 04 | 24 02 | 20 02 | 1E 05 | 06 03 | 00
role 8  CE/0970 (21B): D9 30 | 24 01 | 1C 00 | 73 03 | 72 03 | 60 00 | 20 04 | 02 00 | 70 | 24 02 | 71 | 00
role ?  CE/0985      : 06 19 | 20 14 | 03 17 | 03 17 | 50 | 06 03 | 2E 01 | 00     (not instantiated here)
```
`1E 00` = super 0 = `CE/D428` (the victim hurt/ShowDamage block, `battle_ai_interp.md` §13). `$5D9D` is the
**cross-track rendezvous counter**: role 0 INCs it to 1 (post-approach) and 2 (post-spin) with `36`; the
victim tracks `24 01`/`24 02` gate on those; role 0's `24 03` waits for the 3rd INC (oracle GATO f726).

---

## 2 — THE OP SEMANTICS (dispatch table `C1/7A6B[op*2]`, every handler disassembled)

The anim-script op jump table is at **`C1/7A6B`** (located by the byte pattern `09 54 1B 54 2D 54` = the
op-0x16/17/18 handlers of `battle_ai_interp.md` §11b at flat `0x017A97` = `C1/7A97` = base + 0x16*2).

| op | handler | semantics (disasm) | len |
|---|---|---|---|
| `00` | C1/4CF0 | END of track | 1 |
| `02 aa` | C1/4D04 | play anim `aa` **LOOPING**: `$A1E8,cmb2 = $1C99,cmb3 + aa*4`, `$A1FE,cmb2 = $1CBA,cmb3 + aa*4`, `$A124=2`, `$A13A=FF`, `$A214=FF` (`14D45-14D6C`) | 2 |
| `03 aa` | C1/4E22 | play anim `aa` **ONE-SHOT + PARK** (latch `$A145`; `$A124=1`; parks while `$A124!=0`) | 2 (1 on the arming visit) |
| `04/05/06 aa` | C1/4EA2 | same as `03` with latch `$A150` | 2 |
| `08..0F` | C1/4F54 | **speed**: `$A324,cmb = CC/F4BC[op-8]`, `$A2FE,cmb = CC/F5BC[op-8]` (`14F65-14F79`) | 1 |
| `11` | C1/4FD1 | move to STORED point `($5D9E,$5D9F)` → `$A2B0/$A2B2`, arm `C1/5135` | 1 |
| `12 tt` | C1/4FE3 | move to resolver-mode `tt` (the knock op, §10c) | 2 |
| `1B tt` | C1/567D | **teleport** to resolver-mode `tt` (instant) | 2 |
| `1E ss` | C1/56CC | SUPER: push `$E6+2`/`$E8` at `$A569,$A00E`, `$E6 := CE/0000[ss*2]`, bank CE | 2 |
| `1F` | C1/56FB | POP | 1 |
| `20 nn` | C1/5714 | `$5DFD,role := nn` (the per-track frame stall; the op itself advances) | 2 |
| `24 nn` | C1/5756 | **WAIT UNTIL `$5D9D == nn`** (advance 0 while unequal = park) | 2 |
| `26 aa nn` | C1/577D | play anim `aa` but `$A150 := nn` → the tick forces `cursor = nn-1` and clears `$A124` = **HOLD frame nn-1** (`142B8-142C1`) | 3 |
| `2D` / `2E` | C1/58DC / C1/58E4 | `$CFFF := 1` / `$CFFF := 0` | 1 |
| `30 xx vv` | C1/58EC | `$5D80+xx := vv` | 3 |
| `33 tt` | C1/591A | **`$5D9E := $A2B0 ; $5D9F := $A2B2` after `JSR C1/762E(tt)`** = store the resolved point | 2 |
| `36` | C1/594B | `INC $5D9D` | 1 |
| `49 xx dd` | C1/5A70 | **`$5D80+xx += (int8)dd`, saturating 0..255** (`15A79-15A8E`) | 3 |
| `72 tt` | C1/5FDE | FACE resolver-mode `tt`: `$96DB,cmb := $A2B4` | 2 |
| `78 nn` | C1/619E | sfx | 2 |
| `80 n ..` | C1/621B | copy `(n&0x0F)` bytes to DP `$53..`, `JSL CD/001B` (the effect subsystem) | n+1 |
| `98 mm tt` | C1/6299 | **trig-slide** to resolver-mode `tt`; `$8F=0`; `$90 := mm` | 3 |
| `99` | C1/672E | trig-slide to STORED point; `$8F=0`; `$8E=2` (skips one byte) | 2 |
| `9A mm tt` | C1/6743 | = `98` with `$8F=1` | 3 |
| `9B` | C1/674A | **= `99` with `$8F=1`** | 2 |
| `9C/9D` | 6299/672E | = `98`/`99` with the step count `×4` (`16324-16329`) | 3 / 2 |
| `D9 nn` | C1/753A | `$99B5,cmb := nn` (draw/priority mode) | 2 |
| `DA` | C1/7579 | `$99B5,cmb := $9F3B,cmb` (restore) | **1** (`175B6 LDA #01 / JMP C1/75BB`) |

`C1/75BB` is the shared advance (`$E6 += A`). **`$8E` in this interpreter is the OP LENGTH**, not the animId
(the animId is the operand the anim ops copy into the `$A1E8/$A1FE` pointers).

**⚠ Correction to a pinned table:** `battle_ai_interp.md` §13b pins `DA = 2` in `battleAnimOpLen`. The
handler tail is `LDA #01 / JMP C1/75BB` (`C1/175B6`) and the live cursor walk confirms it:
`$5DBD` = `093F` at f614/f615 → `0940` at f616 (forest run). **`DA` is length 1.** With `DA`=2 the parser
would swallow the `1B` of `1B 19` and mis-decode the snap.

### 2.1 The speed table

`CC/F4BC+16` = `01 01 01 02 04 08 10 20 | 01 04 10 40 …`, `CC/F5BC+16` = `04 02 01 01 01 01 01 01 | …`
→ op `08`→speed 1(÷4), `09`→1(÷2), `0A`→1, **`0B`→2**, **`0C`→4**, `0D`→8, `0E`→16, `0F`→32.
Cyclone: `0C` before both `98`s (4px/f) and `0B` in the spin (2px/f) — matches the oracle deltas exactly.

### 2.2 The linear-slide law (op 11/12/98/99/9A/9B, arm `C1/6299`+`C1/5135`, apply `CF/F113`)

```
16299  $D3/$D4 = live ($1D0C,cmb / $1D23,cmb) ; also stashed $A35D,X / $A370,X   (the slide BASE)
162DA  $D5/$D6 = goal ($A2B0 / $A2B2)
162F2  JSR C1/0222 ; JSR C1/65BA            ; deltas $DE/$E0, dominant flag $8C, bearing $DB
1630C  $B3 = $A324,Y (speed) ; JSR C1/00D7  ; ** $ABFE,Y = step count = dominantDist / speed **
1632C  $AA2A,Y = $8F                        ; the 99-vs-9B flag
16340  $A880/$A882 (unit step) x speed -> $A888,Y / $A8AE,Y   (the 16-bit per-frame accumulator step)
16385  ** $96DB,Y = C0/F700[$DB] **         ; the FACING stamp from the leg bearing
163E5  $A337,Y = 1                          ; "moving"
CF/F136  $1D0C,X = $A8D5,Y + $A35D,X ; $1D23,X = $A8FB,Y + $A370,X   ; live = base + acc-hi
CF/F149  LDA $AA2A,X -> $A4AF,X = -/+ $A96E,Y (x1 if $AA04,Y!=0 else x2)  ; RENDER-ONLY Y hop
CF/F186  DEC $ABFE,X ; on 0 -> $A4AF=0, $A19D,X=1 (arrived)
```
`$A4AF,X` is consumed at **`CF/FED98`**: `LDA $A4AF,X / BPL / DEC $AC6F,X / CLC / ADC $1D23,X / STA $AB9E,X`
— a signed **render-Y offset** added to the live Y (and pushed through the 4-deep position history ring
`$AB9E→$ABB4→$ABCA→$ABE0`). So `9B` vs `99` = the sign of that small hop (Crono rides slightly high on the
right-going legs and low on the left-going legs). It does NOT change the travel.

### 2.3 The resolver modes used (`C1/762E`, mode table `C1/79D3` — `battle_ai_interp.md` §11b)

| mode | meaning | used by |
|---|---|---|
| `0x00` | role 0's LIVE pos = the attacker | `33 00` (the spin centre) |
| `0x03` | role 3's LIVE pos = **the confirmed primary victim** | `98 02 03` (the approach) |
| `0x0D` | (engage-offset family, role 4 + `$9708/$9713`) | `72 0D` (the face op) |
| `0x19` | PC seat 0's **STAGED HOME** (`$99AF/$99B2`) | `98 02 19`, `1B 19` |

**Role 3 = the CONFIRMED PRIMARY, proven (§4.2):** with the target cursor moved one slot down,
`$A014 = 00 01 02 **05** FF…` (role 3 → cmb 5) and Crono slides to (167,127) = Hetake slot 2's spot, not to
cmb 3. `$A014` is the role→combatant map, built at dispatch.

---

## 3 — THE POSE ENGINE (the PC scripted-anim machine, `C1/424C`)

```
1424C  X = $A21F (round-robin) ; require $A124,X != 0 && $A12F,X == 0   ; ONE combatant per frame
14269  gates: $A028 == 0 && $A012 == 0
14283  facing = $96DB,X (latched into $A214,X)
14293  $A5 = facing ; $A7 = $966E,X ; $A8 = $A2C8,X ; JSR C1/00A7
142A2  ** $82 = facing * ($966E,X | $A2C8,X<<8) **                 ; the 16-bit FACING STRIDE
142AF  cursor $A13A,X += 1
142B8  if $A150,X != 0 -> cursor = $A150,X - 1 ; STZ $A124,X       ; op-26 "hold frame n-1"
142C4  dwell = E4[ $A1FE,cmb*2 + cursor ]                          ; ** NO facing offset **
142D4  dwell == 0 -> run END: $A124==2 ? cursor=0 (LOOP) : STZ $A124 (one-shot done, unparks op 03/06)
142EC  $A12F,X = dwell
142F1  ** $967B = E4[ $A1E8,cmb*2 + $82 + cursor ] **
14304  STA $967B ; JSR C1/1C4A -> publish $9907,cmb   ; $A012 = 2
```
`$A1E8,cmb*2 = $1C99+cmb*3 + animId*4` and `$A1FE,cmb*2 = $1CBA+cmb*3 + animId*4` (op-02 handler
`14D45-14D56`). Crono (cmb 0): `$1C99 = $E43000`, `$1CBA = $E4A800`, `$966E[0] = 0x28`, `$A2C8[0] = 0x01`
→ **stride 0x0128** (oracle `--monitor "966E:1,A2C8:1"` f385: `28` / `01`).

**Byte-verified**: `E4/30CC = 7F 80 B7 81`, `E4/A8CC = 04 04 04 04` (animId 0x33 = the spin) — the oracle
publishes exactly `7F,80,B7,81` with 4-frame dwells. The facing window was proven by the two runs:
animId 0x1D → `E4[0x3074 + facing*0x128]` = **0x47 at facing 0 (GATO f462)** and **0xAA at facing 2
(forest f413)**; animId 0x26 → 0xD5 / 0xD7; animId 0x03 → 0x43 at facing 1 (GATO f676) / 0xCA at facing 3
(forest f640). A brute-force solve over the ROM confirms `d ∈ {0, 0x128, 0x250, 0x378}` is the ONLY family
that satisfies all measured (animId, facing, pose) triples.

**Cyclone's pose ladder (role 0), as `winOff = animId*4`:**

| script op | animId | winOff | entries (Crono, facing 0) | dwells (`E4/A800+winOff`) |
|---|---|---|---|---|
| `06 1D` | 0x1D | **0x74** | `47` (FF-terminated) | `0A` |
| `06 26` | 0x26 | **0x98** | `D5` | `05` |
| `03 33` / `02 33` | 0x33 | **0xCC** | `7F 80 B7 81` (loops) | `04 04 04 04` |
| `26 0E 02` | 0x0E | **0x38**, entry **1** | `49 8D 8E 49` → holds `8D` | `08 08 08 08` |
| `06 03` | 0x03 | **0x0C** | `C6 4F C7 4F` (the idle bob) | `05 05 05 05` |

**This CLOSES the `CtBattlePcIdleWin` "ptrB decode is OPEN" note (`src/battle.c:283`)**: the dwell is
`E4[ptrB(char) + winOff + entry]`, facing-independent, terminator `0x00`.
⚠ The *observed* cadence equals the table byte only when the `C1/424C` round-robin is uncontended: forest =
a clean 4f per pose; the GATO fight showed 6/4/5/7 before settling to 4 (the `$A012`/`$A21F` lattice, the
same un-pinned item as `battle_ai_interp.md` §14). See OPEN **C3**.

---

## 4 — THE ORACLE FRAME TIMELINE

### 4.1 GATO (`gato_cyclone.state`) — the arena our engine's checks use. Home (134,144), victim cmb3.

`--monitor "A013:1,1D0C:1,1D23:1,5D9D:1,9907:1,96DB:1,A3D1:1" --diff`

| frame | rel (−f438) | event |
|---|---|---|
| f438 | 0 | `$A013=1` — span ON (313f, ends f751 — the pinned `CT_SPAN_PC_TECH_CYCLONE`) |
| f462 | 24 | pose `0x47` (`06 1D`), facing 0 |
| f470 | 32 | pose `0xD5` (`06 26`) |
| f471-473 | 33-35 | `1E 1D` → `2D` (f471) + **`80 02 F0` = the whirl FX SPAWN (f472 = rel 34)** + `1F`/`D0` (f473) |
| f478-484 | 40-46 | **APPROACH slide**: 7 steps ×(+0/−4) → (134,144) → **(135,116)** = GATO's live foot |
| f486 | 48 | `36` → `$5D9D = 1` |
| f488 | 50 | spin pose loop starts (`03 33`: `7F` f488, `80` f494, `B7` f498, `81` f503) |
| f511-525 | 73-87 | **LEG 1** (0,−30) 15×2px, facing 0 → (135,86) |
| f527-541 | 89-103 | LEG 2 (+30,+30), facing 1 (stamped f526) → (165,116) |
| f543-557 | 105-119 | LEG 3 (−30,+30), facing 2 (f542) → (135,146) |
| f559-573 | 121-135 | LEG 4 (−30,−30), facing 0 (f558) → (105,116) |
| f575-589 | 137-151 | LEG 5 (+30,−30), facing 3 (f574) → (135,86) |
| f591-605 | 153-167 | LEG 6 (+30,+30), facing 1 (f590) → (165,116) |
| f607-621 | 169-183 | LEG 7 (−30,+30), facing 2 (f606) → (135,146) |
| f623-637 | 185-199 | LEG 8 (−30,−30), facing 0 (f622) → (105,116) |
| f638-639 | 200-201 | `1F` pop, `30 00 00`, `78 FF`, `36` → `$5D9D = 2`, `1E 1E` → `2D` (f638) + **`80 12 F0` = the whirl FX TEARDOWN (f639 = rel 201)** |
| f641 | 203 | pose `0x8D` (`26 0E 02`) |
| f645-651 | 207-213 | **RETURN slide** 7 steps ×(+4,+4) → (133,143) |
| f655 | 217 | **`1B 19` SNAP** → (134,144) exact |
| f657-676 | 219-238 | `03 33` spin cycle again (`7F`/`80`/`B7`/`81`) |
| f665 | 227 | `$A3D1` popup born (58f, off f723) |
| f676 | 238 | pose `0x43` (`06 03`, idle bob, facing 1) |
| f726 | 288 | `$5D9D = 3` → role 0's `24 03` releases |
| f751 | **313** | span OFF; HP applied |

Visual confirmation: `--peek gato_cyclone.state … 100:2,0:213` (= f637, the LEFT diamond vertex) renders
Crono standing to the LEFT of GATO, far from his seat; Marle stays at her home (176,111).

### 4.2 FOREST (`forest_cyclone.state`) — 3 Hetakes; home (160,71), primary cmb3 @(120,63)

| frame | event |
|---|---|
| f388 | span ON; the 4 tracks load (`$5DBD` §1.1) |
| f410-421 | `0C`, `72 0D`, `06 1D` (pose `0xAA`, facing 2), `20 05`, `06 26` (pose `0xD7`) |
| f422-424 | `1E 1D` → `2D`, `80 02 F0`, `1F`, `D0` |
| f425-426 | `27`, `D9 30` (`$99B5` 0x20→0x30) |
| f428-437 | **APPROACH** 10 steps ×(−4,−0.8) → **(120,63) = the primary's exact foot** |
| f438-439 | `D1`, `36` (`$5D9D`=1), `1E 1F`→`0B`, `78 91` |
| f440-457 | `03 33` (poses `7F` f441 / `80` f445 / `B7` f449 / `81` f453, dwell **exactly 4**) |
| f458 | `02 33` (loop) + `33 00` (stored := (120,63)) |
| f460-474 | LEG 1 (0,−30) → (120,33), facing 0 |
| f476-490 | LEG 2 (+30,+30) → (150,63), facing 1 |
| f492-506 | LEG 3 (−30,+30) → (120,93), facing 2 |
| f508-522 | LEG 4 (−30,−30) → (90,63), facing 0 |
| f524-538 | LEG 5 (+30,−30) → (120,33), facing 3 |
| f540-554 | LEG 6 (+30,+30) → (150,63), facing 1 |
| f556-570 | LEG 7 (−30,+30) → (120,93), facing 2 |
| f572-586 | LEG 8 (−30,−30) → (90,63), facing 0 |
| f587-589 | `1F`, `30 00 00`, `78 FF`, `36` (`$5D9D`=2), `1E 1E`→`2D`, `80 12 F0`, `1F`, `26 0E 02` |
| f591-592 | `0C`, `D0` |
| f596-612 | **RETURN** 17 steps ×(+4,+0.5) → (158,70), facing 3 |
| f613-616 | `D1`, `28`, `DA` (`$99B5`→0x20) |
| f617 | **`1B 19` SNAP → (160,71)** |
| f638-640 | `03 33`, then `06 03` (pose `0xCA`) |
| f620-678 | `$A3D1` popup (58f) |
| f681-703 | `$5D9D`=3 releases `24 03`; `20 14` |
| f706 | span OFF — **318f** (emergent, not 313: the approach/return distances differ) |

**Target-cursor experiment** (btnscript with an extra `20:2,0:12` before the final A): primary = Hetake
slot 2 → `$9605 = 05`, `$99C0 = 05 FF FF`, **`$A014 = 00 01 02 05 FF…`** and Crono slides f438-451
(14 = 56/4 steps) to **(167,127)** = that Hetake's foot; the spin then orbits **(167,127)**. Both the
approach target and the spin centre follow the confirmed primary — no fixed coordinates anywhere.

---

## 5 — WHAT OUR ENGINE DOES INSTEAD (and why the user sees only the FX)

1. **No route is armed for a tech.** `src/battle.c:3404`
   ```c
   if (techId == 0 && itemId == 0) battlePcAtkArm(h);   // "a PC tech/item has NO melee run-up"
   ```
   `battlePcAtkArm` (`src/battle.c:2333`) is therefore never called for Cyclone → `pcAtkMvActive = 0`,
   `mvLiveX[0]/mvLiveY[0]` never move. `battlePcAtkTick` (`src/battle.c:2370`, called at
   `src/battle.c:3483`) returns immediately. **This is the root cause of "he is not even leaving his
   position".** The comment's premise ("the CE presentation is the T3 boundary") is now refuted: the CE
   script's travel ops are the SAME `98`/`1B`/`49`/`11`/`99`/`9B` family already ported for the physical.
2. **Even a moved live foot would not render.** `battleStanceRender` (`src/field.c:2135`) draws every PC
   seat at the **STAGED** foot `mem + 0x29B0 + seat*5` (`16*r[0]+r[2]`), never at `h->mvLiveX/Y`; only
   `battleAttackRender` (`src/field.c:3306`) reads the live foot, and it early-returns unless
   `h->fxAtkPose ∈ [0,6)` or `h->critAnim` — neither is set for a tech. So during Cyclone Crono is drawn
   by the stance/idle-bob layer, pinned at his seat, with the default stance pose.
3. **No pose ladder.** Nothing selects animIds 0x1D/0x26/0x33/0x0E/0x03 for a tech, so his sprite never
   shows the spin frames (`0x7F/0x80/0xB7/0x81`) nor the recovery `0x8D`.
4. **The FX are right, which is exactly why only they show.** `battleWhirlRender` (`src/field.c:3629`) draws
   the pinned oracle fixture at `rel ∈ [60, 201)` (`src/battle_fx_whirl.h:16`) with the fixture's own
   absolute (x,y) — captured from the oracle, i.e. **around the victim**, where Crono is supposed to be.
   Our Crono stays at his seat → the whirl plays alone in the middle of the arena.
   Corroboration that the fixture window is the script's: the oracle's `80 12 F0` teardown fires at
   forest f588 = span+200 and GATO f639 = span+**201** (`$5DBD` walks `D618`→`D61B`) — the fixture's last
   drawn frame is rel 200 (`CT_WHIRL_REL0 60 + CT_WHIRL_RELN 141`), i.e. it stops **exactly** when the script
   tears the effect down.

---

## 6 — IMPLEMENTATION SPEC (mandate-safe; every constant cited)

The mechanism is GENERAL: it is the CE **command script** of whatever tech is dispatched, run by the same
interpreter that already drives the physical. Nothing below is keyed on a tech id or a scene.

### (a) Arm the route for a TECH — a per-tech CHOREOGRAPHY, not a physical run-up

Replace the physical-only gate at `src/battle.c:3404` with a dispatcher that arms *whatever the tech's
role-0 CE track says*. Minimum faithful shape (the ops Cyclone uses; all already have ported analogues):

```
struct { int kind; int a, b; } step;    // a tiny op-driven route program for role 0
```
armed at dispatch from the tech's own script address `CE/CD5EF0[$9877*2]` (`$9877 = CD/45A6[techId*7][0]`,
already computed in `battleActionSpan`), walking the track with the op-length table of §2 and emitting:

| CE op | route step to emit |
|---|---|
| `08..0F` | `speed = ROM[CC/F4BC + (op-8)]` (`0x0CF4BC`), sub-divisor `ROM[CC/F5BC + (op-8)]` (`0x0CF5BC`) |
| `98 mm tt` / `9A` | slide to `resolve(tt)` at the current speed; `$8F` = 0 / 1 |
| `99` / `9B` | slide to the STORED point at the current speed; `$8F` = 0 / 1 |
| `11` | slide to the STORED point (same arm) |
| `1B tt` | teleport (snap) to `resolve(tt)` |
| `33 tt` | `stored = resolve(tt)` |
| `49 xx dd` | `stored[xx-0x1E] += (int8)dd`, clamped to `[0,255]` |
| `72 tt` | facing := aim at `resolve(tt)` |
| `03/06 aa`, `02 aa`, `26 aa nn` | pose ops (see (c)) |
| `20 nn` | stall `nn` frames |
| `24 nn` | wait for the rendezvous counter (see (d)) |
| `36` | rendezvous counter += 1 |
| `1E ss` / `1F` | call `ROM[CE/0000 + ss*2]` (flat `0x0E0000`) / return (1-deep per track, `$A569`) |
| `2D`/`2E`/`30`/`27`/`28`/`D0`/`D1`/`78`/`80`/`D9`/`DA` | no route effect (sfx/FX/flags — `D9`/`DA` = the draw-mode byte, see (e)) |

**`resolve(tt)`** must use the mode table already derived (`battle_ai_interp.md` §11b): mode 0 → the
attacker's live foot (`mvLive[0]`); mode 3 → the **confirmed primary victim** `h->actTarget`'s live foot
(the oracle's `$A014[3]`, proven §4.2); mode `0x19+seat` → the seat's staged home (`mem+0x29B0+seat*5`,
`16*r[0]+r[2]` — the same read `battlePcAtkArm` already does at `src/battle.c:2341`).

**The slide arm/tick is ALREADY in the engine** — `battlePcAtkTick`'s op-98 branch (`src/battle.c:2419`):
`dom = max(|dx|,|dy|)`, `count = dom / speed`, per-frame accumulate `battleTrigMul(angle, speed)` with the
`0x80` rounding seed. Generalise it to take `(goalX, goalY, speed)` and reuse it verbatim for every slide
op (approach speed 4, the 8 legs speed 2, the return speed 4). Do **not** snap to the goal on arrival —
the accumulator's own landing is canonical (forest primary-slot-2: it lands at 167 while the goal is 168).

Facing per slide = `ctBattleFaceTab[battleAtan2(live, goal)]` (the ROM's `$96DB,Y = C0/F700[$DB]` at
`C1/6385`) — the engine's existing `CtBattleAimFacing` does exactly this.

### (b) The spin is DATA, not code

Nothing about "8 legs of 30px" may be hardcoded: it falls out of `49 1E/1F ±0x1E` + `11`/`99`/`9B` in
`CE/D61C..D661`. If the interpreter of (a) is implemented, Cyclone's diamond emerges and any other tech gets
its own figure for free. (If the full interpreter is out of scope for one pass, the legs may be driven from
a table **generated by parsing that ROM range**, never from typed-in coordinates.)

### (c) Pose ladder — reuse `CtBattlePcWinPose`

`src/battle.c:287` already implements `E4[ptrA(char) + facing*stride + winOff + entry]` with
`stride = (E4/2600[cid+1] − E4/2600[cid]) >> 2` (= 0x128 for Crono — matches the measured
`$966E|$A2C8<<8`). So:
- `winOff = animId * 4`; `entry` = the run cursor.
- **dwell** (new, now derivable): `E4[ptrB(char) + winOff + entry]`, `ptrB = E4/2800[cid*2]`, terminator
  `0x00`; `03/06` = one-shot (park until the run ends), `02` = loop, `26 aa nn` = hold entry `nn-1`.
  This closes the `CtBattlePcIdleWin` dwell TODO (`src/battle.c:283`).
- Cyclone's ladder (from the script, §3): `06 1D`→win 0x74 · `06 26`→win 0x98 · `03 33`/`02 33`→win 0xCC
  entries 0..3 dwell 4 (loops through the whole spin) · `26 0E 02`→win 0x38 entry 1 · `06 03`→win 0x0C.

### (d) Render — let the tech draw Crono from the LIVE foot

`battleStanceRender` (`src/field.c:2135`) must skip seat 0 while a PC tech animates (the same shape as its
existing `cronoAtk` / `marleAtk` gates), and a tech render path (extend `battleAttackRender`,
`src/field.c:3306`, or add a sibling) must blit `ctAnimengPcBlit(fb, charId, poseIdx, mvLiveX[0],
mvLiveY[0], pal)` with `poseIdx` from (c) and `facing` from the route's current stamp. The
`battleCronoIdleRender` active-bob gate (`src/field.c:3751`) needs the same exclusion.
Optional fidelity: the render Y may add `$A4AF` (§2.2) — a ±1..4px hop; it is derived but small, and can be
deferred to OPEN **C4** without affecting the report.

### (e) Timing / span

- Keep `CT_SPAN_PC_TECH_CYCLONE = 313` (`src/battle.c:2050`): it IS the GATO ledger's f438→f751 and the
  choreography above fits inside it exactly (return snap at rel 217, popup rel 227, idle rel 238).
- Anchor every route/pose step on `rel = h->frames − h->actStart` using the §4.1 GATO table, since that is
  the arena all the pinned checks use. The **true** span is emergent (forest = 318) — see OPEN **C2**.
- `D9 30` / `DA` set `$99B5[cmb]` 0x30 during the tech and restore `$9F3B[cmb]`=0x20 after; if our renderer
  ever consumes a per-combatant draw-mode, mirror it (currently unused → no-op).

### (f) FX

Leave `battleWhirlRender` and its fixture untouched: the script's `80 02 F0` spawn (GATO **rel 34**) and
`80 12 F0` teardown (GATO **rel 201**, forest rel 200) bracket the pinned `CT_WHIRL_REL0 60 … +141` window, and the whirl's
fixture coordinates are the oracle's — i.e. around the victim, which is where the ported Crono will now be.
The pins `field-battle-tech-cyclone-whirl-asma` / `-asmb-flip` therefore stay valid **as long as the
whirl keeps being drawn from the fixture**; if it is ever re-anchored to `mvLive[0]`, re-pin knowingly.

### (g) Checks to pin

1. `field-battle-tech-cyclone-route` (seed `gato_cyclone.raw.wram`, `CT_BATTLE_ROUTETRACE=1`): live foot
   (134,144) → (135,116) by rel 46 → 8 legs of 15×2px through (135,86)/(165,116)/(135,146)/(105,116) ×2 →
   return → **snap (134,144) at rel 217**.
2. `field-battle-tech-cyclone-pose` (`CT_ANIMENG_PCTRACE=1` + `CT_BATTLESHOT`): the published index
   sequence `0x47 → 0xD5 → (7F,80,B7,81)×… → 0x8D → (7F,80,B7,81) → 0x43`.
3. `field-battle-tech-cyclone-multi` (existing) must stay byte-identical — the route touches presentation
   only; the hit set / damage / span are unchanged.

---

## 7 — OPEN

| # | item | what would close it |
|---|---|---|
| **C1** | **The header mask bit → ROLE law.** `word1=0x8098 / word2=0x0080` (5 bits) instantiates roles **0, 3, 4, 8** (oracle `$5DBD` f388), which is neither `role = bit` nor `role = 15−bit`. §11a's "bit15→role 0, bit14→role 1" model does not reproduce it. | disassemble the CE track loader (the three call sites named in §13: `C1/4477` / `C1/4662` / `C1/4816`) and read the mask walk directly; cross-check on a 3rd script with a different mask |
| **C2** | **The span is EMERGENT, not constant.** GATO 313f, forest 318f for the same tech (the approach/return distances and the `24 03` rendezvous differ). Our engine pins 313 for all arenas. | run the whole interpreter (the ops park; the span falls out) — or measure a per-arena pin. Cosmetic-only at the 5-frame scale |
| **C3** | **The pose-tick cadence contention.** `C1/424C` ticks at most ONE combatant per frame and gates on `$A012`/`$A028`; the forest run shows a clean 4f dwell, GATO shows 6/4/5/7 then 4. Where `$A012` is cleared is still unlocated (the same §14 "6.5f lattice" open). | find the per-frame `$A012` clear (an indirect dispatch; no static JSR/JSL refs) |
| **C4** | **`$A4AF` render hop** (§2.2): the derived formula is `∓$A96E,Y` scaled by `$AA04` — the exact per-frame magnitude was not tabulated against a pixel capture. | OAM/`--peek` diff of a slide frame vs the live `$1D23` |
| **C5** | **`CD/001B`** (the `80 nn ..` effect-subsystem call) — the whirl's actual pixels/mover. Unchanged T3 boundary; the fixture stands in. | the C3/FD effect-subsystem decode |
| **C6** | Ops `27`, `28`, `1C`, `60`, `6C`, `73` (used by the victim/effect tracks) are located in the table but not disassembled here. | dis.py at `C1/5806`, `C1/5840`, `C1/5694`, `C1/5D1D`, `C1/5F13`, `C1/6059` |
| **C7** | ⚠ **`battleAnimOpLen` has `DA = 2`; the ROM says 1** (`C1/175B6`, live cursor `093F→0940`). Fix when the parser is next touched — with 2 the snap `1B 19` mis-parses. | one-line change + re-run the parse-diagnostic checks |

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-e956ecca-…/scratchpad/lane_D2/`
- `ct_probe.exe` (= build/ct.exe), `patch_state.py` (WRAM patch via `tools/ctlib.lake`).
- `gato_cyclone.state` (= repo `gato_press_action.state` + `2837|=80`),
  `forest_cyclone.state` (= `saves/battle_re/forest_holdleft_pre.state` + `2837|=80`).
- Monitors: `mon_pos.txt` (`A013/1D0C/1D23`), `mon_cursor.txt` (`5DBD:4` = the op-by-op walk),
  `mon_pose.txt` (`1C99/1CBA/9907/968C/9689/96DB`), `mon_gato.txt` (the GATO ledger).
- Renders: `cyc637.png` (Crono at the LEFT diamond vertex), `cyc540.png`.
- Disasm anchors (flat = `(bank−0xC0)<<16|addr`): op table `0x017A6B`; speed `0x014F54`; op-11 `0x014FD1`;
  op-49 `0x015A70`; op-24/26 `0x015756`/`0x01577D`; ops 2D-3C `0x0158DC`; op-72 `0x015FDE`; op-80 `0x01621B`;
  op-98 `0x016299`; op-99/9A/9B `0x01672E`/`0x016743`/`0x01674A`; op-D9/DA `0x01753A`/`0x017579`;
  advance `0x0175BB`; the PC pose tick `0x01424C`-`0x01430F`; the slide apply `0x0FF113`; the `$A4AF`
  consumer `0x0FED95`.
- ROM tables: `CE/0909` (Cyclone script), `CE/0000` (super table), `CC/F4BC`/`CC/F5BC` (speed),
  `E4/3000`/`E4/A800` (Crono pose ptrA/ptrB), `E4/2600`/`E4/2800` (per-char pose-pointer tables).

---

## VERIFICATION (adversarial pass)

Independent re-derivation by a second agent. Probe = `verify_D2/ct_probe.exe` (copy of `build/ct.exe`);
states rebuilt from scratch (`gato_press_action.state` / `saves/battle_re/forest_holdleft_pre.state` +
`$2837|=0x80`); every ROM byte re-dumped from `Chrono_Trigger_USA_SNES.sfc`; every cited handler re-run
through `tools/dis.py`. **Verdict: SOUND WITH CORRECTIONS** — the mechanism reproduces byte-for-byte; the
items in the second table must be fixed before code is written.

### CONFIRMED (re-measured, not re-read)

| claim | independent evidence |
|---|---|
| `$AE93=1` -> `$9877=1` -> `$9878=0909`; header `CE/0909 = 98 80 / 80 00 / 17 09 4D 09 60 09 70 09 85 09` | ROM dump + `--monitor "9877:1,9878:2,AE93:1"` forest **f388** |
| track cursors `$5DBD+4*role` = 0917 / 094D / 0960 / 0970 for roles 0/3/4/8 | `--monitor "5DBD:60"` forest f388 = `1709CE00 … 4D09CE00 6009CE00 … 7009CE00` |
| role-0 track = 54 B, decoded op-for-op as section 1.2 | ROM bytes + a full live cursor walk: 0918 f410 / 091A f411 / 091C f413 / 091E f414 / 0920 f421 / D613 f422 / D616 f423 / 0923 f424 / 0924 f425 / 0926 f426 / 0929 f437 / 092A f438 / D61D f439 / ... / 093F f614 / **0940 f616** / 0942 f617 / 0944 f638 / 0946 f640 / 094A f682 / 094B f703. **Every op length matches.** |
| `CE/D61C..D661` = the 8 legs (`49 1E/1F +-1E` + `11` / `99 01` / `9B 01`) | ROM dump byte-identical to section 1.3 |
| dispatch table `C1/7A6B[op*2]` | dumped: 00->4CF0 02->4D04 03->4E22 06->4EA2 0C->4F54 11->4FD1 1B->567D 1E->56CC 1F->56FB 20->5714 24->5756 26->577D 33->591A 36->594B 49->5A70 72->5FDE 80->621B 98->6299 99->672E 9B->674A D9->753A DA->7579 |
| handler semantics (11 / 1B / 1E / 1F / 20 / 24 / 26 / 33 / 36 / 49 / 72 / 80 / 98 / DA) | each re-disassembled; all match section 2 |
| speed `CC/F4BC = 01 01 01 02 04 08 10 20`, `CC/F5BC = 04 02 01 01 ...`; `0B`->2 `0C`->4 | ROM + observed 4px/f approach, 2px/f legs |
| slide law: `$ABFE = dom/speed` (`C1/00D7` = the hardware divider), `$96DB,Y = C0/F700[$DB]` @16385, `$A337=1` @163E5, PARK on re-visit (`C1/653B` advance 0) / advance `$A3A9` (= `$8E`) on arrival (`C1/6540`), apply `CF/F136` live = base + acc-hi, `CF/F186` DEC -> `$A19D=1` | disasm; **dominant = max(abs dx, abs dy)** proven by the forest return (dx 70 / dy 8 -> 17 steps) |
| resolver `C1/762E` -> `JSR ($79D3,X)`; modes 00-08 -> `C1/7639` = `$A014[mode]` live pos; 09-11 -> `C1/764A`; **19-1B -> `C1/77DA` = `$99AF/$99B2[m-0x19]`** | mode table dumped + handlers disassembled |
| **role 3 = the CONFIRMED PRIMARY, not a fixed slot** | default forest `$A014 = 00 01 02 03 04 FF...`; with `20:2,0:12` inserted: `$9605=05`, `$99C0=05FFFF`, `$A014 = 00 01 02 05 FF...`, slide f438-451 (14 steps) -> **(167,127)** while cmb5 sits at **(168,127)** (`--monitor "1D0C:8,1D23:8"`) -> **lands 1px short, no snap** |
| spin centre = `33 00` = own live pos; the 8 stored points | forest `$5D9E/$5D9F`: f458 `783F` -> f459 `7821` -> f475 `963F` -> f491 `785D` -> f507 `5A3F` -> f523 `7821` -> f539 `963F` -> f555 `785D` -> f571 `5A3F` |
| 8 legs = 15 f x 2px, facings 0/1/2/0/3/1/2/0 | forest f460-474 / f476-490 / ... / f572-586, endpoints (120,33) (150,63) (120,93) (90,63) twice |
| return + `1B 19` snap | forest 17 steps -> (158,70), snap **f617 -> (160,71)** exact; GATO 7 steps -> (133,143), snap **f655 -> (134,144)** = rel 217 |
| **`DA` is length 1** | ROM tail `A9 01 4C BB 75` @C1/75B6 **and** both captures: forest 093F f614 -> 0940 f616; GATO 093F f653 -> 0940 f654 |
| pose engine `C1/424C` (facing-stride multiply, `$A150` hold, dwell with no facing offset, 0 = terminator, loop vs one-shot) | disassembled 1424C-1430F line-for-line |
| pose windows + stride 0x128 | `E4/3074 / 3098 / 30CC / 3038 / 300C` + `A874 / A898 / A8CC / A838 / A80C` dumped at **all 4 facings**: anim1D `47/A9/AA/AB`, anim26 `D5/D6/D7/D8`, anim33 `7F 80 B7 81` at *every* facing, anim0E f0 `49 8D 8E 49`, anim03 `C6../43../C8../CA..`. Stride proven 3 ways: `$966E|$A2C8<<8 = 0128` (f385), `(E4/2600[1]-[0])>>2 = 0128`, and the measured (anim,facing,pose) triples |
| `$1C99[0]=E4/3000`, `$1CBA[0]=E4/A800`, `E4/2600[0]=3000`, `E4/2800[0]=A800` | `--monitor "1C99:3,1CBA:3"` f240 = `0030E4` / `00A8E4` + ROM |
| `mem+0x29B0+seat*5` identical to `$99AF/$99B2` | forest f388: `$29B0 = 0A 04 00 07 00 | 07 09 0F 00 00 | 08 0A 0F 00 D3` -> (160,71) (127,144) (143,160) == `$99AF=A07F8F` / `$99B2=4790A0` for **all three seats** |
| rendezvous `$5D9D` 0->1->2->3 | forest f439 / f587 / f681 (`24 03` releases f682); GATO f486 / f638 / f726 |
| FX spawn / teardown | GATO cursor D613->D616 at **f472 = rel 34**, D618->D61B at **f639 = rel 201**; forest rel 200 |
| spans are EMERGENT: GATO 313 (f438->f751), forest 318 (f388->f706) | both re-measured |
| **Crono is off his seat mid-spin** | independent **OAM** check (not a PNG): `--peek ... 0:213` -> pal5 (seat 0) quad at x[101..117] y[75..107]; pal6 (Marle) at x[164..180] y[63..95] = exactly her home min-corner (176-12, 111-48); peek WRAM `$1D0C/$1D23 = (107,118)` |
| every `src/` citation (battle.c 283 / 287 / 2050 / 2333 / 2341 / 2370 / 3404 / 3483 / 2419; field.c 2135 / 2168 / 3306 / 3324 / 3629 / 3751) | each line re-read; the quoted conditions are exact; `mvLive[0]` is used as a draw foot ONLY at field.c:3324 |
| `$8F -> $AA2A,Y` -> `CF/F149` sign of `$A4AF` (doubled when `$AA04==0`) -> consumed at `CF/ED98`: `LDA $A4AF,X / BPL / DEC $AC6F,X / CLC / ADC $1D23,X / STA $AB9E,X` | disassembled (re-aligned at 0xFED95) |

### REFUTED / must be corrected before coding

**V1 (spec-breaking). `04/05/06 aa` does NOT park for the whole run — it shows entry 0 and advances.**
`C1/4EA2` does `INC $A150,X` -> `$A150 = 1`; the pose tick's `$A150` path (`C1/142B8-142C1`) then forces
`cursor = $A150-1 = 0` **and `STZ $A124,X`**, so the run terminates after the FIRST entry. `06 aa` is
exactly `26 aa 01`. Measured: `06 1D` 091A->091C in **2 f** (f411->f413) although anim 1D's entry-0 dwell
is `0A`; `06 03` 0944->0946 in **2 f** (f638->f640) although anim 03 is 4 entries x 5 f; while the true
one-shot `03 33` (`C1/4E22`, latch `$A145` — a variable the tick never touches) parked **21 f**
(f617->f638). Our own source already states this: `src/battle.c:2518  // 04/05/06 aa play first-frame only`.
Section 1.2's "play anim, WAIT" annotations and implSpec (4)/(c) "op 03/06 = one-shot, park until the run
ends" are WRONG and would add about 9 f at `06 1D` plus about 18 f at `06 03`, drifting the whole ledger.

**V2 (spec gap). implSpec (1)'s script address is the HEADER, not a track.** `CD/5EF0[$9877*2] = 0x0909` is
the 14-byte header; the role-0 track is `ROM16[CE:base+4] = 0x0917`. A walker started at 0x0909 decodes
`98 80 / 80 00 ...` = a bogus trig-slide. Also "pointer[0] = role 0" is an OBSERVATION for this mask only
(OPEN C1), so the walker is general in form but unproven for any other tech — state that.

**V3 (spec gap). `72 0D` is in the track but `resolve()` (implSpec (2)) never defines mode 0x0D — and role 4
does not exist in the GATO arena.** Mode 0x0D -> `C1/764A` -> `$A014[0x0D-9] = $A014[4]`, and GATO's
`$A014 = 00 01 02 06 FF FF FF FF` -> **0xFF**. Measured: `$96DB` never changed at the `72 0D` in EITHER
capture (forest = 2 since f257, GATO = 0 since f282), i.e. the pre-approach facing that selects pose `0xAA`
vs `0x47` is the PRE-EXISTING facing. Implementing "72 tt -> aim at resolve(tt)" as written is unverified
and can pick the wrong pose.

**V4. Section 4.1's GATO victim identification is wrong.** `$A014 = 00 01 02 06 FF...` (role 3 -> cmb **6**,
not 3) and GATO's live foot is `$1D0C[6]/$1D23[6] = (136,113)`. The approach landing **(135,116) is NOT
"GATO's live foot"** — it is the accumulator's landing, 3px short (dx 2 / dy -31 -> dom 31 -> 7 steps).
(implSpec's "do not snap" is right; the prose is not. Same for the slot-2 case: 167 vs a goal of 168.)

**V5. The whirl fixture numbers misquote our header.** `src/battle_fx_whirl.h:17` is
`CT_WHIRL_RELN **142**` (its own comment: "live rel 60..201") and `battleWhirlRender` draws for
`rel` in `[60, 202)` -> **last drawn rel = 201**, not 200. Section 5.4 / 6(f) / the summary's "60 + 141 ->
rel 200" is wrong (the conclusion survives: the last drawn frame IS the teardown frame).

**V6. "`battleAnimOpLen` has `DA = 2`" is FALSE of our code.** `battleAnimOpLenRom` (`src/battle.c:2483`)
scans C1/7579 for `A9 nn 4C BB 75`, hits `A9 01 4C BB 75` at C1/75B6 with no earlier `4C/60/6B`, and
returns **1** — `battleAnimOpLen` returns that and never reaches the fallback switch, so
`case 0x7579: return 2;` (`src/battle.c:2547`) is DEAD CODE. The stale `DA=2` pin lives in
`battle_ai_interp.md` section 13b (a NOTE), not in the parser. There is **no live parse bug**; do not
"fix" `battleAnimOpLenRom`.

**V7 (prose). "`$8E` is the OP LENGTH" is not universal**: op 72 stores its *mode* operand in `$8E`
(`C1/5FE1`), and ops 03/06/26 set `$8E=1` as an ARM signal to `C1/4D08`, which then RTSs (= PARK, not
advance-by-1). `$8E` is the length only for the ops that stash it before a shared tail (11/12, 1B, 98).

**V8 (prose). Not "two full orbits".** The legs run centre -> N, then E, S, W, N (one orbit), then E, S, W —
ending at the **WEST** vertex, which is where the return slide starts (that IS what the ledger shows).

**V9 (nit).** keyFact "cursor 0946->0948 at f682": measured **0946 -> 094A** at f682 (`24 03` and `20 14`
both run that frame).


---

# IMPLEMENTATION ADDENDUM (session 24) — SHIPPED

A general **CE role-0 route interpreter** landed in `src/battle.c` (`battleTechOpLen` /
`battleTechRouteParse` / `battleTechRouteArm` / `battleTechRouteTick` / `battleTechPoseTick` /
`battleTechResolvePt` / `battleTechSlideArm`), armed from `battleDispatchScheduled` for any PC tech. The
track is PARSED from the ROM at dispatch (header `CD/5EF0[$9877*2]`, role-0 = `ROM16[header+4]`, `1E ss`
supers inlined via `CE/0000[ss*2]`) into a step program — so **the eight legs are ROM data, not typed-in
coordinates**, and any other tech gets its own figure for free. The old
`if (techId == 0 && itemId == 0) battlePcAtkArm(h);` comment ("a PC tech has NO melee run-up") is deleted.

**The slide law was corrected during implementation** and is now shared with the physical's op-98 return
(`battleSlideArmSteps`): `step_axis = speed * delta_axis / dominant`, `count = dominant / speed` TRUNCATED.
The first attempt used the Euclidean-unit `battleTrigProdS` step (correct only for cardinals) and left every
45° leg ~9px short; the dominant-axis form reproduces all three of this dossier's data points — the
`10 x (-4,-0.8)` approach, the `15 x 2px` diagonal legs landing exactly on the stored point, and the
17-step return landing **2px short** so the script's own `1B 19` finishes it.

**Corrections from the adversarial pass that were applied:** `04/05/06 aa` is a HOLD (entry 0, one pose
tick) — identical to `26 aa 01` — not a play-and-wait; only `03 aa` parks the thread. The role-0 track is
`ROM16[header+4]`, never the header address. `72 0D` is INERT (mode 0x0D is unresolved and the oracle shows
`$96DB` unchanged at it), so the facing comes only from each slide's own `C0/F700[bearing]` stamp.
`battleAnimOpLen`'s stale `DA=2` note was left alone — the live parser already returns 1 via its ROM tail
scan. `CT_WHIRL_RELN` is 142 (last drawn rel 201).

**Validated** — forest: `techroute-snap … approach=(120,63) slides=10 home=(160,71)`, i.e. he slides off his
seat (160,71) onto Hetake slot0's exact staged foot, runs 8 x 15-frame legs through
(120,33)/(150,63)/(120,93)/(90,63) twice, slides back and snaps home. GATO: `approach=(136,118)`,
`home=(134,144)`. `field-battle-tech-cyclone-dispatch` and `-multi` stayed BYTE-IDENTICAL (the hit set,
per-victim damage and the 313f span are untouched — presentation only); the two whirl-shot canaries
re-pinned because Crono is now visibly orbiting in them (the whirl fixture itself is unchanged). Pinned as
`field-battle-tech-cyclone-route`.

**Residual (OPEN C3, unchanged):** our pose-op cadence advances ~1 frame where the ROM's takes ~2, so the
whole route runs ~30f ahead of the §4.1 ledger inside the same 313f span. The 21-frame pre-roll IS modelled
(`CT_PC_ACT_PREROLL`, corroborated by this fight's own rel-24 first pose).

---

# ADDENDUM 2 (session 24, follow-up) — THE WHIRL IS ATTACHED TO THE ATTACKER

User report right after the route shipped: *"the effects of crono's cyclone are not synchronised with
crono's position in the animation."* §6(f) had left `battleWhirlRender` on its ABSOLUTE fixture anchors on the
assumption that they sit "around the victim, which is where the ported Crono will now be" — that assumption
is wrong twice over: the anchors are the GATO fight's own arena coordinates (so any other arena is off by
the whole formation delta), and they are indexed by `rel`, so they only line up if our route is
frame-identical to the capture — which OPEN **C3** says it is not (~30f ahead).

**DERIVED (oracle, this session).** `--monitor "1D0C:1,1D23:1,A013:1"` on the same
`gato_press_action + $2837|=0x80` state gives Crono's live foot for every frame of the span (dispatch
f438; approach lands (135,116) at rel ~46; the eight legs then walk the 30px diamond). Subtracting that
foot from each of the 142 captured whirl anchors leaves an offset that is **constant per orientation to
within a 4px band, for the whole window**:

| orientation | offset from the attacker's live foot | spread |
|---|---|---|
| O0 asm A upright | **(−31, −16)** | dx −33..−29, dy −18..−14 |
| O1 asm B upright | **(−28, −47)** | dx −30..−26, dy −49..−45 |
| O2 asm A 180 | **(+1, −45)** | dx −1..+3, dy −47..−43 |
| O3 asm B 180 | **(−2, −10)** | dx −4..0, dy −12..−8 |

Four points ringing him — the blades sweeping around Crono. This is decisive: a CENTRE-attached (or
victim-attached) effect would have swung ±30px as his foot walked the diamond; the residual ±2 is just the
blades' own 2px/frame step interleaving with his. The `battle_fx_streamer.md` O-B note ("a diamond loop
2px/frame ~15f edges around GATO's midsection") was describing **Crono's own orbit**, seen through the
effect.

**SHIPPED.** `tools/_gen_battlefx_whirl.py` now reads `$1D0C+0`/`$1D23+0` from the **same peek** as each OAM
capture (no timeline ambiguity — the earlier `--monitor` cross-check needed a −1 frame shift to align, the
peek's own WRAM does not) and bakes `CtWhirlFrame.x/y` as **signed offsets** from that foot;
`battleWhirlRender` draws at `mvLive[actAttacker] + offset`. The bake stays byte-verbatim — the same capture
data, re-expressed — so the sweep now follows Crono into any arena and stays in phase however long the route
takes. Re-pinned `field-battle-tech-cyclone-whirl-asma` / `-asmb-flip`.

**OPEN (analogous, NOT touched):** `battleIceRender` (`src/battle_fx_ice.h`) still replays ABSOLUTE anchors.
Marle is planted for Ice so nothing desyncs today, but its shards orbit the **target**, so the same
re-anchoring is needed against the victim's live foot before Ice is exercised in another arena — that
attachment has not been measured, so it stays absolute rather than guessed.

---

## ADDENDUM (2026-08-23) — THE SPIN'S 120 FRAMES: ops 0x11 / 0x99 / 0x9B are SLIDE-TO-STORED

User report (translated from Portuguese): *"the sound of crono's 'cyclone' tech is too short, it should repeat"*.

The spin super `CE/D61C` is eight legs of `49 1E ±0x1E` / `49 1F ±0x1E` followed by a
**move-to-stored** op — `11`, `99 01` or `9B 01` (§4 above). Those three opcodes had no case in
the anim-script interpreter, so they fell through to the default (no route effect) and cost
**nothing**: the whole super ran in 28 frames instead of ~147, and the `78 FF` at `CE/0930` that
STOPS the whirl sound arrived ~120 frames early. That is the entire report — the sound is not
short, it is cut.

They are the STORED-POINT twins of the resolved-point ops the port already had, and the ROM
shares the bodies literally:

```
C1/4FD1 (op 11)   LDA $5D9E / STA $A2B0 / LDA $5D9F / STA $A2B2 / LDA #01 / STA $8E
                  BRA ->C1/4FED          ; = op 0x12's own body, past its resolve
C1/4FE3 (op 12)   INY / LDA [$E6],Y / JSR C1/762E / LDA #02 / STA $8E   (falls into 4FED)
C1/672E (op 99)   STZ $8F / the same two loads / LDA #02 / STA $8E / JMP C1/62AA  ; op 0x98's body
C1/6743 (op 9A)   LDA #01 / STA $8F / JMP C1/629B                                 ; op 0x98 + $8F
C1/674A (op 9B)   LDA #01 / STA $8F / JMP C1/6730                                 ; op 0x99 + $8F
```

`$5D9E`/`$5D9F` are `$5D80[0x1E]`/`[0x1F]` — the pair op `33` captures and ops `49`/`3A` walk,
i.e. exactly the point op `1A` teleports to.

### Measured

`forest_cyclone.state = saves/battle_re/forest_holdleft_pre.state + $2837 |= 0x80`,
`--monitor "7E1E00:4" --writer --diff --input "40:60,0:275,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:400"`:

```
f440   7E1E00=00918004    the `78 91` at CE/D61D -- the whirl sound STARTS
f587   7E1E00=00FF8004    the `78 FF` at CE/0930 -- it STOPS
                          -> 147 frames
```

Ours, same fixture (`--battletest saves/battle_re/forest_cyclone.raw.wram`, `CT_BATTLE_TRACE`):

| | `78 91` | `78 FF` | span |
|---|---|---|---|
| before | f291 (rel 41) | f318 (rel 68) | **27** |
| after | f291 (rel 41) | f437 (rel 187) | **146** |

The eight legs now arm and park (`asc … SLIDE-STORED … n=14/11/15/19/15/11/15/19`), and the
downstream tracks that wait on the `36` sync move with them: `t8 track-end` rel 69 → 188, the
victim track's `SUPER 00` rel 71 → 190. The dossier's own oracle walk puts the whirl teardown
`80 12 F0` at **rel 201**; ours went from ~120 frames early to ~13.

### Not closed

- **`$8F`.** Ops `9A`/`9B` set it, `98`/`99` clear it, and nothing in this port models it — the
  shipped code already conflated `9A` with `98`, and `9B` is kept conflated with `99` for symmetry
  rather than guessing what the flag gates.
- **The legs are not uniform.** The ROM's are 30px/15 frames each; ours measure 14/11/15/19,
  because `battleAscSlideArm` computes the step count from the caster's LIVE position (which for a
  PC is the *bake's*, since `battleAscSetPos` deliberately refuses to move a PC — the route owns
  the motion) and that path is not the interpreter's own. The totals agree to one frame; the
  per-leg split does not, and closing it means deciding which of the two drivers owns a PC tech's
  position.
- **`field-battle-tech-cyclone-whirl-asma`'s frame** moved and was re-pinned. The diff bbox is
  GATO's body only — the 309 whirlwind pixels that check was byte-verified on are untouched, and
  the sibling `-whirl-asmb-flip` pin at rel 75 (now *inside* the spin) still passes. What changed
  is GATO's published idle pose, the pose engine's slot being contended for 120 more frames. That
  contention is the ROM's, but the frame was NOT re-verified pixel-for-pixel: the fixture
  `gato_cyclone.raw.wram` has no matching `.state` in the tree, and one rebuilt from
  `gato_press_action.state + $2837|=0x80` is a different capture moment, so `peek-f600` no longer
  lands on rel 161.

---

**V-2026-09-09 (the victim does not move).** `gato_cyclone.state` rebuilt (`$2837 |= 0x80` on `gato_press_action.state`),
the canonical btnscript: GATO's foot `$1D12/$1D29` = (88,71) is constant from f438 to f751, the whole 313-frame span.
The port used to throw him 48 px up at the strike -- a stale `animKnockDist` from his song's `A8 30`, carried into the
PC-tech path that never parses; fixed at the source (see `battle_enemy_move_target.md`, "the (135,37) trap").

---

**V-2026-09-13 (ONE DRIVER: the route is retired).** The "Not closed" item above ("closing it means deciding which of
the two drivers owns a PC tech's position") is decided by the ROM: C1/5135 and C1/6299 pick the moving combatant from
`$A014[track]` and nothing else, so the anim interpreter owns a PC tech's foot, facing and pose, and battleTechRouteParse
/ battleTechRouteTick are deleted. Forest, through the interpreter: approach `(160,71)->(120,63) n=10`, the eight legs
`n=15` each through (120,33)/(150,63)/(120,93)/(90,63) with the slide arms' C1/51A6 facings 0,1,2,0,3,1,2,0, return
`(90,63)->(160,71) n=17` facing 3 -- section 4.2 exactly; arms 1 frame early, the return 3 (spill frames of C1/40B0,
see sequence_table.md 9.29). GATO: the approach is 6 steps, not the ledger's 7, because the port's GATO foot is
(136,118) where the cartridge's `$1D12/$1D29` read (136,113) -- a staging difference outside this change. Pinned by
`field-battle-tech-cyclone-route` (rewritten) and `attract-demo-battle-xstrike-motion-vs-oracle`.
