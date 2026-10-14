# menu2_form_screen — SCREEN 5 = FORM / PARTY-EXCHANGE (C2/C621), derived end to end

Scope: the gap named "the Form screen is a documented APPROXIMATION". Everything below is either a
disassembly of a named ROM address or an oracle command that was actually run (output pasted in §4).
Probe binary: `scratchpad/menu/ct_probe.exe` (never `build/ct.exe`). ROM: `Chrono_Trigger_USA_SNES.sfc`.

> ⚠ **The blocker recorded in `menu_phase2.md` is DISPROVEN, twice over.**
> (1) `saves/com_jogo_salvo.state` **passes** the entry gate and the screen opens.
> (2) The screen is **not** a task-spawned slide-in: it is fully settled **2 frames** after the A press
>     (`scratchpad/menu/sl2.png` … `sl24.png` are pixel-identical). `menu_phase2.md`'s "never settles
>     headlessly" was the *refusal* — `$0067` bouncing 5→0 one frame later — misread as an animation.
>     That misreading is already named correctly in `src/menu.c` (the CtMenuFormRoster comment); this
>     dossier retires the "slide-in" wording from `src/menu.h` and `CtMenuRenderForm` too.

---

## 1. VERDICT

The Form screen is **the main menu screen with the ribbon box erased, plus one hand-cursor sprite** —
measured, not inferred: the entered-Form BG1 tilemap is **byte-identical (0 differing tiles out of 896)**
to the ribbon's opt-4 hover, and the BG2 tilemap differs **only** in rows 12–17 × cols 8–23, i.e. exactly
where the ribbon icon box was. So there is no new layout to build: the panels, the "Exchange" window and
all panel text are already on screen when the player presses A.

The machine behind it: at ribbon-option 4, **C2/CD2B** builds a 9-entry panel array `$0F02..$0F0A`
(`0x80` = empty slot, `0x40|slot` = locked member, `slot` = selectable) plus two counts `$0F0B`
(selectable ACTIVE members) / `$0F0C` (RESERVE members, **only when `$0D36 == 0`**). The screen's own
substate-0 handler **C2/C627** opens iff `$0F0B + $0F0C >= 2`, else writes `$0067 = 0`, `$0068 = 1`
and buzzes. In the X-menu `$0D36` is **always `0xFF`** (`C2/989E LDA #FF / STA $0D36`, run at menu-open),
so **`$0F0C` is always 0 and the reserve column NEVER appears in the X-menu Form screen** — reserves
belong to a *different* menu program, mode 5 / screen `$07` (C2/C7A6), which the field opens on the **Y**
button gated on `$7F0000 (storyline) >= 0x49` (C0/1985 → C0/19CE `LDA #05 / JSL C2/8000`).

Picker: `$0054` = cursor cell, driven by the generic mover **C2/E9E2** through a 6-byte-per-cell graph at
**`$7E9380`** (`{handX, handY, up, down, left, right}`, ROM source `$FF:B77A`), then re-driven by
**C2/C805** whenever the landed cell's `$0F02` entry has bit6/bit7 (locked/empty). A picks
(`$0F0D → $0F00`, then `$0F0D → $0F01` + **C2/C70C** = swap two bytes of `$2980`), B cancels/exits.
A reserve↔active exchange is *the same instruction*: the indices simply run 0..8 over `$2980..$2988`.

**What the C engine must do**, one line: render screen 5 as the opt-4 ribbon frame minus the ribbon box
plus the ROM's hand at `(56, 36+48*slot)`, drive the cursor over `$0F02` (skipping **locked** members, not
just empty ones), and — the visible bug — **run the party reconcile at menu exit**: if `$2980..$2982`
differs from the shadow `$0194..$0196`, re-run the init function of the seven PC objects named by
`$018D..$0193` (**C0/597D**, order `8D,8E,8F,91,90,92,93`), refresh the shadow, then restore the three
active objects' `$1800/$1880/$0C00` from the menu-entry snapshot `$7F:1D09/1D0F/1D15` indexed by the
**new** `$0197/$0199/$019B` (**C0/1A03**).

---

## 2. ROM TRACE

### 2.1 Dispatch into screen 5

```
C2/8292  per-screen handler-array table (dumped): CA 99 | 29 9C | 34 AA | 53 B3 | 35 BF | 21 C6 | D4 CE | A6 C7 …
         -> screen 5 handler array = C2/C621 ; screen 7 = C2/C7A6
C2/C621  substate table (dumped): 27 C6  5D C6  B9 C6
         $0068 = 0 -> C2/C627 (gate + init)
         $0068 = 1 -> C2/C65D (picker idle)
         $0068 = 2 -> C2/C6B9 (second-pick idle)
C2/808F  mode table (dumped): 0E 00 0C 0D 0F 07 0B   -> request kind 5 enters screen $07
```

### 2.2 The roster builder — C2/CD2B (the data model)

```
2CD2B PHP / REP #30
2CD2E LDA #8080 / STA $0F02          ; $0F02 = $0F03 = 0x80
2CD34 LDX #0F02 / LDY #0F04 / LDA #0006 / MVN $7E,$7E   ; propagate 0x80 over $0F02..$0F0A (9 entries)
2CD40 STZ $0F0B                      ; 16-bit STZ -> clears BOTH $0F0B and $0F0C
2CD43 SEP #30 / STZ $00 / LDX #00 / TXY
2CD4A LDX $00
2CD4C LDA $2980,X
2CD4F BMI ->2CD6A                    ; bit7 = empty slot -> no panel entry, no count
2CD51 INC $0F0B                      ; count it
2CD54 TAX                            ; X = charId
2CD55 LDA $7F01DF                    ; the party-LOCK bitfield
2CD59 AND $FFF9BB,X                  ; mask table $FF:F9BB (dumped: 80 40 20 10 08 04 02 01 00) = 0x80>>charId
2CD5D BEQ ->2CD64                    ; not locked
2CD5F DEC $0F0B                      ;   LOCKED -> un-count
2CD62 LDA #40                        ;   and tag the panel entry with bit6
2CD64 ORA $00 / STA $0F02,Y / INY    ; panel[Y] = slot | (0x40 if locked)
2CD6A INC $00 / LDA $00 / CMP #03 / BCC ->2CD4A
2CD72 LDA $0D36
2CD75 BNE ->2CD99                    ; ★ $0D36 != 0 -> SKIP the whole reserve scan ($0F0C stays 0)
2CD77 BEQ ->2CD7B                    ; (always taken; 2CD79 LDA #40 is unreachable dead code)
2CD7B STA $00 / LDX #03 / LDY #00
2CD81 LDA $2980,X / BMI ->2CD8D      ; reserve slots $2983..$2988
2CD86 TXA / ORA $00 / STA $0F05,Y / INY
2CD8D INX / CPX #09 / BCC ->2CD81
2CD92 BIT $00 / BVS ->2CD99
2CD96 STY $0F0C                      ; reserve count
2CD99 PLP / RTS
```

