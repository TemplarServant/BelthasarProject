# The EQUIP CHANGE flow (screen 1, `$0067 = 01`) — the interactive gear swap

Scope = the gap "the EQUIP screen is read-only": our engine draws the 4 gear slots and moves a cursor,
but **A on a slot is a no-op**. Below is the whole ROM flow: the 4-substate handler table, the
candidate-list builder, the commit, the derived-stat recompute, nav/refusals and the SFX sites.

Everything carries a ROM address I disassembled this pass (`python tools/dis.py <hexoff> <len> [m] [x]`)
or an oracle command pasted verbatim in §4. Unproven items are in §6.

---

## 1. VERDICT

Screen 1 is a **four-substate machine**, not two: `C2/9C29` = `31 9C 39 9C 70 9C A5 9C` ->
`0` build, `1` character-select, `2` **gear-slot select**, `3` **candidate list**. Our engine stops at
substate 2. `A` there is not a no-op in the ROM — it is `C2/9C88 LDA $0FC6 / BNE ... / JMP C2/EACC`:
**it enters substate 3 when the candidate count is non-zero and BUZZES (id `$01`) when it is zero**,
which is why every capture in this repo (all of them hold only consumables) sees "nothing happens".

The candidate list is `C2/8953`: walk `$7E:2400[0..0xF1]`, keep an id iff it is inside the gear row's
`[low,high)` window (`C2/89DC = 00 5A | 7B 94 | 5A 7B | 94 BC`, indexed by `$0077*2`) **and**
`$7E:7A00[id] & (0x80 >> charId)` (`C2/93A8` + `$FF:F9BB`), then selection-sort ascending by
`$7E:7C00[id]`. The commit is `C2/9CC1`: give the old piece back (`C2/8791`), write the new id into
the **live sheet** at `$0D26 = C2/A4AB[$0077] = 0x29/0x27/0x28/0x2A` (Weapon/Helm/Armor/Accessory —
`src/menu.h`'s guess, now ROM-proven), take the new piece out (`C2/87D5`), post SFX id `$B5`, and on a
WEAPON change zero `$7F:00B4 + charId`. Every cursor move first previews into the 0x50-byte **sheet
copy `$7E:9A90`** (`C2/A5C3`), so the live sheet is untouched until A. The recompute is `C2/916E` —
four equipment passes (accessory, helm, armor, weapon) into `$0DAE[7]/$0DB5/$0DB6`, seven capped stats,
a per-class attack divide, `Def = Stamina + armour`, and the accessory's max-HP scaling — written back
by `C2/9137` to **sheet `+0x03` (curHP) and `+0x36..+0x40` (7 stats, Atk, Def, maxHP word)**. Its
arithmetic reproduces the oracle's own live bytes exactly (§4.1: Atk `= (5*3+12*7)/9 = 0x0B`,
Def `= 11+8 = 0x13`, Speed `0x0C -> 0x0D` from the Bandana).

**What the C engine must do:** add a third equip substate holding `$0F00/$0F62/$0FC6/$0FC8` and the
`$9A90` sheet copy; make A on a gear row branch on the candidate count (**buzz `$01` when empty**);
implement `C2/8953`'s predicate + sort, `C2/9CC1`'s two inventory ops (`C2/8791` refuses the three
sentinels `$5A/$7B/$94` — that IS the unequip rule), and `C2/916E`/`C2/9137`'s recompute into
`+0x03` and `+0x36..+0x40`; and honour the ROM's frame costs (entry 24+1 yields, exit 26, list panel
7, plus one dead input frame on every substate change, `$0D17`).

---

## 2. ROM TRACE

### 2.0 The screen-1 substate table `C2/9C29` (raw ROM bytes)

```
$C2:9C20  96 A9 05 00 54 7E FF 28 60 | 31 9C  39 9C  70 9C  A5 9C | 20 C2 EA ...
                                        ^--------- the table @ C2/9C29 ---------^
```
(`python -c` hexdump of `Chrono_Trigger_USA_SNES.sfc` at file offset `0x29C20`, HiROM bank C2 = file
`0x20000+`.) So screen 1 has **exactly four substates**:

| `$68` | handler | what it is |
|---|---|---|
| 0 | `C2/9C31` | **BUILD** — blip, `INC $68`, `JMP C2/A11A` (the char-select layout) |
| 1 | `C2/9C39` | **CHAR SELECT** — pick the party member |
| 2 | `C2/9C70` | **GEAR-SLOT SELECT** — the 4 equipment rows (Weapon/Helm/Armor/Accessory) |
| 3 | `C2/9CA5` | **CANDIDATE LIST** — pick the replacement item; A commits |

> The dispatcher is `C2/826F` (menu2_shell_exit.md §2.4): `Y = $68*2`, `X = $67*2`,
> `A = [C2/8292+X],Y`, `JMP ($0000)`. `$68` is TASK-LOCAL: the real value lives in slot 1's `+6`
> word = **`$7E:00BE`**.

### 2.1 `C2/9C31` — substate 0, the BUILD (`python tools/dis.py 29C31 0x120 1 0`)

```
29C31: JSR C2/EAC2        ; blip $00
29C34: INC $68            ; substate := 1
29C36: JMP C2/A11A        ; the char-select layout builder
```

### 2.2 `C2/9C39` — substate 1, CHARACTER SELECT

```
29C39: JSR C2/E984                 ; the shared input prologue (2.9)
29C3C: LDA $5A / AND #01           ; $5A = $00F5 = the d-pad EDGE byte; bit0 = RIGHT
29C40: BNE ->29C4C                 ; ** RIGHT also confirms **
29C42: BIT $0D1D
29C45: BMI ->29C4C                 ; bit7 = A/confirm -> ENTER the gear panel
29C47: BVS ->29C6D                 ; bit6 = B/cancel  -> C2/9A98 (back to the ribbon)
29C49: JMP C2/9D17                 ; else: cursor movement (2.10)
--- confirm ---
29C4C: LDA #FF / STA $84           ; ** $84 := FF = force the candidate-list rebuild **
29C50: INC $68                     ; substate := 2
29C52: JSR C2/EAC2                 ; BLIP #1
29C55: JSR C2/A71E                 ; the panel slide: 24 yields (menu2_shell_exit.md 2.11)
29C58: JSR C2/A321                 ; build the gear panel; ** STZ $54 ** -> cursor on Weapon
29C5B: JSR C2/821E                 ; 1 yield
29C5E: JSR C2/EAC2                 ; BLIP #2  (= BLIP#1 + 25 frames, measured §4.2)
29C61: JSR C2/8953                 ; ** BUILD THE CANDIDATE LIST for gear slot $0077 = 0 **
29C64: JSR C2/A569                 ; draw the 8 list rows
29C67: LDX #FC1B / JMP C2/8385     ; queue the VRAM record
29C6D: JMP C2/9A98                 ; B -> the shared back-to-ribbon
```

### 2.3 `C2/9C70` — substate 2, GEAR-SLOT SELECT (the 4 equipment rows)

```
29C70: JSR C2/E984
29C73: LDA $5A / AND #02           ; bit1 = LEFT
29C77: BNE ->29C98                 ; ** LEFT also cancels **
29C79: BIT $0D1D
29C7C: BMI ->29C83                 ; A -> open the candidate list
29C7E: BVS ->29C98                 ; B -> back to char-select
29C80: JMP C2/9ECD                 ; else: slot cursor / L-R char switch (2.11)
--- A on a gear slot ---
29C83: LDA $0077 / STA $54         ; (immediately overwritten by C2/A455 LDA #04 / STA $54)
29C88: LDA $0FC6                   ; ** $0FC6 = the CANDIDATE COUNT **
29C8B: BNE ->29C90
29C8D: JMP C2/EACC                 ; ** EMPTY LIST -> BUZZ id $01, substate UNCHANGED **
29C90: JSR C2/EAC2                 ; blip $00
29C93: INC $68                     ; substate := 3
29C95: JMP C2/A41A                 ; open the list panel (7-yield slide + C2/A463)
--- B / LEFT ---
29C98: LDA #01 / STA $68           ; substate := 1
29C9C: JSR C2/EAC2                 ; blip
29C9F: JSR C2/A85C                 ; tear the gear panel down (contains 2 x C2/821E)
29CA2: JMP C2/A2CD                 ; rebuild char-select; `LDA $0412 / STA $54` restores the cursor
```

### 2.4 `C2/9CA5` — substate 3, THE CANDIDATE LIST + **THE COMMIT**

```
29CA5: JSR C2/E984
29CA8: BIT $0D1D
29CAB: BMI ->29CC1                 ; A -> COMMIT
29CAD: BVS ->29CE4                 ; B -> cancel (same tail as the commit)
29CAF: JSR C2/9F05                 ; list cursor / scroll (2.12)
29CB2: JSR C2/A5C3                 ; ** the PREVIEW: patch $9A90+$0D26 and recompute (2.16) **
29CB5: SEP #20
29CB7: LDA $81 / CMP $54 / BEQ +   ; $81 = the pre-move cursor (C2/E9E5)
29CBD: JSR C2/EAC2                 ; cursor moved -> blip
29CC0: RTS
--- A = THE EQUIP COMMIT ---
29CC1: LDA #01  / STA $04CB        ; qty := 1
29CC6: LDY $0D26                   ; ** $0D26 = the SHEET OFFSET of the gear slot (0x27/28/29/2A) **
29CC9: LDA ($6F),Y                 ; $6F -> the LIVE sheet ($7E:2600 + charId*0x50); A = the OLD item
29CCB: STA $04C9
29CCE: JSR C2/8791                 ; ** GIVE the old item back to $2400/$2500 (2.13) **
29CD1: LDA $9A90,Y                 ; ** the NEW id, out of the preview sheet copy **
29CD4: STA $04C9
29CD7: STA ($6F),Y                 ; ** WRITE THE LIVE SHEET **
29CD9: JSR C2/87D5                 ; ** TAKE the new item out of $2400/$2500 **
29CDC: LDA #B5 / JSR C2/EABA       ; ** the EQUIP SFX: cmd $18, id $B5 ** (throttled)
29CE1: JSR C2/9CFC                 ; the weapon-change side effect (2.14)
--- the shared tail (also B) ---
29CE4: JSR C2/EAC2                 ; blip
29CE7: LDA $0FC5 / STA $54         ; restore the GEAR cursor ($0FC5 is kept by C2/9ED6)
29CEC: JSR C2/A64D                 ; ** rebuild: reload the buffer, RECOMPUTE, rewrite the sheet (2.16) **
29CEF: LDA #FF / STA $84           ; force a candidate rebuild next frame
29CF3: LDA #02 / STA $68           ; substate := 2
29CF7: LDA #10 / JMP C2/8255       ; kill task slot 4
```

### 2.5 `C2/8953` — **THE CANDIDATE-LIST BUILDER** (gap (b))

`python tools/dis.py 28953 0x80 1 0`; the two tables re-read as raw bytes.

```
28953: PHP / SEP #30
28956: STZ $0FC6                    ; count := 0
28959: LDA $0077 / ASL A / TAX      ; X = gearSlot * 2
2895E: LDA $C289DC,X / STA $00      ; the id range LOW  (inclusive)
28964: LDA $C289DD,X / STA $01      ; the id range HIGH (exclusive)
2896A: LDY #00 / TYX
2896D: LDA $2400,Y / BEQ ->28994    ; empty inventory slot -> skip
28972: CMP $00 / BCC ->28994        ; id <  low  -> skip
28976: CMP $01 / BCS ->28994        ; id >= high -> skip
2897A: STA $04C9
2897D: JSR C2/93A8                  ; ** the per-character EQUIP MASK test **
28980: ORA #00 / BEQ ->28994        ; result 0 -> this character cannot wear it -> skip
28984: LDA $04C9 / STA $0F00,X      ; ** $7E:0F00[k] = the candidate item id **
2898A: LDA $2500,Y / STA $0F62,X    ; ** $7E:0F62[k] = its inventory COUNT **
28990: INC $0FC6 / INX
28994: INY / CPY #F2 / BCC ->2896D  ; inventory slots 0 .. 0xF1
28999: STZ $0F00,X                  ; 0-terminate the list
2899C..289DA: SELECTION SORT, ascending by $7E:7C00[id] (the gear "power" byte), swapping BOTH
              $0F00[] and $0F62[]:
              STZ $01
              outer: LDX $01 / LDY $0F00,X / BEQ done / $00 = $7C00,Y
              inner: INX / LDY $0F00,X / A = $7C00,Y / BEQ ->(INC $01, restart outer)
                     CMP $00 / BCC ->inner / STA $00 / swap [$01]<->[X] / BRA ->inner
```

`C2/89DC` raw = **`00 5A 7B 94 5A 7B 94 BC`** (`d[0x289DC:0x289E4]`). Indexed by `gearSlot*2` these are
four (low, high) pairs:

| `$0077` | gear row | sheet offset (`C2/A4AB`) | id range `[low, high)` | "none" id |
|---|---|---|---|---|
| 0 | **Weapon** | +0x29 | `[0x00, 0x5A)` | — (0 = none) |
| 1 | **Helm** | +0x27 | `[0x7B, 0x94)` | `0x7B` |
| 2 | **Armor** | +0x28 | `[0x5A, 0x7B)` | `0x5A` |
| 3 | **Accessory** | +0x2A | `[0x94, 0xBC)` | `0x94` |

`C2/A4AB` raw = **`29 27 28 2A`** (`d[0x2A4AB:0x2A4AF]`), read by `C2/A47E LDX $0077 / LDA $C2A4AB,X /
STA $0D26`. **This is the ROM proof of `src/menu.h`'s "Weapon +0x29, Helm +0x27, Armor +0x28,
Accessory +0x2A" — verified.**

The same four boundaries appear independently in the item CLASSIFIER `C2/8881`, whose table
`C2/88A7` raw = **`00 5A 7B 94 BC F2`**: class 0 weapon, 1 armor, 2 helm, 3 accessory, 4 consumable;
`$04CC = class`, `$04CE = id - C2/88A7[class]`. Cross-checked against the ROM's own item names
(`$CC:0B5E + id*11`): `01 Wood Sword`, **`5A` blank**, `5B Hide Tunic`, `7A Taban Suit`,
**`7B` blank**, `7C Hide Cap`, `93 MermaidCap`, **`94` blank**, `95 Bandana`, `BB PrismSpecs`,
**`BC` blank**, `BD Tonic`. The three blanks ARE the "nothing equipped" sentinels.

### 2.6 `C2/93A8` — **THE EQUIP PREDICATE** (one item, one character)

Raw bytes `d[0x293A8:0x293BF]` = `DA 08 E2 30 AE C9 04 BD 00 7A 8D 71 0D AE 90 9A 3F BB F9 FF 28 FA 60`:

```
293A8: PHX / PHP / SEP #30
293AC: LDX $04C9                    ; X = the item id (8-bit)
293AF: LDA $7A00,X                  ; ** $7E:7A00[id] = the item's EQUIP-BY-CHARACTER BITMASK **
293B2: STA $0D71                    ; (spilled)
293B5: LDX $9A90                    ; ** $7E:9A90+0 = the sheet copy's byte 0 = the charId **
293B8: AND $FFF9BB,X                ; ** $FF:F9BB = 80 40 20 10 08 04 02 01 00 = the bit (0x80 >> charId) **
293BC: PLP / PLX / RTS              ; A == 0 -> NOT equippable
```

**The predicate, in one line:**
`equippable(id, charId)  <=>  ($7E:7A00[id] & (0x80 >> charId)) != 0`
(with `id` already inside the gear row's `[low,high)` range and `$2400[slot] != 0`).

Measured on the oracle's live page (`eq0.raw.wram`, §4.1): `$7A00[0x01] = 0x80` (Wood Sword: Crono
only), `$7A00[0x11..0x1A] = 0x40` (bows: Marle only), `$7A00[0x1F..0x20] = 0x20` (guns: Lucca only),
`$7A00[0x5B] = $7A00[0x7C] = $7A00[0x95] = 0xFE` (Hide Tunic / Hide Cap / Bandana: charIds 0..6).

### 2.7 The ITEM PROPERTY PAGES `$7E:7700/7A00/7B00/7C00` — built at menu OPEN

`C2/9875` (menu2_shell_exit.md §2.2 [I]) + the locators `C2/88AD` / `C2/88D8`:

```
C2/8881(id): class = the first k with id < C2/88A7[k+1]   (C2/88A7 = 00 5A 7B 94 BC F2)
             $04CC = class ; $04CE = id - C2/88A7[class]
C2/88AD:     X  = $FF:CF0E[class]  + $04CE * $FF:CF18[class]   -> $0D28
C2/88D8:     X2 = $FF:CF1D[class]  + $04CE * $FF:CF27[class]   -> $0D2A
```
`$FF:CF0E` (5 words) = `06A4 08C0 0986 0A1C 0ABC`, `$FF:CF18` (5 bytes) = `06 06 06 04 03`,
`$FF:CF1D` (5 words) = `0262 047E 04E1 052C 05CC`, `$FF:CF27` (5 bytes) = `05 03 03 04 04`.
Both X and X2 index bank **`$CC`**.

The build, in order (`C2/98B5`, `C2/9906`, `C2/992B`, `C2/994C`, `FF/F82E`, `C2/9969`):
1. `for id 0..0xF1`: `$7700[id] = $CC:0000+X & 0x1E`; **`$7A00[id] = $CC:0003+X`**;
   `$7B00[id] = $CC:0004+X`; `$7800[id*2] = word $CC:0001+X`
2. zero `$7A00[0xBC..0xEF]` and `$7B00[0x94..0xEF]`
3. `for id 0..0xBB`: `if ($CC:0001+X & 0x40) then $7B00[id] = $CC:0002+X`
4. `for id 0..0x93`: **`$7C00[id] = $CC:0000 + X2`**
5. `FF/F82E`: `LDA #80 / TSB` at `$7709 $770C $770F $771A $771D $7721 $7724 $7727 $7729`
   (raw `d[0x3FF82E:0x3FF84C]` = `A9 80 0C 09 77 0C 0C 77 0C 0F 77 0C 1A 77 0C 1D 77 0C 21 77 0C 24
   77 0C 27 77 0C 29 77 22`)
6. `if $0D34 != 0`: `LDA #02 / TRB $77C8`

**Validated byte-for-byte against the oracle's live WRAM** (§4.1): `$7C00[0..0x93]` = **0 mismatches
in 148**; `$7A00[0..0xF1]` = 2 (ids `0xF0/0xF1`, outside every gear range — the zero-fill of step 2 in
fact reaches them); `$7B00[0..0xF1]` = 64, all of the same kind (the model leaves a value where the
live page is 0), and none of them at an id the equip path reads; `$7700` = 6 (ids
`02 09 11 19 22 29` carry an extra `0x40` from a writer I did not locate — **OPEN**, and irrelevant
here: the equip predicate never reads `$7700`).

