# Hetake: damage too high + "sometimes they never attack"

> User pointer (translated from Portuguese): *"the hetake's damage is too high, and sometimes they go without attacking"*.
> Both defects reproduced and closed on the oracle. **They are two independent bugs**, plus a third
> (a second damage term) that piggybacks on the first symptom.
>
> Every claim below carries the command that produced it. Anything not measured is marked
> **NOT DERIVED** or **disasm-only, unconfirmed**.

---

## 0. The test states actually exercise the feature (the "verify negatives" guard)

`cbdd2e4`'s warning was that the forest capture shows *no enemy acting* because the PCs one-shot the
Hetakes. Both harnesses used here **let the enemy reach its turn and land hits**, proven by HP loss:

* ORACLE — `saves/battle_re/forest_holdleft_pre.state` driven with **hold-LEFT and nothing else**
  (`--input "40:4200"`): PCs never press A, so they never act; the Hetakes attack **11 times** in
  ~3800 battle frames (HP drops listed in §1).
* OURS — `build/Belthasar.exe --scene 19 --battletest saves/battle_re/forest_pre.raw.wram` with
  `CT_BATTLE_SCRIPT="n:1200"` (idle button script): the Hetakes attack 7 times, then go silent.

Neither run is a "nobody got a turn" false negative.

---

## 1. ORACLE GROUND TRUTH — what a Hetake actually does

```
build/ct.exe --monitor "7EAD89:2,7E5E30:2,7E5EB0:2,7EAEEF:1,7EB2C8:1,7E5FB0:2,7E6030:2,7E60B0:2" \
  --from saves/battle_re/forest_holdleft_pre.state --input "40:4200" --frames 4200 --diff
```

```
  f234    7EAD89=0000  7E5E30=0000  7E5EB0=0000  7EAEEF=00*  7EB2C8=00*   <- battle staged
  f235    7EAD89=0000  7E5E30=4600*  7E5EB0=4100*  ...  7E5FB0=0E00* 7E6030=0E00* 7E60B0=0E00*
  f449    7EAD89=0400*  ...  7EAEEF=0C*  7EB2C8=00
  f628    7EAD89=0400   7E5E30=4200*                     <- Crono 70 -> 66   (-4)
  f648    7EAD89=0500*
  f852    7EAD89=0500   7E5E30=3D00*                     <- Crono 66 -> 61   (-5)
  f1132   7EAD89=0400*
  f1322   7EAD89=0400   7E5E30=3900*                     <- 61 -> 57  (-4)
  f1528                 7E5E30=3400*                     <- 57 -> 52  (-5)
  f1727                               7E5EB0=3D00*       <- Marle 65 -> 61 (-4)
  f2211                               7E5EB0=3900*       <- 61 -> 57 (-4)
  f2686                 7E5E30=3000*                     <- 52 -> 48 (-4)
  f2890                               7E5EB0=3400*       <- 57 -> 52 (-5)
  f3090                               7E5EB0=2F00*       <- 52 -> 47 (-5)
  f3561                 7E5E30=2B00*                     <- 48 -> 43 (-5)
  f3766                               7E5EB0=2B00*       <- 47 -> 43 (-4)
```

**Every Hetake hit is 4 or 5. `$AD89` (the computed damage) is only ever `0004`/`0005`.
`$AEEF` (attack POWER) is `0x0C = 12`. `$B2C8` (the attack-record index) is `0x00`.**
Enemy HP 0x0E=14 for all three slots; nothing kills anything, so no state is degenerate.

Supporting stat read (same drive, second monitor call):

```
build/ct.exe --monitor "...,7E5FEA:2,7E5E6B:1,7E5EEB:1,7EB1F4:2,..." --from ... --input "40:1400" --frames 1400 --diff
  f235    7E5FEA=047F*  7E5E6B=10*  7E5EEB=0E*
```
* `$5FEA/$5FEB` = enemy slot-0 block **+0x3D = 0x04** (attack stat), **+0x3E = 0x7F** (defense).
* `$5E6B` = Crono block **+0x3E = 0x10 = 16** (physical defense); `$5EEB` = Marle **+0x3E = 0x0E = 14**.
* `$B1F4` (attacker block offset) cycles `0180 / 0200 / 0280` = enemy slots **0, 1 and 2** — all three
  Hetakes take turns on the oracle.