Callers (byte scan `20 2B CD`): **C2/CA2A** (the ribbon's opt-4 hover preview builder) and **C2/C77C**
(the swap tail). Never C2/C627 itself — the counts are already standing when the gate reads them.

`$0D36` writers (byte scan): only **C2/989E** (`LDA #FF / STA $0D36`, the menu-open computation) and
**C2/C7AE** (`LDA $71 / STA $0D36`, screen 7's init). Zero comes from a DMA clear, not an instruction.
⇒ **inside the X-menu `$0D36` is `0xFF`, permanently.**

### 2.3 The gate + init — C2/C627 / C2/C649

```
2C627 SEP #30
2C629 LDX $7F                        ; the stale cursor scratch (measured 0 at Form entry, §4.6)
2C62B LDA $0F0B / CLC / ADC $0F0C / CMP #02
2C634 BCS ->2C63D                    ; >= 2 -> OPEN
2C636 STZ $67                        ; else REFUSE: back to the ribbon
2C638 INC $68
2C63A JMP C2/EACC                    ;   the BUZZ (cmd $18 id $01)
2C63D JSR C2/C649                    ; picker init
2C640 REP #10
2C642 JSR C2/EAC2                    ;   the screen-open BLIP (cmd $18 id $00)
2C645 JMP C2/CAF3                    ; build the screen

2C648 INX                            ; <- C2/C649 loop tail
2C649 LDA $0F02,X / AND #C0 / BNE ->2C648   ; skip empty (0x80) AND locked (0x40) entries
2C650 STX $54                        ; cursor cell
2C652 LDA #FF / STA $0F00 / STA $0F01 ; both picks = none
2C65A INC $68                        ; -> substate 1
2C65C RTS
```

### 2.4 Picker idle — C2/C65D, and the second-pick idle — C2/C6B9

```
2C65D JSR C2/E984                    ; input decode (moves $54 first, see 2.5) -> $0D1D bit7=A bit6=B
2C660 BIT $0D1D
2C663 BMI ->2C674                    ;   A -> first pick
2C665 BVS ->2C6B3                    ;   B -> exit to ribbon
2C667 JSR C2/C805                    ;   else: validate/re-drive the cursor + redraw
2C66A LDA $81 / CMP $54 / BEQ ->2C673
2C670 JSR C2/EAC2                    ;   cursor MOVED -> blip
2C673 RTS

2C6B9 JSR C2/E984
2C6BC BIT $0D1D
2C6BF BMI ->2C6D0                    ;   A -> second pick
2C6C1 BVS ->2C6DE                    ;   B -> cancel the pending pick
2C6C3 JSR C2/C805
2C6C6 LDA $81 / CMP $54 / BEQ ->2C6CF
2C6CC JSR C2/EAC2                    ;   cursor MOVED -> blip
2C6CF RTS
```

### 2.5 The cursor mover — C2/E9E2 (generic) + the graph

```
2E9E2 PHP / SEP #30
2E9E5 LDA $54 / STA $81              ; $81 = the pre-move cursor (the "did it move?" blip gate)
2E9E9 LDA #80 / STA $0D1E
2E9EE LDX #00
2E9F0 LDA $F5 / STA $5A              ; $5A = the auto-repeat direction latch (SNES pad-high bits)
2E9F4 LSR A / BCS ->2E9FE / INX / CPX #04 / BCC ->2E9F4   ; X = index of the first set bit 0..3
2E9FC BRA ->2EA1A                    ;   none -> no move
2E9FE STX $0D1E
2EA01 LDA $0D1F / BNE ->2EA1A
2EA06 LDA $54 / JSR C2/EA27          ; X = cell*6   (2EA27: AND #00FF / ASL / PHA / ASL / ADC $01,S / TAX)
2EA0B TXY
2EA0C LDA $54 / STA $55
2EA10 LDA $0D1E / ASL / TAX / REP #10
2EA17 JSR ($EA1F,X)                  ; table: EA45 EA40 EA3B EA36
2EA36 LDA $9382,Y -> 2EA45           ; bit3 = UP    -> graph +2
2EA3B LDA $9383,Y -> 2EA45           ; bit2 = DOWN  -> graph +3
2EA40 LDA $9384,Y -> 2EA45           ; bit1 = LEFT  -> graph +4
2EA45 LDA $9385,Y                    ; bit0 = RIGHT -> graph +5
2EA48 STA $54
2EA4A BPL ->2EA52 / LDA #80 / TSB $5A / TRB $54   ; graph entry bit7 = "ran off the list" -> $5A bit7 = SCROLL
2EA52 RTS
```

Graph record (6 bytes/cell) at **`$7E9380`**, copied verbatim from ROM **`$FF:B77A`** by the screen's
setup list (see 2.8). ROM bytes (dumped, first byte `2A` = 42 = the length):

```
cell0 (active slot0)  08 23 | 02 01 | 03 03      ; handX=0x08 handY=0x23  up=2 dn=1 lf=3 rt=3
cell1 (active slot1)  08 53 | 00 02 | 04 04
cell2 (active slot2)  08 83 | 01 00 | 05 05
cell3 (reserve row0)  E8 23 | 86 04 | 00 00      ; up = 0x86 -> cell 6 + the 0x80 SCROLL bit
cell4 (reserve row1)  E8 53 | 03 05 | 01 01
cell5 (reserve row2)  E8 83 | 04 06 | 02 02
cell6 (reserve row3)  E8 B3 | 05 83 | 02 02      ; dn = 0x83 -> cell 3 + the SCROLL bit
```

`+0/+1` are the hand sprite's OAM **X and Y** (verified: OAM obj28 x=56 y=83 for slot 1, y=131 for slot 2
— §4.4). The active column's `handX` is **overwritten to `0x38` (56)** by `C2/CB18-CB22` and
`C2/CC63-CC6D` when `$0D36 != 0` (`STA $9380 / STA $9386 / STA $938C`), i.e. in the X-menu; the ROM table's
`0x08` is the two-column (screen 7) position.

### 2.6 The re-drive / validity loop and the reserve scroll — C2/C805, C2/C85B, C2/C9AE

```
2C805 PHP / SEP #30
2C808 STZ $0D9E
2C80B LDA $0F0C / CMP #05 / BCC ->2C819   ; scroll only exists with >= 5 reserves
2C812 LDA $5A / BPL ->2C819               ;   and only when the move set the 0x80 SCROLL bit
2C816 JSR C2/C85B
2C819 LDA $0F0B / ORA $0F0C / BNE ->2C827
2C821 STZ $54 / LDA #40 / BRA ->2C83D     ; nothing selectable at all -> $0F0D = 0x40 (invalid)
2C827 LDA $54 / CMP #03 / BCC ->2C830
2C82D CLC / ADC $80                       ; reserve cells: roster index = cell + scroll $80
2C830 TAX / LDA $0F02,X
2C834 BIT #C0 / BEQ ->2C83D               ; valid entry?
2C838 JSR C2/C9AE / BRA ->2C819           ;   no -> move again and re-test
2C83D STA $0F0D                           ; $0F0D = the selected roster entry (slot | 0x40 if locked)
2C840 JSR C2/C973                         ; repaint the per-character sprite POSES
2C843 LDA #04 / TRB $0D13
2C848 STZ $56
2C84A LDA $54 / STA $7F                   ; $7F = the settled cursor
2C84E CMP #03 / BCC ->2C854 / INC $56     ; $56 = 1 when the cursor is on the reserve column
2C854 PLP / LDX #FC37 / JMP C2/8385       ; redraw command list $FF:FC37

2C85B LDA $5A / AND #08 / BEQ ->2C888     ; UP at the top of the reserve list
2C861 LDA #03 / STA $54
2C865 LDA $80 / BEQ ->2C887 / DEC $80     ;   scroll up
2C86B LDA #D0 / STA $0D9E ; … JSR C2/C8BA ;   (smooth-scroll animation setup)
2C888 LDA $0F0C / SEC / SBC #03 / STA $00 ; DOWN at the bottom
2C890 LDA #06 / STA $54
2C894 LDA $80 / INC A / CMP $00 / BCS ->rts
2C89B STA $80 …                           ;   scroll down; max $80 = $0F0C - 4
```

⇒ the reserve window shows **4 rows** (cells 3..6) and scrolls only when `$0F0C >= 5`.

```
2C9AE LDA $5A / AND #03 / BEQ ->2C9CD     ; LEFT|RIGHT arm
2C9B4 LDA $54 / CMP #03 / BCS ->2C9C3
2C9BA LDX $0F0B / BNE ->2C9CD             ;   on the active side: any free actives -> use the graph
2C9BF ADC #03 / BRA ->2C9CA               ;   else hop to the reserve column ($54 += 3)
2C9C3 LDX $0F0C / BNE ->2C9CD             ;   on the reserve side: any reserves -> use the graph
2C9C8 SBC #03                             ;   else hop back ($54 -= 3)
2C9CA STA $54 / RTS
2C9CD LDA $54 / JSR C2/EA27               ; UP/DOWN (or a valid L/R): walk the graph again
2C9D2 LDA $5A / BIT #08 / BNE ->2C9DD
2C9D8 LDA $9383,X / BRA ->2C9E0           ;   DOWN
2C9DD LDA $9382,X                         ;   UP
2C9E0 AND #0F / STA $54 / RTS
```

**Consequence for the X-menu** (`$0F0C == 0`): a LEFT/RIGHT press sends the cursor to cell `$54+3`, whose
`$0F02` entry is `0x80`; C2/C9AE's L/R arm then sees `$0F0C == 0` and subtracts 3 again — **LEFT/RIGHT are
inert.** Measured in §4.5 (three L/R presses, `$0054` never moves).

### 2.7 The picks and THE SWAP — C2/C674 / C2/C6D0 / C2/C70C

```
2C674 JSR C2/EAC2                    ; blip (FIRST PICK)
2C677 LDA $80 / STA $0DA8            ; remember the scroll at pick time
2C67C LDA $7F / STA $54 / STA $0D9C  ; remember the cursor cell at pick time
2C683 SEC / SBC #03 / BMI ->2C6A8    ; a RESERVE was picked?
2C688 ASL / STA $00 / ASL / ADC $00 / INC A / INC A / STA $5C   ; $5C = (cell-3)*6 + 2  (tile row)
2C692 LDA #13 / STA $5B                                          ; $5B = 0x13            (tile col)
2C696 LDX #2E00 / STX $61            ; BG1 staging base $7E:2E00
2C69B LDX #2002 / STX $5D            ; fill word
2C6A0 LDX #060A / STX $5F            ; 10 cols x 6 rows
2C6A5 JSR C2/EF05                    ;   blank the reserve panel it lifted
2C6A8 LDA $0F0D / STA $0F00          ; ★ first pick = the roster entry
2C6AE INC $68                        ; -> substate 2
2C6B0 JMP C2/8A31

2C6D0 BIT $0F0D / BVS ->2C6DE        ; bit6 = LOCKED entry -> refuse, fall into the cancel path
2C6D5 LDA $0F0D / STA $0F01          ; ★ second pick
2C6DB JSR C2/C70C                    ; ★ THE SWAP
2C6DE JSR C2/EAC2                    ; blip (second pick OR cancel OR B — ONE shared site)
2C6E1 LDA $7F / STA $54
2C6E5 DEC $68                        ; -> back to substate 1
2C6E7 PHP / SEP #20                  ; …falls THROUGH into the teardown…
2C6EA LDA #FF / STA $0F00 / STA $0F01
2C6F2 LDA #40 / JSR C2/8255          ; kill menu task bit6 (C2/8255 = STZ $00B0 + 8*bit per set mask bit)
2C6F7 LDX $51
2C6F9 SEP #20 / STZ $1811,X / REP #21 / TXA / ADC #0040 / TAX / CMP #01C0 / BCC ->2C6F9
                                     ;   clear every character's POSE byte ($1811 + charId*0x40, 7 entries)
2C70A PLP / RTS
```

```
2C70C PHP / SEP #30
2C70F LDX $0F00 / LDA $2980,X / STA $00
2C717 LDY $0F01 / LDA $2980,Y / STA $2980,X
2C720 LDA $00 / STA $2980,Y          ; ★ the ONE intentional RAM write — indices run 0..8, so a
                                     ;   reserve<->active exchange is the SAME two-byte swap
2C725 JSR C2/816A                    ; rebuild the compacted party index (see 2.9)
2C728 LDA $0D9C / CMP #03 / BCC ->2C74A
2C72F CLC / ADC $0DA8 / SEC / SBC $80 / STA $0D9C     ; re-base the pick-time cell against today's scroll
2C739 SEC / SBC #03 / CMP #04 / BCC ->2C74A
2C740 LDA #02 / STA $5B / LDA #20 / STA $5C / BRA ->2C758   ; scrolled out of view -> offscreen row 0x20
2C74A LDA $0D9C / JSR C2/C784 / LDA $00 -> $5B / LDA $01 -> $5C   ; src panel (col,row)
2C758 LDA $7F   / JSR C2/C784 / LDA $00 -> $5D / LDA $01 -> $5E   ; dst panel (col,row)
2C767 REP #30
2C769 LDA #2E00 / STA $61            ; BG1 staging base
2C76E LDA #050A / STA $5F            ; 10 cols x 5 rows = the panel's text block
2C773 JSR C2/F378                    ; exchange the two text blocks in the tilemap
2C776 LDX #FBE3 / JSR C2/8385        ; flush
2C77C JSR C2/CD2B                    ; ★ rebuild $0F02..$0F0C from the NEW $2980
2C77F JSR C2/CDE3                    ; ★ re-place the menu character objects
2C782 PLP / RTS
```

**C2/C784 — the panel (col,row) formula** (raw `[34 ROM bytes at 0x2C784: python tools/dis.py 0x2C784 34]`):

```
C784 LDX #03                 ; default column = 3   (two-column screen: the ACTIVE column)
C786 CMP #03 / BCC ->C78E
C78A SBC #03 / LDX #13       ; slot >= 3 -> reserve index, column = 0x13 (19)
C78E STX $00
C790 AND #07 / ASL / PHA / ASL / ADC $01,S / ADC #02 / STA $01   ; row = (slot & 7)*6 + 2
C79B LDA $0D36 / BEQ ->C7A4
C7A0 LDA #09 / STA $00       ; ★ $0D36 != 0 (the X-menu) -> ONE centred column, col 9
C7A4 PLA / RTS
```

### 2.8 The screen build — C2/CAF3, and the command lists

```
2CAF3 PHP / SEP #20
2CAF6 LDX #BF7B / JSR C2/ED31        ; run the command list at $FF:BF7B
2CAFC JSR C2/984A
2CAFF LDA #01 / STA $0D13
2CB04 LDX #01F0 / STX $0D0E
2CB0A JSR C2/F5A7
2CB0D JSR C2/CC4F                    ; blank/dim the locked+empty panels
2CB10 INC $0D15
2CB13 LDA $0D36 / BEQ ->2CB29
2CB18 LDA #38 / STA $9380 / STA $9386 / STA $938C   ; ★ X-menu: active hand column -> x 56
2CB23 LDX #BF40 / STX $9683
2CB29 REP #30 / LDA #A400 / STA $004204     ; scrollbar: A400 / ((reserveN-4)*6)
2CB34 SEP #21 / LDA $0F0C / SBC #04 / STA $0D94 / DEC A / BMI ->2CB6C
      … $0D94 = (reserveN-4)*6 -> $4206 ; $0D95 = scroll*6 ; $0D92 = 12EC ; $0D98 = RDDIV quotient
2CB6C REP #30 / LDY #9500 / JSR C2/A0E7
2CB74 JSR C2/C805                    ; place the cursor
2CB77 LDX #FBE3 / JSR C2/8385
2CB7D LDX #FC37 / JSR C2/8385
2CB83 LDX #FBFF / JSR C2/8385
2CB89 PLP / RTS
```

**C2/ED31** is a command-list interpreter whose lists live in **bank `$FF`** (`LDA #FF / PHA / PLB`,
`LDA $0000,X`, terminator = any byte with bit7 set). Per opcode it copies `$FF:BD18[op] + 1` argument
bytes to `$7E:005B` (`MVN $7E,$FF`, which also leaves `DB = $7E`) and dispatches through `($ED59,X)` with
`X = opcode` (opcodes are pre-doubled). Handler table dumped: `00→ED77 02→EF05 04→EDAA 06→EF48 08→F332
0A→F364 0C→F337 0E→F364 10→F378 12→EDB0 14→EDB6`. Arg counts (`$FF:BD18`, dumped
`03 00 05 00 05 00 04 00 01 00 01 00 03 00 01 00 05 00 05 00 05 00 08 00`): op00→4, op02→6, op04→6,
op06→5, op08→2, op0A→2, op0C→4, op0E→2, op10→6, op12→6, op14→6, op16→9.

`$FF:BF7B` (dumped `[27 ROM bytes at 0x3FBF7B: python tools/dis.py 0x3FBF7B 27]
0E 7A B7 08 80 96 0E F6 BC FF`) decodes to:

| bytes | meaning |
|---|---|
| `08 00 4E` | `$61 = $7E:4E00` (op08 = `LDX $5B / STX $61`) |
| `00 80 00 00 00` | fill 0x80 bytes at `$7E:4E00` with `0x0000` (op00 = word-fill via MVN propagate) |
| `0C 30 43 00 05` | DMA channel **3** ← `$7E:9180`, params `$0500` (op0C sets `$4330-37`, `$61 = ((lowbyte&0xF0)<<3)+0x9000`) |
| `0E EA BA` | copy the length-prefixed ROM block `$FF:BAEA` → `$7E:9180` (op0E) |
| `0C 20 43 00 2C` | DMA channel **2** ← `$7E:9100`, params `$2C00` |
| `0E 17 BB` | copy `$FF:BB17` → `$7E:9100` |
| `08 80 93` | `$61 = $7E:9380` |
| `0E 7A B7` | ★ copy `$FF:B77A` (0x2A bytes) → `$7E:9380` — **the cursor graph** |
| `08 80 96` | `$61 = $7E:9680` |
| `0E F6 BC` | copy `$FF:BCF6` (0x10 bytes) → `$7E:9680` |
| `FF` | end |

### 2.9 The dim/blank of locked & empty panels — C2/CC4F

```
2CC4F PHP / SEP #20
2CC52 LDX #2E84 / LDY #100E          ; BG1 staging address of panel 0, fill word
2CC58 LDA $0D36 / BEQ ->2CC6E
2CC5D LDX #2E90 / LDY #1010          ;   X-menu variant
2CC63 LDA #38 / STA $9380 / STA $9386 / STA $938C   ; (same hand-column override as C2/CB18)
2CC6E STX $04 / STY $02 / REP #30 / STZ $00
2CC76 LDX $00 / LDA $0F02,X / AND #00C0
2CC7E BEQ ->2CCA0                    ; ★ acts only on LOCKED (0x40) or EMPTY (0x80) panels
2CC80 TXA / JSR C2/CCB3
2CC84 LDX $04 / STX $61 / LDX #0006 / LDA $02 / JSR C2/EC93     ; BG1 plane
2CC90 LDA $04 / CLC / ADC #1000 / STA $61 / LDX #0006 / LDA $02 / JSR C2/EC93   ; BG2 plane (+0x1000)
2CCA0 LDA $04 / CLC / ADC #0180 / STA $04     ; +0x180 = 6 tile rows -> next panel
2CCA8 INC $00 / LDA $00 / CMP #0003 / BCC ->2CC76
2CCB1 PLP / RTS
```

Callers (byte scan `20 4F CC`): **C2/CA3F** (the ribbon's opt-4 hover) and **C2/CB0D** (the Form build) —
**only those two**. That is exactly why the locked panel is drawn in palette 4 on the ribbon at opt 4 and
on the Form screen, and *not* on the other ribbon options (measured, §4.7). The staging bases confirm
`$7E:2E00` = BG1 tilemap staging (`C2/C769 LDA #2E00`) and `$7E:3E00` = BG2 staging (`+0x1000`).

### 2.10 The per-character POSE — C2/C973 / C2/C997

```
2C973 SEP #30 / LDA #08 / STA $00 / LDY #00
2C97B LDX $00 / JSR C2/C997 / DEC $00 / BPL ->2C97B   ; Y = 0 for every roster slot 8..0 (reset)
2C984 LDY #01 / LDX $0F0D / JSR C2/C997               ; Y = 1 for the CURSOR's member
2C98C LDA $0F00 / BPL ->2C992 / RTS
2C992 LDY #0B / LDX $0F00                             ; Y = 0x0B for the FIRST-PICKED member
2C997 PHP / LDA $2980,X / BMI ->2C9AC                 ; (entry)
2C99D REP #30 / AND #00FF / XBA / LSR / LSR / TAX     ; X = charId * 0x40
2C9A6 SEP #20 / TYA / STA $1811,X                     ; $7E:1811 + charId*0x40 = the pose byte
2C9AC PLP / RTS
```

Measured `$1811 + charId*0x40` (§4.4): all 0 except **1** on the cursor's member and **0x0B** on the picked
one (the pick wins, it is written second). The picked member's on-screen sprite visibly changes pose
(`scratchpad/menu/z_idle.png` vs `z_pick.png`).

### 2.11 Screen 7 — the two-column exchange with reserves (a DIFFERENT program)

```
2C7A6 sub-table (dumped): AC C7  E0 C7  B9 C6         ; note it SHARES C2/C6B9 with screen 5
2C7AC LDA $71 / STA $0D36                             ; ★ $0D36 comes from the request parameter
2C7B1 LDA $71 / STA $0D37
2C7B6 JSR C2/CB8B / JSR C2/C9E5                       ; C2/C9E5 dispatches ($CA63,X) on ($0D36 & 2):
                                                      ;   X=0 -> C2/CA67 (list $FF:BF05, TWO columns)
                                                      ;   X=2 -> C2/CAE0 (list $FF:BF48, ONE column)
2C7BC SEP #30 / LDX #00 / JSR C2/C649 / REP #10 / JSR C2/CAF3 / JSR C2/CE86 …
```

Reached only through `JSL C2/8000` with **A = 5** (`C2/808F[5] = $07`). The one caller:

```
C0/1916 LDA $00F6 / BIT #01 / BEQ ->1923 / JSR C0/1985      ; ← a DIFFERENT button than the menu's BIT #40
C0/198E LDA $7F0000 / SEC / SBC #49 / BCS ->1998 / RTS      ; ★ storyline >= 0x49 or nothing happens
C0/19C1 JSR C0/0B4E / JSR C0/011B / LDX #0000 / TDC / XBA / LDA #05 / JSL C2/8000
C0/19D2 …/ JSR C0/01A5 / JSR C0/1A03 / … / JSR C0/B192 / JSR C0/2824 / STZ $0407 / RTS
```

`$00F6` bit0 = **Y** and bit6 = **X**, measured directly (§4.8).

### 2.12 THE PARTY RECONCILE — C0/011B (snapshot) and C0/1A03 (restore)

Menu entry `C0/1958 JSR C0/0B4E / C0/195B JSR C0/011B / … / C0/1960 JSL C2/8000`;
menu exit `C0/1964 … C0/1975 JSR C0/01A5 / C0/1978 JSR C0/1A03 / C0/197B JSR C0/B192 / C0/197E JSR C0/2824`.
**DP = $0100** from `C0/196D REP #20 / LDA #0100 / TCD`, so `$8D` = `$018D` etc.

```
011B  …
012C  LDY $97 / LDA $1801,Y -> $02 / LDA $1881,Y -> $03 / LDA $1600,Y -> $04
013D  LDX #0000
0142  LDA $97,X / BMI ->0160          ; $0197/$0199/$019B = the PC object index of active slot 0/1/2
0146  TAY / REP #20
0149  LDA $1800,Y / STA $7F1D09,X     ; X pos
0150  LDA $1880,Y / STA $7F1D0F,X     ; Y pos
0157  LDA $0C00,Y / STA $7F1D15,X     ; the front/back-FG prio word
0160  INX / INX / CPX #06 / BNE ->0142
0169  LDA $AB -> $7F1D1B ; $AC -> $7F1D1C ; $AD -> $7F1D1D
0179  $7F3728 -> $7F1D1E ; $7F3748 -> $7F1D20 ; $7F3768 -> $7F1D22 ; $7F3781 -> $7F1D24
019D  LDA $1DF9 -> $7F1D26 / RTS
```

```
1A03  LDA $7E2980 / CMP $94 / BNE ->1A1C
1A0B  LDA $7E2981 / CMP $95 / BNE ->1A1C
1A13  LDA $7E2982 / CMP $96 / BNE ->1A1C
1A1B  RTS                              ; ★ $0194..$0196 is the SHADOW: unchanged party -> do nothing
1A1C  STZ $6E
1A1E  LDA $8D / BMI ->1A27 / STA $6D / JSR C0/597D     ; charId 0  (Crono)
1A27  LDA $8E …                                       ; charId 1  (Marle)
1A30  LDA $8F …                                       ; charId 2  (Lucca)
1A39  LDA $91 …                                       ; charId 4  (Frog)   ← note the 91/90 ORDER
1A42  LDA $90 …                                       ; charId 3  (Robo)
1A4B  LDA $92 …                                       ; charId 5  (Ayla)
1A54  LDA $93 …                                       ; charId 6  (Magus)
1A5D  LDA $7E2980 -> $94 ; $2981 -> $95 ; $2982 -> $96 ; refresh the shadow
1A6F  LDX #0000
1A74  LDA $97,X / BMI ->1A92           ; the NEW $0197/$0199/$019B (the respawns rewrote them)
1A78  TAY / REP #20
1A7B  LDA $7F1D09,X / STA $1800,Y
1A82  LDA $7F1D0F,X / STA $1880,Y
1A89  LDA $7F1D15,X / STA $0C00,Y
1A92  INX / INX / CPX #0006 / BNE ->1A74
1A99  $7F1D1B -> $AB ; $7F1D1C -> $AC ; $7F1D1D -> $AD / RTS
```

`$018D..$0193` (indexed by charId) and `$0197/$0199/$019B` (indexed by active slot) are both written by
the **PC loader** — the op-0x80/0x81 object-init handler:

```
C0/421E INY / TYX / LDA $7F2001,X / STA $D9            ; $01D9 = the charId this object loads
C0/4226 CMP #00 / BNE / LDA $6D / STA $8D              ; charId 0 -> $018D = this object's index
C0/4230 CMP #01 …  STA $8E                             ; 1 -> $018E
C0/423A CMP #02 …  STA $8F                             ; 2 -> $018F
C0/4244 CMP #03 …  STA $90                             ; 3 -> $0190
C0/424E CMP #04 …  STA $91                             ; 4 -> $0191
C0/4258 CMP #05 …  STA $92                             ; 5 -> $0192
C0/4262 CMP #06 …  STA $93                             ; 6 -> $0193
C0/426A LDA $D9 / CMP $7E2980 / BNE / LDA #00 / BRA ->429C   ; active slot 0 -> … STX $97
C0/4276          CMP $7E2981 / BNE / LDA #01 / BRL ->4310   ; active slot 1 -> … STX $99  (raw `86 99`)
C0/4281          CMP $7E2982 / BNE / LDA #02 / BRL ->43C3   ; active slot 2 -> … STX $9B
C0/428C LDA #80 / LDX $6D / STA $1100,X / …                 ; not in the party -> object parked
```

```
C0/597D LDA $6D / REP #20 / AND #00FF / ASL x4 / TAX        ; the object's script table entry
C0/5989 LDA $7F2001,X / TAX
C0/598E SEP #20
C0/5990 LDA $7F2001,X / BEQ ->59A5
C0/5996 TXY / REP #20 / AND #00FF / ASL / TAX / SEP #20
C0/59A0 JSR ($5D6E,X) / BRA ->5990                          ; ★ re-run the object's INIT function (fn0)
C0/59A5 REP #20 / INX / TXA / LDX $6D / STA $1180,X         ; the resulting IP
C0/59AE LDA #0000 -> $7F0580,X $7F0600,X $7F0680,X $7F0700,X
                    $7F0780,X $7F0800,X $7F0880,X $7F0900,X ; clear the 8 per-object script words
C0/59D3 SEP #20 / LDA #07 / STA $1C00,X / RTS
```

Re-running the PC object's init makes its `op 0x80` re-read `$2980` and re-classify itself — which is
what rewrites `$0197/$0199/$019B` and reloads the sprite. `C0/1A03` is also called from `C0/19E6`
(the mode-5 exit) and from battle teardown (`CD/B508`, `CD/B51C`, `CD/B530`) — it is a general routine,
not a menu special case.

### 2.13 The compacted party index — C2/816A (run by the swap)

```
2816A SEP #30 / LDX #00 / TXY / STX $73 / STX $75
28174 LDA $2980,X / BMI ->28189
28179 STA $0D5F,Y      ; $0D5F+i = charId, in roster order, empties squeezed out
2817C TXA / STA $0D68,Y ; $0D68+i = that member's roster slot
28180 INY / CPX #03 / BCS ->28187 / INC $73    ; $0073 = number of ACTIVE members
28187 INC $75                                  ; $0075 = number of roster members
28189 INX / CPX #09 / BCC ->28174
2818E RTS
```

Measured after the swap on `com_jogo_salvo`: `$0D5F.. = 00 02 04 01 FF FF FF FF FF`,
`$0D68.. = 00 01 02`, `$0073 = 3`, `$0075 = 4`.

---

## 3. RAM CONTRACT

| addr | meaning | proof |
|---|---|---|
| `$7E2980..$2982` | ACTIVE party charIds (bit7 = empty slot) | swapped by C2/C722, measured `000204 → 000402` writer `C2:C725` (§4.3) |
| `$7E2983..$2988` | RESERVE roster (bit7 = empty). `com_jogo_salvo` holds Marle at `$2983` | `--read wram:7E2980+9` (§4.1) |
| `$7E0F02..$0F0A` | 9 panel entries: `0x80` empty, `0x40\|slot` locked, `slot` selectable | `--callrom C2/CD2B --writes` (§4.2); measured live `40 01 02 80 80 80 80 80 80` (§4.3) |
| `$7E0F0B` | count of SELECTABLE active members | `--callrom` write log: `+1 (CD54) / −1 (CD62) / +1 (CD54)` → 1 on gato2, 2 on com_jogo_salvo |
| `$7E0F0C` | reserve count — **always 0 in the X-menu** (`$0D36 != 0`) | C2/CD72 + measured `$0F0B$0F0C = 02 00` (§4.3) |
| `$7E0F00` | first pick (roster index, `0xFF` = none) | writer `C2:C6AE` (= `STA $0F00` @C6AB) and `C2:C6F2` on reset (§4.3/§4.5) |
| `$7E0F01` | second pick (`0xFF` = none) | writer `C2:C65A` (= `STA $0F01` @C657) |
| `$7E0F0D` | the entry under the cursor (`slot`, or `0x40\|slot` / `0x40`) | writer `C2:C840` (= `STA $0F0D` @C83D) |
| `$7E0054` | cursor CELL (0..2 active, 3..6 reserve rows) | writers `C2:C652` (init), `C2:EA4A` (graph move), `C2:C9E4` (re-drive), `C2:C680`/`C2:C6E5` (pick/cancel) |
| `$7E007F` | the settled cursor (`$54` copy; also C2/C627's start index) | writer `C2:C84E`; **measured 0 at Form entry** (§4.6) |
| `$7E0081` | pre-move cursor — the "did it move?" blip gate | `C2/E9E7 STA $81` |
| `$7E0080` | reserve-list SCROLL offset | `C2/C869 DEC $80` / `C2/C89B STA $80`; measured 0 throughout (no reserves) |
| `$7E005A` | direction latch (copy of `$F5`); bit7 = "ran off the list" | `C2/E9F2 STA $5A`, `C2/EA4E TSB $5A` |
| `$7E0D9C`/`$0DA8` | cursor cell / scroll **at first-pick time** | `C2/C680` / `C2/C679` |
| `$7E0D36` | 0 ⇒ two-column screen with reserves; ≠0 ⇒ single centred column, no reserves. **`0xFF` in the X-menu** | writers C2/989E (`#FF`) and C2/C7AE (`$71`) only — byte scan; measured `FF` at f55 writer `C2:98A1` (§4.3) |
| `$7E0D17` | the LIVE substate mirror (`$0068` is coroutine-virtualised) | `C2/E98D LDA $68 / CMP $0D17 / STA $0D17`; measured `FF→01→02→01` (§4.4) |
| `$7E0067` / `$0068` | screen id / substate | `$0067 = 05` writer `C2:9A83`, refusal writer `C2:C638`, exit writer `C2:9A9E` |
| `$7E9380 + cell*6` | `{handX, handY, up, dn, lf, rt}`, 7 cells | copied from `$FF:B77A` by list `$FF:BF7B`; read back from `form1.raw.wram` (§4.4) |
| `$7E1811 + charId*0x40` | menu character POSE: 0 idle, 1 cursor, 0x0B picked | `C2/C9A9 STA $1811,X`; read back (§4.4) |
| `$7F01DF` | party-LOCK bitfield, masked by `$FF:F9BB[charId]` = `0x80 >> charId` | `C2/CD55/CD59`; ROM table dumped `80 40 20 10 08 04 02 01 00` |
| `$7E0194..$0196` | shadow of `$2980..$2982` — the reconcile's change detector | `C0/1A61/1A67/1A6D`; measured `000204 → 000402` writer `C0:1A6F` (§4.9) |
| `$7E018D..$0193` | PC object index **by charId** (bit7 = that PC has no object here) | writers `C0/422C..C0/4268`; measured `02 04 06 0A 08 0C 0E` writer `C0:426A` |
| `$7E0197/$0199/$019B` | PC object index **by active slot** | writers `C0/…STX $97/$99/$9B` (raw `86 99` at `C0/4327`); measured `02·06·08 → 02·08·06` writer `C0:4329` |
| `$7F1D09/0B/0D` | snapshot of the 3 active objects' `$1800` (X) | `C0/014C`; measured `2E50 2D61 2C71` |
| `$7F1D0F/11/13` | snapshot of `$1880` (Y) | `C0/0153` |
| `$7F1D15/17/19` | snapshot of `$0C00` (FG prio) | `C0/015A` |
| `$7F1D1B/1C/1D` | snapshot of `$01AB/AC/AD` | `C0/0169` |
| `$7E0D5F+i` / `$0D68+i` / `$0073` / `$0075` | compacted roster (charId / slot / active count / total) | `C2/816A`; read back after the swap |

---

## 4. ORACLE MEASUREMENTS

### 4.1 The gate, per state (answers (a))

```
$ for f in saves/*.state saves/battle_re/*.state saves/field_re/*.state ; do
    scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
      --read "wram:7E2980+9,wram:7F01DF+1,wram:7E0D36+1,wram:7E0100+2" --from "$f" ; done
```

`$0F0B` is computed the ROM's way: active members with bit7 clear, minus those with
`$7F01DF & (0x80>>charId)`. `$0F0C` is 0 everywhere because `$0D36` is set to `0xFF` at menu-open.
Verdict column = `($0F0B + $0F0C) >= 2`.

| state | `$2980..$2988` | `$7F01DF` | activeFree | **Form gate** |
|---|---|---|---|---|
| `saves/menu_open_gato2.state` | `00 01 80 \| 80×6` | 80 | 1 (Crono locked) | **REFUSE** |
| `saves/menu_open.state`, `menu_open_fair.state`, `menu_open_fair2.state` | `00 01 80` | 80 | 1 | **REFUSE** |
| `saves/field_pre_menu*.state`, `field_post_menu*.state` | `00 01 80` | 80 | 1 | REFUSE |
| `saves/base_loc6`, `battle592`, `gato_*`, `p300`, `p600`, `predoor_f2f_s5`, `forest_*` | `00 01 80` | 80 | 1 | REFUSE |
| `saves/bed3460`, `cat_anim`, `wake`, `slide*`, `portrun_*`, `intro_*`, `bell_lake`, `truce_market`, `predoor_s17`, `ow_w0`, `solo_forest`, `crononame`, `luccaname`, `field_re/scene254_tutorial` | `00 80 80` | 80 | 0 | REFUSE |
| `saves/field_re/scene8_taban_moving*.state`, `saves/jogo_trava_lake.state` | `00 80 80 \| 01 …` | 80 | 0 | REFUSE |
| `saves/battle_re/bat4..bat7_lake.state` | `00 80 80 \| 01 …` | 80 | 0 | REFUSE |
| `saves/battle_re/bat8_lake.state` | `00 02 80 \| 01 …` | 80 | 1 | REFUSE |
| **`saves/com_jogo_salvo.state`** | `00 02 04 \| 01 80×5` | 80 | **2** | **OPEN** ✅ |
| **`saves/grupo_3_mapa.state`** | `00 02 04 \| 01 …` | 80 | **2** | **OPEN** ✅ (field, loc 0x0082) |
| **`saves/battle_re/party3_field.state`** | `00 02 04 \| 01 …` | 80 | **2** | **OPEN** ✅ |
| **`saves/status_aplicado.state`, `saves/pre_derrota.state`** | `00 02 04 \| 01 …` | 80 | **2** | **OPEN** ✅ |
| **`saves/battle_re/bat9_lake.state`** | `00 02 04 \| 01 …` | 80 | **2** | OPEN (in battle) |
| `saves/audio_boot_f133.state` | `80×9` | 00 | 0 | REFUSE |

So **five** repo states open the Form screen; two of them (`grupo_3_mapa`, `party3_field`) are on a
**field map**, which is what the reconcile needs. `com_jogo_salvo` is the overworld (loc `0x01F1`).

### 4.2 C2/CD2B measured directly

```
$ scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --callrom C2/CD2B \
    --from saves/menu_open_gato2.state --writes
callrom: C2/CD2B in a=0000 x=0000 y=0000 m8 x16 c=0
callrom: ret ops=62 a=FFFF x=0002 y=0002 sp=09FF dp=0000 db=7E p=22 C=0 Z=1 N=0 V=0
callrom: 21 write(s)
  w 7E0F02 = 80  (by C2:CD34)      w 7E0F03 = 80  (by C2:CD34)
  w 7E0F04..7E0F0A = 80  (by C2:CD40)
  w 7E0F0B = 00  (by C2:CD43)      w 7E0F0C = 00  (by C2:CD43)
  w 000000 = 00  (by C2:CD47)
  w 7E0F0B = 01  (by C2:CD54)      ; slot0 Crono counted
  w 7E0F0B = 00  (by C2:CD62)      ; …then UN-counted: $7F01DF(80) & FF/F9BB[0](80) != 0 = LOCKED
  w 7E0F02 = 40  (by C2:CD69)      ; panel[0] = 0x40
  w 000000 = 01  (by C2:CD6C)
  w 7E0F0B = 01  (by C2:CD54)      ; slot1 Marle counted (80 & 40 == 0)
  w 7E0F03 = 01  (by C2:CD69)      ; panel[1] = 0x01
  w 000000 = 02 / 03  (by C2:CD6C) ; slot2 empty -> no panel write; reserve scan SKIPPED ($0D36=FF)
```

`$FF:F9BB` read through the harness's own HiROM mapper (the flat-file offset for bank `$FF` is
`0x3F0000 + addr`, **not** `0x30000 + addr` — an easy and silent off-by-a-nibble):

```
$ scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --read "rom:FF/F9B8+16,rom:C2/CD55+8"
  rom[FFF9B8+16] = 282B6B80 40201008 04020100 DA8B08C2      -> $FF:F9BB = 80 40 20 10 08 04 02 01 00
  rom[C2CD55+8]  = AFDF017F 3FBBF9FF                        -> LDA $7F01DF / AND $FFF9BB,X
```

### 4.3 The gate PASSES and the screen STAYS OPEN

```
$ scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
    --monitor "7E0F0B:2,7E0411:1,7E0067:1,7E0068:1,7E0D36:1,7E0054:1" \
    --from saves/com_jogo_salvo.state \
    --btnscript "0:20,200:2,0:40,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:20,100:2,0:80" --diff --writer
  f42     7E0D36=00*                                   ; DMA clear at menu open
  f55     7E0D36=FF*(C2:98A1)                          ; ★ C2/989E LDA #FF / STA $0D36
  f93     7E0F0B=0200*(C2:CD54)  7E0411=04*(C2:9A48)   ; ★ $0F0B=2 $0F0C=0 on the Form tab
  f115    7E0067=05*(C2:9A83)
  f116    7E0067=05             7E0054=01*(C2:C652)    ; ★ IT STAYS 05 — the screen is OPEN
```

Contrast, the refusal (this is what `menu_phase2.md` saw):

```
$ … --monitor "7E1E00:4,7E0067:1,7E0D76:1" --from saves/menu_open_gato2.state \
      --btnscript "0:20,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:20,100:2,0:40" --diff --writer
  f73     7E1E00=0000800A  7E0067=05*(C2:9A83)  7E0D76=00
  f74     7E1E00=0001800A*(C7:0196)  7E0067=00*(C2:C638)  7E0D76=03*(C2:EB15)
                                     ^^^^^^^^^^^^^^^^^^^ ★ back to 0 by C2/C638 + the BUZZ id $01
```

Panel array + swap, live:

```
$ … --monitor "7E0F00:2,7E0F0D:1,7E0054:1,7E2980:3,7E0067:1,7E0068:1,7E0F02:9,7E0080:2" …
  f93   7E0F02=400102808080808080*(C2:CD69)            ; ★ locked | slot1 | slot2 | 6× empty
  f116  7E0F00=FFFF*(C2:C65A)  7E0F0D=01*(C2:C840)  7E0054=01*(C2:C652)
  f177  7E0F0D=02*(C2:C840)  7E0054=02*(C2:EA4A)       ; Down 1->2 (graph)
  f199  7E0F0D=01*(C2:C840)  7E0054=01*(C2:C9E4)       ; ★ Down 2->0 is LOCKED -> re-driven to 1
  f221  7E0054=02*(C2:C9E4)                            ; ★ Up 1->0 LOCKED -> re-driven to 2

$ … --btnscript "<open>,100:2,0:40,20:2,0:30,100:2,0:60" --diff --writer
  f177  7E0F00=01FF*(C2:C6AE)                          ; first pick = slot 1
  f219  7E0F0D=02*(C2:C840)  7E0054=02*(C2:EA4A)
  f251  7E2980=000402*(C2:C725)  7E0F00=FFFF*(C2:C6F2) ; ★ THE SWAP: 00 02 04 -> 00 04 02
```

### 4.4 The screen itself (answers (c))

```
$ scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/com_jogo_salvo.state \
    scratchpad/menu/form1.raw \
    --btnscript "0:20,200:2,0:40,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:20,100:2,0:120"
$ python tools/_fbpng.py scratchpad/menu/form1.raw scratchpad/menu/form1.png
```

`scratchpad/menu/form1.png` — three 128×48 panels (Crono dim, Lucca, Frog), the hand on Lucca, an
"Exchange" window at the bottom. Also captured: `form_pick.png` (first pick made), `form_cur2.png`
(cursor on slot 2), `form_swap.png` (after the swap — Lucca and Frog have exchanged panels),
`rib4.png` (the ribbon at opt 4), `rib0.png` (the ribbon at opt 0), `sl2/6/10/16/24.png` (the entry frames).

PPU setup for that frame (`--ppuregs`): `mode=1`, **BG1 map word `$5800`**, **BG2 map word `$6000`**, both
32×64, **vertical scroll 1023** ⇒ **tile row `r` renders at screen `y = r*8 + 1`**; `objSize=0`
(8×8 / 16×16), OBJ tiles at `$4000`/`$5000`.

**The measurement that collapses the layout problem:**

```
form1.raw.vram vs rib4.raw.vram (the ribbon at opt 4):
  BG1 diff tiles 0                       ← the panels, the numbers, the "Exchange" label: IDENTICAL
  BG2 diff tiles 96  rows [12..17]  cols (8..23)   ← only where the ribbon icon box was
```

BG2 (chrome), cols 8..23 = x 64..191:

| tile rows | screen y | content |
|---|---|---|
| 2..7 | 17..64 | panel 0 — palette **4** when locked/empty, else 0 |
| 8..13 | 65..112 | panel 1 |
| 14..19 | 113..160 | panel 2 |
| 20..25 | 161..208 | the "Exchange" window |
| 0,1,26,27 | — | marble (`1FA0/1FA1/1FA2/1FA3`) |

Window tile set (per 16×6 box): corners `3DC/3DF/3E4/3E7`, top edge `3DD/3DE`, bottom `3E5/3E6`,
left column `3E0/3E2`, right `3E1/3E3`, interior `3E8..3EF` in a 2×2 brushed pattern.

BG1 (text), decoded with the CT mapped charset (`0xA0-B9='A'-'Z'`, `0xBA-D3='a'-'z'`, `0xD4-DD='0'-'9'`,
`0xE0='/'`, `0xFF=' '`, `0x20/0x22/0x24` = weapon-type icons, `0x2E` = armour icon). Panel `n`, base
tile row `R = 6n+2`:

| tile row | y | cols 9..18 |
|---|---|---|
| `R+1` | `48n+25` | name (6, space-padded) 9..14 · `LV` 15..16 · level right-aligned 17..18 |
| `R+2` | `48n+33` | `HP` 9..10 · cur right-aligned 11..14 · `/` 15 · max right-aligned 16..18 |
| `R+3` | `48n+41` | `MP` 9..10 · cur right-aligned 11..14 · `/` 15 · max right-aligned 16..18 |
| `R+4` | `48n+49` | ` ` 9 · weapon icon 10 · atk right-aligned 11..13 · ` ` 14 · armour icon `0x2E` 15 · def right-aligned 16..18 |

Verified row by row against the render: `Crono LV 3 / HP  25/ 96 / MP   2/ 12 / ⚔ 11 ⛊ 19`,
`Lucca LV 3 / HP  40/ 88 / MP  14/ 16 / ⚔ 8 ⛊ 20`, `Frog  LV 5 / HP 128/128 / MP  17/ 17 / ⚔ 18 ⛊ 38`.

The "Exchange" window: 2×2 icon at BG1 tiles `0x178/0x179/0x184/0x185` **palette 6**, cols 11..12,
rows 22..23 (x 88..103, y 177..192); the label `Exchange` at row 23, cols 13..20 (x 104..167, y 185..192).

OAM (`form1.raw.oam`):

```
 obj028 x= 56 y= 83 tile=020 pal=0 prio=3 big=1     ← THE HAND (slot 1); y=131 with the cursor on slot 2
 obj064..067 x=157/160 y=8,24,40,56  tile=080/082/084/086 pal=1 prio=2 big=1   ← Crono  (panel 0)
 obj072..075 x=152/168 y=70,70,86,86 tile=0A0..0A6            pal=3 prio=2     ← Lucca  (panel 1)
 obj080..083 x=152/168 y=120,120,136,136 tile=0C0..0C6        pal=5 prio=2     ← Frog   (panel 2)
```

After the swap the *same* object indices/palettes/tiles move to the other panel's Y
(`obj072..075 → y 119/135`, `obj080..083 → y 72/88`), i.e. the sprite slot belongs to the **character**
and only its position follows the panel. Hand ink box measured from the framebuffer (remembering the
`--peek` 512×480 buffer has 8 black rows of padding at the top, so measured y = SNES y + 8):
**x 56..71, SNES y 85..96 for slot 1, 133..144 for slot 2** — inside the 16×16 cell at OAM (56, 35+48n).

`form1.raw.wram` read-back:

```
$7E9380 graph: cell0 38 23 02 01 03 03 | cell1 38 53 00 02 04 04 | cell2 38 83 01 00 05 05
               cell3 E8 23 86 04 00 00 | cell4 E8 53 03 05 01 01 | cell5 E8 83 04 06 02 02
               cell6 E8 B3 05 83 02 02          ← ROM $FF:B77A with cells 0-2 handX patched 08 -> 38
$0F02..$0F0D: 40 01 02 80 80 80 80 80 80 | 02 00 | 01
$1811+n*0x40: 00 00 01 00 00 00 00      (cursor on Lucca, charId 2)
   …form_cur2: 00 00 00 00 01 00 00      (cursor on Frog, charId 4)
   …form_pick: 00 00 0B 00 00 00 00      (Lucca picked)
$0051 = 0000   $0054 = 01   $007F..$0081 = 01 00 01   $0D36 = FF
$0D5F.. = 00 02 04 01 FF … ; $0D68.. = 00 01 02 ; $0073 = 03 ; $0075 = 04   (C2/816A)
```

Substate mirror `$0D17` across a full session: `FF` (entering) → `01` (picker idle) → `02` (after the
first pick) → `01` (after the second pick/cancel), writer `C2:E995` each time.

**No entry animation.** `sl2.png` (2 frames after A) is pixel-identical to `sl6/sl10/sl16/sl24.png`.

### 4.5 LEFT/RIGHT are inert; B cancels a pick

```
$ … --monitor "7E0054:1,7E0F00:2,7E0F0D:1,7E0D17:1,7E0080:1,7E0067:1" --from saves/com_jogo_salvo.state \
    --btnscript "<open>,40:2,0:30,80:2,0:30,40:2,0:30,100:2,0:40,01:2,0:40,01:2,0:40" --diff --writer
  f116  7E0054=01*(C2:C652)          ; Form opens
  (Left @f177, Right @f209, Left @f241 -> NO row at all: $0054 never moves)
  f273  7E0F00=01FF*(C2:C6AE)  7E0D17=02   ; A -> first pick
  f315  7E0F00=FFFF*(C2:C6F2)  7E0054=01(C2:C6E5)  7E0D17=01   ; B -> pick CANCELLED
  f357  7E0067=00*(C2:9A9E)                ; B again -> back to the ribbon
```

A second A on the *same* member is **not** a cancel in the ROM: it stores `$0F0D → $0F01` and calls
C2/C70C, which swaps a byte with itself. `$2980` is unchanged (measured `f305 7E2980=000204(C2:C725)`),
so the net effect matches the engine's shortcut — but the ROM does run the whole reconcile tail
(C2/816A, C2/C784, C2/F378, C2/CD2B, C2/CDE3) and blips at C2/C6DE.

### 4.6 The entry cursor

```
$ … --monitor "7E007F:1,7E0054:1,7E0067:1" … --diff --writer
  f93   7E007F=00(C2:9A67)   ; the ribbon idle parks $7F at 0
  f115  7E0067=05*(C2:9A83)
  f116  7E007F=01*(C2:C84E)  7E0054=01*(C2:C652)
```

So C2/C627's `LDX $7F` starts the scan at 0; C2/C649 skips `$0F02[0] = 0x40` (locked) and lands on 1.

### 4.7 The dim is the Form context, not the main screen

BG1/BG2 word at (row, col 10) on `com_jogo_salvo`:

| capture | row 3 (panel 0) | row 9 (panel 1) | row 15 (panel 2) |
|---|---|---|---|
| ribbon opt 0 | BG1 `00B5` / BG2 `03ED` → **pal 0** | `10B5` / `13ED` → pal 4 | `10B5` / `3FAC` (ribbon box) |
| ribbon opt 4 | `10CB` / `13E9` → **pal 4** | `00CE` / `03E9` → pal 0 | `00CB` / `3FAC` |
| Form screen | `10CB` / `13E9` → **pal 4** | `00CE` / `03E9` | `00CB` / `03E9` |

At opt 0 the dim marks the *non-selected* member (the existing `CtMenuSetCharSel` behaviour). At opt 4
and on the Form screen it marks the **locked/empty** panel — matching C2/CC4F's only two callers,
C2/CA3F and C2/CB0D.

### 4.8 The reserve screen's trigger button

```
$ … --monitor "7E00F0:8" --from saves/grupo_3_mapa.state \
    --btnscript "0:20,400:2,0:20,800:2,0:20,4:2,0:20,2:2,0:20,200:2,0:20" --diff
  f21 (L)      7E00F0=20 00 20 00 20 00 20 00      -> $F0 bit5, $F6 bit5
  f43 (R)      7E00F0=10 00 10 00 10 00 10 00      -> $F0 bit4, $F6 bit4
  f65 (Select) 7E00F0=02 00 02 00 02 00 04 00      -> $F0 bit1, $F6 bit2
  f87 (Y)      7E00F0=04 00 04 00 04 00 01 00      -> ★ $F6 bit0 = Y
  f109 (X)     7E00F0=40 00 40 00 40 00 40 00      -> ★ $F6 bit6 = X  (the main menu)
```

And the storyline gate refuses:

```
$ … --hookpc 1985 --from saves/grupo_3_mapa.state --btnscript "0:20,2:2,0:60" --frames 90
  f20   visits=1   end=C0/A211  $6D: 08  vPos: 239*        ; C0/1985 IS reached by Y
$ … --hookpc 19CE …
  (no line with visits>0)                                   ; C0/19CE is NEVER reached
$ … --read "wram:7F0000+1" --from saves/grupo_3_mapa.state  ->  15
```

`0x15 < 0x49` ⇒ `C0/1993 SBC #49 / BCS` fails. **No repo state can open the reserve screen.**

### 4.9 THE PARTY RECONCILE, measured (answers (d))

```
$ scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
    --monitor "7E2980:3,7E0194:3,7E018D:7,7E0197:6,7E0067:1,7E0F00:2" --from saves/grupo_3_mapa.state \
    --btnscript "<open>,100:2,0:40,20:2,0:30,100:2,0:60,01:2,0:60,01:2,0:120" --diff --writer
  f0    7E2980=000204  7E0194=000204  7E018D=0204060A080C0E*  7E0197=020006000800*
  f251  7E2980=000402*(C2:C725)  7E0194=000204                 ; swapped in the menu; shadow stale
  f313  7E0067=00*(C2:9A9E)                                    ; menu closed
  f416  7E0194=000402*(C0:1A6F)   7E018D=0204060A080C0E(C0:426A)
        7E0197=020008000600*(C0:4329)                          ; ★ slot1<->slot2 objects exchanged
```

```
$ … --hookpc 597D --from saves/grupo_3_mapa.state --btnscript "<open+swap+close>" --frames 460
  f416  visits=7  end=C0/760B  $6D: 02 04 06 08 0A 0C 0E  vPos: 21* 25 26 30 34 35 37
$ … --hookpc 597D … --btnscript "<open+close, NO swap>" --frames 460
  (no line with visits>0)
```

⇒ **seven** `C0/597D` calls on the resume frame when the party changed, **zero** when it did not, in
ascending PC-object order.

```
$ … --monitor "7F1D09:22,7E1800:12,7E1880:12" … --diff --writer
  (throughout)  7F1D09=502E 612D 712C | 002C 002C 002C | 2020 2020 2020 | 25 16 07 00
  f383  7E1800=00FF 502E 00FF 612D 712C 00FF*(C0:0914)   ; obj1=2E50 obj3=2D61 obj4=2C71
  f416  7E1800=00FF 502E 00FF 712C 612D 00FF*(C0:1A82)   ; ★ obj3=2C71 obj4=2D61 — positions restored
        7E1880=…(C0:1A89)                                ;   through the NEW $0197/$0199/$019B
```

And the visible proof: `scratchpad/menu/fld_ctl.png` (open+close, no swap) vs
`scratchpad/menu/fld_swap.png` (open+swap+close) — the two followers have exchanged places **without a
scene reload**.

---

## 5. IMPLEMENTATION SPEC

Targets: `src/menu.c` (`CtMenuRenderForm`, plus a small roster helper), `src/menu.h` (retire the
"task-spawned slide-in / never settles" wording — it is wrong), `src/field.c` (the screen-5 input block
around `:8290`, the screen-5 render at `:8476`, and a NEW menu-exit reconcile).

### 5.0 What is already correct — do not touch

* `CtMenuFormRoster` / `CtMenuFormCanOpen` reproduce C2/CD2B and C2/C627 exactly, including the
  `0x80>>charId` mask (ROM `$FF:F9BB` verified byte-for-byte) and the `$0D36` gate on the reserve scan.
* `CtMenuFormSwap` reproduces C2/C70C's two-byte exchange.
* `ctMenuChromeEnteredTM[4]` in `src/data/menu_chrome.h` is **byte-identical** to the oracle's Form BG2
  tilemap (verified this pass: 0 differing tiles over all 896). `CtMenuChrome(fb, 5, …)` is right.
* The refusal path (`menuScreen = 0` + `menuSfx(s7, 0x01, "form-refuse")`) matches C2/C636-C63A.

### 5.1 Roster + panel array (new, small)

Add a builder that returns the ROM's `$0F02..$0F0A` array, not just the two counts — the picker needs it:

```
panel[i] for i in 0..8:
   b = mem[0x2980+i]
   if (b & 0x80)                              panel[i] = 0x80          // empty
   else if (i >= 3 && mem_0D36 != 0)          panel[i] = 0x80          // reserve scan skipped
   else if (i < 3 && (glb[0x01DF] & (0x80>>b))) panel[i] = 0x40 | i     // LOCKED
   else                                       panel[i] = i
```
**`mem_0D36` must be forced to non-zero for the X-menu** (ROM `C2/989E LDA #FF / STA $0D36` at menu-open).
Reading whatever the field left in `mem[0x0D36]` is a latent bug in `CtMenuFormRoster` today: a field
state that happens to leave `$0D36 == 0` would make our engine count reserves the ROM never counts.

### 5.2 Cursor (`menuFormSlot` = ROM `$0054`)

* **Entry**: scan `k = 0,1,2,…` and take the first `k` with `(panel[k] & 0xC0) == 0` (C2/C649, with
  `$7F = 0` measured at entry). Clear both picks.
* **UP** → `k-1 mod 3`, **DOWN** → `k+1 mod 3` (graph cells 0..2). Then, while
  `(panel[k] & 0xC0) != 0`, apply the same graph step again (C2/C805 → C2/C9AE → C2/C9CD). Today the
  engine only skips `0x80`-empty slots — it must skip **locked** members too, or the cursor rests on
  Crono and the player can pick an exchange the ROM forbids.
* **LEFT / RIGHT** → no-op in the X-menu (derived in 2.6, measured in §4.5). Keep them inert, but say
  *why* in the comment: the graph sends the cursor to cells 3..5, whose entries are `0x80`, and
  C2/C9AE subtracts the 3 back because `$0F0C == 0`.
* `menuFormSlot` is also the roster index handed to `CtMenuFormSwap` (identical for active and reserve —
  the ROM indexes `$2980..$2988` uniformly).

### 5.3 Picks

* **A, no pick pending** → `menuFormPick = menuFormSlot` (C2/C6A8 `$0F0D → $0F00`).
* **A, pick pending** → if `(panel[menuFormSlot] & 0x40)` refuse (C2/C6D0 `BIT $0F0D / BVS`), else
  `CtMenuFormSwap(mem, menuFormPick, menuFormSlot)`; then in **either** case reset both picks and stay
  in substate 1 (C2/C6DE falls through into C2/C6E7). The engine's current "A on the same member
  cancels" produces the same `$2980`, but write it as the ROM does so the refusal condition is the
  lock bit, not slot equality.
* **B, pick pending** → cancel (reset picks, stay). **B, no pick** → `menuScreen = 0`.

### 5.4 SFX — the five sites, now with their conditions (answers (e))

All are `cmd $18` through the throttle `fieldMenuSfx` (`src/field.c:1607`), id `$00` unless noted:

| site | when | engine hook |
|---|---|---|
| C2/C63A → **EACC**, id **`$01`** | gate `($0F0B+$0F0C) < 2` | already ported (`"form-refuse"`) |
| **C2/C642** | the gate PASSED, screen committed | already ported (`"screen-open"`) |
| **C2/C670** | substate 1: the cursor actually moved (`$81 != $54` after C2/E9E2 + C2/C805) | add: on a U/D press that changes `menuFormSlot` |
| **C2/C674** | substate 1: **A** (`$0D1D` bit7) — the first pick | add: unconditionally on the first A |
| **C2/C6CC** | substate 2: the cursor actually moved (`$81 != $54`) | add: same test, while a pick is pending |
| **C2/C6DE** | substate 2: **A** (swap OR lock-refused) **or B** (cancel) — ONE shared site | add: on any of the three |
| C2/C6B3 → C2/C6E7 → C2/9A98 → **C2/9AAA** | B with no pick — exit to the ribbon | already ported (`"back-to-ribbon"`) |

No site fires "because the swap happened"; the swap's sound is the shared C2/C6DE blip. `menu_sfx.md`
row **P2** can now be closed with these conditions; **P3**'s note (Form is the only sub-screen that runs
a teardown, C2/C6E7, before C2/9A98) stands.

### 5.5 Render (`CtMenuRenderForm`)

The measured identity makes this nearly free: **the Form frame == the frame the engine already renders
for the ribbon at `menuOpt == 4`, minus the ribbon box.** Concretely:

1. `CtMenuChrome(fb, 5, 4, 2)` — already byte-exact.
2. Draw the same party-panel content the ribbon opt-4 view draws, at the measured geometry
   (col 9 → x 72; panel `n` text rows at y `48n+25 / 48n+33 / 48n+41 / 48n+49`; field sub-columns as
   tabulated in §4.4). Dim panel `n` (palette 4) iff `(panel[n] & 0xC0) != 0` — **locked or empty**, not
   "not the lead". `mPartyPanels(…, h->mem[0x2980] & 7)` (lead-bright) is invention and must go.
3. Hand cursor: ROM `$7E9380[cell*6 + 0..1]` = `(56, 35 + 48*slot)`; the 16×16 sprite covers
   `x 56..71, y (36+48*slot)..(51+48*slot)`. One hand only.
4. **Remove the second hand on the right edge for the picked member** — invention. The ROM's picked
   indicator is the character's SPRITE POSE (`$1811 + charId*0x40 = 0x0B`; cursor = `1`, idle = `0`).
   Until the menu sprite animator exists, either show no picked marker or reuse the pose byte in the
   engine's own sprite draw — but do not invent a second cursor.
5. Bottom window: the "Exchange" icon at x 88..103, y 177..192 and the label at x 104, y 185.

### 5.6 THE PARTY RECONCILE (the visible bug) — new code at menu EXIT

Model `C0/011B` + `C0/1A03` in the engine's field/menu boundary:

* **At menu open** (where the engine already freezes the field): snapshot, for each active slot
  `s = 0,1,2` whose PC object index `objIdx[s]` is valid, the object's X, Y and FG-prio words. That is
  ROM `$7F1D09/0B/0D`, `$7F1D0F/11/13`, `$7F1D15/17/19` indexed by `$0197/$0199/$019B`.
* **At menu close**, before the field resumes:
  1. If `mem[0x2980..0x2982]` equals the shadow (ROM `$0194..$0196`), do nothing (C0/1A1B).
  2. Else, for each charId in the ROM's order **0, 1, 2, 4, 3, 5, 6** (i.e. object order Crono, Marle,
     Lucca, Frog, Robo, Ayla, Magus), if that charId has a PC object in this scene
     (ROM `$018D..$0193`, bit7 = absent), **re-run that object's init function (fn0)** — the engine's
     equivalent of `C0/597D`: re-execute the object's script from its init entry, store the resulting IP,
     zero its 8 per-object script state words (`$7F0580/0600/0680/0700/0780/0800/0880/0900`), and set
     `$1C00,X = 7`. Re-running `op 0x80` is what re-reads `$2980` and re-derives which slot (or none)
     each PC object now is — that is what rewrites `$0197/$0199/$019B` and reloads the sprite.
  3. Refresh the shadow from `$2980..$2982`.
  4. Restore the three snapshotted X / Y / FG-prio words into the objects named by the **new**
     `$0197/$0199/$019B` (so slot 1's old on-screen position is inherited by whoever is slot 1 now),
     and `$7F1D1B/1C/1D → $01AB/AC/AD`.

  This is what makes the new order show on the map with no scene reload (§4.9, `fld_swap.png`).
  Do **not** scope it to the Form screen: `C0/1A03` is the general post-menu / post-battle reconcile
  (also called from `C0/19E6`, `CD/B508`, `CD/B51C`, `CD/B530`).

### 5.7 Reserves — do NOT model them on screen 5

`$0D36 = 0xFF` for the whole X-menu, so C2/CD72 always skips the reserve scan and `$0F0C` is always 0.
Reserve exchange is a **different menu program** (mode 5, screen `$07`, C2/C7A6), opened by **Y** in the
field with `$7F0000 >= 0x49`. Adding a reserve column to screen 5 would be invention. The reserve data
model itself is nonetheless pinned above for whenever that screen is done: roster `$2983..$2988`, panel
entries `$0F05..$0F0A`, four visible rows (cells 3..6) with scroll `$0080` capped at `$0F0C - 4`, a
scrollbar sized `0xA400 / ((reserveN-4)*6)`, and the **same** `CtMenuFormSwap` for the exchange.

### 5.8 Suggested regression checks

* `menuform-gate`: on `saves/menu_open_gato2` `CtMenuFormCanOpen == 0`; on `saves/grupo_3_mapa`
  (party `00 02 04`, `$7F01DF = 0x80`) `== 1` with `activeFree == 2, reserveN == 0`.
* `menuform-cursor-skips-lock`: with `panel = {0x40, 0x01, 0x02, 0x80×6}`, Down from 2 lands on **1**
  and Up from 1 lands on **2** (never 0) — the oracle rows `f199`/`f221` above.
* `menuform-swap-reconcile`: after `CtMenuFormSwap(mem,1,2)` and a menu close on a 3-PC field scene,
  the follower object indices for slots 1 and 2 have exchanged and each keeps the *slot's* old position
  (anchor on the EVENT — "party changed at menu close" — not on a frame number).

---

## 6. OPEN / BLOCKED ON A CAPTURE

1. **The two-column reserve screen (mode 5 / screen `$07`, `$0D36 == 0`) cannot be captured here.**
   Every 3-party state has `$7F0000 = 0x15`, and `C0/1993 SBC #49 / BCS` needs `>= 0x49`; `--hookpc 19CE`
   with Y held gives **zero** visits (§4.8), and `ct_probe.exe` has no WRAM-poke mode.
   **Unblocking capture:** a LakeSnes state on a **field map** with `$7F0000 >= 0x49`, three active
   members and at least one non-empty `$2983..$2988` — then `--peek <state> out --btnscript "0:20,2:2,0:120"`
   (Y = harness bit `0x02`). Without it these stay underived: the reserve panel's window rectangle
   (C2/C784 says text col `0x13` = 19, but the graph's reserve `handX = 0xE8` = 232 does not sit one tile
   left of a 16-wide box the way the active column's does, so the reserve box is either narrower or the
   hand sits on its right), the reserve panel's text fields, the scrollbar's pixels, and the smooth
   scroll animation (`C2/C8BA`, `$0D9E = 0xD0/0x30`, `$0D22 = ±8`, `$0022 = 0x0E/0x10`).
2. **The menu character-sprite animator is not derived.** The pose byte and its three values are
   (`$7E1811 + charId*0x40` = 0 / 1 / 0x0B, C2/C973); its consumer is the menu object engine at DP `$1980`
   (`C2/CE2A`, `C2/CE49`, driven by `C2/CDE3`), and the OAM composition differs per character
   (Crono renders as a 1×4 column of 16×16 objects, Lucca and Frog as 2×2). Neither the per-character
   OAM template nor the animation tables were traced. The panels' Y also **bobs** frame to frame, so no
   fixed sprite offset exists.
3. **The `Atk`/`Def` row's source bytes.** The panel shows a weapon-type icon + a number and an armour
   icon + a number (Crono 11/19, Lucca 8/20, Frog 18/38). `menu_phase2.md` already lists this as
   untraced; it is inherited here unchanged because the Form's BG1 is literally the ribbon's BG1.
4. **Two-digit level alignment.** Every observable character is `LV 3`/`LV 5`, so "level right-aligned in
   cols 17..18" is inferred from the surrounding spaces, not observed with a 2-digit level.
5. **The 9-byte blocks `$FF:BAEA` / `$FF:BB17` / `$FF:BB45` / `$FF:BCF6`** copied into the DMA buffers
   `$7E:9180 / $9100 / $9750 / $9680` by the setup lists were dumped but not decoded; the engine replaces
   them with the captured tilemap, so they only matter if someone wants pixel-exact chrome generation.
6. **`$0068` vs `$0D17`.** The engine has no coroutine, so the ROM's substate register is only visible
   through the mirror `$0D17`. Values pinned (`FF / 01 / 02`), but the one-frame ordering the ROM gets
   from `C2/821E` yields is not modelled — the same boundary `menu_sfx.md` §4 already records.
7. **Left/Right inertness is state-dependent, not absolute.** It is inert *because* `$0F0C == 0`. If a
   future engine ever models `$0D36 == 0`, L/R becomes a real column hop (C2/C9AE) and must be added.

---

## ADVERSARIAL VERIFICATION

Independent review pass. The author of §1-§6 died before reporting, so nothing above had ever been
checked. Every citation below was **re-disassembled** and every measurement **re-run** from scratch
(probe `scratchpad/menu/ct_probe.exe`, ROM `Chrono_Trigger_USA_SNES.sfc`). Verdict:
**SOUND WITH FIXES** — the ROM trace is unusually accurate (I could not break a single opcode-level
claim in §2), but there are **eight defects**, three of them load-bearing: a citation that does not
exist (D1), a measurement the author's own files disprove (D2), and one place where the
IMPLEMENTATION SPEC does not reproduce the ROM routine it claims to (D3).

Shorthand below: `OPEN` = `0:20,200:2,0:40,80:2,0:8,80:2,0:8,80:2,0:8,80:2,0:20` (X, then Right x4 to
ribbon opt 4). Button bits are the LakeSnes serial order — **A = 0x100, 0x80 = Right** — which is what
every script in §4 already uses; I found **no** wrong-bit script anywhere in the dossier.

---

### AV.1 — DEFECTS

#### D1 (MAJOR — a citation that is not there). `C0/1A03` is **not** called from battle teardown.

§2.12 and §5.6 both state: *"`C0/1A03` is also called from `C0/19E6` (the mode-5 exit) and from battle
teardown (`CD/B508`, `CD/B51C`, `CD/B530`) — it is a general routine, not a menu special case."*

**REFUTED.** There is **no cross-bank call to `C0/1A03` anywhere in the 4 MB ROM** — a scan for the JSL
encoding `22 03 1A C0` returns **zero** hits. `JSR` is bank-local, so the only real callers are the two
in bank `$C0`:

```
JSR C0/1A03 (bank C0 only): ['C0/1978', 'C0/19E6']      # scan `20 03 1A`, bank-filtered
JSL C0/1A03               : []                          # scan `22 03 1A C0`, whole ROM
```

The `CD/B50x` hits are a **bank-naive byte scan mistaking data for code**. `CD/B4F8` is a 20-byte-periodic
table (a battle animation script):

```
CD/B4F8: 10 90 B0 85 A0 0D 02 06 A8 08 71 00 73 00 24 02
CD/B508: 20 03 1A 70 02 05 0C 10 50 90 85 20 0D 02 06 A8
CD/B518: 08 71 24 05 20 03 1A 70 02 05 0C 10 A0 A0 85 20
CD/B528: 0D 02 06 A8 08 71 24 08 20 03 1A 70 02 05 0C 10
CD/B538: 90 B0 85 20 0D 02 06 A8 08 71 00 73 00 24 02 20
```

`... 20 03 1A 70 02 05 0C 10 xx xx 85 20 0D 02 06 A8 08 71 24 xx ...` repeats verbatim every 20 bytes
with only two operand bytes varying. And even if it were code, `20 03 1A` executing in bank `$CD` is
`JSR CD/1A03`, not a call into bank `$C0`.

**Impact:** §5.6's justification *"Do not scope it to the Form screen"* survives — but on `C0/19E6`
**alone** (the mode-5 / reserve-screen exit), not on a post-battle path. Delete the `CD/B50x` half.
A C engine that wires the reconcile into *battle* teardown on the strength of this line would be
implementing a mechanism the ROM does not have.

#### D2 (MAJOR — a measurement the author's own captures disprove). The Form entry frames are **not** pixel-identical.

The frontmatter and §4.4 both assert: *"it is fully settled **2 frames** after the A press
(`scratchpad/menu/sl2.png` ... `sl24.png` are pixel-identical)"* and *"**No entry animation.** `sl2.png`
(2 frames after A) is pixel-identical to `sl6/sl10/sl16/sl24.png`."*

**REFUTED by the files the dossier itself points at.** They are not even the same size on disk
(10244 / 10244 / 10214 / 10225 / 10249 bytes). Hashing the raws and diffing pixel-by-pixel:

```
sl2   raw md5 2585907771818192   sl6  raw md5 2585907771818192
sl10  raw md5 0845c258cb7a5263   sl16 raw md5 f7287606942863d4   sl24 raw md5 431999fc96283169
sl2 vs sl6   differing PIXELS = 0
sl2 vs sl10  differing PIXELS = 620
sl2 vs sl16  differing PIXELS = 1188
sl2 vs sl24  differing PIXELS = 420
```

I re-captured them independently and got byte-identical raws (same md5s), so this is not a stale
artifact — the author simply never diffed the five captures the claim rests on.

**What is actually true** (measured, `--peek ... --btnscript "OPEN,100:2,0:N"` for N = 2,6,10,16,24,40,
comparing the BG1/BG2 tilemaps at word `$5800`/`$6000`, the OBJ tile block at word `$4000`, and OAM):

```
N     BG1 map same   BG2 map same   OBJ tiles same   OAM(512) same   OBJ tile bytes differing
 2       True           True            True             True                0
 6       True           True            True             True                0
10       True           True            False            True              155
16       True           True            False            False             166
24       True           True            False            True              110
40       True           True            False            False             191
```

=> **the CHROME is settled 2 frames after A and never changes again** (0 differing tilemap entries out to
f+40) — so the dossier's *conclusion* (there is no slide-in; `menu_phase2.md`'s "never settles" was the
refusal misread) **stands, and is the thing that matters for §5.5.** What is false is the *evidence*:
the frame is not pixel-stable, because the menu character sprites keep animating via re-DMA'd OBJ tile
data plus an alternating OAM — which is exactly the mechanism §6.2 already books as **underived**
("the panels' Y also bobs frame to frame"). Restate as: *"the BG1/BG2 tilemaps are byte-identical from
+2 frames onward (measured to +40); the framebuffer is not, because the character sprites animate."*

