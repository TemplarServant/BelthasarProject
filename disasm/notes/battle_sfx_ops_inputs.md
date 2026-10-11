# Battle SFX — closing the two undefined inputs ($A4E7, $A2B0), the interpreter verdict, and the port site

Companion to `battle_sfx_ops.md` (commit 2dfdd86), which left three things open. This file closes all
three **on the oracle**, and — as a by-product — corrects the script TABLE the port is parsing.

Every claim below carries the command that produced it. Anything I could not measure is marked
**NOT DERIVED** or **disasm-only, unconfirmed**, explicitly.

Drive used everywhere below (the forest battle, Crono + Marle, 3 PC physical attacks):

```
S = "40:240," + "100:4,0:4" x150          # hold LEFT into the encounter, then A-mash
build/ct.exe --monitor "<regions>" --from saves/battle_re/forest_holdleft_pre.state \
             --btnscript "$S" --frames 1450 --diff --writer
```

⚠ `--writer` reports the PC **after** the storing instruction. Calibrated on two known sites:
`C1/1B43 STA $1E01` reports `C1:1B46`; `C1/40D5 STA $1E01` reports `C1:40D8`. Both are +3 (the
`8D xx xx` length). Every writer attribution below uses that convention.

---

## 0. TL;DR

| open item | verdict |
|---|---|
| `$A4E7` | = the acting combatant's **equipped weapon item id**, `$1C48[5 * $AE91]`, written by the ONLY writer `C1/4349` at each PC-action start. **DERIVED + measured** (01 = Crono/Wood Sword, 11 = Marle/Bow). |
| `$A2B0` | not persistent state — it is the **output register of `C1/762E`**, which op 0x7A/0x7B call with their 2nd operand one instruction before reading it. **DERIVED** (the mode table was already in `battle_ice_anchor.md` §2). No new input needed. |
| which interpreter | **the C1 handlers post INLINE.** The CD staging slot `$CD23/$CD24/$CD25` is provably NOT the source in this battle. **MEASURED.** |
| port site | **there is NO live per-frame opcode execution point in `src/battle.c`.** `battleAnimParse` (3267) is a static pre-pass; `battleAdvanceAction` (4181, called from `CtBattleTick` at 4888) is the per-render-frame driver and is where a post would have to be emitted. |
| ⚠ bonus defect | `ROM_ATK_ANIM_TBL 0x0D5FF0` (battle.c:84) is **the wrong table for a PC physical attack**. The oracle resolves the PC melee script through **`CD/5EC6[$9877*2]` into bank CE**, and that is where the `7C` opcode actually lives. The CD/5FF0 scripts contain **no** 0x78-0x7D opcode at all. |

---

## 1. `$A4E7` — DERIVED (disasm) + MEASURED (oracle)

### 1.1 The writers — a ROM-wide scan

```
python - <<'EOF'   # scan every abs/abs,X/abs,Y opcode whose operand is E7 A4
...  (full script: match rom.find(b'\xE7\xA4'), classify rom[i-1] against the 65816 abs opcode set)
EOF
=== target $A4E7 ===
  file 0x014349  bank C1:4349  8D STA abs $A4E7
  file 0x0161EB  bank C1:61EB  AD LDA abs $A4E7
  file 0x0161F5  bank C1:61F5  AD LDA abs $A4E7
  file 0x1F600D  bank C1F:600D  5E LSR abs,X $A4E7     <- file 0x1F600D = bank $DF, a data bank
  file 0x2CA6AE  bank C2C:A6AE  99 STA abs,Y $A4E7     <- file 0x2CA6AE = bank $EC, a data bank
```

**Exactly one writer in code: `C1/4349`.** (The two high-file-offset hits are in banks $DF/$EC — graphics/
data space; they are byte coincidences, not instructions. *disasm-only, unconfirmed* that they are data —
but the oracle agrees: the only writer PC ever reported for `$A4E7` during a battle is `C1:434C`.)

### 1.2 What `C1/4349` stores — `python tools/dis.py 14300 0x90`

