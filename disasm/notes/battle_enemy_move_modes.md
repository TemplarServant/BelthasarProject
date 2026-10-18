# BATTLE — the enemy MOVEMENT-MODE class table (`CC/FBAB` → `C1/3760`) and the frozen Hetakes

**User report 3 (translated from Portuguese):** *"in battle against other hetake, they never attack and do not even move"* —
in the three-Hetake fight (Guardia Forest, loc 19, story 03, solo Crono) the other Hetakes never
attack and never move.

Scenario RAM: `$AF02 = 5E 5E 5E FF` (three Hetakes, enemy id `0x5E`), enemies = combatants
`cmb 3/4/5`, lead PC = obj2 (`$0197=04`), `$2980 = 00 80 80` (solo Crono).

---

## VERDICT (two halves, measured separately)

| half of the report | reproduced? | verdict |
|---|---|---|
| **"do not even move"** (never move) | **YES, deterministically** | **REAL BUG, byte-derived.** The Hetake's AI arms movement **mode `0x0E`**. The ROM maps mode → class through **`CC/FBAB`** and dispatches **`JSR ($3760,X)`**; `0x0E` → class **5** → handler **`C1/3BC2`**. Our `battleMoveTick` hard-codes `mvMode==1 → approach` / `mvMode==6 → return` and silently ignores every other mode, so the Hetakes never take a single walking step outside their own attack lunge. `src/battle.c:4167-4170`. |
| **"they never attack"** (never attack) | **NO — not reproduced as a turn-selection defect** | In the oracle **and** in our engine all three Hetakes take turns and all three land attacks. Frame-by-frame evidence in §D. The parent lane's lead ("`AE93=01` every time ⇒ the same seat acts forever") is a **misread**: `$AE93` is the attack **A/B selector** (`C1/AD02`, `LDA $AEE4 / STA $AE93`), not the acting-enemy index — see §E. What is genuinely wrong on the selector side is only the **direction of the `$B163` walk** (§C.3), which changes the turn ORDER, not who acts. |

The visible symptom the user reports is best explained by the movement half: in the ROM the two
idle Hetakes **wander continuously** across the arena between turns (measured, §B.3); in our engine
they are pinned to their staging coordinates for the whole battle, so the fight reads as "nobody
but one of them is doing anything".

---

## A — THE CANONICAL CHAIN (ROM, byte-cited end to end)

### A.1 The Hetake AI script arms mode `0x0E`

Pointer table `CC/8B08` (`ROM_AI_PTR_TABLE`, `src/battle.c:68`), id `0x5E`:
`CC/8B08 + 0x5E*2` → **`CC/9E36`** (file `0x0C9E36`). Raw bytes, split on the `FE` terminators:

```
17 10 00 00                 FE   ; conditions  (cond 0x17, the RNG re-roll)
00 00 06 04                 FE   ; block1 : op 00 MOVE-only, tgt 06 (nearest living PC), mode 04
00 00 00 00                 FE   ; block2 : op 00 MOVE-only, tgt 00,               mode 00
01 01 05 00  01 01 06 0E    FE   ; two ATTACK entries, 4 bytes each: `01 p1 tgt MODE`
                                 ;   01 01 05 00 -> p1=01 (attack-B), tgt 05 (random living PC), mode 00
                                 ;   01 01 06 0E -> p1=01,            tgt 06 (nearest living PC), MODE 0x0E
FF 00 00 00 00              FE
00 00 06 00                 FE
...
```

Both attack entries are visible live in our own trace as `AEE3=01,01,05` and `AEE3=01,01,06`
(`$AEE3`=op, `$AEE4`=p1, `$AEE5`=tgt) — see §D.2. The 4th byte is the **movement-mode suffix**
(the grammar of `battle_enemy_move_target.md` §A.5); the ROM handler `C1/983A` stores it
`$5E0D,X ← mode` (writer **C1:9833**) and the resolved target `$5E15,X ← $AECC` (writer **C1:9839**).

### A.2 The request → live mode latch

`$5E0D,slot` (request) → `$974A,slot` (live mode), writer **C1:336A** (the mode-change site
`C1/334x`). Oracle-confirmed in §B.2 (request at the span end, latch 3 frames later).

### A.3 The mode → class → handler dispatch (**the piece we do not have**)

Per-frame mode machine **`C1/3714`** (gated `LDA $99E0 / BNE skip`), per slot: `$974A,X`==0 → none;
`DEC $9897,X` reload `$989F,X` (=0x20) → **`C1/373B`**: refresh `$9752,X ← $5E15,X`, then

```
class = CC/FBAB[mode]          ; ROM bytes @ 0x0CFBAB, verified this session:
                               ;   00 01 01 01 00 01 04 02 03 03 06 06 06 05 05 05
JSR ($3760,X)                  ; C1/3760 handler table, ROM bytes @ 0x013760, verified:
                               ;   72 37  9A 37  BF 38  59 39  3D 3A  C2 3B  33 3E  5A 3F
```

Full expansion (mode → class → handler):

| mode | class | handler | ported? |
|---|---|---|---|
| 00 | 0 | C1/3772 (watch/idle — no motion) | effectively (we no-op) |
| 01 | 1 | **C1/379A APPROACH** | **yes** (`battleMoveApproach`) |
| 02 | 1 | C1/379A APPROACH | **no** (falls through) |
| 03 | 1 | C1/379A APPROACH | **no** |
| **04** | **0** | C1/3772 (watch/idle) | effectively (we no-op) — **correct by accident** |
| 05 | 1 | C1/379A APPROACH | **no** |
| 06 | 4 | **C1/3A3D RETURN/AWAY** | **yes** (`battleMoveReturn`) |
| 07 | 2 | C1/38BF | **no** |
| 08 | 3 | C1/3959 | **no** |
| 09 | 3 | C1/3959 | **no** |
| 0A–0C | 6 | C1/3E33 | **no** |
| 0D–**0E**–0F | **5** | **C1/3BC2** | **NO — this is the Hetake's mode** |

### A.4 What class 5 (`C1/3BC2`) is

Byte-diffed against the already-derived class-4 return handler `C1/3A3D`
(`battle_ai_interp.md` §9). The two are the **same routine shape** — the octant reverse-oscillation
"return dance" — differing only in the distance scalar and two tail calls:

```
C1/3A3D  (class 4, mode 06)                 C1/3BC2  (class 5, modes 0D/0E/0F)
  A6 9C     LDX $9C                           A6 9C     LDX $9C
  9E FD A5  STZ $A5FD,X                       9E FD A5  STZ $A5FD,X
  BD 91 97  LDA $9791,X   ; bearing latch     BD 91 97  LDA $9791,X
  F0 08     BEQ +8                            F0 08     BEQ +8
  BD 86 97  LDA $9786,X                       BD 86 97  LDA $9786,X
  85 DB     STA $DB                           85 DB     STA $DB
  4C F7 3A  JMP C1/3AF7                       4C 81 3C  JMP C1/3C81
  BD 52 97  LDA $9752,X   ; move target       BD 52 97  (identical)
  ...       target pos $1D0C,Y/$1D23,Y        ...
  ...       self pos $1D0F,X/$1D26,X          ...
            -> $D3/$D4, $D5/$D6, $98AF/$98B7            (identical)
  A9 40     LDA #$40                          A9 20     LDA #$20
  85 AE     STA $AE                           8D FC A5  STA $A5FC
  AD B5 A2  LDA $A2B5                         AD B5 A2  LDA $A2B5
  C9 10     CMP #$10                          C9 0E     CMP #$0E
  D0 04     BNE +4                            D0 03     BNE +3
  A9 80     LDA #$80                          0E FC A5  ASL $A5FC      ; 0x20 -> 0x40
  85 AE     STA $AE                           AD FC A5  LDA $A5FC
                                              85 AE     STA $AE
  20 22 02  JSR C1/0222   ; atan2             20 22 02  (identical)
  ... ref point -> $A5E4,X / $A5EC,X ...      ... identical ...
  ... quadrant C1/0118 + reverse $DB+=0x80    ... identical (byte-for-byte) ...
```

and in the tails (`C1/3AF7` vs `C1/3C81`, identical for the first 0x26 bytes: `$AE=8`, latch the
bearing into `$9786,X`, trig `C1/01F9` ×2, facing `$C0F700[bearing] → $96DE,X`):

```
C1/3AF7 tail:  AD B5 A2 C9 10 F0 3C  (skip block if $A2B5 == 0x10) ... JSR C1/2B13 ...
C1/3C81 tail:  (no $A2B5==0x10 skip)                                ... JSR C1/2B1F ...
               AD 72 98 30 03 4C 40 3D   ; $9872 BPL -> JMP C1/3D40
               A6 9C AD B5 A2 C9 0D D0 05 9E A7 ..  ; if $A2B5 == 0x0D -> STZ $98A7,..
```

**Net:** class 5 = the same reverse-oscillation wander as class 4 (mode-6 return), at **half the
reference distance** (`$AE = 0x20`, doubled to `0x40` iff `$A2B5 == 0x0E`), a different
distance/collision probe (`C1/2B1F` instead of `C1/2B13`), and an extra `$A2B5 == 0x0D` arm.
In the forest fight **`$A2B5 = 0x04`** (measured, §B.1) so neither variant fires: plain `$AE = 0x20`.

### A.5 The mover that executes the segments (already ported)

`CF/F98F`: phase 0..8, `$1D26,X = $98B7,X + trig(angle)·phase>>8` (writer CF:F9C5),
`$1D0F,X = $98AF,X + trig(angle+0x40)·phase>>8`; cadence `DEC $9819,X` reload `$9742,X` (=4).
Our `battleMoveTick` mover half (`src/battle.c:4174-4184`) already implements exactly this — only
the **handler that arms a segment for class 5** is missing.

---

## B — THE ORACLE (three Hetakes DO wander; frame numbers)

State: `saves/battle_re/forest_battle_3hetake.state` (loc 19, three Hetakes, timers `$AFAB[3..5]=13`).
Static reads: `$AEC7 = 0x0A`, `$989F = 20 20 20 …` (segment cadence), `$9742 = 04 04 04 …`
(mover cadence), **`$A2B5 = 0x04`**, `$B163 = 05 0A 04 00 09 06 07 08 03 01 FF`,
`$B158 = 48 66 69 78 78 78 …` (Hetake reload `0x78`).

### B.1 / B.2 The mode machine per frame

```
<probe> --monitor "974A:8,5E0D:8,5E15:8,98A7:8,A013:1,B18B:1" \
        --from saves/battle_re/forest_battle_3hetake.state --input "0:800" --diff
```

| frame | `$974A[0..2]` (live mode) | `$5E0D[0..2]` (request) | `$98A7[0..2]` (mover active) | note |
|---|---|---|---|---|
| f0 | `00 00 04` | `00 00 04` | `00 00 00` | slot2 already at mode 04 (class 0 = idle) |
| f48 | — | — | — | `$B18B=03` → **slot0 takes its turn**; f51 `$A013=1` (span) |
| **f228** | `00 00 04` | **`0E` 00 04** | `00 00 00` | span end → slot0 **requests mode 0x0E** |
| **f231** | **`0E` 00 04** | `0E 00 04` | `00 00 00` | latch (C1:336A), +3f |
| f242/f243 | `0E 00 04` | `0E 00 00` | — | `$B18B=05` → slot2's turn: **no span**, mode request 04→00 |
| f244 | `0E 00 00` | | | slot2 latch |
| f247/f250 | | | | `$B18B=04` → slot1's turn, span f250→f452 |
| **f452** | `0E 00 00` | `0E` **`0E`** `00` | | slot1's span end → **requests 0x0E** |
| **f455** | `0E 0E 00` | | | latch |
| **f461** | `0E 0E 00` | | **`01` 00 00** | **slot0's first wander segment fires** |
| f483 | | | `01 01 00` | slot1's first segment |
| f493/f501/f515/f523/f533/f555/f563/f581/f595… | | | toggling `01/00` per slot | segments chain (0x20 cadence) |

### B.3 The measured wander (the motion our engine has none of)

```
<probe> --monitor "1D0C:11,1D23:11,A013:1,B18B:1" \
        --from saves/battle_re/forest_battle_3hetake.state --input "0:700" --diff
```
`$1D0C+cmb` = live X, `$1D23+cmb` = live Y (`battle_enemy_move_target.md` §A.2).
Staging: PCs `A0 7F 8F` / `47 90 A0`; enemies `78 58 A8` / `3F 5F 7F`.

- **Inside the action span** (`$A013=1`, `$98A7` all zero) each acting Hetake performs its
  **attack lunge from the anim script**: slot0 f87→f100 X `78 → 94` (+28) then f134→f140 back to
  `78`; slot1 f286→f315 (`58,5F`) → (`94,4A`) then f349→f364 home. This half **is already ported**
  (`battleAscSetPos`, `src/battle.c:4673`) and works — see §D.3.
- **Outside the span** (`$A013=0`, class-5 handler running) both Hetakes that latched mode `0x0E`
  **drift continuously**:
  - slot0 (cmb3): f465→f635 X `78→6A`, Y `3F→32`; then reverses.
  - slot1 (cmb4): f489→f663 X `58→7E`, Y `5F→54`; then f665→f699 reverses X back, Y `54→32`.
  - slot2 (cmb5), still at mode `00`/`04` (class 0 = watch), stays byte-static at `(A8,7F)` —
    **the class table's own answer for "which enemy stands still"**, not a scene gate.

---

## C — OUR ENGINE

### C.1 The bug site — `battleMoveTick`, `src/battle.c:4167-4170`

```c
      if (h->mvPendAtk != cmb && !(h->mvMode[slot] & 0x80)) {
        if (h->mvMode[slot] == 1) battleMoveApproach(h, slot);        // <- 4168
        else if (h->mvMode[slot] == 6) battleMoveReturn(h, slot);     // <- 4169
      }
```
and, two lines below, `src/battle.c:4172`:
```c
    if (h->mvMode[slot] == 0 || (h->mvMode[slot] & 0x80)) continue; // none / halted
```

Only modes 1 and 6 are dispatched. **Grep proof that the class table does not exist in the port:**
`grep -rn "FBAB\|3BC2\|3E33\|38BF\|3959\|3F5A\|3772" src/` → **zero hits** (it appears only in
`disasm/notes/battle_enemy_move_target.md:96-99`).

**Consequence chain, verified:** the only writer of an ENEMY's live foot in the whole engine is
`battleMoveTick` (`src/battle.c:4182-4183`) plus the in-span anim-script setter
(`battleAscSetPos`, `src/battle.c:4659-4674`, gated `c == h->actAttacker`); every other
`mvLiveX/Y` assignment is `[0]` (Crono) or the PC walker. So a mode whose handler is missing ⇒ the
enemy is frozen at its staged foot forever (`field.c:3811` even carries the comment
*"walk displacement (0 if this Hetake never moves)"*).

### C.2 Our AI decode is NOT the problem

Our decoder already reads the 4th (mode) byte of an `01` attack entry
(`src/battle.c:1186`, `int mode = (len > 3) ? rom[q + 3] : 0;` → `battleMoveArm`) and the arm lands:
`CT_BATTLE_SCANTRACE=1` shows `mode=4` and then `mode=14` (0x0E) on all three enemy slots (§D.1).
So `mvMode` holds the canonical value; nothing consumes it.

### C.3 A second, smaller divergence: the `$B163` walk DIRECTION

The ready-scanner reader `C1/847E`:
```
1847E A2 0C 00   LDX #$000C
18481 BD 79 B1   LDA $B179,X      ; = $B185, the cursor
18484 D0 03      BNE +3
18486 AD C7 AE   LDA $AEC7        ; cursor 0 -> reload count (=0x0A here)
18489 3A         DEC A
1848A AA         TAX
1848B BD 63 B1   LDA $B163,X      ; index = cursor - 1
```
and the cursor is **INCREMENTED**, not decremented — the per-frame loop at `C1/82E5-8301`
(`LDY $00 / LDA $B179,Y / INC A / STA $B179,Y`, writers **C1:82EE** and **C1:82F9/8301**, wrap
`CMP #$0B / BCC keep / else 0`). Measured on the oracle (`--monitor "B185:1" --writer`):
`f0=04 f2=0A f8=00 f13=04 f18=0A f23=00 … f48=09`.

Our port walks it **downward** and indexes without the `-1`:
`src/battle.c:6345-6346`
```c
    if (--h->scanCur < 0) h->scanCur = h->scanTotal - 1;                 // cursor $B185: DEC, wrap to count-1
    int cmb = s[0xB163 + (h->scanCur % 11)];
```
Observable effect on this fight: the oracle cycles the three enemies **5 → 4 → 3** (turn frames
f48 s3, f242 s5, f247 s4, f731 s3, f936 s5, f1132 s4, …), ours cycles **3 → 4 → 5**
(f286 atk4, f503 atk5, f715 atk3, f1214 atk4, …). Same set, mirrored order. **Not** the user's bug,
but a real byte-level divergence; the exact per-frame increment RATE of `$B185` is left OPEN
(§F.2).

### C.4 The scanner's mode gate is also mode-keyed in the ROM (latent)

`C1/849E-84BB` (hand-decoded from `0x1849E`):
```
1849E BD AB AF   LDA $AFAB,X   / D0 49  BNE  -> not ready, skip
184A3 BD 0A 5E   LDA $5E0A,X   ; = $5E0D + slot : the MODE
184A6 F0 15      BEQ  ->184BD  ; mode 00       -> PASS
184A8 C9 04 F0 11  CMP #$04 BEQ ->184BD ; mode 04 -> PASS
184AC C9 06 F0 0D  CMP #$06 BEQ ->184BD ; mode 06 -> PASS
184B0 C9 0A 90 04  CMP #$0A BCC ->184B8 ; mode < 0A -> GATED
184B4 C9 11 90 05  CMP #$11 BCC ->184BD ; 0A..10   -> PASS
184B8 BD 26 98   LDA $9826,X  / F0 2F BEQ -> arrival latch clear -> SKIP the turn
184BD 9E 26 98   STZ $9826,X  ; turn start
```
So the `$9826` arrival gate applies to modes **1,2,3,5,7,8,9 and ≥0x11** — mode `0x0E` PASSES
freely, which is why the Hetakes still get turns in both engines. Our port
(`src/battle.c:6368`, `if (h->mvMode[slot] == 1 && !h->enArrive[slot])`) happens to agree for
modes 0/1/4/6/0x0E but is wrong for 2/3/5/7/8/9 — latent, no in-scope user.

---

## D — OURS, MEASURED (the "never attack" half does not reproduce)

### D.1 The armed modes

```
CT_BATTLE_SCANTRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  <scratch>/Belthasar.exe --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest o.raw --frames 2400 \
  --walkscript "l:400,." 2>&1 | grep "^scan" | grep -o "mode=[-0-9]*" | sort | uniq -c
    743 mode=-1     (PC rows)
   5617 mode=0
    125 mode=14     <- 0x0E  (all three enemy slots, from ~f919)
    213 mode=4
```
and
```
CT_BATTLE_MVTRACE=1 … --frames 2400 …      ->  ZERO "battlemove" lines
```
**No enemy ever executes a movement segment in 2400 frames.**

### D.2 Turn distribution — all three DO attack

```
CT_BATTLE_TRACE=1 … --frames 2400 --walkscript "l:400,." | grep -E "hitroll|act-end"
  f286  hitroll atk=4   AEE3=01,01,05   (span f286->f488, cronoHP 70->65)
  f503  hitroll atk=5   AEE3=01,01,05   (span f503->f705, 65->61)
  f715  hitroll atk=3   AEE3=01,01,05   (span f715->f917, 61->56)
  f1214 hitroll atk=4   AEE3=01,01,06
  f1431 hitroll atk=5   AEE3=01,01,06
  f1643 hitroll atk=3   AEE3=01,01,06
  f2140 enemy-noop id5E#1
  f2155 hitroll atk=5 ; f2367 hitroll atk=3 ; …
```
Oracle for comparison (`--monitor "B18B:1,A013:1,5E30:2" --input "0:4000" --diff`): turns at
f48 s3, f242 s5 (no span), f247 s4, f731 s3, f936 s5, f1132 s4, f1606 s3 (no span), f1621 s5,
f1815 s4 (no span), f2095 s3, f2300 s5, f2495 s4, f2969 s3, f3175 s5, f3370 s4 (no span),
f3650 s3, f3890 s5, f3895 s4. **Both engines cycle all three seats.**

`CT_BATTLE_MASHA=1` (Crono auto-attacks) gives `atk=4 @f466`, `atk=5 @f683` before they die —
again, more than one enemy attacks.

### D.3 The in-span attack lunge IS ported and works

```
CT_BATTLE_ASCTRACE=1 … --frames 560 …
  asc arm f307 sel=14 mask=9001 tracks=3 actor=4 victim=0 home=(88,95)
  asc f321 rel=35 t0 SLIDE (88,95)->(148,73) speed=2 n=30
  asc f384 rel=98 t0 SLIDE (148,73)->(88,95) speed=4 n=15
  asc arm f524 … actor=5 …
```
So the ACTING Hetake lunges; the two IDLE ones are what stand frozen.

---

## E — `$AE93` is NOT the acting-enemy selector (the lead was a misread)

`$AE93` = the attack action's own **p1**, copied by `C1/AD02` (`LDA $AEE4 / STA $AE93`, writer
**C1:AD08**), and read at anim dispatch `C1/44AF` (`LDA $AE93 / BNE →44E0`) to pick the
attack-**B** table `CD/4F26` vs attack-**A** `CD/4926` (already pinned in
`battle_interp_verify.md` T1.C and `battle_confirm_queue.md` B.4). The Hetake script's two attack
entries both carry `p1 = 01`, which is exactly why our trace prints `AE93=01` on every
`enemy-arrive`. The acting combatant is **`$B18B`** (`C1/BCE1 LDA $B18B / TAX`), and our engine's
equivalent is `h->actAttacker` / the `atk=` field of the `hitroll` line — both of which vary.