#### D3 (MAJOR — the spec does not reproduce the routine it cites). §5.1's panel array is index-aligned; `C2/CD2B` **compacts**.

§5.1 gives:

```
panel[i] for i in 0..8:  b = mem[0x2980+i]
   if (b & 0x80) panel[i] = 0x80 ; else if (i>=3 && $0D36) panel[i] = 0x80
   else if (i<3 && lock) panel[i] = 0x40|i ; else panel[i] = i
```

That writes entry `i` at **index `i`**. The ROM does not. Re-disassembled (matches §2.2 exactly):

```
2CD43 SEP #30 / STZ $00 / LDX #00 / TXY        ; * Y is a SEPARATE cursor from the slot counter $00
2CD4A LDX $00 / LDA $2980,X
2CD4F BMI ->2CD6A                              ; * empty -> Y is NOT advanced
2CD51 INC $0F0B / TAX / LDA $7F01DF / AND $FFF9BB,X
2CD5D BEQ ->2CD64 / DEC $0F0B / LDA #40
2CD64 ORA $00 / STA $0F02,Y / INY              ; * VALUE = the roster slot $00 ; POSITION = Y
2CD6A INC $00 / LDA $00 / CMP #03 / BCC ->2CD4A
```

So on the ACTIVE side the ROM builds a **compacted** list: the value stored is the roster slot (`$00`),
the position is the count of non-empty slots seen so far (`Y`). The two agree only when the empty active
slots are **trailing** — which is the case in every capture this repo owns, which is why it was never
caught.