```
  1432C: LDA  $AE91                 ; the acting combatant's PC slot
  1432F: CMP  #03
  14331: BCC  ->14336
  14333: JMP  C1/4497
  14336: JSL  CCF110
  1433A: JSR  C1/49FF
  1433D: LDA  $AE91
  14340: TAX
  14341: LDA  $CCF83F,X             ; X2 = CC/F83F[slot]
  14345: TAX
  14346: LDA  $1C48,X               ; A = $7E:1C48[X2]
  14349: STA  $A4E7                 ; <<<< THE ONLY WRITER
```

`CC/F83F` (`rom[0x0CF83F..]`):

```
00 05 0A 0F 14 19 1E | 00 06 0C 12 18 1E 24 | 80 11 80 11 84 11
^^^^^^^^^^^^^^^^^^^^ stride-5 index table (a second, stride-6 table follows at CC/F846)
```

so **`$A4E7 = $1C48[5 * $AE91]`**.

### 1.3 What `$1C48` is — the filler, `python tools/dis.py 3DAF04 0x50`

```
  3DAF04: LDX  $02              ; $02 = the slot counter
  3DAF08: TXA / ASL A / TAX
  3DAF0B: LDA  $FDA80B,X        ; per-slot BASE (16-bit)
  3DAF0F: TAX
  3DAF13: LDA  $0029,X          ; base + 0x29 = the slot's EQUIPPED WEAPON byte
  3DAF16: STA  $06
  3DAF18: BEQ  ->3DAF67         ; no weapon -> leave the record blank
  3DAF2C: LDX  $0A
  3DAF2E: STA  $001C48,X        ; $1C48[5*slot] = weapon item id
  ...
  3DAF5F: STA  $1C4A,X          ; +2 = #80  (constant)
  3DAF64: STA  $1C4B,X          ; +3 = #01  (constant)
  3DAF6C: ADC  #0005            ; $0A += 5 -> the record STRIDE is 5
  3DAF74: INC  $02
  3DAF78: CMP  #03              ; ... for slots 0,1,2 ONLY
  3DAF7A: BCS  ->3DAF7F  (RTL)
```

`FD/A80B` = `2D 5E AD 5E 2D 5F AD 5F …` → `0x5E2D, 0x5EAD, 0x5F2D, …` (stride 0x80): the three battle-PC
record bases. `+0x29` is the weapon.

⇒ **`$1C48` is a 3-entry, stride-5 per-BATTLE-SLOT array whose byte +0 is the slot's equipped weapon
item id, and `$A4E7` is that byte for the acting slot.**

*Caveat, stated honestly:* the forest party is Crono (char 0, slot 0) + Marle (char 1, slot 1), so
**slot index and character index coincide** and this capture cannot separate them. The structural argument
that `$AE91` is a SLOT is: the filler loop runs `slot = 0..2` (`CMP #03`) and `$1C48` therefore has 3
records, so an `$AE91` ranging over character ids 0..6 would index off the end. A 3-PC party with a
non-0/1/2 character (e.g. Frog in slot 2) would settle it — **NOT DERIVED** (no such capture used).

### 1.4 The oracle — `--monitor "7E1E00:3,7ECD23:3,7EA4E7:1,7EA2B0:1,7EAE91:1" … --diff --writer`

```
  f229    7E1E00=0045B0  7ECD23=00A411  7EA4E7=00*(CF:FB9A)  7EA2B0=00*(CF:FB9A)  7EAE91=80
  f341    7E1E00=000080  7ECD23=00A411  7EA4E7=01*(C1:434C)  7EA2B0=00        7EAE91=00
  f383    7E1E00=004F80*(C7:0196) …                          7EA4E7=01        7EAE91=00
  f601    7E1E00=000080  …                                   7EA4E7=11*(C1:434C)  7EAE91=01
  f625    7E1E00=00437F*(C7:0196) …                          7EA4E7=11        7EAE91=01
  f887    7E1E00=000080  …                                   7EA4E7=01*(C1:434C)  7EAE91=00
  f921    7E1E00=004FA0*(C7:0196) …                          7EA4E7=01        7EAE91=00
```

