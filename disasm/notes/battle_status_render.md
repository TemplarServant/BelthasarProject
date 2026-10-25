# BATTLE STATUS **RENDER** — the slept-PC POSE + the status ICON OBJ (closes `battle_status_pipeline.md` §9 "OPEN (render)")

Closes the one open item left by `battle_status_pipeline.md` (line ~405):

> **OPEN (render)**: the slept-PC POSE + the floating STAR OBJ (§8.6) are captured but not rendered — the
> status→pose/OBJ selector in the anim engine was not traced (§7 O4).

**It is now traced, byte-for-byte.** Both halves come from **one** general, table-driven ROM machine — the
per-combatant *status scan* `C1/2F97`, whose single pass emits **two** outputs:

| output | ROM var | consumer | what it selects |
|---|---|---|---|
| **pose class** | `$A440` | PC pose tick `C1/2E00` (`12E6B`) | `ptrA/ptrB += $A440 × 4` → a **different pose-script window** |
| **icon state** | `$A441,X` | icon dispatch `C1/308C` → `($3216,X)` | arms the **status-icon OBJ** (`$A48E/$A4D0/$A4C5`) |

Both are read out of **one 4-byte ROM record table `CC/F77F`** keyed by (block byte, bit mask). There is **no
sleep-specific code path anywhere** — sleep is row 2 of that table.

> ⚠ **CORRECTION to `battle_status_pipeline.md` §8.6.** The icon does **NOT** float/orbit. Its OAM (x,y) is
> **constant** at `origin + (−8, −48)`; the animation is a **2-frame TILE swap** `0x1A6 ↔ 0x1C6` at a
> **10-render-frame** dwell. §8.6's "floating STAR" was an eyeball reading of one `--peek`; the measured
> per-frame OAM (§4) shows zero positional motion. (Other statuses' icons *do* hop — see the `CC/F6D4`
> table in §3.2, groups 8-11 and 16-19 — so "some status icons move" is true, "sleep's does" is not.)

Method / provenance: ROM disasm via `python tools/dis.py <fileoffset> <len> [m] [x]`
(fileoffset = `(bank−0xC0)<<16 | addr`); oracle = `ct_probe.exe` (= `build/ct.exe`) run from the
**`sleeppatch/`** ROM copy built by `mk_sleeppatch.py` (the `battle_status_pipeline.md` §8.3 recipe — GATO's
attack headers `CC/89C6[0]`/`[1]` and Crono's `CC/213F[58]` carry the sleep rider: byte+1 = 0x63 accuracy,
byte+2 = 0x04 mask, byte+7 |= 0x04). Base state `crono_normal_ready.state`; driver
`--input "100:10,0:20,100:10,0:N"`. **Writer PCs = the PC after the store retires.**

---

## 0 — HEADLINE RESULTS

1. **The status byte + bit (re-confirmed).** Combatant `cmb`'s status byte 1 is `$5E4A + cmb×0x80 + 0x01`
   ( = block `$5E2D + cmb×0x80`, offset **+0x1E**); **SLEEP = bit 2 (mask 0x04)**. The scan's own per-combatant
   base table `CC/F8ED[cmb]` is literally `cmb × 0x80` added to `$5E4A` (`12FB1`, dumped §2.2) — i.e. the ROM's
   own confirmation of the block stride the pipeline dossier derived.

2. **status → POSE (`$A440`).** `C1/2F97` walks the 4-byte records at **`CC/F77F`**
   `{+0 blockByteOffset (rel. $5E4A; bit7 = END), +1 mask, +2 iconState, +3 POSE CLASS}` and on the FIRST
   match sets `$84 = rec+3`, which lands in **`$A440`** (`13058`). The PC pose tick `C1/2E00` then does
   **`ptrA += $A440×4` and `ptrB += $A440×4`** (`12E6B-12E82`). Normal = class **3** → **+0x0C** (exactly the
   constant `src/battle.c:CtBattlePcIdleWin` already hardcodes). **Sleep = class 0x1A → +0x68.**

3. **The sleep POSE windows are real ROM data.** `ptrA(Crono) = E4/3000`, stride 0x128;
   `window = ptrA + facing×stride + 0x68` = **`{0xCC, 0xCD, 0xFF}`** (facing 0/1), `{0xCE,0xCF,0xFF}` (facing 2),
   `{0xD0,0xD1,0xFF}` (facing 3). Marle (`ptrA = E4/34A0`, stride 0xF8): `{0x54,0x55}` / `{0x56,0x57}` /
   `{0x58,0x59}`. The E3 assembly records for those indices reproduce the observed OAM **exactly** (§5.3).