---

## 2. THE ROM FORMULA (type-4 enemy physical, `C1/E276`)

`python tools/dis.py 1E276 0x70 1 1`:

```
1E276: CMP #04            ; $16 == 4 -> the ENEMY PHYSICAL formula ($AEEB = 04, measured below)
1E27A: JSR C1/E63B        ; wake-on-hit (clears sleep bits, ATB reload) -- not part of the number
1E27E: LDX $B1F4
1E281: LDA $5E6A,X        ; ** ATTACKER block+0x3D = the enemy ATTACK stat **
1E287: LDA #02 ... JSR C1/C90B     ; $0A = atk * 2
1E293: LDA $AEEF          ; ** attack-record POWER **
1E299: LDA #01 ... JSR C1/C90B     ; $2C = POW * 1
1E2A5: LDX $0A ... JSR C1/C90B     ; $2C = POW * (atk*2)
1E2B1: LDX #$0012 / STX $2A / JSR C1/C92A   ; / 18
1E2BA: LDX $B1F4
1E2BD: LDA $5E6A,X        ; ** the VARIANCE RANGE is also block+0x3D **
1E2C0: JSR C1/C02A        ; $30 = the C02A fraction of it
1E2C5: LDA $30 / CLC / ADC #02 / JSR C1/AF22 / STA $B2BE   ; bonus = rand(0 .. $30+1)
1E2D0: JMP C1/E575        ; elemental, then the (256-def)/256 cut at C1/E65F+
```

`C1/C02A` (`python tools/dis.py 1C02A 0x50 1 1`) is a fixed-point multiply:
`A=v*256; Y=A>>2 (v*64); $08=A>>5 (v*8); Y-=$08 (v*56); $08>>=1 (v*4); Y-=$08 (v*52);
$08>>=3 (v>>1); Y-=$08;  $30 = (Y & 0xFF00) >> 8` →

```
C02A(v) = ((v*52) - (v>>1)) >> 8
```

`C1/E65F` (`python tools/dis.py 1E63B 0x60 1 1`) selects the defense: `$18 == 0x3E` → `def = target
block+0x3E` raw; `$18 == 0x3C` → `def * 5/2`. Enemy physical uses `0x3E`.

**Closed arithmetic vs the oracle (Hetake atk 4, POW 12, Crono def 16):**

```
base   = 12 * (4*2) / 18            = 96/18   = 5
dmg    = 5 * (256-16) / 256         = 1200/256 = 4
bonus  = rand(0 .. C02A(4)+1) = rand(0..0+1)  = 0 or 1
dmg    = 4 or 5                     ==  the oracle's $AD89 = 0004 / 0005  (exact, both values seen)
Marle: 5*(256-14)/256 = 4  -> 4 or 5 == the -4 / -5 Marle drops at f1727/f2890/f3090/f3766
```

---

## 3. THE ATTACK-RECORD INDEX — the ROM's real chain (this is DEFECT #1)

Record loader for the enemy class (`python tools/dis.py 1D900 0x60 1 1`):

```
1D916: LDA $B2C7 / CLC / ADC $B2C3 / TAX
1D923: LDA $CC88CE,X / STA $B2C8       ; ** the attack id **
1D931: TAX / LDA #0C / JSR C1/C90B     ; X = id*12
1D945: LDA $CC89C6,X / STA $AEE6,Y (12 bytes)   ; -> $AEE6..$AEF1, so $AEEF = record+9 = POWER
```
and its index setter (`python tools/dis.py 1D8C0 0x40 1 1`):
```
1D8DA: LDA $B18C / LDA #0B / JSR C1/C90B / STX $B2C3    ; $B2C3 = $B18C * 11
1D8ED: STZ $B2C7                                        ; cursor 0
```