`C1:434C` = the PC after `14349 STA $A4E7` ⇒ the derived writer, confirmed. `$A4E7` is **re-loaded at the
start of every PC action** (f341 / f601 / f887, one per attack).

### 1.5 The live `$1C48` array — `--monitor "7ECD24:2,7E1C48:16,7E2980:3,7EAE91:1" … --diff --writer`

```
  f0      7E1C48=07000700070007000700070000000000     (field values)
  f236    7E1C48=01008001801100800140000000000000*(FD:AF67)
  f240    7E1C48=010080018011008001400000000000C0*(CC:E997)
  f1381   7E1C48=07000700070007000700070000000000*(C0:0914)   (battle over, field restored)
          7E2980=000180   -> party = Crono(0), Marle(1), empty(0x80)
```

Split into stride-5 records:

| slot | +0 weapon | +1 | +2 | +3 | +4 |
|---|---|---|---|---|---|
| 0 (Crono) | **01** | 00 | 80 | 01 | 80 |
| 1 (Marle) | **11** | 00 | 80 | 01 | 40 |
| 2 (empty) | 00 | 00 | 00 | 00 | 00 |

`$A4E7` measured `01` when `$AE91=0` and `11` when `$AE91=1` — **exactly `$1C48[5*$AE91]`, byte-for-byte.**
(`+2 = 80`, `+3 = 01` are the two constants the filler stores; `+4` is the `LSR` loop's output at
`3DAF4D-3DAF55`, not needed here.)

### 1.6 What the table lookup yields — `CD/6D4C` / `CD/6DAC`

```
python -c "rom=open('Chrono_Trigger_USA_SNES.sfc','rb').read(); print(' '.join('%02X'%b for b in rom[0x0D6D4C:0x0D6D4C+0x60]))"
CD/6D4C: 4F x17, 43 x14, (52 50)x…, 51 x13, 4E x9, E7 x7, 5F x4, 4F …
CD/6DAC: 40 x17, 47 x14, 44 x15,   41 x13, 4D x9, E8 x7, 60 x4, 40 …
```

The run boundaries (0x00-0x10, 0x11-0x1E, 0x1F-0x2D, 0x2E-0x3A, 0x3B-0x43, 0x44-0x4A, 0x4B-0x4E) are the
CT weapon-item id blocks. `CD/6D4C[0x01] = 4F` and `CD/6D4C[0x11] = 43` — and **`4F` / `43` are exactly the
ids the oracle posted** (§4). So `CD/6D4C` = the per-weapon *hit* sound, `CD/6DAC` = the per-weapon *second*
sound (op 0x7D). No 0x7D fired in this capture ⇒ `CD/6DAC`'s role is **disasm-only, unconfirmed**.

---

## 2. `$A2B0` — DERIVED: it is an OUTPUT, not an input

`python - <<scan>>` for `$A2B0` finds **28 writers, all in bank C1**, all inside `C1/762E`'s leaf routines
(`C1/7640`, `C1/765A`, `C1/76CF`, `C1/7736`, `C1/778A`, `C1/77CA/D3/E2/FA`, `C1/78D0`, `C1/790A`, `C1/796D`,
plus the `C1/4FC2/4FD4`, `C1/51F6/520D`, `C1/540C/541E`, `C1/6733`, `C1/6F43/6F87`, `C1/7211` copy sites).

`python tools/dis.py 1762E 0x60`:

```
  1762E: PHY / PHX / TAY / ASL A / TAX / JSR ($79D3,X)   ; mode -> leaf
  17639: LDA $A014,Y / TAX / LDA $1D0C,X / STA $A2B0     ; mode 00-08: a COMBATANT's screen X
                                        LDA $1D23,X / STA $A2B2
  1764A: TYA / SBC #09 / TAY / LDA $A014,Y / TAX
         CLC / LDA $1D0C,X / ADC $9708,X / STA $A2B0     ; mode 09-11: + the per-combatant engage offset
```