### 2.8 `C2/8839` — **THE SHEET COPY `$7E:9A90`** (what "the selected character" is)

```
28839: STA $004202 / ORA #00 / BMI ->28880    ; A = charId; bit7 (empty slot) -> return
28841: $004203 = #50                          ; charId * 0x50
28848: REP #31
2884A: LDA #2600 / ADC $004216
28851: STA $6F                                ; ** $00:006F = the LIVE sheet pointer $2600+charId*0x50 **
28853: TAX / LDY #9A90 / LDA #004F / MVN $7E,$7E   ; ** 0x50 bytes  sheet -> $7E:9A90 **
2885D: SEP #20
2885F: LDA $9A90 / AND #07 -> $4202 ; $4203 = 6
2886E: REP #21 / LDA #2C23 / ADC $4216 / TAX
28879: LDA #0005 / MVN $7E,$7E                ; 6 name bytes $2C23+charId*6 -> $7E:9AE0
```
`C2/8820` is the wrapper the menu uses: `STZ $78 / LDX $71 / CPX $73 / BCC + / DEC $78 /
LDA $0D5F,X / JSR C2/8839` — i.e. **`$71` (the ROSTER index) -> charId via `$7E:0D5F[]`**, and
`$78 = FF` when the member is a reserve (index >= `$73`).

Measured (`eq0.raw.wram`): `$7E:9A90 .. $9AE5` = Crono's sheet verbatim followed by
`A2 CB C8 C7 C8 00` (his name). The buffer stride is therefore **0x56**, and `$9AE6` (used as a base
in `C2/A5E7`) is the scratch immediately above it, so `$9AE6 + off` for `off = 0x36..0x40` is exactly
`$9B1C..$9B26` — the freshly recomputed derived block.

### 2.9 `C2/E984` / `C2/E9E2` — the shared input prologue

```
2E984: JSR C2/E9E2                     ; the CURSOR MOVER (below)
2E987: PHP / SEP #30
2E98A: STZ $0D1D
2E98D: LDA $68 / CMP $0D17 / STA $0D17
2E995: BNE ->2E9CD                     ; ** substate CHANGED this frame -> NO button edges at all **
2E997: LDA #80 / STA $00 / LDX #00
2E99D: Y = $C2:E9D9[X]  (raw table = 00 01)
2E9A2: LDA $2993,Y / AND $F0           ; $F0 = the pure pad EDGE; $7E:2993/$2994 = the A/B remap masks
2E9A7: BEQ + / LDA $00 / TSB $0D1D     ; -> $0D1D bit7 (confirm), then bit6 (cancel)
2E9B5: LDA $F4 / AND $2999 / BNE -> $0D1D |= 0x02     ; ** bit1 = R **
2E9C1: LDA $F4 / AND $299A / BNE -> $0D1D |= 0x01     ; ** bit0 = L **
2E9CD: LDA $0D83 / BNE -> $0D1D |= 0x40               ; a forced cancel
```
```
2E9E2: PHP / SEP #30
2E9E5: LDA $54 / STA $81               ; ** $81 = the PRE-move cursor **
2E9E9: LDA #80 / STA $0D1E
2E9F0: LDA $F5 / STA $5A               ; ** $5A = $00F5 = the d-pad edge+repeat byte **
2E9F4: LSR A / BCS ->2E9FE / INX / CPX #04 / BCC   ; bit0 Right->0, bit1 Left->1, bit2 Down->2, bit3 Up->3
2E9FE: STX $0D1E
2EA01: LDA $0D1F / BNE ->2EA1A         ; movement suppressed for this frame
2EA06: LDA $54 / JSR C2/EA27           ; X = cell*6   (C2/EA27 = A*2 then A*4 + A*2)
2EA0C: LDA $54 / STA $55
2EA10: LDA $0D1E / ASL / TAX / REP #10 / JSR ($EA1F,X)
2EA1A: STZ $0D1F
```
`C2/EA1F` raw = `45 EA 40 EA 3B EA 36 EA` -> dir 0 Right = `C2/EA45` (**graph +5**), 1 Left =
`C2/EA40` (**+4**), 2 Down = `C2/EA3B` (**+3**), 3 Up = `C2/EA36` (**+2**) — the nav-graph field order
`src/menu.h` already documents. The handler tail:
```
2EA48: STA $54 / BPL ->2EA52
2EA4C: LDA #80 / TSB $5A / TRB $54     ; ** neighbour byte bit7 = EDGE: set $5A bit7, strip bit7 of $54 **
```
So a graph entry `0x80|cell` means "cell, but you hit the edge", and **`$5A` bit7 is the edge flag**
every screen's post-move fixup tests.

### 2.10 `C2/9D17` — substate-1 cursor (character select)

```
29D17: PHP / SEP #30
29D1A: LDA $0FC4 / BEQ ->29D28        ; $0FC4 = the char-list SCROLL MAX = max($85-3, 0)  (C2/A056)
29D1F: LDA $5A / BPL ->29D3D / JSR C2/9D5B / BRA ->29D3D    ; the scrolling variant
--- no scroll (roster <= 3) ---
29D28: LDX #00
29D2A: LDA $54 / CMP $85 / BCS ->29D3B    ; cursor ran past the list -> 0
29D30: LDA $5A / BPL ->29D3D               ; no edge -> keep
29D32: LDA $54 / BNE ->29D3B               ; edge at a non-zero cell (DOWN) -> wrap to 0
29D36: LDX $85 / DEX                       ; edge at cell 0 (UP)          -> wrap to the last
29D3B: STX $54
29D3D: STZ $0D22 / JSR C2/9E76             ; redraw the 3 panels
29D43: LDA $54 / STA $0412                 ; ** $0412 = the WINDOW slot **
29D48: CLC / ADC $0413 / STA $71           ; ** $71 = $0412 + $0413 = the ROSTER index **
29D4E: CMP $83 / STA $83 / BEQ + / JSR C2/EAC2   ; blip when the character changed
29D57: PLP / JMP C2/A22F                   ; reload + recompute + redraw
```
`C2/9D5B` is the >3-member variant: `$5A & 04` (DOWN) -> `$0413 < $0FC4 ? ++ : nothing`; UP ->
`$0413-- if >= 0`; each accepted step runs `C2/9DAF`, whose loop is `$0D24 = 0x0C` passes with one
`JSR C2/821E` each = **12 frames of panel slide** per scrolled row (and it ends `DEC $0D18`).

### 2.11 `C2/9ECD` — substate-2 cursor + the L/R CHARACTER SWITCH

```
29ECD: PHP / SEP #20
29ED0: JSR C2/A38B                  ; ** $0077 := $54 ** + redraw the 4 slot labels (VWF table C2/A3BA, stride 10)
29ED3: JSR C2/A00B                  ; L/R -> previous/next character (below)
29ED6: LDA $54 / CMP $0FC5 / STA $0FC5 / BEQ + / JSR C2/EAC2   ; ** $0FC5 = the saved GEAR cursor ** + blip
29EE3: JSR C2/A3E2                  ; recolour the 4 slot rows (cursor row bright: C2/A3F9 1C0B vs 000B)
29EE6: LDA #4D / STA $0D13
29EEB: LDX #FBE3 / JSR C2/8385
29EF1: LDA $0077 / CLC / ADC $71    ; ** the DIRTY KEY = gearSlot + rosterIndex **
29EF7: CMP $84 / STA $84 / BEQ ->29F03
29EFD: JSR C2/821E                  ; 1 yield
29F00: JSR C2/A45D                  ; ** REBUILD the candidate list for the new (slot, character) **
```
```
2A00B: PHP / SEP #30
2A00E: LDX $71
2A010: LDA $0D1D / AND #03 / BEQ ->2A04F      ; neither L nor R
2A017: BIT #02 / BNE ->2A022                  ; ** bit1 = R -> NEXT **
2A01B: DEX / BPL ->2A029 / LDX $85 / DEX / DEX ; L: wrap to the last
2A022: INX / CPX $85 / BCC ->2A029 / LDX #00   ; R: wrap to the first
2A029: CPX $71 / STX $71 / STX $0412 / BEQ ->2A04F
2A032: STZ $0413 / TXA / SEC / SBC #02 / BMI ->2A043 / STA $0413 / LDA #02 / STA $0412
                                               ; the 3-wide window: $0412 = min(X,2), $0413 = max(X-2,0)
2A043: JSR C2/EAC2 / JSR C2/A21F / JSR C2/A3E2 / JSR C2/821E    ; blip, reload+recompute+redraw, 1 YIELD
```
`C2/A45D` = `LDX #6268 / STX $0D92` then falls into `C2/A463`:
```
2A466: JSR C2/8953                  ; ** rebuild the candidate list **
2A469: LDA #1500 / STA $004204
2A470: LDX #FC1B / JSR C2/8385
2A478: STZ $0FC8                    ; ** the list SCROLL := 0 **
2A47B: STZ $0D27
2A47E: LDX $0077 / LDA $C2A4AB,X / STA $0D26   ; ** $0D26 = the gear slot's SHEET OFFSET **
2A488: LDA #04 / STA $0DDB          ; ** $0DDB = the FIRST list nav cell = 4 **
2A48D: LDA #08 / STA $00            ; the page size
2A491: LDY $0FC6 / JSR C2/EA81      ; the window/scroll setup (2.12)
2A497..2A4A6: cursor sprite Y ($0D94 via C2/8BA6, $0D95 := 0, C2/8B93), then JSR C2/A569 (draw 8 rows)
```
`C2/A41A` (entering substate 3) is the same body preceded by the gear-list slide `C2/A4AF`
(`$96A3` += `$26` = 4 per pass until >= 0x20 -> **7 `JSR C2/821E`**), with `LDX #4268 / STX $0D92`,
`LDA #04 / STA $54` (**the list cursor starts on nav cell 4**), `INC $00C1`, `LDA #DF / STA $0D13`
and a final `JMP C2/EAC2`.

### 2.12 `C2/EA81` — the list WINDOW parameters (`$00` = page = 8, `Y` = `$0FC6`)

```
2EA84: LDA $00 / STA $0DD8            ; ** $0DD8 = PAGE SIZE = 8 **
2EA89: TYA / SEC / SBC $00 / STA $4206 / STA $01
2EA93: BCS + / STZ $01 / STY $00      ; count <= 8: scroll range 0, $00 = count
2EA99: LDA $00 / BEQ + / DEC A
2EA9E: CLC / ADC $0DDB / STA $0DDC    ; ** $0DDC = LAST cell = 4 + min(count,8) - 1 **
2EAA5: LDA $01 / STA $57 / STA $0D94 / STA $0DDA  ; ** $57 = $0DDA = max(count-8, 0) = SCROLL RANGE **
2EAAF: REP #20 / LDA $004214 / STA $0D98
```
Measured live in substate 2 (§4.3): `$0DD8 = 08`, `$0DDA = 00`, `$0DDB = 04`, `$0DDC = 04` with
`$0FC6 = 0` — exactly what the arithmetic predicts for count 0.

`C2/9F05` — the substate-3 cursor:
```
29F07: LDA $0FC8 / STA $0DD9 / JSR C2/93BF   ; the L/R PAGE scroller
29F10: CMP #00 / BEQ ->29F2F                 ; paged -> $0D95 via C2/8BA6, $0DD9 -> $0FC8, C2/A569, blip
29F2F: JSR C2/EA53 / BMI ->29F35 / RTS       ; N set = the Up/Down move hit an edge
29F35: LDA $54 / SEC / SBC #04 / STA $04C9   ; the row within the window (0..7)
29F3D: CLC / ADC $0FC8 / BEQ ->29F49         ; absolute index 0 and moving up -> REFUSE (silent)
29F43: INC A / CMP $0FC6 / BCC ->29F4A / RTS ; already at the last -> REFUSE (silent)
29F4A..29F5B: $0DAA = 9 ; JSR C2/EB9B        ; the ONE-ROW pixel slide
29F5E: LDA $5A / BIT #04 / BNE + / DEC $0FC8 / BRA + / INC $0FC8   ; bit2 = DOWN
29F6C..29F8D: redraw the newly exposed row ($04C9 = $0F00[i], $04CA = $0F62[i]) + blip
```
`C2/93BF` (generic, shared with the Item screen): on `$0D1D & 3`; **bit1 (R) -> `$0DD9 -= $0DD8`**,
and if that goes below the range, clamp to the top (`$54 := $0DDB`, scroll 0); **bit0 (L) ->
`$0DD9 += $0DD8`**, clamped to the bottom (`$54 := $0DDC`, scroll `$0DDA`). Returns A = 0 when
nothing moved; a real move also sets `$0D95` and `$81 = FF`.

`C2/EA53` (the edge decision):
```
2EA53: LDA $57 / BEQ ->2EA66     ; $57 == 0 = NOT scrollable
2EA57: LDA $00F2 / BIT #01 / BEQ + / DEC $0D18       ; Start held
2EA61: LDA $5A / BPL ->2EA7F(A=0) ; RTS               ; edge -> return with N set (the caller scrolls)
2EA66: LDA $5A / BPL ->2EA78                          ; not scrollable: WRAP
2EA6A: AND #04 / BNE ->2EA73     ; DOWN edge -> $54 := $0DDB (first) ; UP edge -> $54 := $0DDC (last)
2EA78: LDA $0DDC / CMP $54 / BCC ->2EA73              ; cursor past the last -> clamp to the first
2EA7F: TDC / RTS
```

### 2.13 `C2/8791` / `C2/87D5` / `C2/87FA` — the inventory add / remove

```
C2/87FA FIND(A = id):  X = 0..255: if $2400,X == id AND $2500,X != 0 -> $04C7 = X, $04C8 = 0, CLC
                       (a slot whose COUNT is 0 is skipped); wrapped -> SEC (not found)
C2/8791 ADD($04C9 = id, $04CB = qty):
  28795: LDA $04C9 / BEQ done                      ; id 0 -> nothing
  2879A: CMP #5A / BEQ done                        ; ** the three "none" sentinels are NEVER added **
  2879E: CMP #7B / BEQ done
  287A2: CMP #94 / BEQ done
  287A6: JSR C2/87FA / BCC ->287C0                 ; found a stack -> just add
  287AB: X = the first slot with $2400,X == 0; none -> A = #FF (INVENTORY FULL) and RETURN
  287B7: $2400,X = id ; $2500,X = 0
  287C0: $2500,X = min($2500,X + $04CB, 0x63)      ; ** the stack cap is 99 **
  287D0: A = 0 (ok)
C2/87D5 REMOVE($04C9 = id, $04CB = qty):
  287D9: LDA $04C9 / BEQ done
  287DE: JSR C2/87FA / BCS -> A = #FF (not held)
  287E5: $2500,X -= $04CB ; borrow -> A = #FE (not enough), NO write
  287EE: result != 0 -> store ; result == 0 -> ** STZ $2400,X ** then store the 0 count
```
So the commit's net effect on the bag is exactly: *the old piece appears (unless it was a sentinel or
0), the new piece disappears.* Because `C2/8791` refuses `$5A/$7B/$94`, equipping into an **empty**
helm/armor/accessory slot consumes the new item and adds nothing back — and that is the ROM's only
"unequip" semantics. There is **no** blank/remove row in the list: `C2/8953` only ever emits ids that
are physically in `$2400`, and the three sentinels can never be in `$2400` (`C2/8791` refuses them).
The commit also **ignores `C2/8791`'s FULL return** (`A = FF` is never tested at `C2/9CCE`), so with a
242-slot-full bag the old piece is destroyed.

### 2.14 `C2/9CFC` — the WEAPON-change side effect

```
29CFC: PHP / SEP #30
29CFF: LDA $0077 / BNE ->29D15        ; ** only when the WEAPON row (slot 0) was committed **
29D04: LDX $0412                      ; ** the WINDOW slot, not the roster index -- see below **
29D07: LDA $0D5F,X / CMP #06 / BCS ->29D15   ; charId >= 6 (Magus) -> skip
29D0E: TAX / LDA #00 / STA $7F00B4,X  ; ** $7F:00B4 + charId := 0 **
```
`$7F:00B4` has exactly **two** references in the whole 4 MB ROM (byte scan for `9F/BF/8F/AF B4 00 7F`):
this store at `C2/9D11`, and `CF/FBCC LDA $7F00B4,X / STA $A09B,Y` inside the battle boot, which fills
`$7E:A09B + partySlot` for the three party members and only when `$7F:00BA & 01`. So the equip screen
**invalidates a per-character battle byte whenever the weapon changes.**
*ROM quirk, reproduce verbatim:* it indexes `$0D5F` (the packed roster charId list) with `$0412`
(the 0..2 window slot) instead of `$71 = $0412 + $0413`, so with a roster > 3 scrolled down it clears
the wrong character's byte.

### 2.15 `C2/916E` — **THE DERIVED-STAT RECOMPUTE** (gap (d))

```
2916E: PHP / REP #30
29171: STZ $0DAE / MVN $0DAE -> $0DB0 (7 bytes)   ; ** clear $0DAE..$0DB6 (9 bytes) **
29180: SEP #30
29182: LDA #03 / STA $0DAD
29187: JSR C2/9261 / DEC $0DAD / BPL ->29187      ; ** 4 passes: 3,2,1,0 **
2918F: LDX #06
29191: LDA $FFCF2C,X / TAY                        ; Y = the stat offset
29196: LDA $9A9B,Y                                ; base stat = sheetCopy + 0x0B + Y
29199: CLC / ADC $0DAE,Y                          ; + the accumulated equipment bonus
2919D: CMP $FFCF33,X / BCC + / LDA $FFCF33,X      ; clamp to the per-stat cap
291A7: STA $9B1C,Y                                ; ** derived[Y] -> $9B1C+Y  (= sheet+0x36+Y) **
291AA: DEX / BPL ->29191
291AD: LDA $9A90 / AND #07 / ASL / TAX
291B4: JSR ($91DB,X)                              ; ** the per-CLASS attack formula (below) **
291B7: STA $004206                                ; the divisor it returned
291BB: LDA $9B1D / CLC / ADC $0DB6 / BCC + / LDA #FF
291C6: STA $9B24                                  ; ** derived +0x3E = DEFENCE = Stamina + armour + helm **
291C9: LDA $4214 -> X ; LDA $4215 ; BEQ + ; LDX #FF
291D6: STX $9B23                                  ; ** derived +0x3D = ATTACK = quotient, capped 255 **
```
`$FF:CF2C` (7 bytes) = **`00 02 01 04 05 03 06`** = the stat offsets; `$FF:CF33` (7 bytes) =
**`63 10 63 63 63 63 63`** = the caps (index 1 -> offset 2 -> cap 16 = the Speed cap).

`C2/9261` — the four equipment passes; dispatch table `C2/926E` raw = `76 92 81 92 86 92 95 92`:

| `$0DAD` | handler | piece | what it adds |
|---|---|---|---|
| 3 | `C2/9295` | **Accessory** `$9ABA` (+0x2A) | max-HP: `A = ($ABA==$A1 ? maxHP>>1 : $ABA==$A0 ? maxHP>>2 : 0) + maxHP`, clamp 999 -> **`$9B25` = derived +0x3F**; then `if (that < curHP) curHP = that` (`$9A93`) |
| 2 | `C2/9286` | **Helm** `$9AB7` (+0x27) | `$0DB6 += $7E:7C00[id]` |
| 1 | `C2/9281` | **Armor** `$9AB8` (+0x28) | `$0DB6 += $7E:7C00[id]` |
| 0 | `C2/9276` | **Weapon** `$9AB9` (+0x29) | `$0DB5 = $7E:7C00[id]` |

and **all four fall through the shared tail `C2/92C1`**, which applies that piece's *stat* bonuses:
```
292C1: PHP / REP #20
292C4: LDA $7B00,X / AND #00FF / BEQ ->292F2   ; ** $7E:7B00[id] = the bonus-set index; 0 = none **
292CC: ASL A / TAX
292CE: LDA $CC29D7,X / AND #FFFE / STA $00     ; ** the bonus WORD: low byte = stat MASK, high = AMOUNT **
292D7: SEP #30 / LDX #00
292DB: ASL $00 / BCC ->292ED                   ; walk the mask MSB-first
292DF: LDA $FFCF2C,X / TAY                     ; the stat offset for this mask bit
292E4: LDA $01 / CLC / ADC $0DAE,Y / STA $0DAE,Y
292ED: INX / LDA $00 / BNE ->292DB
```
so the mask bit -> stat mapping is `bit7 -> +0x0B`, `bit6 -> +0x0D`, `bit5 -> +0x0C`, `bit4 -> +0x0F`,
`bit3 -> +0x10`, `bit2 -> +0x0E`, `bit1 -> +0x11` (bit0 is cleared by `AND #FFFE`).

The per-class attack formulas, table `C2/91DB` raw = `EB 91 23 92 23 92 EB 91 EB 91 3C 92 EB 91 EB 91`
(indexed by `charId & 7`):

| charId | routine | dividend -> `$4204` | divisor -> `$4206` |
|---|---|---|---|
| 0,3,4,6,7 (Crono/Robo/Frog/Magus) | `C2/91EB` | `5*$0DB5 + 12*derived[0]` | 9 |
| 1,2 (Marle/Lucca) | `C2/9223` | `2*(derived[4] + $0DB5)` | 3 |
| 5 (Ayla) | `C2/923C` | `16*derived[0] + word $CC:2D69[level*2]` | 9 |

`C2/9137` is the write-back wrapper (the one the menu calls everywhere except the live preview):
```
29138: JSR C2/916E
2913D: LDA $9ABA / CMP #A8 / BNE + / ASL $9B21     ; ** accessory $A8 (Third Eye) DOUBLES derived[+5] **
29149: LDY $6F                                     ; the LIVE sheet pointer
2914B: LDA $9A93 / STA $0003,Y                     ; ** sheet +0x03 = curHP (clamped by the accessory pass) **
29151: LDX #9B1C / TYA / ADC #0036 / TAY / LDA #000A / MVN $7E,$7E
                                                   ; ** 11 bytes $9B1C..$9B26 -> sheet +0x36..+0x40 **
29160: LDX #9B1C / LDY #9AC6 / LDA #000A / MVN $7E,$7E    ; and into the sheet COPY at $9A90+0x36
```

**The derived block, sheet +0x36 .. +0x40 (11 bytes):**

| offset | meaning |
|---|---|
| +0x36 | Power   (base +0x0B + bonus, cap 99) |
| +0x37 | Stamina (base +0x0C + bonus, cap 99) |
| +0x38 | Speed   (base +0x0D + bonus, **cap 16**) |
| +0x39 | Magic   (base +0x0E + bonus, cap 99) |
| +0x3A | Hit     (base +0x0F + bonus, cap 99) |
| +0x3B | Evade   (base +0x10 + bonus, cap 99; **x2 if accessory == $A8**) |
| +0x3C | M.Def   (base +0x11 + bonus, cap 99) |
| +0x3D | **Attack**  (the per-class quotient, cap 255) |
| +0x3E | **Defence** (derived Stamina + `$0DB6`, cap 255) |
| +0x3F/+0x40 | **effective max HP** (word, cap 999) |

**NUMERICALLY VALIDATED against the oracle's own live bytes** (§4.1, Crono in `com_jogo_salvo`,
sheet `+0x27 = 0x7C` Hide Cap, `+0x28 = 0x5B` Hide Tunic, `+0x29 = 0x01` Wood Sword,
`+0x2A = 0x95` Bandana, base stats `07 0B 0C 05 08 08 05`, maxHP `0x0060`):
* `$0DB5 = 0x03` = `$7C00[0x01]`; `$0DB6 = 0x08` = `$7C00[0x7C] (3) + $7C00[0x5B] (5)` — both exact.
* `$0DAE[2] = 1`: Bandana `$7B00[0x95] = 1` -> `$CC:29D7 + 2` = `0x0140` -> mask `0x40` (bit6 ->
  offset 2 = Speed), amount `0x01`. Derived Speed `0x0C -> 0x0D` — exact.
* Attack `= (5*3 + 12*7)/9 = 99/9 = 11 = 0x0B` — the live `sheet+0x3D` is **`0x0B`**.
* Defence `= 11 + 8 = 19 = 0x13` — the live `sheet+0x3E` is **`0x13`**.
* max HP: accessory `0x95` is neither `$A0` nor `$A1`, so `0 + 0x60 = 0x60` — live `+0x3F` = `60 00`.
* the live `$9B1C..$9B26` = `07 0B 0D 05 08 08 05 0B 13 60 00` = the sheet's own `+0x36..+0x40`.

### 2.16 `C2/A5C3` / `C2/A5E7` — the LIVE PREVIEW and the up/down arrows

```
2A5C3: PHP / SEP #21                          ; ** SEP #21 also SETS carry **
2A5C6: TDC / LDA $0FC8 / SBC #04 / CLC / ADC $54 / TAX  ; X = $0FC8 + ($54 - 4) = the absolute candidate index
2A5D0: LDA $0F00,X / STA $04C9                ; the candidate id
2A5D6: LDX $0D26 / STA $9A90,X                ; ** patch the SHEET COPY's gear byte -- NOT the live sheet **
2A5DC: JSR C2/A5E7
2A5DF: LDA $04C9 / JSR C2/F2F3                ; draw the item's description
```
```
2A5E7: PHP / SEP #30
2A5EA: JSR C2/916E                            ; recompute -> $9B1C.. ( = $9AE6 + 0x36.. )
2A5ED: LDA $9ABA / CMP #A8 / BNE + / ASL $9B21
2A5F7: LDY $51 / TYX                          ; $51 measured = 0 (§4.3)
2A5FA: PHX / LDA $C2A642,X / TAX              ; the sheet offset for row X
2A600: LDA $9A90,X / STA $00                  ; the CURRENT value (sheet copy, already committed)
2A605: LDA $9AE6,X / TAX                      ; the NEW value (the just-computed block)
2A609: LDA #00 / CPX $00 / BEQ + / LDA #08 / BCS + / LDA #1C
2A615: STA $0D4D,Y / INY                      ; ** $7E:0D4D+row = the row's colour code **
2A619: PLX / INX / CPX #09 / BCC ->2A5FA
2A61F: REP #10 / LDA #00 / LDX $9B25 / CPX $9ACF / BEQ + / LDA #08 / BCS + / LDA #1C
2A631: STA $0D57                              ; ** the HP row's colour (word compare) **
2A634: LDX #BEE2 / JSR C2/ED31 / LDX #FBE3 / JSR C2/8385
```
`C2/A642` raw = **`3D 3E 36 38 3B 39 37 3A 3C 03 3F`** (`d[0x2A642:0x2A64D]`), so the nine compared
rows are, in screen order: **Atk (+0x3D), Def (+0x3E), Power (+0x36), Speed (+0x38), Evade (+0x3B),
Magic (+0x39), Stamina (+0x37), Hit (+0x3A), M.Def (+0x3C)**, plus the max-HP word (+0x3F) into
`$0D57`. Colour codes: **0 = unchanged, 0x08 = the new item is HIGHER, 0x1C = LOWER**.

Because `C2/A5C3` writes only the **copy** at `$9A90`, moving the list cursor is free: the live sheet
is untouched until `C2/9CD7`, and `C2/A64D` (the tail of both A and B) reloads the copy from the live
sheet (`C2/A22F` -> `C2/8820` -> `C2/8839`) and re-runs `C2/9137`, so a cancelled preview is discarded
and a committed one is written through.

```
2A64D: PHP / REP #30
2A650: JSR C2/A22F        ; $71 = $0412 + $0413 ; C2/8820 (reload $9A90 + $6F) ; ** C2/9137 ** ; redraw
2A653: JSR C2/813C        ; ** rebuild the HP-bar scale $0D38+id*2 = maxHP/10 ** (the menu-open [J] loop)
2A656: LDA #2E00 / STA $61 / JSR C2/A216 / JSR C2/A3E2
2A661: JSR C2/821E        ; 1 yield
2A664: JSR C2/A697
2A667: JSR C2/A45D        ; rebuild the candidate list (the bag changed!)
```

### 2.17 The 4 slot LABELS `C2/A3BA` (stride 10, rendered by `C2/A38B`)

`C2/A38B`: `$0077 := $54`; `Y = A*2; Y = Y*4 + Y*... ` (`ASL / STA $00 / ASL / ASL / ADC $00` = **A*10**)
`+ #A3BA`; `LDX #3086 / LDA #C20A / JSR C2/EF65` (the VWF renderer, source bank `$C2`).
Raw `d[0x2A3BA:0x2A3E2]`, CT mapped encoding (`A0-B9` = `A-Z`, `BA-D3` = `a-z`, `FF` = space):

```
FF FF B6 BE BA C9 C8 C7 FF FF   ->  "Weapon"
FF FF A7 BE C5 C6 BE CD FF FF   ->  "Helmet"
FF FF A0 CB C6 C8 CB FF FF FF   ->  "Armor"
A0 BC BC BE CC CC C8 CB D2 00   ->  "Accessory"
```
so the gear-row order on screen is **Weapon, Helmet, Armor, Accessory**, top to bottom — matching
`C2/A4AB = 29 27 28 2A` and the on-screen render (§4.5: `Wood Sword / Hide Cap / Hide Tunic /
Bandana` = `+0x29 / +0x27 / +0x28 / +0x2A`).

### 2.18 The two panel SLIDES (frame costs)

* **entry**, `C2/A71E` (menu2_shell_exit.md §2.11): `$0D25 = (3 - $0412) * 8`, phase 1 runs
  `$0D24` up to `$0D25`, phase 2 continues to `#18` — **always 24 `JSR C2/821E`**; `C2/9C5B` adds one
  more, so **BLIP#1 -> BLIP#2 = 25 frames** (measured §4.2).
* **exit**, `C2/A85C`: `JSR C2/821E` at `C2/A85F`, `C2/A88F` and inside the loop at `C2/A945`.
  `2A920: LDA $0412 / BNE ->2A92C ; LDA #18 / STA $0D25` else `$0D25 = $0412 * 8` (+ `$0DAB = 6`),
  loop `INC $0D24 / CMP $0D25 / BCC`, then a second phase to `#18`. **24 loop yields + 2 = 26 frames**
  — measured for BOTH `$0412 = 0` and `$0412 = 1` (§4.4), and `$0D24` was watched counting
  `02 .. 18` with `$0D25 = 08` in the slot-1 run, which is the proof that the second phase exists.
* the candidate-list panel slide `C2/A4AF` = **7 yields** (`$96A3 += 4` until `>= 0x20`).
* the char-select scroll step `C2/9DAF` = **12 yields** (`$0D24 = 0x0C`).

### 2.19 The RIBBON entry has NO gate for screen 1

```
29A77: LDX #BD51 / JSR C2/ED31
29A7D: LDA $0411 / INC A / STA $67      ; ** $0067 := ribbonOpt + 1 -- unconditional **
29A83: STZ $68                          ; substate := 0
29A85: TDC / LDX #9DF0 / LDY #9580 / LDA #1F / MVN $7E,$FF
29A91: STA $0D17                        ; ** poison the substate latch so C2/E984 eats one frame of input **
```
Unlike Form (`C2/C627`) and Save (`C2/CEDC`), **Equip/Status has no entry predicate** — it always
opens.

---

## 3. RAM CONTRACT

Every row is either a store I disassembled or a byte I read out of an oracle capture (§4).

