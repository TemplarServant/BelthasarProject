# `C2/8F55` — what a FIELD TECH (and a field ITEM) actually DOES

> Derived 2026-08-30. Fills hole (3) of `disasm/notes/menu2_tech_screen.md` §6 — *"`C2/8F55` (the actual
> tech EFFECT) is not decoded here … whether `C2/8F55` is the same routine [as the Item screen's] was not
> checked."* Every claim below carries a ROM address disassembled in this pass (`python tools/dis.py
> <hexoff> <len> [m] [x]`, or a raw byte dump) **or** an oracle command run with
> `scratchpad/menu/ct_probe.exe`, pasted verbatim in §4. Scratch scripts written for this pass:
> `scratchpad/menu/tfx_rom.py` (ROM hexdump), `scratchpad/menu/tfx_splice.py` (WRAM state splicer),
> `scratchpad/menu/tfx_call.py` (splice + `--callrom` + write-log parser), `scratchpad/menu/tfx_sheet.py`.
> States spliced in this pass: `scratchpad/menu/tfx_pre.state`, `tfx_lapis.state`, `tfx_elix.state`,
> `tfx_nomp.state`, `tfx_full.state`, `tfx_dead.state`, `_tfx_mag.state`, `_tfx_big.state`, `_tfx_mp1.state`.

---

## 1. VERDICT

**`C2/8F55` is ONE routine shared by the field TECH cast and the field ITEM use — the Item screen calls it
at `C2/B1BF`, the Tech screen at `C2/B4FC`.** It is a pure "restore HP/MP to a set of party members and
float the numbers" machine, driven entirely by five WRAM bytes the *caller* sets: `$0DBD` (what/whom),
`$0DBE` (which member), `$0DBF` + `$0DC0` (how much), and `$73`/`$0D5F` (the party).

* `C2/8F55` = `JSR C2/8E2D` (**compute + apply**) · `MVN` 8 bytes `$FF:CB9C → $7E:9588` (the popup STYLE
  block; the status-cure sibling `C2/B2A7` MVNs `$FF:CBAC` to the same place) · `JSR C2/8F6C` (**present**:
  BCD-ise, spawn the popup task in slot 7, and BLOCK until it dies).
* **THE AMOUNT (`C2/8ECC`) has two arms, and the `$0DBF` bit7 is what selects them — that is the ONLY
  place where a tech and an item diverge inside the shared code:**
  * bit7 **set** (items: `$0DBF` = the constant `$B2` for an HP item, `$8A` for an MP item, `C2/B17F`) →
    **flat**: `amount = ($0DBF & $7F) * ($0DC0 & $0F)`, the same value for all six slots. Tonic = `$32*1` = 50.
  * bit7 **clear** (techs: `$0DBF` = the caster's sheet `+$39` = his **derived MAGIC** stat) →
    **magic-scaled with a per-target RNG spread**, with the six rolls consumed in REVERSE:
    `amount[k] = ((($0DBF + 1) * rnd[5 - k]) >> 8) + $0DBF * ($0DC0 & $1F)` for `k = 0..5`
    (`k` 0/1/2 = HP for party index 0/1/2 → `$0022/$0024/$0026`; `k` 3/4/5 = MP → `$0028/$002A/$002C`),
    `rnd[i] = ROM[$C0:FE00 + $7E:0D00 + i]` — a 256-byte permutation read with **no `& $FF` wrap** and a
    cursor `$0D00` that the menu advances **one per frame**.
  Aura on a magic-8 Marle: base `8*5 = 40`, spread `+0..+8` → **40..48**; the 44 in the brief is
  `((9 * $80) >> 8) + 40` with cursor `$24`. Proven over 26 `--callrom C2/8ECC` cases (§4.2) and over a
  caster-MAGIC sweep 3/8/16/32/64/99 (§4.4), including the derive step's **clamp of MAGIC to 99**.
* **THE APPLY (`C2/8E82` HP, `C2/8EAB` MP)** — the two `BIT $0DBF` tests inside them are **16-bit** reads,
  so they actually test **`$0DC0`**: bit6 of `$0DC0` = **FULL restore** (amount := the max), bit7 of `$0DC0`
  (tested at `C2/8F41`, also 16-bit) = **negate** = the damage arm. HP is clamped to 999 *before* it is
  displayed and applied; **MP is not** (`C2/8EAB` has no `CMP #03E7`). Pools: `+3/+4` curHP, `+$3F/$40`
  maxHP, `+7/+8` curMP, **`+9/+$0A` maxMP**.
* **TARGETING** — `$0DBD` bit7 = touch HP, bit6 = touch MP, **bit3 = ALL PARTY**. Single target →
  `$71 = $0DBE`, count 1; all-party → `$71 = 0`, count `$73`. Measured end to end with a spliced Lapis
  (`$0DBD = $89`) and Elixir (`$0DBD = $CA`, `$0DC0 = $40`): three members healed in one press, three
  numbers, two chimes (§4.5/§4.6).
* **AURA WHIRL CANNOT BE CAST IN THE FIELD.** `C2/B401 LDA $7700,X / BMI` requires `$7E:7700+techId`
  bit7, and `FF/F82E` sets that bit on **exactly nine ids — `09 0C 0F 1A 1D 21 24 27 29`, all SINGLE
  techs**. `$7E:7739` (Aura Whirl) is 0 and A on it **buzzes** (measured, §4.7). Its record
  `$CC:23EC` is `00 00 …` anyway → `$0DBD = $08` (neither HP nor MP). The all-party arm is real and
  reachable, but only through techs `$1D` and `$24` (`$0DBD = $88`) and through all-party items.
* **REFUSALS** (all `C2/EACC`, sound id `$01`): reserve caster (`$78` < 0), not-field-usable
  (`$7700+id` bit7), **not enough MP** (`$9A97 < $0F4F`, 8-bit), and — in the overlay — **`$0DBC == 0`**
  (nothing left to restore on any candidate). A **dead** (HP 0) target is **VALID** and is healed to
  `amount`: neither `C2/B870` nor `C2/8E82` has a death test (measured, §4.8).
* **THE CONTRACT**: A → `$19D1 = $90`, `$19E1 = $C2:B519[$19C0]` (menu **sprite record 7**, `D = $19C0`)
  → `JSR C2/822B` with A=10 = **sleep exactly 10 frames** → `JSR C2/8F55`: apply + chime `cmd $19 id $06`
  (HP) / `$07` (MP) via `C2/EAF9` (never throttled) + a **41-frame** popup per pool, blocking on
  `$E8` (task slot 7's state word) → `STZ $19D1` → `sheet[+7] -= $0F4F` (**8-bit**) → `JMP C2/BE79`
  (redraw). The screen stays in **substate 4**; if the MP now falls below the cost, `C2/B839 STZ $0DBC`
  makes every further A buzz. Measured f339 → f349 → f391 (§4.3).

**What the C engine must do:** add `CtMenuTechUse(mem, casterSlot, techId, targetSlot, rngCursor)` that
computes the magic/RNG amount and then calls the **same** per-target applier the item path already uses —
and fix that applier: `mItemApplyOne` reads the effective max MP from **`+0x41`, which is wrong; the ROM
reads `+0x09`** (§5.3). Also gate the tech cast on `$7E:7700+techId` bit7 (nine ids), price it from
`CtMenuTechMp`, and refuse on `$0DBC == 0`.

---

## 2. ROM TRACE

### 2.1 `C2/8F55` — the whole routine (`python tools/dis.py 28F55 0x120`)

```
28F55: PHP / REP #30
28F58: JSR C2/8E2D            ; *** COMPUTE THE AMOUNTS AND APPLY THEM (§2.2) ***
28F5B: LDX #CB9C / LDY #9588 / LDA #0007 / MVN $7E,$FF
                              ; 8 bytes $FF:CB9C -> $7E:9588 = the POPUP STYLE BLOCK
                              ;   $FF:CB9C = 00 00 81 04 44 22 E8 33   (the heal/damage number style)
                              ;   $FF:CBAC = 00 00 80 10 40 4A E0 7F   (used by the sibling C2/B2A7)
28F67: JSR C2/8F6C            ; *** PRESENT (§2.5) ***
28F6A: PLP / RTS
```
The `MVN` is `54 7E FF` = `MVN dst=$7E, src=$FF`, X = source, Y = dest, A = len-1 → 8 bytes.

### 2.2 `C2/8E2D` — clear the number slots, compute, then loop over the targets

```
28E2D: PHP / REP #30                       ; A and X/Y 16-bit
28E30: LDA #FFFF / STA $0F63
28E36: LDX #0F63 / LDY #0F65 / LDA #0009 / MVN $7E,$7E
                                           ; -> $0F63..$0F6E = FFFF FFFF FFFF FFFF FFFF FFFF
                                           ;    ($0F63/65/67 = the 3 HP numbers, $0F69/6B/6D = the 3 MP)
28E42: SEP #20                             ; A 8-bit, X/Y STAY 16-bit
28E44: JSR C2/8ECC                         ; *** THE AMOUNT (§2.3) -> $0022..$002D, six words ***
28E47: LDA $0DBE / STA $71                 ; $71 = the chosen party index
28E4C: LDA #01 / STA $00                   ; count = 1
28E50: LDA $0DBD / BIT #08 / BEQ ->28E5D
28E57:   LDA $73 / STA $00 / STZ $71       ; ** bit3 = ALL PARTY: count = $73, start at 0 **
28E5D: - JSR C2/8820                       ; $6F = $2600 + $0D5F[$71]*$50  (and $78 = FF if $71 >= $73)
28E60:   LDA $71 / ASL A / TAX             ; X = index*2
28E64:   LDY $6F                           ; Y = the char sheet pointer
28E66:   LDA $0DBD / BIT #80 / BEQ ->28E70
28E6D:     JSR C2/8E82                     ; HP arm
28E70:   LDA $0DBD / BIT #40 / BEQ ->28E7A
28E77:     JSR C2/8EAB                     ; MP arm
28E7A:   INC $71 / DEC $00 / BNE ->28E5D
28E80: PLP / RTS
```

### 2.3 `C2/8ECC` — **THE AMOUNT** (six words into `$0022..$002D`)

```
28ECC: PHB / PHP / SEP #30 / PEA $0000 / PLB / PLB     ; A,X,Y 8-bit, DB = 0
28ED5: LDA $0DBF
28ED8: BPL ->28EF9                                     ; ** bit7 CLEAR -> the VARIABLE arm **
;  ---- FLAT arm ($0DBF bit7 SET: every ITEM) ----
28EDA: AND #7F / STA $4202                             ; multiplicand = $0DBF & $7F
28EDF: LDA $0DC0 / AND #0F / STA $4203                 ; ** &$0F here (four bits) **
28EE7: XBA / REP #20 / LDY #0A
28EEC: - LDA $4216 / STA $0022,Y / DEY / DEY / BPL -   ; the SAME product in all six words
28EF6: JMP C2/8F52                                     ; -> PLP / PLB / RTS
;  ---- VARIABLE arm ($0DBF bit7 CLEAR: every TECH) ----
28EF9: STA $4202                                       ; multiplicand A = $0DBF (the caster's MAGIC)
28EFC: LDA $0DC0 / AND #1F / STA $4203                 ; ** &$1F here (FIVE bits) **
28F04: LDX $0D00                                       ; X (8-bit) = the menu RNG cursor
28F07: REP #20
28F09: LDA $C0FE00,X / STA $02                         ; six bytes rnd[0..5] into $02..$07
28F0F: LDA $C0FE02,X / STA $04                         ;   *** NO &$FF WRAP: at cursor $FB..$FF the
28F15: LDA $C0FE04,X / STA $06                         ;       third word reads past $C0:FEFF into
28F1B: LDA $4216 / STA $08                             ;       $C0:FF00 (measured, §4.2) ***
                                                       ; $08 = base = $0DBF * ($0DC0 & $1F), 16-bit
28F20: SEP #20 / LDX #05 / LDY #00
28F26: LDA $0DBF / INC A / STA $4202                   ; multiplicand = ($0DBF + 1) & $FF
28F2D: - SEP #20 / LDA $02,X / STA $4203               ; X = 5,4,3,2,1,0 -> bytes $07,$06,…,$02
28F34:   NOP / NOP / REP #20
28F38:   LDA $4217 / AND #00FF                         ; RDMPYH = ((mag+1)*rnd) >> 8
28F3E:   CLC / ADC $08
28F41:   BIT $0DBF / BPL ->28F4A                       ; ** 16-BIT read: N = bit7 of $0DC0 **
28F46:     EOR #FFFF / INC A                           ;    -> NEGATE (the DAMAGE arm)
28F4A:   STA $0022,Y / INY / INY / DEX / BPL ->28F2D
28F52: PLP / PLB / RTS
```
The store order is **reversed**: `$0022 = f(rnd[5])`, `$0024 = f(rnd[4])`, `$0026 = f(rnd[3])`,
`$0028 = f(rnd[2])`, `$002A = f(rnd[1])`, `$002C = f(rnd[0])`. `$0022/24/26` are the three **HP** amounts
(target index 0/1/2), `$0028/2A/2C` the three **MP** amounts.

`$C0:FE00` is a **256-byte permutation of 0..255** (`sorted(t) == list(range(256))`, checked). `$7E:0D00`
is the MENU's RNG cursor — bank C2 touches it at `C2/805B` (`LDA $0D00` → `$29AD`, the exit hand-off
already recorded in `battle_facing_engine.md`), `C2/8F04` (here) and `C2/B116` (the random-item roll
`C2/B10D`, `(100*rnd)>>8` vs `$C2:B13D = 05 14 29 64`). It increments **once per frame** while the menu
runs (measured, §4.1).

### 2.4 `C2/8E82` (HP) and `C2/8EAB` (MP) — **THE APPLY**

Raw `d[0x28E85:0x28EAC]` = `[25 ROM bytes at 0x28E85: python tools/dis.py 0x28E85 25]
D9 3F 00 90 03 B9 3F 00 99 03 00 28 60` — note `B5 22` = `LDA $22,X` is **direct page**, and `2C BF 0D`
= `BIT $0DBF` is **absolute with A 16-bit**, i.e. it reads the WORD `$0DBF|$0DC0<<8`.

```
28E82: PHP / REP #30
28E85: LDA $22,X                     ; dp,X  -> the target's HP amount
28E87: BIT $0DBF                     ; ** 16-BIT: V = bit6 of $0DC0, N = bit7 of $0DC0 **
28E8A: BVC ->28E8F
28E8C:   LDA $003F,Y                 ;   ** $0DC0 bit6 = FULL: amount := maxHP **
28E8F: CMP #03E7 / BCC + / LDA #03E7 ; clamp the amount to 999
28E97: STA $0F63,X                   ; the number to display for this target
28E9A: CLC / ADC $0003,Y             ; + current HP
28E9E: CMP $003F,Y / BCC + / LDA $003F,Y   ; clamp to max HP
28EA6: STA $0003,Y
28EA9: PLP / RTS

28EAB: PHP / REP #30
28EAE: LDA $28,X                     ; the target's MP amount
28EB0: BIT $0DBF / BVC ->28EB8
28EB5:   LDA $0009,Y                 ;   FULL: amount := maxMP
28EB8: STA $0F69,X                   ; ** NO 999 CLAMP on the MP arm **
28EBB: CLC / ADC $0007,Y
28EBF: CMP $0009,Y / BCC + / LDA $0009,Y
28EC7: STA $0007,Y
28ECA: PLP / RTS
```
**Sheet offsets, from these two routines alone**: `+$03/$04` current HP · `+$3F/$40` effective max HP ·
`+$07/$08` current MP · **`+$09/$0A` effective max MP**. (`$7E:2603` = Crono's HP and `$7E:2657` =
Marle's MP in the brief are `$2600+0*$50+3` and `$2600+1*$50+7` — consistent.)
There is **no death test and no "already full" test** in either routine.

### 2.5 `C2/8F6C` — the presenter (BCD, popup spawn, and the BLOCK)

```
28F6C: PHP / SEP #20
28F6F: JSL FF/F7D9                   ; build the NUMBER GLYPHS. Ten passes: MVN 16 bytes
                                     ;   $FF:9390.. -> $7E:8E80 + $20*n, then eight
                                     ;   `LDA b[Y] / ORA b[Y+1] / STA b[Y+$10]` (Y += 2) fold the pair
                                     ;   into the block's second half. X runs $9390..$942F (CPX #9430),
                                     ;   so the result is $7E:8E80..$8FBF = 320 bytes.
28F73: LDX #FC5A / JSR C2/8385       ; push the 8-byte record $C2:FC5A = `40 57 | 80 8E 7E | 40 01 | 00`
                                     ;   into the menu's transfer queue at $7E:0C00 + $0D11, then
                                     ;   $0D11 += 8 (C2/83A2: `LDA $0D11 / AND #00FF / ADC #0C00 / TAY`,
                                     ;   `MVN $7E,$C2`, A = 7). The record carries the address
                                     ;   `$7E:8E80` FF/F7D9 just filled and the length `$0140` = the 320
                                     ;   bytes it wrote; `$5740` is presumably the VRAM destination
                                     ;   (NOT verified -- the queue's consumer was not decoded)
28F79: LDA $0DBE / STA $71 / INC $0D15
28F81: REP #30
28F83: STZ $0F53                     ; popup COUNT
28F86: STZ $0F5D                     ; b0 = an HP number exists, b1 = an MP number exists
28F89: STZ $71 / STZ $00             ; $71 = member walk, $00 = the COMPACTED popup slot cursor
28F8D: - STZ $04
28F8F:   JSR C2/8FCB                 ; HP: if $0F63+2*$71 >= 0 -> BCD it into $0F63+$00, $0F5D |= 1, INC $04
28F92:   JSR C2/8FEF                 ; MP: same for $0F69, $0F5D |= 2
28F95:   LDA $04 / BEQ ->28FB1
28F99:     JSR C2/8820 / LDA $9A90 / JSR C2/F626 / LDA $180E,Y / STA $0F57,X
                                     ; the popup ANCHOR (screen pos) for this member -> $0F57+$00
28FAA:     INC $0F53 / INC $00 / INC $00
28FB1: - INC $71 / LDA $71 / CMP $73 / BCC ->28F8D
28FB9: LDA #9016 / LDX #0038 / JSR C2/8249       ; spawn C2/9016 in TASK SLOT 7 ($B0+$38 = $E8)
28FC2: PLP
28FC3: - JSR C2/821E / LDA $E8 / BNE -           ; ** BLOCK until slot 7 dies **
28FCA: RTS
```
`C2/8FCB` / `C2/8FEF`:
```
28FCB: LDA $71 / ASL A / ADC #0F63 / TAY / LDA $0000,Y / BMI ->28FEE   ; FFFF = no number
28FD7: LDA #0002 / STA $92 / JSR C2/F13F        ; ** 2 source bytes ** -> binary to BCD in $8A/$8C
28FDF: LDX $00 / LDA $8A / STA $0F63,X          ; ** stored at the COMPACTED slot $00, not at $71*2 **
28FE6: LDA #0001 / STA $0F5D / INC $04 / RTS

28FEF: LDA $71 / ASL A / ADC #0F69 / TAY / LDA $0000,Y / BMI ->29015
28FFB: LDA #0001 / STA $92 / JSR C2/F13F        ; ** ONE source byte: an MP number > 255 prints wrong **
29003: LDX $00 / LDA $8A / STA $0F69,X
2900A: LDA $0F5D / ORA #0002 / STA $0F5D / INC $04 / RTS
```
`C2/F13F` is a binary→BCD converter: it walks `$92` source bytes bit by bit and `SED`-adds the per-bit BCD
weights from `$C2:F17A` (stride 4) into `$8A/$8C`. So **`$0F63[]` is used twice**: first by `C2/8E2D` as
the *raw* per-target amounts (indexed by target), then **compacted in place** by `C2/8FCB` as *BCD*
display values indexed by popup slot. Measured directly in §4.9 (`$0F65` = `0030` raw 48 while `$0F63` =
`0048` BCD 48).

`$E8` is not a magic byte: `C2/8249` was called with `X = #0038` and the menu's task state table is
`$B0 + slot*8`, so slot 7's state word is `$B0+$38 = $E8`. `C2/8216` (`LDX $6E / STZ $B0,X`) is the task
exit. Measured: `$E8` = `0201` for the whole popup and `0000` on the frame the task ends (§4.3).

### 2.6 `C2/9016` — the popup TASK, and the CHIME

```
29016: SEP #30
29018: LSR $0F5D / BCC ->2902C
2901D:   STZ $0F60 / LDA #03 / STA $0F61 / STZ $16 / LDA #06 / JSR C2/9047   ; HP: sound id $06
2902C: LSR $0F5D / BCC ->29044
29031:   LDA #2A / STA $0F60 / LDA #01 / STA $0F61 / LDA #06 / STA $16
2903B:   LDA #07 / JSR C2/9047                                              ; MP: sound id $07
29044: JMP C2/8216                                                          ; kill slot 7 -> $E8 = 0

29047: JSR C2/EAF9                 ; *** POST THE CHIME (A = 06 or 07) ***
2904A: BRA ->2904F
2904C: - JSR C2/821E               ; yield one frame
2904F: SEP #30 / STZ $0908 / STZ $0909 / STZ $090A
2905A:   LDX $0F60 / INC $0F60
29060:   LDA $FFCEBA,X / BPL ->29067 / RTS      ; a NEGATIVE entry ends the animation
29067:   STA $04 / STZ $05 / REP #20
2906D:   LDA $0F53 / STA $22                    ; ** this is what clobbers $0022 after the cast **
29072:   STZ $19 / LDX $16 / LDY $51
29078: -- LDA $0F63,X / STA $8A / BMI ->29087
2907F:      LDA $0F61 / STA $00 / JSR C2/9093   ; emit the digits into the OAM shadow $0780,Y
29087: -- INC $19 / INC $19 / INX / INX / DEC $22 / BNE ->29078
29091:   BRA ->2904C
```
`$FF:CEBA` is the per-frame Y offset of the floating number:
`[25 ROM bytes at 0x3FCEBA: python tools/dis.py 0x3FCEBA 25]×17 | FF` — **41 positive
entries then the `FF` terminator at +$29**, and the MP arm at +$2A is another **41** entries then `FF` at
+$53. That is exactly the 41-frame gap measured between the HP chime and the MP chime (§4.6).
`$0F60` = the animation cursor, `$0F61` = 3 (HP) / 1 (MP) — a colour, `$16` = 0 (read `$0F63`) / 6 (read
`$0F69`).

`C2/9093` writes one OAM entry per digit: `tile = digit + $74`, `attr = $31`, `X = $0F58,X + $01`,
`Y = $0F57,X − $04`. Measured live: OAM entries `$22/$23` become `dc3a7831` / `e43a7831` during the
Aura popup — tile `$78` = digit 4 + `$74`, attr `$31` (§4.10). ✔

`C2/EAF9` is the sound stager's **cmd-`$19` entry**:
```
2EAF9: LDX #19 / STX $1E00 / STZ $0D76 / BRA ->2EB08     ; ** STZ $0D76 first => never throttled **
2EB03: LDX #18 / STX $1E00                                ; (C2/EAC2 blip / C2/EACC buzz land here)
2EB08: STA $1E01 / LDA $0D76 / BNE ->2EB1E
2EB10: LDA #03 / STA $0D76 / LDA #80 / STA $1E02 / JSL C7/0004
```
So **yes, the chime goes through the same stager as the Item screen's** — `C2/9047 → C2/EAF9`, the single
`JSR` site of `C2/EAF9` in the ROM (`menu_sfx.md` §"the census"), posting **cmd `$19`, id `$06`/`$07`**,
with `$0D76` zeroed first so the 3-frame menu throttle can never swallow it. (`C2/EAD6`, which
`menu2_item_rest.md` AV.5 measured as posting `$18`, is **dead code — 0 callers**; it is not this path.)

### 2.7 The two callers, and where they diverge

**TECH — `C2/B4DE`** (raw `[25 ROM bytes at 0x2B4DE: python tools/dis.py 0x2B4DE 25]
[34 ROM bytes at 0x2B4F7: python tools/dis.py 0x2B4F7 34]`):
```
2B4DE: LDA $0DBC / BNE ->2B4E6 / JMP C2/EACC        ; ** REFUSAL: nothing restorable **
2B4E6: LDA #90 / STA $19D1
2B4EB: TDC / LDA $19C0 / TAX / LDA $C2B519,X (BF = long,X) / STA $19E1   ; $C2:B519 = 03 05 04 00 03
2B4F7: LDA #0A
2B4F9: JSR C2/822B                                  ; ** SLEEP EXACTLY 10 FRAMES **
2B4FC: JSR C2/8F55                                  ; ** THE EFFECT **
2B4FF: STZ $19D1
2B502: LDA $0F4D / STA $71 / JSR C2/8820 / LDX $6F
2B50C: LDA $0007,X / SEC / SBC $0F4F / STA $0007,X  ; ** MP -= cost, 8-BIT, abs,X with DB=$7E **
2B516: JMP C2/BE79                                  ; redraw the panels; substate ($68) UNCHANGED
```
**ITEM — `C2/B1BF`** (raw `20 55 8F 20 1C B3 80 DE …`):
```
2B1BF: JSR C2/8F55        ; ** THE SAME ROUTINE **
2B1C2: JSR C2/B31C        ; consume one unit ($04CB=1 -> C2/87D5), refresh the row/description;
                          ;   when the count hits 0 it also does STZ $0DBD (C2/B341)
2B1C5: BRA ->2B1A5        ; back to the overlay loop
```
So the divergence is **exactly four things**, all *outside* `C2/8F55`:

| | ITEM (`C2/B0AB` + `C2/B17F`) | TECH (`C2/B3E6`) |
|---|---|---|
| `$0DBD` | `((\$CC:1BB5[id]+1)&3)<<2` ∣ `$CC[rec+1]` | `$CC:2141[id*12] & $C0` ∣ `((\$CC:1ACB[id*2]&1)+1)<<2` |
| `$0DC0` | `$CC[rec+2] & $0F`, and **`$0F` → `$40` (FULL)** | `$CC:2140[id*12] & $1F` — **never ≥ $20**, so no field tech is ever FULL and none can negate |
| **`$0DBF`** | **`$B2` (HP item) / `$8A` (MP item)** → bit7 set → **FLAT** | **`$9AC9` = caster sheet `+$39` = MAGIC** → bit7 clear → **MAGIC + RNG** |
| after | consume a unit (`C2/B31C`) | 10-frame pre-roll, `$19D1/$19E1`, `MP -= $0F4F`, `C2/BE79` |

There is a **third** caller of the presenter: `C2/B2A7` (status-cure items) MVNs `$FF:CBAC` → `$7E:9588`,
FF-fills `$0F63…`, writes a literal `1` into the chosen target's slot and calls **`C2/8F6C` directly** —
it never runs `C2/8E2D`, i.e. it shows a number without the heal machine.

### 2.8 The gate ladder (why a cast is refused)

`C2/B3E6` — the A press **in the list**:
```
2B3EB: LDA $78 / BPL + / JMP C2/EACC       ; (1) the caster is a RESERVE member ($78 = FF from C2/8820
                                           ;     when $71 >= $73)
2B3F2: LDA #0C / STA $4202 / TDC / LDA $0F4C / STA $4203 / TAX
2B401: LDA $7700,X / BMI + / JMP C2/EACC   ; (2) ** NOT USABLE OUTSIDE BATTLE **
2B409: REP #30 / LDA $4216 / STA $00       ; $00 = techId*12
2B413: LDA $0419 + $041A / STA $0F4D / STA $71 / JSR C2/8820
2B427: JSR C2/BC22 / STA $0F4F             ; the MP cost
2B42D: LDA $9A97 / CMP $0F4F / BCS + / JMP C2/EACC   ; (3) ** NOT ENOUGH MP (8-bit) **
2B43A: JSR C2/EAC2                         ; blip, then build $0DBD/$0DC0/$0DBF and INC $68 -> substate 4
```
The nine field-usable ids come from **`FF/F82E`**, a literal `LDA #80` + nine `TSB`s:
`$7709 $770C $770F $771A $771D $7721 $7724 $7727 $7729` → tech ids
**`09 0C 0F 1A 1D 21 24 27 29`**. A whole-ROM write scan of `$7700..$7780` finds no other writer that
could set bit7 there (`FF/F8B5 STA $7701,X` is the "+1 next tech" marker, and per
`menu2_tech_screen.md` §1 it writes `(prop & $1E) | $40`, i.e. bit7 **clear**). All nine are SINGLE techs;
**no double and no triple is ever field-castable**. Their records:

| id | `$CC:2140+12·id` | `$0DC0` | `$0DBD` | MP `$CC:253B+id` | all-party? |
|---|---|---|---|---|---|
| `09` | `05 80 00 …` | `05` | `84` | 1 | no |
| `0C` | `0E 80 00 …` | `0E` | `84` | 2 | no |
| `0F` | `18 80 00 …` | `18` | `84` | 5 | no |
| `1A` | `0E 80 00 …` | `0E` | `84` | 2 | no |
| `1D` | `0A 80 00 …` | `0A` | **`88`** | 3 | **YES** |
| `21` | `05 80 00 …` | `05` | `84` | 1 | no |
| `24` | `06 80 00 …` | `06` | **`88`** | 2 | **YES** |
| `27` | `18 80 00 …` | `18` | `84` | 5 | no |
| `29` | `4E 80 00 …` | `0E` | `84` | 1 | no |
| `39` (Aura Whirl) | `00 00 3A 3B 05 …` | `00` | `08` | 0 | (unreachable) |

Every one has `rec[1] & $C0 == $80` → **no field tech restores MP**.

The overlay's per-frame body `C2/B48E → C2/B823`:
```
2B823: JSR C2/B83E                         ; cursor clamp + C2/B870 -> $0DBC
2B829: LDA $0F4D / STA $71 / JSR C2/8820
2B831: LDA $9A97 / CMP $0F4F / BCS +
2B839:   STZ $0DBC                         ; ** the caster can no longer pay -> refuse from now on **
```
and `C2/B870` (already transcribed in `menu2_item_rest.md` §2.5) is the shared validity mask:
`$0DBC` bit1 = "this candidate's HP can still rise" (**16-bit** `+3 < +$3F`), bit0 = "its MP can"
(**8-bit** `+7 < +9`); for an all-party effect it scans every member and stops at the first non-zero.
It contains **no death test**.

### 2.9 `$19C0` / `$19D1` / `$19E1` = menu SPRITE RECORD 7

An absolute+long scan of the whole 4 MB finds `$19D1` written only at `C2/B4E8` (`#90`), `C2/B4FF` (`STZ`)
and `C2/B9F5` (`STZ`, the tech screen's one-shot build), and `$19E1` written only at `C2/B4F4` — **and no
absolute reader anywhere**. The reader is direct-page: `C2/F588` does
```
2F588: PHP / PHD / PEA $19C0 / PLD / SEP #30
2F590: LDA #FF / STA $00 / STZ $11 / STZ $18
2F598: REP #21 / TDC / SBC #003F / TCD / CMP #1800 / BCS ->2F58E
```
i.e. it resets **eight 0x40-byte sprite records at `$7E:1800 + $40·k`, k = 0..7**, of which `$19C0` is
record 7 (`C2/F566` does the same for records 0..6 with `$00 = k`, `$11 = 0`, `$18 = $40`). Therefore
`$19C0` = record 7's field **+$00** (its charId), `$19D1` = field **+$11**, `$19E1` = field **+$21**.
`C2/BA09` confirms the meaning: `LDA $9A90 / … / PEA $19C0 / PLD / LDA $9A90 / CMP $00 / BEQ + / STA $00 /
JSR C2/F422` — "make record 7 show character `sheet+0`". So the cast sets record 7's override byte to
`$90` and its pose to `$C2:B519[charId]` (`03 05 04 00 03` = one entry per charId 0..4), and clears the
override when `C2/8F55` returns. **No OAM or framebuffer change attributable to it was observed in the
10-frame window** (§4.11) — see §6.

---

## 3. RAM CONTRACT

| addr | width | what | proof |
|---|---|---|---|
| `$7E:0022`–`$002D` | 6×16 | the computed amounts: `$22/$24/$26` = HP for target 0/1/2, `$28/$2A/$2C` = MP | `C2/8ECC` stores; `C2/8E82` reads `$22,X`, `C2/8EAB` reads `$28,X`. **Clobbered right after by `C2/906D STA $22`** (the popup count) — do not read it back |
| `$7E:0D00` | 8 | the MENU RNG cursor into `$C0:FE00`; **+1 every frame** | `C2/8F04 LDX $0D00`; monitored ticking f0…f489 (§4.1) |
| `$7E:0DBB` | 8 | the target cursor's nav base (`$0C` item / `$08` tech) | `C2/B478`, `C2/B192` |
| `$7E:0DBC` | 8 | target validity mask: b1 HP restorable, b0 MP restorable; **0 = refuse** | `C2/B870`; measured `02` (HP only), `03` (Elixir: HP+MP), `00` (full party) |
| `$7E:0DBD` | 8 | **b7 = touch HP, b6 = touch MP, b3 = ALL PARTY** (b2 unused by this path) | `C2/8E50/8E66/8E70`; measured `84` Aura, `89` Lapis, `CA` Elixir |
| `$7E:0DBE` | 8 | the chosen party index (0..2) | `C2/8E47`, `C2/B87E`; measured `01` when targeting Marle |
| `$7E:0DBF` | 8 | **the amount SOURCE**: b7 set → flat `(v&$7F)·($0DC0&$0F)`; b7 clear → the caster's MAGIC | `C2/8ED5`; item `$B2`/`$8A` (`C2/B17F`), tech `$9AC9` (`C2/B469`) |
| `$7E:0DC0` | 8 | tech: `$CC:2140[id·12] & $1F`; item: nibble, `$0F`→**`$40` = FULL**. **b6 = FULL, b7 = NEGATE**, both read as the high half of the 16-bit `$0DBF` word | `C2/8EDF/8EFC`, `C2/8E87`, `C2/8F41`; swept in §4.2 |
| `$7E:0F4C` | 8 | the highlighted techId | `C2/B3F9` |
| `$7E:0F4D` | 8 | the CASTER's party index (`$0419 + $041A`) | `C2/B41A`, reused for the MP write-back |
| `$7E:0F4F` | 8 | the MP cost (`C2/BC22`) | `C2/B42A`; measured `01` for Aura |
| `$7E:0F53` | 16 | number of popups emitted | `C2/8FAA`; measured `0001` single, `0003` all-party |
| `$7E:0F57`–`$0F5C` | 3×16 | popup screen anchors (`$180E,Y`), compacted | `C2/8FA7`, read by `C2/90D2/90DB` |
| `$7E:0F5D` | 16 | b0 = an HP number exists, b1 = an MP number exists → which chimes fire | `C2/8FE9`, `C2/9010`, `C2/9018` |
| `$7E:0F60` | 8 | popup animation cursor into `$FF:CEBA` (0 = HP arm, `$2A` = MP arm) | `C2/905A`; measured `01`→`$2A` over f349..f390 |
| `$7E:0F61` | 8 | popup colour: 3 = HP, 1 = MP | `C2/9020/9036` |
| `$7E:0F63`–`$0F6E` | 6×16 | **dual use**: first the raw per-target amounts (index = target), then the **compacted BCD** display values (index = popup slot). `FFFF` = "no number" | `C2/8E36` FF-fill, `C2/8E97/8EB8` raw, `C2/8FE3/9005` BCD; measured `0030` raw vs `0048` BCD (§4.9) |
| `$7E:0016` | 8 | `$0F63` vs `$0F69` selector inside the popup task (0 / 6) | `C2/9025/9039`, `C2/9074` |
| `$7E:0071` / `$0073` | 8 | party walk index / **active party count** | `C2/8820`, `C2/8E57` |
| `$7E:0078` | 8 | `$FF` when `$71 >= $73` → "reserve" → the tech list's A buzzes | `C2/8825/882D`, `C2/B3EB` |
| `$7E:006F` | 16 | the live char-sheet pointer `$2600 + $0D5F[$71]·$50` | `C2/8851` |
| `$7E:0D5F+` | 8×n | party slot → charId | `C2/882F` |
| `$7E:9A90`–`$9ADF` | $50 | the **copy** of the pointed sheet (`MVN` in `C2/8854`) | `C2/8857` |
| sheet `+$03/$04` | 16 | current HP | `C2/8E9B/8EA6` |
| sheet `+$07/$08` | 16 | current MP (the cast's write-back touches only `+7`, 8-bit) | `C2/8EBC/8EC7`, `C2/B508` |
| sheet **`+$09/$0A`** | 16 | **effective max MP** | `C2/8EBF/8EC4`, and the `--callrom C2/8EAB` sweep (§4.4b) |
| sheet `+$0E` | 8 | **BASE magic**; the derive step copies it (clamped to 99) into `+$39` | measured by splicing `$265E` and watching `$0DBF` (§4.4) |
| sheet `+$39` | 8 | **derived MAGIC** = `$0DBF` for a tech | `C2/B469` |
| sheet `+$3F/$40` | 16 | effective max HP | `C2/8E8C/8E9E` |
| `$7E:7700+id` | 8 | per-tech property; **b7 = usable outside battle** | `C2/B401`; seeded by `FF/F82E` |
| `$7E:19C0` | 8 | menu sprite record 7's charId (`D = $19C0`) | `C2/F588`, `C2/BA20` |
| `$7E:19D1` / `$19E1` | 8 | record 7's `+$11` override (`$90`) and `+$21` pose (`$C2:B519[charId]`) | `C2/B4E8/B4F4/B4FF/B9F5`; **no reader found** (§6) |
| `$7E:00E8/E9` | 16 | task slot 7's state word (`$B0 + $38`) — the popup's liveness | `C2/8249` with `X=#0038`; measured `0201` → `0000` |
| `$7E:1E00`–`$1E02` | 3 | the sound mailbox: cmd `$19`, id `$06`/`$07`, `$80` | `C2/EAF9/EB08/EB17` |
| `$7E:8E80`–`$8FBF` | 320 | the built number glyphs; the queue record naming them is `$C2:FC5A` | `FF/F7D9`, `C2/8F73` |
| `$7E:9588` | 8 | the popup STYLE block (`$FF:CB9C` heal / `$FF:CBAC` status) | `C2/8F5B`, `C2/B2AA` |
| ROM `$C0:FE00` | 256 | the RNG permutation (verified a true permutation of 0..255) | `C2/8F09` |
| ROM `$C2:B519` | 5 | per-charId cast pose `03 05 04 00 03` | `C2/B4F1` |
| ROM `$FF:CEBA` | 2×41+2 | the popup Y-offset script, `FF`-terminated at +$29 and +$53 | `C2/9060` |

---

## 4. ORACLE MEASUREMENTS

All commands were run from the repo root with `scratchpad/menu/ct_probe.exe` (never `build/ct.exe`).
`SCRIPT` below is the brief's own tech-cast drive script:
```
SCRIPT = 0:20,200:2,0:60,80:2,0:14,80:2,0:30,100:2,0:50,20:2,0:30,100:2,0:60,100:2,0:60,100:2,0:150
```
(X opens the menu at f20, Right×2 to the Tech ribbon, A at f130 enters, Down at f182 picks Marle,
A at f214 → the list, A at f276 → the target overlay, **A at f338 → the cast**.)

### 4.0 The state, decoded

```
ct_probe.exe ROM --read "wram:2600+240" --from saves/tech_double.state
```
```
sheet 0 (7E:2600)  +03 HP=10  +3F maxHP=96  +07 MP=2   +09 maxMP=12  +39=05   (Crono)
sheet 1 (7E:2650)  +03 HP=65  +3F maxHP=76  +07 MP=20  +09 maxMP=20  +39=08   (Marle)
sheet 2 (7E:26A0)  +03 HP=40  +3F maxHP=88  +07 MP=14  +09 maxMP=16  +39=0A   (Lucca)
```
`tfx_splice.py` patches WRAM at file offset **69** in `saves/tech_double.state` (a headerless LakeSnes
stream); `PEEK_SAVE` output carries a 16-byte `CTLS` header whose trailing hash is validated on load, so
the splicer re-emits those files **headerless** (`d[16:]`) — a spliced `CTLS` file is rejected with
`FAIL: cannot load savestate`.

### 4.1 The brief's run, reproduced and extended

```
ct_probe.exe ROM --monitor "7E00BE:1,7E0F4C:1,7E0DBD:1,7E0DBF:1,7E0DC0:1,7E0D00:1,7E0022:12,7E0F63:12,\
7E2603:2,7E2657:2,7E0F53:2,7E0F5D:2,7E19D1:1,7E19E1:1,7E1E00:4" \
  --from saves/tech_double.state --btnscript "$SCRIPT" --diff
```
```
f338  $00BE=04 $0F4C=09 $0DBD=84 $0DBF=08 $0DC0=05 $0D00=19  $0F63=0000…  $2603=0A00 $2657=1400 $19D1=00
f339  …                                            $0D00=1A  …                                  $19D1=90* $19E1=05*
f349  …                                            $0D00=24  $0022=0000 3000 2E00 2C00 2B00 2F00
                                                             $0F63=4400 FFFF FFFF FFFF FFFF FFFF
                                                             $2603=3600*  $0F53=0100*  $1E00=00 06 80 07*
f391  …                                            $0D00=4E                              $2657=1300* $19D1=00*
```
`$0D00` ticks **every single frame** from f48 to f489 in that dump. `$0022` reads back `0000` because
`C2/906D STA $22` / `C2/908D DEC $22` overwrote it — the value it *had* is recovered exactly below.

### 4.2 `C2/8ECC` swept directly — **26/26 byte-exact**

`--callrom` needs the menu's own machine state: `--regdp 0000 --regdb 7E --m16 --x16` (DB must be `$7E`;
with DB = 0 the `$003F,Y` sheet reads would hit bank-0 hardware registers).

```
ct_probe.exe ROM --callrom C2/8ECC --from <spliced tfx_pre.state> --regdp 0000 --regdb 7E --m16 --x16 --writes
```
First run, unpatched (`$0DBF=08 $0DC0=05 $0D00=18`) — the mechanism is visible in the writes themselves:
```
  w 004202 = 08  (by C2:8EFC)      <- multiplicand = $0DBF
  w 004203 = 05  (by C2:8F04)      <- multiplier   = $0DC0 & $1F
  w 000002 = F1 / 000003 = 9A      <- rnd[0..5] = F1 9A 63 75 11 91  = ROM[$C0:FE18..FE1D]
  w 000004 = 63 / 000005 = 75
  w 000006 = 11 / 000007 = 91
  w 000008 = 28 / 000009 = 00      <- base = 8*5 = $0028 = 40
  w 004202 = 09  (by C2:8F2D)      <- ($0DBF + 1)
  w 004203 = 91 (by C2:8F34) -> w 000022 = 2D  (45)
  w 004203 = 11            -> w 000024 = 28  (40)
  w 004203 = 75            -> w 000026 = 2C  (44)
  w 004203 = 63            -> w 000028 = 2B  (43)
  w 004203 = 9A            -> w 00002A = 2D  (45)
  w 004203 = F1            -> w 00002C = 30  (48)
```
The sweep (`scratchpad/menu/_sweep_8ecc.txt`), model
`flat = (dbf&$7F)*(dc0&$0F)` if `dbf&$80` else `((dbf+1)*rnd[5-k])>>8 + dbf*(dc0&$1F)`, negated iff
`dc0&$80`, `rnd[i] = ROM[0xFE00+cur+i]` **without wrap**:
```
OK  DBF=08 DC0=05 D00=18  meas=[45, 40, 44, 43, 45, 48]           pred=[45, 40, 44, 43, 45, 48]
OK  DBF=08 DC0=05 D00=24  meas=[44, 48, 46, 44, 43, 47]           pred=[44, 48, 46, 44, 43, 47]
OK  DBF=10 DC0=05 D00=18  meas=[89, 81, 87, 86, 90, 96]           pred=[89, 81, 87, 86, 90, 96]
OK  DBF=20 DC0=05 D00=18  meas=[178, 162, 175, 172, 179, 191]     pred=…
OK  DBF=03 DC0=05 D00=18  meas=[17, 15, 16, 16, 17, 18]           pred=…
OK  DBF=08 DC0=0E D00=18  meas=[117, 112, 116, 115, 117, 120]     pred=…
OK  DBF=08 DC0=18 D00=18  meas=[197, 192, 196, 195, 197, 200]     pred=…
OK  DBF=63 DC0=1F D00=40  meas=[3070, 3115, 3095, 3100, 3090, 3095] pred=…
OK  DBF=7F DC0=1F D00=77  meas=[3954, 4040, 3990, 4045, 3943, 3945] pred=…
OK  DBF=B2 DC0=01 D00=18  meas=[50, 50, 50, 50, 50, 50]           <- the TONIC, flat 0x32*1
OK  DBF=B2 DC0=0F D00=18  meas=[750, 750, 750, 750, 750, 750]
OK  DBF=B2 DC0=40 D00=18  meas=[0, 0, 0, 0, 0, 0]                 <- FULL: the flat product is 0
OK  DBF=8A DC0=03 D00=18  meas=[30, 30, 30, 30, 30, 30]           <- an MP item, 0x0A*3
OK  DBF=FF DC0=0F D00=18  meas=[1905, …]                          <- 0x7F*0x0F
OK  DBF=00 DC0=05 D00=18  meas=[0, 0, 0, 0, 0, 0]
OK  DBF=08 DC0=40 D00=18  meas=[5, 0, 4, 3, 5, 8]                 <- &$1F makes the base 0; spread only
1 MISMATCH at D00=FC -> the model WRAPPED and the ROM does not.  Re-run with the no-wrap model:
OK  DBF=08 DC0=05 D00=FA/FB/FC/FD/FE/FF   6/6 exact   (the table over-reads into $C0:FF00 = 78 18 …)
```
And the negate arm, isolated (this is what proved `C2/8F41`'s `BIT` is 16-bit, i.e. keyed on `$0DC0` b7):
```
OK  DBF=08 DC0=85 D00=18 meas=[65491, 65496, 65492, 65493, 65491, 65488]   (= -45,-40,-44,-43,-45,-48)
OK  DBF=08 DC0=9F D00=40 meas=[65288, 65284, 65286, 65286, 65287, 65286]
OK  DBF=20 DC0=85 D00=24 meas=[65360, 65345, 65352, 65362, 65362, 65349]
OK  DBF=08 DC0=C5 D00=18 meas=[65491, …]                                   (b7 negates, b6 is orthogonal)
```

### 4.3 The apply law, `C2/8E82` / `C2/8EAB` swept — **16/16 byte-exact**

```
ct_probe.exe ROM --callrom C2/8E82 --from <spliced> --regdp 0000 --regdb 7E --m16 --x16 \
                 --regx <tgt*2> --regy <2600+charId*0x50> --writes
```
```
amt=44    hp=10  max=96  $0DC0=05 X=0 -> disp=44   newHP=54    baseline
amt=200   hp=10  max=96  $0DC0=05 X=0 -> disp=200  newHP=96    overheal -> clamp to maxHP
amt=2000  hp=10  max=96  $0DC0=05 X=0 -> disp=999  newHP=96    amount > 999 -> BOTH display and apply clamp
amt=999   hp=10  max=96  $0DC0=05 X=0 -> disp=999  newHP=96
amt=1000  hp=10  max=96  $0DC0=05 X=0 -> disp=999  newHP=96
amt=44    hp=96  max=96  $0DC0=05 X=0 -> disp=44   newHP=96    already FULL: no change, number STILL shown
amt=44    hp=0   max=96  $0DC0=05 X=0 -> disp=44   newHP=44    ** DEAD target: C2/8E82 REVIVES it **
amt=44    hp=10  max=96  $0DC0=40 X=0 -> disp=96   newHP=96    $0DC0 b6 = FULL: amount := maxHP
amt=7     hp=10  max=96  $0DC0=45 X=0 -> disp=96   newHP=96
amt=44    hp=10  max=96  $0DC0=85 X=0 -> disp=44   newHP=54    b7 without b6 is NOT full (here)
amt=44    hp=10  max=96  $0DC0=05 X=2 -> disp=44   newHP=54    X=2 reads $0024 and writes $0F65
```
```
=== C2/8EAB (MP) ===
amt=5     mp=20  max=20  $0DC0=05 -> disp=5     newMP=20   already full
amt=5     mp=10  max=20  $0DC0=05 -> disp=5     newMP=15   normal
amt=500   mp=10  max=20  $0DC0=05 -> disp=500   newMP=20   ** NO 999 clamp on the MP display **
amt=2000  mp=10  max=20  $0DC0=05 -> disp=2000  newMP=20
amt=5     mp=10  max=20  $0DC0=40 -> disp=20    newMP=20   FULL MP
```
The max-MP cases were driven by patching **`sheet+9`** while `sheet+$41` held `FF FF` — the clamp
followed `+9`, so `+9` is the cap the ROM reads. (This is the defect in `src/menu.c`, §5.3.)

### 4.4 THE INPUTS MOVE — the caster's MAGIC, end to end

`$0DBF` is *not* directly `sheet+$39` in the save: patching `$7E:2689` had **no effect** (`$0DBF` stayed
`08`) because the menu re-derives `+$36..+$3E` from the base block `+$0B..+$13` plus equipment at open.
Patching the **base magic `sheet+$0E` = `$7E:265E`** does move it:
```
for M in 03 08 10 20 40 7F; do
  python scratchpad/menu/tfx_splice.py saves/tech_double.state scratchpad/menu/_tfx_mag.state 265E=$M
  ct_probe.exe ROM --monitor "7E0DBF:1,7E2689:1,7E0F63:2,7E2603:2,7E0D00:1" \
     --from scratchpad/menu/_tfx_mag.state --btnscript "$SCRIPT-truncated" --diff | grep '^  f349'
done
```
```
  +0x0E=0x03 : 7E0DBF=03  7E2689=03  7E0F63=1700*  7E2603=1B00*  7E0D00=24     (17 HP, 10 -> 27)
  +0x0E=0x08 : 7E0DBF=08  7E2689=08  7E0F63=4400*  7E2603=3600*  7E0D00=24     (44 HP, 10 -> 54)
  +0x0E=0x10 : 7E0DBF=10  7E2689=10  7E0F63=8800*  7E2603=6000*  7E0D00=24     (88 HP, clamp 96)
  +0x0E=0x20 : 7E0DBF=20  7E2689=20  7E0F63=7601*  7E2603=6000*  7E0D00=24     (176, clamp 96)
  +0x0E=0x40 : 7E0DBF=40  7E2689=40  7E0F63=5203*  7E2603=6000*  7E0D00=24     (352, clamp 96)
  +0x0E=0x7F : 7E0DBF=63  7E2689=63  7E0F63=4505*  7E2603=6000*  7E0D00=24     (** 0x7F -> 99: CLAMPED **)
predictions (cursor 0x24, rnd[0x29]=0x80, $0DC0=5, Crono 10/96):
  magic   3 -> amount  17  BCD 0017  newHP 27
  magic   8 -> amount  44  BCD 0044  newHP 54
  magic  16 -> amount  88  BCD 0088  newHP 96
  magic  32 -> amount 176  BCD 0176  newHP 96
  magic  64 -> amount 352  BCD 0352  newHP 96
  magic  99 -> amount 545  BCD 0545  newHP 96        <- 0x0545 measured at +0x0E=0x7F
```
Six for six, including the 99 clamp that the derive step applies on the way from `+$0E` to `+$39`.
`$0F63` is **BCD**: `0x0176` = 176, `0x0352` = 352, `0x0545` = 545.

**The target's max HP does NOT enter the amount** — only the clamp:
```
tfx_splice … 265E=20 2605=E703 263F=E703      (magic 32, Crono max HP 999)
  f349  7E0DBF=20  7E263F=E703  7E0F63=7601  7E2603=BA00
```
176 healed onto 10 = **186** (`0x00BA`), unclamped.

**The RNG really is per-frame** — same state, A pressed 1/2/3 frames later:
```
T2 +1 frame : f350  7E0DBF=08 7E0F63=4700 7E2603=3900 7E0D00=25      (47, pred 47)
T3 +2 frames: f351  7E0DBF=08 7E0F63=4600 7E2603=3800 7E0D00=26      (46, pred 46)
T4 +3 frames: f352  7E0DBF=08 7E0F63=4600 7E2603=3800 7E0D00=27      (46, pred 46)
```

### 4.4b The MAGIC clamp is **99**, and the MP cap is **`+$09`** (the `src/menu.c` defect, isolated)

```
for M in 62 63 64 6E FF; do tfx_splice … 265E=$M ; ct_probe --monitor "7E0DBF:1,7E2689:1,7E0F63:2" … ; done
  base 0x62 -> f349  7E0DBF=62  7E2689=62  7E0F63=3905*     (98 -> 539)
  base 0x63 -> f349  7E0DBF=63  7E2689=63  7E0F63=4505*     (99 -> 545)
  base 0x64 -> f349  7E0DBF=63  7E2689=63  7E0F63=4505*     ** clamped **
  base 0x6E -> f349  7E0DBF=63  7E2689=63  7E0F63=4505*
  base 0xFF -> f349  7E0DBF=63  7E2689=63  7E0F63=4505*
```
`magic 98 -> ((99*$80)>>8) + 98*5 = 49 + 490 = 539` and `magic 99 -> 50 + 495 = 545`; the derive step
saturates the derived stat at **99 (`$63`)**.

```
--callrom C2/8EAB   (Marle's sheet, MP := 0, amount := 50, $0DC0 = 05)
  sheet+09 = 7   sheet+41 = 99   -> newMP = 7      <- the cap FOLLOWS +$09
  sheet+09 = 99  sheet+41 = 7    -> newMP = 50     <- +$41 does nothing
  sheet+09 = 33  sheet+41 = 0    -> newMP = 33
```
`src/menu.c:781` reads the effective max MP from `sh[0x41]|sh[0x42]<<8`. It must read `sh[9]|sh[10]<<8`.

### 4.5 ALL-PARTY, measured with a spliced **Lapis** (`$CA`)

```
python scratchpad/menu/tfx_splice.py saves/tech_double.state scratchpad/menu/tfx_lapis.state \
       2400=CA00C500 2500=01000100
ct_probe.exe ROM --monitor "7E0067:1,7E0D17:1,7E0DBB:6,7E0D00:1,7E0F63:12,7E2603:2,7E2653:2,7E26A3:2,\
7E2400:4,7E2500:4,7E1E00:4" --from scratchpad/menu/tfx_lapis.state \
  --btnscript "0:10,200:2,0:70,80:2,0:20,100:2,0:40,100:2,0:30,100:2,0:60,100:2,0:60" --diff
```
```
f179  $0DBB=0C 00 89 00 B2 04        <- base $0C, $0DBC=00, ** $0DBD=89 (HP + ALL) **, $0DBE=00,
                                          $0DBF=B2 (flat), $0DC0=04  => amount 0x32*4 = 200
f182  $0DBB=0C 02 89 00 B2 04        <- $0DBC = 02
f241  $0F63=0002 0002 0002 FFFF FFFF FFFF     <- BCD 200 / 200 / 200
      $2603=6000  $2653=4C00  $26A3=5800      <- Crono 10->96, Marle 65->76, Lucca 40->88 (all maxed)
      $1E00=00 06 80 07                       <- the HP chime, id $06
f283  $2400=0000C500 $2500=00000100  $0DBD -> 00    <- the unit consumed (C2/B31C / C2/B341)
f284  $0DBC -> 00
```

### 4.6 HP **and** MP, and the FULL flag — a spliced **Elixir** (`$C5`)

```
tfx_splice … 2400=C500 2500=0100 2607=0100 2657=0500 26A7=0300     (Crono 1 MP, Marle 5, Lucca 3)
```
```
f179  $0DBB=0C 00 CA 00 B2 40        <- ** $0DBD=CA (HP + MP + ALL), $0DC0=40 = FULL **
f182  $0DBC=03                       <- BOTH bits: HP and MP are restorable
f241  $0F63=9600 7600 8800 | 1200 2000 1600
      $2603=6000 $2607=0C00 | $2653=4C00 $2657=1400 | $26A3=5800 $26A7=1000
      $1E01=06                       <- the HP chime
f282  $1E01=07                       <- ** the MP chime, exactly 41 frames later **
```
The six BCD numbers are each target's **max** HP (96/76/88) and max MP (12/20/16) — the `$0DC0` b6 path
displays the max, not the delta. 241 → 282 = 41 = the length of the `$FF:CEBA` HP arm.

### 4.7 REFUSAL — **Aura Whirl is not field-castable**

Drive script with an extra RIGHT during char-select to reach the **Double** tab:
```
ct_probe.exe ROM --monitor "7E00BE:1,7E0418:1,7E0F49:1,7E0F4C:1,7E0F4F:1,7E0068:1,7E1E01:1,7E0D76:1,7E2603:2" \
  --from saves/tech_double.state \
  --btnscript "0:20,200:2,0:60,80:2,0:14,80:2,0:30,100:2,0:50,20:2,0:20,80:2,0:20,100:2,0:60,100:2,0:60,100:2,0:100" --diff
```
```
f205  $0418=01*  $0F49=01*                     <- tab 1 = DOUBLE, one row
f227  $00BE=03*  $0F4C=39*                     <- the list, ** Aura Whirl ($39) highlighted **
f289  $1E01=01*  $0D76=03*                     <- A: ** THE BUZZ **; $00BE stays 3, $0F4F stays 00
f351  $1E01=01*                                <- A again: buzzes again
```
`$0F4F` never becomes non-zero, i.e. the refusal fired at `C2/B406` (the `$7700` bit7 test) **before**
`JSR C2/BC22`. Confirmed statically on the pre-cast state:
```
ct_probe.exe ROM --read "wram:7709+1,wram:7739+1" --from scratchpad/menu/tfx_pre.state
   7E7709 = 80      (Aura: usable outside battle)
   7E7739 = 00      (Aura Whirl: NOT)
```

### 4.8 The other three refusals

```
R3  Marle MP = 0   (tfx_splice … 2657=0000)
    f277  $00BE=03  $0F4F=01*  $1E01=01*        <- the cost WAS computed, then C2/B437 buzzed;
                                                   $0DBD/$0DBE never written, still in the LIST
R4  whole party at FULL HP   (2603=6000 2653=4C00 26A3=5800)
    f277  $00BE=04*  $0DBD=84*  $0F4F=01*  $0DBC=00     <- the overlay OPENS anyway
    f339  $1E01=01*                                     <- A: buzz; no HP change, no MP spent
R5  target Crono DEAD, HP 0   (2603=0000)
    f277  $00BE=04*  $0DBD=84*  $0DBC=00
    f279  $0DBC=02*                                     <- ** a HP-0 target is VALID **
    f349  $1E01=06*  $0F63=4400  $2603=2C00*            <- healed to 44: the cast REVIVES
    f391  $2657=1300*                                   <- and the MP is still spent
R6  Marle MP = 1 = exactly the cost   (2657=0100)
    f349  $1E01=06*  $2603=3600*                        <- the cast succeeds
    f391  $2657=0000*                                   <- MP -> 0
    f392  $0DBC=00*                                     <- ** C2/B839 STZ $0DBC **
    f421  $1E01=01*                                     <- the next A buzzes; still substate 4
```

### 4.9 The `$0F63` double use, isolated (target = Marle)

Extra DOWN inside the overlay before the cast:
```
--btnscript "$SCRIPT-to-f338, 0:10,20:2,0:20,100:2,0:60"
  f381  7E0DBE=01  7E0F63=4800 3000 FFFF  7E2603=0A00  7E2653=4C00*  7E0D00=44
```
`$0DBE = 1`, so `C2/8E82` ran with `X = 2` and stored the **raw** amount in `$0F65` = `0x0030` = 48
(`rnd[0x49] = 0xFD` → `((9*253)>>8) + 40 = 48`); `C2/8FCB` then wrote its **BCD** `0x0048` into
`$0F63 + $00` with `$00 = 0`. Marle 65 + 48 → clamped to her max 76 (`0x4C`). Crono untouched.

### 4.10 The popup and the task, frame by frame

```
ct_probe.exe ROM --monitor "7E00E8:2,7E006E:1,7E0F60:2,7E0F53:2,7E0F5D:2,7E19D1:1,7E2657:2" \
  --from saves/tech_double.state --btnscript "$SCRIPT-to-f338,100:2,0:70" --diff
```
```
f339  $E8=0000  $6E=28  $0F60=0000  $19D1=90*                 <- A: the pose is armed
f349  $E8=0201* $6E=38* $0F60=0103* $0F53=0100*               <- +10 frames: apply; task slot 7 alive,
                                                                 $0F60=01 $0F61=03 (the HP arm)
f350..f389  $0F60 = 02 03 … 29 (03)                           <- 41 drawing frames
f390  $E8=0000* $0F60=2A03*                                   <- the FF terminator; slot 7 dies
f391  $6E=28*  $19D1=00*  $2657=1300*                         <- C2/8F55 returns: pose cleared, MP spent
```
And the number itself, from the OAM dumps (`--peek … .oam`, pre-cast vs +20 frames):
```
OAM entries changed: [34, 35, 93]
  e22 pre=00e000e0  pop=dc3a7831      <- x=$DC y=$3A tile=$78 attr=$31
  e23 pre=00e000e0  pop=e43a7831      <- x=$E4 …  (two digits, "44": digit 4 + $74 = $78) 
framebuffer pixels differing (pre vs popup): 1064
```

### 4.11 `$19D1` — the negative

Whole-ROM absolute+long scan: `$19D1` written at `C2/B4E8`, `C2/B4FF`, `C2/B9F5`; `$19E1` at `C2/B4F4`;
**no read anywhere**, and no indexed base in banks `$C0..$C3` in `$1900..$19E1` covers them. The reader
is DP-relative (`PEA $19C0 / PLD` at `C2/BA1C`, `C2/CA9F`, `C2/F58A`; `PEA $1980 / PLD` at `C2/CE2C`,
`C2/F56A` — the only five such sites in the ROM). Behaviourally, 5 frames into the 10-frame wait:
```
OAM bytes differing (pre-cast vs +5f): 0
framebuffer pixels differing (pre-cast vs +5f): 504     <- the blinking target cursor, not attributable
```

---

## 5. IMPLEMENTATION SPEC (in the engine's own terms)

### 5.1 New state (`src/field.c`, alongside `menuItemFx` / the screen-3 arm from `menu2_tech_screen.md`)
```c
int menuTechCaster;      /* $0F4D  — the caster's party slot (0..2)                                  */
int menuTechId;          /* $0F4C  — the highlighted tech id                                          */
int menuTechCost;        /* $0F4F  — CtMenuTechMp(...) latched at the A press                         */
int menuTechFx;          /* $0DBD  — b7 HP, b6 MP, b3 ALL                                             */
int menuTechAmtSrc;      /* $0DBF  — the caster's derived MAGIC (sheet+0x39)                          */
int menuTechNib;         /* $0DC0  — $CC:2140[id*12] & 0x1F                                           */
int menuTgtIdx;          /* $0DBE                                                                     */
int menuTgtMask;         /* $0DBC  — recomputed EVERY frame while the overlay is up                   */
int menuCastWait;        /* the 10-frame pre-roll, then the 42-frame popup block                      */
unsigned menuRngCursor;  /* $0D00  — ++ once per frame for the WHOLE menu, not just this screen       */
```

### 5.2 `src/menu.c` — the shared amount + apply (replaces the private half of `mItemApplyOne`)
```c
/* C2/8ECC.  amtSrc = $0DBF, nib = $0DC0, cur = $0D00.  out[0..2] = HP per target, out[3..5] = MP. */
void CtMenuRestoreAmounts(int amtSrc, int nib, unsigned cur, int out[6]) {
  if (amtSrc & 0x80) {                                  /* items: flat  (C2/8EDA) */
    int v = (amtSrc & 0x7F) * (nib & 0x0F);
    for (int i = 0; i < 6; i++) out[i] = v;
    return;
  }
  int base = amtSrc * (nib & 0x1F);                     /* techs (C2/8EF9)        */
  for (int k = 0; k < 6; k++) {
    int r = ctRngTable[cur + (5 - k)];                  /* NO MASK -- see the NOTE below */
    int v = ((((amtSrc + 1) & 0xFF) * r) >> 8) + base;
    if (nib & 0x80) v = (-v) & 0xFFFF;                  /* C2/8F41, 16-bit BIT    */
    out[k] = v & 0xFFFF;
  }
}
```
`ctRngTable` = the 256 bytes at ROM `$C0:FE00` (bake it like the other `src/data/*.h` blobs).
**NOTE — the over-read.** The ROM reads `$C0:FE00 + cur + 0..5` with **no `& 0xFF`**; for `cur >= 0xFB`
the last words come from `$C0:FF00..` (`78 18 FB 5C 00 C0`). Bake **262** bytes (`$C0:FE00..$C0:FF05`) and
index `cur + i` without masking, or the six cursors `$FA..$FF` are wrong.

```c
/* C2/8E82 / C2/8EAB.  `nib` is $0DC0: b6 = restore to max.  `hp` selects the pool. */
static int mRestorePool(uint8_t* sh, int hp, int amt, int nib) {
  int co = hp ? 3 : 7, mo = hp ? 0x3F : 0x09;                  /* +3/+3F HP, +7/+09 MP */
  int cur = sh[co] | (sh[co+1] << 8), mx = sh[mo] | (sh[mo+1] << 8);
  int shown = (nib & 0x40) ? mx : amt;                         /* FULL */
  if (hp && shown > 999) shown = 999;                          /* ** HP ONLY ** */
  int nv = cur + shown; if (nv > mx) nv = mx;
  sh[co] = nv & 0xFF; sh[co+1] = (nv >> 8) & 0xFF;
  return shown;                                                /* the number the popup shows */
}
```
There is **no dead check and no already-full check** here — both live upstream in the `$0DBC` mask.

```c
/* C2/8E2D. slots[] = $0D5F, n = $73. Returns the 6 display numbers (-1 = "no number"). */
void CtMenuRestoreApply(uint8_t* mem, const uint8_t* slots, int n,
                        int fx, int tgt, int amtSrc, int nib, unsigned cur, int show[6]) {
  int amt[6]; CtMenuRestoreAmounts(amtSrc, nib, cur, amt);
  for (int i = 0; i < 6; i++) show[i] = -1;                    /* the FFFF fill, C2/8E36 */
  int i0 = (fx & 0x08) ? 0 : tgt, cnt = (fx & 0x08) ? n : 1;   /* ** bit3 = ALL PARTY ** */
  for (int k = 0; k < cnt; k++) {
    int idx = i0 + k; if (idx > 2) break;
    uint8_t* sh = mem + 0x2600 + (size_t)(slots[idx] & 7) * 0x50;
    if (fx & 0x80) show[idx]     = mRestorePool(sh, 1, amt[idx],     nib);
    if (fx & 0x40) show[idx + 3] = mRestorePool(sh, 0, amt[idx + 3], nib);
  }
}
```

### 5.3 `src/menu.c` — **two defects in the already-ported item path**

1. **`mItemApplyOne` (L779) reads the effective max MP from `sh[0x41]|sh[0x42]<<8`. The ROM reads
   `+0x09/+0x0A`** (`C2/8EBF`/`C2/8EC4`, and the `--callrom C2/8EAB` sweep in §4.3, where `+0x41` was
   `FF FF` and the clamp still landed on `+9`). In the live sheets `+0x41` is `00 FF` / `FF FF` /
   `00 04` — garbage — so every MP item currently clamps to nonsense. `CtMenuItemTargetMask` in the same
   file already uses `sh[9]`, so the file contradicts itself.
2. **`mItemApplyOne` applies the 999 clamp to the MP pool as well.** `C2/8EAB` has no `CMP #03E7`
   (measured: an amount of 2000 displays 2000). Harmless for the applied value (the max clamp hides it)
   but wrong for any future popup, and wrong as a statement of the ROM.

Both are one-line fixes; neither is in scope for this dossier's own change.

### 5.4 `src/menu.c` — the tech entry point (new)
```c
/* C2/B3E6 + C2/B4DE.  Returns: 1 cast, 0 refused (the caller plays the buzz, sound id $01). */
int CtMenuTechUse(uint8_t* mem, const uint8_t* slots, int n,
                  int casterSlot, int techId, int tgtSlot, unsigned rngCur, int show[6]);
```
Order of operations, byte-for-byte:
1. refuse if `casterSlot >= n` (`$78`);
2. refuse if `!(ctTechProp[techId] & 0x80)` — the nine ids `09 0C 0F 1A 1D 21 24 27 29` seeded by
   `FF/F82E`; the port must seed the same nine and must **not** let the "+1 next tech" marker set b7;
3. `cost = CtMenuTechMp(charId, techId, tab, accessory)`; refuse if `sheet[+7] < cost` (8-bit);
4. `fx  = (ctTechRec[techId*12+1] & 0xC0) | (((ctTechRec1ACB[techId*2] & 1) + 1) << 2)`;
   `nib = ctTechRec[techId*12] & 0x1F`; `amtSrc = sheet[casterCharId][0x39]`;
5. **enter the overlay** (a separate substate) — the cast itself only happens on the *next* A, and only
   if `CtMenuItemTargetMask`-style `$0DBC != 0` for the current cursor;
6. on that A: arm the pose (`$19D1 = 0x90`, `$19E1 = ctCastPose[charId]` from `$C2:B519`
   `{3,5,4,0,3}`), **wait 10 frames**, then `CtMenuRestoreApply`, post the chime
   (`cmd $19`, id `$06` if any HP number, `$07` if any MP number — the tech path can only ever produce
   `$06`), run the popup for **41 frames per pool**, clear `$19D1`, then
   `sheet[casterCharId][7] = (sheet[...][7] - cost) & 0xFF` (**8-bit, no borrow into +8**), and redraw;
7. stay in the overlay substate and recompute `$0DBC` every frame, including the
   `sheet[+7] < cost → mask = 0` rule (`C2/B839`), so a second A buzzes once the MP runs out.

### 5.5 What NOT to build
* No all-party field TECH exists for a Crono/Marle/Lucca party, and **no double or triple tech is ever
  field-castable** — but do **not** hard-code "singles only": implement the `$7E:7700+id` bit7 gate, which
  is the ROM's own general rule (`$1D` and `$24` are single techs that *are* all-party).
* No field tech restores MP (`rec[1] & $C0 == $80` for all nine) and none is a FULL restore
  (`$0DC0 <= $1F`), but the shared applier must still implement b6/b7 of `$0DBD` and b6/b7 of `$0DC0`
  because the **item** path uses them.

---

## 6. OPEN / BLOCKED

1. **`$19D1` / `$19E1` have no observable effect.** They are menu sprite record 7's `+$11` / `+$21`
   (proved structurally through `C2/F588`'s `PEA $19C0 / PLD` reset loop), but no code reads them by an
   absolute or long address anywhere in the 4 MB, and 5 frames into the 10-frame pre-roll the OAM is
   **byte-identical** to the pre-cast frame (§4.11). Either the consumer is DP-relative in a routine I did
   not identify, or the record is only re-read on a rebuild that this scenario never triggers. For the
   port: mirror the two writes, gate nothing on them.
2. **`$19C0` is not initialised by the tech path.** Its only ROM writer is `C2/AF4D` (`#FF`, in the
   *item* overlay build). In `saves/tech_double.state` it is `00`; in the mid-menu capture it is `01`.
   `C2/B4EC` indexes `$C2:B519` (a 5-byte table) with it through a **long,X** load, so a stale `$FF`
   would read `$C2:B618` — a real out-of-table read the ROM never guards. I could not find what sets it
   to the caster's charId on the tech screen (`C2/BA20` does, but `C2/BA09` has no caller I traced).
3. **The reserve-caster buzz (`$78`) was not driven.** It is unambiguous in the ROM (`C2/8825 CPX $73 /
   BCC + / DEC $78`, then `C2/B3EB LDA $78 / BPL`), but with a 3-member active party the char cursor can
   never exceed `$73-1`, and I did not find a splice that produces a reserve member without breaking the
   panel count. Reported as ROM-derived, not measured.
4. **"Dead" may not be only HP 0.** §4.8 R5 spliced `$2603 = 0000` on an otherwise-alive Crono and the
   cast healed him to 44. That is exactly what `C2/B870` and `C2/8E82` do — neither has a death test. I
   did **not** prove that the rest of the game keeps no separate death marker in the sheet that a real KO
   would also set; the two menu routines on this path simply never look at one. (`C2/A1F5` and `C2/BF14`
   compare `sheet+3` against `$0D38,X` / `$9890` only for the *low-HP colour*.)
5. **`$7E:9588` (the 8 bytes MVN'd from `$FF:CB9C`) has no located reader.** An absolute+long scan of the
   whole ROM for `$9588` / `$7E9588` returns nothing. The role is inferred from the *contrast* with the
   sibling caller `C2/B2A7`, which MVNs a **different** 8 bytes (`$FF:CBAC`) to the same address before
   calling the same presenter — so it is a per-caller style block — but the consumer is unidentified.
6. **`$51` (the bit-weight cursor `C2/F13F` and `C2/9047` both read) is unpinned.** It is `LDX $51` /
   `LDY $51` with no nearby writer in either routine; the BCD results are correct in every measurement,
   so it is 0 in practice, but I did not find who guarantees that.
7. **The tab-1/tab-2 MP column and the "MP Used" split are still unmeasured** for a real double, and are
   moot for the cast: no double is field-castable. `menu2_tech_screen.md` §6(1) stays open on the
   *rendering* side.
8. **`$0DBD` bit2 is never tested by this path.** For items the scope nibble can be `0`, `4`, `8` or `$0C`
   (`((\$CC:1BB5[id]+1)&3)<<2`); only b3 is read here. The whole byte is used as an index at `C2/B28E`
   (`LDA $FFCF2B,X`) in the *status* path, which is out of scope.
9. **The popup's screen anchor (`$180E,Y` via `C2/F626`) and `C2/9093`'s digit x-offset table
   (`$C2:90F8`) were read but not decoded** — the port has no floating-number renderer yet, so they were
   measured only far enough to confirm the tile/attr (§4.10).

---

## ADVERSARIAL VERIFICATION

> Second pass, 2026-08-30, by a different agent whose brief was to **refute** the sections above.
> Nothing below was taken from the text it checks: every routine was re-disassembled from the ROM
> (`python tools/dis.py`, plus raw byte dumps), every model was re-written from those bytes before
> being compared to a measurement, and every probe was re-run with `scratchpad/menu/ct_probe.exe`.
> Scratch scripts written for this pass: `scratchpad/menu/adv2_sheet.py`, `adv2_sweep.py` (my own
> `C2/8ECC` model + 26 cases), `adv2_apply.py` (`C2/8E82`/`C2/8EAB` sweeps), `adv2_all.py`
> (`C2/8E2D` all-party with a TECH source), `adv2_refuse.py` (the refusal battery), `adv2_mag.py`
> (the caster-MAGIC sweep), `adv2_item.py` (Lapis + Elixir). States spliced in this pass:
> `scratchpad/menu/_adv2.state`, `_adv2a.state`, `_adv2b.state`, `_adv2c.state`, `_adv2d.state`,
> `_adv2i.state`, `_adv2m.state`, `_adv2p.state`, `_adv2r.state`.

**Verdict: SOUND WITH FIXES.** The load-bearing machinery — the two-armed amount at `C2/8ECC`, the
16-bit `BIT $0DBF` reads, the no-wrap RNG over-read, the `+$09` max-MP cap, the HP-only 999 clamp,
the `$0DBD` targeting, the four refusals, the 10-frame / 41-frame contract, and "`C2/8F55` is ONE
routine shared by the item and the tech path" — all survived independent re-derivation and
re-measurement. **Seven defects** are recorded below; one of them (AV.D1) is a real port bug in
§2.9/§3/§5.4, the rest are prose/attribution errors or unreachable spec-vs-ROM divergences.
Nothing in the spec would make the engine spend MP for an invented amount.

### AV.0 What was re-confirmed, and how

| claim | how it was re-checked | result |
|---|---|---|
| `C2/8F55` = `JSR C2/8E2D` + `MVN` **8** bytes `$FF:CB9C`→`$7E:9588` + `JSR C2/8F6C` | `dis.py 28F55` (`LDA #0007` → A+1 = 8 bytes) | ✔ |
| `C2/8F55` is shared by item + tech, and by nothing else | byte scan of all 4 MB for `20 55 8F` / `4C 55 8F` / `22 55 8F C2`: **exactly two** hits, `0x2B1BF` (item) and `0x2B4FC` (tech). `20 2D 8E` → 1 hit, `20 CC 8E` → 1, `20 82 8E` → 1, `20 AB 8E` → 1, `20 6C 8F` → 2 (`28F67`, `2B2D6`) | ✔ |
| the amount formula, **both arms** | my own model, 26 cases, `--callrom C2/8ECC` (§AV.1) | **26/26 exact** |
| the two `BIT $0DBF` in the appliers and the one at `C2/8F41` are 16-bit | raw bytes: `28E87`, `28EB0` and `28F41` all read `2C BF 0D` (`BIT` absolute) with M=16 at that point (`REP #30` at `28E83`/`28EAE`, `REP #20` at `28F36`) | ✔ |
| sheet `+$09/$0A` is the max-MP cap, not `+$41` | `--callrom C2/8EAB` with `sheet+$41 = 63 00` and `sheet+9 = 07 00`, amount 50, MP 0 → **newMP = 7** (§AV.2) | ✔ the `src/menu.c` `sh[9]` fix is right |
| the 999 clamp is HP-only | `--callrom C2/8EAB` amount 2000 → **disp = 2000**, no clamp | ✔ |
| `$0DBF` (tech) = the caster's derived MAGIC, clamped to 99 | 11 spliced states (§AV.3): patching `+$39` directly does nothing; patching `+$0E` moves it; `$64/$7F/$FF` all land on `$63` | ✔ |
| `$0DBD` b7/b6/b3 and the count-`$73` loop | `dis.py 28E2D`; `--callrom C2/8E2D` with `$0DBD = $88` returns `x=0004 y=26A0` (the loop reached party index 2) vs `x=0000 y=2600` for `$84` | ✔ |
| the nine ids, `$CC:2140`/`$CC:2141`/`$CC:1ACB`/`$CC:253B` derivations | `dis.py 3FF82E` (nine `TSB`s) + a table dump: every row of §2.8 reproduces byte-for-byte, including `id 29 rec[0] = $4E → $0DC0 = $0E` | ✔ |
| the item-side build | `dis.py 2B0AB`: `$0DBD = ((($CC:1BB5[i]+1)&3)<<2) | $CC:0001[rec]`; `$0DC0 = $CC:0002[rec] & $0F`, and `$0F → $40` | ✔ |
| Aura Whirl is refused at `C2/B406`, before the cost | drive to the Double tab: f227 `$0F4C=39`, f289 `$1E01=01`, `$0F4F` **never leaves 00** | ✔ |
| the four refusals | re-driven from my own splices (§AV.5) | ✔ all four |
| a dead (HP 0) target is healed | `$2603 = 0000` → f279 `$0DBC=02`, f349 `$2603=2C00` (44), f391 MP still spent | ✔ |
| the `$0F63` double use | reproduced with **different numbers**: targeting Marle gives `$0F63 = 4500 2D00 FFFF` — raw `0x002D` = 45 at slot 1, BCD `0x0045` compacted into slot 0 | ✔ |
| the 10-frame gap and the 41-frame popup | f339 → f349; `$0F60` 01→`$2A` over f349..f390; `$FF:CEBA` = 41 entries + `FF`, twice | ✔ |
| the chime goes through `C2/EAF9` | `dis.py 2EAF9` (`STZ $0D76` **before** the post) + a scan: `C2/EAF9` has **one** JSR site (`C2/9047`) and `C2/EAD6` has **zero** callers | ✔ dead code confirmed |
| all-party, live | Lapis `$0DBD=89 $0DBF=B2 $0DC0=04` → 3 heals, `$0F53=3`, chime `$06`; Elixir `$0DBD=CA $0DC0=40` → six BCD maxima, HP chime f241 → **MP chime f282, exactly 41 frames** | ✔ |
| the item path has the same `$0DBC==0` buzz | `dis.py 2B1B5`: `LDA $0DBC / BNE +5 / JSR C2/EACC` | ✔ |

### AV.1 `C2/8ECC` re-swept against a model written from the bytes, not from §2.3

`scratchpad/menu/adv2_sweep.py`. Model (mine): flat `= (dbf&$7F)*(dc0&$0F)` if `dbf&$80`, else
`((((dbf+1)&$FF) * ROM[$C0FE00 + cur + (5-k)]) >> 8) + dbf*(dc0&$1F)`, two's-complemented iff
`dc0&$80`, **no `& $FF` on the table index**. 26 cases, chosen to include the ones §4.2 did *not*:

```
OK  DBF=08 DC0=05 D00=18 meas=[45, 40, 44, 43, 45, 48]      OK  DBF=08 DC0=05 D00=24 meas=[44, 48, 46, 44, 43, 47]
OK  DBF=05 DC0=05 D00=18 meas=[28, 25, 27, 27, 28, 30]      OK  DBF=0A DC0=18 D00=33 meas=[243, 242, 248, 249, 244, 250]
OK  DBF=7F DC0=1F D00=00 meas=[3993, 3982, 3991, 4056, 4038, 4025]        <- cursor 0
OK  DBF=7F DC0=1F D00=01 meas=[3960, 3993, 3982, 3991, 4056, 4038]        <- cursor 1
OK  DBF=63 DC0=1F D00=FF meas=[3069, 3104, 3167, 3078, 3115, 3073]        <- the WORST over-read
OK  DBF=08 DC0=05 D00=FB meas=[44, 40, 46, 46, 48, 43]
OK  DBF=08 DC0=05 D00=FC meas=[40, 44, 40, 46, 46, 48]
OK  DBF=08 DC0=05 D00=FE meas=[43, 48, 40, 44, 40, 46]
OK  DBF=08 DC0=20 D00=18 meas=[5, 0, 4, 3, 5, 8]            <- b5 of $0DC0 is OUTSIDE the &$1F mask
OK  DBF=08 DC0=3F D00=18 meas=[253, 248, 252, 251, 253, 256]
OK  DBF=00 DC0=1F D00=18 meas=[0, 0, 0, 0, 0, 0]            OK  DBF=01 DC0=1F D00=18 meas=[32, 31, 31, 31, 32, 32]
OK  DBF=80 DC0=0F D00=18 meas=[0, 0, 0, 0, 0, 0]            <- $80 takes the FLAT arm and (v&$7F)=0
OK  DBF=81 DC0=0F D00=18 meas=[15, 15, 15, 15, 15, 15]
OK  DBF=B2 DC0=40 D00=18 meas=[0,...]                       OK  DBF=B2 DC0=4F D00=18 meas=[750,...]
OK  DBF=FF DC0=0F D00=18 meas=[1905,...]                    OK  DBF=8A DC0=03 D00=18 meas=[30,...]
OK  DBF=08 DC0=85 D00=18 meas=[65491, 65496, 65492, 65493, 65491, 65488]
OK  DBF=08 DC0=C5 D00=18 meas=[65491, ...]   OK  DBF=7F DC0=9F D00=40 meas=[61597, 61539, 61565, 61559, 61572, 61565]
OK  DBF=08 DC0=45 D00=24 meas=[44, 48, 46, 44, 43, 47]      OK  DBF=0E DC0=0E D00=7A meas=[199, 210, 201, 198, 208, 202]
OK  DBF=63 DC0=05 D00=AA meas=[509, 562, 503, 559, 529, 564]
mismatches: 0 of 26
```
`$C0:FE00..FEFF` re-checked as a true permutation of 0..255; `$C0:FF00 = 78 18 FB 5C 00 C0` — so
§5.2's "bake 262 bytes `$C0:FE00..$C0:FF05`" is exactly the right extent (`cur $FF` + `i 5` = 0x104).

### AV.2 `C2/8E82` / `C2/8EAB` re-swept (`scratchpad/menu/adv2_apply.py`)

```
=== C2/8E82 (HP), Y=$2600 ===
  baseline         amt=44     hp=10  max=96    DC0=05 X=0 -> disp=44   newHP=54
  overheal         amt=200    hp=10  max=96    DC0=05 X=0 -> disp=200  newHP=96
  amt2000          amt=2000   hp=10  max=96    DC0=05 X=0 -> disp=999  newHP=96
  full-hp tgt      amt=44     hp=96  max=96    DC0=05 X=0 -> disp=44   newHP=96
  dead tgt         amt=44     hp=0   max=96    DC0=05 X=0 -> disp=44   newHP=44
  FULL b6          amt=44     hp=10  max=96    DC0=40 X=0 -> disp=96   newHP=96
  FULL b6 max2000  amt=44     hp=10  max=2000  DC0=40 X=0 -> disp=999  newHP=1009   ** see AV.D6 **
  NEG b7 (-44)     amt=65491  hp=10  max=96    DC0=85 X=0 -> disp=999  newHP=96     ** see AV.D3 **
  NEG small (-1)   amt=65535  hp=10  max=96    DC0=85 X=0 -> disp=999  newHP=96
  X=2 tgt1         amt=44     hp=10  max=96    DC0=05 X=2 -> disp($0F65)=44 newHP=54
  X=4 tgt2         amt=44     hp=10  max=96    DC0=05 X=4 -> disp($0F67)=44 newHP=54
  maxHP 65535      amt=2000   hp=10  max=65535 DC0=05 X=0 -> disp=999  newHP=1009
=== C2/8EAB (MP), Y=$2650 ===
  normal        amt=5     mp=10 max+9=20 DC0=05 -> disp=5     newMP=15
  amt500        amt=500   mp=10 max+9=20 DC0=05 -> disp=500   newMP=20    ** no 999 clamp **
  amt2000       amt=2000  mp=10 max+9=20 DC0=05 -> disp=2000  newMP=20
  FULL b6       amt=5     mp=10 max+9=20 DC0=40 -> disp=20    newMP=20
  cap follows +9   amt=50 mp=0  max+9=33 DC0=05 -> disp=50    newMP=33   (sheet+$41 = 00 00)
  +41=99 +9=7      amt=50 mp=0  max+9=7  DC0=05 -> disp=50    newMP=7    ** the cap is +$09, not +$41 **
  NEG b7        amt=65491 mp=10 max+9=20 DC0=85 -> disp=65491 newMP=20
```
The `+41=99 +9=7` row is the discriminating one the shipped `src/menu.c` fix depends on, re-run here
with the two candidate fields set to **different** values in the same call.

### AV.3 The caster's MAGIC really is the input (`scratchpad/menu/adv2_mag.py`, 11 spliced states)

Same drive script as §4.1; each row is the f349 line plus my independent prediction for
cursor `$24` (`rnd[$29] = $80`), `$0DC0 = 5`, Crono 10/96:
```
derived +39 := 0x40   f349  7E0DBF=08  7E2689=08  7E265E=08  7E0F63=4400  7E2603=3600   (unchanged: re-derived)
base +0E := 0x03      f349  7E0DBF=03  7E2689=03  7E0F63=1700  7E2603=1B00   pred amt=17  BCD=0017 newHP=27
base +0E := 0x08      f349  7E0DBF=08  7E2689=08  7E0F63=4400  7E2603=3600   pred amt=44  BCD=0044 newHP=54
base +0E := 0x10      f349  7E0DBF=10  7E2689=10  7E0F63=8800  7E2603=6000   pred amt=88  BCD=0088 newHP=96
base +0E := 0x20      f349  7E0DBF=20  7E2689=20  7E0F63=7601  7E2603=6000   pred amt=176 BCD=0176
base +0E := 0x40      f349  7E0DBF=40  7E2689=40  7E0F63=5203  7E2603=6000   pred amt=352 BCD=0352
base +0E := 0x62      f349  7E0DBF=62  7E2689=62  7E0F63=3905                pred amt=539 BCD=0539
base +0E := 0x63      f349  7E0DBF=63  7E2689=63  7E0F63=4505                pred amt=545 BCD=0545
base +0E := 0x64      f349  7E0DBF=63  7E2689=63  7E265E=64  7E0F63=4505     ** CLAMP 99 **
base +0E := 0x7F      f349  7E0DBF=63  7E2689=63  7E265E=7F  7E0F63=4505
base +0E := 0xFF      f349  7E0DBF=63  7E2689=63  7E265E=FF  7E0F63=4505
```
11/11 exact, including the BCD of every three-digit amount. §4.4's counter-intuitive claim — that
patching `sheet+$39` does nothing because the menu re-derives it — reproduces.

### AV.4 `$0D00` — the cursor is a MENU-only, 8-bit wrapping counter, and its writer is now named

```
ct_probe --monitor "7E0D00:1" --from saves/tech_double.state --btnscript "0:60,200:2,0:60" --diff --writer
  f0    7E0D00=00*
  f98   7E0D00=01*(C2:842F)      <- first tick, ~37 frames AFTER the X press at f60
  f99   7E0D00=02*(C2:842F)  ... every frame thereafter
```
`dis.py 28420` shows why: `C2/8420` is the **menu's NMI handler** — `PHA/PHD/PHP/SEP #20/PEA $0D00/
PLD/CMP $4210/ INC $00` — so `INC $00` at **`C2/842D`** with `D = $0D00` is `INC $0D00`, once per NMI.
It does **not** tick in the field (f0..f97 stay `00`), and it wraps at 256 because it is `INC` on a
byte. Arithmetic check against §4.1: the menu comes up ~f58 in that script, and f338 − f58 + 1 = 281,
`281 & $FF = $19` — exactly the `$0D00 = 19` measured at f338, and f349 gives `$24`.

### AV.5 The refusal battery, re-driven (`scratchpad/menu/adv2_refuse.py`)

```
### R3 Marle MP=0
   f277  $00BE=03  $0F4F=01*  $1E01=01*        <- cost computed, THEN buzz; still in the LIST, $0DBD untouched
### R4 whole party FULL HP
   f277  $00BE=04*  $0DBD=84*  $0F4F=01*  $0DBC=00     <- the overlay OPENS anyway
   f339  $1E01=01*                                     <- A buzzes; no HP change, no MP spent
### R5 target Crono DEAD (hp=0)
   f279  $0DBC=02*                                     <- a HP-0 target is VALID
   f349  $1E01=06*  $0F63=4400*  $2603=2C00*           <- healed to 44 (revived)
   f391  $2657=1300*                                   <- and the MP is still spent
   f392  $0DBC=00*   f393  $0DBC=02*                   <- TRANSIENT (see AV.D5)
### R6 Marle MP=1 (exactly the cost)
   f349  $1E01=06*  $2603=3600*      f391  $2657=0000*
   f392  $0DBC=00*                                     <- and it NEVER comes back (contrast R5's f393)
### R8 (NEW) Crono FULL 96/96, Marle hurt 30/76
   f339  $1E01=01*                                     <- A on target 0 BUZZES...
   f381  $0DBC=02*  $0DBE=01*                          <- ...one Down later the mask is 02
   f413  $1E01=06*  $0F63=45002D00FFFF*  $2653=4B00*   <- and it casts: Marle 30 -> 75
```
**R8 is new.** It drives what §2.8 only asserts: the mask is evaluated for the **cursor**, not the
party, so a full-HP member refuses while a hurt member two rows down does not. It also re-proves the
`$0F63` double use with different numbers (`$0F65 = 0x002D` = 45 raw at target index 1, `$0F63 =
0x0045` = BCD 45 at popup slot 0), and the R5-vs-R6 pair separates the one-frame redraw transient
from the permanent `C2/B839` zero.

### AV.6 `$E8`/`$E9` — OPEN #6 (numbered 7 in the list) is CLOSED

```
ct_probe --monitor "7E00E8:1,7E00E9:1,7E00EA:2" --from saves/tech_double.state --btnscript <cast> --diff --writer
  f349  7E00E8=02*(C2:823E)  7E00E9=01*(C2:823E)  7E00EA=1690*(C2:824E)
  f390  7E00E8=00*(C2:821C)  7E00E9=00*(C2:81D8)
```
`dis.py 281B9` names both halves. The menu scheduler walks `X = 0, 8, … $38` and does
```
281D1: LDY $B0,X / DEY / BMI ->281DA      ; state 0 = dead slot, skip
281D6: DEC $B1,X / BEQ  ->281E8           ; ** $B1,X is a FRAME COUNTDOWN **
```
so for slot 7 (`X = $38`) **`$E8` is the state byte and `$E9` is the frame countdown**. `C2/8216`'s
8-bit `STZ $B0,X` clears `$E8` (writer `C2:821C`); the countdown's own `DEC` clears `$E9` (writer
`C2:81D8`) on the very frame the task ends — which is why the 16-bit `LDA $E8 / BNE` wait loop at
`C2/8FC3` exits. Nothing beyond the scheduler is involved. The same `$B1,X` is why the pre-roll is a
real sleep and not a magic constant: `C2/822B` builds `A = $0A02` (`XBA` puts the caller's `#0A` in
the high byte, `LDA #02` the state) and stores it 16-bit into `$B0,X`, i.e. **state 2, countdown 10**.

### AV.D1 — DEFECT (port bug): `$C2:B519` is NOT a 5-entry table

§2.9, §3 and §5.4 all state `$C2:B519 = 03 05 04 00 03`, "one entry per charId 0..4", and §5.4 tells
the port to use `ctCastPose[charId] = {3,5,4,0,3}`. The ROM:
```
C2/B519: 03 05 04 00 03 06 04 08 | E2 30 ...   (2B521 = SEP #30, the next routine)
```
and the index is the **charId**, of which Chrono Trigger has **seven**. Proven by reordering the
party so that slot != charId (`$0D5F = $2980 = 01 00 02`, Marle in slot 0) and casting:
```
f215  7E19C0=01*(C2:BA29)  7E0F4D=00       <- caster is party SLOT 0, but $19C0 = 1
f339  7E19E1=05*(C2:B4F7)                  <- = $C2:B519[1], not [0]
f349  7E2653=4C00*(C2:8EA9)                <- and the heal landed on MARLE (slot 0), Crono untouched
f391  7E2657=1300*(C2:B516)                <- MP off MARLE's sheet
```
So the table needs **at least** `03 05 04 00 03 06 04` (charIds 0..6). A port carrying five entries
reads out of range for **Ayla, charId 5** — and Ayla is not hypothetical: the nine field-usable ids
are `charId*8 + 1 .. charId*8 + 8`, so `09/0C/0F` are Marle's (1), `1A/1D` Robo's (3), `21/24/27`
Frog's (4) and **`29` is Ayla's (5)**. (Magus, charId 6, owns `31..38` and can never reach
`C2/B4F0`, so entry 6 is unreachable but entry 5 is not.) Behavioural severity is low only because
§6(1) is right that no consumer of `$19E1` was found; the *table* claim is simply wrong.

### AV.D2 — REFUTED: OPEN #6(2), "`$19C0` is not initialised by the tech path"

It is. `--writer` names the store on the first frame of the tech list:
```
f215  7E19C0=01*(C2:BA29)
```
`dis.py 2BA09` shows the store is `C2/BA27 STA $00` executing under `PEA $19C0 / PLD`, with the
source `LDA $9A90` = the sheet copy's byte `+0` = **the caster's charId**. So `$19C0` is written by
`C2/BA09`'s body every time the tech screen builds, the `$FF` that `C2/F40F` leaves at f55 cannot
survive to `C2/B4EC`, and the "real out-of-table read the ROM never guards" that OPEN #6(2) worries
about does not occur on this path. (`C2/BA09` *does* have a live caller; it simply was not traced.)

### AV.D3 — DEFECT (uncited inference): `$0DC0` bit7 is a NEGATE, but it is not "the damage arm"

§1 and §2.4 call `$0DC0` b7 "= negate = **the DAMAGE arm**". The negate itself is real and I
reproduced it in `C2/8ECC` (§AV.1). But routed through the only two appliers that consume the result,
a negated amount does **not** damage:
```
C2/8E82  amt=65491 (-44) hp=10 max=96 DC0=85 -> disp=999   newHP=96    (full heal)
C2/8E82  amt=65535 (-1)  hp=10 max=96 DC0=85 -> disp=999   newHP=96
C2/8EAB  amt=65491       mp=10 max=20 DC0=85 -> disp=65491 newMP=20    (full MP)
```
because `C2/8E8F CMP #03E7` sees the two's-complement value as ≥ 999 and clamps it, and the
`CMP $003F,Y` that follows then clamps the sum to the max. `C2/8FCB`'s `LDA $0000,Y / BMI` would in
turn classify the raw negative as "no number". And **no caller can set the bit**: for a tech
`$0DC0 = $CC:2140[id·12] & $1F` (§2.7, re-verified), for an item it is a nibble or the literal `$40`
(`C2/B0D0-B0DF`, re-disassembled). The arm is unreachable and its purpose is not established by
anything in this dossier. Keep the two's-complement in the port (it is what `C2/8F41` does); drop the
word "damage".

### AV.D4 — DEFECT (ROM misquoted): `C2/8FCB` **stores** `$0F5D`, it does not OR it

§2.5 annotates `C2/8FCB` as "`$0F5D |= 1`". The bytes at `28FE6` are `A9 01 00 / 8D 5D 0F` =
`LDA #0001 / STA $0F5D` — an assignment. Only the MP half ORs (`28FE9: LDA $0F5D / ORA #0002 /
STA $0F5D`). Consequence in the ROM: a later member's HP number **clears** the MP bit that an earlier
member set. It is invisible today because `C2/8EAB` writes a number for every member the MP arm
touches, so the last member re-ORs bit1 — but a port written from §2.5's text would model a different
machine, and §3's `$0F5D` row inherits the same error.

### AV.D5 — DEFECT (attribution): the f392 `$0DBC = 00` is `C2/BEB7`, not `C2/B839`

§4.8 R6 labels `f392 $0DBC=00*` as "`** C2/B839 STZ $0DBC **`". `--writer` says otherwise, and says
it on **both** runs — the one where the caster can still pay and the one where he cannot:
```
R5 (MP left)      f392  7E0DBC=00*(C2:BEBA)     f393  7E0DBC=02*(C2:B8BC)
R6 (MP exhausted) f392  7E0DBC=00*(C2:BEBA)     (never returns)
```
`C2:BEBA` is the PC after `2BEB7: STZ $0DBC`, inside the `C2/BE79` redraw the cast tail jumps to. The
*mechanism* §1 describes is still right — `C2/B839` is what keeps the mask at 0 from f393 onward —
but the cited frame/instruction pair is wrong, and the R5-vs-R6 contrast (not the f392 line) is the
evidence that separates the two.

### AV.D6 — DEFECT (spec vs ROM, unreachable): FULL is substituted **before** the 999 clamp

Measured: `$0DC0 = $40`, max HP 2000, cur 10 → **disp 999, newHP 1009** (not 2000). The ROM order is
`amount := max` → `clamp 999` → `+ cur` → `clamp max`. §5.2's `mRestorePool` models this correctly.
The **shipped** `mItemApplyOne` (`src/menu.c:773`) does not — `int nv = (kind == ITFX_FULL) ? mx :
cur + (amt > 999 ? 999 : amt);` short-circuits straight to `mx` — so it diverges whenever effective
max HP > 999. §5.3 lists two defects in that function and this is a third. Unreachable while CT caps
max HP at 999; recorded because §5.3 claims to enumerate the divergences.

### AV.D7 — DEFECT (spec vs ROM, unreachable): the all-party loop and an empty party slot

§5.2's `CtMenuRestoreApply` does `uint8_t* sh = mem + 0x2600 + (slots[idx] & 7) * 0x50`. The ROM
(`dis.py 28820` → `C2/8839`) does:
```
28839: STA $004202 / ORA #00 / BMI ->28880      ; ** bit7 set: RETURN WITHOUT TOUCHING $6F **
```
so on a slot byte with bit7 set the ROM leaves `$6F` pointing at the **previous** member's sheet and
`C2/8E2D` applies to that member a second time, where the port would apply to charId 0. Unreachable
while every index below `$73` is occupied, but it is a different machine. (`$78` is set to `$FF` by
the same routine — `28825 STZ $78 / LDX $71 / CPX $73 / BCC + / DEC $78` — confirming §6(3)'s
ROM-derived, unmeasured reserve-caster refusal.)

### AV.M — minor corrections

1. **§2.8's "nine field-usable ids" is right about `FF/F82E` but not about a live save.** Read out of
   the mid-menu state, only **seven** of the nine carry bit7 at the moment of the gate:
   ```
   $7700: 00 00 40 00 00 00 00 00 | $7708: 00 80 40 00 80 00 00 80
   $7710: 14 40 00 00 00 00 00 00 | $7718: 00 40 80 00 00 80 00 00
   $7720: 00 40 00 00 80 00 00 80 | $7728: 00 40 00 00 00 00 00 00
   ```
   `$7721` and `$7729` read `$40`, because `FF/F8B5`'s "next tech" marker runs after `FF/F82E` and
   writes `(prop & $1E) | $40` there. Measured writers on menu open: `f57 $7709=80 (FF:F833)`,
   `f57 $7721=40 (FF:F8B8)`. No behavioural difference was found — the marker only ever lands on a
   tech the character has **not** learned, which the list never offers — but §5.4 step 2's "seed the
   same nine" describes a static table where the ROM has a three-stage build (`C2/98C6
   STA $7700,Y = $CC:0000[rec] & $1E` over all 242 **item** ids first, then the nine `TSB`s, then the
   marker). §2.8's scan conclusion ("no other writer that could **set** bit7") survives literally;
   the omission of the C2-bank writer that **clears** the whole array does not. This is consistent
   with `menu2_tech_screen.md` §AV.1, which already establishes `$7E:7700` as one shared array.
2. **§4.0/§5.3's `+0x41` values.** §5.3 says the live sheets hold "`00 FF` / `FF FF` / `00 04`" at
   `+0x41`. Read out of `saves/tech_double.state` I get `sheet0 +41,+42 = 04 04`, `sheet1 = FF FF`,
   `sheet2 = 04 05`. The substantive point (it is not a pool) is unaffected.
3. **OPEN #6(8) calls `C2/B28E` "the *status* path".** `dis.py 2B27E` shows it doing
   `LDX $0DBD / LDA $FFCF2B,X / TAY / LDA $9A9B,Y / CMP $FFCF32,X / BCS + / LDA #02 / STA $0DBC`,
   i.e. it indexes a **stat offset** into `sheet+$0B` and compares it against a **cap**
   (`$FF:CF2B = 04 00 02 01 04 05 03 06 63`, `$FF:CF32 = 06 63 10 63 …` — the `$63`/`$10` reading as
   the 99 and 16 caps). That reads as the stat-TAB (capsule) validity, not a status mask. Flagged as
   an unverified label, not refuted.
4. **`C2/BC22` re-verified** (it is the source of `$0F4F`, which §1 makes part of the contract):
   `LDA $CC253B,X` with `X = techId` on the single tab (`$0418 == 0`), then `C2/BC59` halves the cost
   once for accessory `$9ABA == $A2` and twice for `$A3`, each halving being `LSR A / ADC #00`
   (round **up**), then `STA $9890`. §5.4 step 3 should say the cost is rounded up, not just
   "`CtMenuTechMp`".
5. `--callrom … --writes` truncates its log at **64 writes** (`src/main.c:10635`), which silently
   hides the per-target stores of a full `C2/8E2D` all-party run. Use the returned `x`/`y` (they are
   the loop's last index and sheet pointer) or call the appliers individually.

## 7. PORTED (2026-09-24): the cast ON SCREEN -- the list's caster, the overlay, block 7's animation, the number

Graded by `menu-tech-cast-vs-oracle` (tools/menu_audit_cmp.py's techcast drive: Frog's Slurp on Crono, every frame
against the logical oracle, 0 of 421; six mutations, all killed). The port had the effect right (RAM) but drew none of
the cast: no overlay, no pose, no number, and the MP spent at the heal.

* **The list's caster is BLOCK 7**, a copy ($19C0): C2/BA09 `LDA $9A90 / JSR C2/F626 / ... PEA $19C0 / PLD / CMP $00 /
  BEQ / STA $00 / JSR C2/F422`, at $19CE = (x 0x1C, feet 0x38). C2/F626 is `id * 0x40` -> the block, so blocks 0-6
  are the characters' own and block 7 the run-time copy. The list hand sits at its nav record: the screen's graph
  `$FF:B7A5` (length 0x54, then [x, y, up, down, left, right]); cells 0-7 = the rows (128, 59 + 12 k), cells 8-10 the
  targets (120, 71 + 32 k).
* **The overlay, C2/BE79** (from C2/B488 on the list's A, and again after a cast): C2/BA2F redraws the caster panel,
  **yields** (C2/BE98), then clears a BG3 row, `$0D75 = 3`, C2/BEDC lays the party -- C2/A1EF's panel at $3662 +
  0x100 k (text rows 64/72/80 + 32 k, x 136), each member's own block at C2:BF2F[k] (feet 58/78/98, centre E4) --
  and queues BECE / BED5 / FC3E. The A frame's picture is still the list, with the hand already on cell 8 (C2/B471
  moved $54 there). The body (C2/B48E -> C2/B823) sets `$0D75 = 2` every frame.
* **The cast** (C2/B4DE): `$19D1 = 90`, `$19E1 = C2:B519[charId]` -- block 7's request and its +0x21: the sprite
  machine plays anim 0x10 and, at its end, **loops from frame +0x21** because the request has bit 7 (2F6E0 `LDA $11 /
  BPL / LDA $21 / STA $16`; without bit 7 it restarts at 0). Then `LDA #0A / JSR C2/822B` (10 frames), C2/8F55: the heal
  on the sheet, C2/9016's chime and number -- at each target's anchor `$0F57 = $180E` of its block, i.e. C2:BF2F here
  (C2:B045 in the Item overlay) -- and C2/8F6C waits on $E8. The number's task reads FF/CEBA's terminator and dies in
  slot 7; **slot 1 sees it on its next run**, so C2/8F55 returns a frame later: STZ $19D1, the MP (C2/B50C, 8-bit), and
  C2/BE79 again (the panels' new numbers two frames after the terminator).
* MEASURED (--monitor): block 7 at f171; $54 = 08 at f211; $0D75 = 3 at f212; 90/03 at f251; HP 25 -> 68 and $0F60 = 1
  at f261; $0F60 = 2A at f302; $19D1 = 0 and MP 17 -> 16 at f303; $0D75 = 3 at f304.

**Open:** a Dual / Triple partner's panel sprite in the list (not traced: none drawn); the overlay's B back to the list
(C2/B49C) is graded only by the same drive's return frames.