and the op that reads it (`python tools/dis.py 1619E 0x90`):

```
  161C1: INY / LDA [$E6],Y / STA $1E01    ; id  = operand 1
  161C7: INY / LDA [$E6],Y
  161CA: JSR  C1/762E                     ; <<-- resolve operand 2 into $A2B0 RIGHT HERE
  161CD: LDA  $A2B0 / STA $1E02           ; pan = the just-resolved X
```

⇒ **`$A2B0` carries no state across the op.** It is written by `C1/762E` one instruction before op 0x7A/0x7B
read it. The full mode table is already derived in `disasm/notes/battle_ice_anchor.md` §2
(`C1/79D3`: 00-08 → `C1/7639`, 09-11 → `C1/764A`, 12-18 → …, 19-1B → `C1/77DA`) and
`battle_tech_cyclone_choreography.md` §2. **Nothing new is needed to port 0x7A/0x7B: pan = C1/762E(operand2).**

Oracle corroboration (same run as §1.4): every `$A2B0` change during the battle carries a `C1/762E`-leaf
writer, and it changes many times per action —

```
  f364  7EA2B0=78*(C1:7643)     f384  7EA2B0=80*(C1:7643)     f422  7EA2B0=78*(C1:7643)
  f429  7EA2B0=A0*(C1:77E5)     f628  7EA2B0=7F*(C1:765D)     f648  7EA2B0=58*(C1:765D)
  f910  7EA2B0=A8*(C1:7643)     f966  7EA2B0=A0*(C1:77E5)
```

**NOT DERIVED:** no op 0x7A/0x7B fires in this capture (§4), so the 0x7A/0x7B pan path is
**disasm-only, unconfirmed** end-to-end. (It IS reachable: the `A0B3 != 0` variant of Crono's own melee
script uses `7A 02 00` — §5.)

---

## 3. Which interpreter drives the battle — MEASURED: the **C1 inline** handlers

### 3.1 The attribution run — `--monitor "7E1E01:1,7E1E02:1" … --diff --writer`

```
  f244    7E1E01=08*(CD:3B74)  7E1E02=80*(CD:3B7D)
  f327    7E1E01=42*(C1:1B46)  7E1E02=80(C1:1B50)
  f329    7E1E01=00*(C1:1B58)  7E1E02=80(C1:1B62)
  f383    7E1E01=4F*(C1:6200)  7E1E02=80(C1:620F)     <-- the anim-script SFX op
  f481    7E1E01=FF*(C1:40D8)  7E1E02=80
  f485    7E1E01=95*(CD:3B74)  7E1E02=78*(CD:3B7D)
  f581    7E1E01=42*(C1:1B46)  f587 00*(C1:1B58)
  f625    7E1E01=43*(C1:6200)  7E1E02=7F*(C1:620F)    <-- the anim-script SFX op
  f744    7E1E01=FF*(C1:40D8)  f748 95*(CD:3B74) pan 58
  f873    42*(C1:1B46)  f876 00*(C1:1B58)
  f921    7E1E01=4F*(C1:6200)  7E1E02=A0*(C1:620F)    <-- the anim-script SFX op
  f1018   FF*(C1:40D8)  f1022 95*(CD:3B74) pan A8
```

`C1:6200` = the PC after `161FD STA $1E01`, and `C1:620F` = the PC after `1620C STA $1E02`. Both live in the
**op 0x7C / 0x7D handler tail** (`C1/61EB` / `C1/61F5`). Confirmed inline, no staging.

### 3.2 The CD staging slot did NOT fire — the falsification

A naive reading of `CD:3B74` says "the flush ran". It did not. `CD/3B71` is a **shared post subroutine**, and
the flush is only one of its four callers:

```
python - <<'EOF'   # scan for 20 71 3B / 22 71 3B CD / 20 5F 3B
=== JSR $3B71 ===  0x0D009D (CD:009D)   0x0D01CE (CD:01CE)   0x0D0502 (CD:0502)
=== JSR $3B5F ===  0x0D04AF (CD:04AF)                        <- the ONLY flush caller
EOF
```