**And the hole is reachable.** Field-VM **opcode `0xD6` -> `C0/3A07`** (dispatch table `C0/$5D6E`, entry
0xD6 = `3A07`) removes a PC from the active party by writing `#80` into the slot **in place, with no
compaction**:

```
C0/3A14 LDA $97 / BMI ->3A2F / TAX / LDA $1101,X / CMP $D9 / BNE ->3A2F
C0/3A20 LDA #03 / STA $1100,X / LDA #80 / STA $97 / STA $7E2980 / BRA ->3A65   ; slot 0
C0/3A31 LDA $99 ...                               STA $99 / STA $7E2981        ; slot 1
C0/3A4C LDA $9B ...                               STA $9B / STA $7E2982        ; slot 2
```

(The *other* removal path, op `0xD5` -> `C0/3A68` -> `JSL C2/8004` -> `C2/8CA2`, **does** compact:
`LDA $2981,Y / STA $2980,Y / INY / CPY #02`. Two different ops, two different behaviours.)

Worked divergence for `$2980 = {80, 02, 04}`, `$7F01DF = 0x80`:

| | panel[0] | panel[1] | panel[2] | `$0F0B` | gate |
|---|---|---|---|---|---|
| **ROM** (`C2/CD2B`) | `0x01` | `0x02` | `0x80` | 2 | OPEN |
| **§5.1 pseudo-code** | `0x80` | `0x01` | `0x02` | 2 | OPEN |