---

## F — WHAT TO CHANGE (minimal, mandate-safe; **not applied — read-only lane**)

**Exact site:** `src/battle.c:4167-4172`, inside `battleMoveTick`.

**Change:** replace the two hard-coded `mvMode == 1 / == 6` comparisons with the ROM's own
**mode → class** lookup and a class-keyed dispatch, i.e. read `rom[0x0CFBAB + (mode & 0x0F)]` and
switch on the CLASS, so that

- class 0 (`C1/3772`, modes 00/04) → no segment armed (today's fall-through, now explicit);
- class 1 (`C1/379A`, modes 01/02/03/05) → `battleMoveApproach` (widened from `mode==1` only);
- class 4 (`C1/3A3D`, mode 06) → `battleMoveReturn` (unchanged);
- class 5 (`C1/3BC2`, modes 0D/0E/0F) → **the new handler** — the same reverse-oscillation body as
  `battleMoveReturn` with the reference distance `$AE = 0x20` (`0x40` iff `$A2B5 == 0x0E`) instead
  of class 4's `0x40`/`0x80`, plus the `C1/2B1F` probe and the `$A2B5 == 0x0D` arm of `C1/3C81`;
- classes 2/3/6 (`C1/38BF` / `C1/3959` / `C1/3E33`) → **leave unhandled and say so** (no enemy in
  scope arms modes 07/08/09/0A-0C; do NOT invent bodies for them).

Because `battleMoveReturn` is already the ported class-4 dance, the smallest honest first step is:
**(i)** add the `CC/FBAB` table read + class switch (removes the mode-keyed hard-coding, fixes
modes 02/03/05 for free), and **(ii)** route class 5 to a `battleMoveReturn` variant parameterised
on the reference distance (`0x20` vs `0x40`) — then validate the wander geometry of §B.3 against
the oracle before claiming class 5 is byte-exact, since `C1/3C81`'s tail is *not* identical to
`C1/3AF7`'s.

The scanner/actor-selection site (`src/battle.c:6344-6390`) needs **no change for this report**;
the `$B163` walk-direction divergence of §C.3 is a separate, smaller ticket.

**Do not** gate anything on the enemy id, the slot index, or the scene. The Hetake is frozen
because of a missing *class*, and GATO walks only because his modes happen to be the two we ported.

---

## G — EXACT REPRO COMMANDS (cwd = repo root; binaries copied to a scratch dir)

```
# --- setup
mkdir -p "$TMP/ctwf3/idle"
cp build/ct.exe "$TMP/ctwf3/idle/ct_probe.exe"; cp build/Belthasar.exe "$TMP/ctwf3/idle/"; cp build/SDL2.dll "$TMP/ctwf3/idle/"

# --- ORACLE: the mode machine (request -> latch -> segments)
"$TMP/ctwf3/idle/ct_probe.exe" --monitor "974A:8,5E0D:8,5E15:8,98A7:8,A013:1,B18B:1" \
  --from saves/battle_re/forest_battle_3hetake.state --input "0:800" --diff
#   f228 $5E0D[0]=0E ; f231 $974A[0]=0E ; f452/f455 slot1 ; f461+ $98A7 segments

# --- ORACLE: the wander itself
"$TMP/ctwf3/idle/ct_probe.exe" --monitor "1D0C:11,1D23:11,A013:1,B18B:1" \
  --from saves/battle_re/forest_battle_3hetake.state --input "0:700" --diff
#   f465-f699: cmb3 X 78->6A, cmb4 X 58->7E->6A ; cmb5 (mode 04) static at (A8,7F)

# --- ORACLE: turn distribution (all three act)
"$TMP/ctwf3/idle/ct_probe.exe" --monitor "B18B:1,A013:1,5E30:2" \
  --from saves/battle_re/forest_battle_3hetake.state --input "0:4000" --diff

# --- ORACLE: the $B185 cursor is INCREMENTED
"$TMP/ctwf3/idle/ct_probe.exe" --monitor "B185:1" \
  --from saves/battle_re/forest_battle_3hetake.state --input "0:60" --diff --writer
#   writers C1:82EE / C1:8301

# --- ROM tables
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read(); print(' '.join('%02X'%b for b in d[0x0CFBAB:0x0CFBBB])); print(' '.join('%02X'%b for b in d[0x013760:0x013772]))"
#   00 01 01 01 00 01 04 02 03 03 06 06 06 05 05 05
#   72 37 9A 37 BF 38 59 39 3D 3A C2 3B 33 3E 5A 3F
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read(); p=0x0C8B08+0x5E*2; b=0x0C0000|(d[p]|(d[p+1]<<8)); print(hex(b)); print(' '.join('%02X'%x for x in d[b:b+48]))"
#   0xc9e36 : 17 10 00 00 FE | 00 00 06 04 FE | 00 00 00 00 FE | 01 01 05 00 01 01 06 0E FE | ...
python tools/dis.py 13BC2 0xA0 0 0     # class-5 handler C1/3BC2 (dis desyncs on the M/X flips -- hand-decode)
python tools/dis.py 13A3D 0x50 0 0     # class-4 handler C1/3A3D for the diff
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read(); print(' '.join('%02X'%b for b in d[0x1847E:0x184F0]))"   # ready scanner
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read(); print(' '.join('%02X'%b for b in d[0x182C0:0x18320]))"   # the $B185 increment loop

# --- OURS: the modes get armed but nothing moves
CT_BATTLE_SCANTRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$TMP/ctwf3/idle/Belthasar.exe" \
  --assets ct_assets.dat --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram \
  --selftest o.raw --frames 2400 --walkscript "l:400,." 2>&1 | grep "^scan" | grep -o "mode=[-0-9]*" | sort | uniq -c
CT_BATTLE_MVTRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$TMP/ctwf3/idle/Belthasar.exe" \
  --assets ct_assets.dat --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram \
  --selftest o.raw --frames 2400 --walkscript "l:400,." 2>&1 | grep battlemove | wc -l    # -> 0

# --- OURS: all three seats attack (the "never attack" half does not reproduce)
CT_BATTLE_TRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$TMP/ctwf3/idle/Belthasar.exe" \
  --assets ct_assets.dat --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram \
  --selftest o.raw --frames 2400 --walkscript "l:400,." 2>&1 | grep -E "^hitroll"
```

⚠ `--battletest saves/forest_re/battle_crono_solo.raw.wram` **fails staging** for this fixture
(`$29BF` slot records all-zero, `$2989=00`, `$AF0D` empty) — that WRAM is the *pre*-battle,
control-free moment. Use the field route above (`--loadwram … --walkscript "l:400,."`), which stages
correctly (`AF02=5E5E5EFF` in the trace). `saves/forest_re/batalha_nao_comeca.raw.wram` **does**
work with `--battletest`, but it stages a *different* encounter (`AF02=FF8D8DFF`, two id-0x8D
enemies) — do not use it for the three-Hetake report.

---

## H — OPEN QUESTIONS

1. **Class-5's exact geometry is not yet byte-modelled.** §A.4 pins the shape and the distance
   scalar by byte-diff against class 4, but the tails `C1/3AF7` vs `C1/3C81` diverge from
   `0x3B1D`/`0x3CA7` on (different probe `C1/2B13` vs `C1/2B1F`, an extra `$A2B5==0x0D` arm, a
   `JMP C1/3D40`). Those two sub-routines were **not** disassembled here. Before pinning a port,
   reproduce the §B.3 drift trace (`1D0C`/`1D23`) segment-for-segment.
2. **The `$B185` advance rate.** The writers (`C1:82EE`, `C1:82F9/8301`) and the direction
   (INC, wrap `0x0B`→0) are byte-certain; the *cadence* is not — the observed frame samples
   (`04 @f0, 0A @f2, 00 @f8, 04 @f13, 0A @f18, 00 @f23`) are not a flat 1/frame, and the
   `LDA $0000,X / BIT #$80` branch at `C1/82D0` selecting between two increment arms (one of which
   also ORs `$AEC3`) was not decoded. Our port's flat "1 entry per battle tick" model
   (`battle_ai_interp.md` §7) is therefore still a MODEL, not a measurement.
3. **Classes 2/3/6** (`C1/38BF`, `C1/3959`, `C1/3E33`) are untouched — no enemy in scope arms modes
   07/08/09/0A-0C. Do not synthesise them.
4. **`$A2B5`** = 0x04 in the forest fight; its meaning (arena/formation variant?) is not derived —
   only that class 4 tests it against `0x10` and class 5 against `0x0E`/`0x0D`.
5. **The user's "they never attack".** Not reproduced as a turn-selection defect in either engine
   (§D.2). Either it is the perceptual consequence of the frozen wander (most likely), or it needs
   an interactive capture from the user's own build — reported here as *"could not reproduce under
   the headless forest scenario"*, never as *"absent"*
   ([[verify-negatives-on-proper-state]]).

---

# Adversarial review (independent lane, read-only, 2026-08-22)

Every ROM address below was re-disassembled from `Chrono_Trigger_USA_SNES.sfc` in this lane, and
every oracle number was re-measured on a state this lane loaded itself, with a private copy of
`build/ct.exe`. Default verdict was "not proven".

**Bottom line: the ROOT CAUSE is correct and reproduces. The DERIVATION carries a wrong headline
constant, and the PROPOSED FIX contains an invented mask that would delete a live, already-working
case. Do not implement §F as written.**

---

## R0 — What I independently CONFIRMED

| claim | verdict |
|---|---|
| `CC/FBAB @0x0CFBAB = 00 01 01 01 00 01 04 02 03 03 06 06 06 05 05 05` | **CONFIRMED** byte-for-byte |
| `C1/3760 @0x013760 = 72 37 9A 37 BF 38 59 39 3D 3A C2 3B 33 3E 5A 3F` | **CONFIRMED**; and the table is **9 entries** (`… 21 40` = class 8 -> `C1/4021`), ending at `0x13772` where the class-0 handler `C1/3772` begins |
| Hetake `0x5E` AI script `CC/8B08+0x5E*2 -> CC/9E36`, attack entry `01 01 06 0E` | **CONFIRMED** |
| `C1/3BC2` vs `C1/3A3D` byte-diff (the `$AE` block, `JMP 3C81` vs `JMP 3AF7`) | **CONFIRMED**; realigned at `JSR $0222` the two bodies are **byte-identical for 0xA1 bytes** (`C1/3A7C..3B1C` = `C1/3C06..3CA6`), diverging exactly at the `$A2B5 == 0x10` skip |
| scanner mode gate `C1/849E-84BB` | **CONFIRMED** (disasm reproduced instruction for instruction) |
| reader `C1/847E`, index = cursor-1 | **CONFIRMED** (`A2 0C 00 / BD 79 B1 / D0 03 / AD C7 AE / 3A / AA / BD 63 B1`) |
| `$B185` cursor is **INC**remented, wrap `CMP #$0B` | **CONFIRMED** in ROM and on the oracle (writers `C1:82EE` / `C1:8301`, f0=04 f2=0A f8=00 f13=04 …) |
| oracle: the two mode-`0x0E` Hetakes wander, cmb5 (mode 00/04) is byte-static at `(A8,7F)` | **CONFIRMED** (see R7 for the numbers) |
| ours: `mode=14` (0x0E) armed on all enemy slots, **ZERO** `battlemove` lines in 2400 f | **CONFIRMED** exactly (`743 mode=-1 / 5617 mode=0 / 125 mode=14 / 213 mode=4`, `grep -c battlemove` = 0) |

---

## R1 — REFUTED: `$A2B5` is NOT a scenario constant. It is the mode being dispatched.

The dossier's §A.3 transcription of `C1/373B` **omits the decisive instruction**. Full hand-decode
of `0x1373B` (raw `[27 ROM bytes at 0x1373B: python tools/dis.py 0x1373B 27]
BF AB FB CC 0A AA FC 60 37 60`):

```
1373B A6 9C        LDX $9C
1373D BD F8 96     LDA $96F8,X        ; per-slot dispatch gate
13740 F0 1D        BEQ ->1375F (RTS)
13742 BD 15 5E     LDA $5E15,X
13745 DD 52 97     CMP $9752,X
13748 F0 03        BEQ ->1374D
1374A 9D 52 97     STA $9752,X        ; target refresh
1374D BD 4A 97     LDA $974A,X        ; the LIVE MODE
13750 30 0D        BMI ->1375F        ; bit7 set (FF = empty slot) -> skip
13752 8D B5 A2     STA $A2B5          ; <<<<<<  $A2B5 := THE MODE BEING DISPATCHED
13755 AA           TAX
13756 BF AB FB CC  LDA $CCFBAB,X      ; class = table[mode]   (NO MASK)
1375A 0A           ASL A
1375B AA           TAX
1375C FC 60 37     JSR ($3760,X)
1375F 60           RTS
```

`$A2B5` has **exactly one writer in the whole 4 MB ROM**:

```
python -c "import re;d=open('Chrono_Trigger_USA_SNES.sfc','rb').read();print([hex(m.start()) for m in re.finditer(re.escape(b'\x8d\xb5\xa2'),d)])"
#   ['0x13752']        (STZ $A2B5: none)
```

Oracle, my own run, forest three-Hetake state:

```
<probe> --monitor "A2B5:1,974A:8,98A7:8" --from saves/battle_re/forest_battle_3hetake.state --input "0:800" --diff
  f0    A2B5=04   974A=000004FFFFFFFFFF   98A7=00...
  f455  A2B5=04   974A=0E0E00FFFFFFFFFF   98A7=00...
  f461  A2B5=0E*  974A=0E0E00FFFFFFFFFF   98A7=0100000000000000*   <-- flips to 0E the frame class 5 first runs
<probe> --monitor "A2B5:1" ... --diff --writer
  f0 04 ; f461 0E*(C1:3755)
```

and, decisively, in the id-`0x8D` fight (`saves/batalha_nao_comeca_lake.state`, `--input "0:1400"`),
`$A2B5` **alternates frame by frame** with which slot is dispatched:

```
f645  974A=FF1001FFFFFFFFFF   A2B5=01
f675  974A=FF1001FFFFFFFFFF   A2B5=10*
f676  974A=FF1001FFFFFFFFFF   A2B5=01*
f707  A2B5=10*   f708 A2B5=01*   f741 A2B5=10*   f742 A2B5=01*   ...
```

**Consequences that overturn the dossier:**

1. §A.4's *"In the forest fight `$A2B5 = 0x04` (measured, §B.1) so neither variant fires: plain
   `$AE = 0x20`"* is **WRONG**. During a class-5 dispatch of mode `0x0E`, `$A2B5 == 0x0E` by
   construction, the `CMP #$0E / D0 03 / ASL $A5FC` **fires**, and the Hetake's reference radius is
   **`$AE = 0x40`** — twice what the dossier tells the implementer to use. The `0x04` in §B.1 was a
   frame-end sample taken before f461, i.e. the last mode dispatched that frame, not a scenario
   constant.
2. The `CMP` constants are now legible as what they are: **within-class mode discriminators.**
   Class 4 covers modes `06` and `0x10`; `CMP #$10` selects mode `0x10` -> `$AE = 0x80`.
   Class 5 covers modes `0D/0E/0F`; `CMP #$0E` selects mode `0x0E` -> `$AE = 0x40`; `0D`/`0F` keep
   `0x20`. `C1/3C81`'s extra `$A2B5 == 0x0D` arm is the mode-`0x0D` case. Nothing is an "arena
   variant".
3. **Open question H.4 is closed**, not open.

---

## R2 — REFUTED: `rom[0x0CFBAB + (mode & 0x0F)]` in §F is an INVENTED MASK, and it regresses a live case.

The ROM does **no** masking (`13752 STA $A2B5 / TAX / LDA $CCFBAB,X` — the raw mode indexes the
table; only bit 7 is filtered, by the `BMI` at `13750`). The table does not stop at 16 entries:

```
0x0CFBAB+00: 00 01 01 01 00 01 04 02 03 03 06 06 06 05 05 05
0x0CFBAB+10: 04 07 07 07 07 07 07 07 07 08 00 02 01 01 02 02
```

`table[0x10] = 04` -> **class 4**, i.e. the *doubled-distance* return. Under `mode & 0x0F` it would
become `table[0x00] = 00` -> class 0 -> **no motion at all**.

Mode `0x10` is not hypothetical:

* ROM — enemy id `0x8D`, script `CC/8B08+0x8D*2 -> CC/A68E`:
  `00 00 00 00 FE | 01 00 06 01  01 01 06 10  00 00 06 04 FE | ...` — the second attack entry arms
  **mode `0x10`**.
* Oracle — `saves/batalha_nao_comeca_lake.state`, `--monitor "AF02:4,974A:8,A2B5:1,98A7:8" --input "0:1400" --diff`:
  `f171 AF02=FF8D8DFF`, `f645 974A=FF`**`10`**`01FF...`, `f995 974A=FF`**`1010`**`FF...`.

Also note the scanner gate's own `CMP #$11 / BCC pass` (`C1/84B4`) presumes modes up to `0x10`
exist — a `& 0x0F` mask contradicts the ROM the dossier itself quotes in §C.4.

---

## R3 — REFUTED: "class 4 (mode 06) -> `battleMoveReturn` (unchanged)". Class 4 is modes 06 **and 0x10**, and mode 0x10 is BROKEN TODAY.

`table[0x06] = 04` and `table[0x10] = 04`. Our `battleMoveTick` matches on the *mode*, not the
class, so mode `0x10` falls through the same hole as `0x0E`. Measured in our engine, same snapshot
binary, on the id-`0x8D` fixture:

```
CT_BATTLE_MVTRACE=1 CT_BATTLE_SCANTRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  <scratch>/Belthasar.exe --assets ct_assets.dat --scene 19 \
  --battletest saves/forest_re/batalha_nao_comeca.raw.wram
    scan modes:  66 mode=-1 | 396 mode=0 | 12 mode=1 | 72 mode=16 (=0x10) | 48 mode=4
    battlemove:  32 lines -- ALL of them "mode1"; first f204, last f273.
```

72 samples of mode `0x10` produce **zero** movement. So this report's bug is one step wider than it
claims: it is not "the Hetake's class 5 is missing", it is **"the engine dispatches on the MODE
instead of the CLASS"**, and that already breaks a handler we have shipped.

---

## R4 — REFUTED: "no enemy in scope arms modes 07/08/09/0A-0C. Do not synthesise them." Class 6 is in scope, in a PINNED fixture.

Enemy id **`0x19`**, AI script `CC/8B08+0x19*2 -> CC/9255`:

```
17 20 00 00 FE | 02 1E 06 00 00 08 FE | 00 00 00 00 FE |
00 00 06 0F  00 00 06 0A FE | FF 00 00 00 00 FE | 00 00 06 00 FE |
FF 00 00 00 00 FE | 00 00 06 0E  00 00 06 0E  02 03 06 00 00 00 FE | ...
```

`00 00 06 0A` = a move-only entry arming **mode `0x0A`** -> `CC/FBAB[0x0A] = 06` -> **class 6,
`C1/3E33`** — one of the three the dossier says are out of scope. Id `0x19` is staged as enemy #5
in `saves/battle_re/ambush_pre.raw.wram`, which is the fixture of the **pinned** check
`field-battle-ambush-generic-dissolve`:

```
CT_BATTLE_SCANTRACE=1 ... --battletest saves/battle_re/ambush_pre.raw.wram
    cmb8 mode10   (0x0A -> class 6)
    cmb8 mode15   (0x0F -> class 5, WITHOUT the 0x0E ASL -> $AE = 0x20)
CT_BATTLE_TRACE=1 ... | grep enemy-noop   ->  "enemy-noop id19#5"
```

The same run also shows mode `0x0F`, which is the case where the dossier's `$AE = 0x20` happens to
be right — for the wrong reason.

---

## R5 — Open question H.1 is CLOSABLE: the two "probes" differ only by a 16-bit threshold constant.

`C1/2B13` and `C1/2B1F` are two entry points of one routine (raw `0x12B13`:
`A9 10 8D 93 98 A9 00 8D 94 98 80 2A | A9 00 8D 93 98 A9 01 8D 94 98 80 1E | ...`):

```
C1/2B13 (class 4):  LDA #$10 / STA $9893 / LDA #$00 / STA $9894 / BRA C1/2B49   ; threshold = $0010
C1/2B1F (class 5):  LDA #$00 / STA $9893 / LDA #$01 / STA $9894 / BRA C1/2B49   ; threshold = $0100
C1/2B49:  |dx| , |dy|  (SEC SBC / BPL / EOR #FFFF / INC)  -> squares via JSR $0089
          -> sum in $AF -> C1/2BA5  CMP $9893  (16-bit, after REP #$21) / BEQ / BCC
```
`CMP $9893` exists at exactly one site (`0x12BA5`); `$9893` is written only by these entry stubs
(`0x12AE5/0x12AF1/0x12AFD/0x12B09/0x12B15/0x12B21/0x12B2D/0x12B39`).

**So class 5's settle test is `dist² <= 0x0100` (16 px), not class 4's `dist² <= 0x0010` (4 px).**
Our port hard-codes `0x10` at `src/battle.c:4111`. That constant is now derived, not guessable —
without it an implementer would have had to invent it.

---

## R6 — MISSED BUG (and a live mandate violation already in the tree): `src/battle.c` branches on a WRAM byte the engine never writes.

```
src/battle.c:4079   int scale = (s[0xA2B5] == 0x10) ? 0x80 : 0x40;   // "double-distance arena variant"
src/battle.c:4111   if (dx*dx + dy*dy <= 0x10 && s[0xA2B5] != 0x10) { ... }
src/battle.c:4126   if (s[0xA2B5] != 0x10) { ... both deflectors ... }
```
`grep -rn "A2B5" src/` returns **reads only — no writer anywhere in the engine.** The value is
whatever the loaded WRAM happened to hold, and every fixture in the tree holds the same thing:

```
gato_t0.raw.wram              $A2B5 = 80
forest_pre.raw.wram           $A2B5 = 80
battle_crono_solo.raw.wram    $A2B5 = 80
batalha_nao_comeca_lake.state $A2B5 = 80   (--read)
```

so `s[0xA2B5] == 0x10` is **permanently false**. Since the ROM's `$A2B5` *is* the dispatched mode
(R1), the canonical predicate is `h->mvMode[slot] == 0x10` — and today mode `0x10` silently takes
class 4's *mode-06* arm: reference radius `0x40` instead of `0x80`, plus the settle test and both
deflectors that `C1/3B1D` explicitly skips. This is exactly the forbidden pattern "a heuristic
standing in where the ROM branches on a RAM flag", and §F would enshrine it by keeping the
`$A2B5` reads.

---

## R7 — §B.3's drift numbers do not reproduce as written (and §F tells the implementer to validate against them).

My run, same state, same probe:

```
<probe> --monitor "1D0C:11,1D23:11,A013:1,B18B:1,96F8:8" \
        --from saves/battle_re/forest_battle_3hetake.state --input "0:700" --diff
```
* cmb3 X: `78` -> `77` (f465) -> `70` (f493), **holds at 70 until f583**, -> `6A` (f613). Y `3F`->`3A`.
* cmb4 X: `58` -> `59` (f491) -> `71` (f623). Y `5F` -> `54` (f595). It does **not** reach `7E`
  inside 700 frames.
* cmb5 (mode 00/04, class 0): byte-static at `(A8,7F)`. **CONFIRMED.**

The direction and the fact of the wander are right; the specific waypoints in §B.3 / §F ("cmb3
78->6A f465-f635", "cmb4 58->7E->6A f489-f699") are not what the oracle prints and must be
re-measured before being used as an acceptance target.

Incidental: `$96F8 = 01 01 01 00 00 ...` for the three enemy slots — that is the ROM's own per-slot
dispatch gate at `0x1373D`, which our port substitutes with `battlePresent(s, cmb)` (uncited).

---

## R8 — Claims left standing but still UNPROVEN

* `src/battle.c:4160` `if (s[0xA013] || s[0x99E0]) return;` — `C1/3714` gates on **`$99E0` only**
  (`AD E0 99 / D0 21`). Its two call sites (`C1/10B5`, `C1/10DC`, the only `20 14 37` in bank C1)
  show no local `$A013` test. The `$A013` half of our gate is consistent with the oracle
  (`$98A7` stays 0 through every span) but is not cited to a branch.
* `C1/373B`'s `$9752,X <- $5E15,X` refresh is not ported (`grep -n "9752\|5E15" src/battle.c` finds
  it only in comments; `mvTarget` is written once, at `src/battle.c:3976`). Harmless only while
  `$5E15` cannot change between arms — unverified.
* `$B185` advance cadence (dossier H.2) — my own `--writer` run reproduces the non-flat samples
  (`f0=04 f2=0A f8=00 f13=04 f18=0A f23=00 f48=09`), so H.2 genuinely stands open.
* "they never attack" — I did not re-run the 4000-frame turn census, so §D.2's *"not reproduced"*
  stands as reported, i.e. "not reproduced under the headless forest scenario", not "absent".

---

## R9 — The corrected minimal change (still NOT applied; read-only lane)

Land in this order. Steps 1 and 2 introduce **no new geometry** and by themselves repair class-4
mode `0x10`.

1. **`src/battle.c:4079 / 4111 / 4126`** — replace `s[0xA2B5]` with `h->mvMode[slot]`. Byte-exact:
   the ROM stores the mode into `$A2B5` at `C1:3755` immediately before entering the handler
   (single writer in the ROM), so the two are the same value by construction. Removes a read of a
   never-written WRAM byte.
2. **`src/battle.c:4167-4172`** — dispatch on the CLASS, not the mode:
   `class = rom[0x0CFBAB + mode]` with the **raw** mode (bit 7 already excluded by the existing
   `& 0x80` test, matching `BMI` at `0x13750`) — **no `& 0x0F`** — then
   `class 0 -> nothing (C1/3772)`, `class 1 -> battleMoveApproach (C1/379A; modes 01/02/03/05)`,
   `class 4 -> battleMoveReturn (C1/3A3D; modes 06 AND 0x10)`.
3. **Class 5 (`C1/3BC2`; modes 0D/0E/0F)** — `battleMoveReturn` parameterised by three derived
   values, not by a scene or an id: reference radius `$AE` = `0x40` iff `mode == 0x0E` else `0x20`
   (`C1/3BF2-3C05`); settle threshold `0x0100` (`C1/2B1F`, R5) instead of class 4's `0x0010`; and
   **no** `$A2B5 == 0x10` skip, i.e. the settle test and both deflectors always run
   (`C1/3C81` lacks `C1/3B1D`'s `AD B5 A2 / C9 10 / F0 3C`). The `$A2B5 == 0x0D` arm at
   `C1/3CC8` and the `$9872` / `JMP C1/3D40` branch are still **not** decoded — derive them or
   state plainly that mode `0x0D` is unhandled.
4. **Class 6 (`C1/3E33`)** must be derived before `ambush_pre` can be right (R4). Until then, leave
   it explicitly unhandled with the id-`0x19` citation in the comment. **Do not synthesise it.**

Forbidden here as everywhere: `if (id == 0x5E)`, `if (id == 0x19)`, a slot or scene gate, or
"make idle enemies wiggle".

---

## R10 — Pinned checks in `tools/regress_checks.json` that would move (read, not run)

**Near-certain movers** (their pinned value is a direct function of an enemy's live X/Y):

| check | why |
|---|---|
| `field-battle-enemy-death-sfx` | pins `pan=78`, `pan=58`, `pan=A8` — those are literally cmb3/4/5's *staged* X. Wandering Hetakes change the pan byte. |
| `field-battle-forest-lastkill-dissolve` | framebuffer fnv at `death#3+18`, late in the fight, after many class-5 segments. |
| `field-battle-tech-cyclone-route` | pins `techroute-snap f450 ... approach=(120,63) ... home=(160,71)` — computed from live enemy positions. |
| `field-battle-tgtcursor-cyclone-multi` / `field-battle-dissolve-serial` | fnv at f234 / `death#1+25`, cursor and dissolve drawn over the enemies. |
| `field-battle-ambush-generic-dissolve` | the id-`0x19` fixture (R4): classes 5 **and** 6 newly arm; pins both `battleshot f362` and `battleloop: victory f2051`. |

**Likely movers** (forest kill order / victory frame / late poses): `field-battleloop-forest`
(`victory f1176`), `field-battle-hetake-return-pose` (`enemy-arrive#1+110`),
`field-battle-hetake-attack-pose` (`+75`), `field-battle-hetake-pose-ladder`,
`field-battle-hetake-attack-sfx` (`pan=80`), `field-battle-results-reward`,
`field-battle-drop-roll`, `field-battle-levelup-growth`, `field-battle-tech-learn`,
`field-battle-tech-cyclone-multi`, `field-battle-tech-cyclone-fx`,
`field-battle-solo-victory-no-marle`, `field-battle-end-uiteardown`.

**Must NOT move — treat any change as a regression alarm:**

* GATO (id `0x92`, `CC/A77B` = `01 00 05 01 | 00 00 06 06 | 02 10 05 ...`) arms **only** modes `01`
  and `06`, both already routed. `field-battle-gato-walk`, `field-battle-ai-cursor-cadence`,
  `field-battle-gato-live-pose-and-foot`, `field-battletest-gato` must be byte-identical after
  steps 1-2. If `field-battle-gato-walk` moves, the class table was masked or `$A2B5` was
  mis-substituted.
* Forest shots taken **before** the first mode latch (~f919 in our engine):
  `field-battle-forest-camera-composite` (f90), `field-battle-pc-approach-early/late` (f172/f196),
  `field-battle-crit-leap-mid` (f223), `field-battle-crit-beatB-dead-suppressed` (f289),
  `field-battle-forest-seek-glide` (f34).

---

## R11 — Exact commands this lane ran (cwd = repo root; binaries snapshotted, nothing written into `build/`)

```
mkdir -p "$TMP/ctwf3/idle-adv"
cp build/ct.exe "$TMP/ctwf3/idle-adv/ct_probe.exe"; cp build/Belthasar.exe "$TMP/ctwf3/idle-adv/"; cp build/SDL2.dll "$TMP/ctwf3/idle-adv/"

# --- ROM
python tools/dis.py 13714 0x60 0 0       # the mode machine + C1/373B  (note the STA $A2B5 the dossier drops)
python tools/dis.py 1847E 0x50 1 1       # the ready scanner
python -c "import re;d=open('Chrono_Trigger_USA_SNES.sfc','rb').read();print([hex(m.start()) for m in re.finditer(re.escape(b'\x8d\xb5\xa2'),d)])"
#   ['0x13752']  -- the ONLY writer of $A2B5 in the ROM
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read();print(' '.join('%02X'%b for b in d[0x0CFBAB:0x0CFBAB+0x20]))"
#   ... table[0x10] = 04, so `mode & 0x0F` is wrong
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read()
for e in (0x92,0x5E,0x8D,0x19):
    p=0x0C8B08+e*2;b=0x0C0000|(d[p]|(d[p+1]<<8));print('%02X->%06X'%(e,b),' '.join('%02X'%x for x in d[b:b+40]))"
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read();print(' '.join('%02X'%b for b in d[0x12B13:0x12BB0]))"
#   the $9893/$9894 threshold stubs + the CMP $9893 at 0x12BA5

# --- ORACLE
<probe> --monitor "A2B5:1,974A:8,98A7:8" --from saves/battle_re/forest_battle_3hetake.state --input "0:800" --diff
<probe> --monitor "A2B5:1"               --from saves/battle_re/forest_battle_3hetake.state --input "0:500" --diff --writer
#   f461 0E*(C1:3755)
<probe> --monitor "1D0C:11,1D23:11,A013:1,B18B:1,96F8:8" --from saves/battle_re/forest_battle_3hetake.state --input "0:700" --diff
<probe> --monitor "AF02:4,974A:8,A2B5:1,98A7:8" --from saves/batalha_nao_comeca_lake.state --input "0:1400" --diff
#   mode 0x10 live: f645 974A=FF1001..., $A2B5 alternates 10/01
<probe> --monitor "B185:1" --from saves/battle_re/forest_battle_3hetake.state --input "0:60" --diff --writer

# --- OURS
CT_BATTLE_MVTRACE=1 CT_BATTLE_SCANTRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy <scratch>/Belthasar.exe \
  --assets ct_assets.dat --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram \
  --selftest <scratch>/o.raw --frames 2400 --walkscript "l:400,."            # 0 battlemove lines
CT_BATTLE_MVTRACE=1 CT_BATTLE_SCANTRACE=1 ... --battletest saves/forest_re/batalha_nao_comeca.raw.wram
CT_BATTLE_SCANTRACE=1 ...                    --battletest saves/battle_re/ambush_pre.raw.wram
CT_BATTLE_TRACE=1     ...                    --battletest saves/battle_re/ambush_pre.raw.wram | grep enemy-noop
```

## R12 — Adversarial verdict

* **Bug site:** correct (`src/battle.c:4167-4172`), reproduced independently.
* **Mechanism:** correct in shape (`CC/FBAB` -> `($3760,X)`), but the dossier's `C1/373B`
  transcription drops `STA $A2B5`, and everything the dossier concludes about `$A2B5` follows from
  that omission.
* **Proposed fix:** **not safe to implement.** `& 0x0F` deletes class 4 for mode `0x10` (live,
  id `0x8D`); the `$AE = 0x20` headline constant is half the true value for mode `0x0E`; the
  "class 4 unchanged" scoping is wrong; the "classes 2/3/6 out of scope" scoping is wrong
  (id `0x19`, in a pinned fixture); and it keeps three reads of a WRAM byte the engine never
  writes.
* **Confidence in the corrected derivation (R1/R2/R5/R9):** DERIVED — single-writer proof plus a
  matching oracle trace on two independent states.

---

# Class derivation pass (session 3)

Third independent lane, read-only on `src/`. Binaries snapshotted to a scratch dir before any probe
(`build/ct.exe` -> `ct_probe.exe`, `build/Belthasar.exe`, `SDL2.dll`); nothing was built, no suite was
run, no `src/` file was touched. Every ROM address below was re-disassembled in THIS lane with
`tools/dis.py <off> <len> 1 0` (**8-bit A / 16-bit X** — with the default `0 0` the disassembler
desyncs on this whole family and prints garbage; that is why session 1 had to "hand-decode"), and
every oracle number was re-measured on a state this lane loaded itself. Default verdict: not proven.

**Headline:** R9's four steps survive re-verification (with one corrected line-number set and one
sharpened citation). The three pieces R9 left underived — **class 6 `C1/3E33`**, the **class-5
mode-`0x0D` arm `C1/3CC8`**, and the **`$9872` / `JMP C1/3D40` branch** — are ALL derivable and are
derived below, instruction by instruction; `C1/3D40` turns out to be the routine that produces most
of the Hetake wander, so a class-5 port WITHOUT it stalls after one segment. Two NEW defects were
found that R9 does not mention (§S2.2 the mode-0 cadence freeze, §S6.4 the target refresh). And the
R10 impact list is **measured and largely refuted** (§S9): the forest/GATO checks it calls
"near-certain movers" arm no unrouted mode at all.

---

## S1 — R9 re-verified, step by step

| R9 step | claim | this lane's verdict |
|---|---|---|
| 1 | `$A2B5` has exactly ONE writer in the ROM, `C1:3752 STA $A2B5`, and it stores THE MODE BEING DISPATCHED | **CONFIRMED.** `STA $A2B5` (`8D B5 A2`) occurs at `0x13752` and nowhere else in the 4 MB image; `STZ $A2B5` (`9C B5 A2`) and `STA long` (`8F B5 A2`) occur **zero** times. The 19 `LDA $A2B5` sites all lie in `0x13813..0x13F6C`, i.e. inside the class handlers themselves. |
| 1 | `s[0xA2B5]` in our engine is a read of a never-written byte | **CONFIRMED**, at three sites — the tree has moved since R9 wrote "4079/4111/4126": today they are **`src/battle.c:4085`, `:4117`, `:4132`**. `grep -rn "A2B5" src/` returns those three plus one comment; no writer. |
| 1 | oracle: `$A2B5` == the dispatched mode | **CONFIRMED independently.** `forest_battle_3hetake.state`: `$A2B5 = 04` at f0, flips to **`0E`** at **f461** — the exact frame `$98A7[0]` goes 1, i.e. the frame class 5 first runs. |
| 2 | `class = rom[0x0CFBAB + mode]`, **raw** mode, bit 7 already excluded, **no `& 0x0F`** | **CONFIRMED.** Full decode of `C1/373B` below (§S2.1): `LDA $974A,X / BMI skip / STA $A2B5 / TAX / LDA $CCFBAB,X`. The `TDC` at `0x13719` is what makes the `TAX` a clean 16-bit index (B is never touched by the 8-bit loads in between), so the index is the raw mode. `CC/FBAB[0x10] = 04`, so `& 0x0F` really does delete class 4 for mode `0x10`. |
| 2 | class 0 -> nothing (`C1/3772`), class 1 -> approach (`C1/379A`), class 4 -> return (`C1/3A3D`, modes 06 **and** `0x10`) | **CONFIRMED** from `C1/3760` (9 entries: `72 37 9A 37 BF 38 59 39 3D 3A C2 3B 33 3E 5A 3F 21 40`, table ends at `0x13772` where `C1/3772` begins) and `CC/FBAB[0x00..0x1F]`. Max class in `[0x00..0x1F]` is 8 -> the 9-entry table is exactly consumed. |
| 2 | mode `0x10` is live and broken in our engine TODAY | **CONFIRMED, re-measured on the current tree.** `--battletest saves/forest_re/batalha_nao_comeca.raw.wram`: `72 mode=16` samples, and `CT_BATTLE_MVTRACE` prints **32 `battlemove` lines, all `mode1`** — zero for mode `0x10`. |
| 3 | class 5 radius `$AE` = `0x40` iff mode `0x0E`, else `0x20` (`C1/3BF2-3C05`) | **CONFIRMED TWICE.** ROM: `13BF2 LDA #20 / STA $A5FC / LDA $A2B5 / CMP #0E / BNE +5 / ASL $A5FC / LDA $A5FC / STA $AE`. Oracle: **`--monitor "A5FC:1"` on the forest state prints `$A5FC = 0x40`** from f461 on. The session-1 headline `0x20` is refuted by direct measurement, not just by argument. A discriminating geometry test is in §S7.1: with `0x20` the first segment's bearing would be `0x08` (facing 3); the oracle shows `0x88` (facing 2). |
| 3 | settle threshold `0x0100` (`C1/2B1F`) vs class 4's `0x0010` (`C1/2B13`) | **CONFIRMED.** `0x12B13: A9 10 8D 93 98 A9 00 8D 94 98` -> `$9893/94 = 10 00` = `0x0010`; `0x12B1F: A9 00 ... A9 01 ...` -> `0x0100`. Single comparison site `0x12BA5 CMP $9893` (16-bit, after `REP #$21`), and the verdict byte is `$9872`: `0x12BAF LDA #FF / STA $9872` (NOT within) vs `0x12BB5 TDC / SEP #20 / STA $9872` (within). |
| 3 | class 5 has **no** `$A2B5 == 0x10` skip | **CONFIRMED.** `C1/3AF7` (class-4 tail) contains `13B1D AD B5 A2 / C9 10 / F0 3C` -> `13B60`; `C1/3C81` (class-5 tail) goes straight from `13C9E LDA $C0F700,X` to `13CA7 LDY #$000B`. |
| 3 | the `$A2B5 == 0x0D` arm at `C1/3CC8` and the `$9872` / `JMP C1/3D40` branch are NOT decoded | **was true; both are decoded here** (§S3.3, §S3.4). |
| 4 | class 6 (`C1/3E33`) is underived and `ambush_pre` (id `0x19`) needs it | **claim CONFIRMED, gap CLOSED** — id `0x19`'s script arms mode `0x0A` (§S4.3), and `C1/3E33` is fully decoded in §S4. |

**One correction to R9 itself:** R9 step 2 says the bit-7 filter is "the existing `& 0x80` test". In
our engine that test lives at `src/battle.c:4173` (`!(h->mvMode[slot] & 0x80)`) and again at `:4178`,
i.e. in two places with slightly different meaning (`:4178` also treats mode 0 as "skip the mover").
The ROM has only ONE filter — the `BMI` at `0x13750` — plus a *separate* `BEQ` at `0x13722` that is
not a filter at all but the cadence freeze of §S2.2. Do not fold them together.

---

## S2 — the dispatcher, exactly

### S2.1 `C1/3714` (per-frame mode machine) and `C1/373B` (dispatch)

Raw `0x13714`: `[39 ROM bytes at 0x13714: python tools/dis.py 0x13714 39]`

```
13714 AD E0 99   LDA $99E0
13717 D0 21      BNE  ->1373A (RTS)          ; the ONLY gate on this routine
13719 7B         TDC                          ; clears the FULL 16-bit accumulator (B := 0)
1371A AA         TAX
1371B 86 9C      STX $9C                      ; slot := 0
1371D A6 9C  L:  LDX $9C
1371F BD 4A 97    LDA $974A,X                 ; live mode
13722 F0 0E       BEQ  ->13732                ; <<< mode 0 -> SKIP THE WHOLE SLOT (no DEC!)
13724 DE 97 98    DEC $9897,X                 ; the segment-cadence counter
13727 D0 09       BNE  ->13732
13729 BD 9F 98    LDA $989F,X                 ; reload ($989F = 0x20)
1372C 9D 97 98    STA $9897,X
1372F 20 3B 37    JSR C1/373B
13732 E6 9C       INC $9C
13734 A5 9C       LDA $9C
13736 C9 08       CMP #08
13738 D0 E3       BNE  L
1373A 60          RTS

1373B A6 9C      LDX $9C
1373D BD F8 96   LDA $96F8,X        ; per-slot present/dispatch gate
13740 F0 1D      BEQ  ->1375F (RTS)
13742 BD 15 5E   LDA $5E15,X        ; requested target
13745 DD 52 97   CMP $9752,X
13748 F0 03      BEQ  ->1374D
1374A 9D 52 97   STA $9752,X        ; <<< the live-target REFRESH
1374D BD 4A 97   LDA $974A,X
13750 30 0D      BMI  ->1375F       ; bit7 (FF = empty slot) -> skip
13752 8D B5 A2   STA $A2B5          ; <<< $A2B5 := THE MODE BEING DISPATCHED
13755 AA         TAX
13756 BF AB FB CC LDA $CCFBAB,X     ; class = table[mode]      (raw index, no mask)
1375A 0A         ASL A
1375B AA         TAX
1375C FC 60 37   JSR ($3760,X)
1375F 60         RTS
```

`CC/FBAB @0x0CFBAB[0x00..0x1F]` (re-dumped): `00 01 01 01 00 01 04 02 03 03 06 06 06 05 05 05 |
04 07 07 07 07 07 07 07 07 08 00 02 01 01 02 02`. `C1/3760 @0x013760` (9 entries):
`3772 379A 38BF 3959 3A3D 3BC2 3E33 3F5A 4021`.

### S2.2 **NEW DEFECT — the segment-cadence counter FREEZES while the mode is 0**

The `BEQ` at `0x13722` sits **before** `DEC $9897,X`. A slot whose live mode is 0 does not tick at
all. Measured, `--monitor "9897:8" --from saves/battle_re/forest_battle_3hetake.state --input "0:120" --diff`:

```
f0   9897 = 16 17 06 04 04 04 04 04
f1   9897 = 16 17 05 03 03 03 03 03      <- slots 0,1 (mode 00) FROZEN at 16/17
...  (231 frames of this)                    slot 2 (mode 04) and slots 3-7 (mode FF) tick
f231 9897 = 15 17 1D ...                 <- slot 0 latches mode 0E at f231 and ONLY THEN starts
```

and the countdown that follows explains the arm frame exactly: slot 0 resumes at `0x16` = 22 ticks,
ticks 4 frames out of 5, is frozen again for the whole `$A013` span f250-f452, and reaches 0 at
**f461** — the observed first segment. Our port ticks unconditionally (`src/battle.c:4169`,
`if (cadFrame && --h->mvModeCad[slot] <= 0)`), i.e. it free-runs through the whole "mode 0" era. On
the GATO fixtures this is invisible (the mode is armed before the staged `0x20+slot` counter has
wrapped once, so free-run and freeze coincide — which is why `battle_ai_interp.md` §7's "arm+45
EXACT" reads as a success); on the forest fixtures the arm is at f724, long after many wraps, so the
two models diverge. **This must land with the class dispatch or the class-5 segment frames cannot
match the oracle**, and it is byte-cited (`F0 0E` at `0x13722`).

Note also that `$A013` is NOT part of `C1/3714`'s own gate (which is `$99E0` only) — but the freeze
is real: every counter stops for the whole span f250-f452 in the trace above, so our
`if (s[0xA013] || s[0x99E0]) return;` at `src/battle.c:4160` is behaviourally right even though the
`$A013` half is still uncited to a branch (R8's open point stands).

---

## S3 — CLASS 5 (`C1/3BC2`; modes `0D`/`0E`/`0F`) — complete

### S3.1 the fresh pass `C1/3BC2` (bearing latch `$9791,X` clear)

```
13BC2 LDX $9C / STZ $A5FD,X
13BC7 LDA $9791,X / BEQ ->13BD4          ; latched? reuse the stored bearing:
13BCC   LDA $9786,X / STA $DB / JMP C1/3C81
13BD4 LDA $9752,X / TAY
13BD8 $D3 := $1D0C,Y   $D4 := $1D23,Y    ; TARGET pos
13BE2 $D5 := $1D0F,X   $D6 := $1D26,X    ; SELF pos  (also -> $98AF,X / $98B7,X, the mover base)
13BF2 LDA #20 / STA $A5FC
13BF7 LDA $A2B5 / CMP #0E / BNE +5 / ASL $A5FC     ; mode 0E -> 0x40
13C01 LDA $A5FC / STA $AE
13C06 JSR C1/0222                        ; -> $DB = bearing (sign law in S5.1)
13C09 JSR C1/01F9 / CLC ADC $D4 -> $82, $A5EC,X    ; refY = targetY + r*trig(bearing)
13C16 LDA $DB ADC #40 / JSR C1/01F9 / ADC $D3 -> $80, $A5E4,X   ; refX
13C28 $81 := hi(selfX - refX) ; $83 := hi(selfY - refY)          ; 0 or FF
13C3F LDA $DB / JSR C1/0118 -> $84 = quadrant
13C46 the 4-quadrant CROSSED matrix (13C4A/13C58/13C68/13C72) -> 13C7A: $DB += 0x80 (reverse)
13C81 (tail)
```

Byte-identical to class 4 (`C1/3A3D`) except `#20` + `CMP #0E` + `ASL` vs `#40` + `CMP #10` + `#80`,
and the tail target. The quadrant matrix at `13C46..13C7F` is byte-for-byte class 4's
`13AA9..13AF6`, i.e. our `battleMoveReturn`'s `crossed` matrix is reusable verbatim.

### S3.2 the tail `C1/3C81`

```
13C81 LDA #08 / STA $AE
13C85 LDX $9C / LDA $DB / STA $9786,X            ; latch the bearing
13C8C $8C := trig(bearing)      ; $8E := trig(bearing+0x40)      ($AE = 8 -> the 8px step)
13C9B $96DE,X := C0/F700[$DB]                    ; facing
13CA7 LDY #$000B / $A050,$0B := $A5EC,X / $A039,$0B := $A5E4,X   ; ref point into scratch seat 0x0B
13CB6 TXA CLC ADC #03 TAX                        ; X := cmb (= slot + 3)
13CBB JSR C1/2B1F                                ; dist^2(cmb, seat 0x0B) vs 0x0100 -> $9872
13CBE LDA $9872 / BMI ->13CC6                    ; FF = still far  -> keep going
13CC3 JMP C1/3D40                                ; 00 = ARRIVED at the ref point -> the ORBIT branch
```

**There is no `$A2B5 == 0x10` skip here** — confirmed on the byte stream. Everything class 4 skips
for mode `0x10`, class 5 always runs.

### S3.3 **NEW — the mode-`0x0D` arm at `C1/3CC8`**

```
13CC6 LDX $9C
13CC8 LDA $A2B5 / CMP #0D / BNE ->13CD4
13CCF   STZ $98A7,X          ; mover OFF
13CD2   BRA ->13D3A          ; -> INX INX INX / STX $80 / RTS
```

So **mode `0x0D` = TURN IN PLACE**: `$9786,X` (bearing) and `$96DE,X` (facing) have already been
written at `13C87`/`13CA4`, and the routine then stops the mover and returns without arming a segment
or touching `$A03C`/`$A053`. It is not "unhandled"; it is a one-line case. No enemy in the tree's
fixtures arms it (census §S9.1), so it is ROM-derived / oracle-unexercised.

### S3.4 **NEW — the arrived branch `C1/3D40`: the ORBIT, and where most of the wander comes from**

```
13D40 LDX $9C / STZ $9791,X                      ; drop the bearing latch
13D45 LDA $9752,X / TAY
13D49 $D3,$D4 := target pos ; $D5,$D6 := self pos (-> $98AF,X/$98B7,X, the mover base)
13D63 JSR C1/0222 / PHA                          ; a0 = bearing (target -> self), see S5.1
13D67 LDA $A5F4,X / BNE +4 : LDA #10 ; else LDA #F0
13D74 CLC ADC $DB / STA $DB                      ; STEP THE ORBIT ANGLE by +0x10 or -0x10
13D79 LDA $A5FC / STA $AE                        ; the SAME radius (0x40 for mode 0E)
13D7E $D6 := targetY + r*trig($DB)  ;  $D5 := targetX + r*trig($DB+0x40)   ; the NEW ref point
13D9B $D3 := selfX ; $D4 := selfY
13DA5 JSR C1/0222                                ; $DB := bearing self -> new ref point
13DA8 PLA CLC ADC #80 / CMP $DB / BNE ->13DBB    ; degenerate: heading == exactly the reverse of a0
13DB0   INC $DB                                  ;   nudge 1 unit
13DB2   LDA $A5FC / CMP #20 / BNE ->13DBB
13DB9     INC $DB                                ;   and a 2nd unit when the radius is the small one
13DBB LDA #08 / STA $AE                          ; the 8px step; same tail shape as 3C81
13DC1 $9786,X := $DB ; $8C := trig($DB) ; $8E := trig($DB+0x40) ; $96DE,X := C0/F700[$DB]
13DE1 $A053,X := selfY + $8C ; $A03C,X := selfX + $8E ; X += 3 ; $80 := X
13DFA JSR C1/285A                                ; stage the segment endpoint
13DFD STZ $9875 ; LDA $A2B5 / CMP #0F / BNE +5 / LDA #01 / STA $9875   ; mode 0F relaxes the grid class
13E0C JSR C1/2926 / BMI ->13E1D                  ; arena grid blocked
13E11 LDA $A2B5 / CMP #0F / BEQ ->13E29          ; mode 0F skips the box probe
13E18 JSR C1/28B0 / BPL ->13E29                  ; box probe clear -> ARM
13E1D LDX $9C / LDA $A5F4,X EOR #01 / STA $A5F4,X ; BLOCKED -> REVERSE THE ORBIT, arm nothing
13E27 BRA ->13E30
13E29 LDX $9C / LDA #01 / STA $98A7,X            ; ARM (mover on)
13E30 JMP C1/3D3A                                ; INX INX INX / STX $80 / RTS
```

`$A5F4,X` is the orbit-direction flag, and the oracle prints exactly the two frames it flips —
`--monitor "A5F4:3"` on the forest state: **`f541 slot0 0 -> 1`, `f621 slot0 1 -> 0`**, which are
precisely the two handler passes in the trajectory table (§S6.1) that changed `$9786` but armed no
segment. Both directions of the derivation therefore close.

**This is the piece that matters most for the port.** In the measured window the Hetake runs
`C1/3BC2` (approach the ref point) for exactly ONE segment and then spends every later pass in
`C1/3D40`, because `dist^2 <= 0x0100` is a 16 px ball and one 8 px segment lands inside it. A class-5
implementation that stops at R9 step 3 (approach + settle) would arm one segment and then be stuck.

---

## S4 — CLASS 6 (`C1/3E33`; modes `0A`/`0B`/`0C`) — derived in full (R9's step-4 gap)

`python tools/dis.py 13E33 0x130 1 0` disassembles cleanly. It is an **ellipse orbit about a fixed
centre captured on first entry**, not a target-relative dance:

```
13E33 LDX $9C
13E35 $98AF,X := $1D0F,X ; $98B7,X := $1D26,X          ; mover base = live pos, every pass
13E41 LDA $A5FD,X / BNE ->13E60                        ; FIRST PASS ONLY:
13E46   INC $A5FD,X
13E49   $A60D,X := selfY + 0x10                        ;   centre Y  (16 px BELOW the entry foot)
13E52   $A605,X := selfX                               ;   centre X
13E58   $A615,X := 0xC0                                ;   orbit angle
13E5D   STZ $A61D,X                                    ;   orbit direction flag
13E60 LDA $A61D,X / BNE +4 : LDA #10 ; else LDA #F0
13E6B CLC ADC $A615,X / STA $A615,X / STA $DB          ; angle += +0x10 / -0x10
13E74 LDA #20 / STA $A625
13E79 LDA $A2B5 / CMP #0A / BNE +3 / LSR $A625         ; mode 0A -> rY = 0x10, else 0x20
13E83 $AE := $A625 ; $D6 := $A60D,X + rY*trig($DB)     ; ref Y
13E95 $AE := $A625 + 0x20 ; $D5 := $A605,X + rX*trig($DB+0x40)   ; ref X  (rX = rY + 0x20: an ELLIPSE)
13EAD $D3 := selfX ; $D4 := selfY ; $AE := 8
13EBB JSR C1/0222                                      ; $DB := bearing self -> ref
13EBE $9786,X := A ; $8C := trig($DB) ; $8E := trig($DB+0x40) ; $96DE,X := C0/F700[$DB]
13EDE $A053,X := selfY + $8C ; $A03C,X := selfX + $8E ; X += 3 ; $80 := X
13EF7 JSR C1/285A
13EFA STZ $9875 ; LDA $A2B5 / CMP #0C / BNE +5 / LDA #01 / STA $9875
13F09 JSR C1/2926 / BMI ->13F1A
13F0E LDA $A2B5 / CMP #0C / BEQ ->13F4B                ; mode 0C skips the box probe
13F15 JSR C1/28B0 / BPL ->13F4B
13F1A LDA #58 / STA $82                                ; BLOCKED: angle kick
13F1E LDA $A2B5 / CMP #0A / BNE +4 / LDA #30 / STA $82 ; mode 0A kicks 0x30 instead of 0x58
13F29 LDX $9C / LDA $A61D,X / BNE ->13F38
13F30   SEC LDA $A615,X SBC $82 / BRA +6               ; dir 0 -> angle -= kick
13F38   CLC LDA $A615,X ADC $82                        ; dir 1 -> angle += kick
13F3E STA $A615,X / LDA $A61D,X EOR #01 / STA $A61D,X  ; and REVERSE the orbit
13F49 BRA ->13F52                                      ; arm NOTHING this pass
13F4B LDX $9C / LDA #01 / STA $98A7,X                  ; ARM
13F52 LDX $9C / INX INX INX / STX $80 / RTS
```

Per-mode constants (all three read from `$A2B5`, i.e. from the mode — no scene, no id):

| mode | class | rY | rX | `$9875` (grid-class relax) | box probe `C1/28B0` | blocked kick |
|---|---|---|---|---|---|---|
| `0A` | 6 | `0x10` | `0x30` | 0 | runs | `0x30` |
| `0B` | 6 | `0x20` | `0x40` | 0 | runs | `0x58` |
| `0C` | 6 | `0x20` | `0x40` | **1** | **skipped** | `0x58` |

New per-slot state class 6 needs and we do not have: `$A5FD,X` (init-once), `$A605,X`/`$A60D,X`
(centre), `$A615,X` (angle), `$A61D,X` (direction).

**Status: ROM-DERIVED, oracle-UNCONFIRMED.** No `.state` in `saves/` stages enemy id `0x19` (or any
other class-6 arm) in the oracle, so the "oracle shows a value -> find its ROM source" leg is missing
for class 6. That is a *state* problem, not a ROM problem; the honest label is "could not reproduce
class 6 on any oracle state available in this tree" ([[verify-negatives-on-proper-state]]). Do not
ship class 6 as byte-exact without capturing one.

### S4.3 mode `0x0A` really is armed by a fixture we pin

`CC/8B08 + 0x19*2 -> CC/9255`, re-dumped:
`17 20 00 00 FE | 02 1E 06 00 00 08 FE | 00 00 00 00 FE | 00 00 06 0F  00 00 06 0A FE | ...`
-> a move-only entry with mode **`0x0A`** and another with **`0x0F`**. Our engine, current tree,
`--battletest saves/battle_re/ambush_pre.raw.wram` with the pinned check's own env:
`mode=15 (0x0F) x19 first @f185`, `mode=10 (0x0A) x6 first @f1460`. Both unrouted today.

---

## S5 — the shared primitives (needed to port ANY of the above correctly)

### S5.1 `C1/0222` — the bearing, and its exact sign law

`$D7 = $D3 - $D5` (call it dx), `$D9 = $D4 - $D6` (dy);
`base = C0/F300[ ((|dy| & 0xFFF8) << 2) + (|dx| >> 3) ]`; then (`0x10274`):

| dx | dy | `$DB` |
|---|---|---|
| >= 0 | >= 0 | `0x80 + base` |
| >= 0 | < 0 | `0x80 - base` |
| < 0 | >= 0 | `0x00 - base` |
| < 0 | < 0 | `base` |

Because every call in this family passes **$D3/$D4 = target, $D5/$D6 = self**, the result points
**away from the target** — exactly the convention `battleMoveReturn` already encodes as
`battleAtan2(tx, ty, sx, sy)`, so class 5 / `C1/3D40` / class 6 can reuse it unchanged.

### S5.2 `C1/01F9` — `trig(a)` scaled by `$AE`

`REP #20 / ASL ASL / AND #03FF / X := A ; A := C0/F900[X] & 0xFF ; if X >= 0x0200 then A := -A ;
$A7 := A ; $A5 := $AE & 0xFF ; JSR C1/00A7 ; return $AA`. The mover and every handler use `trig(a)`
for **Y** and `trig(a + 0x40)` for **X** — already ported as `battleTrigMul`.

### S5.3 `C1/2B13` vs `C1/2B1F` — one routine, two thresholds

Eight entry stubs at `0x12AE3..0x12B3F` each write a 16-bit threshold into `$9893/$9894`
(`0x0640`, `0x0400`, `0x1000`, `0x0010`, **`0x0010`** = `C1/2B13`, **`0x0100`** = `C1/2B1F`,
`0x0900`, `0x0400`), then fall into `C1/2B49`: `|d$A039|`, `|d$A050|`, squares via `C1/0089`, sum in
`$AF`, `0x12BA5 CMP $9893` -> `$9872 := 00` (within) or `FF` (outside). Our port hard-codes `0x10` at
`src/battle.c:4117`; class 5 needs `0x0100`.

---

## S6 — THE ACCEPTANCE TARGET (re-measured; treat these numbers, not session 1's)

State `saves/battle_re/forest_battle_3hetake.state`, no input. `$1D0F/$1D26` = the three enemies'
live X/Y (`cmb3/cmb4/cmb5`). Staging `(78,3F) (58,5F) (A8,7F)`; PCs `$1D0C/$1D23` = `A0 7F 8F` /
`47 90 A0` and **static for the whole window**. `$9752 = 00 01 01` after the refresh (§S6.4).

### S6.1 cmb3 (slot 0, mode `0x0E`) — the oracle

| frames | what | `$9786` bearing | `$96DE` facing | X, Y |
|---|---|---|---|---|
| f0..f460 | staged; in-span attack lunge f87->f100 X `78->94`, f134->f140 back | `00` | 03 | `(78,3F)` |
| **f461** | segment 1 armed (`C1/3BC2` fresh pass) | **`88`** | **02** | |
| f465..f493 | 8 phases / 4 f | | | X `77,76,75,74,73,72,71,70`; Y `3E`,`3D` -> **`(70,3D)`** |
| **f501** | segment 2 armed (**`C1/3D40`**) | **`C0`** | **00** | |
| f505..f533 | | | | X `70` constant; Y `3C,3B,3A,39,38,37,36,35` -> **`(70,35)`** |
| f541 | pass, **no arm** — `C1/3D40` blocked, `$A5F4[0] 0->1` | `C0` | 00 | held |
| **f581** | segment 3 armed | **`60`** | **02** | |
| f583..f613 | | | | `(70,35)` -> **`(6A,3A)`** |
| f621 | pass, **no arm**, `$A5F4[0] 1->0` | **`4E`** | **01** | held |
| **f661** | segment 4 armed | **`C0`** | **00** | |
| f665..f693 | | | | Y `39,38,37,36,35,34,33,32` -> **`(6A,32)`** |
| f693..f734 | rest until the next action span | | | `(6A,32)` |

### S6.2 cmb4 (slot 1, mode `0x0E`) — the oracle

| frames | bearing | facing | X, Y |
|---|---|---|---|
| f0..f482 | `00` | 03 | `(58,5F)`; lunge f286->f315 to `(94,4A)`, home by f364 |
| f483 arm -> f515 | **`F3`** | 03 | `(58,5F)` -> **`(5F,5C)`** |
| f523 arm -> f555 | **`EE`** | 03 | -> **`(66,58)`** |
| f563 arm -> f595 | `EE` | 03 | -> **`(6D,54)`** |
| f603 arm -> f635 | **`00`** | 03 | -> **`(74,54)`** |
| f643 arm -> f675 | `00` | 03 | -> **`(7B,54)`** |
| f683 arm -> f715 | `00` | 03 | -> **`(82,54)`** |

### S6.3 cmb5 (slot 2, mode `04` -> class 0) — the oracle

Byte-static at `(A8,7F)` for the whole window. It is the class table's own answer to "which enemy
stands still", and it only ever leaves `(A8,7F)` via its own attack lunge (`(B3,81)` from f1624).
**A correct fix must NOT make this one wander.**

Long-horizon snapshot (positions at each `$A013` rise, `--input "0:2500"`):
`f51 (78,3F)(58,5F)(A8,7F)` / `f250 same` / `f734 (6A,32)(82,54)(A8,7F)` / `f939, f1135 same` /
`f1624 .. f2498 (6A,32)(82,54)(B3,81)`.

### S6.4 OURS, same tree, same day

* forest, real fight (`--loadwram saves/forest_re/battle_crono_solo.dumpwram --walkscript "l:400,."`,
  2400 f): `mode=14` armed **166** times; `CT_BATTLE_MVTRACE` prints **0 `battlemove` lines**.
* the three enemies' live X/Y are therefore CONSTANT at their staged values for the whole fight.
  Independent confirmation from `CT_BATTLE_ASCTRACE`: each actor's lunge `home=` is byte-identical on
  every one of its turns — `actor=3 home=(120,63)` at f736 / f1664 / f2388 / f3112,
  `actor=4 home=(88,95)` at f307 / f1235 / f3612, `actor=5 home=(168,127)` at f524 / f1452 / f2176 —
  i.e. `(0x78,0x3F) (0x58,0x5F) (0xA8,0x7F)`, the oracle's *staging*, never its wander.
* **NEW, R8's loose end made concrete:** the oracle refreshes `$9752,X <- $5E15,X` at `C1:374A` and
  the value that lands for slot 1 is **`01`** (`$5E15 = 00 01 00` at f452 -> `$9752 = 00 01 01` at
  f483). Combatant 1 is an EMPTY PC seat at `(0x7F,0x90)` in this solo-Crono fight, and cmb4's whole
  wander is computed against it — §S7.2 reproduces its first bearing `F3` exactly from that seat and
  from no other. Our port writes `mvTarget` once (`src/battle.c:3976`) and never refreshes. Without
  the refresh cmb4's trajectory cannot match, whatever the class dispatch does.

---

## S7 — prediction <-> measurement (both directions, the mandate's step 4)

### S7.1 the radius, decided by geometry (not by argument)

cmb3 at `(120,63)`, target cmb0 at `(160,71)`, mode `0x0E`:
`C1/0222` -> dx=+40, dy=+8 -> `base = F300[(8 & ~7)*4 + (40>>3)] = F300[0x25] = 0x08`, both >= 0 ->
bearing **`0x88`**.

* with `$AE = 0x40` (the derived value): ref = `(160-63, 71-12) = (97,59)`; `$81=0, $83=0`;
  quadrant `0x88>>6 = 2` -> the `13C62` arm -> **no reverse** -> `$DB = 0x88`, facing `F700[0x88] = 2`.
  Segment = `trig(0xC8)*8 = -8` in X, `trig(0x88)*8 = -2` in Y -> `(78,3F) -> (70,3D)`.
* with `$AE = 0x20` (session 1's headline): ref = `(129,65)`; `$81=FF, $83=FF`; quadrant 2 ->
  **crossed** -> `$DB = 0x08`, facing `F700[0x08] = 3` — the opposite direction.

**Oracle: `$9786[0] = 0x88`, `$96DE[0] = 2`, first segment `(78,3F) -> (70,3D)`.** `0x40` predicts it
exactly; `0x20` predicts the reverse. And `$A5FC` reads **`0x40`** directly on the state.

### S7.2 `C1/3D40` reproduced from the ROM, twice

* cmb3, f501 pass: previous ref `(97,59)`, self `(112,61)` -> the settle test says "arrived"
  (`$9872 = 00`), which is the branch taken. `C1/3D40` then: `a0 = 0x88`, `$A5F4 = 0` -> `$DB = 0x98`,
  ref = `(160-53, 71-36) = (107,35)`; self->ref dx=+5, dy=+26 -> `base = F300[24*4 + 0] = F300[96] =
  0x40`, both >= 0 -> `$DB = 0xC0`. **Oracle f501: `$9786[0] = C0`, facing 0, segment
  `(70,3D) -> (70,35)`.** Exact.
* cmb4, f483 pass: target = cmb1 `(127,144)`, self `(88,95)` -> `a0 = 0xA8`; ref at `a0`, r = `0x40`
  is `(91,91)`, `dist^2 = 25 <= 0x0100` -> arrived -> `C1/3D40`: `$DB = 0xB8`, ref = `(114,81)`,
  self->ref dx = `-26`, dy = `+14` -> `base = F300[8*4 + 3] = F300[0x23] = 0x0D`, dx<0 / dy>=0 ->
  `$DB = 0x100 - 0x0D = 0xF3`. **Oracle f483: `$9786[1] = F3`, facing `F700[0xF3] = 3`, segment
  `(58,5F) -> (5F,5C)`.** Exact — and it only comes out right with cmb1 as the target (§S6.4).

---

## S8 — the corrected minimal change (R9 v2; still NOT applied — read-only lane)

Order matters; each step is independently validatable.

1. **`src/battle.c:4085 / :4117 / :4132`** — replace `s[0xA2B5]` with the live mode of the slot being
   handled. `C1:3752` is the single writer in the ROM and it stores the dispatched mode immediately
   before `JSR ($3760,X)`, so `s[0xA2B5] == 0x10` **is** `mode == 0x10` by construction. (R9 step 1;
   line numbers corrected for today's tree.)
2. **`src/battle.c:4173-4178`** — dispatch on the CLASS: `class = rom[0x0CFBAB + mode]`, **raw** mode,
   bit 7 filtered exactly once as the ROM's `BMI` at `0x13750` does, **no `& 0x0F`**. Route
   `0 -> nothing (C1/3772)`, `1 -> battleMoveApproach (C1/379A; modes 01/02/03/05)`,
   `4 -> battleMoveReturn (C1/3A3D; modes 06 AND 0x10)`. Steps 1+2 introduce no new geometry and by
   themselves repair mode `0x10` (measured broken, §S1).
3. **`src/battle.c:4169`** — add the ROM's mode-0 cadence freeze (`BEQ` at `0x13722`, §S2.2): a slot
   whose live mode is 0 must not decrement its counter. Without this the class-5 segments cannot land
   on the oracle's frames even if the geometry is perfect. **Validate `field-battle-gato-walk` right
   after this step in isolation** — it is the only pinned check that can see it.
4. **`src/battle.c:3976`** — port the `$9752,X <- $5E15,X` refresh of `C1:3742-374A` (§S6.4). cmb4's
   whole trajectory depends on it.
5. **Class 5** = `C1/3BC2` + tail `C1/3C81` + **`C1/3D40`** + the mode-`0x0D` arm. Same body as
   `battleMoveReturn`, parameterised by: radius `$AE` = `0x40` iff mode `0x0E` else `0x20`
   (`C1/3BF2-3C05`); settle threshold `0x0100` (`C1/2B1F`); NO `mode == 0x10` skip; `$9875 := 1` and
   the box probe skipped iff mode `0x0F`; on arrival, `C1/3D40` (new per-slot state `$A5F4,X`, the
   orbit-direction flag); mode `0x0D` -> face and stop. **Do not ship class 5 without `C1/3D40`** —
   after the first 8 px segment every later pass is in it (§S3.4).
6. **Class 6** = `C1/3E33` (§S4), with the new per-slot cells `$A5FD/$A605/$A60D/$A615/$A61D`.
   Derived from the ROM but **not oracle-confirmed** — capture a state that stages id `0x19` (or any
   class-6 arm) with `capture-oracle-state` first, or land it explicitly labelled HYPOTHESIS.
7. Classes 2 (`C1/38BF`), 3 (`C1/3959`), 7 (`C1/3F5A`), 8 (`C1/4021`) — leave unhandled. (Class 7 is
   visibly a fixed-bearing table for modes `0x11..0x17`: `C0/40/00/80/20/60/A0/E0` with a `$80`
   side-effect, `0x13F5A-0x13FB8` — decode it when something arms it.)

Forbidden here as everywhere: `if (id == 0x5E)`, `if (id == 0x19)`, a slot/scene gate, or "make idle
enemies wiggle". Note in particular that **cmb5 must keep standing still** (§S6.3) — that falls out
of `CC/FBAB[0x04] = 0`, not out of any special case.

### S8.1 an uncited line already in the tree (adjacent, not this report's bug)

`src/battle.c` sets `h->mvFacing[slot] = 1` on settle, commented "settle re-faces the party
(C1/3B40 park)". `C1/3B40-3B5B` is `INC $9829,X / $5E1D,X := $5E0D,X / $9831,X := $1D0C,Y /
$9839,X := $1D23,Y / STZ $98A7,X` — **there is no facing write there**. Flagged, not investigated.

---

## S9 — pinned checks: MEASURED impact (this largely REFUTES R10)

R10 guessed from the shape of the pins. This lane instead ran every battle check's own `argv` + `env`
with `CT_BATTLE_SCANTRACE=1` against the snapshot binary and censused which modes each fixture
actually arms and at which frame. **No suite was run**; each check's command was executed once, for
its mode census only, with its `CT_BATTLESHOT` output redirected into scratch.

### S9.1 the census (unrouted mode = anything not in {0, 1, 4, 6})

| fixture (as used by the checks) | modes armed | first unrouted mode |
|---|---|---|
| every `gato_*.raw.wram` (37+ checks) | 0, 1, 6 only | **none** |
| `forest_pre.raw.wram` **with `CT_BATTLE_MASHA=1`** | 0, 4 only | **none** |
| `forest_pre.raw.wram` with a `CT_BATTLE_SCRIPT` (or plain) | 0, 4, **0x0E** | `0x0E` @ **f724** (f750/f904 under some scripts) |
| `forest_cyclone.raw.wram` | 0, 4, **0x0E** | `0x0E` @ **f1207** |
| `forest_seek_pre` / `forest_stale_deadflag` | 0, 4 (+ **0x0E** @f724 under its own script) | |
| `party3_field.lakewram` (bat9) | 0, 1, **0x0A** | `0x0A` @ **f976** |
| `ambush_pre.raw.wram` | 0, 1, 4, **0x0F**, **0x0A** (+ `0x10` without MASHA) | `0x0F` @ **f185** |

### S9.2 WILL move — and because the fix is RIGHT

| check | pinned at | first unrouted mode | why |
|---|---|---|---|
| `field-battle-ambush-generic-dissolve` | `battleshot f362`, `victory f2059` | `0x0F` @f185, `0x0A` @f1460 | classes 5 **and** 6 newly arm here; both pins are after f185. This is the check class 6 must be right for. |
| `field-battle-hetake-pose-ladder` | f1020..f1129 | `0x0E` @f724 | wandering Hetakes change `asc arm ... home=` -> the SLIDE leg count -> the span length -> every later frame. |
| `field-battle-hetake-attack-sfx` | `battlesfx f1053 rel=51` | `0x0E` @f724 | same; the `rel=` should hold, the absolute frame will not. |
| `field-battle-hurt-flash-script-bound` | 12 `rel=` pairs | `0x0E` @f724 | `rel=` is script-relative so it *may* hold; the turn order/lengths that produce the 12 pairs will not. |
| `field-battle-stale-deadflag-enemies-act` | `enAtk=10`, `cronoHP=48 marleHP=44` | `0x0E` @f724 | a 2200-frame census: any span-length change moves the attack count. |
| `field-battle-end-uiteardown` | `battletrace f1635 target-empty` | `0x0E` @f904 | after the arm. |
| `field-battle-bat9-return` | `returnprobe f2308 ... fieldfnv=` | **`0x0A`** @f976 | a **class-6** case in a pinned check that R10 never listed. |

### S9.3 will **NOT** move — R10's "near-certain movers" are measured wrong

| check R10 predicted would move | measured reason it cannot |
|---|---|
| `field-battle-enemy-death-sfx` (`pan=78/58/A8`) | its fixture is `forest_pre` **+ `CT_BATTLE_MASHA=1`**, where the only modes ever armed are **0 and 4** — and `CC/FBAB[0x04] = 0` = class 0 = no motion **in the ROM either**. The pans stay `78/58/A8`. |
| `field-battleloop-forest` (`victory f1176`) | same fixture/env: modes 0 and 4 only. |
| `field-battle-forest-lastkill-dissolve` (`death#3+18`) | same. |
| `field-battle-tech-cyclone-route` (`approach=(120,63)`, f450/f563) | `forest_cyclone` arms `0x0E` first at **f1207**, long after both pins. |
| `field-battle-tech-cyclone-multi` / `-fx` / `field-battle-dissolve-serial` / `field-battle-tgtcursor-cyclone-multi` | all pin f234..f588; first unrouted mode f724/f1207. |
| `field-battle-hetake-return-pose` (`enemy-arrive#1+110`) | `enemy-arrive#1 = f293` on this fixture -> shot at **f403**, before the f724 arm. |
| `field-battle-hetake-attack-pose` (`enemy-arrive#1+75`) | shot at **f368**, before f724. |
| `field-battle-crit-leap-mid` (f223) / `field-battle-crit-beatB-dead-suppressed` (f289) | first unrouted mode f750. |
| `field-battle-enemy-no-phantom-prepose` (f600) | first unrouted mode f904. |
| `field-battle-results-reward`, `-drop-roll`, `-levelup-growth`, `-tech-learn`, `-pc-approach-early/late`, `-forest-return`, `-forest-camera-composite`, `-forest-seek-glide`, `-solo-victory-no-marle` | MASHA/GATO fixtures: modes 0/1/4/6 only. |

### S9.4 must NOT move — alarms

* **Every GATO check** (`field-battle-gato-walk`, `-ai-cursor-cadence`, `-gato-live-pose-and-foot`,
  `field-battletest-gato`, and the 33 others on `gato_*.raw.wram`): census = modes **0, 1, 6 only**,
  all already routed. If steps 1-2 move any of them, the class table was masked or `$A2B5` was
  mis-substituted.
* **BUT step 3 (the mode-0 cadence freeze) is the one thing that CAN move them**, and
  `field-battle-gato-walk` (`battlemove f206 slot3 cmb6 mode1`, `battlemove f442 slot3 cmb6 mode6`)
  is the only pinned check with the resolution to see it. Land step 3 alone and look at that check
  before adding any geometry. If it moves, re-pin it **against the oracle**, never against our own
  new output — the ROM byte (`F0 0E` at `0x13722`) says the freeze is canonical.

---

## S10 — open questions after this pass

1. **Class 6 has no oracle witness in this tree.** ROM-complete, oracle-unconfirmed (§S4). Needs a
   captured state that stages id `0x19` (or any mode `0x0A`-`0x0C` arm).
2. **Mode `0x0D` (class-5 turn-in-place) and modes `02/03/05` (class 1)** are derived but likewise
   unexercised by any fixture in the tree. Route them by class; do not claim byte-exactness.
3. **Classes 2, 3, 7, 8** (`C1/38BF`, `C1/3959`, `C1/3F5A`, `C1/4021`) — not decoded. Class 7's shape
   is visible (fixed bearing per mode `0x11..0x17`) but nothing in scope arms it.
4. **`C1/285A`, `C1/2926`, `C1/28B0`** — the segment-stage and the two probes. Our port models
   `2926`/`28B0` (`battleGridBlocked` + the victim-box test) and `285A` is implicit. Class 5's
   `$9875 := 1` for mode `0x0F` and class 6's for mode `0x0C` change the `2926` verdict, so the
   `$9875` plumbing must be honoured, not dropped.
5. **`$B185` advance cadence** (session 1, H.2) — untouched here; still open.
6. **`$96F8,X`** (the per-slot dispatch gate at `0x1373D`) reads `01 01 01 00 ...` on the forest state;
   our port substitutes `battlePresent(s, cmb)` (`src/battle.c:4168`) — still uncited (R8).
7. **`src/battle.c`'s `mvFacing = 1` on settle** is not in `C1/3B40` (§S8.1).
8. **"they never attack"** — not re-tested in this lane; §D.2's *"not reproduced under the headless
   forest scenario"* stands as written.

---

## S11 — exact commands this lane ran (cwd = repo root; nothing written into `build/` or `src/`)

```
mkdir -p "$TMP/ctwf4/movers"
cp build/ct.exe "$TMP/ctwf4/movers/ct_probe.exe"; cp build/Belthasar.exe "$TMP/ctwf4/movers/"; cp build/SDL2.dll "$TMP/ctwf4/movers/"

# --- ROM (note the trailing "1 0" = 8-bit A / 16-bit X; with "0 0" this whole family
#     disassembles to garbage -- that is the trap session 1 hit)
python tools/dis.py 13714 0x60  1 0    # the mode machine + C1/373B (the BEQ freeze + STA $A2B5)
python tools/dis.py 13BC2 0xC0  1 0    # class 5 fresh pass
python tools/dis.py 13C81 0xD0  1 0    # class-5 tail + the 0x0D arm + the 3D40 branch
python tools/dis.py 13D40 0xA0  1 0    # the ORBIT branch
python tools/dis.py 13DDE 0x60  1 0    # its tail (the $A5F4 flip at 13E1D)
python tools/dis.py 13E33 0x100 1 0    # CLASS 6
python tools/dis.py 13F29 0x90  1 0    # class-6 tail + class 7 (C1/3F5A) head
python tools/dis.py 13A3D 0x60  1 0 ; python tools/dis.py 13AF7 0x90 1 0 ; python tools/dis.py 13B84 0x40 1 0
python tools/dis.py 12AD0 0xE0  1 0    # the eight 2Bxx threshold stubs + CMP $9893 + the $9872 verdict
python tools/dis.py 10222 0x60  1 0 ; python tools/dis.py 101F9 0x30 1 0   # atan2 sign law + trig
python -c "import re;d=open('Chrono_Trigger_USA_SNES.sfc','rb').read();print([hex(m.start()) for m in re.finditer(re.escape(b'\x8d\xb5\xa2'),d)])"   # ['0x13752'] -- the only writer
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read();print(' '.join('%02X'%b for b in d[0x0CFBAB:0x0CFBAB+0x20]))"
python -c "d=open('Chrono_Trigger_USA_SNES.sfc','rb').read();print(' '.join('%02X'%b for b in d[0xF300:0xF400]));print(' '.join('%02X'%b for b in d[0xF700:0xF800]))"

# --- ORACLE (saves/battle_re/forest_battle_3hetake.state)
<probe> --monitor "A2B5:1,974A:8,98A7:8,96F8:8"                  --from ... --input "0:900"  --diff
<probe> --monitor "9897:8"                                       --from ... --input "0:120"  --diff   # the mode-0 freeze
<probe> --monitor "9897:3,974A:3,98A7:3,A013:1"                  --from ... --input "0:500"  --diff
<probe> --monitor "1D0F:3,1D26:3,98A7:3,9786:3,96DE:3,A013:1"    --from ... --input "0:760"  --diff   # THE TRAJECTORY TABLE
<probe> --monitor "1D0C:3,1D23:3,9752:3,A5FC:1,A5F4:3,9791:3"    --from ... --input "0:700"  --diff   # $A5FC = 0x40, the $A5F4 flips
<probe> --monitor "5E15:3,9752:3"                                --from ... --input "0:700"  --diff   # the C1:374A refresh
<probe> --monitor "1D0F:3,1D26:3,A013:1,B18B:1"                  --from ... --input "0:2500" --diff | grep "A013=01\*"

# --- OURS (snapshot binary; SDL dummy drivers)
CT_BATTLE_MVTRACE=1 CT_BATTLE_SCANTRACE=1 <snap>/Belthasar.exe --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest <scratch>/o.raw --frames 2400 --walkscript "l:400,."
#   166 mode=14, ZERO battlemove
CT_BATTLE_ASCTRACE=1 ... same ...        # home= constant per actor across every turn
CT_BATTLE_SCANTRACE=1 CT_BATTLE_MVTRACE=1 <snap>/Belthasar.exe --assets ct_assets.dat --scene 19 \
  --battletest saves/forest_re/batalha_nao_comeca.raw.wram        # 72 mode=16, 0 battlemove for it
# the per-check census of S9 (runs each check's own argv+env once, SCANTRACE only, shots -> scratch)
```

---

# Adversarial review (session 3)

Fourth lane, read-only on `src/`, default verdict **not proven**. Binaries snapshotted before any
probe (`build/ct.exe` -> `ct_probe.exe`, `build/Belthasar.exe`, `SDL2.dll`); nothing built, no suite
run, no `src/` file touched. **Every ROM address below was re-disassembled in THIS lane** and **every
oracle number re-measured on a state this lane loaded itself**. Where session 3 and this lane agree I
say CONFIRMED and give the byte; where they do not I give the disassembly that refutes it.

**Bottom line.** The MECHANISM survives: the dispatch really is `class = CC/FBAB[raw mode]` through
the 9-entry table at `C1/3760`, `$A2B5` really is the dispatched mode with one writer, and our engine
really does dispatch on the MODE so `0x0A/0x0E/0x0F/0x10` arm and never move. I reproduced the whole
S6 trajectory table and, independently, reproduced three of its segments from the ROM tables alone in
a scratch model. **The PROPOSED FIX (S8) is not landable as written**: step 1 actively BREAKS mode
`0x10` (V1), step 4 names a blank line and an input the engine does not have (V6), step 3 leaves a
presence gate the ROM does not have (V7), "class 0 -> nothing" is refuted by the disassembly (V2),
class 6 is missing six clearers of its own init flag (V3), and **the acceptance target cannot be
measured on our engine at all** (V13, demonstrated, not argued).

---

## V0 — what I independently CONFIRMED (re-run, not re-read)

| claim | this lane's byte / measurement |
|---|---|
| `C1/3714` mode machine | raw `0x13714` = `[39 ROM bytes at 0x13714: python tools/dis.py 0x13714 39]`. `F0 0E` (BEQ) at `0x13722` sits **before** `DE 97 98` (DEC) at `0x13724`. CONFIRMED. |
| `$A2B5` single writer | `8D B5 A2` occurs **once** in the 4 MB image, `0x13752`. `9C B5 A2` = 0, `8F B5 A2 xx` = 0. The 19 `AD B5 A2` readers: `13813 13870 13881 1396E 139EC 13A71 13B1D 13B84 13BF7 13CC8 13CF1 13D02 13E00 13E11 13E79 13EFD 13F0E 13F1E 13F6C` — all inside the class handlers. CONFIRMED. |
| every fixture holds `$A2B5 = 0x80` | `ambush_pre`, `gato_t0`, `batalha_nao_comeca`, `forest_pre` .raw.wram: all `80`. CONFIRMED. |
| `CC/FBAB[0x00..0x1F]` | `[32 ROM bytes at 0xCFBAB: python tools/dis.py 0xCFBAB 32]`. CONFIRMED. |
| `C1/3760` 9 entries | `72 37 9A 37 BF 38 59 39 3D 3A C2 3B 33 3E 5A 3F 21 40`. CONFIRMED. |
| radius `0x40` iff mode `0x0E` | `0x13BF2 LDA #20 / STA $A5FC / LDA $A2B5 / CMP #0E / BNE / ASL $A5FC / LDA $A5FC / STA $AE`. Oracle `--monitor "A5FC:1"`: `00` until f460, **`40` from f461**. CONFIRMED TWICE. |
| settle `0x0100` (C1/2B1F) vs `0x0010` (C1/2B13) | `0x12B13: A9 10 ... A9 00` -> `$9893/94 = 10 00`; `0x12B1F: A9 00 ... A9 01` -> `00 01`. Single compare `0x12BA5 CMP $9893`; `0x12BAF LDA #FF / STA $9872` vs `0x12BB8 STA $9872` (A=0 after `TDC` at `0x12BB5`). CONFIRMED. |
| `C1/3D40` orbit | `0x13D40-0x13E32` re-disassembled; `$A5F4,X` step `+0x10`/`-0x10` at `0x13D69-0x13D77`, EOR flip at `0x13E1F-0x13E24`. CONFIRMED. |
| `C1/3E33` class 6 | `0x13E33-0x13F59` re-disassembled; centre `(selfX, selfY+0x10)` at `0x13E49/0x13E52`, angle `0xC0` at `0x13E58`, `rY = 0x20` (`LSR` for mode `0x0A` at `0x13E80`), `rX = rY + 0x20` at `0x13E99`, kick `0x58`/`0x30` at `0x13F1A/0x13F25`. CONFIRMED, byte for byte. |
| id `0x19` arms class 6 | `CC/8B08 + 0x19*2 -> 0x9255`; `0x0C9255` = `[27 ROM bytes at 0xC9255: python tools/dis.py 0xC9255 27] ...` — `06 0F` and `06 0A` present (and `06 00`, `06 0E` twice later). CONFIRMED. |
| oracle mode/arm timeline | `--monitor "A2B5:1,974A:8,98A7:8,96F8:8,A5FC:1"` on `forest_battle_3hetake.state`: `$A2B5` `04` -> **`0E` at f461**; `$974A` slot0 `00->0E` f231, slot1 `00->0E` f455, slot2 `04->00` f244; `$96F8 = 01 01 01 00 00 00 00 00` FLAT. CONFIRMED. |
| the mode-0 cadence freeze | `--monitor "9897:8"`: f0 `16 17 06 04 04 04 04 04` -> f1 `16 17 05 03 03 03 03 03` -> ... slots 0/1 (mode `00`) pinned at `16/17`, everything else ticks. CONFIRMED. `$989F = 20 20 20 20 20 20 20 20` (the reload). |
| the `$9752 <- $5E15` refresh | `--monitor "5E15:3,9752:3"`: `$9752 = FF FF 01` at f0; `$5E15 = 00 01 00` from f452; `$9752` -> `00 FF 01` at **f461** and `00 01 01` at **f483**. CONFIRMED — and slot 1's target really is combatant 1, `$1D0C/$1D23[1] = 7F/90`, an empty PC seat in this solo fight. |
| `$A5F4` flips | `--monitor "A5F4:3,9791:3"`: slot0 `0->1` at **f541**, `1->0` at **f621**; `$9791` stays `00 00 00` for the whole window. CONFIRMED. |
| S6.1/S6.2 acceptance table | re-measured with `--monitor "1D0F:3,1D26:3,9786:3,96DE:3,98A7:3" --input "0:700" --diff`. cmb3: f461 b=`88` fac 2 -> `(70,3D)` @f493; f501 b=`C0` fac 0 -> `(70,35)` @f533; f541 no-arm; f581 b=`60` fac 2 -> `(6A,3A)` @f613; f621 no-arm b=`4E` fac 1; f661 b=`C0` -> `(6A,32)` @f693. cmb4: f483 `F3` -> `(5F,5C)` @f515; f523 `EE` -> `(66,58)`; f563 `EE` -> `(6D,54)`; f603 `00` -> `(74,54)`; f643 -> `(7B,54)`. cmb5 byte-static `(A8,7F)`. **EVERY ROW CONFIRMED.** |
| ours: forest fight | `CT_BATTLE_MVTRACE=1 CT_BATTLE_SCANTRACE=1 Belthasar --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram --frames 2400 --walkscript "l:400,."` -> **`166 mode=14`, `0 battlemove`**. CONFIRMED. |
| ours: mode `0x10` | `--battletest saves/forest_re/batalha_nao_comeca.raw.wram` -> **`72 mode=16`**, 32 `battlemove` lines **all `mode1`**. CONFIRMED. |
| S9 census (spot-checked 6 of 7 rows) | `ambush_pre` **with the check's own `CT_BATTLE_MASHA=1`**: `mode=15` x19 first **f185**, `mode=10` x6 first **f1460**. `ambush_pre` without MASHA also `mode=16` first f722. `forest_pre` + MASHA: **0 and 4 only**. `forest_pre` plain: `mode=14` first **f724**. `forest_cyclone` (check's own script): `mode=14` first **f1207**. `gato_t0` (check's own env `CT_BATTLE_SCRIPT=n:900`): **0, 1, 6 only**, 264 battlemove lines. `party3_field.lakewram` + MASHA (bat9's own argv): `mode=10` first **f976**. **S9 CONFIRMED.** |
| S8.1's flag | `0x13B40-0x13B5B` = `INC $9829,X / $5E1D,X := $5E0D,X / $9831,X := $1D0C,Y / $9839,X := $1D23,Y / STZ $98A7,X`. **No facing write.** The flag is correct. |

### V0.1 — an independent reconstruction, not a re-reading

I built a scratch model of `C1/0222` (`0x10222`: `$D7 = $D3-$D5`, `$D9 = $D4-$D6`,
`base = C0/F300[((|dy| & 0xFFF8) << 2) + (|dx| >> 3)]`, sign law at `0x10274-0x10298`) and of
`C1/01F9` (`0x101F9`: `X = (a*4) & 0x3FF`, `v = C0/F900[X]`, negate iff `X >= 0x0200`, then the
`C1/00A7` 16x8 multiply by `$AE`, result `(scale*v) >> 8`) straight from the ROM tables, and ran the
class-5 / `C1/3D40` geometry through it with the oracle's own inputs:

* f461 fresh pass, self `(0x78,0x3F)`, target cmb0 `(0xA0,0x47)`, r `0x40`
  -> bearing **`0x88`**, `F700[0x88] = 2`, step `(-8,-2)` -> **`(0x70,0x3D)`**.
* f501 `C1/3D40`, self `(0x70,0x3D)` -> bearing **`0xC0`**, `F700[0xC0] = 0`, step `(0,-8)` ->
  **`(0x70,0x35)`**.
* f483 cmb4, self `(0x58,0x5F)`, target cmb1 `(0x7F,0x90)` -> `a0 = 0xA8`, ref `(0x5B,0x5B)`,
  `dist^2 = 25 <= 0x0100` (arrived), stepped `0xB8`, new ref `(0x72,0x51)`, bearing **`0xF3`**,
  `F700[0xF3] = 3`, step `(+7,-3)` -> **`(0x5F,0x5C)`**.

All three match the oracle exactly. **The mechanism is DERIVED and reproduced from the ROM alone.**
(It also re-confirms `$AE = 0x40`: at `0x20` the f461 bearing would be `0x08` -> facing 3, the
reverse of the measured `88`/2.)

---

## V1 — BLOCKER. S8 step 1 makes mode `0x10` WRONG at `src/battle.c:4132`

`0x13AF7` (class-4 tail), re-disassembled in full:

```
13B1D LDA $A2B5 / CMP #10 / BEQ ->13B60      ; mode 0x10 skips ONLY 13B24-13B5E (the settle + park)
13B24   LDY #000B / $A050,$0B := $A5EC,X / $A039,$0B := $A5E4,X
13B33   TXA CLC ADC #03 TAX / JSR C1/2B13 / LDA $9872 / BMI ->13B60
13B40   INC $9829,X / $5E1D,X := $5E0D,X / $9831,X := $1D0C,Y / $9839,X := $1D23,Y / STZ $98A7,X / BRA 13BBC
13B60 $A053,X := selfY + $8C ; $A03C,X := selfX + $8E ; X += 3 ; $80 := X
13B79 JSR C1/285A
13B7C STZ $9875
13B7F JSR C1/2926 / BMI ->13B90               ; <<< THE ARENA GRID PROBE RUNS FOR EVERY MODE
13B84 LDA $A2B5 / CMP #03 / BEQ ->13BB2       ; <<< the box-probe skip is mode 0x03, NOT 0x10
13B8B JSR C1/28B0 / BPL ->13BB2               ; <<< THE VICTIM-BOX PROBE RUNS FOR MODE 0x10 TOO
13B90 ... axis-quantize + INC $9791,X + STZ $98A7,X
13BB2 STZ $9791,X / $98A7,X := 1              ; ARM
```

Our port wraps **both** probes in `if (s[0xA2B5] != 0x10)` (`src/battle.c:4132`, comment "the doubled
variant skips both (3B22)"). That comment is wrong about the ROM. It is **inert today** only because
`$A2B5` is never written and every fixture holds `0x80`. **The moment S8 step 1 substitutes the live
mode, mode-`0x10` slots stop testing the arena grid and the victim box — a behaviour the ROM never
has — and step 2 then routes mode `0x10` into that broken path.** Steps 1+2, in the order S8 gives
them, therefore introduce a fresh divergence in the exact mode they are advertised to repair.

Correct port: `:4117`'s settle keeps the `mode != 0x10` guard (`13B1D`); `:4132`'s probes become
**unconditional** for class 4. (`CMP #03` at `0x13B84` is unreachable for class 4 — `CC/FBAB[0x03] = 1`
and the only entries into `0x13AF7` are the class-4 head's fall-through and `JMP C1/3AF7` at `0x13A4C`,
verified: `4C F7 3A` occurs once, `20 F7 3A` zero times. It is dead ROM code; do not port it as a
live branch, and do not repurpose its address for mode `0x10`.)

---

## V2 — REFUTED: "class 0 -> nothing (`C1/3772`)". Class 0 is the RE-ACQUIRE half of class 4's settle

`python tools/dis.py 13772 0x30 1 0`:

```
13772 LDX $9C
13774 LDA $9829,X / BEQ ->13799 (RTS)        ; the arrival latch class 4 sets at 13B42
13779 STZ $A5FD,X
1377C LDA $9752,X / TAY
1377F LDA $1D0C,Y / CMP $9831,X / BNE ->13790   ; did the victim MOVE since the settle?
13788 LDA $1D23,Y / CMP $9839,X / BEQ ->13799
13790 LDA $5E1D,X / STA $5E0D,X              ; RESTORE the saved AI/move byte
13796 STZ $9829,X                            ; clear the arrival latch
13799 RTS
```

`$5E0D,X -> $5E1D,X` and `$9831/$9839 := $1D0C/$1D23,Y` are written by class 4's settle at
`0x13B45-0x13B58`. Class 0 is the other half: when the settled enemy's victim has moved off the
recorded seat, it hands `$5E0D` back and drops the latch — the re-acquire. Mode `00` never reaches it
(the `BEQ` at `0x13722` skips dispatch entirely), so class 0's live modes are **`0x04` and `0x1A`** —
and `0x04` is cmb5's mode on the very state S6 uses. S6.3's "cmb5 static" is evidence that class 0
**arms no segment**, not that it does nothing. Shipping `class 0 -> nothing` drops a real mechanism.

---

## V3 — S4/S8 step 6 miss that SIX handlers clear class 6's init flag

Full scan of the image for `$A5FD,X`:

```
STZ $A5FD,X : 0x13779 (class 0)  0x1379C (class 1)  0x138C1 (class 2)
              0x1395B (class 3)  0x13A3F (class 4)  0x13BC4 (class 5)
INC $A5FD,X : 0x13E46 (class 6, first-entry)          STA $A5FD,X : none
```

So `$A5FD,X` is not "init once" — it is **"has class 6 run since the last dispatch of any other
class"**. Class 6 recaptures its orbit centre `(selfX, selfY+0x10)` and resets its angle to `0xC0`
and its direction to 0 every time a class-0..5 pass intervenes. This bites the exact fixture S9.2
says class 6 must be right for: `ambush_pre` arms `0x0F` (class 5) at f185 and `0x0A` (class 6) at
f1460 (both re-measured this lane) — every class-5 pass in between clears the flag.

Related, and also unlisted: **`$A5F4,X` (class 5's orbit direction) has exactly one writer in the
ROM**, `STA $A5F4,X` at `0x13E24` (the EOR flip). There is no initializer anywhere in the image; its
zero comes from whatever clears WRAM at battle boot (measured `00 x8` on `forest_battle_3hetake`).
S8 step 5 calls it "new per-slot state" without saying where the zero comes from.

---

## V4 — S7.2's FIRST worked example does not reproduce (the answer is right by table saturation)

Recomputed from the ROM tables, with S7.2's own inputs (self `(0x70,0x3D)` = (112,61), target cmb0
`(0xA0,0x47)` = (160,71), r = `0x40`):

| quantity | S7.2 says | this lane computes | source |
|---|---|---|---|
| `a0` (13D63) | `0x88` | **`0x86`** | dx=48 -> `48>>3 = 6`; dy=10 -> `(8<<2) = 32`; idx `0x26`; `F300[0x26] = 06`; `0x80+06` |
| `$DB` after the `+0x10` step | `0x98` | **`0x96`** | `0x86 + 0x10` |
| new ref | `(107,35)` | **`(105,38)` = `(0x69,0x26)`** | `target + 0x40*trig(0x96 / 0xD6)` |
| self->ref | `dx=+5 dy=+26` -> `F300[96]` | **`dx=+7 dy=+23`** -> `F300[0x40]` | |
| final bearing | `0xC0` | `0xC0` | **`F300[0x40] = 0x40` AND `F300[0x60] = 0x40`** |

The final bearing agrees **only because the atan2 base table saturates at `0x40` for both indices**.
S7.2 labels this row "Exact." It is not a reproduction; `a0 = 0x88` is the f461 value carried over
to the f501 pass. **The mechanism still stands** — my model (V0.1) reproduces the f501 segment
exactly from the correct intermediates — but anyone re-deriving from S7.2's printed chain will not
be able to reproduce it, and would not notice that the geometry is off by two table cells.

S7.2's **second** example (cmb4 f483) reproduces exactly, every intermediate: `a0 = 0xA8`,
ref `(0x5B,0x5B)`, `dist^2 = 25`, `$DB = 0xB8`, ref `(0x72,0x51)`, `dx = -26 / dy = +14`,
`idx = 0x23`, `F300[0x23] = 0x0D`, bearing `0xF3`, `F700[0xF3] = 3`, step `(+7,-3)`. CONFIRMED.

---

## V5 — REFUTED: S5.1's "every call in this family passes `$D3/$D4 = target`, so reuse it unchanged"

`C1/0222` returns the bearing **from `$D3/$D4` toward `$D5/$D6`** (verified against the f461 segment:
`$D3` = target, `$D5` = self, result `0x88`, motion `(-8,-2)` = away from the target). Operand order
by handler:

| routine | `$D3/$D4` | `$D5/$D6` | address |
|---|---|---|---|
| class 1 `C1/379A` | **self** | target | `0x137AB` / `0x137BB` |
| class 4 `C1/3A3D` | target | self | `0x13A53` / `0x13A5D` |
| class 5 `C1/3BC2` | target | self | `0x13BD8` / `0x13BE2` |
| `C1/3D40` first call | target | self | `0x13D49` / `0x13D53` |
| **`C1/3D40` second call** | **self** | **ref** | `0x13D9B` / `0x13D8B`+`0x13D99` |
| **class 6 `C1/3E33`** | **self** | **ref** | `0x13EAD`/`0x13EB2` / `0x13E93`+`0x13EAB` |

So "every call in this family passes `$D3/$D4 = target` ... class 5 / `C1/3D40` / class 6 can reuse it
unchanged" is **false for two of the three routines it names**, and S5.1 is the section titled "the
shared primitives (needed to port ANY of the above correctly)". An implementer who follows it gets
every `C1/3D40` and class-6 bearing **180 degrees out**. (S3.4 and S4's own code listings say
"bearing self -> ref" and are right; S5.1's generalization contradicts them.)

---

## V6 — S8 step 4 names a blank line, the wrong routine, and an input the engine does not have

* `src/battle.c:3976` is a **blank line**. The `mvTarget` write is `:3982`, inside
  `battleMoveArm` (`:3981`).
* More important: the ROM's refresh is **at dispatch**, `C1/373B` `0x13742-0x1374A`, i.e. once per
  cadence wrap for every `$96F8`-present slot, *before* the bit-7 filter at `0x13750`. Putting it in
  `battleMoveArm` refreshes it **only at arm time** — precisely the once-only behaviour step 4 exists
  to fix. It belongs in `battleMoveTick`'s per-slot body, on the wrap, next to the dispatch.
* `grep -rn "5E15" src/` returns **comments only** (`battle.c:871/938/1148/1149/1195`). The engine has
  no live `$5E15,X` cell to refresh **from**: `battleMoveArm(h, slot, mode, tgtCmb)` receives the
  already-resolved target by value. Step 4 is not implementable as written without first deciding
  where `$5E15` lives.

---

## V7 — S8 step 3 fixes one gate on `:4169` and leaves the other one

The ROM's `DEC $9897,X` (`0x13724`) is gated by the mode-0 `BEQ` **and by nothing else**. The
per-slot presence gate `$96F8,X` is at `0x1373D`, **inside** `C1/373B` — it gates the DISPATCH, not
the tick. Our `:4168` `if (!battlePresent(s, cmb)) continue;` skips the DEC for absent slots, and
`battlePresent` reads `$AF02` (the alive-list), not `$96F8`.

**Measured, this lane:** on `forest_battle_3hetake.state` slots 3..7 are absent
(`$96F8 = 01 01 01 00 00 00 00 00`, flat for 900 frames) and hold mode `FF`, yet their `$9897`
counters tick every frame (`f0 16 17 06 04 04 04 04 04` -> `f1 16 17 05 03 03 03 03 03` -> ...). So
the ROM ticks counters for absent slots and freezes them only for mode 0; our port does the exact
opposite on both halves. Step 3 as written ("add the mode-0 cadence freeze") addresses one and not
the other. Any fight where a slot becomes present mid-fight inherits a stale phase.

---

## V8 — S8 step 2 has no bound, and past `0x1F` the "class table" is not a class table

`class = rom[0x0CFBAB + mode]` with the RAW mode is right **for the ROM**, but `CC/FBAB` is only a
class table over `[0x00..0x1F]`. `rom[0x0CFBAB+0x20 .. +0x7F]` is unrelated data:

```
20: 00 00 01 00 00 00 00 00 01 01 01 01 01 02 02 02
30: 02 02 03 03 03 03 03 04 04 04 04 04 05 05 05 05
40: 05 06 06 06 06 06 07 07 07 07 07 08 08 08 08 08
50: 09 09 09 09 09 0A 0A 0A 0A 0A 0B 0B 0B 0B 0B 0C
60: 0C 0C 0C 0C 0D 0D 0D 0D 0D 0E 0E 0E 0E 0E 0F 0F
70: 0F 0F 0F 00 00 50 00 A0 00 F0 00 40 01 90 01 E0
```

Values reach `0xF0` (indices `0x50..0x7F`). A mode in `0x20..0x7F` (bit 7 is filtered, so the domain
IS `0x00..0x7F`) would make the ROM `JSR ($3760 + 2*class)` into garbage, and a naive C
`handler[class]` would read 0x1E0 bytes past a 9-entry array. **Any bound the port adds is not in the
ROM**, so this needs an explicit, recorded decision, not silence. No fixture in the tree arms a mode
above `0x10` (censused: `0,1,4,6,0x0A,0x0E,0x0F,0x10`), so it is a hazard, not a live bug.

Also: S8 step 2's prose "class 1 (modes 01/02/03/05)" is **not the table** — `CC/FBAB` also gives
class 1 for **`0x1C` and `0x1D`**, class 2 for `0x07/0x1B/0x1E/0x1F`, class 0 for `0x1A`. Harmless
while the dispatch is table-driven; wrong if anyone turns the prose into a switch.

---

## V9 — S3's class-5 listing skips the whole "still far" arm, and never says who SETS `$9791`

S3.2 stops at `0x13CC3`; S3.3 shows `0x13CC6-0x13CD2`. `0x13CD4-0x13D3F` is never listed. It
contains the segment staging, the `$9875` mode-`0x0F` relax, both probes, and — the part that matters
— the BLOCKED arm:

```
13D0E LDX $9C
13D10 CLC LDA $DB ADC #40 / LSR x6 / ASL x6 / STA $DB   ; bearing := ((b + 0x40) >> 6) << 6
13D23 LDA $DB / STA $9786,X
13D28 INC $9791,X          ; <<< THE ONLY SETTER of the latch S3.1 and S3.4 read and clear
13D2B STZ $98A7,X          ; arm nothing
13D2E BRA ->13D3A
13D30 LDX $9C / STZ $9791,X / LDA #01 / STA $98A7,X     ; clear path: ARM + drop the latch
```

`0x13CD4-0x13D3F` is structurally class 4's `0x13B60-0x13BC1` with `CMP #0F` in place of `CMP #03`
plus the `$9875` write, so "same body as `battleMoveReturn`" (S8 step 5) does cover it — but only for
a reader who already knows that body. As printed, S3 documents a class 5 that reads a latch nobody
sets.

(Related citation slip: S3.1 says the quadrant matrix is "byte-for-byte class 4's `13AA9..13AF6`".
The identical `0x3A`-byte run starts at **`0x13ABC`**: `rom[0x13ABC:+0x3A] == rom[0x13C46:+0x3A]`
exactly; at `0x13AA9` zero of 58 bytes match. The port's own comments (`C1/3AC0`, `3ACE`, `3ADE`,
`3AE8`) are correct; the dossier's start address is not.)

---

## V10 — S1's justification for the raw 16-bit index is wrong (the conclusion survives)

S1 row 2: "The `TDC` at `0x13719` is what makes the `TAX` a clean 16-bit index (B is never touched by
the 8-bit loads in between)". B **is** touched: the dispatched handlers run `REP #20` code —
`C1/01F9` (`0x101FB ASL / ASL / AND #03FF / TAX`), `C1/2B49` (`0x12B57 REP #20`, `0x12B9D REP #21`).
What actually restores `B = 0` before the next iteration's `TAX` at `0x13755` is the `TDC` executed in
16-bit-A mode at **`0x100D1`** (`C1/00A7`), **`0x10269`** (`C1/0222`) and **`0x12BAC`/`0x12BB5`**
(`C1/2B49`'s two exits). The conclusion (raw index, no mask) holds on every path I measured, and
`CC/FBAB[0x10] = 04` still means `& 0x0F` deletes class 4 for mode `0x10` — but nobody has shown that
`C1/285A`, `C1/2926` and `C1/28B0` also return with `B = 0`, and the stated reason is not a reason.

---

## V11 — uncited gates the fix leaves sitting on the same path (S10 does not list them)

* **`src/battle.c:4173` `h->mvPendAtk != cmb`** and **`:4179` `if (h->mvPendAtk == cmb) continue;`** —
  no ROM address anywhere in the comment; the justification is an oracle observation ("the f461 pass
  ... never armed"). The ROM's per-slot dispatch gate is `$96F8,X` (`0x1373D`), which is **flat
  `01 01 01 00 00 00 00 00` for the whole 900-frame window and never drops for the acting enemy** —
  so `mvPendAtk` is not `$96F8`. Under the mandate this is a stand-in heuristic sitting **between the
  class dispatch and the handlers**: if it suppresses a pass the ROM performs, the newly routed
  classes will still arm on the wrong frames, and the fix will read as "class 5 geometry is wrong".
  S10.6 flags `$96F8` but not this.
* **`:4168` `battlePresent`** — see V7.

---

## V12 — S9's census is RIGHT (I re-ran six of its seven rows)

Every census row I re-ran reproduced exactly, including the two that carry the most weight:
`ambush_pre` **with `CT_BATTLE_MASHA=1`** (the pinned check's own env) really does arm `0x0F` at
**f185** and `0x0A` at **f1460**; `forest_pre` **with MASHA** really does arm nothing but modes `0`
and `4`, so `field-battle-enemy-death-sfx`'s `pan=78/58/A8` and `field-battleloop-forest` cannot
move; `gato_t0` under `CT_BATTLE_SCRIPT=n:900` really is modes `0/1/6` only (264 `battlemove`
lines); `party3_field` under bat9's own argv really does arm `0x0A` at **f976**. S9's refutation of
R10 stands, and S9.4's alarm ("if steps 1-2 move a GATO check, the table was masked") is the right
tripwire.

---

## V13 — the ACCEPTANCE CRITERION cannot be measured on our engine (demonstrated)

S6's numbers are re-measurable — I re-measured all of them. But they are **oracle frames on
`saves/battle_re/forest_battle_3hetake.state`, a mid-fight LakeSnes state, and nothing in the tree
runs our engine from it.** I tried the obvious route:

```
ct_probe --peek saves/battle_re/forest_battle_3hetake.state <scratch>/f3h.raw --btnscript "0:1"
   -> writes f3h.raw.wram (131072 bytes)
Belthasar --assets ct_assets.dat --scene 19 --battletest <scratch>/f3h.raw.wram --frames 800
   -> "[battletest] seeded VM from ... lead obj2 at (0,0)" and then ZERO scan/mode/battlemove lines
```

`--battletest` stages a battle from a **pre-battle field** WRAM; a mid-fight WRAM produces no combat
at all. S6.4's own measurement of ours uses a completely different scenario
(`battle_crono_solo.dumpwram` + `l:400`; and `forest_pre` arms its first `0x0E` at f724, not f461).
**So there is no stated and no obvious procedure to hold the port against f461/f501/f541/f581/f621/
f661.** As written the acceptance target grades the ORACLE, not the fix.

What would make it landable, in order: (a) restate the target on a fixture the engine actually runs —
`forest_pre.raw.wram` plain, first `0x0E` arm at **f724** (re-measured this lane) — and (b) capture
the oracle driven from the *same* staging (`capture-oracle-state`), so the two frame axes are
comparable, before quoting any per-frame bearing/position pair as a pass/fail.

---

## V14 — corrected change list (still NOT applied; read-only lane)

1. **`:4085` / `:4117`** — substitute the slot's live mode for `s[0xA2B5]`. Identical by construction
   (`C1:3752` stores the mode immediately before `JSR ($3760,X)`, single writer).
2. **`:4132` — DELETE the gate, do not substitute into it.** The probes are unconditional for class 4
   (`0x13B7F`, `0x13B8B`); the only mode-keyed skip in that tail is the settle (`0x13B1D CMP #10`),
   already handled at `:4117`. **Do this in the same commit as step 1 or mode `0x10` regresses.** (V1)
3. **`:4173-4178`** — dispatch on `class = rom[0x0CFBAB + mode]`, raw mode, bit 7 filtered once
   (`BMI 0x13750`), no `& 0x0F`; record an explicit decision for `mode >= 0x20` (V8). Route
   `1 -> battleMoveApproach`, `4 -> battleMoveReturn` (modes `06` **and** `0x10`).
   `0 -> C1/3772`, which is **not** a no-op (V2) — either port the re-acquire or land it as a
   documented, cited omission.
4. **`:4168`/`:4169`** — the DEC is gated by mode-0 only (`0x13722`); the presence gate belongs on the
   DISPATCH (`0x1373D`), not the tick (V7). Validate `field-battle-gato-walk` in isolation, exactly as
   S9.4 says.
5. **the `$9752 <- $5E15` refresh** — in `battleMoveTick`'s per-slot wrap body, not `battleMoveArm`;
   decide where `$5E15` lives first (V6).
6. **Class 5** = `C1/3BC2` + `C1/3C81` + **the full `0x13CD4-0x13D3F` arm incl. the `$9791` setter at
   `0x13D28`** + `C1/3D40` + the mode-`0x0D` stop. `C1/0222` operand order for `C1/3D40`'s second call
   is `(self, ref)`, NOT `(target, self)` (V5). `$A5F4,X` has no ROM initializer (V3).
7. **Class 6** = `C1/3E33`, plus the six `STZ $A5FD,X` clears in classes 0-5 (V3). Still
   **oracle-unwitnessed** — S4's own label stands; capture a state first or ship it labelled
   HYPOTHESIS.
8. Acceptance restated per V13 before anything is called validated.

Forbidden here as everywhere: `if (id == 0x5E)`, `if (id == 0x19)`, a slot/scene gate, an eyeballed
constant. cmb5 must stay at `(A8,7F)` — that falls out of `CC/FBAB[0x04] = 0`, and V2 says class 0
still has work to do while it stands there.

---

## V16 — CLASS 5 IS NOW FULLY DECODED (2026-09-08), and the landing blocker is a MEASURED one

V14 item 6 said class 5 still needed "the full `0x13CD4-0x13D3F` arm incl. the `$9791` setter at
`0x13D28` + `C1/3D40` + the mode-`0x0D` stop". All of it is decoded here, and one thing V14 did not
know about — the orbit-direction flip — turned out to be the whole point of the handler.

### V16.1 The instrument that removes V13's blocker: `--callrom`

V13 is right that no fixture holds our engine against `forest_battle_3hetake`'s frames. It does not
matter for the HANDLER, because the handler can be called on its own:

```
build/ct.exe --callrom C1/3BC2 --from saves/battle_re/forest_battle_3hetake.state \
    --regdb 7E --regdp 0000 --writes \
    --pokewram "7E009C=00,7E009D=00,7EA2B5=0E,7E9752=00,7E9791=00,7E96F8=01,7E974A=0E"
  callrom: ret ops=856 cycles=18310 ... verdict SEC ... 208 write(s)
    w 7EA5FD = 00  (by C1:3BC7)      w 7EA5FC = 20  (by C1:3BF7)
    w 7E98AF = 78  (by C1:3BEA)      w 7EA5FC = 40  (by C1:3C01)   <- the mode-0x0E doubling, IN EXECUTION
    w 7E98B7 = 3F  (by C1:3BF2)      w 7E9893 = 00 / 7E9894 = 01 (by C1:2B24/2B29)  <- threshold 0x0100
```

`$9C` must be poked: the state's own `$9C` is `0x0008`, an empty slot, and the handler then writes only
`$978E`/`$A605` and returns. With a real slot it runs the whole body. **R1's corrected radius and R5's
corrected threshold are both confirmed by the ROM executing, not by reading it.**

### V16.2 The head is class 4's, byte for byte

`rom[0x13ABC:+0x3A] == rom[0x13C46:+0x3A]`, and the operand setup is identical on both sides —
`$D3/$D4` from the move target `$9752,X` through `$1D0C/$1D23`, `$D5/$D6` from self `$1D0F/$1D26,X`,
which also become the segment base `$98AF/$98B7`. The only difference is the radius block:

```
class 4  C1/3A6D  LDA #40 / STA $AE      ; CMP #10 -> #80
class 5  C1/3BF2  LDA #20 / STA $A5FC    ; C1/3BF7 LDA $A2B5 / CMP #0E / BNE / ASL $A5FC -> 0x40
```

### V16.3 ⭐ "SETTLED" DOES NOT PARK — IT ORBITS. `C1/3D40` is the wander.

This is what the earlier passes missed. Class 4 and class 5 branch the SAME way on the probe verdict
(`$9872` bit 7 set -> walk the tail), but their other arm is different:

```
class 4  13B3B  LDA $9872 / BMI ->13B60(tail)   ; else SETTLE: arrival latch, STZ $98A7, park
class 5  13CBE  LDA $9872 / BMI ->13CC6         ; else JMP C1/3D40   <-- the ORBIT arm
```

`C1/3D40`, decoded:

```
13D40  LDX $9C / STZ $9791,X                     clear the bearing latch
13D45  target $9752,X -> $D3/$D4 ; self -> $D5/$D6 and $98AF/$98B7 (the base)
13D63  JSR C1/0222                               raw = atan2(target, self)   [PHA]
13D69  LDA $A5F4,X / BNE                         the ORBIT DIRECTION flag
13D6E    LDA #10   (clear)   13D72  LDA #F0  (set)
13D74  ADC $DB / STA $DB                         bearing = raw +/- 0x10   (22.5 degrees)
13D79  LDA $A5FC / STA $AE                       radius = the class-5 radius
13D7E  refY = targetY + trig(bearing)            13D8B  refX = targetX + trig(bearing+0x40)
13DA5  JSR C1/0222                               b2 = atan2(SELF, REF)   <- (self, ref), per V5
13DA8  PLA / ADC #80 / CMP $DB / BNE             exactly reversed?
13DB0    INC $DB ; LDA $A5FC / CMP #20 / BNE / INC $DB    nudge 1, twice at radius 0x20
13DBB  LDA #08 / STA $AE                         step 8
13DC1  STA $9786,X ; C0F700[$DB] -> $96DE,X      latch the bearing, set the facing
13DE1  destination -> $A053/$A03C,X ; JSR C1/285A ; STZ $9875 ; mode 0x0F -> $9875 = 1
13E0C  JSR C1/2926 (arena)  / mode 0x0F skips  JSR C1/28B0 (victim box)
13E1D  LDA $A5F4,X / EOR #01 / STA $A5F4,X       ⭐ BLOCKED -> REVERSE THE ORBIT, arm nothing
13E29  else                                      arm the segment
```

**So the wander is an orbit around the target at radius `$A5FC`, stepping 22.5 degrees per pass, that
reverses direction whenever the step is blocked.** `$A5F4` has exactly ONE writer in the 4 MB image —
`C1/3E24`, that flip — so a port becomes its sole writer too, exactly like the ROM. (V3 is right that
nothing initialises it; the ROM reads whatever the slot holds, and so should we.)

The far arm is class 4's tail with two edits (`rom[0x13B62:+29] == rom[0x13CD4:+29]` exactly): class 5
inserts `LDA $A2B5 / CMP #0F / BNE +5 / LDA #01 / STA $9875` after the `STZ $9875`, and its
second-probe skip compares `#0F` where class 4 compares `#03`. Plus the mode-`0x0D` park,
`13CC8 LDA $A2B5 / CMP #0D / BNE / STZ $98A7,X`.

### V16.4 ⚠ A CLAIM THIS SECTION MADE AND THEN RETRACTED: the dispatch is NOT starved

First measurement said our CLASS DISPATCH fires "seven times in a whole fight, and only ever with
modes `00` and `04`", and blamed the `$A013` half of our gate. **That was a measurement error and it
is withdrawn.** The run used `CT_BATTLE_MASHA=1`, which mashes the party's attacks and ends the forest
fight before the Hetakes' `0x0E` windows ever come round. Re-measured on the plain no-input drive
(`CT_BATTLE_SCRIPT=n:1400`), counting the cadence WRAP rather than the guard:

```
   73 x  mode=0E  pass=1        <- class 5, dispatched
   40 x  mode=04  pass=1
    6 x  mode=00  pass=1
    1 x  mode=0E  pass=0
```

The engine's unfrozen duty cycle is not the problem either. Our own `$A013` gaps on that drive are
18 / 287 / 298 / 7 / 18 / 282 / 8 / 17 / 283 = **~34% of frames free**, against the oracle's
51 / 22 / 282 / 17 / 7 / 73 = **~32%** on `forest_battle_3hetake` — including a long gap of the same
size (282-298), which is exactly where both sides wander.

### V16.5 THE GATE, DERIVED (this is what R8 left open)

R8 recorded that our `if (s[0xA013] || s[0x99E0]) return;` has an `$A013` half that is "consistent with
the oracle but not cited to a branch". It is now cited, and the citation is structural rather than a
test:

```
C1/3714  LDA $99E0 / BNE ->1373A(RTS)          the mode machine gates on $99E0 ALONE
C1/10B5  JSR C1/3714      \  the two call sites, both inside the routine at C1/1090
C1/10DC  JSR C1/3714      /  (frame wait, work, dispatch, frame wait, work, dispatch, RTS)
C1/10FA  LDA $993A / ...                        a SEPARATE ENTRY -- and it has NO JSR C1/3714
C1/40B6  JSR C1/10FA                            the ACTION frame loop calls THAT one
```

So the mode machine genuinely does not run during an action span — not because anything tests
`$A013`, but because the action loop advances its frames through `C1/10FA`, the entry that omits the
dispatch. Our `$A013` term is the right behaviour standing in for "we are inside `C1/4090`'s loop", and
that is now a derivation instead of a guess. (`$99E0`, the Wait-mode freeze, is the ROM's own test and
was already right.)

### V16.6 LANDED, and what grades it

`battleMoveWander` is in `src/battle.c`, routed from the class dispatch as `cls == 5`. On the plain
drive the Hetakes move, and the **first class-5 segment lands at f724** — which is not a number chosen
here: V13 itself restated the acceptance target as *"`forest_pre.raw.wram` plain, first `0x0E` arm at
**f724** (re-measured this lane)"*. Ours arms and moves on that frame.

Five checks moved with it, all of them on R10's predicted list — `field-battle-hetake-pose-ladder`,
`-attack-sfx`, `field-battle-hurt-flash-script-bound`, `field-battle-stale-deadflag-enemies-act`,
`field-battle-ambush-generic-dissolve` (the id-`0x19` fixture R4 named, where mode `0x0F` now also
runs). E.g. the Hetake's attack sound goes `f1053 rel=51` -> `f1056 rel=54`: an enemy that moves
reaches its attack position on a different frame. R10 listed thirteen more as "likely movers"; none of
them moved.

⚠ **WHAT IS AND IS NOT VALIDATED.** The MECHANISM is: every constant is byte-cited, and the two the
earlier reviews corrected (radius `0x20`/`0x40`, threshold `0x0100`) are confirmed by the ROM
executing under `--callrom`. The TIMELINE is not: V13's staging gap still means no oracle capture runs
the same fight our fixture does, so the five re-pinned values are our own render/sfx frames, not
oracle-checked ones. They replace pins of a fight in which the enemies never moved at all, which is
why re-pinning is the right call and not a papering-over. Closing that last gap needs a capture from
the same staging as `forest_pre.raw.wram`.

Graded by four mutations on `field-battle-hetake-attack-sfx`: the class-5 route, the mode-`0x0E`
radius doubling, and the `0x0100` threshold are all KILLED. A fifth was written and **removed because
it SURVIVED the whole suite**: neutering the orbit reversal (`C1/3E24 EOR #01`) changes nothing any
check can see, because no pinned fixture blocks a wander step inside its window. That is a real blind
spot, recorded rather than papered over -- closing it needs a fixture where a Hetake's orbit runs into
the arena edge or a PC box.

Class 6 was left out here; **V17 overturns the label it was left out on.**

---

## V17 — CLASS 6 IS WITNESSED, DERIVED AND LANDED (2026-09-08)

### V17.1 The "ORACLE-UNWITNESSED" label was stale

S4 labelled class 6 unwitnessed and V14 item 7 kept it out on that basis ("capture a state first or
ship it labelled HYPOTHESIS"). The repo already had the state. On
`saves/battle_re/forest_wait_ambush_active.state`, `--monitor "7E974A:8" --diff --writer` over 5000
frames returns, among others, `FF0104FFFF0AFFFF`, `FF0110FFFF0AFFFF`, `FF1004FFFF0AFFFF` — **slot 5
holds mode `0x0A`**, i.e. class 6. Its timeline on that state:

```
f430  slot5 = 0F   (class 5)
f2158 slot5 = 0A   (class 6)      <-- witnessed, ~490 frames
f2650 slot5 = 0F
f3363 slot5 = 0A
f3896 slot5 = 0F
```

and the enemy's live position confirms the change of character: it walks straight up at constant X
(`cmb8` Y 174 -> 159, f2102-f2170) and then turns and tracks sideways (X 138 -> 140 from f2186).

### V17.2 What class 6 is (C1/3E33, modes 0A/0B/0C)

Same orbit family as class 5, with three differences that are the whole character of it:

1. **IT ORBITS A REMEMBERED CENTRE, NOT THE LIVE TARGET.** On its first pass since any other class ran
   (`C1/3E41 LDA $A5FD,X / BNE`), it captures `centre = (selfX, selfY + 0x10)` into `$A605/$A60D,X`,
   seeds its angle `$A615,X` to `0xC0` and its direction `$A61D,X` to 0. V3 is right that `$A5FD,X` is
   not init-once: six handlers clear it and only `C1/3E46` sets it, so any intervening class makes the
   formation RE-CAPTURE. **The angle is PERSISTENT across passes** — class 5 recomputes its bearing
   from the target every time; class 6 advances a stored one.
2. **THE ORBIT IS AN ELLIPSE.** `C1/3E86` puts the radius in `$AE` for the Y component, then `C1/3E99`
   `LDA $A625 / ADC #20` puts **radius + 0x20** in `$AE` for the X component. And the radius is `0x20`
   **HALVED** to `0x10` for mode `0x0A` (`C1/3E80 LSR $A625`) — note the direction against class 5,
   which DOUBLES for its special mode. Mode `0x0A` therefore sweeps `0x10` by `0x30`: wide and flat,
   which is what the oracle's sideways track looks like.
3. **BLOCKED BACKS OFF BEFORE IT REVERSES.** `C1/3F1A` loads `0x58` (`0x30` for mode `0x0A`) and
   `C1/3F30`/`C1/3F38` subtracts it from the persistent angle when the direction is 0, adds it when it
   is 1 — rewinding the orbit — and only then does `C1/3F46` flip the direction. Class 5's flip
   (`C1/3E24`) has no rewind.

The tail is class 5's shape with `#0C` where class 5 has `#0F` (the `$9875` relax and the victim-box
skip).

### V17.3 Validated in execution

```
--callrom C1/3E33 --from saves/battle_re/forest_wait_ambush_active.state --regdb 7E --regdp 0000
    --writes --pokewram "7E009C=05,7E009D=00,7EA2B5=0A,7EA602=00,..."
  w 7EA602 = 01  (C1:3E49)   the init flag INC          w 7EA61A = C0  (C1:3E5D)  angle seed
  w 7EA612 = 10  (C1:3E52)   centre Y = selfY + 0x10    w 7EA61A = D0  (C1:3E72)  angle += 0x10
  w 7EA60A = 00  (C1:3E58)   centre X = selfX           w 7EA625 = 20 -> 10 (C1:3E79/3E83)  HALVED
```

Every one matches the ported handler. In our engine the ambush fixture now runs **104 class-6
segments** (`mode10` = `0x0A`), and slot 5 tracks sideways at constant Y — the ellipse's wide axis.

### V17.4 Cost, and the blind spots

**ONE** check moved: `field-battle-bat9-return`, `f2270 -> f2286`, with `fieldfnv=CA18873D`
**byte-identical** — the fight takes 16 frames longer because the enemies move, and the field state it
returns to is unchanged. Re-pinned.

Graded by two mutations on that check (dropping the class-6 route; making the orbit a circle) — both
KILLED. **Three more were written and REMOVED because all three SURVIVED the whole suite together**:
the mode-`0x0A` radius halving, the blocked-path angle rewind, and class 4's `STZ $A5FD,X`. Nothing in
the suite reaches a fixture where those matter — bat9's class-6 modes are not `0x0A`, no pinned window
blocks a formation step, and no class-4 pass intervenes between class-6 passes there. Recorded as
blind spots, not papered over; closing them needs a fixture that runs the ambush's mode-`0x0A`
formation past a wall.

Class 0 landed next; see V18. Still out: **classes 2/3/7/8** (`C1/38BF`, `C1/3959`, `C1/3F5A`,
`C1/4021`), which have no witnessed mode in any capture this repo owns. Do not synthesise them.

---

## V18 — CLASS 0 IS THE RE-ACQUIRE, AND IT IS LANDED (2026-09-08) — but it never fires

V2 refuted "class 0 -> nothing" and it is right. Re-verified byte for byte:

```
C1/3772  LDX $9C
C1/3774  LDA $9829,X / BEQ ->RTS           no arrival latch -> nothing to re-acquire
C1/3779  STZ $A5FD,X                       (the sixth of class 6's re-capture clears)
C1/377C  LDA $9752,X / TAY                 the move target
C1/377F  LDA $1D0C,Y / CMP $9831,X / BNE ->3790
C1/3788  LDA $1D23,Y / CMP $9839,X / BEQ ->RTS     victim still on the recorded foot -> nothing
C1/3790  LDA $5E1D,X / STA $5E0D,X         HAND THE SAVED MODE BACK
C1/3796  STZ $9829,X                       and drop the arrival latch
```

and the other half, class 4's settle, likewise:

```
C1/3B42  INC $9829,X                       the arrival latch
C1/3B45  LDA $5E0D,X / STA $5E1D,X         SAVE the requested mode
C1/3B4F  LDA $1D0C,Y / STA $9831,X         RECORD the victim's foot
C1/3B55  LDA $1D23,Y / STA $9839,X
```

So an enemy that has ARRIVED at its victim resumes its approach the moment that victim walks away. Our
engine had **zero references** to all five bytes (`$5E0D`, `$5E1D`, `$9831`, `$9839`, `$9829` — the last
modelled host-side as `enArrive`), so the settle recorded nothing and there was nothing to re-acquire
from. Both halves are ported now, plus the ROM's own `C1/3722 LDA $974A,X / BEQ` mode-0 dispatch skip,
which was inert while class 0 did nothing and is not inert once it does something.

### ⭐ V18.1 THE REAL FIND: `$5E0D` and `$974A` are two different things, and this port has one

`C1/3364` is `LDA $5E0D,X / STA $974A,X`. **`$5E0D` is the REQUESTED move mode; `$974A` is the LIVE
one**, and the commit is a separate step. `battleMoveArm` writes straight into `mvMode`, collapsing them.

MEASURED, so the size of the gap is known rather than assumed (`--monitor "7E5E0D:8,7E974A:8,7E9829:8"
--diff`):

```
bat3  f283 $5E0D slot0 -> 0E   f286 $974A follows      the request LEADS by ~3 frames
      f298 $5E0D slot2 -> 00   f299 $974A follows
bat9  f175 $5E0D slot5 =  0A   $974A slot5 stays 01    they hold DIFFERENT values for a long stretch
```

and `$9829` is a COUNTER (bat9 shows 01 and 02 on the same slot), where `enArrive` is a boolean. So the
collapse is a real simplification, not an identity. Splitting them is its own ticket, and it is what
would let class 0 be graded at all.

### ⚠ V18.2 It never fires — measured on BOTH sides

`C1/3793` is class 0's own `STA $5E0D,X`. Over 2200 frames on bat3, bat9 and bat2, `--monitor
"7E5E0D:8" --diff --writer` attributes **every** write of `$5E0D` to `C1:9833` or `C1:9891` (the AI
decode) and **none to C1:3793**. Instrumenting our own handler the same way gives 0 firings on
`forest_pre`, `ambush_pre` and `bat9`. The suite is unchanged, 416 pass / 0 fail.

So this is a derived mechanism that is **landed, inert, and provably introduces no divergence** — the
condition (a class-4 settle whose victim then walks off the recorded foot) is not reached by any capture
this repo owns. It is here because shipping `class 0 -> nothing` drops a real mechanism, not because
anything grades it. Do not read the green suite as validation of it.


---

## V15 — verdict

* **Mechanism: DERIVED, reproduced.** Every ROM address in S1-S7 that I re-disassembled holds the
  claimed instruction (with the two citation slips in V4/V9), every oracle value I re-measured on a
  state I loaded myself matches, and an independent scratch model built only from `C0/F300`,
  `C0/F700`, `C0/F900` and the disassembly reproduces three of the acceptance segments byte-exactly.
  The bug in `src/battle.c` (dispatch on the MODE, not the CLASS) is real and measured twice
  (`166 mode=14` / 0 moves; `72 mode=16` / 0 moves).
* **Fix: NOT LANDABLE AS WRITTEN.** V1 (step 1 breaks mode `0x10`), V6 (step 4 wrong site, missing
  input), V7 (step 3 half-done), V2 (class 0 is not "nothing"), V3 (class 6's init flag), V13 (no
  measurable acceptance). None of these is fatal to the derivation; all of them are fatal to a
  copy-paste implementation.
* **Forbidden patterns: none found in the proposed fix.** The dispatch is a ROM table indexed by a
  ROM-derived per-slot RAM byte; there is no id, scene, slot or "player vs NPC" gate anywhere in S8.
  The one invention risk is the `mode >= 0x20` bound (V8), which must be recorded as a port decision
  rather than presented as ROM behaviour.
* **Simpler/different cause: none found.** I looked for one; the 166 arms with zero moves and the
  single-writer proof leave no room for an alternative.

### V16 — commands this lane ran (cwd = repo root; nothing written into `build/` or `src/`)

```
mkdir -p "$TMP/ctwf4/movers-adv"; cp build/ct.exe .../ct_probe.exe; cp build/Belthasar.exe ...; cp build/SDL2.dll ...
python tools/dis.py 13714 0x60 1 0     # dispatcher (BEQ 13722, STA 13752, LDA $CCFBAB)
python tools/dis.py 1379A 0x70 1 0     # class 1 head (operand order)
python tools/dis.py 13A3D 0x60 1 0 ; python tools/dis.py 13AF7 0xA0 1 0 ; python tools/dis.py 13B90 0x40 1 0
python tools/dis.py 13BC2 0xC0 1 0 ; python tools/dis.py 13C81 0xC0 1 0 ; python tools/dis.py 13D40 0x100 1 0
python tools/dis.py 13E33 0x110 1 0
python tools/dis.py 13772 0x30 1 0 ; python tools/dis.py 138BF 0x20 1 0 ; python tools/dis.py 13959 0x20 1 0
python tools/dis.py 13F5A 0x20 1 0 ; python tools/dis.py 14021 0x20 1 0
python tools/dis.py 12AE3 0xD0 1 0 ; python tools/dis.py 12B80 0x40 1 0
python tools/dis.py 10222 0x80 1 0 ; python tools/dis.py 101F9 0x30 1 0 ; python tools/dis.py 100A7 0x30 1 0
python -c "...re.finditer(8D B5 A2)..."                 # STA $A2B5 -> ['0x13752'] only
python -c "...STZ/STA/INC of A5FD,X A5F4,X A615,X..."   # the six clearers (V3)
python -c "...CC/FBAB[0x00:0x80], C0/F300, C0/F700, C0/F900..."   # tables + the scratch geometry model (V0.1)
python -c "...rom[0x13ABC:+0x3A] == rom[0x13C46:+0x3A]..."        # the matrix, corrected offset (V9)

<probe> --read    "1D0C+3,1D23+3,9752+3,974A+3,A5FC+1"        --from saves/battle_re/forest_battle_3hetake.state
<probe> --read    "989F+8,9897+8,96F8+8,A5F4+8,A5FD+8,9829+8,5E0D+8,5E1D+8" --from <same>
<probe> --monitor "A2B5:1,974A:8,98A7:8,96F8:8,A5FC:1"  --from <same> --input "0:520" --diff
<probe> --monitor "9897:8"                              --from <same> --input "0:40"  --diff
<probe> --monitor "5E15:3,9752:3,A5F4:3,9791:3"         --from <same> --input "0:700" --diff
<probe> --monitor "1D0F:3,1D26:3,9786:3,96DE:3,98A7:3"  --from <same> --input "0:700" --diff   # the acceptance table
<probe> --peek <same> <scratch>/f3h.raw --btnscript "0:1"     # V13

CT_BATTLE_MVTRACE=1 CT_BATTLE_SCANTRACE=1 <snap>/Belthasar.exe --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest <scratch>/o.raw --frames 2400 --walkscript "l:400,."
<snap>/Belthasar.exe ... --battletest saves/forest_re/batalha_nao_comeca.raw.wram --frames 2400
<snap>/Belthasar.exe ... --battletest saves/battle_re/{ambush_pre,forest_pre,forest_cyclone,gato_t0}.raw.wram
<snap>/Belthasar.exe ... --scene 130 --loadwram saves/battle_re/party3_field.lakewram --walkscript "l:150"
<snap>/Belthasar.exe ... --battletest <scratch>/f3h.raw.wram --frames 800                       # V13, produces nothing
```

---

## V19 — THE `$5E0D` / `$974A` SPLIT: what it is, what it costs, and why it has no faithful port yet

V18.1 found that this engine collapses two ROM bytes into one `mvMode` and measured that they diverge.
This is the follow-up: the split is now fully derived, quantified, and **deliberately not ported**,
because the one thing a port needs is a subsystem this repo has already recorded as underived.

### V19.1 It is a SPRITE-REBUILD CHANGE DETECTOR, not a latch for its own sake

`$974A` has exactly **two** writers in the 4 MB image — `CC/E34E` (the boot clear) and `C1/3367`. That
one write sits inside a change detector:

```
C1/333D  LDA $9841 / TAX                     the ROTATING CURSOR
C1/3341  LDA $992D,X / BMI ->1334E           roster gate      -> JMP C1/348C (advance, no commit)
C1/3349  LDA $96F8,X / BNE ->13351           present gate     -> JMP C1/348C
C1/3351  LDA $974A,X / CMP $5E0D,X / BNE ->13364    live != requested?
C1/3359  LDA $96DE,X / CMP $96E9,X / BNE ->13364    facing != published?
C1/3361  JMP C1/3407                         neither changed -> SKIP THE REBUILD
C1/3364  LDA $5E0D,X / STA $974A,X           commit the mode
C1/336A  LDA $96DE,X / STA $96E9,X           publish the facing
C1/3370  ...                                 and REBUILD the sprite assembly ($1CC3,X ...)
```

So the requested/live pair exists so the engine can answer "has anything about this combatant's pose
changed since I last built its sprite?" — the mode and the facing are checked together and committed
together. Everything ARMS `$5E0D` (the AI decode at `C1/9830`/`C1/988E`, class 0's re-acquire at
`C1/3793`, and four more); exactly one place commits it.

### V19.2 The commit is ONE SLOT PER VISIT, off the `$A4` group scheduler

`$9841` is a rotating cursor: `C1/3234 STZ` (reset), `C1/333D LDA` (read), `C1/348C INC` (advance),
`C1/348F LDA` (wrap test). The reset sits in `C1/3232`, the three-way group dispatcher that decrements
`$A4` and jumps to `C1/3252` / `C1/32A5` / `C1/32FC`. So a combatant's requested mode goes live only
when the cursor reaches it, and the skip paths (`$992D` bit 7, `$96F8 == 0`) advance the cursor WITHOUT
committing — which is why a slot can hold a stale live mode indefinitely.

### V19.3 How much it costs, measured

Over nine captures, sampling every `--diff` transition row of `7E5E0D:8` + `7E974A:8` and ignoring
empty slots:

```
live slot samples          193
request != live              5   (2.6%)
   ...and a DIFFERENT CLASS  5   (2.6%)      e.g. bat8 f917 slot1 req=0E(cls5) live=01(cls1)
                                             bat6 f252 slot6 req=04(cls0) live=06(cls4)
```

so the divergence is rare **but never harmless**: every time the two differ, they select a different
movement handler. All five are transients — the AI has just armed a new mode and the cursor has not
reached that slot yet — and `--diff` over-samples exactly those moments, so 2.6% of transition rows is
an upper bound on transition moments, not a duty cycle.

### V19.4 ⚠ WHY IT IS NOT PORTED

A port needs the commit's cadence, and that cadence is the `$A4` group scheduler's visit rate —
which `battle_facing_engine.md` already records as **not derived** ("the 13/7 jitter belongs to the `$A4`
group scheduler, which is not derived"; the idle replay uses the average). Without it there are only
two options, and both are wrong:

* commit at our own dispatch — the divergence window becomes zero, i.e. exactly today's collapsed
  behaviour with more code;
* invent a cadence — the forbidden pattern, and the one the `$A2B5` and the pre-roll work both got
  caught by.

**WHAT WOULD UNBLOCK IT:** derive the `$A4` group scheduler's visit rate. **DONE, 2026-09-08** --
`battle_facing_engine.md`'s new section derives it: `$A4` is a free-running 0..3 counter INCed at
`CF/E477` and wrapped at `CF/E47F`, borrowed and RESTORED by `C1/10A4-10B3` (which is why its DECs never
show in a monitor), with a second capped bump at `C1/34A4`. Measured cycle: 6 or 7 frames, alternating
-- the ADDENDUM's "13/7 jitter" is two of those. What still stands between that and a port here: the
increment is per pass of `CF/E470`, whose rate is the main loop's own, and this consumer needs the
per-combatant cursor as well as `$A4`.

### ⭐ V19.5 — PORTED (2026-09-09): the commit is the idle-tick cursor walk, and it is the idle animation too

`battle_facing_engine.md`'s 2026-09-09 section derives the per-combatant cursor `$9841` and its dispatch
cadence (two of every five frames, on the `$A4`-INC frames that leave the 3-of-5 accumulator at 1 or 0;
frozen with the action span), and `battle.c battleIdleWalk` ports the whole `C1/333D..3489` pass: the
change detector (`$974A != $5E0D` OR `$96DE != $96E9`), the commit + `$97D9`/`$9799` row rebuild, and the
dwell/cursor/publish tick with the `$9912` upload budget. `battleMoveArm` now writes `mvReq` (`$5E0D`,
mode 0 included) and only the walk writes `mvMode` (`$974A`, which boots FF as `CC/E34E` leaves it).
Measured lead reproduced: bat9 f175→f181, forest3 f228→f231 / f243→f244, and the boot's FF→request at the
first dispatch after combat start. V19.3's five class-different transients are now real windows in this
engine instead of zero-length ones. Still collapsed: `$5E15` → `$9752` (copied by the mode machine's own
pass, `C1/3742-374A`, before any handler reads it).

### V19.6 — three loose ends of the split, closed the same day (2026-09-09)

**The move TARGET is a split too — and an unobservable one.** `$5E15,X` (the request) is written by the AI
decode (`C1/9836`, `C1/9894`) and read by `C1/3742` and `C1/99CE`; `$9752,X` (the live target) is written
ONLY by `C1/374A` — `LDA $5E15,X / CMP $9752,X / BEQ / STA $9752,X`, the first thing the per-slot handler
pass `C1/373B` does, before the class dispatch — and by the boot `CC/E351`. Every reader of `$9752` is a
handler (`C1/377C 37BB 37F2 383C 38D4 3940 396A 39E8 3A24 3A4F 3B4B 3BD4 3D45`), and every handler runs
after that copy in the same pass. So a request written at the arm and copied at the pass is indistinguishable
from a request that goes live at the arm: `battleMoveArm` keeps writing `mvTarget`, with this paragraph as the
reason. (bat9, 420 frames: `$5E15` and `$9752` never differ.)

**The commit writes NOTHING to the mover.** `C1/3364-3405` touches `$974A`, `$96E9`, the two rows and — for a
class-1 window — `$9829`; it does not touch `$98A7`, `$9737`, `$9819` or `$9791`, so a segment in flight and
its cadence survive a mode change and the new class's handler inherits them at its next pass. The port
had reset all of those at the commit (an arm-time habit inherited from `battleMoveArm`); removing the reset
moved six pinned sequences — a Hetake's mode-`0x10` (class 6) orbit that used to restart at phase 0 on the
commit now continues from where the previous class left it — all six re-pinned. One port-only latch still
needs clearing at a mode change: `mvStopped`, the "return settled" early-out, which the ROM does not have (its
class-4 pass re-derives adjacency every time, `C1/3B3B`, and its settle writes `$9829/$5E1D/$9831/$9839` and
`STZ $98A7,X`, nothing sticky); without the clear a settled slot never starts its next mode's first segment.
Replacing that latch with the ROM's re-derivation is the mover's own ticket.

**The turn gate's whole set.** `C1/84A3` reads the REQUEST (`LDA $5E0A,X` = `$5E0D+slot`): modes 0, 4, 6 and
`0x0A..0x10` take the turn outright (`BEQ`/`CMP #04`/`CMP #06`/`CMP #0A BCC`/`CMP #11 BCC` → `184BD`); every
other mode — 1 among them — takes it only if the arrival latch `$9826,X` is set (`184B8 LDA $9826,X / BEQ
->184EC`); the turn's first act is `STZ $9826,X`. The port tested `== 1` only. The oracle's forest captures
(forest3 and wait_ambush, 3000 frames each) request `00 01 04 0A 0E 0F 10` and nothing else, so both halves
are exercised on both sides. Ported as written; graded by a mutation on `field-battle-hetake-attack-sfx`
(drop the `0x0A..0x10` range → a wandering Hetake, whose class never sets `$9829`, skips every turn).

---

## V19.7 (2026-09-09, later) -- pointer

The class-4/5/6 body deflector now calls `C1/28B0` against every body at all four sites (`C1/3B8B 3D09 3E18
3F15`), the projected box comes from `C1/285A` with the record's own Y extent, the handlers never reset `$9737`
(a re-armed segment continues at its phase from the re-based origin), the class-4/5 fresh heads write the base,
the orbit's and class 6's blocked branches write no `$98A7`, and class 4's settle leaves nothing sticky (the
port's `mvStopped` latch is gone). All graded on `wa2510` / `wa4620` seeds and on `gato_press_action`. The
derivations, the oracle rows and the boot-wipe finding (`CF/FB92`: `$2D00..$ACEB`) are in
`battle_enemy_move_target.md`, the section "2026-09-09 (later)".


---

# V19 — CLASSES 2, 3, 7 AND 8 PORTED AND GRADED; ALL NINE OF C1/3760 ARE DONE (2026-09-11)

`--sweepmoveclass`: **810/810 byte-exact, 0 mismatch, 0 lost**, 340 segments left armed / 470 stopped.
Eight single-fact mutations, eight red. Pinned as `sweepmoveclass-3760-classes-2378`.

This item had stood on "no witnessed mode in any capture this repo owns, so nothing can grade them —
do not synthesise them". That was right, and it stopped being the whole story the moment
`--sweepaitarget` put a bank-`$C1` micro-execution harness in the tree.

## V19.1 They are not parameters of class 1

| | class 1 (`379A`) | 2 (`38BF`) | 3 (`3959`) | 7 (`3F5A`) | 8 (`4021`) |
|---|---|---|---|---|---|
| bearing | stored while `$9791,X` set, else atan2 | atan2, **always** | one of 4 cardinals | from the MODE, no target | atan2 to a FIXED point |
| `$98A7,X` armed | at the END, clear path | **before** the tests | before | before | before, and that is all it does |
| stop rule | DISTANCE threshold | **a block IS the arrival** | block **or** same 32-px cell | block only | never |
| when blocked | DEFLECT (`+$40` snapped `$C0`, `INC $9791`) | stop + full bookkeeping | same | disarm + count only | — |
| `STZ $A5FD,X` | yes | yes | yes | **no** | **no** |

Class 8 is the smallest handler in the table: re-base, aim at `($80,$70)`, take the facing, arm the
segment, `RTS`. No step magnitude (`$AE` is never loaded), no `C1/285A` box, no collision test, no
arrival. Mode `0x19` alone.

Class 3's arrival is `C1/0119` — `LSR A` **five** times, a 32-pixel cell, not the 16-pixel tile the
collision grid uses.

Class 7's `else` arm (bearing `$E0`, facing 0) is **not** a defensive default: `CC/FBAB[0x18] = 07`,
so mode `0x18` reaches it.

## V19.2 The class table, and which modes each class owns

`CC/FBAB[0x00..0x1F]`:

```
[32 ROM bytes at 0xCFBAB: python tools/dis.py 0xCFBAB 32]
```

→ class 0: `00/04/1A` · 1: `01/02/03/05/1C/1D` · 2: `07/1B/1E/1F` · 3: `08/09` · 4: `06/10`
· 5: `0D/0E/0F` · 6: `0A/0B/0C` · 7: `11..18` · 8: `19`.

## V19.3 ⚠ Two setup defects the sweep exposed in its own first runs

Both made it *wrong*, not merely incomplete, and both are worth keeping written down.

**(1) X and Y are SIXTEEN bits.** The handlers' bare `LDA $DB / TAX / LDA $C0F700,X` looks like it
demands a narrow X — it does not, because `C1/01F9` returns with the accumulator's B half already
cleared (`29 FF 00`, a 16-bit `AND #00FF`, at `C1/0205`). The width is settled by two three-byte
immediates an 8-bit decode turns into garbage: `C0 0B 00` at `C1/2914` is `CPY #000B`, **not**
`CPY #0B` + `BRK`, and `E0 00 02` at `C1/0208` is `CPX #0200`. Run narrow, the CPU walked off into
bank 0 at `$FF1C` on every case that reaches `C1/28B0` — **756 of 810 lost**, which is how the
mistake surfaced.

**(2) `C1/28B0` reads PRE-BUILT box rows.** Only the ACTOR's `$975A`/`$9770`/`$9765`/`$977B` are
rebuilt by the handler's own `JSR C1/285A`; every other body's come from `C1/283D`, which nothing in
a sweep runs. Without seeding them the oracle compared its boot zeros against a port that recomputes
from the mirror, and **all 132** overlapping-body cases disagreed. They are seeded from `C1/285A`'s
own formula now — whose clamps branch on the **sign of the position**, not on the arithmetic result —
which is what lets this sweep grade `battleBoxAt` instead of assuming it.

## V19.4 One input is forced into agreement rather than graded

`C1/28B0`'s presence gate reads `$96F5,Y`; this engine reads `$2980` / the staged record's `+6` bit7,
the stand-in `battle_enemy_move_target.md` §F.4 records because nothing in the port writes `$96F5`.
The sweep seeds all three consistently, so the body test is exercised and comparable. The handler is
what is under test, not the substitution.

## V19.5 Wired, and why

All four are dispatched by `battleMoveTick` now. No fixture arms them, so it is inert for the suite —
but the ROM's own table dispatches them, and a port that HAS a handler and refuses to call it is less
correct than one that does.