`python tools/dis.py D3B50 0x50`:

```
  D3B5F: LDA $CD23 / BEQ ->D3B70        ; nothing pending
  D3B64: STZ $CD23 / LDA $CD24 / LDX $CD25
  D3B6D: JMP CD/3B71
  D3B71: STA $1E01 / LDA #18 / STA $1E00 / TXA / STA $1E02 / JSL C7/0004 / RTS
```

and the direct callers (`python tools/dis.py D0080 0x30`, raw bytes at `0x0D04FD` / `0x0D01C9`):

```
  D0093: LDA $CD00 / TAX / LDA $1D0C,X / TAX     ; pan = a combatant's screen X
  D009B: LDA #95 / JSR CD/3B71                   ; <- the id-95 posts at f485/f748/f1022
  0x0D04FD: A9 08  A2 80 00  20 71 3B            ; LDA #08 / LDX #0080 / JSR $3B71  <- the f244 post
  0x0D01C9: F0 06  A2 80 00  20 71 3B            ;          LDX #0080 / JSR $3B71
```

**The proof that the flush did not run** — `--monitor "7ECD24:2,…" --diff --writer`:

```
  f0      7ECD24=A411*                       (pre-battle leftovers: $CD24=A4, $CD25=11)
  … no row for 7ECD24 anywhere between f0 and f1385 …
  f1385   7ECD24=0101*(C3:07BB)              (post-battle, field code)
```

`$CD24`/`$CD25` hold **A4 / 11 for the entire battle**. If `CD/3B64` had flushed, the posts would carry
`id = A4, pan = 11`. The actual posts carry `id = 95, pan = 78 / 58 / A8` — three *different* pans. So
`CD/3B71` was entered at `D3B71` from a direct `JSR`, never via `D3B6D`.

Consistent with the disassembly: the stagers read the **bank-CD interpreter's** script pointer, not the C1
anim engine's `[$E6]` (`python tools/dis.py D18C0 0x40` / `D1915 0x30`):

```
  D18CF: LDA [$40] / STA $CD24 / LDA $5DA0 / TAX / STX $CD25 / INC $CD23
  D1923: LDA [$40] / STA $CD24 / LDX #0080          / STX $CD25 / INC $CD23
```

⇒ **VERDICT: a battle is driven by the C1 anim engine; the six opcodes post inline through
`JSL $C7:0004`. The `$CD23/$CD24/$CD25` staging slot belongs to the separate bank-CD `[$40]` interpreter
and is inert during a battle.** *(Measured on the forest battle only — other game modes NOT DERIVED.)*

---

## 4. WHICH of the six ops actually fires (and the ⚠ table correction)

### 4.1 Only 0x7C fires in this capture

The three anim-script posts are all `C1:6200`/`C1:620F` = `C1/61EB`(0x7C) or `C1/61F5`(0x7D). They are
distinguishable by the value: `CD/6D4C[01] = 4F` vs `CD/6DAC[01] = 40`; observed **4F**. And
`CD/6D4C[0x11] = 43` vs `CD/6DAC[0x11] = 47`; observed **43**. ⇒ **op 0x7C**, three times.

**0x78 / 0x79 / 0x7A / 0x7B / 0x7D never fired in this capture — disasm-only, unconfirmed.**

Op 0x7C's pan is `LDA $A014 / TAX / LDA $1D0C,X` = **the caster's screen X**, and it matches 3/3:

| post | pan posted | caster `$1D0C[slot]` at that frame (`--monitor "7E1D0C:3,7E1D23:3" --diff`) |
|---|---|---|
| f383 (Crono) | 80 | `f381 7E1D0C=807F8F` → slot0 = **80** ✓ |
| f625 (Marle) | 7F | slot1 = **7F** ✓ |
| f921 (Crono) | A0 | `f912+ 7E1D0C=A07F8F` → slot0 = **A0** ✓ |

### 4.2 ⚠ The 0x7C is NOT in `CD/5FF0` — the port is parsing the wrong table

