# The TECH screen (screen 3, `C2/B353`): the THREE TABS = Single / Double / Triple

> Derived 2026-08-30. Every claim below carries a ROM address I disassembled in this pass
> (`python tools/dis.py <hexoff> <len>`) or an oracle command I ran (pasted verbatim in §4).
> Anything I could not derive is in §6.

## 1. VERDICT

The Tech screen (`$7E:0067 = 3`, sub-table `C2/B353`) is **one general list machine over a 37-entry
"tech GROUP" space**, not three different screens:

* **`$C2:BD40` is a 38-byte cumulative boundary table over 37 groups.** Groups 0-6 = the seven
  characters' **8 SINGLE** techs each (ids `$01..$38`); groups 7-21 = the fifteen character **PAIRS**,
  **3 DOUBLE** techs each (ids `$39..$65`); groups 22-36 = the fifteen character **TRIPLES**, **1 TRIPLE**
  tech each (ids `$66..$74`). 116 techs total.
* **The three tabs "1 2 3" are Single / Double / Triple**, and the byte that holds the active tab is
  **`$7E:0418`** (0/1/2). `$C2:BD65 = 00 07 | 07 0F | 16 0F` is the `(firstGroup, groupCount)` pair the
  tab indexes: tab0 -> groups 0..6, tab1 -> groups 7..21, tab2 -> groups 22..36 (`C2/BCD6-BCE5`).
* `$0418` moves **LEFT/RIGHT with wrap** in the char-select substate (`C2/B520`, clamp `C2/B533-B53D`).
* The list is rebuilt by `C2/BCCD` (pick the groups of the tab that CONTAIN the selected char, via the
  per-group character mask `$CC:2963`) -> `$7E:9890[]` FF-terminated, then `C2/BD00` (walk each group's
  id range, emit the ids whose bit is set in the menu-visible bitfield `$7E:7F00[group]`) ->
  `$7E:0F00[]` = tech ids (FF-terminated), `$7E:0F30[]` = the group per row, `$7E:0F49` = row count.
* **`$7E:2837` (37 bytes) is the real learned bitfield**; `$7E:7F00` is the MENU's copy, built by
  `FF/F863` **only for groups whose members are all in the roster**, and then `FF/F876-F8BB` **adds one
  extra, not-yet-learned tech per character** (the "next to learn", capped by `FF/F951[charId]`) and
  marks it `(prop & $1E) | $40` in `$7E:7700 + techId` so it draws **dim**.