**Where `$B18C` comes from — the ATTACK exec handler `C1/99BE`** (`python tools/dis.py 199B0 0x50 1 1`
and `199FE 0x70 1 1`):

```
199BE: TDC / STA $B18C            ; ** default: action id = 0 **
199F5: LDA $CC0000,X / STA $0E / STA $AEE4    ; operand1 = the next script byte  ($AEE4!)
199FE: CMP #01
19A00: BNE ->19A17                ; operand1 != 1  ->  KEEP $B18C = 0
19A02: LDA $AEC8 / CLC / ADC #03 / REP #20 / XBA / LSR / TAX   ; X = (slot+3)*0x80 = the attacker's block
19A11: LDA $5E48,X / STA $B18C    ; ** $B18C = ATTACKER block+0x1B **
19A17: LDX $B1D2 / INX ... LDA $CC0000,X / STA $AEE5           ; operand2 = the targeting byte
```

`$AEC8` is the enemy **slot**, measured 0/1/2 (`--monitor "7EAEC8:1,..."`, rows f642 `7EAEC8=02`,
f647 `=01`, f1131 `=00`), so `(slot+3)*0x80` is that enemy's own block. `$5E48 - $5E2D = 0x1B`.

**`block+0x1B` is a STAGED ROM stat** — `python tools/dis.py 3DB438 0xA0 1 1`, the tail of FD/B438:
```
3DB4C1: TDC / STA $00
3DB4C4: INX / LDA $CC4700,X / STA $5FC6,Y / INY / INC $00 / CMP #03 / BCC ->3DB4C4
```
`$5FC6 = block+0x19`, three bytes ⇒ **block+0x19..+0x1B = enemy-stat record bytes rec[20..21..22]**.
(The main stat copy just above is `CMP #0C` = **12** bytes `rec[8..19] -> $5FE4,Y = block+0x37..+0x42`.)

ROM table values:
```
python -c "rom=open('Chrono_Trigger_USA_SNES.sfc','rb').read(); ..."
Hetake rec: 0E 00 01 00 00 00 00 00 0A 05 02 08 06 32 04 7F 04 04 04 04 00 00 00
   rec[20..22] -> block+0x19,+0x1A,+0x1B = 00 00 00
Gato   rec: 4C 00 01 00 FF 00 00 00 01 08 08 64 06 32 07 7F 04 04 04 04 00 00 01
   rec[20..22] -> block+0x19,+0x1A,+0x1B = 00 00 01
```
Live oracle confirmation (`--monitor "7EAEE3:3,7E5FC8:1,7E6048:1,7E60C8:1,7EB18C:1" ... --input "40:4200"`):
```
  f234 ... 7E5FC8=00  7E6048=00  7E60C8=00  7EB18C=00*
  ... all 34 rows: 7E5FC8=00 7E6048=00 7E60C8=00 7EB18C=00
```
All three Hetakes' `block+0x1B = 0` and `$B18C = 0` for the entire fight ⇒ `$B2C3=0`, `$B2C7=0`,
`$B2C8 = CC/88CE[0] = 0x00` (measured), record `CC/89C6[0]`, **POW = 12** (measured `$AEEF=0C`).

ROM record table, first four entries:
```
atkrec 0: 03 00 00 3A 3B 04 3E 10 00 0C 00 80   POW=12
atkrec 1: 03 00 00 3A 3B 04 3E 10 00 12 00 80   POW=18
actrec k: 00 10 04 04 kk FF FF 00 00 00 FF      (CC/88CA[k*11+4] == k, the identity that hid the bug)
```