Walking every `CD/5FF0[sel]` attack script and every `CD/61F0[sel]` tech script with the ROM-derived
opcode lengths (`battleAnimOpLenRom`'s `A9 len 4C BB 75` scan + battle.c's fallback map) finds
**ZERO occurrences of ops 0x7A-0x7D anywhere**. Crono's `CD/5FF0[0x00] = CD/6E0C` reads:

```
  w1.t0 CD/6E1C: 72 0D | 78 5D | 20 02 | 03 07 | 36 | 24 03 | 06 03 | 50 | 2E | 01
```

— a `78 5D`, which would have posted **id 0x5D**. No `5D` was ever posted.

The oracle says why. `--monitor "7E9884:1,7E9885:2,7E9878:2,7E1E01:1" … --diff --writer`:

```
  f341  7E9884=00(C1:43F0)  7E9885=7088*(C1:4427)  7E9878=8003*(C1:4433)   -> script ptr 0x0380
  f601  7E9884=02*(C1:4383) 7E9885=1089*(C1:4427)  7E9878=1008*(C1:4433)   -> script ptr 0x0810
```

(the monitor prints bytes in address order, so `7E9878=8003` is `$9878=80, $9879=03` ⇒ the word `0x0380`.)

`C1:4433` is the PC after `14430 STA $9878`. `python tools/dis.py 14401 0x60`:

```
  14427: LDA  $9877 / ASL A / TAX
  1442C: LDA  $CD5EC6,X          ; <<<< the PC-physical script table
  14430: STA  $9878
  14433: TAX / TAY
  14438: LDA  $CE0000,X …        ; <<<< header read from BANK CE
```

`CD/5EC6[0x00] = 0x0380`, `CD/5EC6[0x0F] = 0x0810` — matching the measurement exactly. The other route
(`C1/4535 LDA $CD5FF0,X`, the one `ROM_ATK_ANIM_TBL` cites) is fed from `C1/44E0`'s `CD/4F26` stride-6
config and its writer PC `C1:453C` **never appears** in the whole 1450-frame run.

Walking the REAL scripts:

```
=== PC atk sel 00 via CD/5EC6  0E/0380  w1=9080(3 tracks) w2=9000(2) ===
  w1.t0 0E/038E: 1E 0A | 36 | 7C <<<SFX>>> | 03 31 | 36 | 1E 2A | 23 01 | 24 03 | 2E | 01
  w1.t1 0E/039E: 24 01 | 20 0F | 1E 00 | 35 | 00
  w1.t2 0E/03A6: 60 00 | 73 03 | 24 01 | 72 00 | 1B 00 | 20 0D | 03 00 | 36 | 00
  w2.t0 0E/03B6: 1E 0A | 7A 02 00 <<<SFX>>> | 03 31 | 36 | 1E 2A | 24 02 | 2E | 01
=== PC atk sel 0F via CD/5EC6  0E/0810 ===
  w1.t0 0E/0820: 7C <<<SFX>>> | …
```

**There it is.** And the sub-scripts (`python -` walk of `CE/0000[ss*2]`):

```
CE super 0A -> CE/D4C3: 07 | 02 06 | 18 03 | 72 0D | 1F        <- `18 03` = WALK-TO-TARGET (C1/542D)
CE super 2A -> CE/D776: 76 03 19 | 08 | A8 01 | 26 0E 02 | …   <- the strike/knock choreography
CE super 00 -> CE/D428: 72 0A | 69 00 | 20 02 | 06 05 | 20 02 | 6A | 03 05 | 06 03 | 50 | 1F
```

`CE/D428`'s `69 00 … 6A … 50` is precisely the block `src/battle.c:2270` already cites for
`CT_PC_ATK_FLASH` — so the CE table is the one the engine's own pins were derived against, while
`battleAnimParse` reads CD/5FF0. **This is the reason the parser would never see a sound opcode.**