Same gate, **different screen**: the ROM draws the two members in panel rows 0-1 (hand Y 35 / 83) and
dims row 2; §5.1 draws them in rows 1-2 (hand Y 83 / 131) and dims row 0. `C2/CC4F` (which loops
`$0F02,X` for X = 0..2 and is also run by the ribbon's opt-4 hover, `C2/CA3F`) would dim the wrong panel
even before the screen opens. Fix §5.1 to carry the ROM's two cursors:

```
y = 0
for i in 0..2:  b = mem[0x2980+i]
    if (b & 0x80) continue                                   # C2/CD4F: no entry, y NOT advanced
    lock = glb[0x01DF] & (0x80 >> b)
    panel[y++] = (lock ? 0x40 : 0x00) | i                    # C2/CD64: VALUE = slot i, POSITION = y
# reserves ALWAYS start at panel index 3 ($0F05), independent of y:
if (mem_0D36 == 0):
    y2 = 0
    for i in 3..8: if (!(mem[0x2980+i] & 0x80)) panel[3 + y2++] = i
```

Corollary: §5.7's *"reserve ... panel entries `$0F05..$0F0A`"* is only right when all three active slots
are occupied. The reserve base is **fixed** at `$0F05` (`STA $0F05,Y`, C2/CD89) — it is not "after the
actives", and with 2 actives `$0F04` stays `0x80` as a permanent gap. `C2/C805`'s `LDA $54 / CMP #03 /
BCC / CLC ADC $80 / TAX / LDA $0F02,X` confirms cell 3 => `$0F05` unconditionally.

#### D4 (MINOR — the verdict undercounts what disappears). Entering Form also removes **8 OBJ sprites**, not just the box.

§1: *"the main menu screen with the ribbon box erased, **plus one hand-cursor sprite**"*; §5.5: *"the Form
frame == the frame the engine already renders for the ribbon at `menuOpt == 4`, minus the ribbon box."*

The BG half is exactly right (I reproduced it: BG1 **0** differing tiles, BG2 **96** in rows 12-17 x
cols 8-23 — see AV.2). The OBJ half is not. Diffing `rib4.raw.oam` against `form1.raw.oam`:

```
idx  0 rib4=(x 80,y112,tile 004,pal0,prio3,16x16)  form=GONE     - the six RIBBON ICONS
idx  1 rib4=(x 96,y112,tile 006,pal0,prio3)        form=GONE
idx  2 rib4=(x112,y112,tile 008,pal0,prio3)        form=GONE
idx  3 rib4=(x128,y112,tile 00A,pal0,prio3)        form=GONE
idx  4 rib4=(x144,y112,tile 02C,pal0,prio3)        form=GONE
idx  5 rib4=(x160,y112,tile 00E,pal0,prio3)        form=GONE
idx  6 rib4=(x145,y128,tile 022,pal7,prio3)        form=GONE     - the ribbon's selection arrows
idx  7 rib4=(x145,y 96,tile 022,pal7,prio3)        form=GONE
idx 28 rib4=GONE                                   form=(x 56,y 83,tile 020,pal0,prio3,16x16)  <- THE HAND
idx 72..75  (Lucca) y 72/88 -> 70/86               <- the sprite BOB of D2, not a Form effect
```

The ribbon icons are **OBJ**, not tilemap, so "erase the ribbon box" does not remove them. Restate §1 as
*"minus the ribbon box **and its eight icon/arrow sprites**, plus one hand"*. In the current engine this
is harmless (the icons come from the ribbon renderer, and `CtMenuChrome(fb, 5, ...)` selects
`ctMenuChromeEnteredTM[4]`, which has no box) — but §5.5 read literally would leave them on screen.

#### D5 (MINOR — an "exhaustive" table with a missing row). **Six** repo states open the Form screen, not five.

§4.1 claims its `for f in saves/*.state saves/battle_re/*.state saves/field_re/*.state` sweep is complete
and concludes *"So **five** repo states open the Form screen"*. I re-ran the sweep over all 81 states.
`saves/eq_spare.state` is **absent from the table entirely**:

```
$ ct_probe.exe ... --read "wram:7F01DF+1,wram:7E2980+9,wram:7E0100+2" --from saves/eq_spare.state
  wram[7F01DF+1] = 80
  wram[7E2980+9] = 000204018080808080
  wram[7E0100+2] = F101              (loc 0x01F1 = overworld, like com_jogo_salvo)
$ ct_probe.exe ... --monitor "7E0067:1,7E0054:1,7E0F0B:2" --from saves/eq_spare.state \
      --btnscript "OPEN,100:2,0:80" --diff --writer
  f93     7E0067=00  7E0054=04*(C2:EA4A)  7E0F0B=0200*(C2:CD54)
  f115    7E0067=05*(C2:9A83)  ...
  f116    7E0067=05  7E0054=01*(C2:C652)  7E0F0B=0200      <- it STAYS 05: the screen is OPEN
```

Corrected count: **six** non-battle states open it (`com_jogo_salvo`, `grupo_3_mapa`, `party3_field`,
`status_aplicado`, `pre_derrota`, **`eq_spare`**) plus `battle_re/bat9_lake` in battle. Nothing else in
§4.1 changes; every other row I spot-checked is right (`bat4..bat7` = `00 80 80 | 01`, `bat8` = `00 02 80
| 01` => activeFree 1 => REFUSE, `scene8_taban_moving*` and `jogo_trava_lake` = `00 80 80 | 01`).

#### D6 (MINOR — §5.0 and §5.2 contradict each other). `CtMenuFormSwap` is clamped to 0..2.

§5.0 lists it under *"What is already correct — do not touch: `CtMenuFormSwap` reproduces C2/C70C's
two-byte exchange."* §1 and §5.2 say the opposite of the shipped code: *"A reserve<->active exchange is
the same instruction: the indices simply run 0..8"* / *"`menuFormSlot` is also the roster index handed to
`CtMenuFormSwap` (identical for active and reserve)"*. The shipped function refuses anything above 2:

```c
// src/menu.c:645
void CtMenuFormSwap(uint8_t* mem, int a, int b) {
  if (!mem || a < 0 || a > 2 || b < 0 || b > 2) return;
```

and `src/menu.h:302-305` documents the clamp (*"a/b out of 0..2 (or NULL mem) = no-op"*). `C2/C70C` has
no such guard (`LDX $0F00 / LDA $2980,X` — 8-bit X, uniform over `$2980..$2988`). Inert **today**
(`$0F0C == 0` on screen 5, so cells >= 3 are never selectable), but the two sections cannot both be
followed, and §5.0's "do not touch" is what a reader will act on.

#### D7 (MINOR — the re-drive rule is stated wrong). `C2/C9CD` does not repeat "the same graph step".

§5.2: *"**UP** -> `k-1 mod 3`, **DOWN** -> `k+1 mod 3` ... Then, while `(panel[k] & 0xC0) != 0`, **apply
the same graph step again**"*. The graph directions are right (I confirmed cell0 up=2 dn=1 / cell1 up=0
dn=2 / cell2 up=1 dn=0 from `$FF:B77A`), the re-drive rule is not:

```
2C9CD LDA $54 / JSR C2/EA27          ; X = cell*6
2C9D2 LDA $5A / BIT #08 / BNE ->2C9DD
2C9D8 LDA $9383,X / BRA ->2C9E0      ; * DOWN — the DEFAULT
2C9DD LDA $9382,X                    ; * UP — only iff the LIVE latch $5A has bit3
2C9E0 AND #0F / STA $54 / RTS
```

The direction is re-read from `$5A` (a copy of `$F5`, refreshed every frame at `C2/E9F0 LDA $F5 / STA
$5A`) at re-drive time, not remembered from the original move. Two consequences the spec must carry:

* **`C2/C805` runs every frame** from both idle handlers (`C2/C667`, `C2/C6C3`), not only on a press. With
  no key held (`$5A = 0`) an invalid resting cell is re-driven **DOWN**. It coincides with "the same step"
  for UP/DOWN only.
* After the L/R `+/-3` arm (`C2/C9BF ADC #03` / `C2/C9C8 SBC #03`) control returns to the `2C819` loop and
  can then take the `C2/C9CD` walk. That is the full reason L/R is inert: LEFT from cell 1 goes
  `1 ->(graph lf) 4 ->($0F0C==0) 1`, and LEFT from a locked cell 0 goes `0 -> 3 -> 0 ->(locked, $5A&03
  still set but now cell<3 and $0F0B!=0) graph DOWN -> 1`.

Also worth correcting: §4.5's *"`$0054` never moves"* on L/R is a **per-frame sample**, not the truth —
`$0054` is written twice inside the frame (`C2:EA4A` to 4, `C2:C9E4` back to 1) and the monitor only
prints the frame-boundary value, hence no row.

#### D8 (NIT — citation addresses off by one instruction).

* §2.13 labels `2816A SEP #30`; `C2/816A` is `PHP` (`08`) and `SEP #30` is at `C2/816B`. Every later
  address in that listing is right.
* Throughout §3 the "writer" PCs are the monitor's **post-instruction** PCs, e.g. `$0F00` writer
  `C2:C6AE` for the `STA $0F00` at `C2/C6AB`; `$0054` writer `C2:EA4A` for the `STA $54` at `C2/EA48`;
  `$0F0D` writer `C2:C840` for the `STA $0F0D` at `C2/C83D`. Correct as tool output, one instruction off
  as a ROM citation. Harmless, but a reader disassembling `C2/C6AE` will not find a store.

---

### AV.2 — WHAT I CONFIRMED (re-derived / re-run, not taken on trust)

**Every opcode-level claim in §2 survived.** I re-disassembled each address and compared instruction by
instruction; I found no address that does not contain what the dossier says.

| § | claim | how confirmed |
|---|---|---|
| 2.1 | `$C2:8292` = per-screen handler-array table, screen 5 -> `C2/C621`, screen 7 -> `C2/C7A6` | bytes `CA 99 29 9C 34 AA 53 B3 35 BF 21 C6 D4 CE A6 C7`; **and** I found its only reader: `C2/826F SEP #30 / LDA $68 / ASL / TAY / LDA $67 / ASL / TAX / LDA $C28292,X / STA $16 / LDA [$16],Y / JMP ($0000)` (scan for `BF 92 82 C2` -> one hit, `C2/827F`) |
| 2.1 | `C2/C621` substate table `27 C6 5D C6 B9 C6` | raw bytes OK |
| 2.1 | `C2/808F` mode table `0E 00 0C 0D 0F 07 0B`, kind 5 -> screen `$07` | bytes OK; **and** its reader `C2/8076 LDX $0A00 / LDA $C2808F,X / STA $67` (scan `BF 8F 80 C2` -> one hit). Bonus: `C2/806B LDA $0A01 / STA $0071` is *why* screen 7 gets `$0D36` from the request — `C0/19C7 LDX #0000` => `$71 = 0` => `$0D36 = 0` => two columns |
| 2.2 | `C2/CD2B` in full, incl. the unreachable `2CD79 LDA #40` | disassembled, identical (but see **D3** for what §5.1 does with it) |
| 2.2 | `$0D36` writers = **only** `C2/989E` and `C2/C7AE` | scans: `8D 36 0D` -> exactly `C2/989E`, `C2/C7AE`; `9C 36 0D` -> 0; `8F 36 0D 7E` -> 0. `C2/989B LDA #FF / C2/989E STA $0D36` OK |
| 2.2 | callers of `C2/CD2B` = `C2/CA2A`, `C2/C77C`; of `C2/CC4F` = `C2/CA3F`, `C2/CB0D` | bank-filtered scans OK (JSL forms: 0 hits) |
| 2.2 | `$FF:F9BB` = `80 40 20 10 08 04 02 01 00` | `FF/F9B8: 28 2B 6B 80 40 20 10 08 04 02 01 00 ...` OK |
| 2.3 | `C2/C627` gate + `C2/C649` skip loop (`AND #C0` = locked **and** empty) | OK byte for byte |
| 2.4 | `C2/C65D` / `C2/C6B9` idles; `BMI`=A `BVS`=B off `$0D1D` | OK; and `C2/E984` builds `$0D1D` bit7/bit6 from `$2993,Y`/`$2994,Y` via table `C2/E9D9 = 00 01 00 00 ...` — note `C2/E9CD LDA $0D83 / BNE / LDA #40 / TSB $0D1D` is a **second** way bit6 (cancel) gets set |
| 2.5 | `C2/E9E2` mover; `($EA1F,X)` = `EA45 EA40 EA3B EA36`; `$F5` bit3=Up bit2=Dn bit1=Lf bit0=Rt -> graph `+2/+3/+4/+5`; `C2/EA27` = `cell*6`; `$5A` bit7 = SCROLL via `TSB $5A / TRB $54` | decoded from raw `C2/E9E2..EA52` OK |
| 2.5 | graph = 42 bytes at `$FF:B77A`, length-prefixed `2A` | `FF/B77A: [25 ROM bytes at 0x3FB77A: python tools/dis.py 0x3FB77A 25] ...` OK, all 7 cells exactly as tabulated |
| 2.5 | active `handX` patched `08 -> 38` by `C2/CB18` and `C2/CC63` when `$0D36 != 0` | disasm OK **and** live `$7E9380 = 38 23 ...` (read-back below) |
| 2.6 | `C2/C805`, `C2/C85B` (both arms; max `$80 = $0F0C-4`), `C2/C9AE` | OK byte for byte. Tail is `PLP / LDX #FC37 / JMP C2/8385` (raw `28 A2 37 FC 4C 85 83`) |
| 2.6 | reserve window = 4 rows, cells 3..6, ring via `cell3.up = 0x86` / `cell6.dn = 0x83` | from `$FF:B77A` OK |
| 2.7 | `C2/C674`, `C2/C6D0`, `C2/C6DE`, the `C2/C6E7` teardown, `C2/C70C` | OK incl. raw `C2/C696: A2 00 2E 86 61 A2 02 20 86 5D A2 0A 06 86 5F 20 05 EF` = the `C2/EF05` blank |
| 2.7 | `C2/8255` = "STZ `$00B0 + 8*bit` per set mask bit" | disassembled: `STA $00 / LDA #00 / LSR $00 / BCC / TAX / STZ $B0,X / CLC ADC #08 / CMP #40 / BCC` OK |
| 2.7 | `C2/C784` raw + `(col,row)` formula, `$0D36 != 0` => col 9 | raw `[34 ROM bytes at 0x2C784: python tools/dis.py 0x2C784 34]` OK identical |
| 2.8 | `C2/CAF3`; `C2/ED31` interpreter; `$FF:BD18` arg counts; handler table `$ED59` | all three dumped and identical (`$ED59: 77 ED 05 EF AA ED 48 EF 32 F3 64 F3 37 F3 64 F3 78 F3 B0 ED B6 ED`) |
| 2.8 | the `$FF:BF7B` decode, **including** what each opcode does | the dossier asserted op semantics without showing them — so **I disassembled all four handlers**, and every description is right: op08 `C2/F332 = LDX $5B / STX $61`; op00 `C2/ED77` = word-fill by `MVN $7E,$7E` propagate (`$5B`=0x80 => 0x80 bytes); op0C `C2/F337` = DMA setup at `$43x0-7` **and** `$61 = (($5B & 0x00F0) << 3) + 0x9000` (`4330 -> $9180`, `4320 -> $9100` OK); op0E `C2/F364` = length-prefixed `MVN $7E,$FF` copy to `$61` (`LDA $0000,X / DEC A / INX / MVN` => exactly `len` bytes after the prefix => 0x2A bytes of `$FF:B77A` -> `$7E:9380`). The list also **re-aligns exactly** on the `FF` terminator at `$FF:BF9F` |
| 2.9 | `C2/CC4F` both variants (`2E84/100E` vs `2E90/1010`), acts only on `AND #00C0` panels, `+0x1000` = BG2, `+0x180` = next panel | OK byte for byte |
| 2.10 | `C2/C973` / `C2/C997`, pose at `$1811 + charId*0x40` (`XBA / LSR / LSR`) | OK |
| 2.11 | `C2/C7A6` sub-table `AC C7 E0 C7 B9 C6` (shares `C2/C6B9`); `C2/C7AC LDA $71 / STA $0D36`; `C2/C9E5` -> `($CA63,X)` = `CA67 / CAE0` on `$0D36 & 2` | OK all raw-verified |
| 2.11 | `C0/1916 BIT #01`->Y->`C0/1985`; `BIT #40`->X; `C0/198E` storyline `>= 0x49`; `C0/19CC LDA #05 / JSL C28000` | OK |
| 2.12 | `C0/011B` snapshot and `C0/1A03` restore in full, incl. the `8D,8E,8F,`**`91,90`**`,92,93` order and the `$7F1D1B/1C/1D -> $01AB/AC/AD` tail | OK byte for byte. The order note is *doubly* right: `$018D..$0193 = 02 04 06 0A 08 0C 0E`, so `91` before `90` makes the object visits **ascending** (02 04 06 08 0A 0C 0E) — which the `--hookpc` output confirms |
| 2.12 | `C0/421E` writes `$018D..$0193` by charId and `$0197/$0199/$019B` by active slot; `C0/597D` re-runs fn0, zeroes the 8 script words, `$1C00,X = 7` | OK; `C0/597D` has exactly **7** callers, all inside `C0/1A24..1A5A` |
| 2.13 | `C2/816A` compacted index (`$0D5F`/`$0D68`/`$0073`/`$0075`) | OK (see D8 for the one-byte label slip) |
| 5.4 | the SFX sites and that `C2/EAC2`/`C2/EACC` are **cmd `$18`, id `$00`/`$01`** | `C2/EAC2 LDA #00 / JSR C2/EB03`, `C2/EACC LDA #01 / JSR C2/EB03`; `C2/EB03 LDX #18 / STX $1E00 / STA $1E01 / LDA $0D76 / BNE / LDA #03 / STA $0D76 / LDA #80 / STA $1E02 / JSL C70004` OK. **And the table is COMPLETE**: a scan of the whole `C2/C621..C7A5` range finds exactly `JSR C2/EAC2` at `C642, C670, C674, C6CC, C6DE` and `JMP C2/EACC` at `C63A` — six sites, the six listed, no more |

**Every oracle measurement in §4 reproduced**, with the dossier's own frame numbers and writer PCs:

* §4.3 gate-pass on `com_jogo_salvo`: `f42 $0D36=00` (no writer = DMA), `f55 $0D36=FF*(C2:98A1)`,
  `f93 $0F0B$0F0C=0200*(C2:CD54)` + `$0F02=400102808080808080*(C2:CD69)`, `f115 $0067=05*(C2:9A83)`,
  `f116 $0067=05` (stays) + `$0054=01*(C2:C652)` + `$0F00=FFFF*(C2:C65A)` + `$0F0D=01*(C2:C840)`.
* §4.3 refusal on `menu_open_gato2`: `f73 $0067=05*(C2:9A83)` -> `f74 $0067=00*(C2:C638)`,
  `$1E00=0001800A*(C7:0196)`, `$0D76=03*(C2:EB15)`.
* §4.3/§4.5 nav: `f177 $0054=02*(C2:EA4A)`, `f199 $0054=01*(C2:C9E4)` (Down 2->0 locked -> 1),
  `f221 $0054=02*(C2:C9E4)` (Up 1->0 locked -> 2). L/R produce **no** row. B cancels the pick
  (`$0F00=FFFF*(C2:C6F2)`, `$0054=01(C2:C6E5)`), B again exits (`$0067=00*(C2:9A9E)`).
  `$0D17` = `FF -> 01 -> 02 -> 01`, writer `C2:E995`.
* §4.5 "second A on the same member": `f189 7E2980=000204(C2:C725)` — the swap writer fires, the party is
  unchanged.
* §4.6 entry cursor: `$7F = 0` at entry (`C2:9A67`) -> `f116 $007F=01*(C2:C84E)`.
* §4.7 dim table: byte-identical to the dossier's (`ribbon opt0` r3 `00B5/03ED`, `opt4` r3 `10CB/13E9`,
  `Form` r15 `00CB/03E9`).
* §4.8 button bits: L->`$F0=20`, R->`10`, Select->`$F0=02 $F6=04`, **Y->`$F6=01`**, **X->`$F6=40`**.
* §4.8 storyline gate: `--hookpc 1985` -> `f20 visits=1`; `--hookpc 19CE` -> 0. **Strengthened**: I also
  hooked the two intermediate PCs to prove *which* gate blocks — `--hookpc 198E` -> `visits=1` (the
  `C0/1985 LDA $26 / BEQ` pre-gate **passes**, `$0126 = 0`), `--hookpc 1998` -> **0** (the
  `SBC #49 / BCS` is the actual refusal). `$7F0000 = 0x15`.
* §4.9 reconcile on `grupo_3_mapa`: `f251 $2980=000402*(C2:C725)`, `f313 $0067=00*(C2:9A9E)`,
  `f416 $0194=000402*(C0:1A6F)` + `$0197=020008000600*(C0:4329)`; positions
  `f383 $1800=00FF502E00FF612D712C00FF*(C0:0914)` -> `f416 ...00FF712C612D00FF*(C0:1A82)`;
  `$7F1D09 = 502E 612D 712C | 002C x3 | 2020 x3 | 25 16 07 00` throughout.
  `--hookpc 597D` with the swap: `f416 visits=7 $6D: 02 04 06 08 0A 0C 0E`; without the swap: **0**.
  WARNING: the §4.9 script needs **two** `01:2` (B) presses — the first only leaves Form for the ribbon;
  the reconcile fires on the second, which closes the menu. A one-B script gives a silent false negative
  (I produced one before spotting it).
* §4.4 the central identity: **BG1 diff = 0, BG2 diff = 96 tiles, rows 12..17, cols 8..23** — reproduced
  exactly (and the full 32x64 map, not just the visible 28 rows, also differs by only those 96).
* §4.4 PPU: `mode=1`, BG1 map `$5800`, BG2 map `$6000`, both `higher=1` (32x64), `vS=1023`,
  `objSize=0`, OBJ tiles `$4000`/`$5000`.
* §4.4 WRAM read-back — every line identical: graph `38 23 02 01 03 03 | 38 53 ... | E8 B3 05 83 02 02`;
  `$0F02..$0F0D = 40 01 02 80 80 80 80 80 80 | 02 00 | 01`; poses `00 00 01 00 00 00 00` (cursor on
  Lucca), `00 00 00 00 01 00 00` (cursor on Frog), `00 00 0B 00 00 00 00` (Lucca picked);
  `$0051=0000 $0054=01 $007F..81=01 00 01 $0D36=FF`; `$0D5F..= 00 02 04 01 FF...`, `$0D68..= 00 01 02`,
  `$0073=03 $0075=04`.
* §4.4 OAM: `obj028 x=56 y=83 tile=020 pal=0 prio=3 big=1`; Crono `obj064..067` at x 157/160,
  y 8/24/40/56 tiles 080-086 pal 1; Lucca `obj072..075` pal 3; Frog `obj080..083` pal 5.
  Hand geometry: `$7E9380[cell*6]` = `(0x38, 0x23/0x53/0x83)` = **(56, 35 + 48*slot)**.
* §4.4 BG1 text: I decoded the tilemap independently and got, character for character,
  `Crono LV 3 / HP  25/ 96 / MP   2/ 12 / {020} 11 {02E} 19` (rows 3-6, palette **4**),
  `Lucca LV 3 / HP  40/ 88 / MP  14/ 16 / {022}  8 {02E} 20` (rows 9-12, palette 0),
  `Frog  LV 5 / HP 128/128 / MP  17/ 17 / {024} 18 {02E} 38` (rows 15-18, palette 0);
  Exchange icon `{178}{179}` / `{184}{185}` at rows 22-23 cols 11-12 palette 6, label `Exchange` at
  row 23 cols 13-20. Every geometry number in §4.4's tables checks out.

**§5.0's claims about the C engine also hold** (verified against `src/`, read-only):

* `ctMenuChromeEnteredTM[4]` (selected by `CtMenuChrome`'s `si = screen - 1`, `src/menu.c:166`) is
  **0 differing tiles out of 896** against the oracle's Form BG2 tilemap — I parsed the header and
  compared. Against the *ribbon opt-4* BG2 it differs by exactly the same 96 tiles in rows 12-17.
* The engine cursor really does skip only `& 0x80` and not the lock bit: `src/field.c:8521`
  `if (!(s7[0x2980 + menuFormSlot] & 0x80)) break;` and `src/field.c:8276`. §5.2's correction is needed.
* The invented second hand is real: `src/menu.c:1504-1506` `if (picked >= 0 ...) mHandCursor(h, fb,
  MP_X + MP_W - 4, ...)`; the lead-bright dim is real: `src/menu.c:1494` `mPartyPanels(h, fb, 24,
  h->mem[0x2980] & 7)`. Both are correctly called invention by §5.5.
* The refusal path (`src/field.c:8267-8274`) matches `C2/C636-C63A`.
* The "slide-in" wording is still in `src/menu.h:290-299` and `src/menu.c:1486/1492`, and
  `CtMenuFormRoster`'s comment (`src/menu.c:664`) already names the refusal correctly — exactly as the
  frontmatter says.

---

### AV.3 — OPEN ITEMS: re-checked, and one strengthened

* **§6.1 (the reserve screen cannot be captured) — CONFIRMED and strengthened.** I swept **all 81**
  `.state` files in `saves/`, `saves/battle_re/`, `saves/field_re/`, `saves/forest_re/`: the maximum
  `$7F0000` anywhere in the repo is **`0x15`**, and only seven states have three active members. The
  `>= 0x49` gate can never pass. I additionally pinned that the storyline compare — not the `$0126`
  pre-gate at `C0/1985` — is what refuses (`--hookpc 198E` visits=1, `--hookpc 1998` visits=0), so the
  named unblocking capture is exactly the right one.
* **§6.2 (the menu sprite animator is not derived) — CONFIRMED, and it is bigger than stated.** It is
  what invalidates D2's "pixel-identical": the OBJ tile block at word `$4000` changes by 110-191 bytes
  between Form frames and OAM alternates between two states. Any future "the Form render is byte-exact"
  claim must be scoped to **BG1+BG2 only** until this is done.
* **§6.7 (L/R inertness is state-dependent) — CONFIRMED**, and see **D7** for the mechanism the note
  should carry.
* §6.3 / §6.4 / §6.5 / §6.6 — I did not attack these; they are honestly booked as underived.

**What I did NOT verify** (so nobody reads this section as blanket approval): `C2/C8BA` (the smooth-scroll
animation), `C2/CB8B`, `C2/CDE3`, `C2/CE86`, `C2/CCB3`, `C2/EC93`, `C2/F378`, `C2/EF05`, `C2/8A31`,
`C2/9B54/9B71`, and the redraw command lists `$FF:FBE3` / `$FF:FC37` / `$FF:FBFF` — all named in §2 but
described only in one-line comments there too. None is load-bearing for §5's spec. The entire screen-7 /
reserve half of §2.6, §2.11 and §5.7 remains **disassembly-only**, unmeasurable on this repo's captures,
and D3 shows one of its index claims (`$0F05..$0F0A`) is already imprecise.

### AV.4 — SUGGESTED FIXES, in priority order

1. **Delete `CD/B508 / CD/B51C / CD/B530`** from §2.12 and §5.6 (D1). Re-word §5.6's "do not scope it"
   to rest on `C0/19E6` only.
2. **Rewrite §5.1's pseudo-code with the ROM's two cursors** (D3), and correct §5.7's `$0F05..$0F0A`
   phrasing to "reserves always begin at panel **index 3** (`$0F05`), independent of the active count".
   Cite `C0/3A07` (op `0xD6`) as the proof that a mid-party hole is reachable.
3. **Restate D2's claim**: BG1/BG2 tilemaps settled at +2 and stable to +40 (that is the result §5.5
   needs); the framebuffer is not pixel-stable because the character sprites animate (§6.2).
4. **Amend §1/§5.5** to say the ribbon's 8 icon/arrow OBJ sprites are removed too (D4).
5. Add `saves/eq_spare.state` to §4.1 and change "five" to "six" (D5).
6. Resolve §5.0 vs §5.2 on `CtMenuFormSwap`'s 0..2 clamp (D6) — either lift the clamp with the ROM's
   uniform indexing, or stop calling it "already correct".
7. Correct §5.2's re-drive rule to `UP iff $5A & 0x08, else DOWN, re-read live` and note that `C2/C805`
   runs every frame (D7).
8. §5.8's three suggested checks are all sound and all **event-anchored**, not frame-anchored — keep
   them. Add a fourth from D3: `menuform-panel-compaction` — with `$2980 = {0x80, 0x02, 0x04}` the
   builder must yield `panel = {0x01, 0x02, 0x80, ...}`, **not** `{0x80, 0x01, 0x02, ...}`. It is the one
   check that would have caught the divergence, and it needs no capture.

## PORTED 2026-09-24: the exchange on the cartridge's frames (tools/menu_audit_cmp.py formswap = 0 frames differ)

* **C2/C973 has one caller**, C2/C840, inside the cursor mover C2/C805 -- which runs on the frames without A or B
  (C2/C65D / C2/C6B9) and once in the build (C2/CAF3 -> C2/CB74). So the sprites' requests (the cursor's member
  walking, the picked one on anim 0B) change the frame AFTER an A: the port used to re-issue them every frame.
* **The confirm's tail C2/C6E7** does not go through C2/C973: `STZ $1811` for all seven blocks (everyone still) and
  kills slot 6; the next frame's cursor mover starts the walk again.
* **The pick's hand** is task slot 6 = C2/8A45 (spawned by C2/C6B0 JMP C2/8A31) on C2/8B6E's counters `$0DA3/$0DA0`,
  drawn while `$0DA0` bit 0 is CLEAR (the Item's pinned hand, C2/89F2, draws while it is set).
* **The sprite budget `$0D75` is the screen's**: 2 from the Form tab's builder (C2/CA39), so two blocks take new poses
  per frame (each upload adds the block's `$1802` = 01 to `$0D73`, 2F820). MEASURED on the swap frame: `$0D73` = 02,
  Lucca's and Frog's new poses both in it. The port had assumed one a frame (a misreading of 2F826 as 0x20).

---

## 2.14 SCREEN $07 PORTED 2026-10-14: the party exchange with reserves (the Y button, the script's `C8 00`)

Roadmap G1a item 2: without it a player could never bring a reserve member into the party, and the four scripts that ask
for the exchange (scenes 08D, 113, 127 and 1D0 -- twelve `C8 00` sites) went on without one.

**How it is reached.** The field's Y: C0/1916 `LDA $00F6 / BIT #01` -> C0/1985 (a window up: flip it, `$26 EOR 3`) ->
C0/198E gates (storyline `$7F0000` >= 0x49, `$011F` != 0, `$0162` = 0, `$0110` = 0) -> C0/19A7 the X path's own fade (`$0407 =
FF`, `DEC $19` / FD/C2C1 / C0/EC60) -> C0/19C1 JSR C0/0B4E, JSR C0/011B -> C0/19C7 `LDX #0000 / TDC / XBA / LDA #05 / JSL
C2/8000`. The script's `C8 00`: C0/3757 parks it (`$0117` bit 6), C0/0CC7's fade (the shop's: C0/881E, FD/FFF7), C0/0CEA's
save, C0/0CF4 `BEQ ->0D04` `LDA #05 / BRL C0/19C7`. Both return through C0/19D2 -- C0/01A5, the party reconcile C0/1A03, and
`$0117` bit 6 -> `$0118` bit 6 (C0/19EB-19F7: the op's handshake) -- then C0/B192 and C0/2824.

**The screen** (C2/808F[5] = $07, C2/C7A6: substates C7AC / C7E0 / C6B9). C2/C7AC: `$0D36 = $0D37 = $71` (0), C2/CB8B (the song
task below), C2/C9E5 (the two-column list $FF:BF05), C2/C649 from X = 0, C2/CAF3 (the build, its C2/C805), C2/CE86, C2/C7CB
waits for the song task (`$00C0`), `$9689 = 17`, C2/CE9D, C2/83B2's fade-in. C2/C7E0: A -> C2/C7F7 (`BIT $0F0D / BVS`: a
locked entry is refused, silently) -> C2/C674; B -> C2/C7FF `JSR C2/CC0E / JMP C2/82B2` (no ribbon in mode 5: the menu
closes); else C2/C805 and the blip. C2/C6B9 is the Form's. With $0D36 = 0, C2/CD2B scans the reserves (from $0F05, compacted,
never locked: $00 = 0 on this entry), C2/C784 puts panels in columns 3 / 0x13, C2/CDE3 the members at C2/CE6E[0 + 2r] (x 116)
and [6 + 2k] (x 140), and the graph $FF:B77A keeps its hand X 08 / E8. C2/9875's head (C2/9895-98B2): `$0D32 = $0D34 = 0`,
`$0D36 = FFFF`, a field map -> `$0D32 = FFFF` (and `$0D34` off a save point).

**The song.** C2/CB8B: unless `$7F01CF` bit 6, `$0F0E` = the song playing (`$29AE` when `$0D32`, else `$02AE`), and if it is not
$3E, `$0F0F = 1` and the task C2/CBBF: `$2141` = 0 (a yield a frame), `14 3E 80 80`, `81 01 FF FF`, the `$2143 & 0F` spin,
C2/8216. Its JSLs hold the CPU, so substate 0's two scheduler scans (C2/C649 at S0 + 2 without the song) come after them.
The close C2/CC0E: `81 08 01 01`, C2/83CA's fade, `$2141` = 0, `11 <$0F0E> 80 80`, `81 10 xx xx` (xx = `$04FB`, or FF).

**MEASURED** (the rule's cartridge, the recipe gato_wait with `7F0000=49, 2982=02, 2983=03, 0126=00`; Y f20, Right f124, A f146,
Left f168, A f190, B f232): `$0119` 0E..00 f21-35, `$0A00 = 05` f36, `$0067 = 07` f51, `$0F02 = 40 01 02 03 80..` f52 (C2:CD8C),
`14 3E` posted f52 and consumed f84, `$54 = 01` f86 (C2:C652), `$0D10` 02..0F f88-95, Right -> `$54 = 03` f125, A -> `$0F00 =
03` f147, Left -> `$54 = 01` f169, A -> `$2980 = 00 03 02 01` f191 (C2:C725), B -> `81 08` f233, `$0D10` 0D..00 f233-240, `11 42`
f250 (`$2141` busy f240-249), `81 10 FF FF` f278 (C2:CC09), C0/0905 f281, `$0119` 01..0F f305-319. With the song suppressed
(`$7F01CF` bit 6): `$54` f54, `$0D10 = 02` f56.

**The port** (src/menu.c CtMenuExch*, CtMenuOpenFlags, CtMenuRenderExchange; src/field.c the Y and `C8 00` launchers, the
song task, the close; src/ct_vm.c CtSceneExchange): `tools/menu_exchange_cmp.py` -- every byte above, frame for frame, 0
differing over 2267 frame-fields (check menu-exchange-vs-oracle). On the way: the menu fades now write `$0119` (C0/1949 /
C0/0CD4 / C0/19B2 `DEC $19`, C0/2829 `INC $19`), and C2/9875's head runs at every open.

**OPEN.** (1) The picture's three layers not drawn: the BG3 wave behind the windows (HDMA channel 4 on `$2111` from
`$7E:922C`), the prompt's text in the bottom box, and the hooded figure in it (C2/CE9D's object, `$FF:D57A` -> `$0F12`). The
rest is the cartridge's layout (the captured chrome `ctMenuChromeExchTM`, C2/C784's panels, C2/CE6E's members, the graph's
hand) but has not been graded pixel by pixel. (2) The script's `C8 00` arm is the same screen through the same launcher
code, but no state reaches one of its twelve sites: ungraded. (3) A song post a busy driver holds past S0 + 2 is modelled
as moving P; the cartridge would run C2/C649 first and hold only the fade-in (C2/C7CB) -- not measured. (4) The overworld's
own poll (C2/2552: `$027C == 5 ? 1 : 5`) is not this launcher and is not ported.