4. **status → ICON.** The same record's `+2` byte goes to `$A441,X`; `C1/308C` (run for all 11 combatants every
   frame) dispatches `JSR ($3216,X)` when `$A441` ≠ the latch `$A44C`. **Sleep = state 4 → `C1/31B0`** which arms
   `$A48E,X = 1` (visible), **`$A4D0,X = 0x30`** (the icon's record-group byte offset), `$A4C5,X = CC/F5FF = 0x0A`
   (period **10**), `$A4BA,X = 1` (timer), `$A4DB,X = 0xFF` (cursor → wraps to 0 on the next build).

5. **The icon OBJ builder = `CF/EE2C`.** Per combatant: `OAM.x = CC/F6D4[$A4D0+cursor×4 +0] + $1D0C[cmb]`,
   `OAM.y = […+1] + $1D23[cmb]`, `OAM.tile = […+2]`, `OAM.attr = […+3] | $A5E3`. Cursor is a **4-step** cycle
   advanced every `$A4C5` builder frames. **Sleep group (records 12-15) = `(−8,−48,0xA6,0x25)`,
   `(−8,−48,0xC6,0x25)`, ×2** → a fixed position and a 2-frame tile blink at 10 frames each.

6. **Icon GATE = the frozen action span.** The builder is only reached when **`$A013 == 0 && $A0FE == 0`**
   (`CF/EDC0 → JMP CF/EE2C`). Oracle-correlated 4/4 windows (§4.3). `$A013` is the port's `battleActionFreeze`.

7. **Icon pixels + palette are already in the tree.** The icon sheet is **uncompressed in ROM at flat
   `0x113900` = `$D1/3900`**, uploaded verbatim to **VRAM tiles 0x1A0-0x1DF** at battle boot and *resident*
   (byte-identical at f100 and f700, and identical in the forest fight). It uses **only palette indices
   0/13/14/15**, and OBJ pal2 idx 13/14/15 (`7FFF` white, `02DF` gold, `0CC9` dark) are **identical in the GATO
   and forest fights** — and already baked as `ctBattleAnimPal[2]` in `src/data/battle_anim.h`. **No new fixture.**

8. **Pixel-verified end to end.** The sleep icon reconstructed from `CC/F6D4` + `$1D0C/$1D23` + VRAM 0x1A6 +
   pal2 matches the oracle framebuffer **62/62 opaque px, 0 mismatch**; the two slept PCs' pose cells
   **743/748** (the 5 are the icon-over-body overlap, which proves the icon draws ON TOP — §6).

9. **PCs only.** `$A440` is read ONLY inside `C1/2E00`-`C1/2EF4` (ROM-wide search for `AD 40 A4` in bank C1:
   `C1/2E31`, `C1/2E6B`, `C1/2EF4`; written at `C1/2E42` and `C1/3058`). The **enemy** pose tick `C1/3360` takes
   its class from `$5E0D,X → $974A,X → CC/FB91[mode]` and never touches `$A440` — so a slept **enemy** gets the
   **icon** (the `C1/308C` loop covers all 11 combatants) but **no** status pose. (Unobservable in this fight:
   GATO's `+0x23 = 0xFF` immunity blocks the inflict — `battle_status_pipeline.md` §8.4.)

---

## 1 — THE STATUS SCAN `C1/2F97` (the single source of both outputs)

`python tools/dis.py 0x12F97 0x70 1 1` + `0x13006 0x5C 1 1` (8-bit m/x):

```
12F97 LDX $94                          ; X = combatant index (0..10)
12F99 LDA $A09B,X / BEQ 12FA2
12F9E   LDA #00 / BRA 12FA4            ;   $A09B[cmb] != 0 -> default class 0
12FA2   LDA #03                        ;   else            -> default class 3   (** the normal PC class **)
12FA4 STA $84
12FA6 LDA #FF / STA $A441,X            ; default ICON STATE = none
12FAB LDA $94 / ASL / TAX / REP #20
12FB1 LDA $CCF8ED,X / TAY / STY $A2    ; $A2 = per-combatant BLOCK OFFSET word (= cmb * 0x80, §2.2)
12FB6 TDC / STA $80 / SEP #20
12FBD LDX $94
12FBF LDA $A119,X / BNE 12FC9
12FC4   LDA $5E4A,Y / BPL 12FD3        ; block+0x1D bit7 (DEAD)?
12FC9   LDA #01 / STA $A0A8,X ; LDA #FF / STA $968C,X   ; dead -> reset the PC pose CURSOR
12FD3 TDC / TAX                        ; X = 0 = the record cursor into CC/F77F
12FD5 LDA $CCF77F,X / BMI 1302C        ; rec+0 = block byte OFFSET (rel. $5E4A); bit7 -> END of table
12FDB REP #21 / ADC $A2 / TAY          ; Y = blockOffset + recOffset
12FE0 TDC / SEP #20
12FE3 LDA $5E4A,Y                      ; ** the status byte **
12FE6 AND $CCF780,X                    ; ** & rec+1 = the MASK **
12FEA BNE 12FFE                        ; set -> MATCH
12FEC   TXA / BNE 13026                ;   no match, not the first record -> next
12FEF   LDX $94 / LDA $A119,X / BEQ 12FFA / TDC / TAX / BRA 12FFE   ; $A119 re-entry (record 0)
12FFA   TDC / TAX / BRA 13026
12FFE LDA $CCF782,X                    ; ** rec+3 = the POSE CLASS **
13002 CMP #FF / BNE 1300D
13006   LDA $A0A7 / BNE 13026          ;   class 0xFF: gated on $A0A7 (0 -> class 3, else keep scanning)
1300B   LDA #03
1300D STA $84                          ; ** $84 = pose class **
1300F LDY $94
13011 LDA $CCF781,X                    ; ** rec+2 = the ICON STATE **
13015 BMI 13024                        ;   bit7 set -> NO icon (leave $A441 = 0xFF)
13017 STA $A441,Y                      ; ** $A441[cmb] = icon state **
1301A CMP $A44C,Y / BEQ 13024
1301F   LDA #FF / STA $968C,Y          ; ** state CHANGED -> reset the PC pose CURSOR to 0xFF **
13024 BRA 13056
13026 INX / INX / INX / INX / BRA 12FD5 ; ** record stride = 4 **
1302C (table exhausted) LDY $A2 / LDA $5E2F,Y / AND #01 / BEQ 13056   ; block+0x02 bit0 ->
        ... $A441 = 0x80 (no icon) ; $84 = 0x12                        ;   class 0x12
13056 LDA $84
13058 STA $A440                        ; ** the POSE CLASS scalar **
1305B RTS
```

**Callers of `C1/2F97`** (ROM search for `20 97 2F` in bank C1 — exactly two):
`C1/2E28` (inside the **PC pose tick** `C1/2E00`) and `C1/3067` (the per-frame round-robin at `C1/305C`).

---

## 2 — THE TABLES

### 2.1 `CC/F77F` — status bit → {icon state, pose class} (4-byte records, `X += 4`, `+0` bit7 = END)

Dump (`0x0CF77F`; `blk` = the byte offset relative to `$5E4A` = block **+0x1D**):

| rec | X | blk | block byte | mask | **icon state** (`+2`) | **pose class** (`+3`) | pose window off = class×4 |
|---|---|---|---|---|---|---|---|
| 0 | 0 | 00 | **+0x1D** | 80 | `81` → bit7 = **no icon** | `08` | 0x20 (the DEAD pose) |
| 1 | 4 | 01 | **+0x1E** | 80 | `02` | `00` | 0x00 |
| **2** | **8** | **01** | **+0x1E** | **04** | **`04` = SLEEP** | **`1A`** | **0x68** |
| 3 | 12 | 01 | +0x1E | 02 | `05` | `08` | 0x20 |
| 4 | 16 | 01 | +0x1E | 08 | `08` | `FF` (→3 iff `$A0A7`==0) | 0x0C |
| 5 | 20 | 01 | +0x1E | 20 | `0D` | `03` | 0x0C |
| 6 | 24 | 01 | +0x1E | 01 | `09` | `03` | 0x0C |
| 7 | 28 | 01 | +0x1E | 40 | `0E` | `03` | 0x0C |
| 8 | 32 | 04 | +0x21 | 80 | `03` | `03` | 0x0C |
| 9 | 36 | 09 | +0x26 | 80 | `03` | `03` | 0x0C |
| 10 | 40 | 04 | +0x21 | 40 | `06` | `03` | 0x0C |
| 11 | 44 | 09 | +0x26 | 40 | `06` | `03` | 0x0C |
| 12 | 48 | 04 | +0x21 | 04 | `07` | `23` | 0x8C |
| 13 | 52 | 09 | +0x26 | 04 | `07` | `23` | 0x8C |
| 14 | 56 | 03 | +0x20 | 80 | `0C` | `03` | 0x0C |
| 15 | 60 | 08 | +0x25 | 80 | `0C` | `03` | 0x0C |
| 16 | 64 | **FF** | — | — | — | — | **END** |

Raw: `00 80 81 08 | 01 80 02 00 | 01 04 04 1A | 01 02 05 08 | 01 08 08 FF | 01 20 0D 03 | 01 01 09 03 |
01 40 0E 03 | 04 80 03 03 | 09 80 03 03 | 04 40 06 03 | 09 40 06 03 | 04 04 07 23 | 09 04 07 23 |
03 80 0C 03 | 08 80 0C 03 | FF 00 00 C0`

**Row order = priority** — the first match wins and the scan exits (`13024 BRA 13056`).

### 2.2 `CC/F8ED` — per-combatant block offset (11 words, `0x0CF8ED`)

`0000 0080 0100 0180 0200 0280 0300 0380 0400 0480 0500` = **`cmb × 0x80`**, added to the `$5E4A` base
(block **+0x1D**). Confirms the 0x80 block stride + the 11-combatant array (3 PC seats + 8 enemy slots).

---

## 3 — THE ICON SUBSYSTEM

### 3.1 Dispatch `C1/308C` (run for **all 11** combatants every frame, from `C1/307C`)

```
1307C TDC / TAX / STX $94 / 13080 JSR C1/308C / INC $94 / CMP #0B / BNE 13080   ; 11 combatants
1308C LDX $94
1308E LDA $96F5,X / BEQ 130B5          ; combatant present? ($96F5 = 01 01 00 00 00 00 01 .. in this fight)
13093 LDA $A441,X / CMP $A44C,X / BEQ 130B5   ; unchanged -> nothing to do
1309B STA $A44C,X                      ; latch
1309E STZ $A48E,X / STZ $A499,X / STZ $A0A8,X / STZ $A46D,X
130AA LDA $A441,X / BPL 130B0 / TDC    ; 0xFF/0x8x (negative) -> state 0
130B0 ASL A / TAX / JSR ($3216,X)      ; ** the icon-state jump table **
```

**Jump table `C1/3216`** (dumped from `0x013216`) and what each handler arms:

| state | handler | `$A4D0` (group byte off) | period `$A4C5` | icon records (§3.2) |
|---|---|---|---|---|
| 0 | `C1/30B6` | — | — | none (clear only) |
| 1, 2, 10 | `C1/3191` | 0x10 | `CC/F5FD` = **0x0E** | 4-7 (tiles AE/CE) |
| 3 | `C1/30E1` | 0x20 | `CC/F5FE` = **0x07** | 8-11 (tiles A0/C0) + a `$2300` HUD-buffer write |
| **4** | **`C1/31B0`** | **0x30** | **`CC/F5FF` = 0x0A** | **12-15 (tiles A6/C6) ← SLEEP** |
| 5 | `C1/31D2` | 0x40 | `CC/F600` = 0x10 | 16-19 (tiles A4/C4) |
| 6, 7, 11, 12, 13 | `C1/3185` | — | — | none (`INC $A46D,X` only) |
| 8 | `C1/31F4` | 0x50 | `CC/F601` = 0x0E | 20-23 (tiles A8/C8, pal3) |
| 9 | `C1/3163` | 0x60 | `CC/F602` = 0x0A | 24-27 (tiles AC/CC, pal3) |
| 14 | `C1/3141` | 0x70 | `CC/F603` = 0x0E | 28-31 (tiles AA/CA, pal3) |

The SLEEP handler verbatim (`dis.py 0x131B0 0x24 1 1`):
```
131B0 JSR C1/30B6            ; clear $A483/$A48E/$A457/$A46D/$A478
131B3 LDX $94
131B5 INC $A48E,X            ; ** icon VISIBLE **
131B8 STZ $A499,X
131BB LDA #FF / STA $A4DB,X  ; ** cursor = 0xFF -> the next build wraps it to 0 **
131C0 LDA #30 / STA $A4D0,X  ; ** group byte offset 0x30 = CC/F6D4 records 12..15 **
131C5 LDA #01 / STA $A4BA,X  ; ** timer = 1 -> advance on the very next build **
131CA LDA $CCF5FF / STA $A4C5,X   ; ** period = 10 **
131D1 RTS
```

### 3.2 `CC/F6D4` — the icon OAM record table (32 × 4 bytes, `0x0CF6D4`)

`record = {int8 dx, int8 dy, uint8 tile, uint8 attr}`; a group is **4 consecutive records** (the cursor cycle).

```
grp base  recs        records (dx, dy, tile, attr)                                         used by state
0x00      0..3    (-24,-40,A2,25) ( +8,-40,C2,25) (-24,-40,A2,25) ( +8,-40,C2,25)          (unreferenced here)
0x10      4..7    ( -8,-48,AE,25) ( -8,-48,CE,25) ( -8,-48,AE,25) ( -8,-48,CE,25)          1, 2, 10
0x20      8..11   (-24,-40,A0,25) ( +8,-40,C0,25) (-24,-40,A0,25) ( +8,-40,C0,25)          3     <- HOPS ±16px
0x30     12..15   ( -8,-48,A6,25) ( -8,-48,C6,25) ( -8,-48,A6,25) ( -8,-48,C6,25)          4  ** SLEEP **
0x40     16..19   (-16,-16,A4,25) (-20,-20,C4,25) (-16,-16,A4,25) (-20,-20,C4,25)          5     <- wobbles
0x50     20..23   ( -8,-48,A8,27) ( -8,-48,C8,27) ( -8,-48,A8,27) ( -8,-48,C8,27)          8
0x60     24..27   ( -8,-32,AC,27) ( -8,-32,CC,27) ( -8,-32,AC,27) ( -8,-32,CC,27)          9
0x70     28..31   ( -8,-48,AA,27) ( -8,-48,CA,27) ( -8,-48,AA,27) ( -8,-48,CA,27)         14
```

**attr `0x25`** → tile bit8 = 1, **pal 2**, prio 2, hf=vf=0. **attr `0x27`** → pal **3**.
So the SLEEP icon's real VRAM tiles are **`0x1A6` and `0x1C6`** (16×16 = tiles T, T+1, T+0x10, T+0x11).

### 3.3 The OAM builder `CF/EE2C` (`dis.py 0xFEE2C 0x14 1 0` + `0xFEE30 0x70 1 1`)

```
FEDC0 LDA $A013 / BNE FEDCD / LDA $A0FE / BNE FEDCD / JMP CF/EE2C   ; ** the GATE (§4.3) **
FEE2C LDY #0000 / PHY / PLD             ; DP = 0
FEE31 SEC / LDA $1DC5 / SBC #0C / TAX   ; X = combatant index ($1DC5 = cmb + 0x0C)
FEE38 LDA $A48E,X / BEQ ->FEE99         ; icon inactive -> exit
FEE3D LDA $1D0C,X / STA $CC             ; ** originX = the combatant's LIVE compose origin **
FEE42 LDA $1D23,X / STA $CD             ; ** originY **
FEE47 DEC $A4BA,X / BNE FEE60           ; per-combatant frame TIMER
FEE4C   LDA $A4C5,X / STA $A4BA,X       ;   reload = the period
FEE52   INC $A4DB,X / LDA $A4DB,X / CMP #04 / BNE FEE60
FEE5C   TDC / STA $A4DB,X               ;   ** cursor wraps at 4 **
FEE60 LDA $A4DB,X / ASL / ASL / CLC / ADC $A4D0,X / TAX    ; X = groupBase + cursor*4
FEE6A LDY $1DC9                          ; the OAM write cursor
FEE6E LDA $CCF6D4,X / ADC $CC / STA $0700,Y   ; ** OAM.x = dx + originX **
FEE78 LDA $CCF6D5,X / ADC $CD / STA $0701,Y   ; ** OAM.y = dy + originY **
FEE81 LDA $CCF6D6,X /            STA $0702,Y  ; ** OAM.tile **
FEE88 LDA $CCF6D7,X / ORA $A5E3 / STA $0703,Y ; ** OAM.attr | $A5E3 **   ($A5E3 = 0x30, observed)
FEE92 INY×4 / STY $1DC9
FEE99 LDY #0000 / PHY / PLD / RTS
```

### 3.4 The re-arm: `CC/F102` invalidates the latch at every action-span start

```
CF102 TDC / TAX / CF104 LDA #55 / CF106 STA $A44C,X / INX / CPX #000B / BNE CF106 / RTL
```
i.e. `$A44C[0..10] = 0x55` for all 11 combatants → every icon's state mismatches its latch → `C1/308C`
re-runs its handler → `$A4BA=1, $A4DB=0xFF` → the icon **restarts at cursor 0 (tile 0xA6)**.
Oracle (`--monitor "A441:11,A44C:11" --diff --writer`): writer **`CC:F109`** fires at **f45, f174, f510, f727**
— exactly one frame before `$A013` goes 0→1 (**f46, f175, f511, f728**). That is the observed phase reset in §4.2.

---

## 4 — ORACLE LEDGERS (all from `sleeppatch/`, `--from crono_normal_ready.state --input "100:10,0:20,100:10,0:N"`)

### 4.1 The setup, per-frame with writers (`--monitor "A48E:12,A4D0:12,A4C5:12,A4DB:12" --diff --writer`)

```
f0     A48E=00…  A4D0=00…  A4C5=00…                                   (nothing armed)
f394   A48E=01…(C1:30BE)  A4D0=30…(C1:31C5)  A4C5=0A…(C1:31D1)  A4DB=FF…(C1:31C0)   ; Crono ARMED
f398   A4D0=30  A4DB=00 (CF:EE55)   ; the builder's first tick: cursor 0xFF -> 0
f408 / f418 / f428 …  A4DB = 01 / 02 / 03 / 00 …  every 10 frames (CF:EE55 / CF:EE60)
f665   A48E=0101(C1:31B8) …  A4DB=FFFF(C1:31C0)   ; Marle ARMED (+ Crono RE-armed by the CC/F102 latch clobber)
```
`--monitor "A441:11,A44C:11,96F5:11" --diff --writer`:
```
f0     A441=FF×11                                     ; no status
f394   A441=04 FF…  (writer C1:2FAB)                  ; ** Crono icon state 4 = SLEEP **
f665   A441=0404 FF…                                  ; ** Marle too **
f45/f174/f510/f727  A44C = 55×11 (writer CC:F109)     ; the latch clobber (§3.4)
       96F5 = 01 01 00 00 00 00 01 00 00 00 00        ; combatants present: seats 0,1 + enemy cmb 6 (GATO)
```

### 4.2 The rendered OAM, per frame (`--monitor "0700:512,0900:32,5E4B:1,5ECB:1,95D5:1"`, 1040 frames)

Run-length of every OAM sprite carrying tile 0x1A6/0x1C6 (decoded from the `$7E0700` shadow):
```
f398   #4(126,96) t1A6 pal2 pr3      f408 t1C6      f418 t1A6      f428 t1C6  …  f508 t1C6
f512   (none)                                                    ; <- the frozen span (§4.3)
f669   #4(126,96) t1A6 | #21(171,63) t1A6            f679 both t1C6   f689 both t1A6  …
f729   (none)
f881   #4(126,96) t1A6 | #21(171,63) t1A6            … alternating every 10 f through f1027
f1028  (none)
```
**Every dwell is exactly 10 render frames** (12 consecutive steps in the first window, 15 in the third).
**Position is byte-constant** — Crono `(126, 96)`, Marle `(171, 63)` — for all 260 visible frames.
The tiles are `0x1A6` and `0x1C6` only; `hf=vf=0`, `pal=2`, `prio=3` throughout.

**Position check against §3.3.** `$1D0C = 86 B3 8F …`, `$1D23 = 90 6F A0 …` (from `pk700.raw.wram`):
* Crono cmb 0: `(0x86, 0x90) = (134,144)`; `(134−8, 144−48) = (126, 96)` ✔
* Marle cmb 1: `(0xB3, 0x6F) = (179,111)`; `(179−8, 111−48) = (171, 63)` ✔
`(134,144)` is exactly Crono's staged foot `16·rel+fine` from `$29B0` (`battle_stance.md` ADDENDUM 8) — i.e.
**`$1D0C/$1D23` == the port's live foot**.

The live control bytes at that frame (`pk700.raw.wram`):
`A48E = 01 01 00…`, `A4BA = 0A 0A`, `A4C5 = 0A 0A`, `A4D0 = 30 30`, `A4DB = 00 00`, `A5E3 = 30`,
`5E4B = 04`, `5ECB = 04`, `614B = 00` (GATO immune), `A440 = 1A`, `A441 = 04 04`, `968C = 01 01`.

### 4.3 The GATE `$A013` (`--monitor "A013:1,A0FE:1,A48E:2" --diff`)

```
$A013 : 0 at f397-510   1 at f511-667   0 at f668-727   1 at f728-879   0 at f880+
icon  :   VISIBLE f398-511   ABSENT f512-668   VISIBLE f669-728   ABSENT f729-880   VISIBLE f881+
```
4/4 windows, with the expected 1-frame OAM-shadow → hardware lag. `$A0FE` stayed **0** for the whole run
(so its role is un-exercised here — noted, not invented). `$A013` is the frozen-action-span flag the port
already mirrors (`battletrace … A013=…`).

---

## 5 — THE SLEPT-PC POSE

### 5.1 The class → window-offset multiply (`dis.py 0x12E00 0xA0 1 1`)

```
12E20 DEC $9689,X / BEQ 12E28 / JMP C1/2F13      ; the per-PC pose TIMER
12E28 JSR C1/2F97      ; ** the status scan -> $A440 (class) + $A441 (icon state) **
12E2B JSR C1/2F22
12E2E JSR C1/308C      ; ** the icon dispatch **
12E31 LDA $A440 / CMP #03 / BNE 12E4C
12E38   LDA $99CF / ORA $99D0 / BEQ 12E4C
12E40   LDA #2B / STA $A440 ; $968C,X = FF        ; a class-3 -> 0x2B override (unrelated to status)
12E4C LDA $94 / ASL / CLC / ADC $94 / TAX         ; X = cmb*3
12E53 $A1E8/$A1E9 = $1C99,X / $1C9A,X             ; ptrA (bank E4)
12E5F $A1FE/$A1FF = $1CBA,X / $1CBB,X             ; ptrB
12E6B LDA $A440 / REP #20 / ASL A / ASL A / STA $82        ; ** $82 = class * 4 **
12E74 $A1FE += $82 ; 12E7C $A1E8 += $82                    ; ** BOTH pose pointers shift **
12E87 $A5 = $96DB,X (facing) ; $A7 = $966E,X (stride) ; JSR C1/00A7 (multiply)
12E9B LDX $A9 / STX $82                                    ; ** $82 is REUSED: now = facing × stride **
12EA6 LDA $968C,X / ADC #01 / STA $968C,X                  ; cursor++
12EB3 X = cursor + $A1FE ; LDA $E40000,X / BNE 12EC5
12EC0   TDC / LDX $94 / BRA 12EAE                          ;   dwell byte 0 = window TERMINATOR -> cursor 0
12EC5 $B1 = dwell ; $B2 = 0 ; $B3 = 5 ; JSR C1/00D7        ; C1/00D7 = the SNES hw DIVIDE ($4204/$4206)
12ED2 LDA $B5 / BNE 12ED7 / INC A / 12ED7 STA $9689,X      ; ** timer = max(dwell / 5, 1) **
12EDC X = cursor + $82 + $A1E8 ; LDA $E40000,X / STA $967B ; ** the E3 ASSEMBLY INDEX **
12EF4 LDA $A440 / CMP $A103,X / BEQ 12F01 / STA $A103,X / BRA 12F10   ; class changed -> rebuild
12F01 CMP #03 / BNE 12F10                                  ; ** class != 3 -> ALWAYS rebuild **
12F03 LDA $80 / BNE 12F10 / LDA $95D5 / CMP $94 / BNE 12F13 ; class 3 -> only the ACTIVE PC rebuilds
12F10 JSR C1/1C4A                                          ; the assembly/tile builder
12F13 INC $92 / CMP #03 / BNE JMP C1/2E01                  ; loop the 3 PC seats
```
The last two lines are an independent confirmation of `battle_stance.md` ADDENDUM 4 §S4.2 ("only the ACTIVE
PC bobs") — and note a **slept** PC (class 0x1A ≠ 3) rebuilds every tick regardless.

Oracle: `--monitor "A440:1,A441:2,5E4B:1,968C:3,1C99:6,1CBA:6,A09B:3,A0A7:1" --diff`
```
f0    A440=03  A441=FFFF  5E4B=00  968C=020000  1C99=00 30 E4 | A0 34 E4  1CBA=00 A8 E4 | 28 A9 E4  A09B=000000
f394  A440=03  A441=04FF  5E4B=04  968C=FF0000                 ; ** cursor reset (13017/1301F) **
f400  A440=1A  A441=04FF  5E4B=04  968C=000000                 ; ** class 0x1A = SLEEP **
```
`$A09B = 0` and `$A0A7 = 0` throughout ⇒ the **default class is 3** in this fight (matches the port's `0x0C`).

### 5.2 The sleep pose WINDOWS in ROM

`ptrA = E4/2600[cid*2]`, `ptrB = E4/2800[cid*2]`, `stride = (ptrA[cid+1] − ptrA[cid]) >> 2`
(exactly `src/battle.c:CtBattlePcPoseStage`). `window = ptrA + facing×stride + class×4`:

| PC | ptrA | ptrB | stride | class 3 (**+0x0C**, normal) | class 0x1A (**+0x68**, SLEEP) | dwell bytes |
|---|---|---|---|---|---|---|
| Crono | `E4/3000` | `E4/A800` | 0x128 | f0 `C6 4F C7 4F FF` · f1 `43 2F 44 2F FF` · f2 `C8 33 C9 33 FF` · f3 `CA 37 CB 37 FF` | **f0/f1 `CC CD FF`** · f2 `CE CF FF` · f3 `D0 D1 FF` | `05 05 00` |
| Marle | `E4/34A0` | `E4/A928` | 0x0F8 | f0 `D3 D4 D5 D4 FF` · f1 `D6 D7 D8 D7 FF` · f2 `D9 DA DB DA FF` · f3 `DC DD DE DD FF` | f0/f1 `54 55 FF` · **f2 `56 57 FF`** · f3 `58 59 FF` | `05 05 00` |

So the sleep window is a **2-entry cycle** (0xFF terminator), unlike the 4-entry idle.

### 5.3 The E3 assembly records reproduce the observed OAM **exactly**

PC E3 record = base `E4/2300[cid*3]` + idx×0x28 = `[16 cell-words +0x00][4 signed (dx,dy) +0x20]`
(`battle_anim_engine.md` ADDENDUM 1 / the 4-cell PC form; = `src/battle.c:CtBattlePcE3Rec`).
Crono base `$DC27B0`, Marle base `$DC9AD8`.

| record | ROM `(dx,dy)` ×4 | observed OAM at `pk700` (origin subtracted) |
|---|---|---|
| Crono **0xCC** | `(-8,-32) (-8,-48) (-8,-16) (-8,0)` | (the cursor-0 frame, f405: 008@-32, 00A@-48, 00C@-16, 00E@0) ✔ |
| Crono **0xCD** | `(-8,-24) (-8,-39) (-8,-8) (-8,8)` | f700, origin (134,144): 008@(126,120) 00A@(126,105) 00C@(126,136) 00E@(126,152) ✔ |
| Marle **0x56** | `(-8,-40) (-8,-56) (-8,-24) (-7,-8)` | (the cursor-0 frame) |
| Marle **0x57** | `(-8,-24) (-7,8) (-7,-8) (-8,-39)` | f700, origin (179,111): 020@(171,87) 022@(172,119) 024@(172,103) 026@(171,72) ✔ |

That Marle matches **0x57** (not 0x55) is itself the proof that the `facing×stride` term is live: her staged
facing is **2**. The pose is a 4-cell **vertical column** (all four cells at the same dx, 16px apart) — the
"front-facing upright asleep pose" §8.6 described by eye, now a ROM address.

### 5.4 The measured pose cadence — **16 render frames per entry (period 32)**

Per-frame `$968C` (cursor) + `$967B` (emitted assembly index) + the pal5/pal6 OAM clusters over the clean
idle window f881-1027:
```
Crono cursor -> 0 at f887, f919, f951, f983, f1015 ; -> 1 at f903, f935, f967, f999   (Δ = 16, 8 periods)
Marle cursor -> 1 at f895, f927, f959, f991        ; -> 0 at f911, f943, f975, f1007  (Δ = 16, offset +8)
$967B on those frames: CC / 57 / CD / 56 / CC …   (exactly the §5.2 windows)
```
**16 render frames per entry, byte-periodic over 8 consecutive periods, the two PCs 8 frames out of phase.**
`$9689` (the timer) reads **1** every frame, and the ROM reload is `max(dwellByte 5 / 5, 1) = 1` **tick** —
so the tick↔render-frame mapping (~16 render frames per PC pose tick here) is the **standing pose-engine
boundary** already flagged in `src/battle.c:CtBattlePcIdleWin` ("the ptrB per-step decode is the dossier's
OPEN item (§7) — pose timing stays on the pinned oracle fixtures"). **16 is the measured value; 5 is the ROM
byte; the multiplier is NOT derived** (§8 G2).

**Phase anchor (derived + observed):** on the inflict the scan writes `$968C,X = 0xFF` (`1301F`), so the
window always starts at **entry 0** on the first tick after the status appears — oracle: `968C=FF` at f394,
`968C=00` at f400.

---

## 6 — THE ICON'S PIXELS, PALETTE, AND DRAW ORDER

### 6.1 Tile source — **uncompressed ROM, flat `0x113900` = `$D1/3900`**

`--peek … pk100/pk700` + a byte search of the ROM for the VRAM bytes:
```
VRAM tile 0x1A6 -> ROM 0x1139C0    0x1A7 -> 0x1139E0    0x1B6 -> 0x113BC0    0x1C6 -> 0x113DC0
whole VRAM block 0x1A0..0x1DF (64 tiles, 0x800 bytes) == ROM[0x113900 : 0x113D00+0x100]   -> 64/64 EXACT
```
i.e. **`vramTile T (0x1A0 ≤ T ≤ 0x1DF)  ==  ROM flat 0x113900 + (T − 0x1A0)×32`**, uncompressed, no fixture.
Resident: identical at f100 (pre-sleep) and f700, and identical in the **forest** fight
(`--peek saves/battle_re/forest_battle_3hetake.state`) — a boot-uploaded, fight-invariant sheet.
16×16 cell of tile `T` = 8×8 tiles `T, T+1, T+0x10, T+0x11` (the project's standard OBJ cell rule).

The SLEEP glyphs (4bpp values; `F`=15 outline, `E`=14 gold, `D`=13 white):
```
tile 0x1A6                     tile 0x1C6
.......F........               ....FFF..FF.....
......FDF.......               ....FEEFFEF.....
..FFFFDEEFFFF...               ....FEEEEEF.....
..FDDDEEEDDEF...               ....FEEEEEF.....
...FEEEEEEEF....               ...FEEEEEEEF....
....FEEEEEF.....               ..FEEEEEEEEEF...
....FEEEEEF.....               ...FFFEEEFFF....
...FEEFFFEEF....               .....FEEF.......
....FF...FF.....               .....FEF........
                               ......F.........
```

### 6.2 Palette — already in the tree, and fight-invariant at the ink indices

Histogram over **all 16** icon cells (`0xA0,0xA2,…,0xAE` × both rows): value 0 = 2991 px, **13 = 195,
14 = 339, 15 = 571** — **no other index is ever used.**

| index | GATO fight (OBJ pal2) | forest fight (OBJ pal2) | colour |
|---|---|---|---|
| 13 | `7FFF` | `7FFF` | white |
| 14 | `02DF` | `02DF` | gold (255,181,0) |
| 15 | `0CC9` | `0CC9` | dark brown |
| 0-12 | fight-dependent | fight-dependent | (unused by the icons) |

OBJ **pal3** is byte-identical between the two fights in all 16 entries.
`src/data/battle_anim.h : ctBattleAnimPal[2]` is **already** the same 16 colours as the table above — the exact oracle CGRAM row, and
`ctBattleAnimPal[3]` likewise. **No palette fixture is needed.**

### 6.3 Pixel verification against the oracle framebuffer

`pk700.raw` (fb 512×480 doubled, true px `(x,y) = fb(2x,2y)`, bytes `[pad,B,G,R]`; a stance OBJ pixel at
OAM `(x,y)` lands at fb true-pixel `(x, y+8)` — `battle_stance.md` PART 2):

```
ICON  OAM#4 x=126 y=96 tile=0x1A6 attr=35 pal=2   ->  62/62 opaque px EXACT, 0 mismatch
POSE  Crono #5-8 + Marle #22-25 (E3 0xCD / 0x57)  -> 743/748 opaque px EXACT
```
The 5 residual pose pixels are precisely where the icon's 16×16 box overlaps Crono's top body cell
(icon rows 101-109 vs `00A@(126,105)`): the icon's own pixels verified 62/62 there, so **the icon is drawn
ON TOP of the body**.

### 6.4 The OAM SLOT rule

The builder appends at the shared OAM cursor `$1DC9` (`FEE6A/FEE96`); it is emitted **immediately before**
that combatant's body cells, so the icon takes the *lower* OAM index = SNES front priority. Observed in every
visible frame: Crono icon `#4`, body `#5-8`; Marle icon `#21`, body `#22-25`. There is **no fixed slot** — it
is "one slot ahead of the combatant's cluster". In our software renderer that is: **blit the icon AFTER the
pose cells** (the port already draws high-oidx-first, so "later = on top").

---

## 7 — THE PORT-FACING SUMMARY (the exact model to implement)

```
per combatant cmb (0..10), every frame:

  block   = 0x5E2D + cmb*0x80            (status byte 1 at block+0x1E; SLEEP = bit2 / mask 0x04)
  base    = 0x5E4A + cmb*0x80            (= block+0x1D; the CC/F77F offsets are relative to THIS)

  /* --- the scan (C1/2F97) --- */
  cls = 3 ; icon = -1                                   /* $A09B==0 in every observed fight -> 3 */
  for (r = ROM[0x0CF77F] ; !(r.off & 0x80) ; r += 4)
      if (mem[base + r.off] & r.mask) {
          cls  = (r.cls == 0xFF) ? 3 : r.cls;           /* $A0A7 == 0 observed */
          icon = (r.state & 0x80) ? -1 : r.state;       /* bit7 -> no icon (e.g. DEAD) */
          break;                                        /* first match wins */
      }
  if (icon != lastIcon[cmb]) { poseCursor[cmb] = 0; iconCursor[cmb] = 0; iconTimer[cmb] = 0; }

  /* --- the POSE (C1/2E00) --- */
  win     = ptrA(charId) + facing*stride + cls*4        /* cls 3 -> +0x0C == today's constant */
  poseIdx = ROM[win + poseCursor]                       /* 0xFF = terminator -> cursor 0 */
  /* cursor advances every 16 render frames (measured, §5.4) */

  /* --- the ICON (C1/308C + CF/EE2C) --- */
  if (icon < 0 || actionFreeze /* $A013 */) -> no icon this frame
  grp    = handlerBase[icon]                            /* state 4 -> 0x30 */
  period = ROM[0x0CF5FD + k[icon]]                      /* state 4 -> CC/F5FF = 10 */
  if (++iconTimer >= period) { iconTimer = 0; iconCursor = (iconCursor + 1) & 3; }
  rec    = ROM[0x0CF6D4 + grp + iconCursor*4]           /* {dx, dy, tile, attr} */
  draw 16x16 cell (ROM 0x113900 + ((0x100|rec.tile) - 0x1A0)*32) with pal (rec.attr>>1)&7
       at (liveFootX + (int8)rec.dx, liveFootY + (int8)rec.dy)      /* AFTER the pose cells */
```

Everything on the right-hand side is a ROM address or an oracle-measured constant. `liveFootX/Y` is the
port's existing foot (`$1D0C/$1D23`-equivalent, proven identical in §4.2).

**Inertness.** `stageAll` already zeroes `block+0x1E` for every combatant at combat start
(`src/battle.c:470-471`, `battle_status_pipeline.md` §9), so in every pinned fight the scan finds no match →
`cls = 3` → the pose window offset is `3*4 = 0x0C` = the byte the port hardcodes today, and `icon = -1` →
nothing drawn. **Zero pins can move.**

---

## 8 — GAPS (admitted, with the capture that closes each)

| # | gap | what would close it |
|---|---|---|
| **G1** | **`$A09B` / `$A0A7` semantics.** Both read **0** in every frame of every capture here, so the "default class 0" branch (`12F99`) and the "class 0xFF → keep scanning" branch (`13006`) are never taken. I implement the observed branch and cite the other. | A capture where a PC has `$A09B[cmb] != 0` (or a status matching record 4, `+0x1E` bit3, with `$A0A7 != 0`) — e.g. a `sleeppatch`-style ROM whose rider mask is `0x08`. |
| **G2** | **The pose-tick ↔ render-frame multiplier.** ROM reload = `max(dwellByte/5, 1)` = **1 tick**; measured = **16 render frames** per entry, with the two PCs 8 frames apart. The tick scheduler (`$9912` gate at `12E11`, `$9922`, the per-combatant anim-slot phase) is the standing `battle_anim_engine.md` §D O7 boundary. | A per-frame `--monitor "9912:1,9922:3,9689:3,92:1"` trace correlated to the call site of `C1/2E00` (find its caller in the C1 frame loop), or a 3-PC fight to see whether the 8-frame offset is `frames/nPresentPCs`. |
| **G3** | **The other 8 icon groups / 13 other status bits are table-derived but not *observed*.** Only state 4 (sleep) was produced live. The table rows are ROM-verbatim, so the mapping is derived; the *rendering* of the other groups is not oracle-confirmed. | The same `sleeppatch/` recipe with the rider mask set to `0x80/0x02/0x08/0x20/0x01/0x40` (all category-1, all reachable through `E976`), one capture each. |
| **G4** | **Enemy status pose.** `$A440` is PC-only (§0.9); an enemy's pose class comes from `$5E0D`. Whether an enemy shows a status *icon* is derived (the `C1/308C` loop covers cmb 0..10) but **not observed** — GATO is immune (`+0x23 = 0xFF`). | A `sleeppatch`-style capture against a **non-immune** enemy (e.g. the forest Hetakes, `forest_battle_3hetake.state`) with Crono's `CC/213F[58]` rider armed. |
| **G5** | **`$A0FE`** (the second builder gate, `CF/EDC5`) read **0** for the whole run — its role is unknown. | Any capture where it is non-zero (unknown trigger). |
| **G6** | **`$A5E3`** (the OAM attr OR-mask) read **0x30** constantly. Treated as "prio 3", which our software renderer expresses as draw order. | Irrelevant to the port unless it is ever ≠ 0x30; a wider `--monitor "A5E3:1" --diff` over a full fight would confirm. |
| **G7** | **ROM-free path.** `ctAnimengPcBlit` falls back to `CtBattlePcE3Rec`/`CtBattlePcSheetTile` when an index is not in the sparse bake, so **with the ROM present nothing needs baking**. Without a ROM (`--datonly`-style runs) the sleep indices are missing. | Add `0xCC,0xCD,0xCE,0xCF,0xD0,0xD1` (Crono) and `0x54..0x59` (Marle) to `tools/_gen_animeng_pc.py`'s index list and regenerate `src/data/animeng_e3_pc.h` / `animeng_sheet_pc.h`. |

---

## 9 — SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)

Dir: `%TEMP%/claude/C--Users-…-06e5d2d4-…/scratchpad/`
- `sleeppatch/` — copied from the §8.3 build (ROM + `ct_assets.dat` + `ct_probe.exe` + `SDL2.dll` +
  `crono_normal_ready.state`). Verified patched: `CC/89C6[0] = 03 63 04 3A 3B 04 3E 14 00 0C 00 80`,
  `CC/89C6[1] = 03 63 04 3A 3B 04 3E 14 00 12 00 80`, `CC/213F[58] = 03 63 04 3A 3B 01 3E 84 80 08 04 80`.
  `mk_sleeppatch.py` rebuilds it from a clean ROM.
- `sleeppatch/pk100.raw{,.wram,.vram,.oam}` — `--peek crono_normal_ready.state pk100.raw --btnscript "100:10,0:20,100:10,0:60"`
- `sleeppatch/pk700.raw{,.wram,.vram,.oam}` — same with `0:660` (**the slept frame used for every pixel verify**)
- `oamtrace.txt` — 1040-frame `--monitor "0700:512,0900:32,5E4B:1,5ECB:1,95D5:1"` OAM-shadow trace
- `trace.py` (monitor→per-frame OAM sprite decode), `oamdec.py` (`.oam` file decode), `rendertile.py`
  (VRAM/ROM 16×16 4bpp cell render + palette)
- ⚠ **`--peek`'s `.oam` layout**: `fwrite(ppu->oam, 2, 512)` writes **1024** bytes (the struct's `oam[0x100]`
  *plus* the adjacent `highOam`), then `highOam` again at **1024**, then **CGRAM at 1056**. Low OAM = `[0:512]`,
  usable high table = `[512:544]` *or* `[1024:1056]`, **CGRAM = `[1056:1568]`** (not `[544:…]`).

Disasm anchors (this pass): status scan **`0x12F97`**(+`0x13006`); icon dispatch **`0x1308C`**; jump table
**`0x13216`**; sleep icon handler **`0x131B0`**; latch clobber **`0xCF102`**; icon OAM builder **`0xFEE2C`**
(gate `0xFEDC0`); PC pose tick **`0x12E00`** (class multiply `0x12E6B`, dwell divide `0x12EC5`→`0x100D7`);
enemy pose tick **`0x13360`** (no `$A440`). ROM tables: `CC/F77F` `0x0CF77F` · `CC/F6D4` `0x0CF6D4` ·
`CC/F5FD..F603` `0x0CF5FD` · `CC/F8ED` `0x0CF8ED` · icon sheet **`0x113900`** (`$D1/3900`) ·
`E4/2600`/`E4/2800`/`E4/2300` (pose staging, already in `src/battle.c`).

---

## 10 — IMPLEMENTATION PLAN (against the tree at HEAD 38d7ec5)

The port is unusually well positioned: the PC pose engine (Stage 3) is already landed, `ctAnimengPcBlit`
already falls back to **ROM-direct** records/tiles for un-baked indices, `ctBattleAnimPal[2]/[3]` already
hold the icon palette, and `stageAll` already zeroes `+0x1E` so the whole slice is inert in every pinned fight.

**S1 — `src/battle.c`: the scan (new, ~25 lines).**
`int CtBattleStatusScan(const uint8_t* mem, int cmb, int* poseCls, int* iconState)` — a literal port of
`C1/2F97`'s record loop over `#define ROM_STATUS_SCAN_TBL 0x0CF77Fu` (same style as the existing
`ROM_DWELL_REMAP_TBL 0x0CFBCEu`). Base `0x5E4A + cmb*0x80`; default `cls = 3`, `icon = -1`; first match wins;
`cls == 0xFF → 3`; `iconState & 0x80 → -1`. **Risk: low** — pure read, and with `+0x1E == 0` the loop always
falls through to the defaults, so byte-identical output today.

**S2 — `src/battle.c`: expose the icon record (new, ~15 lines).**
`int CtBattleStatusIcon(int state, int cursor, int8_t* dx, int8_t* dy, int* tile, int* pal, int* period)` —
reads the `C1/3216`-derived `state → {groupBase, periodAddr}` map (a 15-entry static table transcribed from
§3.1, each entry citing its `C1/31xx` handler) and then `ROM[0x0CF6D4 + groupBase + cursor*4]`.
`#define ROM_STATUS_ICON_TBL 0x0CF6D4u`, `#define ROM_STATUS_ICON_PERIOD 0x0CF5FDu`.
Plus `const uint8_t* CtBattleStatusIconTile(int vramTile)` → `gRom + 0x113900 + (vramTile - 0x1A0)*32`
(`#define ROM_STATUS_ICON_SHEET 0x113900u`), mirroring `CtBattlePcSheetTile`. **Risk: low.**

**S3 — `src/battle.c:CtBattlePcIdleWin` (line 277): the hardcoded `+ 0x0C` becomes `+ cls*4`.**
Add a `cls` parameter (or a sibling `CtBattlePcStatusWin(charId, facing, cls, poses[4])`). The existing
comment "idle = class-0 base 0x0C" is **wrong and should be corrected**: 0x0C is class **3**×4 (§1/§5.1).
Note `CtBattlePcWinPose(charId, winOff, entry, facing)` (line 290) is **already** generic on `winOff`, so
the sleep window needs no new accessor at all: `CtBattlePcWinPose(charId, 0x68, cursor, facing)`.
**Risk: low** — every existing caller passes `cls = 3` ⇒ identical bytes.

**S4 — `src/field.c:pcStanceIdx` (line 2122) + `battleStanceRender` (line 2134): the pose override.**
In the per-seat loop, before `pcStanceIdx`, call `CtBattleStatusScan(mem, seat, &cls, &icon)`.
If `cls == 3` keep today's exact `pcStanceIdx(charId, facing)` path (win[0]/win[1], the pinned static
stance). If `cls != 3`, take `poseIdx = CtBattlePcWinPose(charId, cls*4, cursor, facing)` with
`cursor = ((h->frames - h->statusPoseArm[seat]) / 16) & 1`, `statusPoseArm[seat]` latched on the frame the
scan's `icon` changes (the ROM's `$968C,X = 0xFF` at `1301F`). Nothing else in the function changes; the foot,
knockback, flash-palette and skip gates are untouched. `ctAnimengPcBlit` needs **no change** — it resolves
0xCC/0xCD/0x56/0x57 straight from ROM. **Risk: medium** — the `cls == 3` branch is *deliberately* left on the
existing pinned approximation (the PC idle cursor is the pre-existing `CtBattlePcIdleWin` OPEN, not something
this change may silently alter); document that explicitly so it is not mistaken for a status special-case.

**S5 — `src/field.c`: `battleStatusIconRender` (new, ~35 lines), called from `battleStanceRender`
immediately AFTER `ctAnimengPcBlit` for the same seat (§6.4: the icon is the lower OAM index ⇒ drawn last).**
```
if (icon < 0 || mem[0xA013]) return;                      /* CF/EDC0 gate ($A0FE observed 0, G5) */
cursor = ((h->frames - h->statusIconArm[seat]) / period) & 3;   /* arm = the first drawn frame */
CtBattleStatusIcon(icon, cursor, &dx,&dy,&tile,&pal,&period);
load ROM tiles (tile-0xA0) .. into a 0x40-entry scratch indexed by (vramTile - 0x1A0);
battleBlitCell(fb, scratch, 0x40, tile - 0xA0, footX + dx, footY + dy, ctBattleAnimPal[pal], 0, 0);
```
`footX/footY` is the same live foot the pose used (proven == `$1D0C/$1D23`, §4.2). **Risk: low** — gated on
`icon >= 0`, unreachable in a pinned fight.

**S6 (optional, ROM-free only) — `tools/_gen_animeng_pc.py`:** add Crono `0xCC,0xCD,0xCE,0xCF,0xD0,0xD1` and
Marle `0x54..0x59` to the index list and regenerate `src/data/animeng_e3_pc.h` + `animeng_sheet_pc.h`. With a ROM
present this is a no-op (G7). **Risk: low.**

**Deliberately NOT done (and why):** the enemy status pose (`$A440` is PC-only, §0.9 — an enemy would get the
icon only); the 13 other status bits' icons (table-derived but unobserved, G3); the `$A09B`/`$A0A7` branches
(G1); making the class-3 idle cursor live (G2, a separate re-pin-heavy change).

---

## 11 — THE REGRESSION CHECK TO PIN IT

New `tools/regress_checks.json` entry, same shape as the existing `field-battle-status-inflict`
(`saves/battle_re/gato_t0.raw.wram`, the forced rider) plus the `CT_BATTLESHOT_AT` fnv hook the
`field-battle-blink-*` / `field-battle-victory-*` checks already use:

```json
{ "name": "field-battle-status-render",
  "argv": ["build/Belthasar.exe","--assets","ct_assets.dat","--battletest","saves/battle_re/gato_t0.raw.wram"],
  "env": { "CT_BATTLE_TRACE":"1", "CT_BATTLE_MASHA":"1", "CT_BATTLE_STATUSTEST":"99",
           "CT_BATTLE_DEBUGEND":"1200", "CT_BATTLESHOT_AT":"1150",
           "CT_BATTLESHOT":"build/status_sleep.raw" },
  "inputs": ["ct_assets.dat","saves/battle_re/gato_t0.raw.wram","Chrono_Trigger_USA_SNES.sfc"],
  "expect": [ "battleshot: f1150 fnv=<NEW>",
              "battle-status-skip f1110 seat0 slept(+0x1E=04) turn SKIPPED (no command window)",
              "battletest: PASS (staging 18/18, stats OK (Gato HP 76), points 0F)" ],
  "pin":    [ "^battleshot: f1150 fnv=", "^battle-status-skip ", "^battletest: (PASS|FAIL)" ],
  "tier": "quick", "timeout_s": 30, "exclusive": true }
```

**Why f1150 is the right frame** (verified on the CURRENT binary, before any change):
* the port's own trace gives `A013 = 0` continuously from the `act-end` at **f990** to `end-debug` at f1200 —
  so the icon gate (§4.3) is open;
* `battle-status-wake f990` is immediately followed by a re-inflict (`roll=14 … +0x1E=04`) and
  `battle-status-skip f1110` confirms seat 0 is still slept at f1110 — so Crono is slept across the whole
  f990-1200 window;
* the hook fires and is **deterministic**: `battleshot: f1150 fnv=E9DDFF51`, identical on two consecutive
  runs, and `battleshot: f1020 fnv=8EF13D71` is an equally valid alternative frame.

`E9DDFF51` is the **pre-implementation baseline** (no pose override, no icon). After S1-S5 it MUST change —
and the shot must be eyeballed once (`tools/_fbpng.py` on `build/status_sleep.raw` — the `[u32 w][u32 h][RGB]`
CT_BATTLESHOT format, `src/field.c:4159`) against the oracle frame regenerated from `sleeppatch/`:
```
cd <scratch>/sleeppatch
./ct_probe.exe --peek crono_normal_ready.state ref.raw --btnscript "100:10,0:20,100:10,0:660"
# then, per §6.3: Crono's cluster = E3 0xCD at foot (134,144); icon = tile 0x1A6 pal2 at (126,96)
```

**Suite expectation: ZERO existing pins move.** `stageAll` zeroes `+0x1E` for all combatants at combat start,
so `CtBattleStatusScan` returns `cls = 3, icon = -1` in every other check → the pose window offset stays
`3*4 = 0x0C` (the current constant) and nothing extra is drawn. Run `python tools/regress.py --quick` then the
FULL tier; any moved pin outside `field-battle-status-*` is a port bug, not a legitimate re-pin.

---

## 12 — THE ICON MACHINE IS PORTED (2026-09-10, item 7c of the 09-09 list)

The port used to draw the icon from the ROM's records with a `(frames / period) & 3` cursor and a host-side latch
(`pcIconLast`). It now runs §3's machine in the ROM's own bytes, and the `$96F5` PRESENT flags with it:

* **`$96F5,X` was never staged.** Every `C1/308C` dispatch opens with `LDA $96F5,X / BEQ` (so do `C1/34AF`,
  `C1/2E09`, `C1/35EB`); the loader's `CC/E63D INC $96F5,X` per staged combatant is now done at `CtBattleBegin`
  (oracle gato_t0: `01 01 00 00 00 00 01 ..`). Without it the dispatch bailed for everyone, the `CC/F102`
  clobber was never re-latched, and the PC walk's `C1/301A` test (`$A441 != $A44C -> $968C := FF`) reset the idle
  cursor after every action -- six unrelated pins moved on the first build. `CC/E360-E36D` (`$A441/$A44C := FF`)
  is staged too.
* **The scan's stores** (`$A440`, `$A441,Y`) at the PC walk's visit (`C1/2E28`) and -- the part the old model
  lacked -- at EVERY ACTION CLOSE: `C1/411D-4131` runs `C1/305C` in a loop from `$99D9 = 0` until `$A102` rises
  (the scan cursor wrapping 11 -> 3), scanning every present combatant and dispatching all eleven after each.
  That is how a status landed mid-action reaches its icon: the sleeppatch arm at f394 (§4.1) was the counter
  action's close, and this fixture's Crono arms at its own counter's close f555.
* **`C1/308C` + the `C1/3216` handlers**, table-transcribed with their clears (`C1/30B6`), `$A46D`-only states,
  and the arm shape (`$A48E`, `$A4DB = FF`, `$A4D0` group, `$A4BA = 1`, `$A4C5 = CC/F5FD+k`). State 3's `$2300`
  HUD-buffer copy (`C1/30C7-30E0`, PCs only) and state 15's `BRL C1/4BC8` are not modelled (noted, not invented).
* **`CC/F102` at every open** (`C1/409C`, one instruction before `INC $A013`), on all three of the port's openers.
* **`CF/EDC0 -> CF/EE2C`** once per frame at the top of the tick: skipped while `$A013 | $A0FE`; per armed icon
  `DEC $A4BA`, reload from `$A4C5`, `INC $A4DB` with the wrap at 4. The renderer (`CtBattleStatusIconLive`) reads
  `$A48E/$A4D0/$A4DB` and draws nothing while the cursor is still the arm's `$FF` (the ROM's build precedes the
  arm within a frame). Enemies get the same layer at their live feet.
* Traces: `icon f cmb <arm|clear|mark|state15> state latch on grp per tmr cur`, `icontick f cmb cur per`.
* Residue: the ROM's close sequence runs the scan (`C1/4127`) two to three frames BEFORE `C1/41BD` drops `$A013`
  (sleeppatch: arm f394, gate open f397, first build f398); the port's act-end does both in one frame (arm f555,
  first build f556). The `D6/D8/E0` script-side callers of `C1/307C` (effect routines) are not modelled.