*Scope of this claim:* measured for the **PC physical attack** only, in the forest battle. Whether a TECH
or an ENEMY action goes through `CD/5FF0` (the `C1/44E0`/`CD/4F26` config) is **NOT DERIVED** — no tech was
dispatched in this capture.

---

## 5. The FRAME TIMING of the 0x7C post — measured, and it IS anchorable

`--monitor "7EA013:1,7E9877:1,7E1E01:1,7EAE91:1" … --diff --writer`:

```
  f341    7EA013=01*(C1:40A9)  7E9877=00(C1:43FF)   ; attack #1 opens (Crono, sel 00)
  f383    7E1E01=4F*(C1:6200)                       ; the 0x7C post
  f483    7EA013=00*(C1:41BD)                       ; span end
  f601    7EA013=01*(C1:40A9)  7E9877=0F*(C1:4392)  ; attack #2 (Marle, sel 0F)
  f625    7E1E01=43*(C1:6200)
  f746    7EA013=00*(C1:41BD)
  f887    7EA013=01*(C1:40A9)  7E9877=00*(C1:43FF)  ; attack #3 (Crono, sel 00)
  f921    7E1E01=4F*(C1:6200)
  f1020   7EA013=00*(C1:41BD)
```

and the caster's motion, `--monitor "7E1D0C:3,7E1D23:3" … --diff` (rows filtered to the windows):

```
  attack #1: $1D0C[0] steps 9E->80, first change f366, LAST change f381
  attack #3: $1D23[0] steps 47->57, first change f912, LAST change f919
  attack #2 (Marle, ranged): NO position change in f590-660
```

| | dispatch (`$A013<-1`) | first motion | arrival (last motion) | 0x7C post | span end |
|---|---|---|---|---|---|
| #1 Crono | 341 | 366 (+25) | **381** | **383 = arrival + 2** | 483 (arrival+102) |
| #2 Marle | 601 | — (no walk) | — | **625 = dispatch + 24** | 746 |
| #3 Crono | 887 | 912 (+25) | **919** | **921 = arrival + 2** | 1020 (arrival+101) |

This is *mechanistically* what the script says: `w1.t0 = 1E 0A | 36 | 7C …`, and `1E 0A` is the sub-script
whose body is `07 | 02 06 | 18 03 | 72 0D | 1F` — the **walk-to-target**. So the `7C` is the first thing
after the walk returns (`1F`) and one sync (`36`) — **+2 frames**, twice, independently, on two different
axes. Marle's `w1.t0` starts *with* the `7C` (no `1E 0A`), and her offset (24) equals the Crono pre-roll
(25) to within a frame — i.e. the fixed dispatch→first-op latency.

The engine ALREADY owns the arrival: `h->pcAtkArrRel` (`src/battle.h:222`, set at `src/battle.c:2756`),
and `CT_PC_ATK_SPANEND 102` (`src/battle.c:2277`) reproduces attack #1's span end exactly
(381 + 102 = 483 ✓) and attack #3's to within one frame (919 + 102 = 1021 vs 1020).

**What is NOT derived:** the per-op frame cost of `1F` (return) and `36` (sync) individually — the "+2" is
measured as a whole, on 2 samples, both Crono melee. The 24/25-frame dispatch pre-roll is likewise measured,
not derived from a ROM counter.

---

## 6. THE PORT SITE — `src/battle.c`

**There is no live per-frame anim-script opcode execution point in our engine.** Verified by reading the
file:

- `battleAnimParse` — **`src/battle.c:3267`**. A one-shot STATIC pre-pass: `for (int t = 0; t < ntracks; t++)`
  … `for (int guard = 0; guard < 128 …)`, walking the whole script in one call and collecting only
  `animHasStrike` / `animHasShowDmg` / `animPauseSum` / `animPlayAnims` / `animKnockDist` / `animScriptPtr`.
  It has **no** instruction pointer, no resume state, and is called once per dispatch from
  `battleActionSpan` (`src/battle.c:3503`).
- `h->animScriptPtr` is **never read again** after the parse — `grep -n "animScriptPtr" src/battle.c`
  returns only lines 1405 (a trace print), 3270, 3275, 3420, 3434, 3505 (all resets/sets).