### Why the GATO fight never caught this
GATO's AI script (`0xCA77B`) block-1 attack entry is `01 **00** 05 01` (operand1 = 0 → `$B18C` stays
0 → rec 0 POW 12 → the pinned "attack-A dmg 9"); its block-2 counter is `01 **01** 06 00`
(operand1 = 1 → `$B18C` = GATO's `block+0x1B` = **1** → rec 1 POW 18 → the pinned "attack-B dmg 14").
Both GATO values are reproduced by `$AEE4`-as-index purely by coincidence (0→0, 1→1).
The Hetake's entries are `01 **01** 05 00` / `01 **01** 06 0E`: operand1 = 1 but `block+0x1B` = **0**,
so the coincidence breaks and `$AEE4` picks rec **1** (POW 18) where the ROM picks rec **0** (POW 12).

---

## 4. OUR ENGINE — measured, number for number

```
CT_BATTLE_TRACE=1 CT_BATTLE_SCRIPT="n:1200" SDL_VIDEODRIVER=dummy \
  build/Belthasar.exe --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
```
```
f495  act-end  cronoHP=63   (70 -> 63,  -7)
f715  act-end  cronoHP=41   (63 -> 41, -22)
f1204 act-end  cronoHP=33   (41 -> 33,  -8)
f1424 act-end  marleHP=20   (65 -> 20, -45)
f1913 act-end  marleHP=12   (20 -> 12,  -8)
f2418 act-end  cronoHP=26   (33 -> 26,  -7)
f2922 act-end  marleHP=5    (12 ->  5,  -7)
```
Oracle: 4/5 every time. Ours: **7, 22, 8, 45, 8, 7, 7.**

### 4.1 Defect #1 — POW 18 instead of 12 (`src/battle.c:1881-1888`)

```c
unsigned act2 = s[0xAEE4];                                 // $B18C -- the AI-decoded action id
...
unsigned recIdx = rom[actBase + act2 * 11 + 4];            // = $B2C8
int coef = rom[atkBase + recIdx * 12 + 9];                 // +9 POW
base = coef * (s[ab + 0x3D] * 2) / 18;
```
`$AEE4` is **operand1 of the script entry**, not the action id. For the Hetake it is `1`
(trace row: `AE93=01 AEE3=01,01,05`), so `recIdx = CC/88CA[1*11+4] = 1`, `coef = 18`.

```
ours:   base = 18*(4*2)/18 = 8 ;  8*(256-16)/256 = 7   -> 7 or 8      == observed 7,8,8,7,7
oracle: base = 12*(4*2)/18 = 5 ;  5*(256-16)/256 = 4   -> 4 or 5      == measured $AD89
```
**+75 % on every Hetake physical.** Marle: `8*(256-14)/256 = 7` vs the oracle's 4.

### 4.2 Defect #2 — the variance range reads an UNSTAGED byte (`src/battle.c:1891`)

```c
dmg += battleRng(s, rom, s[ab + 0x36] / 3);   // + bonus rand(0..A36/3) (C1/E15E; RNG AF22)
```
`s[ab+0x36]/3` is the **PC** term (`C1/E15E`: `LDA $5E63,X / LDX #0003 / JSR C92A`). The enemy path is
`C1/E2BD`: `LDA $5E6A,X (block+0x3D) / JSR C1/C02A`, then `ADC #02`.

Worse, `block+0x36` is **never staged for an enemy** — FD/B438 writes +0x19..+0x1B, +0x22..+0x26 and
+0x37..+0x42 only. On the oracle the battle-init clear zeroes it:
`--monitor "7E5FE3:1,7E6063:1,7E60E3:1" ... f0 = 03 / CD / 06 -> f232 = 00 / 00 / 00`.
Our engine keeps the **raw seed WRAM**:

```
python -c "d=open('saves/battle_re/forest_pre.raw.wram','rb').read(); ..."
slot 0 block 5FAD +0x36=03  -> /3 = 1
slot 1 block 602D +0x36=CD  -> /3 = 68
slot 2 block 60AD +0x36=06  -> /3 = 2
```
and the RNG audit shows exactly those ranges being drawn:
```
CT_BATTLE_RNGTRACE=1 ... | grep -oE "max=[0-9]+" | sort | uniq -c
      5 max=1        <- slot 0 (0x03/3)
      2 max=68       <- slot 1 (0xCD/3)   ** the 22- and 45-damage hits **
```
`7 + 15 = 22`, `7 + 38 = 45`. Canonical range for atk 4 is `C02A(4)+1 = 0+1` → **rand(0..1)**.

So the same Hetake deals 7 or 45 depending purely on **which enemy slot it occupies** and what the
field seed left in that byte — the exact shape of "the damage is too high".

### 4.3 Also wrong: the staging over-copy (`src/battle.c:578`)

```c
memcpy(s + b + 0x37, rec + 8, 15);   // rec+8.. -> block+0x37 stats
```
FD/B438 copies **12** (`CMP #0C`) to +0x37, and `rec[20..22]` go to **+0x19..+0x1B** (`CMP #03`,
`STA $5FC6,Y`). Ours writes rec[20..22] into +0x43/+0x44/+0x45 (PC-derived-stat slots) and leaves
**+0x1B unstaged**, i.e. the byte the `$B18C` law reads does not exist in our blocks.

---

## 5. DEFECT #3 — "sometimes they never attack" (the permanent AI lock)

### 5.1 The Hetake AI script (ROM)
```
python -c "... ptr=CC/8B08+0x5E*2 -> 9E36, base 0xC9E36"
17 10 00 00 FE | 00 00 06 04 FE | 00 00 00 00 FE | 01 01 05 00  01 01 06 0E FE | FF ...
^ sec0 cond      ^ sec0 action    ^ sec1 cond      ^ sec1 actions                 ^ block1 end
  (cond 17 = random, prob 0x10 = 16 %)   (op 00 = MOVE-only)      (op 01 = ATTACK x2)
```
`FD/BA4A[0x00] = 4`, `FD/BA4A[0x01] = 4`.

### 5.2 What the ROM does with a move-only section — the saved-IP walk
Dispatch, `python tools/dis.py 18D43 0x80 1 1`:
```
18D77: STX $B1D2 / LDA $CC0000,X / STA $AEE3     ; the op at the per-enemy saved IP $B1D4,Y
18D86: ASL A / TAX / JSR ($B88D,X)               ; run the op's handler
18D93: LDA $B3B8
18D96: CMP #00 / BEQ ->18DA4                     ; 0 (attack, set at 19A33) -> OPEN THE ACTION
18D9A: CMP #01 / BNE ->18DA1
18DA1: JMP C1/8E4A                               ; 2 (op-00 move, set at 199B8) -> the ADVANCE path
```
`C1/8E4A` (`python tools/dis.py 18E40 0x60 1 1`):
```
18E5D: LDX $B1D4,Y / STX $B273
18E63: LDA $CC0000,X / TAX / LDA $FDBA4A,X       ; the op's own byte length
18E6E: CLC / ADC $B273 / STA $B1D4,Y             ; ** saved IP += len -- the move is CONSUMED **
```
Measured live (`--monitor "7EB1D4:6,7EB1E4:6,7EAEE3:1,7EB18C:1" --from forest_holdleft_pre.state
--input "40:2300" --frames 2300 --diff`; the three u16s are slots 0/1/2, script base 0x9E36):
```
  f324   B1D4=459E 459E 3B9E   AEE3=00     slot2 IP = 9E3B = sec0's MOVE entry, op 00 executed
  f642   B1D4=499E 459E 3F9E   AEE3=00     slot2 IP advanced 9E3B -> 9E3F  (= the $FE terminator)
  f643   B1D4=499E 459E 459E   AEE3=01     slot2 IP 9E3F -> 9E45 = SEC-1's ATTACK  ** re-evaluated **
  f1322  B1D4=3B9E 499E 459E   AEE3=00     slot0 re-evaluated the OTHER way: 9E49 -> 9E3B (sec0 move)
  f2006  B1D4=3F9E ...         AEE3=00     slot0 move consumed, IP -> the $FE
  f2007  B1D4=459E ...         AEE3=01     slot0 back on sec-1's attack
```
**The move section is a ONE-TURN detour.** The IP advances past the entry; landing on the section's
`$FE` re-runs the condition walk and re-chooses a section (16 % move / 84 % attack). Over 4200 frames
the oracle executes **7 op-00 turns interleaved with ~17 op-01 attacks** and never stalls.

### 5.3 Why ours locks forever (`src/battle.c:946-976, 978-1030, 3936-3946`)

1. `aiIsFiller()` (`src/battle.c:834`) classifies **op 0x00 as filler**.
2. `battleAiExecSection()` therefore walks section 0's only entry, skips it as filler, wraps
   (`if (q >= actionEnd) { q = actionStart; }`), exhausts the 16-step guard and **returns 0**.
   It does **not** advance a saved IP, does **not** re-walk conditions, and does **not** touch
   `aiArmBase/aiArmBlk/aiArmSec` (those are only written on a *substantive* op).
3. `battleAiRunTurn()` takes the replay path on every non-hit ATB turn
   (`blockIdx == h->aiArmBlk[slot] && h->aiArmBase[slot] > 0`) → back into (2) → 0.
4. `battleOpenAction()` sees `op == 0` → `s[0xAFAB+atk] = s[0xB158+atk]; ... return;` — **no action
   window, therefore no span end, therefore `battleSpanEndArm()` (the only re-eval site, called at
   `src/battle.c:4454` from the span-end path) never runs.**

⇒ The moment `battleAiEval` arms **section 0** (16 % per evaluation: at combat start, or at any
own-action span end), that enemy is silenced **for the rest of the battle**. Being hit does not
unlock it: block 2 is also a lone op-00 (`00 00 00 00 FE 00 00 06 00 FE FF`), so the block-2 walk
also returns 0 without re-arming, and the next non-hit turn replays block-0/section-0 again.

Observed in ours (`CT_BATTLE_TRACE=1 CT_BATTLE_SCRIPT="n:9000"`):
```
f285  enemy-noop id5E#2        <- slot 2 armed sec-0 at BOOT; it never attacks once, all run
f1928 enemy-noop id5E#1        <- slot 1 locks after 2 attacks
f2937 enemy-noop id5E#1
f3217/f3222/f3237  noop #2/#0/#1   <- ALL THREE locked
f3517/f3522/f3537  noop #2/#0/#1
f3817/f3822/f3837  noop #2/#0/#1
f4000 end-debug   (harness cap)    -- 1100 unbroken frames, zero attacks
```
Slot 0 landed 5 attacks before locking, slot 1 landed 2, slot 2 landed 0 — consistent with a 16 %
per-eval lock probability (expected ≈ 6 actions before the roll lands).
Over the same window the oracle lands **11** hits and never goes quiet.

---

## 6. FIX SPEC (mandate-safe; not code)

All four are in `src/battle.c`. None of them is scene- or enemy-gated.

**F1 — stage the bytes FD/B438 actually stages** (`src/battle.c:578`, the `(iv)` enemy-block loop):
`memcpy(b+0x37, rec+8, 15)` → `memcpy(b+0x37, rec+8, 12)` (FD/B4AF-B4BE `CMP #0C`) **plus**
`memcpy(b+0x19, rec+20, 3)` (FD/B4C4-B4D3 `CMP #03`, `STA $5FC6,Y`). Also zero the enemy blocks'
`+0x36` (or stop reading it — see F3); the ROM's battle-init clear leaves it 0 (`7E5FE3` 03→00 at f232).

**F2 — resolve the enemy attack record through `$B18C`, not `$AEE4`**
(`src/battle.c:1881-1886`, the `else` branch of `battleComputeDamage`). The ROM law (`C1/99BE`
`199BE`/`199FE`/`19A11`): the enemy action id is **0**, and becomes the **attacker's block+0x1B**
only when the executed attack entry's **operand1 == 1**. In our terms, with `ab = battleBlock(atk)`:
`act = (s[0xAEE4] == 1) ? s[ab + 0x1B] : 0;` and then the existing
`recIdx = rom[actBase + act*11 + 4]; coef = rom[atkBase + recIdx*12 + 9];` chain is correct as-is.
This keeps GATO byte-identical (block-1 operand1 = 0 → rec 0 POW 12; block-2 operand1 = 1,
GATO `block+0x1B` = 1 → rec 1 POW 18) and turns the Hetake's POW 18 back into **12**.

**F3 — the enemy variance range is `C1/C02A` of block+0x3D, not block+0x36/3**
(`src/battle.c:1891`). Split the bonus by path: PC keeps `battleRng(atk36/3)` (`C1/E15E`); the enemy
uses `battleRng( ((s[ab+0x3D]*52 - (s[ab+0x3D]>>1)) >> 8) + 1 )` — i.e. `C02A(v)` then `ADC #02`
(`C1/E2BD-E2CA`). For the Hetake that is `rand(0..1)`; for GATO (atk 7) `rand(0..2)`.

**F4 — op-00 must be an EXECUTED op that advances the IP, and the section terminator must
re-evaluate** (`src/battle.c:834 aiIsFiller`, `946 battleAiExecSection`, `978 battleAiRunTurn`,
`3936 battleOpenAction`). Canonical model, measured in §5.2: a per-enemy **saved IP `$B1D4,Y`**
(committed mirror `$B1E4,Y`) walks the chosen section's entries; the executed op's handler sets
`$B3B8` (attack → 0 = open the action window `C1/8DA4`; op-00 move → 2 = `C1/8E4A`), and `C1/8E4A`
does `$B1D4,Y += FD/BA4A[op]`. When the IP reaches the section's `$FE`, the condition walk runs
again and re-chooses a section (oracle f642→f643: `9E3F → 9E45`; f1322: `9E49 → 9E3B`).
Replacing our per-section wrapping cursor + "op-00 is filler" with that linear IP model removes the
lock structurally, for every enemy script, without any Hetake/forest special case.

⚠ **Re-pinning warning:** F2 and F3 both move numbers in `field-battleloop-gato`
(`cronoHP=44 marleHP=39`) and `field-battleloop-forest` (`victory f1176 ... cronoHP=70 marleHP=65`)
and every RNG-stream-position check downstream. Those must be re-arbitrated **against the oracle**,
not blessed — `cbdd2e4`'s own note about the four snapshot checks whose *AT frames* still need
re-deriving applies here too.

---

## 7. NOT DERIVED / open

* **The `$AEE5` byte sequence.** `--monitor 7EAEE3:3` shows the third byte taking `05/0E/06/00/04`,
  i.e. more values than the two targeting bytes (`05`, `06`) in the script. Whatever extra write
  produces `0E`/`00`/`04` is **NOT DERIVED**; it does not affect the damage number.
* **The `$B3B8 == 1` branch** (`C1/8D9A → C1/8E47`) is disasm-only, unexercised in these two fights.
* **Whether the re-evaluation at the `$FE` terminator re-walks conditions from block-1 section 0
  every time, or resumes at a stored section**, is inferred from two transitions
  (`9E3F→9E45` and `9E49→9E3B`) plus the 16 % cond-17. It is **consistent with, but not proven to
  be, a full top-down re-walk** — a script with three block-1 sections would settle it. Marked
  *partially derived*; F4's IP mechanics (advance-by-`FD/BA4A[op]`) ARE proven.
* **The oracle's exact enemy TURN count** (as opposed to executed ops) was not counted, so the
  op-00 : op-01 ratio (7 : ~17 over 4200 frames) is an execution ratio, not a per-turn probability.
* **GATO's post-fix damage numbers were not re-measured** on `gato_press_action.state`; the identity
  argument in §3 is a table/script reading, not a fresh oracle run. Do that before re-pinning.
* `--battletest`'s harness ends the battle at f4000 regardless of `CT_BATTLE_SCRIPT="n:9000"`; the
  lock was therefore observed over 1100 frames, not indefinitely. Cause of the cap: **NOT DERIVED**.