| address | meaning | proof |
|---|---|---|
| `$7E:0067` | screen id; **1 = Equip/Status** | `C2/9A81 STA $67`; measured `01` (§4.2) |
| **`$7E:00BE`** | the REAL substate (`$68` is task-local; this is slot 1's `+6`) | measured `0000 -> 0100 -> 0200 -> 0100` (§4.2) |
| `$7E:0D17` | `C2/E984`'s previous-substate latch; `!= $68` **suppresses all button edges for that frame** | `C2/E98D-E995`; poisoned to `FF` by `C2/9A91` |
| `$7E:0D1D` | the decoded buttons: bit7 confirm(A), bit6 cancel(B), bit1 **R**, bit0 **L** | `C2/E9AB/E9BE/E9CA/E9D4`; measured `80` on each A press (§4.2) |
| `$00:005A` | `= $00F5`, the d-pad edge byte (bit0 R, bit1 L, bit2 Dn, bit3 Up) **+ bit7 = "the move hit an edge"** | `C2/E9F2 STA $5A`, `C2/EA4C LDA #80 / TSB $5A` |
| `$00:0054` | the nav-cell cursor. substate 2: **0..3 = the gear rows**; substate 3: **4..11 = the list rows** | `C2/A367 STZ $54` / `C2/A455 LDA #04`; measured `00 1 2 3 0` on Down x4 (§4.4) |
| `$00:0081` | the PRE-move cursor (blip-on-change) | `C2/E9E7 STA $81` |
| `$00:0077` | **the GEAR SLOT 0..3** (0 Weapon, 1 Helm, 2 Armor, 3 Accessory) | `C2/A391 STA $0077`; measured tracking `$54` (§4.4) |
| `$7E:0FC5` | the SAVED gear cursor (restored by `C2/9CE7` after a commit/cancel) | `C2/9EDB STA $0FC5` |
| `$7E:0D26` | **the gear slot's SHEET OFFSET** = `C2/A4AB[$0077]` = `29 / 27 / 28 / 2A` | `C2/A485 STA $0D26`; measured `29` with `$0077 = 0` (§4.3) |
| `$00:006F` (word) | **the LIVE sheet pointer** `$7E:2600 + charId*0x50` | `C2/8851 STA $6F`; measured `2600` for Crono (§4.3) |
| `$7E:9A90` (0x50) | **the SHEET COPY** the whole screen reads/previews | `C2/8854 MVN`; measured byte-identical to `$2600` (§4.1) |
| `$7E:9AE0` (6) | the character's NAME copy | `C2/8879 MVN`; measured `A2 CB C8 C7 C8 00` = "Crono" |
| `$7E:9B1C..9B26` | the FRESHLY computed derived block (= `$9AE6 + 0x36..0x40`) | `C2/91A7 STA $9B1C,Y`; measured `07 0B 0D 05 08 08 05 0B 13 60 00` |
| `$7E:0DAE..0DB4` | the 7 per-stat equipment bonus accumulators | `C2/92E7 STA $0DAE,Y`; measured `00 00 01 00 00 00 00` (Bandana +1 Speed) |
| `$7E:0DB5` | the WEAPON's `$7C00` value | `C2/927C STA $0DB5`; measured `03` (= `$7C00[0x01]`) |
| `$7E:0DB6` | helm + armor `$7C00` sum | `C2/9290 STA $0DB6`; measured `08` (= 3 + 5) |
| `$7E:0DAD` | the equipment-pass index 3..0 | `C2/9184 STA $0DAD` |
| **`$7E:0FC6`** | **the CANDIDATE COUNT** (0 => A buzzes) | `C2/8990 INC $0FC6`; measured `00` for every slot on `com_jogo_salvo` (§4.2/§4.3) |
| **`$7E:0F00[k]`** | the candidate item ids, 0-terminated | `C2/8987 STA $0F00,X` |
| **`$7E:0F62[k]`** | the candidates' inventory counts | `C2/898D STA $0F62,X` |
| `$7E:0FC8` | the list SCROLL (top index) | `C2/A478 STZ $0FC8`, `C2/9F64/9F69` |
| `$7E:0DD8` | the list PAGE size = 8 | `C2/EA86`; measured `08` (§4.3) |
| `$7E:0DDA` / `$00:0057` | the list SCROLL RANGE = `max(count-8, 0)`; `$57 == 0` => wrap instead of scroll | `C2/EAA7/EAAC`; measured `00` |
| `$7E:0DDB` / `$7E:0DDC` | the list's FIRST / LAST nav cell (4 and `4+min(count,8)-1`) | `C2/A488`, `C2/EAA2`; measured `04`/`04` |
| `$00:0084` | the "(gear slot + roster index) changed" dirty key -> rebuild | `C2/9EF9 STA $84`; measured `FF -> 00 -> 01 -> 02` (§4.2/§4.4) |
| `$00:0071` | the ROSTER index; `$0412` = the 0..2 window slot, `$0413` = the window scroll | `C2/9D48`, `C2/A02D/A03B`; measured `$71 = 0,1,2` on R (§4.2) |
| `$7E:0085` | the char-select list LENGTH | `FF/F82A`; measured `03` |
| `$7E:0FC4` | the char-select SCROLL MAX = `max($85-3, 0)` | `C2/A060 STA $0FC4`; measured `00` |
| `$7E:0D5F[]` | the packed roster charId list | menu2_shell_exit.md §2.2 [F] |
| `$7E:04C9 / $04CA / $04CB / $04C7` | the item-op id / count / QTY / found slot | `C2/8791`, `C2/87D5`, `C2/87FA` |
| `$7E:2400[i] / $2500[i]` | the inventory ids / counts (0..0xF1) | `C2/87BA`, `C2/87CD` |
| **sheet `+0x27/+0x28/+0x29/+0x2A`** | **Helm / Armor / Weapon / Accessory** | `C2/A4AB` + `C2/9276-9297`; measured `7C 5B 01 95` (§4.1/§4.3) |
| sheet `+0x03` (word) | curHP — **rewritten by `C2/914E`** after a recompute | `C2/9137` |
| **sheet `+0x36..+0x40`** | **the DERIVED block** (7 stats, Atk, Def, maxHP word) | `C2/915A MVN`; measured equal to `$9B1C..$9B26` |
| `$7E:0D4D..0D55`, `$0D57` | the 9 stat arrow colours + the HP arrow colour (0 same / 8 up / 0x1C down) | `C2/A615`, `C2/A631` |
| `$7E:0D38 + id*2` | maxHP/10 for the HP bar — rebuilt by `C2/813C` after every equip | `C2/A653` |
| `$7E:7A00[id]` | **the item's equip-by-character bitmask** | `C2/93AF`; measured (§4.1) |
| `$7E:7B00[id]` | the item's bonus-set index into `$CC:29D7` | `C2/92C4` |
| `$7E:7C00[id]` | the gear "power" byte (weapon atk / armour+helm def) and the LIST SORT KEY | `C2/927C`, `C2/928C`, `C2/89A5` |
| `$7E:9380 + cell*6` | the nav graph `(x, y, up, dn, lf, rt)`; **bit7 of a neighbour = EDGE** | dumped live (§4.3) |
| `$7F:00B4 + charId` | a per-character battle byte the WEAPON change zeroes | `C2/9D11`; read only by `CF/FBCC` |
| `$7E:1E00-1E03` | the sound mailbox (cmd/id/pan) | menu_sfx.md; measured id `00` blip / `01` buzz (§4.2) |
| `$7E:0D76` | the SFX throttle (3 on each shipped command) | menu_sfx.md; measured (§4.2) |

---

## 4. ORACLE MEASUREMENTS (commands verbatim, output pasted)

### 4.1 The state's inventory + the property pages + the derived block

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/com_jogo_salvo.state \
  scratchpad/menu/eq0.raw --btnscript "0:20,200:2,0:60"
```
then reading `scratchpad/menu/eq0.raw.wram` (flat 128 KB, `$7E` at 0):

```
inventory slots: [(0, 214, 1), (2, 198, 1)]          <- ONLY id 214 (Pendant) and id 198
party 2980: 00 02 04 01 80 80 80 80 80               <- Crono, Lucca, Frog active; Marle reserve
0085 = 3   0073 = 3   0075 = 4
char0 00 86 80 19 00 60 00 02 00 0C 00 07 0B 0C 05 08 08 05 03 58 00 00 12 00 ... (+0x27..) 7C 5B 01 95 2A 00 4B 00
      ... (+0x36..) 07 0B 0D 05 08 08 05 0B 13 60 00 ...
9A90 buffer: 00 86 80 19 00 60 00 02 00 0C 00 07 0B 0C 05 08 08 05 03 58 00 00 12 00 00 ...
             ... 7C 5B 01 95 ... 07 0B 0D 05 08 08 05 0B 13 60 00 ... A2 CB C8 C7 C8 00
9B1C..9B26:  07 0B 0D 05 08 08 05 0B 13 60 00
0DAE..0DB8:  00 00 01 00 00 00 00 03 08 00 00 00
id 01: 7700=00 7A00=80 7B00=00 7C00=03
id 5B: 7700=00 7A00=FE 7B00=00 7C00=05
id 7C: 7700=00 7A00=FE 7B00=00 7C00=03
id 95: 7700=04 7A00=FE 7B00=01 7C00=00
7A00[0x00..0x20]: 00 80 80 80 80 80 80 80 80 80 80 80 80 80 80 80 80 40 40 40 40 40 40 40 40 40 40 00 00 00 00 20 20
```
**Both held items (214, 198) are >= 0xBC, so they are consumables/key items and lie OUTSIDE every one
of the four gear ranges. `$0FC6` is therefore 0 for all four slots on this state — and it is 0 on
EVERY state in `saves/` (§6).**

Re-deriving the property pages straight from ROM bank `$CC` with the §2.7 recipe and diffing against
this live WRAM: `$7C00[0..0x93]` **0 mismatches / 148**, `$7A00[0..0xF1]` 2 (ids `F0/F1`, not gear),
`$7B00` 64 (all "model non-zero where live is 0", none at a gear id), `$7700` 6 (ids `02 09 11 19 22
29`, an extra `0x40` — OPEN, §6).

The arithmetic of §2.15 reproduces the live derived block exactly:
`$0DB5 = 3 = $7C00[weapon 0x01]`; `$0DB6 = 8 = $7C00[0x7C] + $7C00[0x5B] = 3 + 5`;
`$0DAE[2] = 1` from Bandana (`$7B00[0x95] = 1 -> $CC:29D7[1] = 0x0140` -> mask bit6 -> Speed, +1);
Speed `0x0C -> 0x0D`; **Atk `= (5*3 + 12*7)/9 = 11 = 0x0B` = the live `+0x3D`**;
**Def `= 11 + 8 = 19 = 0x13` = the live `+0x3E`**; maxHP `0x0060` = the live `+0x3F`.

### 4.2 The whole interactive flow, frame by frame (substates, SFX, refusal)

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E00BE:2,7E0067:1,7E0054:1,7E0077:1,7E0FC6:1,7E0FC5:1,7E0412:1,7E0413:1,7E1E00:4,7E0D1D:1" \
  --from saves/com_jogo_salvo.state \
  --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60,100:2,0:30,20:2,0:30,100:2,0:30,40:2,0:30" --diff
```
```
  f61     7E00BE=0100*  7E0067=00   ...                                   <- ribbon idle (substate 1)
  f83     7E00BE=0000*  7E0067=01*  ... 7E0D1D=80*                        <- A: screen 1, substate 0
  f84     7E00BE=0100*  7E0067=01   ... 7E1E00=00008007*                  <- sub0 ran: BLIP id 00, $68 -> 1
  f125    7E00BE=0200*  ...              7E0D1D=80*                       <- A: substate 2 + BLIP#1
  f151    7E00BE=0200   ...              7E0D1D=00*                       <- resumed after A71E's 24 yields
  f187    7E00BE=0200   ... 7E0FC6=00  7E1E00=00018007*  7E0D1D=80*       <- A on Weapon: ** BUZZ id 01 **
  f219    7E00BE=0200   7E0054=01*  7E0077=01*  7E0FC5=01*  7E1E00=00008007*   <- Down: gear row 1, blip 00
  f251    7E00BE=0200   ...          7E1E00=00018007*  7E0D1D=80*         <- A on Helm: ** BUZZ id 01 **
  f283    7E00BE=0100*  ...          7E1E00=00008007*                     <- LEFT: back to char-select, blip
  f309    7E00BE=0100   7E0054=00*                                        <- the cursor restore, 26 frames later
```
`$7E1E00` is printed `cmd id pan p2`; the driver consumes `cmd`, so the residue is `00 <id> 80 07` —
**id `00` = the blip, id `01` = the buzz**. `$0FC6 = 00` the entire time.

The SFX ship instants, watched on the throttle `$0D76` (menu_sfx.md: `3` on every shipped command):
```
scratchpad/menu/ct_probe.exe ... --monitor "7E00BE:2,7E0D76:1,7E1E01:1,7E0054:1,7E0077:1,7E0071:1,7E0412:1,7E0413:1,7E0084:1,7E9AB9:1" \
  --from saves/com_jogo_salvo.state --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60,800:2,0:40,800:2,0:40,400:2,0:40" --diff
  f84   7E0D76=03*  7E1E01=00*   ...                                   <- C2/9C31's blip
  f125  7E0D76=03*  7E0084=FF*   ...                                   <- C2/9C52 BLIP #1 + $84 := FF
  f150  7E0D76=03*                                                     <- C2/9C5E BLIP #2  ** = f125 + 25 **
  f151  7E0084=00*                                                     <- the rebuild wrote $84 = $0077+$71 = 0
  f187  7E0071=01*  7E0412=01*  7E9AB9=1F*  7E0D76=03*                 <- ** R = next character ** (Lucca)
  f188  7E0084=01*                                                     <- A00B yielded first, then $84
  f229  7E0071=02*  7E0412=02*  7E9AB9=3B*                             <- ** R again ** (Frog)
  f271  7E0071=01*  7E0412=01*  7E9AB9=1F*                             <- ** L = previous character **
```
`$7E:9AB9` is the sheet copy's `+0x29` (weapon): `01` Crono -> `1F` Lucca -> `3B` Frog -> `1F`, exactly
the three sheets' own `+0x29` bytes (§4.1 char dump). That is the proof that **L/R re-run
`C2/8820`/`C2/8839` and swap the whole `$9A90` copy.**

### 4.3 The nav graph, the list window and `$0D26`, read out of a peek in substate 2

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/com_jogo_salvo.state \
  scratchpad/menu/eqgear.raw --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60"
```
```
nav graph $7E:9380 (6 bytes/cell):
  cell  0: x=120 y= 79 up=03 dn=01 lf=00 rt=00       <- Weapon      (wraps UP to 3)
  cell  1: x=120 y= 91 up=00 dn=02 lf=01 rt=01       <- Helmet
  cell  2: x=120 y=103 up=01 dn=03 lf=02 rt=02       <- Armor
  cell  3: x=120 y=115 up=02 dn=00 lf=03 rt=03       <- Accessory   (wraps DOWN to 0)
  cell  4: x=  8 y= 75 up=84 dn=05 lf=04 rt=04       <- list row 0  ** up = 0x84 = EDGE|4 **
  cell  5: x=  8 y= 87 up=04 dn=06 ...
  ...
  cell 11: x=  8 y=159 up=0A dn=8B lf=0B rt=0B       <- list row 7  ** dn = 0x8B = EDGE|11 **
0054=00 0077=00 0FC6=00 0FC5=00 0FC8=00 0D26=29 0071=00 0412=00 0413=00 0085=03
006F=2600  0DD8=08 0DDA=00 0DDB=04 0DDC=04 0057=00 0051=00
9A90+0x27..0x2A: 7C 5B 01 95
```
This is the direct proof of (i) `$0D26 = 0x29` for gear slot 0 = the WEAPON offset, (ii) the gear
cursor's 4-cell wrap-around ring, (iii) **the 8 candidate rows are nav cells 4..11**, and (iv) the
`0x80` edge encoding (`0x84` / `0x8B`) that `C2/EA4C` decodes. `$0051 = 0`, so `C2/A5E7` fills all
nine `$0D4D..$0D55` arrow slots.

### 4.4 The gear cursor wraps; the exit slide is 26 frames

```
scratchpad/menu/ct_probe.exe ... --monitor "7E0054:1,7E0077:1,7E1E01:1,7E0D76:1,7E0084:1" \
  --from saves/com_jogo_salvo.state \
  --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60,20:2,0:20,20:2,0:20,20:2,0:20,20:2,0:20,10:2,0:20" --diff
  f187  7E0054=01*  7E0077=01*  7E0D76=03*  7E0084=01*     <- Down
  f209  7E0054=02*  7E0077=02*  7E0D76=03*  7E0084=02*     <- Down
  f231  7E0054=03*  7E0077=03*  7E0D76=03*  7E0084=03*     <- Down
  f253  7E0054=00*  7E0077=00*  7E0D76=03*  7E0084=00*     <- Down ** WRAPS to 0 **
  f275  7E0054=03*  7E0077=03*  7E0D76=03*  7E0084=03*     <- Up   ** WRAPS to 3 **
```
Every step ships one blip (`$0D76` re-armed to 3), `$0077` follows `$54` (C2/A38B) and `$0084` follows
`$0077 + $71`.

```
scratchpad/menu/ct_probe.exe ... --monitor "7E00BE:2,7E0054:1,7E0412:1,7E0D25:1,7E0D24:1" \
  --from saves/com_jogo_salvo.state --btnscript "0:20,200:2,0:60,100:2,0:40,20:2,0:30,100:2,0:60,40:2,0:40" --diff
  ... f223 7E0412=01 7E0D25=08 7E0D24=02*   ...   f245 7E0054=01* 7E0D24=18*
```
LEFT pressed at f219, `$0D25 = 8` (window slot 1) but `$0D24` runs all the way to `0x18` = **24**, and
the cursor restore lands at f245 = **f219 + 26**. Identical to the slot-0 run (§4.2 f283 -> f309).

### 4.5 The rendered screen (substate 2)

`python tools/_fbpng.py scratchpad/menu/eqgear.raw scratchpad/menu/eqgear.png` shows the label box
reading **"Weapon"**, the four gear rows **Wood Sword / Hide Cap / Hide Tunic / Bandana** (the first
bright = the cursor row, the other three dim), and the stat block. A 4x crop of the stat block
(`scratchpad/menu/eq_stats_zoom.png`) reads **`SPD. 13`, `EV. 8`, `STAM. 11`**, with
`PWR 7 / HIT 8 / MAG. 5 / M DEF. 5 / EXP 88 / NEXT 42` beside it. Against the live sheet
(`+0x36..+0x3C = 07 0B 0D 05 08 08 05`) that already pins **PWR=+0x36, STAM=+0x37, SPD=+0x38** and
confirms the displayed numbers are the DERIVED block, not the base stats (base `+0x0D` Speed is
`0x0C = 12`; the screen shows 13). Crono's block ties `+0x3A = +0x3B = 08` and `+0x39 = +0x3C = 05`,
so HIT/EV. and MAG./M DEF. are **not** separable from this capture — §4.7 breaks both ties.

### 4.6 The SFX census of the equip screen (byte scan of bank `$C2`)

Scanning `20 C2 EA` / `4C C2 EA` / `20 CC EA` / `4C CC EA` / `20 BA EA` over `C2/9A50-9FA0` and
`C2/A000-A900`:
```
JSR EAC2 (blip $00): 9A73 9C31 9C52 9C5E 9C90 9C9C 9CBD 9CE4 9D54 9EE0 9F26 9F8D  A043
JMP EAC2           : 9AAA  A45A
JSR EACC (buzz $01): (none)
JMP EACC           : 9C8D
JSR EABA (id in A) : 9CDE            <- LDA #B5 immediately before it
```

### 4.7 The stat LABELS pinned by measurement, and the recompute graded 7/7

§4.5 read Crono's panel, but Crono's derived block has two ties (`+0x3A = +0x3B = 08` and
`+0x39 = +0x3C = 05`), so `HIT vs EV.` and `MAG. vs M DEF.` were NOT resolved by that capture alone.
Two more captures break both ties — R once on the gear panel selects Lucca, R twice selects Frog
(§4.2 proves R re-loads `$9A90`):

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/com_jogo_salvo.state \
  scratchpad/menu/eqlucca.raw --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60,800:2,0:40"
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/com_jogo_salvo.state \
  scratchpad/menu/eqfrog.raw  --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60,800:2,0:40,800:2,0:40"
```
(3x crops: `scratchpad/menu/eqlucca_stats.png`, `scratchpad/menu/eqfrog_stats.png`.)

| | live `+0x36..+0x3C` | screen |
|---|---|---|
| Crono | `07 0B 0D 05 08 08 05` | PWR 7, STAM 11, SPD 13, MAG 5, HIT 8, EV 8, M DEF 5 |
| **Lucca** | `02 07 06 0A 08 07 0A` | **PWR. 2, STAM. 7, SPD. 6, MAG. 10, HIT 8, EV. 7, M DEF. 10** |
| **Frog** | `0B 0E 0B 08 09 09 09` | **PWR. 11, STAM. 14, SPD. 11, MAG. 8, HIT 9, EV. 9, M DEF. 9** |

Lucca's `HIT 8 / EV. 7` fixes **HIT = +0x3A (offset 4)** and **EV. = +0x3B (offset 5)**; Frog's
`MAG. 8 / M DEF. 9` fixes **MAG. = +0x39 (offset 3)** and **M DEF. = +0x3C (offset 6)**. With the
Speed cap of 16 at `$FF:CF33[1] -> offset 2` this pins the whole block **by measurement**, not by
inheriting `src/menu.h`'s labels:

```
base +0x0B / derived +0x36 = Power      base +0x0F / derived +0x3A = Hit
base +0x0C / derived +0x37 = Stamina    base +0x10 / derived +0x3B = Evade
base +0x0D / derived +0x38 = Speed      base +0x11 / derived +0x3C = M.Def
base +0x0E / derived +0x39 = Magic
```
Consequences the ROM text alone left ambiguous, now settled:
* `C2/9223` (Marle/Lucca) computes **Attack = 2 * (derived HIT + weaponPower) / 3** — Lucca:
  `2*(8 + $7C00[0x1F]=5)/3 = 26/3 = 8` = her live `+0x3D = 0x08`.
* `C2/9142` / `C2/A5ED`'s `ASL $9B21` (offset 5) **doubles EVADE** for accessory `$A8` (Third Eye).
* `C2/91BB`'s `$9B1D` (offset 1) is **STAMINA**, so `Def = Stamina + helm + armour`.

**The whole §2.15 recompute, re-implemented from the ROM tables and run against the oracle's live
sheets for all seven characters** (`$7E:2600 + id*0x50`, comparing `+0x36..+0x3C`, `+0x3D`, `+0x3E`,
`+0x3F` word):
```
char0 model 07 0B 0D 05 08 08 05 atk 0B def 13 hp 96  | live identical | wp=3   dsum=8   bon=[0,0,1,0,0,0,0]  PASS
char1 model 02 06 08 08 0A 06 09 atk 08 def 0E hp 76  | live identical | wp=3   dsum=8   bon=[0,0,0,0,2,0,0]  PASS
char2 model 02 07 06 0A 08 07 0A atk 08 def 14 hp 88  | live identical | wp=5   dsum=13  bon=[0,0,0,0,0,0,0]  PASS
char3 model 15 1C 06 07 09 08 0C atk 27 def 4A hp 235 | live identical | wp=20  dsum=46  bon=[0,2,0,0,0,0,0]  PASS
char4 model 0B 0E 0B 08 09 09 09 atk 12 def 26 hp 128 | live identical | wp=6   dsum=24  bon=[2,0,0,0,0,0,0]  PASS
char5 model 27 26 0D 08 16 19 1A atk 4C def 67 hp 335 | live identical | wp=0   dsum=65  bon=[4,0,0,0,0,0,0]  PASS
char6 model 33 2B 0C 32 14 21 48 atk 86 def 94 hp 650 | live identical | wp=120 dsum=105 bon=[0,0,0,0,0,0,0]  PASS
```
**7 / 7**, covering all three attack formulas (`C2/91EB` Crono/Robo/Frog/Magus, `C2/9223`
Marle/Lucca, `C2/923C` Ayla's `$CC:2D69[level]` table). Gap (d) is therefore **measured**, even though
gap (c)'s commit cannot be driven on this repo's captures.

---

## 5. IMPLEMENTATION SPEC (in the engine's own terms)

Today `src/field.c` has `menuScreen` (`$0067`), `menuStatSub` (0 = char-select, 1 = detail),
`menuStatSlot` / `menuStatChar`, `menuEquipSlot` (`$0077`), `menuEquipRise` / `menuEquipHold`, and
`src/menu.h` exposes `CtMenuRenderCharSelect` / `CtMenuRenderEquip` / `CtMenuRenderEquipSliding`.
Screen 1's A on a gear row is a no-op.

**Map the ROM's `$68` onto `menuStatSub`:** ROM 1 = engine 0 (char-select), ROM 2 = engine 1 (gear
panel), and add **ROM 3 = engine 2 = the candidate list**.

### 5.1 New state (all of it named after the ROM byte)

```c
uint8_t  menuEqCand[0xF2];   // $7E:0F00  candidate item ids (0-terminated)
uint8_t  menuEqCandN[0xF2];  // $7E:0F62  their inventory counts
int      menuEqCount;        // $7E:0FC6  the candidate COUNT
int      menuEqScroll;       // $7E:0FC8  the list scroll (top index)
int      menuEqCell;         // $00:0054 in substate 3: 4..11
int      menuEqSaveSlot;     // $7E:0FC5  the saved gear cursor
int      menuEqDirty;        // $00:0084  = menuEquipSlot + rosterIndex, FF forces a rebuild
uint8_t  menuEqSheet[0x50];  // $7E:9A90  the sheet COPY (the preview target)
```

### 5.2 The functions to add to `src/menu.c` / `src/menu.h`

```c
// ROM C2/8953 + C2/93A8. Fills cand[]/cnt[], returns the count. gearSlot 0..3, charId 0..6.
int  CtMenuEquipCandidates(const uint8_t* mem, int gearSlot, int charId,
                           uint8_t* cand, uint8_t* cnt);
// ROM C2/916E + C2/9137. Recomputes the 11 derived bytes for `sheet` (a 0x50 copy) into out[11]
// and, when writeBack != 0, into the LIVE sheet at +0x03 (curHP) and +0x36..+0x40.
void CtMenuRecalcDerived(const uint8_t* mem, const uint8_t* sheet, uint8_t* out11);
void CtMenuEquipApply(uint8_t* mem, int charId, int gearSlot, int newId);  // C2/9CC1..C2/9CE1
// ROM C2/8791 / C2/87D5 (the Item screen's CtMenuItemUse already needs the same two).
int  CtMenuInvAdd(uint8_t* mem, int id, int qty);   // 0 ok, 0xFF full
int  CtMenuInvRemove(uint8_t* mem, int id, int qty);// 0 ok, 0xFF absent, 0xFE short
```

**`CtMenuEquipCandidates`, byte-exact:**
```
static const uint8_t kEqRange[8] = { 0x00,0x5A, 0x7B,0x94, 0x5A,0x7B, 0x94,0xBC }; // C2/89DC
lo = kEqRange[gearSlot*2]; hi = kEqRange[gearSlot*2+1];
n = 0;
for (i = 0; i < 0xF2; i++) {
    id = mem[0x2400+i];
    if (!id || id < lo || id >= hi) continue;
    if (!(mem[0x7A00+id] & (0x80 >> charId))) continue;      // C2/93A8
    cand[n] = id; cnt[n] = mem[0x2500+i]; n++;
}
cand[n] = 0;
selection-sort cand[0..n) ascending by mem[0x7C00 + cand[k]], swapping cnt[] with it;  // C2/899C
return n;
```

**`CtMenuEquipApply`, byte-exact (ROM `C2/9CC1`):**
```
static const uint8_t kEqOff[4] = { 0x29, 0x27, 0x28, 0x2A };   // C2/A4AB
sheet = 0x2600 + charId*0x50;  off = kEqOff[gearSlot];
old = mem[sheet+off];
CtMenuInvAdd(mem, old, 1);              // ignores the FULL return, exactly like the ROM
mem[sheet+off] = newId;
CtMenuInvRemove(mem, newId, 1);
menuSfx(mem, 0xB5, "equip");            // C2/9CDE  LDA #B5 / JSR C2/EABA
if (gearSlot == 0 && charId < 6) glb[0x00B4 + charId] = 0;    // C2/9CFC  ($7F:00B4)
// then the ROM's tail: reload the sheet copy, recompute, rewrite +0x03/+0x36..+0x40, rebuild
// the HP-bar scale $0D38+charId*2 = maxHP/10, and rebuild the candidate list (the bag changed).
```
`CtMenuInvAdd` must keep the three sentinel refusals (`id == 0x5A || 0x7B || 0x94` -> no-op) — that
is what makes "equipping into an empty slot" consume the item and add nothing back, which is the
engine's whole unequip story.

**`CtMenuRecalcDerived`, byte-exact (ROM `C2/916E`):**
```
uint8_t bon[7] = {0}; int wpnPow = 0, defSum = 0;
for (pass = 3; pass >= 0; pass--) {           // accessory, helm, armor, weapon -- IN THAT ORDER
   id = sheet[ (pass==3)?0x2A : (pass==2)?0x27 : (pass==1)?0x28 : 0x29 ];
   if (pass == 3) {                                       // C2/9295
       hp = rd16(sheet+5);
       add = (id == 0xA1) ? (hp >> 1) : (id == 0xA0) ? (hp >> 2) : 0;
       hp = add + hp; if (hp > 999) hp = 999;
       out[9..10] = hp;  if (hp < curHP) curHP = hp;
   } else if (pass == 0) wpnPow = mem[0x7C00+id];         // C2/9276
   else                  defSum += mem[0x7C00+id];        // C2/9281 / C2/9286
   b = mem[0x7B00+id];                                    // the shared tail C2/92C1
   if (b) { w = rom16(0x0C29D7 + b*2) & 0xFFFE;
            mask = w & 0xFF; amt = w >> 8;
            for (k = 0; mask; k++) { if (mask & 0x80) bon[ kStatOff[k] ] += amt;
                                     mask = (mask << 1) & 0xFF; } }
}
static const uint8_t kStatOff[7] = { 0,2,1,4,5,3,6 };            // $FF:CF2C
static const uint8_t kStatCap[7] = { 99,16,99,99,99,99,99 };     // $FF:CF33 (indexed by k, not by offset)
for (k = 6; k >= 0; k--) { y = kStatOff[k];
    v = sheet[0x0B+y] + bon[y]; if (v > kStatCap[k]) v = kStatCap[k]; out[y] = v; }
switch (sheet[0] & 7) {                                          // C2/91DB
  case 1: case 2: atk = (2*(out[4] + wpnPow)) / 3;                       break;  // Marle, Lucca: out[4] = HIT
  case 5: atk = (16*out[0] + rom16(0x0C2D69 + sheet[0x12]*2)) / 9;       break;  // Ayla:  out[0] = POWER
  default:atk = (5*wpnPow + 12*out[0]) / 9;                              break;  // Crono/Robo/Frog/Magus
}
out[7] = min(atk, 255);                       // +0x3D Attack
out[8] = min(out[1] + defSum, 255);           // +0x3E Defence  (out[1] = STAMINA)
if (sheet[0x2A] == 0xA8) out[5] <<= 1;        // C2/9142  Third Eye doubles EVADE (+0x3B)
```
(the `$A8` doubling lives in the *callers* `C2/9137` and `C2/A5E7`, right after `C2/916E`, so it must
be applied after the loop and NOT inside it — and it is applied to the derived Evade, i.e. after the
99 clamp.)

### 5.3 The substate machine

```
sub 0 (build)      : blip $00 -> sub 1
sub 1 char-select  : Up/Dn move over $0085 members (wrap; scroll only when $0085 > 3, 12 frames/row)
                     A or RIGHT -> blip, sub 2, 24-frame slide, blip, build candidates for slot 0
                     B          -> screen 0 substate 1 (C2/9A98)
sub 2 gear panel   : Up/Dn wrap over the 4 rows (blip on change); L/R change character (blip, 1 yield);
                     any change of (slot + rosterIndex) -> 1 yield -> rebuild candidates
                     A: candidates == 0 -> ** BUZZ $01, stay **  ;  != 0 -> blip, sub 3, 7-frame slide
                     B or LEFT  -> sub 1 (blip, 26-frame teardown, gear cursor saved in $0FC5)
sub 3 list         : Up/Dn move cells 4..11; at an edge with count <= 8 it WRAPS silently; with
                     count > 8 it SCROLLS by one row (blip). L/R page by 8 (blip). Every move
                     re-previews: copy sheet, patch the gear byte, recompute, set the 10 arrow colours.
                     A -> COMMIT (equip SFX $B5) ; B -> discard. BOTH then: restore $54 from $0FC5,
                     reload + recompute + write the sheet, rebuild the HP scale, sub 2.
```
Input suppression: the frame a substate CHANGES, `C2/E984` publishes **no** button edges
(`$0D17 != $68`) — the engine must do the same or an A press will be eaten twice.

### 5.4 Deltas against `src/field.c` as it stands today (lines ~8231-8264)

1. **`menuStatSub == 1` is the ROM's substate 2, and it has nowhere to go.** Add substate 2 (ROM 3)
   and make `aHeld` in the gear panel branch on `menuEqCount`:
   `count ? (blip, sub 2, 7-frame slide, build the panel) : menuSfx(s7, 0x01, "equip-list-empty")`.
   Today the press is swallowed with no sound at all; the ROM **buzzes** (`C2/9C8D JMP C2/EACC`,
   measured `$7E:1E01 = 01` at §4.2 f187/f251).
2. **The second blip is now a measured constant, not a cadence to eyeball.** `src/field.c:8239-8244`
   declines to post `C2/9C5E`'s blip because "timing a second post to about 25 frames would be
   eyeballing". It is `C2/A71E`'s 24 yields + `C2/9C5B`'s 1 = **25 frames**, from the ROM's own loop
   bounds and measured f125 -> f150 (§4.2). It can be ported as a counter with a citation.
3. **`LEFT` and `RIGHT` are missing.** `C2/9C3C`: RIGHT confirms in char-select. `C2/9C73`: LEFT
   cancels in the gear panel. **`L`/`R` change the character in the gear panel** (`C2/A00B`,
   measured §4.2) — the engine has no L/R handling on screen 1 at all.
4. **The char-select cursor is the wrong list.** The engine wraps `menuStatSlot` mod 3 over `$2980`,
   skipping `0x80` slots. The ROM moves `$54` over the **packed** list `$7E:0D5F[0..$0085)` (built at
   menu open, `C2/816A`), whose length `$0085` is the ACTIVE count `$73` — or the WHOLE roster `$75`
   once `$7F:0000 >= 0x49` and `$7F:00BA` bit0 is clear (`FF/F813`) — with a 3-row window
   (`$0412` = 0..2) plus a scroll `$0413` capped at `$0FC4 = max($0085-3, 0)` and a 12-frame slide per
   scrolled row (`C2/9DAF`). On this repo's states (`$0085 = 2` or `3`) the two agree; on a 4+ roster
   they do not.
5. **The gear cursor's wrap is already right for the wrong reason.** `(slot+3)&3` / `(slot+1)&3`
   happens to match the nav graph's ring (cells 0..3, `up=03 dn=01 / 00 02 / 01 03 / 02 00`, §4.3),
   which is the canonical mechanism — quote the graph in the comment.
6. **Nothing recomputes stats.** `CtMenuRenderStatus`/`CtMenuRenderEquip` read the sheet's `+0x36..`
   block, which is only correct because no state in this repo has ever changed equipment. The moment
   `CtMenuEquipApply` exists, `CtMenuRecalcDerived` must run with it (and rebuild
   `$0D38 + charId*2 = maxHP/10`, `C2/813C`, which `C2/A64D` re-runs on every equip).

---

## 6. OPEN / BLOCKED ON A CAPTURE

1. **NO capture in this repo can reach substate 3.** I scanned every `saves/*.state` and
   `saves/battle_re/*.state` (WRAM at file offset 69 for a bare stream, 85 behind a `CTLS` header) for
   an inventory id in `[0x01,0xBC)`: **there is none** — every state holds only ids 189/198/199/200/214
   (Tonic / Shelter / Pendant / ...), all `>= 0xBC`. So `$0FC6 == 0` everywhere, `A` on a gear row
   always buzzes, and the candidate list, the preview arrows, the commit, the equip SFX `$B5` and the
   `$7F:00B4` clear are **derived from the ROM only and NOT measured**. Specifically unmeasured:
   `C2/9CC1-9CFC`, `C2/A5C3`, `C2/A5E7`, `C2/9F05`, `C2/93BF`, `C2/EA53`'s scrolling branch,
   `C2/A41A`'s 7-yield slide, and the L/R page direction of `C2/93BF` (the ROM text says bit1 = R
   subtracts a page, bit0 = L adds one — the opposite of `C2/A00B`'s R = next; I could not drive it).
   **The capture that unblocks all of it:** a savestate on the field/overworld with the menu open and
   at least two spare weapons/armors in `$7E:2400` for the lead character — e.g. leave Truce with a
   bought Iron Blade + Bronze Mail, or a post-Guardia-Forest state carrying a second weapon. One
   `--peek`/`--monitor` run on it would grade the commit end to end (watch
   `7E2400:8,7E2500:8,7E2627:4,7E0F00:8,7E0FC6:1,7E1E01:1,7E2636:B`).
   **Note what IS graded despite this:** the recompute (gap (d)) is validated 7/7 against the
   oracle's own sheets (§4.7), the candidate PREDICATE's inputs (`$7A00`, the ranges) are validated
   against the live property page (§4.1), and the refusal/nav/SFX half of gap (e)/(f) is driven
   frame-exactly (§4.2/§4.4). What is unmeasured is the *transition into* substate 3 and everything
   inside it.
2. `$7E:7700`: six ids (`02 09 11 19 22 29`) carry a `0x40` the §2.7 recipe does not produce. Not on
   the equip path (`C2/93A8` reads `$7A00`, never `$7700`), but the Item screen's usable-flag model
   should be re-checked before it is trusted for those ids.
3. The exact SCREEN ROWS of the substate-3 comparison panel. `C2/A642 = 3D 3E 36 38 3B 39 37 3A 3C`
   fixes the ORDER of the ten arrow colours `$0D4D..$0D55` + `$0D57` (Atk, Def, Power, Speed, Evade,
   Magic, Stamina, Hit, M.Def, then HP), but the panel it decorates is built by `C2/ED31` list
   `$FF:BEE2`, whose command language is undecoded (menu2_shell_exit.md §2.13 leaves it out of scope).
   The **substate-2** panel (list `$FF:BE0E`) is fully measured in §4.5/§4.7 and lays the same stats
   out in a different order (`PWR/HIT/MAG./M DEF.` left column, `SPD./EV./STAM.` right), so "which
   arrow sits on which printed row of the substate-3 panel" is **not** pinned.
4. `$7F:00B4 + charId` — proven to be written only by `C2/9D11` and read only by `CF/FBCC` (into
   `$7E:A09B + partySlot`, gated on `$7F:00BA & 01`), but **what it means** is not derived.
5. `C2/9CFC` indexes `$0D5F` with `$0412` (the 0..2 window slot) rather than `$71`. With a roster
   larger than 3 AND the char window scrolled, the ROM clears the wrong character's `$7F:00B4`. I
   could not reach that configuration (roster 4, `$0FC4 = 1`) with a menu-open capture; the code path
   is read from the listing.
6. `C2/A5E7`'s colour codes `0x00 / 0x08 / 0x1C` are read from the ROM; I did not confirm which
   palette/tile attribute each produces on screen (needs substate 3).
7. `$00:0051` was measured as 0 on this state, so `C2/A5E7` writes all nine `$0D4D` slots. No writer
   to `$51` other than `C2/80FF STZ $51` was located, but I did not prove it never changes on another
   screen path.

---

# ADVERSARIAL VERIFICATION (second agent, independent re-derivation)

Everything below was re-disassembled from the ROM and/or re-measured on the oracle by a second agent
who did not take any citation above on trust. Commands are verbatim; output is pasted.

**Headline: §6.1 is wrong. Substate 3 IS reachable in this repo, and it grades most of what §6.1
declared unmeasurable — including one MAJOR defect (the list sort runs the wrong way) that only a
substate-3 capture could have caught.**

`scratchpad/menu/eqlist.state`, `eqdone.state` and `acc.state` (created 2026-08-29 19:49-20:06,
i.e. before this dossier was written) sit inside screen 1 with real gear in the bag. §6.1's scan
covered only `saves/*.state` and `saves/battle_re/*.state`.

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --read "wram:0100+2,wram:0067+2,wram:00BE+2,wram:2400+8,wram:2500+8,wram:0FC6+1,wram:0077+1,wram:2627+4" \
  --from scratchpad/menu/eqlist.state
  wram[000067+2] = 0100          <- screen 1
  wram[0000BE+2] = 0300          <- ** SUBSTATE 3 **
  wram[002400+8] = 03040F0000000000   wram[002500+8] = 0101010000000000
  wram[000FC6+1] = 03            <- three candidates
  wram[000077+1] = 00            <- gear row 0 = Weapon
  wram[002627+4] = 7C5B0195      <- Crono's live sheet +0x27..+0x2A
```
(`acc.state`: same screen, `$00BE = 0003`, `$0077 = 03` = the **accessory** row, bag
`03 04 0F 5D 97 69 60 AD A3 8F`. `eqdone.state`: `$00BE = 0002`, one commit later.)

⚠ `--read`/`--monitor` lengths are **DECIMAL**. `wram:2400+F2` silently reads ~1 byte; that is how a
"no state has gear" scan can come back clean. Use `wram:2400+242`.

---

## A. REFUTED

### A1 [MAJOR] The candidate list is sorted **DESCENDING**, not ascending (§2.5, §5.2)

`python tools/dis.py 28953 0x8A 1 0` — the inner loop:
```
  289AA: INX
  289AB: LDY  $0F00,X
  289AE: LDA  $7C00,Y
  289B1: BEQ  ->289D6      ; ** a ZERO key ENDS the inner scan (INC $01, restart outer) **
  289B3: CMP  $00
  289B5: BCC  ->289AA      ; ** key <  running key -> SKIP **
  289B7: STA  $00          ; key >= running key -> it becomes the new running key
  289B9..289D4: swap $0F00[$01]<->$0F00[X] and $0F62[$01]<->$0F62[X]
```
`BCC` skips the **smaller** element, so each outer pass leaves the **maximum** of the tail at
position `$01`. That is a descending selection sort. Measured twice:

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --read "wram:0F00+8,wram:7C03+2,wram:7C0F+1" --from scratchpad/menu/eqlist.state
  wram[000F00+8] = 0F04030000000000
  wram[007C03+2] = 0F14        wram[007C0F+1] = 19
```
ids `0F, 04, 03` carry `$7C00` = `0x19, 0x14, 0x0F` — strictly **decreasing**. (Names: `0F` Bolt
Sword, `04` Lode Sword, `03` SteelSaber; `$CC:0B5E + id*11`.)

The armor row, reached by driving `acc.state` B -> Up -> A:
```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E00BE:2,7E0077:1,7E0054:1,7E0F00:6,7E0FC6:1,7E0D26:1" --from scratchpad/menu/acc.state \
  --btnscript "0:10,1:2,0:40,10:2,0:30,100:2,0:40" --diff
  f53  7E00BE=0200  7E0077=02*  7E0054=02*  7E0F00=97ADA3000000  7E0D26=2A
  f54  7E00BE=0200  7E0077=02   7E0054=02   7E0F00=69605D000000* 7E0D26=28*
  f85  7E00BE=0300* ...
  f92  7E00BE=0300  7E0054=04* ...
```
bag order was `5D`(idx3), `69`(idx5), `60`(idx6); the list is `69 60 5D` with `$7C00` = `0x4B, 0x20,
0x10` — reordered **descending by power**, i.e. the best piece is row 0.

**§5.2's pseudocode (`selection-sort … ascending`) puts the weakest gear at the top of the panel.**

### A2 [MAJOR] The sort has a **zero-key terminator** that §5.2 omits

`289AE-289B1` treats `$7C00[id] == 0` as *end of list*, not as a comparison. It is reached both by the
0-terminator (`$7C00[0] = 0`, measured) and by any real item whose `$7C00` byte is 0.

### A3 [MAJOR] `$7C00` is built only for ids `0x00..0x93`, so the **accessory list is never sorted**

`C2/994C`: `STZ $00 / STZ $01 / … / 29965: CMP #94 / BCC ->29950`. §2.7 step 4 states this correctly,
but §2.5/§2.15/§3/§5.2 then use `$7C00[id]` as "the gear power byte **and the LIST SORT KEY**" for all
four rows. Accessory ids are `0x94..0xBB` — outside that loop. Measured on the live page:
`$7C00[0x94..0xF1]` is all `00` (see A4's 0/242 diff). Combined with A2 the accessory scan aborts on
its first element, so the list keeps **raw `$2400` scan order**:

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --read "wram:2400+16,wram:0F00+8,wram:0FC6+1,wram:0077+1,wram:0D26+2,wram:7C97+1,wram:7CA3+1,wram:7CAD+1" \
  --from scratchpad/menu/acc.state
  wram[002400+16] = 03040F5D976960ADA38F000000000000
  wram[000F00+8]  = 97ADA30000000000     <- inventory order (idx 4, 7, 8), UNSORTED
  wram[000FC6+1]  = 03    wram[000077+1] = 03    wram[000D26+2] = 2A00
  wram[007C97+1]=00  wram[007CA3+1]=00  wram[007CAD+1]=00
```
The engine must reproduce this: **accessory rows come out in bag order.**

*Already biting:* the in-flight port bakes `static const uint8_t ctEqPower[148]` (`src/data/menu_equip.h`,
correctly 148 = ids 0..0x93) and then sorts with `ctEqPower[cand[b]]` (`src/menu.c` `CtMenuEquipCandidates`).
For the accessory row `cand[]` holds ids `0x94..0xBB` = 148..187 — an **out-of-bounds read** on every
accessory list. The fix is the ROM's own shape: key = `id < 0x94 ? power[id] : 0`, plus A2's terminator.

### A4 [MAJOR] §2.7 step 3 is wrong twice — wrong range AND wrong index — and it corrupts `$7B00`, which IS on the equip path

`python tools/dis.py 29875 0x120 1 0`:
```
  29924: LDA  #0094          ; ** the loop STARTS at id 0x94 **
  29927: STA  $00
  2992B: LDA  $00
  2992D: JSR  C2/8881
  29930: JSR  C2/88D8        ; ** the C2/88D8 index (X2 = $FF:CF1D + off*$FF:CF27), NOT C2/88AD **
  29935: LDA  $CC0001,X / BIT #40 / BEQ ->29944
  2993D: LDA  $CC0002,X / STA $7B00,Y
  29948: CMP  #BC / BCC ->2992B
```
§2.7 says "`for id 0..0xBB`" and indexes with the `C2/88AD` `X`. Both wrong. Re-deriving all four
pages with the corrections (`scratchpad/menu/adv/pages.py`) against the same live WRAM the dossier
used:
```
python scratchpad/menu/adv/pages.py scratchpad/menu/eq0.raw.wram
7700: 6 mismatches / 242 [(2,0,64),(9,128,64),(17,0,64),(25,0,64),(34,0,64),(41,128,64)]
7A00: 0 mismatches / 242 []
7B00: 0 mismatches / 242 []
7C00: 0 mismatches / 242 []
```
**`$7B00` goes 64 -> 0.** §2.7/§4.1's diagnosis ("the model non-zero where live is 0 … the zero-fill of
step 2 is wider than I modelled") is a wrong explanation of a plain mis-read of the loop.

This matters: `C2/92C4 LDA $7B00,X` is read for **all four** equipment passes, so a `$7B00` page built
from §2.7's recipe hands the wrong `$CC:29D7` bonus-set index to 64 non-accessory ids.

### A5 [MINOR] §2.7 step 2's zero-fill bounds are `0xBC..0xF1` and `0x94..0xF1`, not `..0xEF`

`MVN` copies `A+1` bytes: `STZ $7ABC / LDX #7ABC / LDY #7ABE / LDA #0033 / MVN` -> `$7ABC..$7AF1`;
`STZ $7B94 / … / LDA #005B / MVN` -> `$7B94..$7BF1`. With that, `$7A00` model vs live is **0/242**
(§4.1 reported 2, at F0/F1 — the same off-by-two).

*Already biting:* `src/data/menu_equip.h`'s `ctEqBonusIdx[242]` ends `0x00,0xEF,` — id `0xF1` carries
`0xEF` where the oracle's live `$7B00[0xF1]` is `00`, because the generator stopped the zero-fill at
`0xF0`. Inert (0xF1 is outside every gear range) but not byte-exact.

### A6 [MAJOR, method] Reading `$7E:1E01` to identify a played sound is **unsound**, and it mis-reads the commit frame

`python tools/dis.py 2EAF9 0x30 1 1` — the shipper `C2/EB03`:
```
  2EB03: LDX  #18
  2EB05: STX  $1E00        ; cmd, UNCONDITIONAL
  2EB08: STA  $1E01        ; ** the id byte is written BEFORE the throttle is consulted **
  2EB0B: LDA  $0D76
  2EB0E: BNE  ->2EB1E      ; ** throttled -> RETURN: no $1E02, no JSL C7:0004, nothing plays **
  2EB10: LDA #03 / STA $0D76 / LDA #80 / STA $1E02 / JSL C7:0004
```
The commit posts `$B5` (`29CDE`) and then `29CE4 JSR C2/EAC2` posts `$00` **in the same frame**; the
second post is dropped but still clobbers `$1E01`. A real commit therefore *reads* as a blip:
```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E00BE:2,7E0054:1,7E2400:4,7E2500:4,7E2629:1,7E9AB9:1,7E1E00:4,7E0FC6:1,7E0F00:4,7F00B4:1,7E0D76:1" \
  --from scratchpad/menu/eqlist.state --btnscript "0:10,100:2,0:90" --diff
  f0   7E00BE=0300* 7E0054=04* 7E2400=03040F00* 7E2500=01010100* 7E2629=01* 7E9AB9=0F* 7E1E00=00008006* ...
  f11  7E00BE=0300  7E0054=00* 7E2400=03040001* 7E2500=01010001* 7E2629=0F* 7E9AB9=0F  7E1E00=18008006* 7E0D76=03*
  f19  7E00BE=0200* ...                                            7E0F00=04030100*
```
`$1E01 = 00`, not `B5`. The `$B5` really is dispatched — measured at the APU ports:
```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --callrom C2/EABA --rega B5 \
  --from scratchpad/menu/eqlist.state --writes
  w 002143 = 06  (by C7:0623)
  w 002142 = 80  (by C7:0628)
  w 002141 = B5  (by C7:062D)      <- ** the equip SFX id **
  w 002140 = 18  (by C7:0635)
```
So §2.4/§5.2's "`$B5`" is right, but §4.6's census method and §4.2's `$7E1E00` reading cannot see it,
and any port validated that way would silently ship a blip instead.

### A7 [MINOR] §2.13's "242-slot-full bag" — `C2/8791`'s free-slot scan is **256** slots

`287AB: LDA $2400,X / BEQ / INX / BNE ->287AB` with `x8`: X runs 0..255 (and it enters at X=0 because
`C2/87FA` wraps X to 0 on a miss). `C2/8953` scans only 0..0xF1, so ADD can park a returned piece in
slots `0xF2..0xFF` that the equip list can never show. Measured in the armor commit below: with slots
0..9 occupied the old armor `5B` went to slot 10.

### A8 [MINOR] §5.2's `CtMenuRecalcDerived(..., const uint8_t* sheet, ...)` cannot implement `C2/916E`

`292B9: CMP $9A93 / BCS ->292C1 / 292BE: STA $9A93` — the accessory pass **writes curHP back into the
sheet COPY**, and `C2/9137`'s `2914B: LDA $9A93 / STA $0003,Y` then propagates that clamped value to
the live sheet. A `const` sheet cannot carry the clamp; the copy must be mutable.

### A9 [MINOR] §2.6's predicate is keyed on the **raw sheet byte 0**, not on "charId"

`293B5: LDX $9A90` — there is **no `AND #07`** here, unlike `C2/91AD` (`LDA $9A90 / AND #07`) and
`C2/885F`. `$FF:F9BB` is only 9 bytes long. Equivalent for ids 0..6, but the spec sentence
"`equippable(id, charId)`" hides a real asymmetry between two routines that read the same byte.

---

## B. CONFIRMED (re-derived and, where §6.1 said "not measured", now measured)

* **B1 the substate table and the four handlers.** Raw `d[0x29C29:0x29C31] = 31 9C 39 9C 70 9C A5 9C`;
  `29C31/29C39/29C70/29CA5` disassemble exactly as §2.1-§2.4 print them, including
  `29C83: LDA $0077 / STA $54`, `29C88: LDA $0FC6 / BNE / 29C8D JMP C2/EACC`.
* **B2 the empty-list BUZZ.** `--monitor "7E00BE:2,7E0FC6:1,7E1E00:4,7E0D76:1" --from
  saves/com_jogo_salvo.state --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60,100:2,0:40" --diff`
  -> `f187 7E1E00=00018007* 7E0D76=03*` with `7E00BE=0200` unchanged. id `01`, substate held. (One
  post that frame, so A6's caveat does not bite here.)
* **B3 `C2/A4AB = 29 27 28 2A`, MEASURED end-to-end.** An armor commit driven out of `acc.state`
  (`--btnscript "0:10,1:2,0:40,10:2,0:30,100:2,0:40,100:2,0:60"`, monitor
  `7E2627:4,7E2400:10,7E2500:10,7E2636:11`):
  ```
  f92   7E0D26=28  7E2627=7C5B0195  7E2400=03040F5D976960ADA38F  7E2636=05080D0508080208104600
  f127  7E0D26=28  7E2627=7C690195* 7E2400=03040F5D970060ADA38F* 7E2636=05080D0508080208564600*
  f135  7E00BE=0200*
  ```
  **only sheet `+0x28` moved (`5B` -> `69`)**, and `$0D26` was measured `29` (weapon, eqlist),
  `28` (armor) and `2A` (accessory, acc.state). The whole offset table is now measured, not read.
* **B4 the commit ORDER (add-old, write-sheet, remove-new).** In the eqlist run the old `01` landed in
  slot **3** while slot 2 still held `0F` — only possible if `C2/8791` runs before `C2/87D5`.
* **B5 `$7F:00B4`.** `--callrom C2/9CFC --from scratchpad/menu/eqlist.state --writes` ->
  `w 7F00B4 = 00  (by C2:9D15)` (2 writes total; `$0077 = 0`, `$0412 = 0`, `$0D5F[0] = 0`).
* **B6 the PREVIEW touches only the copy.** eqlist Down/Down/Down/Up/Up/Up: `$9AB9` tracks the cursor
  (`0F -> 04 -> 03 -> 0F -> 03 -> 04 -> 0F`) while `$2629` stays `01` for all 124 frames.
* **B7 the arrow colours 00 / 08 / 1C.** eqlist (weak weapon equipped) gives `$0D4D = 08` with
  `$9B1C..$9B26 = 05080D0508080214104600` vs `$9AC6.. = …08104600` (Atk 0x14 > 0x08). eqdone (the
  strongest weapon equipped) gives `$0D4D = 1C` at the moment the preview lands
  (`f19 7E0D4D=1C0000000000000000* 7E9B1C=…11104600*`). `$0D57` stayed 00 (HP unchanged) in both.
* **B8 the RECOMPUTE, live, through the equip path.** B3's `$2636+11` went
  `05 08 0D 05 08 08 02 | 08 | 10 | 46 00` -> `… | 08 | 56 | 46 00`: Def `0x10 -> 0x56` =
  `Stamina 8 + ($7C00[0x7C]=3 + $7C00[0x69]=0x4B)` = `8 + 78 = 86`. `--callrom C2/916E --writes` on
  eqlist shows `$0DB6 = 08`, `$0DB5 = 19` (read from the **copy's** patched `$9AB9`), the seven
  `$9B1C..$9B22` stores, and `$4202/$4203 = 05/19` then `0C/05` — i.e. `(5*25 + 12*5)/9 = 20`, which
  is the `$9B23 = 0x14` in B7. Gap (d) is now graded **on the equip path**, not only against sheets
  that `C2/99B2` had just written with the same routine at menu open.
* **B9 the 7-yield list slide.** Measured three times (eqdone: substate 3 at f11, `$54 = 04` at f18;
  acc->armor: f85 -> f92; eqlist family). Exactly 7.
* **B10 the 26-frame exit slide for window slot 2 — §6's "assumed" is CORRECT.**
  `--from saves/com_jogo_salvo.state --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60,800:2,0:40,800:2,0:40,40:2,0:60"`:
  ```
  f229  7E0412=02*  7E0071=02*  7E0D1D=02*
  f271  7E00BE=0100*  7E0D25=... (LEFT accepted)
  f273  7E0D24=00*  7E0D25=10*        <- $0D25 = $0412*8 = 16, phase 1
  f297  7E0071=02*  7E0054=02*  7E0D24=18*   <- restore at f271+26
  ```
  `$0D24` runs `00..0x18` past `$0D25 = 0x10`, which is the second phase the dossier inferred.
* **B11 the 25-frame blip gap.** `f125 7E0D76=03* 7E0D25=18* 7E0084=FF*` -> `f150 7E0D76=03*`.
* **B12 `$0D1D` bits, MEASURED.** A (`0x100`) -> `7E0D1D=80`; R (`0x800`) -> `7E0D1D=02` at f187/f229
  above. So `C2/93BF`'s `BIT #02` really is R. (Its *direction* — R subtracts a page — is still
  listing-only; no state has >8 candidates. Re-read at `293C9-293F5` and it is as printed.)
* **B13 `C2/8255` really does kill slot 4.** `28255` decodes `A` as a **bitmask**: `LSR $00` per slot
  with `A += 8`; `#10` -> bit4 -> `X = 0x20` -> `STZ $B0+0x20` = slot 4. The phrasing "kill task slot
  4" is right, but the immediate is a mask, not an index.
* **B14 everything else re-disassembled and matching**: `C2/E984`/`E9E2`/`EA1F`/`EA27`/`EA36-EA52`
  (the `+5/+4/+3/+2` graph fields and the `LDA #80 / TSB $5A / TRB $54` edge), `EA81` (the window
  arithmetic; measured `$0DD8/$0DDA/$0DDB/$0DDC = 08/00/04/06` at count 3), `8791`/`87D5`/`87FA`,
  `916E`/`9261`/`926E`/`9276`/`9281`/`9286`/`9295`/`92C1`, `91EB`/`9223`/`923C` (Ayla's divisor is
  indeed `#09`; `9223` reads `$9B20` = derived offset 4 = HIT), `9137` (incl. the `ASL $9B21` **before**
  the MVN), `A5C3`/`A5E7`/`A642`, `A41A`/`A45D`/`A463`/`A4AB`, `9D17`, `9ECD`, `A00B`, `9A77`,
  `8881`/`88AD`/`88D8`, `FF/F813` (`$85 = $73`, or `$75` when `$7F0000 >= 0x49 && !($7F00BA & 1)`).
  The bonus table `$CC:29D7` idx1 = `0140` -> mask `0x40` (bit6 -> offset 2 = Speed) amount 1 =
  the Bandana, as §4.1 says.
* **B15 the three sentinels are unreachable as candidates** — a stronger argument than §2.13's.
  Live page: `$7A00[0x5A] = $7A00[0x7B] = $7A00[0x94] = 0x00`, so `C2/93A8` rejects them for **every**
  character even if one were in `$2400`. Their name-table rows (`$CC:0B5E + id*11`) are all-`FF`
  filler, as are `0x00`, `0x59` and `0xBC`.

---

## C. AN OPEN ITEM NOW CLOSED, AND A NEW BOUNDARY

**§6.2 (`$7700`'s stray `0x40`): the writer is located.** It is inside `FF/F813` itself, after the
`TSB #80` block:
```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --callrom FF/F813 --long --regdb 7E \
  --from scratchpad/menu/eqlist.state --writes
  w 7E7709 = C0  (by FF:F833)     <- the TSB #80 block, on top of an existing 0x40
  ...
  w 7E7701 = 40  (by FF:F8B8)     <- ** a later, state-dependent STA #40 pass **
  w 7E770A = 40  (by FF:F8B8)
```
It uses `STA`, not `TSB`, so it can *replace* the `0x80` — which is exactly why live `$7709` and
`$7729` read `0x40` and not `0xC0`. §2.7 step 5 is therefore incomplete, and no `$7700` model can
match the live page without `FF/F8B8`. Which ids it hits depends on RAM (it differs between
`eq0.raw.wram` and `eqlist.state`), so the rule itself is **still underived**.

**New boundary raised by A3:** because `$7C00[0x94..0xBB]` is never written by the page build, its
value in a fresh boot is whatever `$7E:7C94..7CBB` last held. It measured `00` on every capture I
looked at, and the accessory list depends on that being 0 — but "who guarantees the zero" is not
derived. If it is only the console's power-on clear, an engine that reuses the buffer must zero it.

---

## D. STILL OPEN (after this pass)

1. Anything needing **more than 8 candidates for one gear row**: `C2/93BF`'s page direction,
   `C2/EA53`'s scrolling branch, `C2/9F05`'s two silent refusals, `$0FC8`, `$0DDA != 0`. The richest
   bag available (`acc.state`) has at most 3 per row. **The capture that unblocks it:** a menu-open
   state with >=9 items inside one `[low,high)` window for the selected character.
2. A character sheet actually holding `0x5A` / `0x7B` / `0x94` / weapon `0x00`. Every sheet in every
   state in the repo (chars 0..5, six states checked) has real gear in all four slots, so §2.13's
   "equipping into an empty slot consumes the item" is still **inference from `C2/8791`'s refusal**.
3. The `0xA0` / `0xA1` max-HP accessories and `0xA8` (Third Eye, `ASL $9B21`): no capture has one
   equipped or in the bag, so `C2/9295`'s two shift arms and the Evade doubling stay listing-only.
4. §6.3 (the substate-3 panel's printed row order, list `$FF:BEE2`) — unchanged.
5. `C2/9CFC`'s `$0412`-vs-`$71` quirk still needs a roster > 3 with the window scrolled.

---

## E. VERDICT

**SOUND WITH FIXES.** The ROM trace is accurate almost everywhere — every address I re-disassembled
contained what §2 says it contains, and §2's hardest claims (the substate table, the refusal, the
commit sequence, the sheet-offset table, the recompute arithmetic, `$7F:00B4`, the frame costs) all
survived, several of them upgraded from listing-only to measured. But **do not implement §5.2 as
written**: the sort direction is inverted (A1), its terminator is missing (A2), the accessory row must
not be sorted at all (A3), and the `$7B00` recipe in §2.7 that feeds the stat-bonus lookup is built
from a mis-read loop (A4). A6 additionally invalidates the technique used to certify the SFX, so any
"the port matches the mailbox" check on the commit frame will agree with a wrong port.

---

# F. THE SUBSTATE-3 WINDOW — where the candidate list is actually DRAWN (2026-09-12)

Closes the render half of §D.1 ("what is unmeasured is the *transition into* substate 3 and everything
inside it"). Trigger: a user capture of the real screen — `divergence_equip.state`, 600 A.D. overworld,
menu open, `$0067 = 01`, `$00BE = 03`, `$0077 = 03` (Accessory), `$0FC6 = 01`, `$0F00[0] = 0x97`
(PowerGlove) — against which the port drew the list in the WRONG BOX.

## F.1 The list rows live in ONE place; the WINDOW is what moves

`C2/A569` (`python tools/dis.py 2A569 0x90 1 0`):

```
2A569: LDY #0008 / STY $22          ; eight rows
2A571: LDX #4FC6                    ; ** the BG1 tilemap destination of row 0 **
2A574: LDA $0FC8 / AND #00FF / TAY  ; Y = the scroll
2A57D: LDA $0F00,Y / STA $04C9 / BEQ + / INY
2A58A: JSR C2/A59A                  ;   -> C2/F2E2 + C2/EF65 (the 8x8 tilemap writer), or 24 blanks
2A58F: PLA / CLC / ADC #0080 / TAX  ; ** +0x80 per row **
2A596: DEC $22 / BNE loop
```

Measured in BOTH substates (`$7E:4FC6 + row*0x80`, attribute `0x20`, tile = the char code):

| | row 0 | row 1 | row 2 |
|---|---|---|---|
| oracle substate 2 (`saves/eq_spare.state`) | `Red Katana` | `SteelSaber` | `Iron Blade` |
| oracle substate 3 (same, one more A) | `Red Katana` | `SteelSaber` | `Iron Blade` |
| the user capture (substate 3) | `PowerGlove` | blank (`FF`) | blank |

**Byte-identical.** `$7E:4E00 -> VRAM word $6800` (descriptor `d[0x2FC1B:0x2FC23] = 00 68 00 4E 7E 80
06 80`), so `$4FC6` is map entry `(0x4FC6-0x4E00)/2 = 227` = row 7, col 3 = **x = 24**, which is where
both oracle renders put the names. Nothing about the text moves between the two substates.

## F.2 What moves: `$FF:BEA4`'s BLOCK COPY, 4 tile rows = 32 px

`C2/A41A` (entering substate 3) = `JSR C2/A4AF / LDX #BEEC JSR C2/ED31 / ... / JSR C2/A463 / LDA #04
STA $54 / JMP C2/EAC2`, and `C2/A4AF` opens with `LDX #BEA4 / JSR C2/ED31`. Decoding both lists through
the `C2/ED31` interpreter + the `$FF:BD18` parameter counts (menu2_save_rest.md §2.3):

```
$FF:BEA4   08 00 3E                  ; $61 = $3E00   -- the BG2 chrome buffer
           02 02 0C 02 08 0C 0E      ; BLOCK COPY (col 2,row 12) 12w x 14h -> (col 2,row  8)
           08 9A 96 / 0E 56 B9 / 0C 10 43 01 31 / 0E 8F BB
$FF:BEEC   08 00 3E                  ; $61 = $3E00
           02 02 20 02 16 1C 04      ; BLOCK COPY (col 2,row 32) 28w x  4h -> (col 2,row 22)
           0C 40 43 00 05 / 0E 67 B9 / 08 9A 96 / 0E FC B8
```

So the `[item list]` box climbs **four tile rows = 32 px**, swallowing the `[slot-type "Weapon"]` box,
and the bottom description bar is restored from the off-screen backup at row 32. The same 32 px is in
the list scrollbar sprite's Y base `$0D93` — the only reader is `C2/8BE9` (`LDA $0D92 / STA $00 / STA
$02 / ... ADC $0D93`), i.e. `$0D92` = sprite X, `$0D93` = sprite Y — written as the high byte of
`C2/A45D`'s `LDX #6268` (substate 2) vs `C2/A41A`'s `LDX #4268` (substate 3):

```
$0D92/93   substate 2: 68 62      substate 3: 68 42      0x62 - 0x42 = 0x20 = 32
```
(read back off both oracle WRAMs and off the user capture — the user capture holds `68 42`.)

**Confirmed against the capture, not assumed.** `tools/_gen_menuchrome_equiplist.py` dumps the BG2
tilemap (word `0x6000`) of the real substate-3 screen; its rows 8..21 cols 2..13 are **byte-identical**
to `ctMenuChromeEquipTM`'s rows 12..25 cols 2..13 — the `02` command, verbatim. Rows 22..25 (the
description bar) come from the row-32 backup, which is NOT in the visible map, which is exactly why the
chrome has to be captured rather than assembled.

## F.3 The two layouts, measured

`ct.exe --peek saves/eq_spare.state out.raw --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60[,100:2,0:60]"`
(add the last pair for substate 3), both rendered and profiled:

| | substate 2 (gear select) | substate 3 (candidate list) |
|---|---|---|
| left boxes | char panel y16..61, **slot-type box y64..95**, list box y96..207 | char panel y16..61, **list box y64..173**, description bar y176..205 |
| list rows | **y = 108 + row*12**, x24 | **y = 76 + row*12**, x24 |
| hand | on the gear row (x120, `$9380` cells 0..3) | on the list (x8, cells 4..11: y 75,87,…,159) |
| right box | full, EXP y184 / NEXT y192 shown | shortened to tile row 21 — **EXP/NEXT masked** |
| description | none | `C2/A44A -> C2/F2F3`, y188 |

`108 = 76 + 32`: one origin plus the ROM's own block-copy delta, not two constants. EXP/NEXT are NOT
erased — `"EXP"`/`"NEXT"` sit at `$7E:0345E`/`$7E:0349E` in BOTH oracle WRAMs; the shortened window
masks them. The window-gradient list `$7E:9D99` is `00 10 | 01 C0 | 00 10 | 80` in all three captures,
so the ramp does not change with the substate.

**And `C2/9C61` draws the list in substate 2 too** (`JSR C2/8953 / JSR C2/A569 / LDX #FC1B / JMP
C2/8385`): the ROM shows you what you could put in the row you are standing on, 32 px lower. The port
drew nothing there.

## F.4 The port, and what it was doing wrong

`src/menu.c` reused `CtMenuChromeEquip` (the substate-2 chrome) for substate 3, so the first two list
rows landed inside the slot-type box and the rest spilled across its bottom edge — the user's report.
Fixed structurally: `ctMenuChromeEquipListTM` (captured), `CtMenuChromeEquipList`, and ONE row renderer
`mEquipCandRows(..., yOff, hand)` called with `yOff = CT_MENU_EQ_LIST_RISE (32)` from the gear panel and
`0` from the list, plus `mClipBot` for the shortened right box. Validated: every chrome rule and every
text band matches the oracle in BOTH substates, and matches the user's own capture (§F.1) exactly.
Remaining pixel delta: the description line is our fixed 8x8 font where the ROM runs its VWF (the
pre-existing boundary in menu_item_descriptions.md), so `"Magic +2"` spaces differently.

---

## 5. PORTED (2026-09-24): the list's climb and descent, and the live preview

User report (translated from Portuguese): *"when opening to equip weapons, the box does not 'rise' with the canonical animation"*. Graded by
`menu-equip-list-climb-and-preview-vs-oracle` (`tools/menu_equip_list_cmp.py`, every frame f138-f330 of one drive on
party3_field + a poked Iron Blade, against the logical oracle: the window rules' rows, the bar's pixels, the Attack
digits' pixels -- 0 frames differ; six mutations).

* **The climb, C2/A41A -> C2/A4AF.** A over a gear row: the list rows cleared, then eight passes one frame apart
  (C2/A518, `JSR C2/821E` while `$96A3 < 0x20`), each +4 on the two offset-per-tile tables (`$5CC2`, the left column's
  11 columns from `$60FF`; `$5D42`, 27 columns from `$402F`) and `$96A0` -4. On screen: the [slot label][list] column
  climbs 4 px a frame in under the picked member's box (which stays on top), and the description bar climbs behind it
  from the window's bottom (its top at 176 + (32 - s)); the right-hand detail and the gear hand do not move. The first
  pass runs in the A frame itself (C2/A518 before the loop's first yield), so the A frame's picture is s = 4 and the
  eighth frame's is the settled list with its hand (C2/A463 runs in the frame the loop exits).
* **The settled list's window ramp.** Once the loop exits, the band table is re-pointed (`$9694/$9696/$9698 =
  969A/9300/0025`, against `0010` during the climb): the bar gets a 32-line ramp of its own -- MEASURED with `ct.exe
  --hdma`: option 0's 192-line ramp stops at line 176, and lines 177-208 are a 16-step ramp of 2 lines (8 .. 0 .. -7).
* **The way back, C2/A64D -> C2/A697** (after B, and after a commit): C2/A64D reloads the sheet copy, redraws and
  yields once (that frame: the list's hand gone, the gear hand back), then C2/A697 runs the same passes with `$26 = -4`
  from s = 32 down to 0 (`$9698 = 0x10`: the climb's band table again); C2/A45D redraws the gear panel's list after it,
  visible the frame after the descent ends. The B (or commit) frame is C2/A64D's: the list's hand gone, the gear hand
  back.
* **The live preview, C2/A5C3 / C2/A5E7** -- the port had none. From the frame after the list appears (substate 3's
  handler), the candidate is patched into the sheet COPY (`$9A90 + $0D26`, C2/A4AB = 29 27 28 2A by slot), recomputed
  (C2/916E, the Third Eye doubling), and each of C2/A642's rows -- Atk +3D, Def +3E, Power, Speed, Evade, Magic,
  Stamina, Hit, M.Def -- drawn with its NEW value, `$0D4D+row` = 00 unchanged / 08 raised (palette 2: 0,255,255) / 1C
  lowered (palette 7: 140,148,148).

**CORRECTED 2026-09-24 (the same day): the frame alignment.** This section was first measured with "port frame F =
`--peek` F+1", and three details were modelled to fit it: the climb standing still on the A frame, the descent starting
from 36, and the commit frame drawing the list as it was before the commit. They were one compensating frame, not ROM
behaviour: the cartridge displays frame P's state during P+1, i.e. `--peek` P+2 (tools/menu_audit_cmp.py measured it on
every kind of change -- the cursor, the screen switch, the field walk, the menu's sprites; memory ct-frame-alignment-p2).
At P+2 the climb is s = 4 on the A frame, the descent s = 32 on the B/commit frame, and the commit frame shows the
committed sheet -- the three regions 0 frames differ, with no snapshot.

**Open:** the max-HP word's preview (`$0D57`, an accessory's +25/50% HP) is computed by the ROM but its row is not drawn
by the port; the portraits and plaques exist only for Crono and Marle (Lucca's is missing in the member select --
tools/menu_audit_cmp.py tour). The marble's colour conversion is fixed (the chrome palette re-expanded (v<<3)|(v>>2)),
and the member boxes' dim is C2/9E76's palette | 4 (not a fitted colour formula).

## 6. PORTED (2026-09-24): the gear panel's ENTRY and EXIT slides are offset-per-tile, not a moving picture

Graded by `tools/menu_audit_cmp.py` (tour = Crono, `equip1` = Lucca, `equip2` = Frog; every frame, logical oracle, P+2):
the entry and the exit are 0 px on all three. The port's old model (a "rigid rising stack" whose offsets were fitted to
captures, `CtMenuRenderEquipSliding`) is gone.

**The mechanism.** BG mode 2 (channel 5, from line 16) gives screen column c >= 1 the V scroll of BOTH BG1 and BG2 from
entry c-1 of a BG3 map row -- the row the line's BG3 V scroll points at (LakeSnes `ppu_handleOPT`: H offsets from row
`BG3VOFS/8`, V offsets from the next; bits 13/14 = BG1/BG2; value & 0x3FF replaces the scroll). `$5CC2` / `$5D42` are
entries 1-12 (screen columns 2-13, the left column) of BG3 rows 59 and 61 (`$4E00` + row*0x40); FC45 uploads rows 58-61.
Channel 6 writes BG3 V from a table at `$9300`; `$9694/$9696/$9698` are the source, destination and length of the NMI
thunk `$9690` (`MVN $7E,$7E` + `RTL`, C2/8626-864B) that copies it there in any vblank after a frame that set `$0D13`
bit 7 (C2/8628 `TRB`). C2/AA19 writes an entry, splitting a count >= 0x80 into (n+1)&7F lines then 0x7F.

**Entry, C2/A71E** (called from C2/9C55 in the A frame):
* setup (the A frame, no yield before it): `$FF:BE26` through C2/ED31 -- BG1 rows 32-63 cleared, string 7 "Weapon" at
  (5,42); BG2 rows 22-25 backed up to 54-57, window 8B/23 at (2,40) 12x4, a 10x2 copy from (15,11) to (3,41), window
  8B/23 at (2,44) 12x14, window 8B/23 at (2,32) 28x4; FBEA + FC06 upload BG1/BG2 rows 32-63. `$5CC2` = 0x606F, `$0DAB` = 0,
  `$22/$24/$26` = 0x90/0x30/0, `$0D25` = (3 - `$0412`) * 8, 8-bit `DEC $0D9A` (the scrollbar only). The picture is still
  the member select, hand included.
* 24 steps, one a frame (the yield first): `INC $0D9B` (the hand's one-shot suppress, C2/E956), FC45, C2/A7B7 --
  `$5CC2` += 6, `$5D42` += `$0DAB`; `$26` += 6 while `$24` (then `$22`) gives 6 back; the table = `$22` lines 0x1E0
  (row 61), `$24` lines 0x1F0, `$26` lines 0x1D0 (row 59), 16 lines 0x1F0 -- from line 16. After step k the new column
  fills lines 208-6k .. 207 and shows map line 320 (row 40) at its top: it rises 6 px a frame over the TIME box and the
  lower panels. After `$0D25` steps `$0DAB` = 6: the party column scrolls up with it (the picked member's box ends at
  the top; the boxes above go under line 16, where mode 1 has no offsets).
* step 24's frame is also C2/A321's build: `$FF:BE6D` (BG1 rows 40-45 -> 8-13; BG2 windows at (2,2) and (2,12), rows
  40-45 -> 8-13; channel 3 = TM from `$FF:BB0D`; the channel-6 table `$FF:B922` -> `$969A`; the nav graph `$FF:B731`),
  A216 (the member's panel), `$5CC2` = 0x40FF (BG2 only), `STZ $54`, A38B, FBE3/FBFF/FC45, `$0D13` = C5. On screen: the
  left column built, the right still the member select's (all four gear rows bright), no hand.
* C2/9C5B yields; the frame after runs C2/9C5E-9C67: **the second blip** (C2/EAC2 -- the port posts it now, 25 frames
  after the first), C2/8953 + C2/A569 (the list) + FC1B. The hand is on the gear row from this frame; the rows dim on
  the next, when substate 2's C2/9ECD first runs (C2/A3E2).
* MEASURED: `$5CC2` 606F on the A frame, 6075 .. 60FF over the next 24, 40FF with the build; BG3 row 59 = `0000 60A5 x12
  0000..` and row 61 = `0000 61FF x12 0000..` at step 9; the table 11+127 lines 0x1E0, 54 lines 0x1D0, 16 lines 0x1F0.

**Exit, C2/A85C** (B or LEFT on the gear select, C2/9C98): yields at once (the picture stands); the next frame builds in
the buffers only (`$FF:BE5B`: BG2's member box at (2,2) block-copied over rows 8-25 -- C2/EF05 copies row by row, top
down, so it becomes FOUR copies of it; BG1 rows 9-10 cleared; C2/A1B2 writes the three panels; FBEA); the frame after
is the setup, and it shows: BG3 rows 61-62 cleared, C2/9E76 (the boxes toned for `$0412`; its own record FBFF uploads BG2
rows 2-27), the table C2:AA06 (16 FFFF, 48 x 0x1E0, 144 x 0x1D0, 16 x 0x1F0), `$5CC2` = 0x60FF, `$5D42` = 0x6000 |
(48 * `$0412` - 1), FC45, FBE3, and a **16-bit** `DEC $0D9A` (REP #30 is still on) that also sets `$0D9B`: the hand is
gone on this frame already. Then 24 steps (C2/A970): `$5CC2` -= 6, `$5D42` -= `$0DAB` (6 for 8 * `$0412` steps, none for
the lead), the table 16 FFFF, `$22` x 0x1E0, 0xC0 - `$22` x 0x1D0, 16 x 0x1F0 with `$22` = 0x30 + 6 per step: the gear
column sinks 6 px a frame, cut at line 207, while the party column opens above it (the panels, and the member select's
TIME/gold which BG1 rows 20-25 never lost). The band's label is C2/A38B's copy of BG1 rows 9-10 into rows 41-42
(`MVN` 0x80 bytes, every gear-select frame) -- the current slot's, not "Weapon". The 24th step's frame is also C2/A2CD's
(the member select rebuilt, `$54` := `$0412`, still no hand); C2/9C39 reads input from the frame after.

**Input order, measured in the ROM.** C2/9C39 (the member select) is one branch a frame: RIGHT (`$5A` bit 0) or A
confirms, else B goes back, else C2/9D17 moves the cursor. C2/9C70 (the gear select): LEFT (`$5A` bit 1) goes back, else
A opens the list, else B goes back, else C2/9ECD. The port had RIGHT/LEFT inert there and moved the cursor in the same
frame as a confirm.

**The portraits and plaques** (C2/EC38, C2/A273) are the ROM's now: the menu's BG1 characters hold the seven portraits
from tile 0x201 (FF/0000, 0x480 bytes a character) and the four plaques from tile 0x100 (FF/D5E4, 0x180 each) --
MEASURED byte for byte against the oracle's VRAM. Portrait = tile 0x200 + FF:CE70[id] (6x6, palette 3 = the colours
at FF:CE78[id]); plaque = C2:A2C6[id] -> the tile word at FF:CE88+x (6x2, palette 6 = the colours at FF:CE92+x).
Robo, Ayla and id 7 get word 0000 (tiles 0-11, blank in VRAM): no plaque.

**Still open here:** C2/9ECD's first frame compares `$54` with the saved `$0FC5` and blips if they differ (stale from an
earlier visit?); the dirty-key yield (C2/9EFD) makes the list redraw one frame after a slot change and skips that
frame's input -- neither measured yet.

## 7. PORTED (2026-09-24): the list's LAYER, the label's cut, the list hand's record, the commit frame

Graded by `menu-equip-list-commit-vs-oracle` (tools/menu_audit_cmp.py's equiplist drive: an Iron Blade poked into the
bag, the whole change every frame against the logical oracle, 0 of 361; four mutations, all killed). The earlier drives
had nothing to put on, so the list was always empty and none of this could show.

* **The list is BG3, and BG3 is on only through HDMA channel 3.** The table at `$7E:9180` (`48 13 | 7F 17 | 01 17 | 01
  13 | 00`) gives TM 13 for 72 lines, then TM 17 (BG3 on) for lines 73-200. C2/9ECD, the gear select's handler, does
  `LDA #4D / STA $0D13` every frame (C2/9EE6: HDMAEN = ch0, ch2, ch3, ch6); C2/A321's build leaves C5 (no ch3), the
  climb and the descent E5 (no ch3), the settled list DF/5F. MEASURED: on C2/9C5E's frame C2/A569 writes the rows,
  C2/9C67 queues FC1B and the NMI uploads them (--cputrace CPUTRACE_BEAM: A569 at line 23, the drain at line 231), yet
  the next scan has TM 13 on those lines -- the rows show one frame later, with the dims, when C2/9ECD first runs
  ($0D13 45 -> 4D). The port drew them on C2/9C5E's frame.
* **The slot label is cut line by line at 64** (the band's lines above 64 show the member's box): at s = 20 its rows
  60..67 leave 64-67 on screen. The port clipped whole text rows and lost it a frame early (climb) / late (descent);
  it is now drawn into the slide text layer and blitted with the line clip, as the exit already was.
* **The list hand sits at its nav record.** C2/A321's `$FF:BE6D` loads the graph `$FF:B731`: a length byte (0x48,
  twelve records) then `[x, y, up, down, left, right]`; cells 4-11 = (8, 75 + 12 row), one line above the row text's
  origin (the port had placed it from the text, 1 px low). Cells 0-3 are the gear rows at (120, 79 + 12 slot).
* **C2/A64D's frame keeps the rows C2/A569 drew**: a commit changes the bag at once, but the list's BG3 rows are
  C2/A45D's after the descent; the port keeps the pre-commit candidates for that one frame.

(The max-HP preview: section 8. The Save screen's per-slot window colours: menu2_save_rest.md section 9.)

## 8. PORTED (2026-09-25): the member panel's HP (string 06), the preview's max HP (string 09), the bar's typed line

Graded by tools/menu_audit_cmp.py against the logical oracle: `hpacc` (a Gold Earring poked onto Crono, HP 5),
`hpprev` (a Gold Earring in the bag, previewed on his accessory row) -- 0 frames each (checks
`menu-panel-effective-max-hp-and-critical-hp-vs-oracle`, `menu-equip-preview-max-hp-and-typed-description-vs-oracle`).

* **The panel's max HP is the EFFECTIVE one.** FF:C457 string 06 (the member panel; list FF:BE0E through C2/A216) is
  `HP <PAL $0D5D> <NUM $9A93 f22> <PAL $0051> / <NUM $9ACF f22>`: the max is the copy's +0x3F (C2/916E's result, the
  accessory's +25/+50% included), not +0x05. The port read +0x05 -- invisible until an HP accessory is worn (96 vs 144).
* **The critical HP colour.** C2/A1EF, run by every panel builder (C2/A216, the Item overlay's C2/B002, the Tech
  screen's C2/BA32 and C2/BEEF): `$0D5D` = 4 when `$0D38[char]` (C2/813C: +0x3F / 10) >= the current HP -- BG1 palette
  1, whose text colours are CGRAM 21-23 (57,0,0) / (148,33,33) / (255,33,33); a dimmed panel's palette 5. MEASURED
  (hpacc): the ribbon's panel reads 5/144 with the 5 in that red.
* **The preview's max HP.** String 09 (FF:C5FD) draws, besides the stats, `goto 0114 / PAL $0D57 / NUM $9B25 f22` -- the
  recomputed +0x3F in the panel's max cell, coloured by C2/A61F (`LDX $9B25 / CPX $9ACF`, 16-bit: 00 same, 08 higher,
  1C lower). MEASURED (hpprev): $0D57 = 08, $9B25 = 0090 against $9ACF = 0060; the panel shows 25/144 in the raise
  colour.
* **The bar's line is typed.** C2/A5DF calls C2/F2F3 on each preview, the same stager as the Item screen's: the line of
  `$0D77` (restarted only when the id changes) typed one character a frame by C2/FA49 in the VWF. The port printed the
  whole line at once in the 8x8 font (MEASURED: "Max HP up by 50%" growing from f208, lines 188-199).
* (The capped stars of code 0C in strings 04 / 05 / 09: menu2_item_rest.md section 7.)

## 9. PORTED (2026-09-25): L/R switch the member in the gear panel (C2/A00B), over the ROM's own roster list

`C2/9ECD` runs `C2/A00B` every frame of the gear panel: `$0D1D & 3` -- the page bits `C2/E984` decodes from `$F4` (the
latch's edge + autorepeat byte) AND the configured page buttons (`C2/E9B5`: `$2999` -> bit1, R; `C2/E9C1`: `$299A` ->
bit0, L) -- steps `X = $71` (bit1 on, wrapping at `$85` to 0; bit0 back, wrapping to `$85 - 1`), stores `$71` and
`$0412`, and on a change: `$0413` / `$0412` = the 3-wide window (`X - 2` / 2 once `X >= 2`), the blip, `C2/A21F` (the
panel for the member: `$71 = $0412 + $0413`, `C2/8820`, `C2/9137`, the strings, the portrait `C2/EC38`, the plate
`C2/A273`) and ONE yield (`C2/821E`): `C2/9ED6` onwards -- the saved gear cursor, the dirty key `$0077 + $71`, whose
change yields again (`C2/9EFD`) before `C2/A45D` rebuilds the list -- runs the next frame, reading no input.

The list `$71` indexes is the open's: `C2/8115` fills `$0D5F..$0D67` with FF, `C2/816A` packs the present `$2980[0..8]`
charIds into `$0D5F` and their slots into `$0D68`, counting `$73` (slots 0-2) and `$75` (all); `FF/F813` sets `$85 = $73`,
or `$75` once `$7F:0000 >= 0x49` with `$7F:00BA` bit0 clear. The port builds them in the init chain's C+15 frame beside
`C2/8133` (after the `$0C00-1DFF` fill, as the ROM orders it). MEASURED (party3_field, `$7F:0000` = 15): `$73 3 $75 4 $85
3`, `$0D5F = 00 02 04 01 FF..`; R x3 walks Crono -> Lucca -> Frog -> Crono, L -> Frog; `tools/menu_audit_cmp.py equiplr`
0 of 447 frames (regress `menu-equip-lr-member-switch-vs-oracle`).

## 10. PORTED (2026-09-25): the list's scrollbar and its L/R page -- one machinery for every list

**The scrollbar is one task on WRAM bytes.** `C2/8BB8` (task slot 4, spawned by `C2/8B93`, which also clears `$0D9A`)
draws two OBJ sprites -- tile 22 attr 30 (up) and B0 (V-flipped, down) -- at X = `$0D92`, the up at
`$0D93 + hi($0D95 x $0D98) + lo($0D95 x $0D99)` (8-bit adds, the carry kept into the down's `+8`), shown only in the
blink's on phase (`$0D97` bit0), with `$0D9A` clear and `$0D94 != 0`; no up at `$0D95 = 0`, no down at `$0D95 >= $0D94`.
Where the two overlap OAM 30 (the up) is in front -- MEASURED: the shared rows carry the up arrow's outline. Every list
seeds the bytes through `C2/EA81` (`$0DD8` page, `$0DDC` last cell, `$57 / $0D94 / $0DDA` = max(count - page, 0), and
`$0D98` = the caller's `$4204` over (count - page) as an 8-bit divide -- 0 gives FFFF):
* the Item list, `C2/ADD0`: E8 / 50, 0x6800 over 242 - 10 -> `$0D94` E8, `$0D98` 0072, `$0D95` = `$0416` (and
  `C2/ACA7` on a scroll);
* the Equip gear panel `C2/A45D` (68 / 62) and the list `C2/A41A` (68 / 42), both through `C2/A463`: `$0FC8` := 0,
  first cell 4, 0x1500 over count - 8, then `$0D94` x 4 and `$0D95` = 0 (C2/8BA6: quarter rows);
* the Tech member column `C2/BA4F` (12 / 78, 0x09AA over `$85` - 3, x 12) and list `C2/BB50` (E8 / 32, 0x1500, x 4)
  -- not wired yet: their ranges are 0 on every party the repo holds.
MEASURED on the oracle: after an Equip visit with no candidates `$0D92..9A` = 68 62 00 00 FF 01 15 00 FF (the count-0
window: 0x1500 / F8 = 15; `$0D9A` FF); on the Item screen E8 50 E8 00 05 00 72 00 00. The Equip screen hides it with
`$0D9A` through its slides: `C2/A71E` and `C2/A321` DEC it on the way in, `C2/A518` stores E5 on every rise / fall step
(so the A that opens the list already hides the gear panel's arrow), `C2/A697` DECs it, `C2/A85C`'s setup frame DECs it.
The port had the Item screen's arrows only, from constants; they are now `CtMenuRenderScrollbar` on the seeded bytes
(the Item picture byte-identical: menu-item-screen's hash unchanged).

**The page.** `C2/93BF` (Equip list `C2/9F0D`, Item list `C2/AC42`, Tech list `C2/B6E8`, the shop): on `$0D1D`'s page
bits, from `$0DD9` = the scroll -- R: `$0DD9 - $0DD8` (8-bit), kept if below `$0DDA`, else 0 with the cursor on `$0DDB`;
L: `$0DD9 + $0DD8`, kept if no carry and below `$0DDA`, else `$0DDA` with the cursor on `$0DDC` (so landing exactly on
the last page moves the cursor too). A moved scroll stores `$0DD9` / `$0D95` and `$81 = FF`; the Equip list then makes
`$0D95` quarter rows, redraws, blips (`C2/9F26`) and returns -- and `C2/9CBD` blips again for `$81 != $54` (the
throttle takes the second). A clamp alone only moves the cursor (one blip). `tools/menu_audit_cmp.py equippage` (twelve
weapons): f20-f509 0 of 490 frames (regress `menu-equip-list-scrollbar-and-page-vs-oracle`).

**The one-row slide -- PORTED, section 11.** A Down / Up past the window's edge (`C2/9F4A`) runs `C2/EB9B` (copy the HDMA V-scroll
table -- 8 bands of 12 lines at V + 4k, the 16-px buffer rows shown at 12 px -- with one band more above and below)
and `C2/9F90`'s loop: `$0DAB += $0D22` (+-3), `$0D95` +-1, `C2/EB1F` rebuilds the live table (`$969A`) with every V
+ `$0DAB` and yields -- three frames at 3 / 6 / 9 px -- then the buffer shifts a row (`C2/9FDE` MVN / MVP), `$0D95` =
`$0FC8` x 4 and the table goes back to `$9710`. The Item list (`C2/AC8A`, 11 entries, the hand following by `$0D9E`),
the Tech list (`C2/B753`) and the shop lists (`C2/D84F`, `C2/D932`) share it. (Section 11 ports it for the Equip list.)

## 11. PORTED (2026-09-25): the Equip list's one-row slide (C2/EB9B, C2/9F90's loop, C2/EB1F)

MEASURED with `ct.exe --hdma` (HDMA_TRACE_BG=1) on equippage's Down past the bottom edge: HDMA channel 6 writes BG3's V
scroll (`$2112`, table `$9300`): 72 lines at 1F0, then the list's eight bands of 12 lines at V = 1EB + 4k -- band k shows
lines 0-11 of the k-th 16-px slot of the BG3 buffer (the rows at `$4FC6 + 0x80 k`) -- then 16 at 0FF, 16 at 157. On the
three frames after the press the bands read 9 / 12 x 7 / 3 lines at V + 3, then 6 / .. / 6 at V + 6, then 3 / .. / 9 at
V + 9, and on the fourth the static table again. In row space the list's lines 72-167 show `u = y - 72 + o`: slot
`u / 12`, line `u % 12` -- the eight rows and slot 8 (the incoming row, drawn at `$53C6` by C2/9F81) moved o px as ONE
strip. For an Up the band above (C2/EB9B's extra entry, V = V0 + 16n - 4) maps to the same slot 8.

The ROM, frame by frame. Press frame N: C2/9F4A (a row exists past the edge) -> C2/EB9B (the table copied to `$9793`
with one band more below at V + 4 and one above; `$0D22` = +3 Down / -3 Up, `$0DA9` 3 / 0, `$0D24` 4), `INC/DEC $0FC8`,
the row drawn into slot 8, the blip; then C2/9F90's loop: `$0DAB += $0D22`, `$0D95` +-1, `DEC $0D24`, C2/EB1F (the live
table `$969A` = the copy with every V + `$0DAB`, the first band `$0DAB` lines shorter; `$0D13 |= 80` so the NMI copies
it to `$9303`) and a yield -- o = 3 on N, 6 on N+1, 9 on N+2. On N+3 the fourth step exits: `$0D95 = $0FC8 x 4`,
`$9694 = $9710` (the base copy), C2/9FDE (Down: MVN `$5000 -> $4F80`, 0x400 bytes; Up: MVP `$4F80-$52FF -> $5000-
$537F` then MVN slot 8 -> slot 0), `$0D22` := 0 -- and only then does C2/9CB2 run C2/A5C3: the right panel's preview and
the description stay the old candidate's through the slide and restart on N+3. No input is read from N+1 to N+3.
`$0D18` negative skips the loop (29F90 `LDA $0D18 / BMI`); C2/EA53 counts it down while START is held in a scrollable
list (and C2/9DAF, the whole-roster member scroll, DECs it too).

The port: the rows drawn apart (`mSlideTxt`) at their static places plus the incoming one, blitted with the offset over
lines 72-167 (`mEqListSlideRows`); the hand on its nav record; the preview on the id C2/A5C3 last saw
(`menuEqPrevId`); `$0D95` / `$0DAB` / `$0D24` / `$0D22` as the loop writes them. `tools/menu_audit_cmp.py equipslide`
(three rows down, three back up) 0 of 561 frames and `equippage` 0 of 681 (regress
`menu-equip-list-one-row-slide-vs-oracle`, `menu-equip-list-scrollbar-and-page-vs-oracle`).

Still to port on the same machinery: the Item list (C2/AC6E: 11 entries, the hand following by `$0D9E`, `$0D95` =
`$0416` before the loop), the Tech list (C2/B753) and the shop lists (C2/D84F, C2/D932), and their L/R pages (C2/AC42,
C2/B6E8; the shop's is ported).

## 12. PORTED (2026-09-25): the member select over the WHOLE roster -- the window, its scroll, its scrollbar

Once the story byte `$7F:0000` reaches 0x49 (with `$7F:00BA` bit0 clear), FF/F813 makes the char-select list the whole
roster (`$85 = $75`): the reserve members join the Equip screen. MEASURED with that byte poked on party3_field ($2980 =
00 02 04 01: Marle in reserve): `$85` 4, and the member column is a three-row WINDOW into C2/816A's list.

* **The build (`C2/A051`)**: `$0FC4 = max($85 - 3, 0)` (the scroll range), `$0413` clamped to it, `$54 = $0412` held
  below `$73`, `$71 = $54 + $0413`; `C2/A170` then seeds the scrollbar only when `$0FC4 != 0`: `$0D94 = 12 x $0FC4`,
  `$0D98` = 7400 over it (MEASURED 09AA), `$0D92` = 1268, `$0D95 = 12 x $0413`, and spawns the task (C2/A2CD re-runs it
  on the way back from the gear panel). The panels (C2/A1B2) are `$0D5F[$0413 + row]`, `$0D68` their `$2980` slots.
* **The cursor (`C2/9D17`)** over FF/B71E (rows 0-1-2 at the 48-px pitch, an edge Up from 0 and Down from 2): with no
  range it wraps over `$85` (a row past it -> 0, the Up edge -> `$85 - 1`, the Down edge -> 0); with one, the edges scroll
  `$0413` (`C2/9D5B`, no wrap) through `C2/9DAF`. Then `$0412 = $54`, `$71`, a blip for a new `$71`, C2/A22F.
* **`C2/9DAF`**: the incoming member's panel into the off-screen slot (C2/A216), its box dim (`C2/9EB0`, Y = 100C), the
  column block-copied (C2/EF05, BG1 and BG2) so that it and the new box form one strip, then twelve steps of `$0D22` =
  +-4 on the column's offset-per-tile V (`$5D42`, screen columns 2-13) with `$0D95` +-1, a yield each -- the column slides
  4 px a frame, cut to lines 16-159 -- and on the twelfth the settle, `DEC $0D18` (every later list slide is skipped
  after one), and C2/9D3D: the dim (C2/9E76, `$0412` unchanged), `$71`, the blip, the detail. MEASURED: Frog bright and
  Marle coming in dim through f171-f181, Marle bright with her detail at f182.

The port: `CtMenuSetPanelWindow` (the window the panel column draws, `$0413` on the Equip screen and its ribbon hover;
the Tech screen keeps the three active slots -- its own window `$0419` / `$041A`, C2/BA4F, is not ported),
`CtMenuPanelData` over all nine `$2980` slots, the member select's rows in `$0412` / `$0413` / `$71` (menuStatSlot the
`$2980` slot of `$71`), C2/9E76's bright box by the window's row (`mCharSelSlot`), `CtMenuRenderEquipMemberSlide`.
`tools/menu_audit_cmp.py equiproster` 0 of 477 frames and `equiproster2` (down a member and back up) 0 of 377 (regress
`menu-equip-whole-roster-member-select-vs-oracle`, `menu-equip-member-scroll-both-ways-vs-oracle`).