- The per-render-frame action driver is **`battleAdvanceAction`** — **`src/battle.c:4181`**, called from
  **`CtBattleTick` at `src/battle.c:4888`**. It computes `int rel = h->frames - h->actStart;`
  (`src/battle.c:4191`) and fires every event at a **pinned offset**: `strikeRel`, `animRel`,
  `spanRel = h->actSpan` — with the PC-physical variants arrival-anchored at `src/battle.c:4246-4250`
  (`strikeRel = h->pcAtkArrRel + CT_PC_ATK_FLASH`).
- **`src/battle.c` and `src/battle.h` contain no sound plumbing at all.**
  `grep -n -i "sound\|sfx\|audio\|1E00\|1E01" src/battle.c src/battle.h` returns 3 hits, all comments
  (battle.c:2138 an unrelated `1E01D` address, battle.c:3259 "facing/sound/palette … passthrough",
  battle.h:66 "every other op (sfx, …)").
- The field VM already has the mechanism: `CtSoundSink` (`src/ct_vm.h:179`), `CtSceneSetSoundSink`
  (`src/ct_vm.h:180`), and `vmSoundPostMem` (`src/ct_vm.c:4649`) = "the `JSL $C7:0004` the six sound ops end
  with". Nothing installs it for battle.

⇒ **The engine only pre-parses.** A byte-exact per-opcode emission is impossible without a live
interpreter. What IS available today, and what the measurement above supports:

- op **0x7C** on a PC physical attack: the id is fully computable (`CD/6D4C[$1C48[5*slot]]`), the pan is
  fully computable (the caster's `$1D0C`, which battle.c already tracks as `mvLiveX`/the screen-X chain),
  and the frame is `h->pcAtkArrRel + 2` — every term already exists in `CtBattleHost`.
- ops **0x78/0x79/0x7A/0x7B/0x7D**: their operands come from the script byte stream at an *instruction
  offset*, which the static parser does not retain, and their frame position is whatever the interpreter's
  cursor reaches — **NOT derivable from what the engine has**. What would be needed: either (a) a live
  per-track interpreter (IP + per-track pause/wait counters) in `battleAdvanceAction`, or (b) a per-op
  frame-offset measurement per script, which the project has repeatedly refused as a pin family.

---

## 7. Open / NOT DERIVED (loudly)

1. **`$AE91` = battle SLOT vs CHARACTER id** — the 2-PC forest party makes them identical. The structural
   argument (the `$1C48` filler runs slots 0..2) favours SLOT. Needs a 3-PC capture with a character whose
   id ≠ its slot.
2. **`CD/6DAC` (op 0x7D)** — never exercised. disasm-only.
3. **ops 0x78/0x79/0x7A/0x7B** — never fired in this capture. The 0x7A path IS reachable
   (`CE/03B6: 1E 0A | 7A 02 00`, the `$A0B3 != 0` variant of Crono's own melee), so a capture that forces
   `$A0B3 != 0` would grade it. I did not measure `$A0B3`.
4. **`id FF` (`C1/40D3`)** and **`id 42`/`id 00` (`C1/1B43` / `C1/1B55`)** — still unexplained *semantically*;
   their SITES are now named (`C1/1B41 LDA #42 / STA $1E01 / LDA #19` and `C1/1B55 STZ $1E01 / LDA #19`,
   both cmd `$19`, both pan `#80`). They are NOT anim-script opcodes.
5. **Whether a TECH or an ENEMY action resolves through `CD/5FF0` or `CD/5EC6`** — the forest capture
   dispatched only PC physicals. `ROM_ATK_ANIM_TBL 0x0D5FF0` may still be right for some path; it is
   demonstrably wrong for the PC physical.
6. **The `+2` (arrival → 0x7C) and the 24/25-frame dispatch pre-roll** are measured on 3 actions in one
   battle, not derived from a ROM counter.
7. **No fix was implemented and nothing was built** — this run was derivation-only.