* Check: `field-battle-status-render` re-pinned with the `icon`/`icontick` lines (five mutations: the period
  reload, the wrap at 4, the open clobber, the build gate, the close scan-all).


## The colour cycle ends with the fight: C1/356D's `JSR C1/30B6` x3 (2026-10-04)

A status whose icon handler is a mark (C1/3185: states 6 Magic Wall, 7 Protect, C Haste, D Slow) has no OBJ over the
head; CF/E7D7 walks colours 1 and 12 of the seat's OBJ palette row ($993C,Y << 4 over the live page $2300) along
CE/0100[CC/F754[state] + $A499,Y] while $A46D,Y is set. Nothing on the field undoes that row: the page outlives the
battle (C0/0304's tail rebuilds the map and the script image, not $2300).

The victory's stager C1/356D (C1/8186 `LDA #08 / JSR C1/0003` -> C1/0051[8]) opens:

```
356D JSL CD/0009 / INC $A0FE
3574 LDX #0007 / STZ $98A7,X / DEX / BPL            ; the eight movers off
357D LDX #0002 / STX $94 / JSR C1/30B6 / DEC $94 / BPL   ; seats 2, 1, 0
```

and C1/30B6 clears $A483 / $A48E / $A457 / $A46D / $A478 for the seat and, for a PC (`CPX #0003 / BCS`), copies the
row's 26 bytes from the base page ($2100 + row -> $2300 + row, C1/30CC-30DE). MEASURED, the rule's cartridge, the
Naga-ettes' fight mashing A (saves/field_re/m81_nuns_lake.state; Crono slowed c3911, Marle c5673; saved at f9000): the
last enemy falls and on that frame `$5E4B 20 -> 00` (C1:EAB0, C1/EA9D's status wipe), `$A46D = 00` (C1:30C4) and
`$23A2 / $23B8 = 1065 / 1C88` (C1:30DC); the field after the ack keeps them. The port's victory branch named the sequence
in a comment and did not run the three calls: a Slowed Crono walked the field with colours 1 / 12 at the ramp's last
step (18C6) -- the outline the user saw. battle.c's victory branch now runs `battleIconClear` for seats 2..0; pinned by
`battle-status-colour-cycle-ends-with-the-fight-vs-oracle` (tools/battle_status_palette_after_cmp.py).

Not covered: the run-away and the defeat. Neither reaches C1/356D; whether either clears a cycling row is not measured.