* MP: singles = `$CC:253B + techId`. **Doubles/triples = `$CC:253B + $7E:1607[charId*64 + techId]`**
  (a per-character MP index table built at menu-open by `FF/F8BD-F94B` from `$CC:28DB`/`$CC:2935`);
  `C2/BC22` picks the branch on `$0418 != 0`. The **"MP Used"** column is that number, printed into the
  per-character panel (so a Double shows a DIFFERENT cost in each participant's panel).
* A on a tech in the FIELD menu does **not** fire it from the list: it validates
  `$7E:7700+techId` bit7 (usable outside battle) and `$9A97 >= MP`, then goes to **substate 4 = the
  target-select overlay** (same machinery as the Item screen).

**What the C engine must do:** replace `CtMenuTechList`'s single-char scan with the general
group machine (`$C2:BD40` boundaries + `$CC:2963` masks + `$C2:BD65` tab window + the `$7E:7F00`
menu-visible bitfield incl. the "+1 next tech" rule), keep a `techTab` byte, draw the 3 tab chips and
the per-character "MP Used" number, and add the description line.

---

## 2. ROM TRACE

### 2.1 The screen-3 sub-table `C2/B353` (raw `5D B3 65 B3 6D B3 AE B3 8E B4`)

Five substates; the table is 10 bytes and code starts at `C2/B35D`:

| `$68` | entry | what it is |
|---|---|---|
| 0 | `C2/B35D` | blip (`C2/EAC2`), `INC $68`, `JMP C2/B979` (the one-shot screen BUILD) |
| 1 | `C2/B365` | `LDX #C042 / JSR C2/ED31` (the declarative chrome build), `INC $68`, **falls through into 2** |
| 2 | `C2/B36D` | **CHAR-SELECT** (the hand on a party panel; the tab lives here) |
| 3 | `C2/B3AE` | **THE TECH LIST** |
| 4 | `C2/B48E` | **TARGET SELECT** (the field "use this tech on whom" overlay) |

```
; ---- substate 2: CHAR SELECT ----
2B36D: LDA $84 / STA $83          ; $83 = the previous nav signature
2B371: JSR C2/E984                ; the shared nav/input service -> $0D1D, $5A, $54
2B374: BIT $0D1D
2B377: BMI ->2B38A                ; bit7 = A pressed
2B379: BVS ->2B3AB                ; bit6 = B pressed -> 2B3AB JMP C2/9A98 (back to the ribbon)
2B37B: JSR C2/B520                ; ** the TAB + char cursor nav **
2B37E: LDA $84 / CMP $83 / BEQ +  ; the signature moved -> blip
2B384: STA $83 / JSR C2/EAC2
2B389: RTS
; A pressed:
2B38A: LDA $0F49 / BNE +          ; ** $0F49 = the built row count; 0 -> refuse **
2B38F: JMP C2/EACC                ;    C2/EACC = the BUZZ
2B392: JSR C2/EAC2                ;    blip
2B395: LDA $0F48 / STA $54        ;    restore the saved list cell
2B39A: INC $68                    ;    -> substate 3
2B39C: DEC $0D78                  ;    force the description line to redraw
2B39F: JSR C2/B9E1 / JSR C2/BB50
2B3A5: LDX #FBE3 / JMP C2/8385    ;    queue a VRAM record
; ---- substate 3: THE LIST ----
2B3AE: LDA #6D / STA $0D13        ; HDMA shadow
2B3B3: JSR C2/E984
2B3B6: BIT $0D1D / BMI ->2B3E6 (A) / BVS ->2B3C0 (B)
2B3BD: JMP C2/B6D3                ; the per-frame list nav
; B pressed -> back to char select:
2B3C0: LDA #04 / JSR C2/8255      ; kill slot 2
2B3C5: JSR C2/EAC2                ; blip
2B3C8: DEC $68                    ; -> substate 2
2B3CA: JSR C2/F566
2B3CD: LDA #FF / STA $1A18
2B3D2: LDA $0419 / CLC / ADC #0B / STA $54   ; the char-panel cell = 0x0B + charRow
2B3DA: STZ $0F4C                  ; clear the selected TECH ID
2B3DD: JSR C2/BA4F
2B3E0: LDX #FBE3 / JMP C2/8385
```

### 2.2 `C2/B520` — the TAB nav (`$7E:0418`) and the char cursor (`$7E:0419` / `$7E:041A`)

```
2B520: PHP / SEP #30
2B523: LDX $0418                  ; ** $7E:0418 = THE TAB (0 Single / 1 Dual / 2 Triple) **
2B526: LDA $5A / AND #03          ; $5A bit0 = RIGHT, bit1 = LEFT (from C2/E984)
2B52A: BEQ ->2B542                ; neither -> vertical nav only
2B52C: BIT #01 / BEQ ->2B532
2B530: INX / INX                  ; RIGHT: +2 ...
2B532: DEX                        ; ... then the shared -1  => RIGHT = +1, LEFT = -1
2B533: CPX #03 / BCC ->2B53F      ; 0..2 -> keep
2B535: BPL ->2B53D                ; X == 3      -> LDX #00   (wrap high)
2B539: LDX #02                    ; X == $FF    -> LDX #02   (wrap low)
2B53F: STX $0418
2B542: JSR C2/EA53                ; the shared up/down cell walk on the nav graph -> $54
...
2B54D: LDA $54 / SEC / SBC #0B / STA $0419   ; ** $0419 = the char ROW (nav cell - 0x0B) **
2B555: JSR C2/BC6F                ; rebuild the preview list if the signature changed
2B558: JSR C2/B6AE
2B55B: JSR C2/BE16                ; ** the bottom bar = the TAB NAME **
2B560: LDX #FBE3 / JSR C2/8385
```
(`$7E:041A` is the char-list SCROLL: `C2/B568` does `LDA $041A / DEC A / BMI skip / STA $041A` +
a `C2/B5C8` scroll animation, and the mirror at `C2/B599` increments it. The list index handed to
`C2/8820` is always `$0419 + $041A`.)

### 2.3 `C2/BC6F` — the rebuild trigger, and the SIGNATURE `$7E:0084`

```
2BC6F: LDA $0419 / CLC / ADC $041A     ; the roster list index
2BC76: ASL A x4 / ORA $0418            ; ** signature = (charIdx << 4) | tab **
2BC7D: CMP $84 / STA $84 / BNE + / RTS ; unchanged -> nothing to do
2BC84: STZ $0F4A / STZ $0F48           ; scroll := 0, saved list cell := 0
2BC8A: LDX #FBE3 / JSR C2/8385
2BC95: LDA $0419 / CLC / ADC $041A / STA $71 / JSR C2/8820   ; load the char's sheet -> $7E:9A90
2BCA1: JSR C2/BCAE                     ; rebuild the id list
2BCA4: LDA $0F4A / STA $00 / JSR C2/BD6B ; redraw the 8 rows from the scroll
```

### 2.4 `C2/BCAE` / `C2/BCCD` / `C2/BD00` — **THE LIST BUILDERS** (the answer to (a) and (b))

```
2BCAE: LDA #FFFF / STA $0F00 / MVN $7E,$7E len $15   ; $7E:0F00..0F17 := FF (blank the id list)
2BCC5: JSR C2/BCCD          ; step 1: which GROUPS
2BCC8: JSR C2/BD00          ; step 2: which TECH IDS

; ---- step 1: the groups of this TAB that contain the selected character ----
2BCCD: LDX $9A90                    ; $7E:9A90 = the loaded sheet; byte 0 = the CHAR ID
2BCD0: LDA $FFF9BB,X / STA $00      ; $00 = 0x80 >> charId   (table FF/F9BB = 80 40 20 10 08 04 02 01 00)
2BCD6: LDA $0418 / ASL A / TAX      ; ** the TAB indexes a (start,count) pair **
2BCDB: LDA $C2BD66,X / STA $01      ;   count
2BCE1: LDA $C2BD65,X / TAX          ;   first group
2BCE6: LDY #00
2BCE8: LDA $CC2963,X / AND $00      ; ** the group's PARTICIPANT MASK **
2BCEC: BEQ ->2BCF5                  ;   selected char not in this group -> skip
2BCF0: TXA / STA $9890,Y / INY      ;   append the group index
2BCF5: INX / DEC $01 / BNE ->2BCE8
2BCFA: LDA #FF / STA $9890,Y        ; FF terminator

; ---- step 2: the learned ids inside those groups ----
2BD00: STZ $00 / LDY #00
2BD04: LDX $00 / LDA $9890,X / BMI ->2BD37      ; walk the group list
2BD0B: TAX
2BD0C: LDA $7F00,X / STA $01        ; ** $7E:7F00[group] = the MENU-VISIBLE learned bitfield **
2BD11: LDA $C2BD41,X / STA $02      ; the group's END id (exclusive)
2BD17: LDA $C2BD40,X / STA $03      ; the group's FIRST id
2BD1D: ASL $01 / BCC ->2BD2B        ; MSB first
2BD21: LDA $03 / STA $0F00,Y        ; ** $7E:0F00[row] = the TECH ID **
2BD26: TXA    / STA $0F30,Y         ; ** $7E:0F30[row] = the GROUP **
2BD2A: INY
2BD2B: INC $03 / LDA $03 / CMP $02 / BCC ->2BD1D
2BD33: INC $00 / BRA ->2BD04
2BD37: STY $0F49                    ; ** $7E:0F49 = the ROW COUNT **
2BD3A: LDA #FF / STA $0F00,Y        ; FF terminator
```

**`$C2:BD65` (raw `00 07 07 0F 16 0F`) — the TAB WINDOW table:**

| `$0418` | first group | count | groups | what the bottom bar says (measured, §4.2) |
|---|---|---|---|---|
| 0 | `$00` | 7 | 0..6 = the 7 characters | **"Single Tech"** |
| 1 | `$07` | 15 | 7..21 = the 15 character PAIRS | **"Dual Tech"** |
| 2 | `$16` | 15 | 22..36 = the 15 character TRIPLES | **"Triple Tech"** |

**`$C2:BD40` (38 bytes, raw
`[38 ROM bytes at 0x2BD40: python tools/dis.py 0x2BD40 38]`)**
— group *g* owns tech ids `BD40[g] .. BD40[g+1]-1`. Groups 0-6 = 8 ids each, 7-21 = 3 each,
22-36 = 1 each (group 36's end reads `BD40[37] = $00`, and the do/while `INC $03 / CMP $02 / BCC`
therefore emits exactly the one id `$74`). **116 techs, ids `$01..$74`.**

**`$CC:2963` (37 bytes) — the per-group PARTICIPANT MASK** (bit `0x80>>charId`), raw
`80 40 20 10 08 04 02 | C0 A0 90 88 84 60 50 48 44 30 24 18 14 0C 28 | E0 D0 C8 C4 B0 A8 A4 98 94 8C 62 32 1C 64 58`.
(Note pair 21 = `$28` = Lucca+Frog is appended out of the otherwise-canonical pair order.)
`$CC:2979` (used by `FF/F914`) and `$CC:2983` (used by `FF/F97A`) are **the same table** at
`+$16` and `+$20`.

### 2.5 `FF/F813` — where `$7E:7F00` (the menu-visible bitfield) comes from

`$7E:2837` is the **real, persistent, 37-byte learned-tech bitfield** (byte *g* = group *g*, bit
`$80>>k` = the group's *k*-th id). `$7E:7F00` is the **menu copy** built once per menu-open inside
`C2/9875` -> `JSL FF/F813` (`python tools/dis.py 3FF813 0xB0`):

```
3FF816: LDX $73 ; if $7F0000 (storyline) >= $49 and $7F00BA bit0 clear -> LDX $75
3FF82A: STX $85 / STZ $86           ; $7E:0085 = the char-select LIST LENGTH
3FF82E: LDA #80 / TSB $7709 $770C $770F $771A $771D $7721 $7724 $7727 $7729
        ; ** bit7 of $7E:7700+techId = "usable OUTSIDE battle" for exactly NINE tech ids:
        ;    $09 Aura, $0C Cure, $0F Cure 2, $1A Cure Beam, $1D Heal Beam,
        ;    $21 Slurp, $24 Heal, $27 Cure 2 (Frog), $29 Kiss **
3FF84B: JSL FF/F958                 ; the 5 triple-availability flags (2.6)
3FF84F: LDA #FE / STA $00 ; for Y = 8..0: X = $2980,Y ; if >=0: TRB $00 with FFF9BB[X]
        ; ** $00 = the mask of characters NOT in the roster **
3FF863: LDX #24                     ; groups $24..$00
3FF865: LDA $CC2963,X / AND $00 / BNE ->3FF873     ; a member is ABSENT -> ** SKIP (leave $7F00 stale) **
3FF86D: LDA $2837,X / STA $7F00,X
3FF873: DEX / BPL ->3FF865
; ---- the "+1 next tech to learn" pass, characters only ----
3FF876: LDA $7F01E0 / STA $02       ; a per-char "always show" override bitfield (0 in this era)
3FF87C: LDY #00
3FF87E: LDA #08 / STA $00
3FF882: LDA $7F00,Y / LSR A / BCS + / DEC $00 / BNE ->3FF885
        ; $00 = the number of learned singles (bits are contiguous from the MSB), 0 if none
3FF88C: LSR $02 / BCS ->3FF899      ; override bit -> grant unconditionally
3FF890: TYX / LDA $00 / CMP $FFF951,X / BCS ->3FF8B8    ; count >= the cap -> no extra row
3FF899: LDX $00 / LDA $FFF9BB,X / BEQ ->3FF8B8          ; all 8 known -> stop
3FF8A1: ORA $7F00,Y / STA $7F00,Y   ; ** SHOW one unlearned tech **
3FF8A7: X = Y*8 + $00
3FF8AE: LDA $7701,X / AND #1E / ORA #40 / STA $7701,X
        ; ** $7E:7700 + techId := (old & $1E) | $40 -> bit6 = "not learned" (DIM), bit7 CLEARED **
3FF8B8: INY / CPY #07 / BCC ->3FF87E
```

`FF/F951` (7 bytes, raw `02 02 02 FF 02 FF 00`) = the per-character cap on that extra row.
`FF/F9BB` (raw `80 40 20 10 08 04 02 01 00`) = the `0x80>>n` mask table (used both for charId->bit
and for bit-index->mask).

Measured on `saves/menu_open_gato2.state` (Crono+Marle, storyline 08):
`$2837 = 00 00 00 C0 80 00 E0 00 ...`, `$7F00 = 80 80 80 80 80 80 00 00 ...` — i.e. **nobody has
actually learned anything**; every character shows exactly one greyed "next" tech, and Magus shows
none (`FFF951[6] = 00`). On `saves/com_jogo_salvo.state` (Crono/Lucca/Frog active, Marle in reserve):
`$2837 = 80 00 00 C0 80 00 E0 ...` -> `$7F00 = C0 80 80 80 C0 80 00 ...` — Crono `80|40`, Frog `80|40`,
and **Robo shows `80` although `$2837[3] = $C0`** because Robo is absent and `FF/F865` skipped his
group, leaving the stale 0 that the +1 pass then turned into `$80`. That stale-on-absent behaviour is
the ROM's, and it is invisible in play only because an absent character is not selectable.

`$7E:7700` is therefore a **shared** array: `$7700+itemId` low bits `$1E` = the item property byte
(`C2/98C6`), and `$7700+techId` bits 6/7 = the tech flags above. Measured `$7700..$770F` in the
`tk3` capture: `00 40 00 00 00 00 00 00 00 40 00 00 80 00 00 80` — id1 Cyclone `$40` (next-to-learn),
id9 Aura `$40` (next-to-learn: `F8AE` cleared the `$80` that `F82E` had set), id`$0C` Cure `$80`,
id`$0F` Cure 2 `$80`.

### 2.6 The five ROCK triples: `C2/82E1` (learn) and `FF/F958` (availability)

```
C2/82E1: STZ $2857 / MVN fill -> $7E:2857..285B := 0     ; = bitfield groups $20..$24 (32..36)
   X = $2600 ; loop 7 sheets, X += $50:
     LDA $002A,X            ; sheet+$2A = the equipped ACCESSORY id
     CMP #AE / BCC skip ; CMP #B3 / BCS skip             ; ** accessory ids $AE..$B2 **
     TAY / LDA $03F6,Y / BEQ skip                        ; $7E:03F6+accId must be non-zero
     LDA #80 / STA $27A9,Y                               ; $27A9+$AE = $2857 ... $27A9+$B2 = $285B
```
So the **five "rock" accessories `$AE..$B2`**, worn by anyone, set the single learned bit of the five
triple groups 32..36 (ids `$70` DarkEternal, `$71` Omega Flare, `$72` Spin Strike, `$73` PoyozoDance,
`$74` Grand Dream). `C2/B979` (the Tech-screen build) then copies `$7E:2857..285B -> $7E:7F20..7F24`
(raw `7B A2 57 28 A0 20 7F A9 04 54 7E 7E` = `TDC / LDX #2857 / LDY #7F20 / LDA #04 / MVN $7E,$7E`),
i.e. into the same five bytes of the menu copy — the Tech screen re-derives them on every open.

`FF/F958` builds `$7E:04A4..04A8`, one flag per rock triple:
```
3FF960: $04A3 = $FE, minus the bit of every roster member  -> the ABSENT mask
3FF976: for i = 0..4:
          STZ $04A4,X
          LDA $CC2983,X / AND $A3 / BNE skip     ; $CC:2983 = $CC:2963+$20 = groups 32..36's masks
          X' = i*3 ; three times:
            LDA $CC2953,X'      ; a prerequisite TECH id (1-based)
            t = id-1 ; Y = t>>3 ; mask = FFF9BB[t&7]
            AND $2837,Y / BEQ skip               ; ** tested against the REAL bitfield $2837 **
          INC $04A4,X
```
`$CC:2953` (15 bytes) = `0E 16 38 | 18 1B 34 | 1C 25 2E | 0A 12 2E | 10 1D 28` — the three prerequisite
tech ids per rock triple. **These five flags are NOT read anywhere in bank C2** (opcode scan for
`AD/BD/B9/2C/1C/0C/9C $04A4..$04A8` over the whole ROM finds only `C1/DAC6` and `C1/DB51`, i.e.
battle) — the tech MENU gates purely on `$7E:7F00`.

### 2.7 The MP tables — `C2/BC22` and the `$7E:1600` build (`FF/F8BD`)

```
2BC22: LDA $0418 / BNE ->2BC31          ; ** tab 0 (SINGLE): direct **
2BC2A: TDC / LDA $0F4C / TAX            ;    X = techId
2BC2F: BRA ->2BC4D
2BC31: REP #30                          ; ** tabs 1/2 (DOUBLE/TRIPLE): per-character remap **
2BC33: LDA $9A8F / AND #0F00 / LSR / LSR / PHA   ; = (charId & $0F) << 6
2BC3C: LDA $0F4C / AND #00FF / ADC $01,S / TAX
2BC46: LDA $1607,X / AND #00FF / TAX    ; ** X = $7E:1607[charId*64 + techId] **
2BC4D: LDA $CC253B,X                    ; ** the MP byte **
2BC51: JSR C2/BC59                      ; the accessory halving
2BC54: STA $9890                        ; ** $7E:9890 = the MP cost to display for THIS character **
; --- C2/BC59: the MP-halving accessory ---
2BC5C: LDY $9ABA                        ; sheet+$2A = the accessory id
2BC5F: CPY #A2 / BEQ ->2BC6A            ; $A2 -> ONE halving
2BC63: CPY #A3 / BNE ->2BC6D            ; not $A3 -> unchanged
2BC67: LSR A / ADC #00                  ; $A3 -> TWO halvings
2BC6A: LSR A / ADC #00                  ; (both round UP: `ADC #00` after `LSR`)
```

`FF/F8BD-F94B` builds `$7E:1640...` at menu-open (a byte per (character, tech) pair; the byte is an
INDEX into `$CC:253B`, not the MP itself):
```
$16 = $7E:1640 (a running pointer, +1 per tech)
for pair = 0..14:                                   ; the 15 DOUBLE groups
   a = $CC:24B4[pair*3] ; b = $CC:24B5[pair*3]      ; the two participants' charIds
   for k = 0..2:  [$16] at offset a*64 := $CC:28DB[pair*6 + k*2 + 0]
                  [$16] at offset b*64 := $CC:28DB[pair*6 + k*2 + 1]
                  $16 += 1
for tri = 0..14:                                    ; the 15 TRIPLE groups
   for each set bit `c` of $CC:2979[tri] (MSB first):
        [$16] at offset c*64 := $CC:2935[running++]
   $16 += 1
```
Since double ids start at `$39` and `$1640 = $1607 + $39`, and triple ids start at `$66` and
`$1640+45 = $166D = $1607 + $66`, the whole table is addressed exactly as `C2/BC46` reads it:
**`$7E:1607 + charId*64 + techId`**.

**Oracle-validated**: a Python replay of `FF/F8BD-F94B` reproduces `$7E:1640..17BF` of the live
`tk3.wram` **byte-for-byte** — 135 non-zero entries, 0 mismatches (§4.5).

### 2.8 The list RENDER `C2/BD6B` / `C2/BD99` / `C2/BDB2` — 8 rows, and the DIM rule

```
2BD6E: LDA #3024 / STA $61          ; row 0's VWF destination
2BD73: LDA #0008 / STA $02          ; ** EIGHT visible rows **
2BD78: LDY $00 (= $0F4A, the scroll) / LDA $0F00,Y / STA $0F4C
2BD84: LDX $61 / JSR C2/BD99
2BD89: INC $00 / $61 += $0080 / DEC $02 / BNE
; ---- one row ----
2BD9C: LDA $0F4C / BMI ->2BDA6      ; $FF = past the end
2BDA1: JSR C2/BDB2 / RTS
2BDA6: LDY #9B76 / LDA #7E / XBA / LDA #0B / JMP C2/EF65   ; blank row ($7E:9B76 is FF-filled at C2/8103)
; ---- the name + its colour ----
2BDB5: STA $4202 / TAY / LDA #0B / STA $4203     ; techId * 11
2BDC0: LDX #0C                                   ; ** $0C = DIM/grey **
2BDC2: LDA $7700,Y / BIT #40 / BNE ->2BDD5       ;   bit6 (not learned) -> keep $0C
2BDC9: LDX #00                                   ; ** $00 = normal WHITE **
2BDCB: BIT #80 / BEQ ->2BDD5                     ;   bit7 clear (not usable outside battle) -> $00
2BDCF: BIT $78 / BMI ->2BDD5                     ;   $78 = $FF (caster is a RESERVE member) -> $00
2BDD3: LDX #08                                   ; ** $08 = the bright "usable now" colour **
2BDD5: STX $7E                                   ; $7E:007E = the VWF colour selector
2BDD9: LDA #15C4 / ADC $4216 / TAY               ; ** name = $CC:15C4 + techId*11, 11 bytes **
2BDE1: LDX $61 / LDA #CC0B / JSR C2/EF65
```
All three colours were seen on the oracle (§4.3/§4.4): Cyclone learned-not-usable = white,
Slash not-learned = grey, Slurp learned+usable = bright.

### 2.9 `C2/BDEB` — the "Skill Pts / Need N" block

```
2BDEB: LDA $0418 / BNE ->2BE00     ; ** only on tab 0 (SINGLE) **
2BDF0: TDC / LDA $0F4C / TAX
2BDF5: BIT $7700,X / BVC ->2BE00   ; ** only when the highlighted tech is the NOT-LEARNED one (bit6) **
2BDFA: LDX #C026 / JSR C2/ED31
```
The `C2/ED31` list at `$FF:C026` is `08 00 2E | 06 21 00 05 09 00 | FF` (lengths from `$FF:BD18`):
`cmd $08` (`C2/F332 LDX $5B / STX $61`) sets the destination base `$61 = $2E00`; `cmd $06`
(`C2/EF48`) prints string **#$21** of the `$FF:C457` pointer table at `$63 = $61 + row*$40 + col*2`
(`C2/ED90`), row `$09`, col `$05`. String #$21 (`$FF:C86B`) decodes to
`"Skill Pts" {01} "Need " {03} BD 9A 42 7E {00}`, and control code `$03`
(handler table `$C2:EFD4`, raw `D3EF F4EF 05F0 11F0 ...` -> `C2/F011`) is *print the number at the
address that follows*: `LDA $0000,Y` = `$9ABD`, `LDA $0002,Y` = `$7E42` -> `JSR C2/F114` with bank
`$7E`, format `$42` (`AND #03` = 2 bytes).
**So "Need N" = the 16-bit value at char-sheet + `$2D`** (`$7E:9A90+$2D = $9ABD`, master copy
`$7E:2600 + charId*$50 + $2D`). Measured: `$9ABD = 0005` -> "Need 5" (Crono, TP to Cyclone) and
`$9ABD = 004B` -> "Need 75" (Crono on `com_jogo_salvo`, TP to Slash); `$7E:262D = 4B` confirms the
master copy.

### 2.10 The description bar — `C2/BE16` (tab name) and `C2/BE33` (tech description)

```
; char-select: the TAB NAME
2BE1B: LDA #6D / STA $0D13
2BE20: LDX #5356 / STX $0DC5     ; the destination
2BE26: LDA #0A / STA $0DC9
2BE2B: LDA $0418 / CLC / ADC #76 ; ** message id = $76 + tab **
2BE31: BRA ->2BE44
; the list: the TECH DESCRIPTION
2BE36: LDX #5344 / STX $0DC5     ; ** $12 bytes = 9 tiles = 72 px LEFT of the tab-name slot **
2BE3C: LDA #01 / STA $0DC9
2BE41: LDA $0F4C                 ; ** message id = the TECH ID **
; common tail
2BE44: BIT $0D78 / BMI force ; CMP $0D77 / BEQ ->2BE77   ; $0D77 = the last id drawn, $0D78 = force
2BE4E: STA $0D77 / STA $020C                             ; ** $020C = the message index **
2BE54: STZ $0D78
2BE57: LDX #6AA0 / STX $0DCC
2BE5D: LDX #0F75 / STX $0F73
2BE63: LDX #3A09 / STX $020D ; LDA #CC / STA $020F       ; ** the pointer table = $CC:3A09 **
2BE6E: LDX #0080 / STX $0DD0
2BE74: JSR C2/FA49                                       ; the string task
```
`$CC:3A09 + id*2` -> a 16-bit pointer into bank `$CC`; the string is DTE-compressed, `$00`-terminated.
Decoded: id `$01` = `"Spin-cut nearby enemies"`, id `$02` = `"Slash attack"`, id `$21` =
`"Restore ally's HP (Some)"`, id **`$76` = "Single Tech", `$77` = "Dual Tech", `$78` = "Triple Tech"** —
all four confirmed on-screen (§4.2/§4.3/§4.4).

### 2.11 The PANEL set: `C2/BB50` + `C2/BBD6` — "MP Used" is PER PARTICIPANT

In char-select (`C2/BA4F`) the three panels are the three *roster* entries from `$041A` on, and the
MP value is blanked (`C2/BA85 STZ $9890`). In the LIST (`C2/BB50`) the panels are the *participants
of the highlighted tech*:
```
2BBD7: LDX #FFFF / STX $17                   ; $17/$18 := empty
2BBE1: LDA $0F48 / CLC / ADC $0F4A / TAX     ; absolute row = cursor cell + scroll
2BBE9: LDA $0F00,X / STA $0F4C               ; the highlighted TECH ID
2BBEF: LDA $0F30,X / TAX / LDA $CC2963,X / STA $00    ; its group's participant mask
2BBF9: LDA $0419 + $041A / TAX / LDA $0D5F,X / STA $16 ; panel 0 = the SELECTED character
2BC06: TAX / LDA $FFF9BB,X / TRB $00                   ; clear his bit
2BC0D: STZ $01 / LDX #00
2BC11: ASL $00 / BCC + / LDA $01 / STA $17,X / INX     ; panels 1,2 = the other participants
2BC1A: INC $01 / LDA $00 / BNE ->2BC11
; ---- C2/BB50's panel loop ----
2BB69: LDA #08 / STA $00 ; LDY $0F49 / JSR C2/EA81     ; nav list extent = 8 rows over $0F49 entries
2BB99: LDA $16,X / BPL ->2BBA7 ; else LDX #C030 / JSR C2/ED31   (blank panel)
2BBA7: JSR C2/8816 (load that charId's sheet) / C2/BAFC (draw it)
2BBB0: JSR C2/BC22 / TXA / STA $0F6F,X          ; $7E:0F6F[panel] = the MP index
2BBB9: JSR C2/BA2F                              ; draw the value; $0D5E := 04 when $9A97 < $9890
2BBBC: $61 += $0180 ; INC $71 ; CMP #3 ; BCC
```
`C2/BA2F` also colours the number: `LDA $9A97 / CMP $9890 / BCS + / LDA #04 / STA $0D5E` — a
participant who cannot pay shows the cost in the "$04" colour.

### 2.12 Substate 4 = TARGET SELECT, and what A actually does

Entering (`C2/B3E6`, the A press in the list):
```
2B3E6: LDA $0F48 / STA $54
2B3EB: LDA $78 / BPL ->2B3F2 / JMP C2/EACC       ; (buzz) the caster is a RESERVE member
2B3F2: LDA #0C / STA $4202 ; LDA $0F4C / STA $4203 ; TAX   ; techId*12 started
2B401: LDA $7700,X / BMI ->2B409 / JMP C2/EACC   ; (buzz) not usable outside battle (bit7)
2B409: LDA $4216 / STA $00                       ; $00 = techId*12
2B413: LDA $0419 + $041A / STA $0F4D / STA $71 / JSR C2/8820    ; $0F4D = the CASTER's list index
2B422: LDA #FF / STA $0F4E
2B427: JSR C2/BC22 / STA $0F4F                   ; ** $0F4F = the MP COST **
2B42D: LDA $9A97 / CMP $0F4F / BCS + / JMP C2/EACC ; (buzz) not enough MP
2B43A: JSR C2/EAC2                               ; blip
2B43D: LDX $00 / LDA $CC2141,X / AND #C0 / STA $0DBD          ; targeting class
2B448: LDA $CC2140,X / AND #1F / STA $0DC0                    ; element/effect id
2B451: LDA $0F4C / ASL A / TAX / LDA $CC1ACB,X / AND #01 / INC A / ASL A / ASL A / ORA $0DBD / STA $0DBD
                                                 ; ** bit3 of $0DBD = "targets ALL" **
2B466: STZ $00D0 ; LDA $9AC9 / STA $0DBF
2B46F: INC $68                                   ; ** -> substate 4 **
2B471: LDA #08 / STA $54 / STA $0D9C / STA $0DBB ; the target cursor base = nav cell 8
2B47B: STZ $0DBE
2B47E: LDA $0DBD / AND #08 / BEQ + / JSR C2/8AD5  ; "all" -> the multi-marker task
2B488: JSR C2/BE79 / INC $C1
```
Per frame (`C2/B48E -> C2/B823`): `C2/B83E` walks `$54` inside `[$0DBB, $0DBB+$73)` with wrap
(`$5A` bit3 = UP -> wrap to the last member, else wrap to the first); then the caster's sheet is
re-read and `STZ $0DBC` if his MP has fallen below `$0F4F`. **B** (`C2/B49C`) blips, restores
`$54 = $0F48`, `DEC $68` back to the list, sets `$0D75 = 02`, `INC $D0`, kills slot 6 (`#40`).
**A** (`C2/B4DE`, raw `[27 ROM bytes at 0x2B4DE: python tools/dis.py 0x2B4DE 27]
[32 ROM bytes at 0x2B4F9: python tools/dis.py 0x2B4F9 32]`):
```
LDA $0DBC / BNE + / JMP C2/EACC          ; (buzz) MP no longer sufficient
LDA #90 / STA $19D1
TDC / LDA $19C0 / TAX / LDA $C2B519,X / STA $19E1     ; table C2/B519 = 03 05 04 00 03
LDA #0A / JSR C2/822B                    ; ** YIELD 10 FRAMES (the cast animation) **
JSR C2/8F55                              ; the effect
STZ $19D1
LDA $0F4D / STA $71 / JSR C2/8820 / LDX $6F
LDA $0007,X / SEC / SBC $0F4F / STA $0007,X
        ; ** the MP is deducted from the LIVE sheet $7E:2600 + charId*$50 + 7 **
JMP C2/BE79                              ; redraw the panel + MP Used
```
So a field tech is **cast from the target-select substate, not from the list**, it costs 10 frames of
scheduler yield, and it writes MP back to the persistent character record.

### 2.13 The char-panel SCROLL `$7E:041A` (`C2/B568`, raw at `2B568`)

```
LDA $5A / AND #04 / BNE ->2B595          ; $5A bit2 = DOWN
LDA $041A / DEC A / BMI ->2B598          ; already at 0 -> nothing
STA $041A ; then $22=3 $26=$2605 $61=$364A $0D22=$FFFC $24=$211F ; JSR C2/B5C8  (the slide)
2B595: LDA $041A / CLC / ADC #03 / CMP $85 / BCS ->2B5BD   ; scroll+3 >= $0085 -> nothing
INC $041A ; then $22=4 $26=$2005 $61=$3ACA $0D22=$0004 $24=$20EF ; JSR C2/B5C8
```
`$5A` bit map (from `C2/B526`, `C2/B756`, `C2/B854`): **bit0 RIGHT, bit1 LEFT, bit2 DOWN, bit3 UP.**

### 2.14 The list SCROLL `$7E:0F4A` (`C2/B6D3` / `C2/B72F`)

```
2B6D3: $71 = $0419 + $041A / JSR C2/8820        ; the sheet, every frame
2B6E2: LDA $0F4A / STA $0DD9
2B6E8: JSR C2/93BF / CMP #00 / BEQ ->2B70A      ; the L/R PAGE service
2B6EF: ... $0F4A := $0DD9 ; $00 := it ; STZ $01 ; JSR C2/BD6B ; JSR C2/821E (1 yield)
2B70A: JSR C2/EA53 / BPL ->2B712 / JSR C2/B72F  ; up/down; N set = hit the nav edge -> scroll
2B712: LDA $54 / STA $0F48                      ; ** $0F48 = the saved list cell (0..7) **
2B717: CMP $81 / BEQ + / JSR C2/EAC2            ; blip on a real move
2B71E: JSR C2/BB1F                              ; the "MP Used" value for the current panel
2B721: JSR C2/BE01 -> C2/BE33                   ; the description line
2B724: JSR C2/BDEB                              ; "Skill Pts / Need N"
; ---- C2/B72F: the edge scroll ----
2B72F: LDA $54 / BNE + / LDA $0F4A / BEQ RTS    ; top cell and scroll 0 -> nothing
2B73A: LDA $0F4A / CMP $57 / BCC ->2B742 / RTS  ; bottom clamp ($57 = the extent)
2B742: $9696=$9303 $9698=$0022 $0DAA=09 ; JSR C2/EB9B    (the 1-row slide)
2B756: LDA $5A / BIT #04 / BNE + / DEC $0F4A / BRA + / INC $0F4A
```

### 2.15 SFX census for screen 3 (byte scan of `C2/B300..C2/BF00` for `20/4C C2 EA` and `20/4C CC EA`)

| site | form | id | when |
|---|---|---|---|
| `C2/B35D` | `JSR C2/EAC2` | `$00` | the screen opens (substate 0) |
| `C2/B386` | `JSR C2/EAC2` | `$00` | char-select: the composite `$84` (charIdx<<4 \| tab) CHANGED — so a **tab move also blips** |
| `C2/B392` | `JSR C2/EAC2` | `$00` | char-select A accepted |
| `C2/B3C5` | `JSR C2/EAC2` | `$00` | list B -> char-select |
| `C2/B43A` | `JSR C2/EAC2` | `$00` | list A accepted -> target select |
| `C2/B49C` | `JSR C2/EAC2` | `$00` | target-select B -> list |
| `C2/B71B` | `JSR C2/EAC2` | `$00` | list cursor moved (`$54 != $81`) |
| `C2/B780` | `JSR C2/EAC2` | `$00` | list edge SCROLL (inside `C2/B72F`) |
| `C2/B88A` | `JSR C2/EAC2` | `$00` | target cursor moved (inside `C2/B870`) |
| `C2/B38F` | `JMP C2/EACC` | `$01` | char-select A with `$0F49 == 0` (**the list is empty**) |
| `C2/B3EF` | `JMP C2/EACC` | `$01` | list A with `$78` bit7 (**the caster is a RESERVE member**) |
| `C2/B406` | `JMP C2/EACC` | `$01` | list A, `$7700[techId]` bit7 clear (**not usable outside battle**) |
| `C2/B437` | `JMP C2/EACC` | `$01` | list A, `$9A97 < $0F4F` (**not enough MP**) |
| `C2/B4E3` | `JMP C2/EACC` | `$01` | target-select A with `$0DBC == 0` (**MP fell below the cost**) |

This closes row **E18** of `disasm/notes/menu_sfx.md` ("five `JMP C2/EACC` sites inside screen 3 ...
per-site, not individually derived here").

### 2.16 `C2/B979` — the one-shot screen BUILD (substate 0's `JMP`), raw-decoded

`python tools/dis.py` desyncs here (X is 16-bit on entry from the dispatcher `C2/828C SEP #20`).
Raw `d[0x2B979:0x2B9E1]` decoded by hand:
```
08 E2 20            PHP / SEP #20
A2 DA BF 20 31 ED   LDX #BFDA / JSR C2/ED31      ; the chrome/nav-graph description list
AD 19 04 18 69 0B 85 54    $54 = $0419 + $0B     ; the cursor lands on the char panel
EE 15 0D            INC $0D15                    ; CGRAM dirty
A9 FF 85 84 8D 78 0D  $84 = FF ; $0D78 = FF      ; force the list rebuild + the description redraw
A9 6D 8D 13 0D      $0D13 = 6D
A2 3E FC 20 85 83   LDX #FC3E / JSR C2/8385      ; three VRAM queue records
A2 FF FB 20 85 83   LDX #FBFF / JSR C2/8385
A2 1B FC 20 85 83   LDX #FC1B / JSR C2/8385
20 E1 82            JSR C2/82E1                  ; recompute the 5 rock-triple learned bits
7B A2 57 28 A0 20 7F A9 04 54 7E 7E
                    TDC / LDX #2857 / LDY #7F20 / LDA #04 / MVN $7E,$7E
                                                 ; ** $2857..$285B -> $7F20..$7F24 **
C2 30 A9 FF 21 8D 44 5D    REP #30 / $5D44 := $21FF
A2 44 5D A0 46 5D A9 15 00 54 7E 7E   MVN fill $7E:5D44..5D5B with $21FF
A2 44 5D A0 C4 5D A9 17 00 54 7E 7E   MVN copy  $7E:5D44 -> $7E:5DC4, $18 bytes
20 93 8B            JSR C2/8B93
28 4C 23 E9         PLP / JMP C2/E923
```
---

## 3. RAM CONTRACT

| address | meaning | proof |
|---|---|---|
| `$7E:0067` | screen id (`3` = Tech) | `C2/807D`; measured `03` in every capture below |
| `$7E:00BE` | the screen task's real substate (slot 1 `+6`; `$0068` is task-local — see `menu2_shell_exit.md`) | measured `02` char-select (`tk2`), `03` list (`tk3`), `04` target-select (`cjD`) |
| **`$7E:0418`** | **THE TAB: 0 Single / 1 Dual / 2 Triple** | `C2/B523 LDX $0418` … `C2/B53F STX $0418`; consumed by `C2/BCD6` (`ASL A / TAX` -> `$C2:BD65,X`), `C2/BC25` (MP branch), `C2/BDEB` (Skill-Pts gate), `C2/BE2B` (bottom-bar id), `C2/B6BC` (which chip is bright). **Measured** §4.2/§4.6: `00 -> 01 -> 02` on RIGHT, `00 -> 02` on LEFT (wrap) |
| `$7E:0419` | the char-select cursor ROW (0..2) = `$54 - $0B` | `C2/B54D LDA $54 / SEC / SBC #0B / STA $0419`; measured `02` after two Downs (`cjB`) |
| `$7E:041A` | the char-panel SCROLL (0 .. `$0085`-3) | `C2/B568` DEC / `C2/B599` INC, clamped `+3 CMP $85`; the roster index is always `$0419 + $041A` |
| `$7E:0084` | the rebuild SIGNATURE `(charIdx << 4) \| tab` | `C2/BC76-BC7F`; **measured** §4.6: `FF` at build, then `00`, `01`, `02` as the tab moves |
| `$7E:0083` | the previous `$84` (drives the char-select blip) | `C2/B36D LDA $84 / STA $83`, `C2/B380 CMP $83` |
| `$7E:0054` | the nav-graph CELL | list rows `$00..$07`, target rows `$08..$0A`, char panels `$0B..$0D` — the graph itself is at `$7E:9380`, dumped in §4.7 |
| **`$7E:0F00[]`** | the built TECH-ID list, `$FF`-terminated (24 bytes cleared by `C2/BCB1`) | `C2/BD23 STA $0F00,Y`; measured `01 FF` (Crono@sl08), `01 02 FF` (`cjA`), `21 22 FF` (Frog, `cjB`) |
| **`$7E:0F30[]`** | the GROUP index per row | `C2/BD27 STA $0F30,Y`; measured `00 00` (Crono singles), `04 04` (Frog singles) |
| **`$7E:0F49`** | the row COUNT | `C2/BD37 STY $0F49`; also the A-gate in char-select (`C2/B38A`). Measured `01` (tab0 Crono), `00` (tab1/tab2 Crono) |
| `$7E:0F48` | the saved list CELL (0..7) | `C2/B714 STA $0F48`, restored at `C2/B395` and `C2/B49F` |
| `$7E:0F4A` | the list SCROLL | `C2/B75C/B761`, consumed by `C2/BD7A` and `C2/BBE5` |
| **`$7E:0F4C`** | the highlighted TECH ID | `C2/BD7F STA $0F4C` (per row while rendering) and `C2/BBEC` (the real one); measured `02` = Slash (`cjE`), `21` = Slurp (`cjD`) |
| `$7E:0F4D` | the CASTER's roster list index (target-select) | `C2/B41A`; measured `02` (Frog, `cjD`) |
| `$7E:0F4F` | the MP cost latched for the cast | `C2/B42A`; measured `01` (Slurp) |
| `$7E:0F6F[3]` | the MP-table INDEX shown on each panel | `C2/BB27`, `C2/BBB6`; measured `01` (Cyclone) |
| **`$7E:0016/0017/0018`** | the three panel occupants in the LIST state: `$16` = the selected charId, `$17/$18` = the tech's OTHER participants (`$FF` = blank) | `C2/BBD7 LDX #FFFF / STX $17`, `C2/BC04 STA $16`, `C2/BC17 STA $17,X`; measured `00 FF FF` (Crono/Cyclone), `04 FF FF` (Frog/Slurp) |
| **`$7E:2837[0x25]`** | the REAL, persistent learned-tech bitfield, one byte per GROUP, MSB = the group's first id | `FF/F86D LDA $2837,X` (X = `$24`..0), `FF/F9A2 AND $2837,Y`, `C2/82E4` writes `$2857..285B` = groups 32..36 |
| **`$7E:7F00[0x25]`** | the MENU copy of that bitfield — the ONLY thing `C2/BD00` consults | `FF/F870 STA $7F00,X`, `FF/F8A4 STA $7F00,Y`, `C2/BD0C LDA $7F00,X`, `C2/B979`'s `MVN $2857 -> $7F20` |
| **`$7E:7700 + techId`** | per-tech flags: **bit7 = usable outside battle**, **bit6 = shown but NOT learned (dim)** | `FF/F82E TSB $7709…$7729` (9 ids), `FF/F8AE AND #1E / ORA #40`; read by `C2/BDC2` (colour), `C2/B401` (A gate), `C2/BDF5` (Skill Pts). Measured `$7700..$770F = 00 40 00 00 00 00 00 00 00 40 00 00 80 00 00 80` |
| `$7E:007E` | the VWF text COLOUR selector: `$00` normal, `$08` bright (usable now), `$0C` dim (not learned) | `C2/BDC0-BDD5`; the three cases seen on screen in §4.3/§4.4 |
| `$7E:0078` | `$FF` when the loaded roster index is a RESERVE member (`>= $73`) | `C2/882B-882D`; gates the bright colour (`C2/BDCF`) and the A press (`C2/B3EB`) |
| `$7E:0085` | char-select list LENGTH (active count `$73`, or the whole roster `$75` once storyline `>= $49`) | `FF/F82A STX $85`; measured `2` (gato2), `3` (com_jogo_salvo) |
| `$7E:0073 / $0075` | active-party / total-roster counts | `C2/8185/8187`; measured `2/2` and `3/4` |
| `$7E:0D5F[9]` | the packed charId list (roster order) | `C2/8179`; measured `00 01 FF…` and `00 02 04 01 FF…` |
| `$7E:9A90` (0x50) | the loaded character SHEET copy (`$7E:2600 + charId*$50`); `+0` charId, `+7` current MP, `+$2A` accessory, **`+$2D` (word) = TP still needed** | `C2/8854 MVN`; measured `$9A90=00`, `$9A97=08`, `$9ABA=95`, `$9ABD=0005` (tk3) and `$9ABD=004B`, `$7E:262D=4B` (cjE) |
| `$7E:9890` | scratch: the MP cost currently being drawn | `C2/BC54 STA $9890`; measured `02` = Cyclone's MP (tk3). **Also** the transient group list `C2/BCF1 STA $9890,Y` — the same bytes, consumed inside `C2/BCAE` before `BC54` overwrites them |
| `$7E:1607 + charId*64 + techId` | the per-character MP INDEX for DOUBLE/TRIPLE techs | built `FF/F8BD-F94B`, read `C2/BC46`; **replayed byte-exact against the oracle** (§4.5) |
| `$7E:04A4..04A8` | the 5 "rock triple" availability flags | `FF/F9AE INC $A4,X` (DP `$0400`); measured `$04A0..$04A3 = 05 00 00 3E` (`$A3` = the ABSENT mask = Lucca+Robo+Frog+Ayla+Magus). **No bank-C2 reader** |
| `$7E:2857..285B` | the 5 rock-triple learned bits (= `$2837 + $20..$24`) | `C2/82E4` clear + `C2/830E STA $27A9,Y` (`$27A9+$AE = $2857`) |
| `$7E:0D77 / $0D78` | the last description id drawn / force-redraw flag | `C2/BE4E`, `C2/B39C DEC $0D78`, `C2/B991 STA $0D78` |
| `$7E:020C / $020D-0F` | the message index / the `$CC:3A09` pointer table | `C2/BE51`, `C2/BE66-BE6D` |
| `$7E:0DBB` | the target-cursor BASE cell (`$08`) | `C2/B478` |
| `$7E:0DBD` | targeting class: `$CC:2141[techId*12] & $C0`, plus bit2/3 from `$CC:1ACB[techId*2] & 1` (**bit3 = targets ALL**) | `C2/B43F-B463`; measured `84` for Slurp |
| `$7E:0DBC` | "the cast is still affordable" (A gate in substate 4) | `C2/B870 STZ $0DBC`, `C2/B839 STZ $0DBC`, `C2/B4DE LDA $0DBC` |
| `$7E:0DC0` | `$CC:2140[techId*12] & $1F` (the effect/element id) | `C2/B448-B44E`; measured `05` for Slurp |
| `$7E:005A` | the nav DIRECTION bits: **bit0 RIGHT, bit1 LEFT, bit2 DOWN, bit3 UP** | `C2/B526/B52C` (tab), `C2/B756 BIT #04` (list scroll dir), `C2/B854 BIT #08` (target wrap) |

ROM tables (all read out of the ROM in this pass, none guessed):

| ROM address | contents |
|---|---|
| `$C2:BD40` (38 B) | cumulative tech-id boundary per GROUP (`[38 ROM bytes at 0x2BD40: python tools/dis.py 0x2BD40 38]`) |
| `$C2:BD65` (6 B) | the TAB window `(firstGroup, count)` (`00 07 07 0F 16 0F`) |
| `$CC:2963` (37 B) | per-group participant MASK (`$80>>charId` OR'd) |
| `$CC:2953` (15 B) | the 3 prerequisite tech ids per ROCK triple |
| `$CC:2979` = `$CC:2963+$16` | the 15 triple masks (aliased) |
| `$CC:2983` = `$CC:2963+$20` | the 5 rock-triple masks (aliased) |
| `$CC:24B3` (45 B, stride 3) | per double-PAIR record; `+1`/`+2` = the two charIds |
| `$CC:28DB` (90 B, stride 6) | per pair, 3 techs x 2 participants: the MP index |
| `$CC:2935` (45 B) | per triple, one MP index per participant, in MSB-first mask order |
| `$CC:253B` | tech MP cost, 1 byte per index |
| `$CC:15C4` | tech NAME, 11-byte mapped record per techId (the item-name table continues into it) |
| `$CC:3A09` | tech DESCRIPTION 16-bit pointer table (bank `$CC`), id `$76/$77/$78` = the tab names |
| `$CC:2140` (stride 12) | per-tech battle record; `+0 & $1F` -> `$0DC0`, `+1 & $C0` -> `$0DBD` |
| `$CC:1ACB` (stride 2) | per-tech; bit0 of `+0` = single-target(0)/all(1) |
| `$FF:F951` (7 B) | `02 02 02 FF 02 FF 00` — the per-character cap on the "next tech" preview row |
| `$FF:F9BB` (9 B) | `80 40 20 10 08 04 02 01 00` — the `$80>>n` mask table |
| `$FF:C457 + $21*2` | the "Skill Pts / Need {num@$7E:9ABD}" string |

### 3.1 The full derived tech table (from the tables above, `scratchpad/menu/techtab.py`)

```
g0  SINGLE mask=80 Crono              ids 01..08   g19 DOUBLE mask=14 Robo+Ayla        ids 5D..5F
g1  SINGLE mask=40 Marle              ids 09..10   g20 DOUBLE mask=0C Frog+Ayla        ids 60..62
g2  SINGLE mask=20 Lucca              ids 11..18   g21 DOUBLE mask=28 Lucca+Frog       ids 63..65
g3  SINGLE mask=10 Robo               ids 19..20   g22 TRIPLE mask=E0 Crono+Marle+Lucca id 66
g4  SINGLE mask=08 Frog               ids 21..28   g23 TRIPLE mask=D0 Crono+Marle+Robo  id 67
g5  SINGLE mask=04 Ayla               ids 29..30   g24 TRIPLE mask=C8 Crono+Marle+Frog  id 68
g6  SINGLE mask=02 Magus              ids 31..38   g25 TRIPLE mask=C4 Crono+Marle+Ayla  id 69
g7  DOUBLE mask=C0 Crono+Marle        ids 39..3B   g26 TRIPLE mask=B0 Crono+Lucca+Robo  id 6A
g8  DOUBLE mask=A0 Crono+Lucca        ids 3C..3E   g27 TRIPLE mask=A8 Crono+Lucca+Frog  id 6B
g9  DOUBLE mask=90 Crono+Robo         ids 3F..41   g28 TRIPLE mask=A4 Crono+Lucca+Ayla  id 6C
g10 DOUBLE mask=88 Crono+Frog         ids 42..44   g29 TRIPLE mask=98 Crono+Robo+Frog   id 6D
g11 DOUBLE mask=84 Crono+Ayla         ids 45..47   g30 TRIPLE mask=94 Crono+Robo+Ayla   id 6E
g12 DOUBLE mask=60 Marle+Lucca        ids 48..4A   g31 TRIPLE mask=8C Crono+Frog+Ayla   id 6F
g13 DOUBLE mask=50 Marle+Robo         ids 4B..4D   g32 TRIPLE mask=62 Marle+Lucca+Magus id 70
g14 DOUBLE mask=48 Marle+Frog         ids 4E..50   g33 TRIPLE mask=32 Lucca+Robo+Magus  id 71
g15 DOUBLE mask=44 Marle+Ayla         ids 51..53   g34 TRIPLE mask=1C Robo+Frog+Ayla    id 72
g16 DOUBLE mask=30 Lucca+Robo         ids 54..56   g35 TRIPLE mask=64 Marle+Lucca+Ayla  id 73
g17 DOUBLE mask=24 Lucca+Ayla         ids 57..59   g36 TRIPLE mask=58 Marle+Robo+Frog   id 74
g18 DOUBLE mask=18 Robo+Frog          ids 5A..5C
```
Per-participant MP (`$CC:253B[$7E:1607[c*64+id]]`), e.g. `39 Aura Whirl` Crono 2 / Marle 1;
`3B Ice Sword 2` Crono 12 / Marle 8; `66 Delta Force` 8/8/8; `74 Grand Dream` Marle 15 / Robo 3 /
Frog 15. The full 116-row table is reproduced by `scratchpad/menu/techtab.py` (ROM-only, no state).

---

## 4. ORACLE MEASUREMENTS

Every command below was run verbatim from the repo root; the oracle is the pre-made
`scratchpad/menu/ct_probe.exe`.

### 4.1 The captures

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/menu_open_gato2.state scratchpad/menu/tk1 --btnscript "80:2,0:10,80:2,0:30"
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/menu_open_gato2.state scratchpad/menu/tk2 --btnscript "80:2,0:10,80:2,0:30,100:2,0:60"
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/menu_open_gato2.state scratchpad/menu/tk3 --btnscript "80:2,0:10,80:2,0:30,100:2,0:60,100:2,0:60"
python tools/_fbpng.py scratchpad/menu/tk3 scratchpad/menu/tk3.png
```
* `tk1` = the ribbon PREVIEW on option 2 (Tech): the three chips **1 2 3** top-right with chip 1
  bright; both party panels carry a 4th label **"MP Used"** with no value.
* `tk2` = substate 2 (char-select): the hand on Crono's panel, "Cyclone" grey in the right list,
  bottom bar **"Single Tech"**.
* `tk3` = substate 3 (the list): only **Crono's** panel remains ("MP Used 2"), plus
  **"Skill Pts" / "Need 5"**; the right list shows "Cyclone" with the hand on it; bottom bar
  **"Spin-cut nearby enemies"**.

### 4.2 The TAB byte, and its wrap

```
... --peek saves/menu_open_gato2.state scratchpad/menu/tkR1 --btnscript "<charsel>,80:2,0:40"   (RIGHT)
... --peek saves/menu_open_gato2.state scratchpad/menu/tkR2 --btnscript "<charsel>,80:2,0:20,80:2,0:40"
... --peek saves/menu_open_gato2.state scratchpad/menu/tkL1 --btnscript "<charsel>,40:2,0:40"   (LEFT)
```
```
-- tkR1        -- tkR2        -- tkL1
7E0067 03 00   7E0067 03 00   7E0067 03 00
7E00BE 02 00   7E00BE 02 00   7E00BE 02 00
7E0418 01 ...  7E0418 02 ...  7E0418 02 ...
7E0F49 00      7E0F49 00      7E0F49 00
7E0F00 FF ...  7E0F00 FF ...  7E0F00 FF ...
```
and the rendered bottom bars: **tkR1 = "Dual Tech"** (chip 2 bright), **tkR2 = "Triple Tech"**
(chip 3 bright). LEFT from tab 0 lands on tab 2 — the wrap of `C2/B533-B53D`.

### 4.3 The list on `menu_open_gato2` (`tk3.wram`)

```
7E0067 03 00        7E0054 00 01        7E0F00 01 FF FF FF ...
7E00BE 03 00        7E0F48 00 01 00 00  7E0F30 00 00 00 00 ...
7E0418 00 00 00 4B  7E0016 00 FF FF     7E0F6F 01 00 00 00
7E9A90 00 86 80 46 00 46 00 08 00 08 ...          ($9A90=charId 0, $9A97=MP 8)
7E9AB8 5B 01 95 14 00 05 00 05                    ($9ABA=accessory $95, $9ABD=0005 -> "Need 5")
7E7700 00 40 00 00 00 00 00 00 00 40 00 00 80 00 00 80
7E2837 00 00 00 C0 80 00 E0 00 ...
7E7F00 80 80 80 80 80 80 00 00 ...
```

### 4.4 `com_jogo_salvo` — three colours and the A press

```
OPEN="0:20,200:2,0:60,80:2,0:10,80:2,0:30,100:2,0:60"
... --peek saves/com_jogo_salvo.state scratchpad/menu/cjA --btnscript "$OPEN"
... --peek saves/com_jogo_salvo.state scratchpad/menu/cjB --btnscript "$OPEN,20:2,0:20,20:2,0:30"
... --peek saves/com_jogo_salvo.state scratchpad/menu/cjC --btnscript "$OPEN,20:2,0:20,20:2,0:30,100:2,0:60"
... --peek saves/com_jogo_salvo.state scratchpad/menu/cjD --btnscript "$OPEN,20:2,0:20,20:2,0:30,100:2,0:60,100:2,0:60"
... --peek saves/com_jogo_salvo.state scratchpad/menu/cjE --btnscript "$OPEN,100:2,0:60,20:2,0:40"
```
```
-- cjA (Crono, char-select)        -- cjB (Frog picked)        -- cjC (Frog's list)
7E0418 00 00 00 4B                 7E0418 00 02 00 4B          7E00BE 03 00
7E0F00 01 02 FF ...                7E0F00 21 22 FF ...         7E0016 04 FF FF
7E0F30 00 00 ...                   7E0F30 04 04 ...            7E0054 00 0C
7E0054 0B 01                       7E0054 0D 0C
-- cjD (A on Slurp)                                 -- cjE (Crono list, cursor on Slash)
7E00BE 04 00   7E0054 08 0C                         7E00BE 03 00   7E0054 01 00
7E0F4C 21 02 FF 01     ($0F4C=$21 $0F4D=02 $0F4E=FF $0F4F=01)     7E0F4C 02 00 00 00
7E0DBB 08 02 84 00 08                               7E9ABD 4B 00   7E262D 4B 00
7E0DC0 05 00                                        7E9890 02
```
* **cjA**: "Cyclone" WHITE (learned, not field-usable -> `$7E`=`$00`), "Slash" GREY (not learned ->
  `$0C`); all three panels drawn with a blank "MP Used"; bottom bar "Single Tech".
* **cjC**: "Slurp" BRIGHT (learned + `$7721` bit7 + Frog is active -> `$08`), "Slurp Cut" GREY;
  only Frog's panel, "MP Used 1"; bottom bar **"Restore ally's HP (Some)"** = description id `$21`
  (`python assets/text.py … table 0C3A09 121` row 33 = `Restore ally's HP (Some)`).
* **cjD**: substate **4**. The right half is replaced by a slid-in 3-row target list at the nav
  cells `$08/$09/$0A` (x120, y71/103/135); the hand sits on cell `$08` (Crono). The left panel is
  still Frog with "MP Used 1", the bottom bar unchanged.
* **cjE**: cursor on the DIM "Slash" -> the **"Skill Pts / Need 75"** block appears, bottom bar
  "Slash attack", "MP Used 2".

### 4.5 The `$7E:1607` MP-index table, replayed against the oracle

A Python replay of `FF/F8BD-F94B` (reading only `$CC:24B4/24B5`, `$CC:28DB`, `$CC:2979`, `$CC:2935`)
compared against `scratchpad/menu/tk3.wram`:
```
nonzero entries: 135 mismatches: 0
oracle $7E:1640..167F: 01 04 07 01 04 07 01 04 05 01 04 05 01 03 04 00 ... 00 05 01 04 05 04 05 05 01 01 01 00 ...
mine   $7E:1640..167F: 01 04 07 01 04 07 01 04 05 01 04 05 01 03 04 00 ... 00 05 01 04 05 04 05 05 01 01 01 00 ...
oracle $7E:1680..16BF: 09 0B 0E 00 ... 0B 0E 0E 09 0B 0F 0B 0E 0F 0A 0B 0E 00 ... 0E 10 0E 0E 00 ... 0E 00 00 0A 10 00 ...
mine   $7E:1680..16BF: 09 0B 0E 00 ... 0B 0E 0E 09 0B 0F 0B 0E 0F 0A 0B 0E 00 ... 0E 10 0E 0E 00 ... 0E 00 00 0A 10 00 ...
```

### 4.6 Frame-exact: the tab move rebuilds the list AND blips

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --monitor "7E0418:1,7E0084:1,7E0D76:1,7E1E01:1,7E0F49:1" --from saves/menu_open_gato2.state --btnscript "80:2,0:10,80:2,0:30,100:2,0:40,80:2,0:40,80:2,0:40,40:2,0:40" --diff
```
```
  f46     7E0418=00  7E0084=FF*  7E0D76=03*  7E1E01=00  7E0F49=00      <- C2/B979 build + C2/B35D blip
  f47     7E0418=00  7E0084=00*  7E0D76=02*  7E1E01=00  7E0F49=01*     <- BC6F rebuild: 1 row
  f87     7E0418=01*  7E0084=01*  7E0D76=03*  7E1E01=00  7E0F49=00*    <- RIGHT: tab 1, list EMPTY, blip
  f129    7E0418=02*  7E0084=02*  7E0D76=03*  7E1E01=00  7E0F49=00
  f171    7E0418=01*  7E0084=01*  7E0D76=03*  7E1E01=00  7E0F49=00     <- LEFT: 2 -> 1
```
The tab byte, the signature, the list rebuild and the `$0D76 = 3` sound-throttle arm all land in the
SAME frame as the press. (`$7E:1E01` never changes because id `$00` is what was already latched.)

### 4.7 Layout, decoded from the `--peek` framebuffer and the nav graph

The nav graph `$7E:9380 + cell*6` = `(x, y, up, dn, lf, rt)` (identical in `tk2` and `tk3`):
```
  cell 00  x=128 y= 59  up=80 dn=01     <- list row 0 (up = $80: the EDGE that triggers the scroll)
  cell 01  x=128 y= 71  up=00 dn=02
  cell 02  x=128 y= 83   ...            list rows are y = 59 + 12*row
  cell 07  x=128 y=143  up=06 dn=87
  cell 08  x=120 y= 71  up=0A dn=09     <- target-select rows (32 px pitch, WRAP 08<->0A)
  cell 09  x=120 y=103
  cell 0A  x=120 y=135
  cell 0B  x=  8 y= 35  up=8B dn=0C     <- char panels (48 px pitch = MP_PITCH)
  cell 0C  x=  8 y= 83
  cell 0D  x=  8 y=131  up=0C dn=8D
```
The cursor OAM entry (`$7E:0770`, OAM index 28) reads `80 3B 20 30` in `tk3` = exactly `(128, 59)`,
tile `$20`, attr `$30`. In the 240-line `--peek` fb a sprite lands at `fb_y = snes_y + 9`, BG at `+8`.

Measured off the fb (`scratchpad/menu/scan2.py`, background-difference):
* **tech name row 0**: glyph box `x = 144..198`, `snes_y = 60..67`; **row 1** `snes_y = 72..79`
  -> **left x = 144, top y = 60 + 12*row, 8 rows** (matches `C2/BD73 LDA #0008`).
* **the tab chips**: content band `snes_y = 25..39`, first chip left `x = 144` — i.e. the engine's
  existing `techTabX = {144,176,208}` / `TECH_TAB_Y = 24` (16x16 each) is correct.
* **the bottom bar**: text top `snes_y = 180`; the tech description starts at **x = 24**
  ("Restore ally's HP (Some)" runs to x=199), the tab name at **x = 96** ("Single Tech" runs to
  x=159). That 72-px offset is exactly the ROM's `$0DC5` difference `$5356 - $5344 = $12` bytes
  = 9 tiles = 72 px.
* **the char panels** keep the shared geometry (`MP_X 16`, `MP_W 96`, `MP_H 48`, pitch 48), and the
  4th text row ("MP Used" + value) is the one the Tech screen adds.

---

## 5. IMPLEMENTATION SPEC (in the engine's own terms)

Files: `src/menu.c` (render + tables), `src/menu.h` (the spec comment block, lines ~150-175),
`src/field.c` (the `menuScreen == 1 || menuScreen == 3` input branch at ~L8214 and the render
dispatch at ~L8556), `src/data/menu_tech.h` (the chip tiles/palettes — already correct).

### 5.1 New state (all in `src/field.c`, next to `menuStatSub/menuStatSlot/menuStatChar`)

| C var | ROM byte | notes |
|---|---|---|
| `menuTechTab` | `$7E:0418` | 0/1/2. Persists across entering and leaving the list; reset only by the ribbon re-open |
| `menuTechScroll` | `$7E:041A` | char-panel scroll; `menuStatSlot` is `$0419`; roster index = sum |
| `menuTechRow` | `$7E:0F48` | the list cell 0..7 |
| `menuTechListScroll` | `$7E:0F4A` | list scroll |
| `menuTechTgt` | `$7E:0054 - $0DBB` | the target-select row 0..`$73`-1 (substate 4) |

`menuStatSub` grows a third value: 0 = char-select, 1 = list, 2 = target-select
(mirroring `$00BE` = 2 / 3 / 4).

### 5.2 Replace `mTechBits` + `CtMenuTechList` with the GENERAL group machine

`mTechBits(charId)` today is single-char only and skips the roster gate. The replacement is a
**37-entry** buffer built once per render (the engine has no menu-open hook, and `menu.c` already
recomputes `$7F00` from `$2837` for exactly this reason):

```
menuBits[37]:                                  // = $7E:7F00
  absent = 0xFE; for slot 0..8: c = mem[0x2980+slot]; if (!(c & 0x80)) absent &= ~(0x80>>(c&7));
  for g = 36..0:                               // FF/F863
      if (CC2963[g] & absent) continue;        // leave 0 (the ROM leaves it STALE; 0 is the
                                               //   only value the engine can honestly produce)
      menuBits[g] = mem[0x2837+g];
  for c = 0..6:                                // FF/F876
      n = 8; a = menuBits[c];
      for(;;){ int b = a & 1; a >>= 1; if (b) break; if (--n == 0) break; }
      if (n < romFF_F951[c] && n < 8) { menuBits[c] |= (0x80 >> n); techProp[8*c+n+1] = 0x40; }
  for k = 0..4: menuBits[32+k] = mem[0x2857+k]; // C2/B979's MVN
```
and `techProp[techId]` (= `$7E:7700`) starts as `0x80` for the nine ids
`{09,0C,0F,1A,1D,21,24,27,29}` (`FF/F82E`), then the loop above overwrites the "next" id with `0x40`
(bit7 cleared) — that ORDER matters, it is what makes an unlearned healing tech dim AND unusable.

`CtMenuTechList(h, charId, tab, techId[], group[], cap)` then becomes `C2/BCCD` + `C2/BD00`
verbatim:
```
first = romC2_BD65[tab*2]; cnt = romC2_BD65[tab*2+1]; mask = 0x80 >> charId;
n = 0;
for (g = first; g < first+cnt; g++) {
   if (!(CC2963[g] & mask)) continue;
   bits = menuBits[g];
   for (id = BD40[g]; id != BD40[g+1]; id++) {       // do/while: group 36 ends on BD40[37]==0
       if (bits & 0x80) { techId[n]=id; group[n]=g; n++; }
       bits = (bits << 1) & 0xFF;
   }
}
```
(the `id != BD40[g+1]` form reproduces the ROM's `INC $03 / CMP $02 / BCC` do-while, which is what
makes group 36 emit exactly one id).

### 5.3 MP

```
int techMp(charId, techId, tab, accessoryId) {
   int idx = (tab == 0) ? techId : mpIndex[charId*64 + techId];   // $7E:1607
   int mp  = romCC_253B[idx];
   if (accessoryId == 0xA3) mp = (mp + 1) >> 1;                    // two halvings, round up
   if (accessoryId == 0xA2 || accessoryId == 0xA3) mp = (mp + 1) >> 1;
   return mp;
}
```
`mpIndex[]` is built once from `$CC:24B4/24B5`, `$CC:28DB`, `$CC:2979`, `$CC:2935` exactly as §2.7
(the replay in §4.5 is the acceptance test; bake it into `menu_romdata.h` as a 7*64-byte blob).

### 5.4 Render (`CtMenuRenderTech`)

1. `mTechTabs(fb, menuTechTab)` — pass the live tab instead of the hard-coded `0`.
2. Panels: in char-select draw the roster entries `menuTechScroll + 0..2` with a **blank** MP value
   (`C2/BA85 STZ $9890`); in the list draw the **participants** of `techId[row]` —
   panel0 = the selected char, then the other set bits of `CC2963[group[row]]`, `$FF` = blank panel
   (`C2/BBD6`). `mTechMpUsed` must take a per-panel value, not a single `(slot, value)` pair.
3. List rows: 8 rows, `y = 60 + 12*i`, `x = 144`, source row `menuTechListScroll + i`; a row past
   the end draws BLANK (`C2/BDA6`). Colour per row (`C2/BDC0-BDD5`):
   `dim` if `techProp[id] & 0x40`; else `bright` if `(techProp[id] & 0x80) && !reserveMember`;
   else normal. (Today `CtMenuRenderTech` uses `y = 64`, pitch 8, no scroll, no colour.)
4. Bottom bar: a new `mDescBar(text, x)` — `x = 24` for the tech description
   (`descTable[techId]`), `x = 96` for the tab name (`descTable[0x76 + tab]`), text top `y = 180`.
   The strings come from `$CC:3A09 + id*2` and the existing `assets/text.py` dictionary decoder;
   bake the 121 decoded strings into `menu_romdata.h` the way `ctItemCat` is baked.
5. "Skill Pts / Need N": only when `menuTechTab == 0 && (techProp[id] & 0x40)`; the number is the
   16-bit `mem[0x2600 + charId*0x50 + 0x2D]`. From the fb it sits in the left panel column under
   the character block (`$61 = $2E00`, row `$09`, col `$05` of a 32-tile row -> x = 10 tiles,
   y = 9 tiles below the panel base; the fb shows "Skill Pts" and "Need" left-aligned in the
   panel column with the number right-aligned).

### 5.5 Input (`src/field.c`, the screen-3 arm)

* char-select (`menuStatSub == 0`): **LEFT/RIGHT now move `menuTechTab` with wrap 0<->2**, and every
  tab move must re-run the list build and post the blip `menuSfx(s7, 0x00, ...)` — the ROM's gate is
  "the composite `$84 = (charIdx<<4)|tab` changed" (`C2/B380`), so one predicate covers Up/Down and
  Left/Right. Up/Down at the graph edge scrolls `menuTechScroll` while `menuTechScroll+3 < $0085`.
* char-select A: refuse with the BUZZ (`menuSfx(s7, 0x01, ...)`) when the built row count is 0
  (`C2/B38F`); else blip and enter the list with `menuTechRow = 0` (`C2/BC87 STZ $0F48`).
* list: Up/Down move `menuTechRow` 0..7 and scroll at the edges; each move blips, redraws the
  description bar and the Skill-Pts block.
* list A: three refusals in this order — reserve caster (`$78`), `!(techProp[id] & 0x80)`,
  `curMp < cost` — then blip and go to target-select.
* list B: blip, back to char-select, `menuTechRow` is **kept** in `$0F48` but `$0F4C` is cleared.
* target-select: Up/Down over the active party with wrap; A refuses if MP fell below the cost, else
  blips, **holds 10 frames**, applies the effect, and subtracts the cost from
  `mem[0x2600 + casterCharId*0x50 + 7]`; B returns to the list.

### 5.6 What is WRONG in the engine today (the diff)

| `src/menu.c` / `src/field.c` | ROM |
|---|---|
| `mTechTabs(fb, 0)` at L1340 and L962 — the tab is hard-coded | `$7E:0418`, moved by LEFT/RIGHT with wrap |
| `CtMenuTechList` walks only `BD40[charId]..BD40[charId+1]` | a walk over the tab's group window (`$C2:BD65`) filtered by `$CC:2963` |
| `mTechBits` has no roster-absent gate and no rock-triple copy | `FF/F865` gate + `C2/B979`'s `$2857 -> $7F20` |
| rows at `x=144, y=64, pitch 8`, unbounded | `x=144, y=60+12*row`, **8 rows**, scrolled by `$0F4A` |
| every row drawn the same colour | `$7E:007E` = `$0C` dim / `$00` normal / `$08` bright |
| `mTechMpUsed(h, fb, -1, -1)` — labels only, one slot | a value per PARTICIPANT panel, `$04`-coloured when unaffordable |
| no description bar | `C2/BE16` / `C2/BE33`, table `$CC:3A09` |
| no "Skill Pts / Need N" | `C2/BDEB` + `$FF:C457[$21]` + sheet`+$2D` |
| A on a tech does nothing (`menuStatSub` has no state 2) | substate 4 target-select, 3 buzz gates, 10-frame cast, MP written back |
| `menu.h` L152 "SINGLE techs only (Double/Triple deferred)" and L160-165's per-char framing | the group machine above — the comment block needs rewriting, not extending |

---

## 6. OPEN / BLOCKED

1. **BLOCKED ON A CAPTURE — a non-empty Dual/Triple list has never been observed.** Every state in
   `saves/` is early game (`menu_open_gato2` = storyline `$08`, `$2837` all zero; `com_jogo_salvo`
   = Crono/Lucca/Frog, only Cyclone + Slurp learned). With `$7F00[7..36] = 0` the Dual and Triple
   tabs build **zero rows** (measured, §4.6 f87/f129), so the *rendered* layout of a double-tech row,
   the two-panel "MP Used" split, and the rock-triple path are **derived from the ROM but never
   seen**. *Unblocking capture*: a LakeSnes state anywhere after the party has learned at least one
   double tech — e.g. Crono+Marle at storyline >= the point where `$7E:2837[7] != 0` (Aura Whirl),
   or any late-game save with a rock equipped (`sheet+$2A` in `$AE..$B2`) for the triple tab.
   `saves/com_jogo_salvo.state` is the furthest-progressed state this repo owns.
2. **The list SCROLL (`$0F4A`) was never exercised** — no character in any available state has more
   than 2 rows, so the 8-row window never overflows. `C2/B72F`'s bottom clamp reads `$57`, which I
   did not derive (it is written by `C2/EA81`, the nav-graph list-extent service called at
   `C2/BB6D` with `Y = $0F49` and `$00 = 8`); the top clamp (`$54 == 0 && $0F4A == 0`) is certain.
3. **`C2/8F55` (the actual tech EFFECT) is not decoded here.** I proved the surrounding contract
   (10-frame yield, `$19D1 = $90`, `$19E1 = C2B519[$19C0]`, the MP write-back to
   `$2600+charId*$50+7`) but not the heal formula. The Item screen's use-effect is the analogous
   already-ported path; whether `C2/8F55` is the same routine was not checked.
4. **`$7E:7F00`'s stale-on-absent values.** `FF/F865` *skips* groups with an absent member instead of
   zeroing them, so the ROM can carry a bit from an earlier party. On a fresh boot those bytes are 0
   and the spec in §5.2 writes 0; a save-state that entered the menu with a different party could
   in principle differ. I could not construct that case with the available captures.
5. **`$7E:04A4..04A8`** (the 5 rock-triple availability flags) have **no bank-C2 reader** by opcode
   scan (only `C1/DAC6`, `C1/DB51`). I did not verify by execution that the menu ignores them —
   that would need a state with a rock equipped (same blocked capture as (1)).
6. **The `C2/ED31` screen-description list at `$FF:BFDA`** (the Tech screen's chrome + nav graph) is
   not decoded. The nav graph it produces is measured directly (§4.7), which is what the engine
   needs; the window/chrome tiles remain the port's reused assets, per the existing menu standard.
7. **`$7E:0F51`, `$7E:0DBE/$0DBF`, `$7E:19C0/19D1/19E1`, `C2/B519`'s 5-byte table** are named in the
   trace but their meaning was not pinned; they are only touched on the cast path.
8. **Exact VWF geometry.** `$61` is a byte address into the menu's text buffer with a 0x40-byte row
   pitch, but the list's `+$0080` per row corresponds to **12** screen pixels, not 16, so the mapping
   is not a plain 8px tile row. I did not decode `C2/EF65`'s addressing; the pixel positions in
   §4.7 are framebuffer measurements, which is the standard this port already uses for the menu.

---

## ADVERSARIAL VERIFICATION (independent pass, 2026-08-30)

> A second agent re-derived the load-bearing claims from the ROM and the oracle **without trusting the
> citations above**: every ROM address in this section was re-disassembled (`tools/dis.py`) or dumped raw
> in this pass, and every oracle line was re-run with `scratchpad/menu/ct_probe.exe`. Scratch scripts:
> `scratchpad/menu/adv_mpidx.py` (the `$7E:1607` replay) and `scratchpad/menu/adv_scan04a4.py` (the
> `$04A4` opcode scan).
> **Verdict: SOUND_WITH_FIXES.** The mechanism (the 37-group machine, the tab byte `$7E:0418`, the
> `$7E:7F00` menu bitfield, the `$7E:1607` MP-index table, the three colours, the target-select cast) is
> confirmed - several claims are now *stronger* than the dossier left them. **Four defects are in §5
> (the IMPLEMENTATION SPEC), which is the part that gets coded; one of them is refuted by measurement.**

### AV.0 The six self-declared riskiest claims - all CONFIRMED, three upgraded

| # | claim | verdict | how |
|---|---|---|---|
| 1 | `$7E:7700` is SHARED (item `prop & $1E` low bits + tech bits 6/7) | **CONFIRMED** | AV.1 |
| 2 | "Need N" is a **16-bit** read of sheet+`$2D` | **CONFIRMED (upgraded)** | AV.2 |
| 3 | the TRIPLE half of the `$7E:1607` build (FF/F914-F94F) | **CONFIRMED (upgraded: fully decoded, no reconstruction left)** | AV.3 |
| 4 | `C2/B979`'s `TDC / ... / MVN` copies **5** bytes | **CONFIRMED (upgraded: DP=0 proven, not inferred)** | AV.4 |
| 5 | the tech MENU never reads `$7E:04A4..04A8` | **REPRODUCED, method fragile** | AV.5 |
| 6 | `$5A` bit0 RIGHT / bit1 LEFT / bit2 DOWN / bit3 UP | **CONFIRMED (upgraded: all four isolated + a ROM-structural proof)** | AV.6 |

### AV.1 `$7E:7700` really is one array - proven twice

`python tools/dis.py 298A0 0x60 1 1` re-reads the item pass verbatim:

```
  298B7: SEP  #20
  298B9: LDA  $00
  298BB: JSR  C2/88AD
  298BE: LDY  $00
  298C0: LDA  $CC0000,X
  298C4: AND  #1E
  298C6: STA  $7700,Y      ; <-- item id Y = 0..$F1
  ...
  298E8: CMP  #00F2 / BCC ->298B7
```

and - the ordering the whole `$7E:7700` story depends on, which the dossier asserted but never showed -
`python tools/dis.py 29944 0x60 1 0` proves the tech pass runs **after** it, in the same routine:

```
  29969: LDA  $0D34 / BEQ / LDA #02 / TRB $77C8
  29973: JSL  FFF813        ; <-- FF/F82E's TSB #80 and FF/F8AE's ORA #40 come LAST
```

Empirical proof from `scratchpad/menu/cjA.wram` (`com_jogo_salvo`, Crono/Lucca/Frog + Marle reserve):

```
$7E:7700..772F = 00 00 40 00 00 00 00 00 00 40 00 00 80 00 00 80 14 40 00 00 00 00 00 00
                 00 40 80 00 00 80 00 00 00 80 40 00 80 00 00 80 00 40 00 00 00 00 00 00
ids with bit6/bit7 set: 02:40 09:40 0C:80 0F:80 11:40 19:40 1A:80 1D:80 21:80 22:40 24:80 27:80 29:40
```

* `$7710 = $14`, and every id past the tech range carries only values inside the `$1E` mask (`04`, `14`)
  - **item property bytes the tech pass never touched, sitting between two tech-flag bytes.** One array.
* the six `$40` ids are **exactly** the six "next to learn" the `FF/F876` pass must produce for this save:
  `$2837 = 80 00 00 C0 80 00 E0` -> Crono n=1 -> id `$02`; Marle n=0 -> `$09`; Lucca n=0 -> `$11`;
  Robo (absent, stale 0) n=0 -> `$19`; Frog n=1 -> `$22`; Ayla n=0 -> `$29`; Magus cap `$00` -> none.
  Note `$09` and `$29` read `$40`, **not** `$C0` - `FF/F8B1 AND #1E` cleared the `$80` that `FF/F82E`
  had set, exactly as §2.5 claims.
* `$7E:7F00 = C0 80 80 80 C0 80 00 ...` - the dossier's transcription is byte-exact, including the
  stale-on-absent `$7F00[3] = 80` while `$2837[3] = C0` (Robo) and `$7F00[6] = 00` while `$2837[6] = E0`
  (Magus, cap 0).

`python tools/dis.py 3FF813 0xB0 1 1` reproduces FF/F813 instruction-for-instruction as printed in §2.5,
including the nine `TSB $77xx`, the `LDX #24` group loop, the `CMP $FFF951,X` cap and `AND #1E / ORA #40`.
Traced by hand, the prefix-count loop `LDA #08 / STA $00 / LSR A / BCS / DEC $00 / BNE` yields
`$00` = 1 for `$80`, 2 for `$C0`, 0 for `$00`, 8 for `$FF` (then `FFF9BB[8] = 0` stops it) - §5.2's
count loop is equivalent. `$7F01E0 = 00` measured in `cjA.wram`, so the override path is indeed dead here.

### AV.2 "Need N" is 16-bit - and a real char sheet proves it matters

The chain re-read this pass, all four steps:

* `C2/BDFA` raw = `A2 26 C0 20 31 ED 60` = `LDX #C026 / JSR C2/ED31 / RTS` (the 8-bit dis desyncs here).
* `$FF:C026` raw = `08 00 2E 06 21 00 05 09 00 FF`. `C2/ED31` (`python tools/dis.py 2ED31 0x40 1 1`)
  reads the command byte, takes its **byte count from `$FF:BD18 + cmd`** (a BYTE table:
  `03 00 05 00 05 00 04 00 01 00 ...`), `MVN`s count+1 operand bytes to `$7E:005B`, and dispatches
  `JSR ($ED59,X)` with `X = cmd` - **so the command bytes are pre-doubled**. `C2/ED59` raw =
  `77 ED 05 EF AA ED 48 EF 32 F3 ...` -> cmd `$06` = `C2/EF48`, cmd `$08` = `C2/F332`. Both handlers
  check out: `C2/F332 = LDX $5B / STX $61` (so `$61 = $2E00` - the dossier's uncited value, now proven),
  and `C2/EF48` does `JSR C2/ED90 / LDA $5B / ASL / TAX / LDA $FFC457,X`. `C2/ED90` computes
  `$63 = ($5D>>8)*$40 + ($5D & $1F)*2 + $61`, and the operands `21 00 05 09 00` give string `$0021`,
  col `$05`, row `$09` - §2.9 verbatim.
* `$FF:C457[$21] -> $FF:C86B` raw = `B2 C4 C2 C5 C5 EF AF CD CC 01 AD BE BE BD FF 03 BD 9A 42 7E 00`
  = `"Skill Pts" {01} "Need " {03} BD 9A 42 7E {00}`.
* `C2/EFD4` = `D3EF F4EF 05F0 11F0 ...` -> code `$03` = `C2/F011`, which is
  `LDA $0000,Y / PHA / LDA $0002,Y / PLY / JSR C2/F114` - Y = `$9ABD`, A = `$7E42`.
* **`C2/F114`**: `PHA/PLB/PLB` -> DB = `$7E`; `SEP #20 / PHA / AND #03 / STA $92` -> `$92 = $42 & 3 = 2`;
  and `C2/F13F` is a per-BYTE BCD accumulator whose outer loop is **`PLY / INY / DEC $92 / BNE`** around
  an inner `LDY #0008` bit loop, with a 4-byte-stride weight table at `C2/F17A`
  (`01 00 00 00 | 02 00 00 00 | 04 ... | 96 40 00 00 | 92 81 00 00 | 84 63 01 00 ...` - sixteen weights,
  i.e. up to 32768). **`$92` is the BYTE COUNT: the read is unambiguously 16-bit.**

The oracle turns this from an academic point into a live one. From `scratchpad/menu/cjA.wram`:

```
char0 sheet+2D = 004B   char1 000A   char2 0003   char3 0005   char4 000C   char5 000A
char6 (Magus) sheet+$2D = 0190  (+2D=90 +2E=01)   <-- 400 decimal, > 255
```

An 8-bit read would print "144" for Magus. **A byte-wide port is measurably wrong on a real sheet.**

### AV.3 The triple half of the `$7E:1607` build - fully decoded, no reconstruction left

Raw `FF/F914..F950` = `[26 ROM bytes at 0x3FF914: python tools/dis.py 0x3FF914 26]
[35 ROM bytes at 0x3FF92E: python tools/dis.py 0x3FF92E 35]`,
which decodes exactly (the `28 6B` = `PLP/RTL` in the middle is the routine's exit; `B0 10` jumps *past*
it to the per-participant tail, and `80 D5` at `F94F` branches back to `F926`):

```
F914 STZ $04                 ; running index into $CC:2935
F916 STZ $06                 ; tri = 0
F918 LDX $06 / LDA $CC2979,X / STA $00        ; the triple's participant mask
F920 STZ $02 / STZ $03 / DEC $03              ; $03 = FF so the first INC gives bit index 0
F926 INC $03
F928 ASL $00 / BCS ->F93C                     ; MSB first
F92C LDA $00 / BNE ->F926
F930 INC $06 / INC $16                        ; ** $16 advances ONCE PER TRIPLE **
F934 LDA $06 / CMP #0F / BCC ->F918
F93A PLP / RTL
F93C REP #20 / LDA $02 / LSR / LSR / TAY      ; $02/$03 = bitIdx<<8  ->  Y = charId*64
F943 LDX $04 / SEP #20 / LDA $CC2935,X / STA [$16],Y
F94D INC $04 / BRA ->F926
```

That is §2.7's pseudo-code exactly. The DOUBLE half (`F8CB-F912`) also re-reads clean, including
`LDA $CC24B3,X / AND #FF00 / LSR / LSR` = `charId * 64`.

**Independent replay** (`scratchpad/menu/adv_mpidx.py`, written from this decode only, reading only
`$CC:24B4/24B5`, `$CC:28DB`, `$CC:2979`, `$CC:2935`):

```
$ python scratchpad/menu/adv_mpidx.py scratchpad/menu/tk3.wram
slots used: p ends at 167C ; run=45
nonzero=133 mismatches=0 over $1640..$17BF
oracle nonzero in $1607..$163F: []
39 AuraWhirl Crono/Marle: 2 1        3B IceSword2 Crono/Marle: 12 8
66 DeltaForce C/M/L: 8 8 8           74 GrandDream Ma/Ro/Fr: 15 3 15
```

0 mismatches, and the four spot MP values in §3.1 reproduce. (My nonzero count is 133 vs the dossier's
135 - a counting convention, not a data difference; the byte comparison is what matters.) The table
spans exactly `$1640..$167B` (60 slots = 45 doubles + 15 triples), so `$1607 + charId*64 + techId`
has **no aliasing** between adjacent characters (`c*64+116 < (c+1)*64+57`).

**A cross-check that also validates the group tables**: for all 15 pairs,
`(0x80>>CC24B4[p*3]) | (0x80>>CC24B5[p*3])` equals `CC2963[7+p]` - 15/15, 0 mismatches, including
pair 14 = `a=4 b=2` = Frog+Lucca = mask `$28`, the out-of-order one §2.4 flags.

### AV.4 The `C2/B979` `MVN` - DP = 0 is now MEASURED, not assumed

Raw `C2/B979 + 0x38` = `7B A2 57 28 A0 20 7F A9 04 54 7E 7E`; the byte alignment is self-consistent
(every `A2 xx xx` in the routine is a 3-byte `LDX #imm16` and every `20 xx xx` lands on a real routine).
`MVN`'s count is the full 16-bit C, so the copy length is `(D>>8)*256 + 5`.

The dossier *inferred* `D = 0`. It is **directly measurable, from this very routine**: `C2/B979` also
executes `AD 19 04 18 69 0B 85 54` = `$54 = $0419 + $0B`, and the oracle shows that write landing at
`$7E:0054`:

```
$ scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
    --monitor "7E00F5:1,7E005A:1,7E0418:1,7E0419:1,7E0054:1,7E041A:1,7E0067:1,7E00BE:1" \
    --from saves/menu_open_gato2.state \
    --btnscript "80:2,0:10,80:2,0:30,100:2,0:60,80:2,0:30,40:2,0:30,20:2,0:30,10:2,0:30" --diff
  f45     ... 7E0054=02   7E0067=03*  7E00BE=00*
  f46     ... 7E0054=0B*  7E0067=03   7E00BE=01*      <- C2/B979's STA $54 -> $7E:0054 = $0B
```

`$0419 = 0` at that moment, so `$54 = $0B` is written to `$7E:0054` -> **DP = $0000 inside `C2/B979`**
-> `TDC` leaves `C = 0`, `LDA #04` (M=1) leaves `C = $0004`, and the `MVN` copies **5 bytes**. Confirmed
by execution rather than by inference.

`C2/82E1` (the `JSR` immediately before; `python tools/dis.py 282E1 0x50 0 0`) also re-reads exactly as
§2.6 states: `STZ $2857` + an `MVN` fill clears the 5 bytes, then 7 sheets at stride `$50` up to `$2830`,
accessory ids `$AE..$B2`, `LDA $03F6,Y / BEQ skip`, `LDA #80 / STA $27A9,Y`.

### AV.5 `$7E:04A4..04A8` - the negative reproduces, but the method has two blind spots

`scratchpad/menu/adv_scan04a4.py` re-runs the scan **wider** than the dossier's: every absolute and long
opcode whose operand lies in `$0490..$04A9` (so an indexed read from a lower base cannot hide), plus a
census of direct-page `$90..$A8` forms. Result in bank `$C2`: **four apparent hits at `$04A4` and five
more nearby - all decode false positives.** Each was re-aligned by hand:

* `C2/0783 / 07D2 / 0853 / 089F` "`ORA $04A4,Y`" is really `85 19` (`STA $19`) + `A4 04` (`LDY $04`)
  + `B1 02` (`LDA ($02),Y`).
* `C2/9B2A` "`ROL $04A0,X`" is really `A2 00 3E` (`LDX #3E00`) + `A0 04 3E` (`LDY #3E04`).
* `C2/EC23` "`ORA $04A2`" is really `8D A9 0D` (`STA $0DA9`) + `A2 04 00` (`LDX #0004`).

Only `C1/DAC6` and `C1/DB51` survive - the dossier's two, both in battle. `FF/F958`'s DP is confirmed
(`PEA $0400 / PLD`, `python tools/dis.py 3FF958 0x60 1 1`), and it does read the REAL bitfield
(`AND $2837,Y`), not the menu copy.

**Caveat (recorded, not refuted):** a byte scan over 4 MB cannot see (a) a DP-relative read if some
bank-`$C2` routine ran with `D = $0400`, or (b) an indirect/computed access. I scanned bank `$C2` for
every `5B` (`TCD`) byte and found no `LDA #$0400 / TCD` in the menu path, which closes (a) as far as a
static method can. The negative is **not load-bearing** in any case: `C2/BD00` provably reads only
`$7F00`, and the rock-triple bits provably arrive via `C2/82E1` -> the `C2/B979` `MVN`.

### AV.6 `$5A` - all four directions isolated, plus a structural proof

The dossier called this "inferred from three consistent uses". It is now **measured one direction at a
time** on the Tech screen's char-select (same command as AV.4):

```
  f107  7E00F5=01*  7E005A=01*  7E0418=01*  ...                RIGHT (btn 0x80) -> bit0, tab 0->1
  f139  7E00F5=02*  7E005A=02*  7E0418=00*  ...                LEFT  (btn 0x40) -> bit1, tab 1->0
  f171  7E00F5=04*  7E005A=04*  7E0419=01*  7E0054=0C*         DOWN  (btn 0x20) -> bit2, cell 0B->0C
  f203  7E00F5=08*  7E005A=08*  7E0419=00*  7E0054=0B*         UP    (btn 0x10) -> bit3, cell 0C->0B
```

and it is structural, not just empirical:

* `C2/E9F0` is literally `LDA $F5 / STA $5A` - `$5A` **is** the SNES joypad high byte (bit0 Right,
  bit1 Left, bit2 Down, bit3 Up).
* `C2/E9F4-E9FC` scans bits 0..3 (`CPX #04`) into `$0D1E`, then `JSR ($EA1F,X)` with `X = $0D1E*2`.
  `C2/EA1F` raw = `45 EA 40 EA 3B EA 36 EA`, and the four handlers are
  `C2/EA45 LDA $9385,Y` / `C2/EA40 LDA $9384,Y` / `C2/EA3B LDA $9383,Y` / `C2/EA36 LDA $9382,Y`.
  With `C2/EA27` computing `Y = cell*6`, that is **+5 = rt, +4 = lf, +3 = dn, +2 = up** - which
  simultaneously proves §4.7's nav-record field order `(x, y, up, dn, lf, rt)` from the ROM.

The nav graph dumped from `tk3.wram` matches §4.7 byte for byte (cells `00..0D`), including
`cell 00 = 80 3B 80 01 00 00` (x=128, y=59, up=`$80` = the scroll edge) and the `08 <-> 0A` target wrap.
The tab wrap was re-measured in both directions: LEFT from tab 0 -> `$0418 = 02`; RIGHT from tab 2 ->
`$0418 = 00`. The signature's `<<4` half was isolated too (it had only ever been shown with charIdx 0):
selecting Marle gives `$0084 = 10`, then RIGHT gives `$11`, and her list becomes `$0F00 = 09 FF`,
`$0F30 = 01` (group 1) - the group machine, not a per-character table.

### AV.7 Other claims re-checked and CONFIRMED (no defect found)

* **Every ROM table byte re-dumped and identical**: `$C2:B353` (`5D B3 65 B3 6D B3 AE B3 8E B4`),
  `$C2:BD40` (38 B), `$C2:BD65` (`00 07 07 0F 16 0F`), `$CC:2963` (37 B), `$CC:2953` (15 B),
  `$FF:F951` (`02 02 02 FF 02 FF 00`), `$FF:F9BB`, `$C2:B519` (`03 05 04 00 03`).
* **`C2/BCAE` / `C2/BCCD` / `C2/BD00`** re-disassemble line-for-line as printed, including the `MVN`
  that FF-fills `$0F00..$0F17`, `LDX $9A90` -> `$FFF9BB,X`, and the `ASL $01` MSB-first walk.
* **`C2/B520`, `C2/B568`, `C2/B599`, `C2/BC6F`, `C2/BC22`+`C2/BC59`, `C2/BD6B`/`BD99`/`BDB2`,
  `C2/BDEB`, `C2/BE16`/`BE33`, `C2/BBD6`, `C2/B3E6..B43A`, `C2/B4DE`, `C2/B6D3`, `C2/B72F`,
  `C2/B823`/`B83E`/`B870`** - all re-disassembled, all as described. `C2/BC59`'s `LSR A / ADC #00`
  is indeed `ceil(mp/2)`, and `$A3` really is two halvings.
* **`C2/8820`** confirms both inferences the cast path rests on: `STZ $78 / LDX $71 / CPX $73 / BCC /
  DEC $78` (the reserve flag) and `LDA #2600 / ADC $4216 / STA $6F` - so `C2/B50C`'s `LDA $0007,X`
  with `X = $6F` **is** `$2600 + charId*$50 + 7`.
* **The SFX census is exact.** An independent byte scan of `C2/B300..BF00` for `20/4C C2 EA` and
  `20/4C CC EA` returns **the same 14 sites at the same addresses** as §2.15.
* **Tech names re-decoded from `$CC:15C4 + id*11`** (`assets/text.py`'s `decode_mapped`):
  `01 Cyclone, 02 Slash, 09 Aura, 0C *Cure, 0F *Cure 2, 1A Cure Beam, 1D Heal Beam, 21 Slurp,
  24 *Heal, 27 *Cure 2, 29 Kiss, 39 Aura Whirl, 3B Ice Sword 2, 66 Delta Force, 70 DarkEternal,
  71 Omega Flare, 72 Spin Strike, 73 PoyozoDance, 74 Grand Dream` - the nine `FF/F82E` ids and the five
  rock-triple names in §2.6 (which carried no citation) are now ROM-sourced.
* **The tab names.** `assets/text.py ... table 0C3A09` truncates at row 119, so `$78` was never actually
  printed by that tool. Decoded by hand from the pointer table: `$CC:3A09[$78] -> $CC:43A4 =
  B3 CB C2 C9 C5 BE EF B3 BE 6C 00` = `"Triple Te" + dict[75]` = **"Triple Tech"**. And measured live:

  ```
  $ ... --monitor "7E020C:1,7E020D:3,7E0D77:1,7E0418:1,7E0DC5:2,7E0DC9:1" --from saves/menu_open_gato2.state \
        --btnscript "80:2,0:10,80:2,0:30,100:2,0:60,80:2,0:40,80:2,0:40,100:2,0:60" --diff
    f47   7E020C=76*  7E020D=093ACC*  7E0418=00   7E0DC5=5653*  7E0DC9=0A*
    f107  7E020C=77*  7E020D=093ACC   7E0418=01*  7E0DC5=5653   7E0DC9=0A
    f149  7E020C=78*  7E020D=093ACC   7E0418=02*  7E0DC5=5653   7E0DC9=0A
  ```

  and entering the list switches to the tech id: `7E020C=01  7E0DC5=4453  7E0DC9=01`.
* **§4.3 and §4.4 are transcribed byte-exactly.** Re-dumped from `tk3.wram` and `cjA/cjC/cjD/cjE.wram`:
  `$0F4C..$0F4F = 21 02 FF 01` (cjD), `$0DBB.. = 08 02 84 00 08`, `$0DC0 = 05`, `$9ABD = 004B` (cjE),
  `$9890 = 02`, OAM `$0770 = 80 3B 20 30`, and `$7721 = 80` / `$7722 = 40` / `$0078 = 00` (cjC) - so the
  three colours (`$00` normal / `$08` bright / `$0C` dim) follow from measured RAM, not from eyeballing.
* **§5.6's engine diff is accurate**: `src/menu.c:1342` and `:964` really do call `mTechTabs(fb, 0)`;
  `CtMenuTechList` (`src/menu.c:1315`) really is a single-group walk with `k < 8`; `mTechBits`
  (`:1303`) has no roster gate; the render loop is `y = 64; y += 8` with no scroll, no colour and no
  bound; `src/data/menu_tech.h:30-31` really is `techTabX = {144,176,208}` / `TECH_TAB_Y 24`;
  `src/menu.h:152` really says "SINGLE techs only (Double/Triple deferred)".
  (Aside, not a dossier defect: `src/menu.c`'s own comments name the builder **`FF/F850`**; the entry
  point is `FF/F813` and the bit copy is at `FF/F86D`.)

### AV.8 DEFECTS FOUND

**D1 - MAJOR (refuted by measurement). §5.5's "char-select A ... enter the list with `menuTechRow = 0`
(`C2/BC87 STZ $0F48`)" is WRONG. The ROM restores the SAVED row.**

`C2/B395` is `LDA $0F48 / STA $54` - it *restores*. `C2/BC87`'s `STZ $0F48` lives inside `C2/BC6F`,
which `RTS`es immediately unless the signature `$84 = (charIdx<<4)|tab` changed, so B-ing out of the
list and pressing A again does **not** zero it. Measured on `com_jogo_salvo` (Crono has 2 rows there):

```
$ scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
    --monitor "7E00BE:1,7E0054:1,7E0F48:1,7E0F4C:1,7E0084:1" --from saves/com_jogo_salvo.state \
    --btnscript "0:20,200:2,0:60,80:2,0:10,80:2,0:30,100:2,0:60,100:2,0:60,20:2,0:40,1:2,0:50,100:2,0:60" --diff
  f189  7E00BE=03*  7E0054=00*  7E0F48=00   7E0F4C=01*  7E0084=00    <- A: enter the list, row 0 (Cyclone)
  f251  7E00BE=03   7E0054=01*  7E0F48=01*  7E0F4C=02*  7E0084=00    <- Down: row 1 (Slash)
  f293  7E00BE=02*  7E0054=0B*  7E0F48=01   7E0F4C=00*  7E0084=00    <- B: char-select, $0F48 KEPT = 1
  f345  7E00BE=03*  7E0054=01*  7E0F48=01   7E0F4C=02*  7E0084=00    <- A: RE-ENTERS AT ROW 1
```

This contradicts the dossier's own §2.1 ("restore the saved list cell"), §3 (`$0F48` ... "restored at
`C2/B395`") and §5.5's own list-B bullet. **Fix §5.5**: A enters at `menuTechRow = $0F48` as it stands;
`$0F48` (and `$0F4A`) are zeroed only when the `(charIdx<<4)|tab` signature changes.

**D2 - MAJOR (spec). §5.2's id loop is not the ROM's, and its stated justification is wrong.**

The ROM terminator is an UNSIGNED LESS-THAN do-while: `2BD2B INC $03 / LDA $03 / CMP $02 / BCC ->2BD1D`
= "loop while `id < end`". §5.2 writes `for (id = BD40[g]; id != BD40[g+1]; id++)` and justifies it as
"the `id != BD40[g+1]` form reproduces the ROM's `INC $03 / CMP $02 / BCC` do-while, which is what makes
group 36 emit exactly one id". Both halves are wrong:

* what makes group 36 emit one id is that `BD40[37] = $00` and `$75 < $00` is **false** - the `<` test,
  not the `!=` test;
* with `int id`, `id != 0` is **never** satisfied starting from `$74` - an infinite loop. Only a
  wrapping `uint8_t` terminates it, and then it spins 140 iterations (harmlessly, because `bits` has
  shifted to 0 by then - i.e. it happens to work by accident).

**Fix**: `int id = BD40[g], end = BD40[g+1]; do { ... id = (id + 1) & 0xFF; } while (id < end);` - or
equivalently walk the 8 bit positions and stop as soon as `id >= end` after the first.

**D3 - MINOR (spec). §5.2 clobbers the shared item-property bits.**

`techProp[8*c+n+1] = 0x40;` vs the ROM's `FF/F8AE LDA $7701,X / AND #1E / ORA #40 / STA $7701,X`.
§2.5 states the correct formula; §5.2 drops the `& $1E`. Because §2.5 also establishes that
`$7700 + itemId` low bits **are** the item property byte (`C2/98C6`), a plain assignment destroys the
property of item ids `{02, 09, 11, 19, 22, 29, ...}` - measured live at `$7710 = $14`, so those bits
really are non-zero in that range. **Fix**: `techProp[id] = (techProp[id] & 0x1E) | 0x40;`.
(§5.2 also silently drops the `$7F01E0` override branch; measured `00` here, so inert today, but it is
a `LSR $02 / BCS` that **bypasses the cap** when set.)

**D4 - MINOR (spec). §5.2 reads the rock-triple bits instead of re-deriving them.**

`for (k = 0..4) menuBits[32+k] = mem[0x2857+k];` treats `$2857..$285B` as an input. The ROM
**recomputes** them on *every* Tech-screen open: `C2/B979`'s `JSR C2/82E1` rescans all seven sheets'
`+$2A` accessory for ids `$AE..$B2` (gated on `$7E:03F6 + accId != 0`) and rebuilds `$2857..$285B`
*before* the `MVN` copies them to `$7F20`. A port that only reads the RAM will show stale triple
availability after the player equips or removes a rock on the Equip screen inside the same menu session
- a reachable in-game path. **Fix**: run `C2/82E1` (§2.6) at Tech-screen open, then copy.

**D5 - MINOR (citation drift).** Two §2 line labels do not hold what the text says:

* §2.2 prints `2B535: BPL ->2B53D`; `$2B535` is the `BCC ->2B53F` - the `BPL` is at `$2B537`.
* §2.13 prints `BNE ->2B595` and `BCS ->2B5BD`; the real targets are `$2B599` and `$2B5C7`.

Semantics unaffected in both cases (the wrap and the scroll clamp are as described).

**D6 - MINOR (presentation). §4.6's fenced block is filtered, not verbatim.**

Re-running the §4.6 command exactly reproduces **f46 / f47 / f87 / f129 / f171 with identical values** -
the measurement is real - but the command emits **25 rows**, not 5: every press is followed by the
`$0D76` throttle decay `3 -> 2 -> 1 -> 0` on the next three frames. Presenting a filtered subset inside
a block introduced as the real pasted output is the one habit that leaves a later reader unable to tell
a trim from a fabrication. (Also: "`$7E:1E01` never changes" - it changes once, `01 -> 00` at f3.)

**D7 - MINOR (wording). §2.5's "nobody has actually learned anything" is false as written.**

Measured `$2837` on `menu_open_gato2` is `00 00 00 C0 80 00 E0` - Robo, Frog and Magus carry learned
bits in that record. The true statement is "nobody **in the present party** (Crono, Marle) has learned
anything", which is what the rest of the paragraph then relies on.

### AV.9 One of the dossier's OPEN items is now CLOSED

**OPEN #2 - the list-scroll bottom clamp `$57`.** `C2/BB6D` calls `C2/EA81` with `$00 = 8` and
`Y = $0F49`; `python tools/dis.py 2EA81 0x40 1 1`:

```
  2EA84: LDA $00 / STA $0DD8            ; window = 8
  2EA89: TYA / SEC / SBC $00            ; rowCount - 8
  2EA8D: STA $004206 / STA $01
  2EA93: BCS ->2EA99                    ; rowCount >= 8
  2EA95: STZ $01 / STY $00              ; else scrollMax = 0
  2EAA5: LDA $01 / STA $57              ; ** $57 = max(0, rowCount - 8) **
```

So `C2/B72F`'s clamp `LDA $0F4A / CMP $57 / BCC` means "scroll down only while
`listScroll < max(0, rowCount - 8)`". Together with the certain top clamp (`$54 == 0 && $0F4A == 0`)
the whole scroll is now derived; it still cannot be *exercised* on any state this repo owns
(OPEN #1 stands - no character in any available save reaches 9 rows).

### AV.10 Observations recorded, no action required

* **`C2/BC22` writes 16 bits of `$9890` on the double/triple path.** On tab 0 the whole path is 8-bit
  (`SEP #20` at `2BC23`), so only `$9890` is written - measured `$9890 = 02 6A` for Cyclone, the `6A`
  being untouched scratch. On tabs 1/2 the `REP #30` at `2BC31` is still in force at `2BC4D`
  (`LDA $CC253B,X`) and `2BC54` (`STA $9890`), so the ROM loads **two** MP bytes and stores both.
  `$9891` is scratch, so this is inert - but a port that "returns a byte" is not literally the ROM.
* **`FF/F943 LDX $04` is a 16-bit load** (X is 16-bit from `FF/F8CB`'s `REP #30`, while `FF/F914`'s
  `STZ $04` is 8-bit under `SEP #20`), so the triple loop's index depends on `$05` being 0 on entry.
  It is, in every capture (the replay is byte-exact), but a port should use an explicit 8-bit counter.
* **`$0418` really is persistent**: the only writers in the whole ROM are `C2/B53F` (the tab nav) and
  `C2/9A5C` (`STZ $0418`, inside a block that also zeroes `$0416/$0412/$0413/$0417/$0419/$041A` - the
  ribbon/menu reset), plus three cross-bank false positives (`D3/46F9`, `EF/1DCA`, `F8/D2FB`).
* **The `$04A3` value in §3 is capture-specific**, and correct: `3E` on `menu_open_gato2`
  (absent = Lucca+Robo+Frog+Ayla+Magus) and `16` on `com_jogo_salvo` (absent = Robo+Ayla+Magus),
  both = `$FE` minus the roster bits, as `FF/F960` computes. `$04A4..$04A8 = 00 00 00 00 00` in both.

## 7. PORTED 2026-09-24: the screen's frames (tools/menu_audit_cmp.py tour f721-f876 = 0 frames differ)

* The build frame is substate 0's alone (C2/B35D -> C2/B979: the panels, the tabs, the hand; `$84` = FF, `$0D78` = FF).
  The list and the bar arrive on the next frame, substate 1 (C2/B365, the C2:C042 chrome) falling into substate 2,
  whose first C2/B520 runs C2/BC6F (the signature was FF: the list is built and drawn) and C2/BE16.
* C2/BE16 and C2/BE33 share C2/F2F3's tail and its cache (`$0D77`/`$0D78`), and hand C2/FA49 a characters-per-step
  count in `$0DC9`: 0A for the tab name (x 96), 01 for the tech description (x 24). C2/FAA5, the string task, stores it
  in `$0213` and runs C2/0009 once a frame: MEASURED "Single Tec" on the first member-select frame (f753) and the h on
  the next. The port drew both lines whole.
* C2/BDB2's third colour, `$08`: `$7700` bit 7, set by FF/F82E's `TSB $77xx` run (ids 09 0C 0F 1A 1D 21 24 27 29), not
  the next-to-learn tech, and not a reserve caster (`$78`). MEASURED on Frog's Slurp (f816-f845, yellow).

## 8. PORTED (2026-09-25): "MP Used" in red when the member cannot pay it

String 0x20 (FF:C82B, the caster panel) ends `MP Used <PAL $0D5E> <NUM $9890 f19>`. C2/BA2F: `JSR C2/A1EF / STZ $0D5E /
LDA $9A97 / CMP $9890 / BCS / LDA #04 / STA $0D5E` -- the cost in BG1 palette 1 (red) when the member's current MP
(8-bit) is below it. C2/BBB9 runs it for every participant of a Dual / Triple with that member's own cost (C2/BC22); the
screen's builds (C2/B5E6, C2/BA88) zero $9890 first. MEASURED (menu_audit_cmp.py techmp: Frog's MP poked to 0, his
list): the cost's digits in (255,33,33) / (57,0,0); 0 frames against the logical oracle (check
`menu-tech-mp-used-short-in-red-vs-oracle`). The panel's HP rules are string 06's (menu2_equip_change.md section 8).

## 9. PORTED (2026-09-25): a Dual / Triple partner's own sprite, and the tab the screen remembers

tools/menu_audit_cmp.py techdual (saves/tech_double.state + its new seed saves/field_re/tech_double.lakewram), graded
f130-f450 against the logical oracle, 0 differ (check `menu-tech-dual-list-partner-sprite-and-tab-memory-vs-oracle`).

* **The partner's sprite is its OWN block.** C2/BBA7's participant loop runs C2/BAFC per participant i: `JSR C2/F626`
  (Y = its block), `STA $1800,Y` (the char), `STZ $1818,Y` (+0x18 = 0 -- C2/F657 emits a block's OAM only then), +0x0F
  = 1C, +0x0E = C2:BB1A[i] (38 / 68 / 98). The caster shows through block 7 (C2/BA09); its own block and the members
  outside the tech keep +0x18 = 40. MEASURED (Aura Whirl): block 1 (Marle) 681C with +0x18 = 00, blocks 0 and 2 at 40,
  block 7 = Crono at 381C. The port drew only the caster.
* **$0418 (the tab) is remembered.** Only C2/9A5C (the ribbon's move, `STZ $0415 .. $041A` unless `$0D88`) and C2/B53F
  (the tab move) write it -- the Tech screen's entry does not. B back to the ribbon (C2/9B71 -> C2/B960: `$0D13` = 01,
  C2/F643, list $FF:C037) redraws no tab, so the preview shows the tab last used, and A re-enters Tech on it. The port
  reset the tab on entry and drew the preview on tab 0. (The same C2/9A45 reset now clears the port's other remembered
  cursors, $0415 / $0416 / $0417 / $0419 / $041A, on a ribbon move, as the ROM does.)

## 10. PORTED (2026-09-25): the member column over the whole roster, the "Skill Pts" block, a reserve caster

With the story byte at 0x49 (FF/F813: `$85 = $75`, the reserve joins), the Tech screen's member column is its OWN
three-row window, separate from the Equip screen's: `$0419` the row (`$54 = $0419 + 0B`, C2/B982), `$041A` the scroll.

* **The seeds** (`C2/BA4F`, run by the ribbon's Tech build and on B back from the list): the panels from `$71 =
  $041A`, `min(3, $85)` of them, blank boxes (FF:C030) for the rest; C2/EA81 over `$85` (3 a page, first cell 0B,
  `$4204` = 09AA), `$0D95 = 12 x $041A`, `$0D92` = 1278, `$0D94 = 12 x` the range. The list (`C2/BB50`, the A) re-seeds
  it for the tech list (E8/32, 1500 over `$0F49` - 8, x 4) -- always range 0 in practice (no list passes 8).
* **The cursor** (`C2/B520`): the tab (L/R), then `C2/EA53` over cells 0B-0D (FF/B7A6: an edge Up from 0B, Down from
  0D) -- no range: the edge wraps (Down -> `$0DDB`, Up -> `$0DDC`), a cell past `$0DDC` -> `$0DDB`; a range: `C2/B568`
  (Up while `$041A > 0`; Down while `$041A + 3 < $85`, 2B599 ADC #03 / CMP $85). `STZ $0D18` every frame (C2/B545).
* **`C2/B5C8`, the slide**: the incoming panel (C2/BAFC's sprite block, C2/BA2F's "MP Used"), BG1's column block-copied
  (C2/EF05 from (5,2), 10 x 18), then twelve steps of `$0D22` = +-4: `$0D30 -= $0D22` (the OAM Y the party sprites are
  drawn at), `$0DAB`, the offset-per-tile word `$24` into `$5D44` / `$5DC4` (twelve words from screen column 3; 20EF /
  211F -- bit 13 alone, BG1: the boxes stay while the text moves), `$0D95` +-1, a yield; the twelfth settles (`$0D30`
  := 0, C2/F566) and C2/B54D goes on (`$0419`, C2/BC6F's preview and blip, C2/BE16). MEASURED with `ct.exe --hdma`: the
  slide switches the HDMA set (`$0D13` = 6B) and its TM table shows BG1 and OBJ on lines 24-151 only (the trace's 25-152;
  17-160 on every settled frame) -- the sprites and text are cut 8 px inside the column.
* **`C2/BDEB`, "Skill Pts / Need N"**: every frame of the list (C2/B724), on the Single tab, when the highlighted tech has
  `$7700` bit6 (the not-learned one): C2/ED31 FF:C026 prints string #$21 at (5,9) -- "Skill Pts", a line down "Need " and
  the caster's sheet + `$2D` (C2/F114 fmt $42: five cells). Never ported before; MEASURED on Marle's list (the dim Aura,
  "Need 10"), from the list handler's first frame (none on the A's).
* **A reserve caster** (`C2/8820`: `$78` = FF when `$71 >= $73`): the field-usable techs lose their bright colour
  (C2/BDCF `BIT $78 / BMI`) and A on a tech is refused first (C2/B3EB `LDA $78 / BPL`, the buzz).

`tools/menu_audit_cmp.py techroster` 0 of 477, `techreserve` 0 of 417, `techreserve2` ($2838 = C0: Marle's Aura
learned) 0 of 417 (regress `menu-tech-whole-roster-member-select-vs-oracle`, `menu-tech-skill-pts-block-vs-oracle`,
`menu-tech-reserve-caster-vs-oracle`).
