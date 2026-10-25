# CONFIG screen (menu screen 4) — the TOGGLE machine, byte-derived

Gap: *"the CONFIG screen is read-only — no setting can be changed."*
Scope: the cell→handler dispatch, every setting's arithmetic, the cursor/nav graph, the APU side
effects, persistence, and the gameplay readers.

Everything below is either a ROM address I disassembled (`tools/dis.py`) or an oracle measurement I
ran (command + real output pasted in §4). Anything I could not pin is in §6.

---

## 1. VERDICT

Screen 4 is a **21-cell 2-D cursor machine over 12 settings packed into three saved bytes.**
`$7E:0054` is the cell id. Every frame, substate 1 (`C2/BF45`) calls `C2/E984` (which first runs the
cursor mover `C2/E9E2`) and then `C2/BFD4`, which does `LDA $54 / STA $0417 / ASL A / TAX /
JSR ($BFFF,X)` — a **12-entry indirect jump table at `C2/BFFF`**, one routine per cell 0-11. Each cell
routine tests `$0D1D` bit7 (the A press, decoded from the *configured* Confirm button) and, if set,
mutates its own byte in the working block `$7E:0D86..$0D91`. Nine of the twelve cells are `INC A /
AND #01` two-state toggles; `$0D8B`/`$0D8C`/`$0D90` are 8-step wrapping cycles driven by LEFT/RIGHT
through the shared routine `C2/C164` (`AND #07`); `$0D8A` is a 3-option row reached through a
sub-cursor (substate 3); `$0D87` (Control Pad) opens a whole second editor (substate 2) over the
9-byte button map `$0408..$0410`. The cursor graph is **not** computed: it is a 21×6-byte table
copied verbatim from ROM `FF/B5A0` into `$7E:9380` (`[x, y, up, down, left, right]` per cell).
**Nothing is written to the save while you are on the screen**: pressing **B** (the screen's *only*
exit — X/L/R/Select/Start are inert, measured) calls `C2/934F`, which bit-packs the twelve bytes into
`$7E:2990/$2991/$2992`, and those three bytes sit inside save record 0 (`$7E:2400 + 0x0590..0x0592`).
The reverse unpack `C2/92F4` runs once per menu-open from `C2/8133`.

**What the C engine must do:** give `CtMenuRenderConfig`'s cursor the real 21-cell nav table
(`FF/B5A0`), dispatch A through the `C2/BFFF` cell table into per-cell toggles that write
`$7E:0D86..$0D90` exactly as §2.3-2.6 specify, keep the `$0F09..$0F12` mirror in step (three of its
bytes double as the sub-mode flags), post APU cmd `$F3` only for cell 0, blip on every A/every cursor
move, and on B run the `C2/934F` pack into `$2990-92` before returning to the ribbon.

---

## 2. ROM TRACE

### 2.1 Screen-4 entry: the substate handler array `C2/BF35`

`C2/8292` (screen→handler-array table, already pinned in `menu_phase2.md`) gives screen 4 =
`C2/BF35`. Raw bytes:

```
C2/BF35: 3D BF  45 BF  6D BF  9F BF     -> 4 substates
```

| `$68` | handler | role |
|---|---|---|
| 0 | `C2/BF3D` | `JSR C2/EAC2` (blip) / `INC $68` / `JMP C2/C3B5` — build the screen |
| 1 | `C2/BF45` | the idle loop (grid cells 0-11) |
| 2 | `C2/BF6D` | the **Control-Pad button editor** (cells 15-20) |
| 3 | `C2/BF9F` | the **Battle-Gauge 3-option row** (cells 12-14) |

```
C2/BF3D: 20 C2 EA   JSR C2/EAC2
C2/BF40: E6 68      INC $68
C2/BF42: 4C B5 C3   JMP C2/C3B5

C2/BF45: A9 5D      LDA #$5D
C2/BF47: 8D 13 0D   STA $0D13          ; cursor-sprite id for this screen
C2/BF4A: 20 84 E9   JSR C2/E984        ; (runs C2/E9E2 = the cursor mover) -> $0D1D
C2/BF4D: 2C 1D 0D   BIT $0D1D
C2/BF50: 70 0D      BVS ->BF5F         ; bit6 = B (Cancel)
C2/BF52: 20 D4 BF   JSR C2/BFD4        ; per-cell dispatch
C2/BF55: A5 81      LDA $81            ; $81 = the cursor BEFORE the mover ran
C2/BF57: C5 54      CMP $54
C2/BF59: F0 03      BEQ ->BF5E
C2/BF5B: 20 C2 EA   JSR C2/EAC2        ; blip when the cursor MOVED
C2/BF5E: 60         RTS
C2/BF5F: AD 0E 0F   LDA $0F0E          ; B pressed: are we inside a sub-mode?
C2/BF62: 0D 0F 0F   ORA $0F0F
C2/BF65: D0 EB      BNE ->BF52         ; yes -> let the CELL handler eat the B
C2/BF67: 20 4F 93   JSR C2/934F        ; no  -> PACK the config into $2990-92
C2/BF6A: 4C 98 9A   JMP C2/9A98        ; ... and return to the ribbon
```

Only **A** (`$0D1D` bit7) and **B** (bit6) are decoded. `$0D1D` bits 0/1 (the L/R page bits E984
also builds) are never tested on this screen — measured in §4.7: X, Select, Start, L and R do
nothing at all, and the menu cannot be closed from screen 4.

### 2.2 The per-cell dispatcher `C2/BFD4` and the cell→handler table `C2/BFFF`

```
C2/BFD4: 08         PHP
C2/BFD5: E2 20      SEP #$20
C2/BFD7: 7B         TDC
C2/BFD8: A5 54      LDA $54            ; cursor cell
C2/BFDA: 8D 17 04   STA $0417          ; $0417 = "the cell the bottom bar describes"
C2/BFDD: 0A         ASL A
C2/BFDE: AA         TAX
C2/BFDF: 08         PHP
C2/BFE0: FC FF BF   JSR ($BFFF,X)      ; <<< THE CELL -> HANDLER TABLE
C2/BFE3: 28         PLP
C2/BFE4: AD 17 04   LDA $0417
C2/BFE7: 85 7F      STA $7F            ; $7F = the cell as of the PREVIOUS frame's dispatch
C2/BFE9: 20 BC C4   JSR C2/C4BC        ; recompute the bottom bar ($0DCE/$0DCF)
C2/BFEC: 20 E4 C3   JSR C2/C3E4        ; redraw the 10 checkboxes
C2/BFEF: A2 E3 FB   LDX #$FBE3
C2/BFF2: 20 85 83   JSR C2/8385        ; queue the tilemap upload
C2/BFF5: AD 1D 0D   LDA $0D1D
C2/BFF8: 10 03      BPL ->BFFD
C2/BFFA: 20 C2 EA   JSR C2/EAC2        ; blip on EVERY A press, whatever the cell did
C2/BFFD: 28         PLP
C2/BFFE: 60         RTS
```

`C2/BFFF` (raw, 24 bytes — the handler block starts at `C2/C017` immediately after it):

```
C2/BFFF: 17 C0  6E C0  38 C0  4A C0  5C C0  A7 C0
C2/C00B: 2C C1  80 C0  E9 C0  95 C0  B9 C0  D1 C0
```

| cell | handler | cell | handler |
|---|---|---|---|
| 0 | `C2/C017` | 6 | `C2/C12C` |
| 1 | `C2/C06E` | 7 | `C2/C080` |
| 2 | `C2/C038` | 8 | `C2/C0E9` |
| 3 | `C2/C04A` | 9 | `C2/C095` |
| 4 | `C2/C05C` | 10 | `C2/C0B9` |
| 5 | `C2/C0A7` | 11 | `C2/C0D1` |

The dispatch is unconditional (every frame), so a handler that wants to react to *no* button still
runs — that is how cell 8's RIGHT branch and cells 6/10/11's LEFT/RIGHT cycles work.
`$54 <= 11` is guaranteed while substate == 1, so the 12-entry table is never over-indexed;
cells 12-20 exist only inside substates 3 and 2, which never call `C2/BFD4`.

### 2.3 The two-state toggles (cells 0, 1, 2, 3, 4, 9)

All five share one shape. Cell 0 additionally posts to the APU.

```
C2/C017: AD 1D 0D   LDA $0D1D
C2/C01A: 10 1B      BPL ->C037          ; no A -> nothing
C2/C01C: AD 86 0D   LDA $0D86
C2/C01F: 1A         INC A
C2/C020: 29 01      AND #$01            ; two-state, wraps 1->0
C2/C022: 8D 86 0D   STA $0D86
C2/C025: 8D 09 0F   STA $0F09           ; the checkbox mirror
C2/C028: A9 F3      LDA #$F3            ; APU command $F3 = STEREO
C2/C02A: 8D 00 1E   STA $1E00
C2/C02D: AD 86 0D   LDA $0D86
C2/C030: 8D 01 1E   STA $1E01           ; parameter = 0 (On/stereo) or 1 (Off/mono)
C2/C033: 22 04 00 C7 JSL $C70004        ; immediate dispatch (not throttled)
C2/C037: 60         RTS
```

| cell | handler | byte | mirror | arithmetic |
|---|---|---|---|---|
| 0 | `C2/C017` | `$0D86` Stereo | `$0F09` | `INC A / AND #01` + APU `$F3` |
| 1 | `C2/C06E` | `$0D89` Battle Mode | `$0F0C` | `INC A / AND #01` |
| 2 | `C2/C038` | `$0D88` Save Menu Cursor | `$0F0B` | `INC A / AND #01` |
| 3 | `C2/C04A` | `$0D8D` Save Battle Cursor | `$0F10` | `INC A / AND #01` |
| 4 | `C2/C05C` | `$0D8E` Save Skill/Item Cursor | `$0F11` | `INC A / AND #01` |
| 9 | `C2/C095` | `$0D8F` Skill/Item Info. | `$0F12` | `INC A / AND #01` |

All six wrap 0↔1. There is no bound check beyond `AND #01`; a corrupt value >1 self-heals on the
first press.

### 2.4 The 8-step cycles: cells 5→10/11 and cell 6, through `C2/C164`

`C2/C164` is the shared "cycle a byte 0..7 with LEFT/RIGHT" routine. **X is 16-bit here** (the menu
runs with `REP #$10`), and it is the *address* of the byte to cycle:

```
C2/C164: 7B         TDC
C2/C165: BD 00 00   LDA $0000,X        ; X = &byte
C2/C168: A8         TAY
C2/C169: A5 5A      LDA $5A            ; $5A = this frame's NEW direction bits ( = $F5 )
C2/C16B: 29 03      AND #$03           ; bit0 = RIGHT, bit1 = LEFT
C2/C16D: F0 13      BEQ ->C182         ; neither -> nothing (UP/DOWN fall through to the nav table)
C2/C16F: 29 01      AND #$01
C2/C171: D0 02      BNE ->C175         ; RIGHT -> +1
C2/C173: 88 88      DEY / DEY          ; LEFT  -> -1 (DEY DEY then INY)
C2/C175: C8         INY
C2/C176: 98         TYA
C2/C177: 29 07      AND #$07           ; 8 values, WRAPS both ways
C2/C179: 9D 00 00   STA $0000,X
C2/C17C: 20 56 C4   JSR C2/C456        ; redraw the three 8-slot strips
C2/C17F: 20 C2 EA   JSR C2/EAC2        ; blip
C2/C182: 60         RTS
```

**Cell 5 ("Battle Speed") is a group header, not a setting.** Its A press moves the cursor onto the
right-hand panel:

```
C2/C0A7: AD 1D 0D   LDA $0D1D
C2/C0AA: 10 0C      BPL ->C0B8
C2/C0AC: A9 0A      LDA #$0A
C2/C0AE: 85 54      STA $54            ; cursor -> cell 10
C2/C0B0: 8D 17 04   STA $0417
C2/C0B3: A9 01      LDA #$01
C2/C0B5: 8D 0E 0F   STA $0F0E          ; sub-mode flag (also checkbox mirror slot 5)
C2/C0B8: 60         RTS
```

Cells 10 and 11 then cycle their bytes; **A *or* B leaves** (they test `AND #$C0`, i.e. either bit):

```
C2/C0B9: A2 8B 0D   LDX #$0D8B         ; Battle Gauge Speed
C2/C0BC: 20 64 C1   JSR C2/C164
C2/C0BF: AD 1D 0D   LDA $0D1D
C2/C0C2: 29 C0      AND #$C0           ; A or B
C2/C0C4: F0 0A      BEQ ->C0D0
C2/C0C6: A9 05      LDA #$05
C2/C0C8: 85 54      STA $54            ; back to cell 5
C2/C0CA: 8D 17 04   STA $0417
C2/C0CD: 9C 0E 0F   STZ $0F0E
C2/C0D0: 60         RTS

C2/C0D1: A2 90 0D   LDX #$0D90         ; Battle Mes. Speed — identical body
C2/C0D4: 20 64 C1   JSR C2/C164
  ... AND #$C0 ... LDA #$05 / STA $54 / STA $0417 / STZ $0F0E / RTS
```

While the cursor is on 10/11 the nav table still works, so **UP/DOWN swap between the two strips and
LEFT/RIGHT change the value** (`C2/C164` ignores UP/DOWN because `$5A & 3 == 0`). Measured in §4.4.

**Cell 6 (Window Color)** edits in place instead, and *locks the cursor* while it does:

```
C2/C12C: AD 0F 0F   LDA $0F0F
C2/C12F: F0 0F      BEQ ->C140         ; not in edit mode -> skip the cycle
C2/C131: A2 8C 0D   LDX #$0D8C
C2/C134: 20 64 C1   JSR C2/C164
C2/C137: A2 10 00   LDX #$0010
C2/C13A: 8E 47 0D   STX $0D47
C2/C13D: 20 11 C5   JSR C2/C511        ; reload the window palette/graphics for the new colour
C2/C140: AD 1D 0D   LDA $0D1D
C2/C143: 29 C0      AND #$C0           ; A or B toggles the edit mode
C2/C145: F0 16      BEQ ->C15D
C2/C147: A2 38 3B   LDX #$3B38         ; cursor (x,y) = (0x38,0x3B) = the grid slot
C2/C14A: AD 0F 0F   LDA $0F0F
C2/C14D: 49 01      EOR #$01
C2/C14F: 8D 0F 0F   STA $0F0F
C2/C152: F0 03      BEQ ->C157
C2/C154: A2 88 37   LDX #$3788         ; entering edit: cursor (x,y) = (0x88,0x37) = beside the strip
C2/C157: 8E A4 93   STX $93A4          ; <<< cell 6's OWN nav-table (x,y) pair, $9380 + 6*6
C2/C15A: 20 C2 EA   JSR C2/EAC2
C2/C15D: AD 0F 0F   LDA $0F0F
C2/C160: 8D 1F 0D   STA $0D1F          ; $0D1F != 0 -> C2/E9E2 refuses to move the cursor
C2/C163: 60         RTS
```

Note `STX $93A4`: the ROM *rewrites its own nav record* for cell 6 so the hand jumps to the right
panel while editing and back to the grid when done. This is the proof that the first two bytes of a
nav record are the cursor's screen (x, y) — see §2.7.

### 2.5 Cell 7 → substate 3: the 3-option Battle-Gauge row (cells 12-14)

```
C2/C080: AD 1D 0D   LDA $0D1D
C2/C083: 10 0F      BPL ->C094
C2/C085: AD 8A 0D   LDA $0D8A
C2/C088: 18         CLC
C2/C089: 69 0C      ADC #$0C
C2/C08B: 85 54      STA $54            ; cursor -> cell 12 + current value
C2/C08D: EE 0D 0F   INC $0F0D          ; sub-mode flag (also checkbox mirror slot 4)
C2/C090: A9 03      LDA #$03
C2/C092: 85 68      STA $68            ; substate 3
C2/C094: 60         RTS
```

```
C2/BF9F: 20 84 E9   JSR C2/E984        ; the nav mover walks cells 12<->13<->14 (circular)
C2/BFA2: AD 1D 0D   LDA $0D1D
C2/BFA5: 29 C0      AND #$C0
C2/BFA7: D0 1C      BNE ->BFC5         ; A or B -> leave
C2/BFA9: A5 54      LDA $54
C2/BFAB: C5 81      CMP $81
C2/BFAD: F0 15      BEQ ->BFC4         ; cursor didn't move -> nothing
C2/BFAF: 38         SEC
C2/BFB0: E9 0C      SBC #$0C
C2/BFB2: 8D 8A 0D   STA $0D8A          ; <<< THE VALUE IS THE CURSOR CELL - 12
C2/BFB5: 20 BC C4   JSR C2/C4BC
C2/BFB8: 20 95 C4   JSR C2/C495        ; redraw the Off/1/2 row
C2/BFBB: A2 E3 FB   LDX #$FBE3 / JSR C2/8385
C2/BFC1: 20 C2 EA   JSR C2/EAC2
C2/BFC4: 60         RTS
C2/BFC5: 9C 0D 0F   STZ $0F0D
C2/BFC8: AD 17 04   LDA $0417
C2/BFCB: 85 54      STA $54            ; $0417 is still 7 -> back to cell 7
C2/BFCD: A9 01      LDA #$01
C2/BFCF: 85 68      STA $68            ; substate 1
C2/BFD1: 4C C2 EA   JMP C2/EAC2
```

The legal range is therefore **0..2** (three cells in the nav table), not `AND #3`: `C2/C495`'s
`AND #0003` never sees 3 because the cursor can never reach cell 15 from here. Measured §4.5.

### 2.6 Cell 8 → substate 2: the Control-Pad button editor (cells 15-20)

```
C2/C0E9: AD 1D 0D   LDA $0D1D
C2/C0EC: 30 1A      BMI ->C108          ; A pressed
C2/C0EE: A5 7F      LDA $7F             ; else: were we on cell 8 last frame?
C2/C0F0: C9 08      CMP #$08
C2/C0F2: D0 34      BNE ->C128
C2/C0F4: A5 5A      LDA $5A
C2/C0F6: 29 01      AND #$01            ; RIGHT
C2/C0F8: F0 2E      BEQ ->C128
C2/C0FA: AD 87 0D   LDA $0D87
C2/C0FD: D0 17      BNE ->C116          ; already Custom -> open the editor (no toggle)
C2/C0FF: A9 03      LDA #$03
C2/C101: 85 54      STA $54             ; Standard: RIGHT jumps the cursor to cell 3
C2/C103: 8D 17 04   STA $0417
C2/C106: 80 20      BRA ->C128
C2/C108: AD 87 0D   LDA $0D87
C2/C10B: 1A         INC A
C2/C10C: 29 01      AND #$01
C2/C10E: 8D 87 0D   STA $0D87           ; Standard(0) <-> Custom(1)
C2/C111: 8D 0A 0F   STA $0F0A
C2/C114: F0 12      BEQ ->C128          ; became Standard -> just rebuild the live map
C2/C116: A9 0F      LDA #$0F
C2/C118: 85 54      STA $54             ; cursor -> cell 15 (button row 0)
C2/C11A: E6 68      INC $68             ; substate 2
C2/C11C: 7B         TDC
C2/C11D: A2 08 04   LDX #$0408
C2/C120: A0 00 0F   LDY #$0F00
C2/C123: A9 08      LDA #$08
C2/C125: 54 7E 7E   MVN $7E,$7E         ; live map $0408..$0410 -> working copy $0F00..$0F08
C2/C128: 20 4F C5   JSR C2/C54F         ; rebuild the LIVE map $2993 + the button labels
C2/C12B: 60         RTS
```

`C2/C54F` is what actually makes the setting bite:

```
C2/C555: PHP / REP #30
         LDX #$FCA9 / LDY #$2993 / LDA #$0008 / MVN $7E,$C2   ; ROM defaults -> $2993..$299B
         LDA $0D87 / AND #$00FF / BEQ done
         LDX #$0408 / LDY #$2993 / LDA #$0008 / MVN $7E,$7E   ; Custom: $0408..$0410 -> $2993..$299B
```

Substate 2 (`C2/BF6D` + `C2/C183`):

```
C2/BF6D: 20 84 E9   JSR C2/E984
C2/BF70: A5 5A      LDA $5A
C2/BF72: 29 02      AND #$02            ; LEFT
C2/BF74: F0 0E      BEQ ->BF84
C2/BF76: 7B / A2 00 0F / A0 08 04 / A9 08 / 54 7E 7E          ; commit $0F00 -> $0408
C2/BF82: 80 08      BRA ->BF8C
C2/BF84: 2C 1D 0D   BIT $0D1D
C2/BF87: 70 03      BVS ->BF8C          ; B also commits + exits
C2/BF89: 4C 83 C1   JMP C2/C183
C2/BF8C: 20 C2 EA   JSR C2/EAC2
C2/BF8F: AD 17 04   LDA $0417 / 85 54 STA $54   ; $0417 == 8 -> back to cell 8
C2/BF94: C6 68      DEC $68             ; substate 1
C2/BF96: 20 4F C5   JSR C2/C54F         ; rebuild the live map from the committed $0408
C2/BF99: A2 E3 FB   LDX #$FBE3 / JMP C2/8385

C2/C183: PHP / SEP #$20
C2/C186: A5 F0      LDA $F0
C2/C188: 89 01      BIT #$01            ; START
C2/C18A: F0 20      BEQ ->C1AC
C2/C18C: 20 C2 EA   JSR C2/EAC2
         LDX #$FCA9 / LDY #$0408 / LDA #$08 / MVN $7E,$C2     ; RESTORE DEFAULTS -> live
         LDX #$FCA9 / LDY #$0F00 / LDA #$08 / MVN $7E,$C2     ; ... and working
C2/C1A7: 20 4F C5   JSR C2/C54F
C2/C1AA: 80 18      BRA ->C1C4
C2/C1AC: A5 5A      LDA $5A / 29 0C AND #$0C   ; UP or DOWN (a row change)
C2/C1B0: F0 12      BEQ ->C1C4
C2/C1B2: 20 C2 EA   JSR C2/EAC2
         LDX #$0F00 / LDY #$0408 / LDA #$08 / MVN $7E,$7E     ; COMMIT working -> live
C2/C1C1: 20 4F C5   JSR C2/C54F
C2/C1C4: E2 30      SEP #$30
C2/C1C6: A5 54 / 38 / E9 0F / A8        ; Y = row = $54 - 15
C2/C1CC: A6 51      LDX $51             ; scan start (measured 0 — see §6)
C2/C1CE: BF 07 C2 C2 LDA $C2C207,X      ; the assignable-button cycle order
C2/C1D2: C9 FF      CMP #$FF / F0 2A BEQ ->C200
C2/C1D6: 39 00 0F   AND $0F00,Y / D0 03 BNE ->C1DE
C2/C1DB: E8 / 80 F1  INX / BRA ->C1CE   ; find the index of the row's CURRENT button
C2/C1DE: AD 1E 0D   LDA $0D1E           ; the direction index (0=R,1=L,2=D,3=U)
C2/C1E1: C9 02      CMP #$02
C2/C1E3: B0 1B      BCS ->C200          ; UP/DOWN never re-assign
C2/C1E5: 3A         DEC A
C2/C1E6: F0 07      BEQ ->C1EF          ; LEFT  -> DEX (wrap to 6)
C2/C1E8: E8 / E0 07 / 90 07             ; RIGHT -> INX, wrap at 7
C2/C1ED: A2 01      LDX #$01
C2/C1EF: CA / 10 03 / A2 06             ; DEX, wrap to 6
C2/C1F4: BF 07 C2 C2 LDA $C2C207,X / 85 00 STA $00
C2/C1FA: 20 0F C2   JSR C2/C20F         ; do the assignment + conflict swap
C2/C1FD: 20 C2 EA   JSR C2/EAC2
C2/C200: PLP / LDX #$FBE3 / JMP C2/8385
```

`C2/C207` = the assignable-button cycle order, `FF`-terminated:
`80 08 40 04 10 20 02 FF` = **A, B, X, Y, R, L, Select** (7 entries; START is not assignable).

The assignment + conflict machinery:

```
C2/C20F: REP #30 / LDX #$0408 / LDY #$0F00 / LDA #$0008 / MVN $7E,$7E   ; RESET working from LIVE
         SEP #30 / STZ $04
         LDA $54 / SEC / SBC #$0F / TAX          ; X = row
         JSR C2/C23A                             ; pass 1: assign + swap
         LDX $04 / LDA $0F00,X / STA $00
         JSR C2/C23A                             ; pass 2: re-settle the last row that was swapped
         JSR C2/C286                             ; repair the L/R page slots
         JMP C2/C5C7

C2/C23A: LDA $C2C2BD,X / TAY                     ; row -> map index (identity: 00..07)
         LDA $00 / STA $0F00,Y                   ; the new mask
         LDA $2993,Y / STA $01                   ; the row's OLD (live) mask
         REP #30 / TXA / ASL A / TAX
         LDA $C2C2C5,X / TAX                     ; -> that row's conflict list
         LDA $C20000,X / AND #$00FF / DEC A / STA $02
         INX / LDY #$969A / MVN $7E,$C2          ; list -> $7E:969A
         JSR C2/C26C

C2/C26C: SEP #30 / LDX $02
  loop:  LDA $969A,X / TAY
         LDA $00 / CMP $0F00,Y / BNE skip
         LDA $01 / STA $0F00,Y / STY $04         ; the conflicting row inherits the old mask
  skip:  DEX / BPL loop / RTS
```

Conflict lists (`C2/C2C5` pointer table → the per-row lists; a list is `len, rows...`):

| row | label (drawn) | list at | rows it may not share with |
|---|---|---|---|
| 0 | Confirm | `C2/C2D3` | 1,2,3,4,5,6,7 |
| 1 | Cancel | `C2/C2DB` | 0,2,4,5,6,7 |
| 2 | Menu | `C2/C2E2` | 0,1,3,4,5 |
| 3 | Dash | `C2/C2E8` | 0,2,5 |
| 4 | Map | `C2/C2EC` | 0,1,2,5 |
| 5 | Warp | `C2/C2F1` | 0,1,3,2,4 |
| 6 | (page-R slot) | `C2/C2F7` | 0,1,7 |

Row 1 and row 3 are *deliberately* absent from each other's list — that is why Cancel and Dash are
both **B** in the factory map. `C2/C286` then repairs the two page slots:

```
C2/C286: LDA $0F00 / ORA $0F01 / STA $00       ; the masks Confirm+Cancel took
         LDA $0F07 / CMP #$10 / BNE + / LDA $0F06 / STA $0F07
       + LDA #$10 / BIT $00 / BNE + / STA $0F06
       + LDA $0F06 / CMP #$20 / BNE + / LDA $0F07 / STA $0F06
       + LDA #$20 / BIT $00 / BNE + / STA $0F07 / RTS
```

The six drawn labels come from `C2/C57A`, which walks `$2993+0..5`, converts each single-bit mask to
an index with `C2/C5B3` (`SEC; INX INX; ROL A; BCC loop` → X = 2×(7 − bit)), and reads the glyph word
from `FF/D554`:

```
FF/D554: A0 00   B7 00   AB 00   B1 00   A1 00   B8 00   B2 00
          bit7    bit6    bit5    bit4    bit3    bit2    bit1
           'A'     'X'     'L'     'R'     'B'     'Y'     'S'
```

This is the whole **merged button-byte layout**, and it is confirmed by measurement (§4.8):

```
$00F0 :  bit7 A | bit6 X | bit5 L | bit4 R | bit3 B | bit2 Y | bit1 Select | bit0 Start
$00F1 :  bit3 Up | bit2 Down | bit1 Left | bit0 Right
$00F0/$00F1 = NEW PRESS   $00F2/$00F3 = HELD   $00F4/$00F5 = NEW PRESS
```

Default map `C2/FCA9` = `80 08 40 08 02 04 10 20 40` →
Confirm A, Cancel B, Menu X, Dash B, Map Select, Warp Y, page-R `$2999`=`0x10`(R),
page-L `$299A`=`0x20`(L), `$299B`=`0x40`.

### 2.7 The cursor: `C2/E9E2` and the nav table `FF/B5A0` → `$7E:9380`

```
C2/E9E2: PHP / SEP #$30
C2/E9E5: A5 54 / 85 81   LDA $54 / STA $81      ; remember the old cell (BF55 compares it)
C2/E9E9: A9 80 / 8D 1E 0D  LDA #$80 / STA $0D1E ; "no direction"
C2/E9EE: A2 00           LDX #$00
C2/E9F0: A5 F5 / 85 5A   LDA $F5 / STA $5A      ; the NEW-PRESS direction byte
C2/E9F4: 4A / B0 07      LSR A / BCS found      ; bit0=Right,1=Left,2=Down,3=Up
         INX / CPX #$04 / BCC loop / BRA out
C2/E9FE: 8E 1E 0D  found: STX $0D1E             ; 0=Right 1=Left 2=Down 3=Up
C2/EA01: AD 1F 0D  LDA $0D1F
C2/EA04: D0 14     BNE out                      ; <<< $0D1F != 0 FREEZES the cursor
C2/EA06: A5 54 / 20 27 EA  LDA $54 / JSR C2/EA27
C2/EA0B: BB        TXY                          ; Y = cell * 6
C2/EA0C: A5 54 / 85 55
C2/EA10: AD 1E 0D / 0A / AA / C2 10
C2/EA17: FC 1F EA  JSR ($EA1F,X)                ; X = dir*2
C2/EA1A: 9C 1F 0D  out: STZ $0D1F
C2/EA27: PHP / REP #30 / AND #$00FF / ASL A / PHA / ASL A / ADC $01,S / TAX  ; X = A*6

C2/EA1F: 45 EA  40 EA  3B EA  36 EA      ; dir 0=Right 1=Left 2=Down 3=Up
C2/EA36: B9 82 93  LDA $9382,Y / BRA ->EA48     ; UP
C2/EA3B: B9 83 93  LDA $9383,Y / BRA ->EA48     ; DOWN
C2/EA40: B9 84 93  LDA $9384,Y / BRA ->EA48     ; LEFT
C2/EA45: B9 85 93  LDA $9385,Y                  ; RIGHT
C2/EA48: 85 54     STA $54
C2/EA4A: 10 06     BPL ->EA52
C2/EA4C: A9 80 / 04 5A / 14 54   LDA #$80 / TSB $5A / TRB $54   ; bit7 = cross-page flag (unused here)
C2/EA52: 60        RTS
```

The **record is 6 bytes at `$7E:9380 + cell*6`**: `[+0]=cursor X, [+1]=cursor Y, [+2]=UP,
[+3]=DOWN, [+4]=LEFT, [+5]=RIGHT`. It is a straight copy of ROM **`FF/B5A0`** (verified byte-for-byte
against the live `$7E:9380` in §4.2; the copy is issued by the screen builder `C2/C3B5` →
`LDX #$C1F6 / JSR C2/ED31`, the bank-`$FF` display-list interpreter). That the first two bytes are the
cursor's screen position is proved three ways: `C2/C157` writes `$93A4` (= cell 6's pair) to move the
hand, `C2/E964` does `LDA $9380,X / STA $0770` (the hand's x/y), and `C2/8A16/8A1E` read
`$9380,X`/`$9381,X` for the hand's second sprite.

```
FF/B5A0 (21 records)
 cell  x    y    up  dn  lf  rt   what sits there
   0   08   1B   04  01  05  05   Stereo                 (col A row 0)
   1   08   3B   00  02  06  06   Battle Mode            (col A row 1)
   2   08   5B   01  03  07  07   Save Menu Cursor       (col A row 2)
   3   08   7B   02  04  08  08   Save Battle Cursor     (col A row 3)
   4   08   9B   03  00  09  09   Save Skill/Item Cursor (col A row 4)
   5   38   1B   09  06  00  00   Battle Speed (header)  (col B row 0)
   6   38   3B   05  07  01  01   Window Color           (col B row 1)
   7   38   5B   06  08  02  02   Battle Gauge           (col B row 2)
   8   38   7B   07  09  03  08   Control Pad            (col B row 3)  <- rt = SELF
   9   38   9B   08  05  04  04   Skill/Item Info.       (col B row 4)
  10   88   17   0B  0B  0A  0A   Battle Gauge Speed strip
  11   88   27   0A  0A  0B  0B   Battle Mes. Speed strip
  12   80   4F   0C  0C  0E  0D   Battle Gauge = Off
  13   A8   4F   0D  0D  0C  0E   Battle Gauge = 1
  14   C0   4F   0E  0E  0D  0C   Battle Gauge = 2
  15   80   63   14  10  0F  0F   button row 0 (Confirm)
  16   80   6F   0F  11  10  10   button row 1 (Cancel)
  17   80   7B   10  12  11  11   button row 2 (Menu)
  18   80   87   11  13  12  12   button row 3 (Dash)
  19   80   93   12  14  13  13   button row 4 (Map)
  20   80   9F   13  0F  14  14   button row 5 (Warp)
```

Geometry that falls out of the table: the left grid is **2 columns × 5 rows**, column A hand x = `0x08`
(icon 24, checkbox 40 in screen px), column B hand x = `0x38` (icon 72, checkbox 88), row pitch
`0x20` = 32 px. Both columns wrap vertically; LEFT and RIGHT swap columns on the same row except on
cell 8, whose RIGHT is itself (its handler intercepts RIGHT — §2.6). Cells 10/11, 12-14 and 15-20 are
three closed islands with their own wrap.

### 2.8 A/B decode `C2/E984` — the menu's A is the *configured* Confirm button

```
C2/E984: 20 E2 E9   JSR C2/E9E2          ; the cursor moves FIRST
C2/E98A: 9C 1D 0D   STZ $0D1D
C2/E98D: A5 68 / CD 17 0D / 8D 17 0D / D0 36
                                          ; if $68 changed since last frame, SKIP the A/B decode
C2/E997: A9 80 / 85 00   LDA #$80 / STA $00
C2/E99B: A2 00      LDX #$00
C2/E99D: BF D9 E9 C2  LDA $C2E9D9,X / TAY   ; C2/E9D9 = 00 01 ... (Confirm, Cancel)
C2/E9A2: B9 93 29   LDA $2993,Y
C2/E9A5: 25 F0      AND $F0                ; the LIVE button map vs the new-press byte
C2/E9A7: F0 05      BEQ +
C2/E9A9: A5 00 / 0C 1D 0D  LDA $00 / TSB $0D1D
C2/E9AE: 46 00 / E8 / E0 02 / 90 EA        ; $80 then $40
C2/E9B5: A5 F4 / 2D 99 29 / ... TSB $0D1D #$02   ; page-R  ($2999)
C2/E9C1: A5 F4 / 2D 9A 29 / ... TSB $0D1D #$01   ; page-L  ($299A)
C2/E9CD: AD 83 0D / F0 05 / A9 40 / 0C 1D 0D     ; $0D83 = a forced B (auto-cancel)
```

So `$0D1D` bit7 = *Confirm*, bit6 = *Cancel*, and both are read through `$2993/$2994` — remap
Confirm to X and the config screen's "A" becomes X. The `$68`/`$0D17` guard means the frame a
substate changes eats its own button, which is why entering a sub-mode never immediately re-fires.

### 2.9 Render side (what a toggle must refresh)

| routine | what it draws | source |
|---|---|---|
| `C2/C4BC` | bottom bar: `$0DCE = $0417`; `$0DCF = $0D86[C2C4F9[$0417]] + C2C505[$0417]`; the 2×2 icon tiles at `$3446/$3448/$3486/$3488` from `FF/D562[$0417]` | called every frame from `BFD4` |
| `C2/C3E4` | the ten 16×16 checkboxes: for i=9..0, tile `$1530 + 4*((C2C42F[i] ^ $0F09[i]) & 1)` written as a 2×2 block at `C2C41B[i]`, `+2`, `+$40`, `+$42` | every frame from `BFD4` |
| `C2/C43A` | `MVN` `$0D86..$0D8F` → `$0F09..$0F12`, then `STZ $0F0D/$0F0E/$0F0F` | screen build, from `C2/C334` |
| `C2/C456` | the three 8-slot strips: `$61=$2EA6`←`$0D8B`, `$61=$2F66`←`$0D90`, `$61=$2FE6`←`$0D8C`; marker at `$61 + (v&7)*2` | after every `C2/C164` |
| `C2/C495` | the Off/1/2 row: `$61 = $3064 + ($0D8A&3)*6` | after every `$0D8A` change |
| `C2/C511` | window colour: copies `0x281` bytes from `FF/(9E10 + ($0D8C&7)*0x280)` to `$7E:8800` and uploads | cell 6 edit, and menu-open via `C2/983F` |
| `C2/C57A` | the six button labels from `$2993+0..5` via `C2/C5B3` + `FF/D554` | after every `C2/C54F` |

`C2/C41B` (checkbox tilemap words) / `C2/C42F` (polarity):

```
C2/C41B: CA2E D631 CA30 CA2F D630 D62E D62F CA31 CA32 D632
C2/C42F: 01 00 00 00 00 00 00 00 00 00
```

Mapping the addresses to the grid (column A = `…CA`, column B = `…D6`, +`$0100` per row):

| grid slot | cell | checkbox i | drawn from |
|---|---|---|---|
| A row 0 | 0 | 0 | `$0F09` = `$0D86` **inverted** (polarity 1, because Stereo On == 0) |
| A row 1 | 1 | 3 | `$0F0C` = `$0D89` |
| A row 2 | 2 | 2 | `$0F0B` = `$0D88` |
| A row 3 | 3 | 7 | `$0F10` = `$0D8D` |
| A row 4 | 4 | 8 | `$0F11` = `$0D8E` |
| B row 0 | 5 | 5 | `$0F0E` = **the cell-5 sub-mode flag** (not `$0D8B`) |
| B row 1 | 6 | 6 | `$0F0F` = **the cell-6 edit flag** (not `$0D8C`) |
| B row 2 | 7 | 4 | `$0F0D` = **the cell-7 sub-mode flag** (not `$0D8A`) |
| B row 3 | 8 | 1 | `$0F0A` = `$0D87` |
| B row 4 | 9 | 9 | `$0F12` = `$0D8F` |

The three overlaps are deliberate: `C2/C43A` zeroes `$0F0D/$0F0E/$0F0F` right after the mirror copy,
and the sub-mode handlers write them. **Measured both ways** in §4.9/§4.10: cycling `$0D8B` does
*not* move the B-row-0 checkbox, but entering cell 5's sub-mode does.

### 2.10 Persistence: pack `C2/934F`, unpack `C2/92F4`, defaults `C2/FCA6`

```
C2/934F  PACK  (called ONLY from C2/BF67, i.e. B out of the config screen)
  STZ $00
  LDA $0D86 / LSR A / ROR $00      \
  LDA $0D87 / LSR A / ROR $00       |  bit0 of each byte shifted in
  LDA $0D88 / LSR A / ROR $00       |
  LDA $0D89 / LSR A / ROR $00       |
  LDA $0D8F / LSR A / ROR $00      /
  LDA $0D8B / AND #$07 / ORA $00 / STA $2990
  STZ $00
  LDA $0D8D / LSR A / ROR $00
  LDA $0D8E / LSR A / ROR $00
  LDA $0D90 / AND #$07 / ASL A / ASL A / ASL A / TSB $00
  LDA $0D8C / AND #$07 / ORA $00 / STA $2991
  LDA $0D8A / AND #$03 / STA $2992
```

```
$2990 = $0D8F<<7 | $0D89<<6 | $0D88<<5 | $0D87<<4 | $0D86<<3 | ($0D8B & 7)
$2991 = $0D8E<<7 | $0D8D<<6 | (($0D90 & 7) << 3)  |  ($0D8C & 7)
$2992 = $0D8A & 3
```

```
C2/92F4  UNPACK  (called ONLY from C2/8133, the X-menu open sequence)
  PHP / REP #$30
  STZ $0D86                                  ; 16-bit -> clears $0D86,$0D87
  LDX #$0D86 / LDY #$0D88 / LDA #$0009 / MVN $7E,$7E   ; overlapping copy: CLEARS $0D86..$0D91
  SEP #$20
  LDA $2990 / ASL A / ROL $0D8F / ASL A / ROL $0D89 / ASL A / ROL $0D88
             / ASL A / ROL $0D87 / ASL A / ROL $0D86
  LDA $2990 / AND #$07 / STA $0D8B
  LDA $2991 / AND #$07 / STA $0D8C
  LDA $2991 / LSR A / LSR A / LSR A / AND #$07 / STA $0D90
  LDA $2991 / ASL A / ROL $0D8E / ASL A / ROL $0D8D
  LDA $2992 / AND #$03 / STA $0D8A
```

**`$0D91` is cleared by the unpack and never written by anything** — that is why cell 5's value
string (base `0x1E` + `$0D91`) is always entry `0x1E`, the *empty* string, and the bottom bar shows
"Battle Speed" with no value (rendered, §4.11).

**The three packed bytes are inside save record 0.** `FF/FA7B` (dumped, §4.12) = `{$7E:2400, 0x05B0}`,
so `$2990/$2991/$2992` land at slot offsets **`0x0590/0x0591/0x0592`**, and the LIVE button map
`$2993..$299B` at **`0x0593..0x059B`**. The custom map `$0408..$040F` is in record 2
(`{$7E:0400, 0x0010}` → slot offsets `0x05EB..0x05F2`) — note **`$0410`, the 9th byte, is outside the
record** and is only restored via `$2993+8` and `C2/C54F`.

Config therefore survives save/load with **no extra code**: it is already in the bytes
`CtMenuSaveWrite` packs. Because B is the only way off the screen (§4.7) and B always packs, a save
can never capture a stale config.

New-game defaults, `C2/95D4`..`C2/960A`: clears `$0400..$04FF` and `$2990..$29AF`, then
`MVN` **12 bytes from `C2/FCA6`** → `$2990`, and 9 bytes from `C2/FCA9` → `$0408`.

```
C2/FCA6: 84 A0 01 | 80 08 40 08 02 04 10 20 40
```
→ Stereo On, Control Pad Standard, Save Menu Cursor N, Battle Mode Wait, Battle Gauge 1,
Battle Gauge Speed 5, Window Color 1, Save Battle Cursor N, Save Skill/Item Cursor Y,
Skill/Item Info On, Battle Mes. Speed 5.

### 2.11 SFX

`C2/EAC2` = `LDA #$00 / JSR C2/EB03` (the menu **blip**); `C2/EACC` = id `$01` (buzz).

```
C2/EB03: LDX #$18 / STX $1E00 / STA $1E01
         LDA $0D76 / BNE done          ; throttle
         LDA #$03 / STA $0D76
         LDA #$80 / STA $1E02
         JSL $C70004
```

Every `JSR/JMP C2/EAC2` in the config code (exhaustive scan of `C2/BF00..C2/C600`, §4.13):
`BF3D` (screen open), `BF5B` (cursor moved), `BF8C` (leaving the button editor), `BFC1` (moving in
the gauge row), `BFD1` (leaving the gauge row), `BFFA` (**any** A press), `C15A` (cell-6 edit
enter/exit), `C17F` (a strip step), `C18C` (button defaults restored), `C1B2` (button row change),
`C1FD` (a button re-assign). **There is no `C2/EACC` (buzz) anywhere in the screen — nothing on the
config screen can be refused.**

---

## 3. RAM CONTRACT

### The working block (valid only while the menu is open; rebuilt by `C2/92F4` at every open)

| addr | setting (string idx) | values | written by | mirror |
|---|---|---|---|---|
| `$7E:0D86` | Stereo (0) | 0=`On` 1=`Off` | `C2/C022` | `$0F09` |
| `$7E:0D87` | Control Pad (8) | 0=`Standard` 1=`Custom` | `C2/C10E` | `$0F0A` |
| `$7E:0D88` | Save Menu Cursor (2) | 0=`N` 1=`Y` | `C2/C043` | `$0F0B` |
| `$7E:0D89` | Battle Mode (1) | 0=`Wait` 1=`Active` | `C2/C079` | `$0F0C` |
| `$7E:0D8A` | Battle Gauge (7) | 0=`Off` 1=`1` 2=`2` | `C2/BFB2` | (`$0F0D` = flag) |
| `$7E:0D8B` | Battle Gauge Speed (10) | 0..7 → `1`..`8` | `C2/C179` (X=`$0D8B`) | (`$0F0E` = flag) |
| `$7E:0D8C` | Window Color (6) | 0..7 → `1`..`8` | `C2/C179` (X=`$0D8C`) | (`$0F0F` = flag) |
| `$7E:0D8D` | Save Battle Cursor (3) | 0=`N` 1=`Y` | `C2/C055` | `$0F10` |
| `$7E:0D8E` | Save Skill/Item Cursor (4) | 0=`N` 1=`Y` | `C2/C067` | `$0F11` |
| `$7E:0D8F` | Skill/Item Info. (9) | 0=`Off` 1=`On` | `C2/C0A0` | `$0F12` |
| `$7E:0D90` | Battle Mes. Speed (11) | 0..7 → `1`..`8` | `C2/C179` (X=`$0D90`) | — |
| `$7E:0D91` | (cell 5's value slot) | always 0 → `""` | nothing | — |

Per-cell tables (both 12 bytes, ROM):

```
C2/C4F9  cell -> offset into $0D86:  00 03 02 07 08 0B 06 04 01 09 05 0A
C2/C505  cell -> value-string base :  28 18 0C 1C 0C 1E 0E 1F 16 1A 20 2A
```

Value-string ranges (decoded from the master pointer table `FF/D3FE`, strings at `FF/D462+`;
entries `0x00-0x0B` are the NAMES, `0x0C-0x31` the values — `0x32+` is past the end of the table):

```
0C 'N'  0D 'Y'  0E..15 '1'..'8'  16 'Standard'  17 'Custom'  18 'Wait'  19 'Active'
1A 'Off' 1B 'On' 1C 'N' 1D 'Y'  1E ''  1F 'Off' 20..27 '1'..'8'  28 'On' 29 'Off'  2A..31 '1'..'8'
```

### Control / cursor RAM

| addr | meaning | proof |
|---|---|---|
| `$7E:0054` | cursor CELL (0..20) | `C2/BFD8`; measured every run in §4 |
| `$7E:0081` | the cell before the mover ran | `C2/E9E7`; `BF55` compares for the move blip |
| `$7E:0417` | the cell the bottom bar describes | `C2/BFDA`; frozen at 7/8 inside substates 3/2 |
| `$7E:007F` | previous frame's `$0417` | `C2/BFE4`; cell 8's RIGHT gate |
| `$7E:0068` | substate 0..3 | `C2/BF40`, `C2/C092`, `C2/C11A` |
| `$7E:005A` | this frame's NEW direction bits (`= $F5`) | `C2/E9F0` |
| `$7E:0D1E` | direction index 0=R 1=L 2=D 3=U, `$80` = none | `C2/E9FE` |
| `$7E:0D1D` | bit7 = Confirm, bit6 = Cancel, bit1 = page-R, bit0 = page-L | `C2/E98A..E9CD` |
| `$7E:0D1F` | non-zero **freezes** the cursor | `C2/EA01`; written `C2/C160` |
| `$7E:0D76` | SFX throttle (3 frames) | `C2/EB0B` |
| `$7E:0D13` | `$5D` — this screen's cursor-sprite id | `C2/BF47` |
| `$7E:0DCE/$0DCF` | bottom bar: cell / composed value-string index | `C2/C4E0`, `C2/C4F1` |
| `$7E:9380 + c*6` | nav record `[x,y,up,dn,lf,rt]`, 21 cells | copy of `FF/B5A0` |
| `$7E:0F09..$0F12` | checkbox mirror of `$0D86..$0D8F` | `C2/C43A` |
| `$7E:0F0D/$0F0E/$0F0F` | sub-mode flags for cells 7 / 5 / 6 | `C2/C08D`, `C2/C0B5`, `C2/C14F` |
| `$7E:0F00..$0F08` | button-map WORKING copy | `C2/C125` |
| `$7E:0408..$0410` | button-map LIVE (Custom) | `C2/C1BE`, `C2/BF7F` |
| `$7E:2993..$299B` | button map actually read by the input decoder | `C2/C555` |
| `$7E:2990..$2992` | the packed, SAVED config | `C2/9379/939B/93A3` |
| `$7E:1E00/01/02` | sound mailbox (cmd / id / param) | `C2/C02A`, `C2/EB05` |

### Gameplay readers (who actually honours a setting)

| setting | reader | effect |
|---|---|---|
| Stereo `$0D86` | `C2/C033` (this screen) and `C2/E6A1` (the other config UI, screen `$0C`, which posts `$2990 & 08`) | APU cmd `$F3` |
| Battle Mode `$0D89` | `CC/E387`: `LDA $2990 / AND #40 / EOR #40 / STA $99E1` | Wait ⇒ `$99E1 = 0x40` |
| Battle Gauge `$0D8A` | `CC/E391`: `LDA $2992 / TAX / LDA $CCFBC5,X / STA $9F20` | gauge style |
| Battle Gauge Speed `$0D8B` | `FD/B814` and `FD/B88C`: `LDA $2990 / AND #07 / TAX / STX $28` | the ATB rate index |
| Window Color `$0D8C` | `C2/C511` (palette block `FF/9E10 + v*0x280`), applied at menu-open by `C2/983F` (`LDA $2991 / AND #07 / STA $0D8C / JMP C2/C511`) and per-slot on the save screen (`C2/D334`, restored `C2/D362`) | window graphics |
| Save Battle Cursor `$0D8D` | `CC/E39C`: `LDA $2991 / AND #40 / STA $A0D4` | battle cursor memory |
| Save Skill/Item Cursor `$0D8E` | `CC/E3A4`: `LDA $2991 / AND #80 / STA $A0D5` | battle cursor memory |
| Skill/Item Info. `$0D8F` | `CC/E3AC`: `LDA $2990 / AND #80 / STA $A0DB` | battle info popup |
| Battle Mes. Speed `$0D90` | `CD/01A3`: `LDA $2991 / LSR/LSR/LSR / AND #07 / TAX / LDA $CD019D,X` | the message dwell — this is the same `CD/019D[$2991>>3&7]` the results-box dossier already uses |
| Save Menu Cursor `$0D88` | `C2/99D9` and `C2/9A48` (`BNE` else `STZ $0411..$041A`), `C2/DC91` (`STZ $1040/$1041`), `C2/DEA7` | whether the menu remembers cursor positions |
| Control Pad `$0D87` | `C2/C564` inside `C2/C54F` → `$2993`, read by `C2/E9A2` and by every field/battle input site that consults `$2993..$299B` | the whole button map |

---

## 4. ORACLE MEASUREMENTS

All runs use `scratchpad/menu/ct_probe.exe` and `saves/com_jogo_salvo.state`, with the shared prefix

```
P = 0:20,200:2,0:40,80:2,0:8,80:2,0:8,80:2,0:20,100:2,0:60
    (X opens the menu, Right×3 to ribbon option 3, A -> screen 4, settle)
```

### 4.1 The screen is reached; the working block and the nav table are live

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/com_jogo_salvo.state \
  scratchpad/menu/cfg0.raw --btnscript "<P>"
```
`cfg0.raw.wram`:
```
$0067 screen = 04   $0068 = 00   $0054 cursor = 00   $0417 = 00
$0D86..$0D95: 00 00 00 01 01 04 00 00 01 01 04 00 78 12 00 00
$0F00..$0F13: 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00 01 01 00
$0408..$0410: 80 08 40 08 02 04 10 20 40
$2990..$2992: C4 A0 01
$2993..$299B: 80 08 40 08 02 04 10 20 40
$0DCE,$0DCF: 00 28
$0D13,$0D1D,$0D1E,$0D1F: 5D 00 80 00
```
Pack check against §2.10: `$2990 = 1<<7|1<<6|0|0|0|4 = C4` ✔, `$2991 = 1<<7|0|4<<3|0 = A0` ✔,
`$2992 = 1` ✔.

### 4.2 The nav table in RAM == `FF/B5A0`

Same `.wram`, `$7E:9380`:
```
9380: 08 1B 04 01 05 05  08 3B 00 02 06 06  08 5B 01 03
9390: 07 07 08 7B 02 04  08 08 08 9B 03 00  09 09 38 1B
93A0: 09 06 00 00 38 3B  05 07 01 01 38 5B  06 08 02 02
93B0: 38 7B 07 09 03 08  38 9B 08 05 04 04  88 17 0B 0B
93C0: 0A 0A 88 27 0A 0A  0B 0B 80 4F 0C 0C  0E 0D A8 4F
93D0: 0D 0D 0C 0E C0 4F  0E 0E 0D 0C 80 63  14 10 0F 0F
93E0: 80 6F 0F 11 10 10  80 7B 10 12 11 11  80 87 11 13
93F0: 12 12 80 93 12 14  13 13 80 9F 13 0F  14 14 00 00
```
A ROM search for `08 1B 04 01 05 05 08 3B 00 02 06 06` finds exactly one hit: **`FF/B5A0`**
(file `0x3FB5A0`), and the 21 records there are byte-identical to the block above.

### 4.3 Five boolean toggles + the B-exit pack (the §(g) end-to-end run)

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E0D86:12,7E2990:3,7E0054:1,7E0068:1,7E0D1F:1" \
  --from saves/com_jogo_salvo.state \
  --btnscript "<P>,20:2,0:12,100:2,0:12,20:2,0:12,100:2,0:12,20:2,0:12,100:2,0:12,\
20:2,0:12,100:2,0:12,80:2,0:12,100:2,0:12,01:2,0:30" --diff --writer
```
```
  f54     7E0D86=000000010104000001010400*(C2:934D)  ...   <- UNPACK at menu-open
  f106    ...  7E0054=00*(C2:C3BD)                          <- screen built, cursor 0
  f167    7E0054=01*(C2:EA4A)                               <- Down -> cell 1
  f181    7E0D86=000000000104000001010400*(C2:C07C)         <- A: $0D89 1->0
  f195    7E0054=02*(C2:EA4A)
  f209    7E0D86=000001000104000001010400*(C2:C046)         <- A: $0D88 0->1
  f223    7E0054=03*(C2:EA4A)
  f237    7E0D86=000001000104000101010400*(C2:C058)         <- A: $0D8D 0->1
  f251    7E0054=04*(C2:EA4A)
  f265    7E0D86=000001000104000100010400*(C2:C06A)         <- A: $0D8E 1->0
  f279    7E0054=09*(C2:EA4A)                               <- Right from cell 4 -> cell 9 (nav rt=09)
  f293    7E0D86=000001000104000100000400*(C2:C0A3)         <- A: $0D8F 1->0
  f307    7E2990=246001*(C2:93A6)  7E0054=03*(C2:9BA2)      <- B: PACK + back to ribbon option 3
```
Pack check: bytes now `00 00 01 00 01 04 00 01 00 00 04 00` →
`$2990 = 0<<7|0<<6|1<<5|0|0|4 = 0x24` ✔, `$2991 = 0<<7|1<<6|4<<3|0 = 0x60` ✔, `$2992 = 1` ✔.

### 4.4 Cell 5 → the two 8-step strips (wrap proven)

```
--monitor "7E0D86:12,7E0F0D:3,7E0D1F:1,7E0054:1,7E0068:1,7E0417:1" \
--btnscript "<P>,80:2,0:12,100:2,0:12,80:2,0:12,80:2,0:12,80:2,0:12,80:2,0:12,\
20:2,0:12,80:2,0:12,40:2,0:12,100:2,0:20" --diff --writer
```
```
  f167  7E0054=05*(C2:EA4A)  7E0417=05*(C2:BFDD)                 Right -> cell 5
  f181  7E0F0D=000100*(C2:C0B8)  7E0054=0A*(C2:C0B0)             A     -> cursor 10, $0F0E=1
  f195  7E0D86=000000010105000001010400*(C2:C17C)                Right -> $0D8B 4->5
  f209  ...0106...                                               Right -> 6
  f223  ...0107...                                               Right -> 7
  f237  ...0100...                                               Right -> 0   (WRAPS)
  f251  7E0054=0B*(C2:EA4A)                                      Down  -> cell 11 (value unchanged)
  f265  7E0D86=...01 01 05 00(C2:C17C)                           Right -> $0D90 4->5
  f279  ...0400                                                  Left  -> 4
  f293  7E0F0D=000000*(C2:C0E8)  7E0054=05*(C2:C0E2)             A     -> back to cell 5
```

### 4.5 Cell 7 → the 3-option row, and cell 8 → the button editor

```
--monitor "7E0D86:12,7E0F0D:3,7E0054:1,7E0068:1,7E0417:1" \
--btnscript "<P>,80:2,0:12,20:2,0:12,20:2,0:12,100:2,0:12,80:2,0:12,80:2,0:12,40:2,0:12,\
100:2,0:20,20:2,0:12,100:2,0:30" --diff --writer
```
```
  f195  7E0054=07*(C2:EA4A)                                   cursor on cell 7
  f209  7E0F0D=010000*(C2:C090)  7E0054=0D*(C2:C08D)          A -> cell 12+$0D8A(=1) = 13
  f223  7E0D86=...0102 04...*(C2:BFB5)  7E0054=0E             Right -> cell 14, $0D8A=2
  f237  7E0D86=...0100 04...*(C2:BFB5)  7E0054=0C             Right -> cell 12 (WRAPS), $0D8A=0
  f251  7E0D86=...0102 04...*(C2:BFB5)  7E0054=0E             Left  -> cell 14 (WRAPS), $0D8A=2
  f265  7E0F0D=000000*(C2:BFC8)  7E0054=07*(C2:BFCD)          A -> back to cell 7
  f287  7E0054=08*(C2:EA4A)                                   Down -> cell 8
  f301  7E0D86=000100010204...*(C2:C111)  7E0054=0F*(C2:C11A) A -> $0D87=1 (Custom), cursor 15
```

### 4.6 The button editor: assignment, conflict swap, commit

```
--monitor "7E0F00:9,7E0408:9,7E2993:9,7E0054:1" \
--btnscript "<P>,80:2,0:12,20:2,0:12,20:2,0:12,20:2,0:12,100:2,0:16,\
20:2,0:14,20:2,0:14,40:2,0:20,80:2,0:20,80:2,0:20" --diff --writer
```
```
  f223  7E0F00=800840080204102040*(C2:C128)  7E0054=0F*(C2:C11A)   enter editor, working = live
  f241  7E0408=...(C2:C1C1)  7E0054=10*(C2:EA4A)                   Down: COMMITS then moves to row 1
  f257  7E0408=...(C2:C1C1)  7E0054=11*(C2:EA4A)                   Down: row 2
  f273  7E0408=...(C2:BF82)  7E0054=08*(C2:BF94)                   LEFT: commit + leave -> cell 8
  f295  7E0F00=800840080204102040*(C2:C128)  7E0054=0F             re-enter
  f317  7E0F00=088040800204102040*(C2:C2BC)                        RIGHT on row 0
```
Row 0 went `80`→`08` (cycle index 0→1 in `C2/C207`), and **both** rows that held `08` — row 1
(Cancel) *and* row 3 (Dash) — received the old `80`. That is `C2/C26C` scanning row 0's whole
conflict list `{1,2,3,4,5,6,7}` without breaking, exactly as §2.6 predicts.

A second RIGHT from an earlier run (`--monitor "7E0F00:8,..."`) gave `4008800802041020`: row 0 →
`40` and only row 2 (which held `40`) inherited `80` — because `C2/C20F` **re-copies `$0408` into
`$0F00` before every assignment**, so each press is one swap applied to the LIVE map, not a chain.

### 4.7 Only A, B and the d-pad exist on this screen

```
--monitor "7E0054:1,7E0067:1,7E2990:3,7E0D86:12" \
--btnscript "<P>,100:2,0:20,200:2,0:40,04:2,0:20,08:2,0:20,400:2,0:20,800:2,0:20" --diff --writer
```
```
  f167    7E0054=00  7E0067=04  7E2990=C4A001  7E0D86=010000010104000001010400*(C2:C025)
```
One row only: the A press. **X, Select, Start, L and R changed nothing** — `$0067` stayed `04`, the
menu did not close. The config screen has exactly one exit, B, and B always packs.

### 4.8 The merged button byte

```
--monitor "7E00F0:6" --btnscript "0:20,01:2,0:8,02:2,0:8,04:2,0:8,08:2,0:8,100:2,0:8,400:2,0:8"
  f21 (B)      7E00F0=080008000800
  f31 (Y)      7E00F0=040004000400
  f41 (Select) 7E00F0=020002000200
  f51 (Start)  7E00F0=010001000100
  f61 (A)      7E00F0=800080008000
  f71 (L)      7E00F0=200020002000
--monitor "7E00F0:6" --btnscript "0:20,800:2,0:10,200:2,0:10,10:2,0:10,40:2,0:14"
  f21 (R)      7E00F0=100010001000
  f33 (X)      7E00F0=400040004000
  f45 (Up)     7E00F0=000800080008
  (Left) -> byte1 = 02 ; (Down) -> byte1 = 04 ; (Right) -> byte1 = 01   [from the 4.x runs]
```
and, holding a direction for 30 frames (`--btnscript "<P>,20:30,..."`), only the **first** frame
shows a non-zero `$F0/$F1` and `$F4/$F5` (`f167 000400040004`, `f168 000000040000`) and the cursor
moved exactly once — the config nav is **edge-triggered, no auto-repeat**.

### 4.9 The `$0DCF` bottom-bar composition, on four cells

```
--monitor "7E0DCE:2,7E0054:1" --btnscript "<P>,20:2,0:14,20:2,0:14,80:2,0:14,20:2,0:14,20:2,0:14"
  f183  7E0DCE=020C  cell 2 -> C505[2]=0C + $0D88(0) = 0C = 'N'
  f199  7E0DCE=0720  cell 7 -> C505[7]=1F + $0D8A(1) = 20 = '1'
  f215  7E0DCE=0816  cell 8 -> C505[8]=16 + $0D87(0) = 16 = 'Standard'
  f231  7E0DCE=091B  cell 9 -> C505[9]=1A + $0D8F(1) = 1B = 'On'
```

### 4.10 The checkbox model, graded both ways (a render diff)

Negative test — cycle `$0D8B` with the cursor parked on cell 5 in both captures:
```
cfgA: <P>,80:2,0:12,100:2,0:14,0:14,100:2,0:40        $0D8B=04, cursor 5
cfgB: <P>,80:2,0:12,100:2,0:14,80:2,0:14,100:2,0:40   $0D8B=05, cursor 5
diff -> changed: y 32..39  x 184..199        (ONLY the strip marker moved)
```
The B-row-0 checkbox did **not** move: it is not driven by `$0D8B`.

Positive test — enter cell 5's sub-mode:
```
cfgC: <P>,80:2,0:12,0:40          cursor 05, $0F0D-0F = 00 00 00
cfgD: <P>,80:2,0:12,100:2,0:40    cursor 0A, $0F0D-0F = 00 01 00
diff -> y 33..48: x runs [(56,71), (89,102), (136,151)]
        y 196..207 (the bottom bar)
```
`x 56..71` = the hand leaving cell 5 (nav x `0x38` = 56), `x 136..151` = the hand arriving at cell 10
(nav x `0x88` = 136) — **the nav record's first two bytes are the hand's screen position** — and
`x 89..102` is the **B-row-0 checkbox**, which changed because `$0F0E` went 0→1.

Anchor for the checkbox geometry (toggling Stereo, cursor unmoved):
```
cfgE: <P>,0:30            $0D86=00 $0F09=00
cfgF: <P>,100:2,0:30      $0D86=01 $0F09=01
diff -> y 35..44 x 41..54   (the check mark inside the 16x16 box at screen (40,32))
        y 196..204 x 103..114  (the bottom-bar value text 'On' -> 'Off')
```

### 4.11 The empty value string of cell 5

`--peek ... --btnscript "<P>,80:2,0:12,100:2,0:14,80:2,0:14,20:2,0:14,80:2,0:14,100:2,0:40"`
leaves `$0D86..= 00 00 00 01 01 05 00 00 01 01 05 00`, cursor `05`, and the render
(`scratchpad/menu/cfg1.png`) shows the bottom bar reading **"Battle Speed"** with **no value** —
`C505[5] + $0D91 = 0x1E + 0 = 0x1E`, the empty string.

### 4.12 The save record table

```
FF/FA7B: rec0 $7E:2400 len 0x05B0   rec1 $7E:2C23 len 0x0033   rec2 $7E:0400 len 0x0010
         rec3 $00:0100 len 0x0010   rec4 $7F:0000 len 0x0200   rec5 $00:027E len 0x0002
         rec6 $00:0290 len 0x0011   rec7 $7E:2C7C len 0x001E
```

### 4.13 SFX call sites in the config code

ROM scan of `C2/BF00..C2/C600` for `20 C2 EA` / `20 CC EA`:
```
JSR C2/EACC (buzz):  (none)
JSR C2/EAC2 (blip):  C2/BF3D C2/BF5B C2/BF8C C2/BFC1 C2/BFFA C2/C15A C2/C17F C2/C18C C2/C1B2 C2/C1FD
```
(plus `C2/BFD1`, a `JMP C2/EAC2`.)
And live: `--monitor "7E1E00:4,7E0D76:1,7E0D86:1,7E0054:1"` over `<P>,100:2,0:20,100:2,0:20,20:2,0:20,100:2,0:20`
shows `7E0D76=03*(C2:EB15)` on the screen-open frame, on every cursor move and on **every** A press —
including presses that changed nothing (`f211`, `f233`, cursor on cell 1 with no value change).
`$1E00` reads `00 00 80 07` at every frame boundary because `C7/0196` zeroes it inside the frame.

---

## 5. IMPLEMENTATION SPEC (for `src/menu.c` + the `menuScreen` loop in `src/field.c`)

**S1. State.** Add to the menu state: `cfgCursor` (0..20, the ROM's `$54`), `cfgSubstate` (0..3, the
ROM's `$68`), and the working button map `btnWork[9]`. The twelve setting bytes must live in
`h->mem[0x0D86..0x0D91]` (the engine already unpacks them for rendering via `mCfgUnpack`); the toggle
path must WRITE those bytes, and `mCfgUnpack` must be replaced by a real unpack **on menu open**
(`C2/92F4`) so `$0D86..$0D91` is the single working copy, not a per-render recomputation.

**S2. Menu-open (`C2/8133`).** Clear `$0D86..$0D91`, then unpack `$2990/$2991/$2992` per §2.10.
Also mirror `$0D86..$0D8F` → `$0F09..$0F12` and `STZ $0F0D/$0F0E/$0F0F` when the config screen is
BUILT (`C2/C43A`), and set `$0D8C = $2991 & 7` at menu-open (`C2/983F`).

**S3. Nav.** Bake `FF/B5A0` (21×6) as a table (or read it from ROM at `0x3FB5A0`). Per frame, in
this order:
  1. `prev = cursor`; `dirIdx = 0x80`; if `$0D1F == 0` and a d-pad edge exists, take the FIRST set
     bit of the new-press direction byte in the order Right(0), Left(1), Down(2), Up(3), set
     `dirIdx`, and `cursor = nav[cursor][2 + (3 - dirIdx)]`  — concretely
     `up = nav[c][2], down = nav[c][3], left = nav[c][4], right = nav[c][5]`.
     Clear `$0D1F` afterwards (the ROM's `STZ $0D1F` at `C2/EA1A`).
  2. Decode A/B from the **live map**: `A = (btnLive[0] & newPressed) != 0`,
     `B = (btnLive[1] & newPressed) != 0`, using the merged byte layout of §2.6. Skip this decode on
     any frame where the substate changed (`C2/E98D`).
  3. Dispatch (below).
  4. If `cursor != prev` → blip.
Edge-triggered only; there is no auto-repeat on this screen.

**S4. Substate 1 dispatch.** `$0417 = cursor`, then switch on `cursor` 0..11 exactly as the
`C2/BFFF` table:
```
0  if A: m[0x0D86] = (m[0x0D86]+1)&1; m[0x0F09] = m[0x0D86];  post APU cmd $F3 param m[0x0D86]
1  if A: m[0x0D89] = (m[0x0D89]+1)&1; m[0x0F0C] = m[0x0D89]
2  if A: m[0x0D88] = (m[0x0D88]+1)&1; m[0x0F0B] = m[0x0D88]
3  if A: m[0x0D8D] = (m[0x0D8D]+1)&1; m[0x0F10] = m[0x0D8D]
4  if A: m[0x0D8E] = (m[0x0D8E]+1)&1; m[0x0F11] = m[0x0D8E]
5  if A: cursor = 10; $0417 = 10; m[0x0F0E] = 1
6  if m[0x0F0F]: cycle(&m[0x0D8C]); reload the window palette
   if A||B: m[0x0F0F] ^= 1; nav[6].x/y = m[0x0F0F] ? (0x88,0x37) : (0x38,0x3B); blip
   m[0x0D1F] = m[0x0F0F]
7  if A: cursor = 12 + m[0x0D8A]; m[0x0F0D]++; substate = 3
8  if A: m[0x0D87] = (m[0x0D87]+1)&1; m[0x0F0A] = m[0x0D87];
        if m[0x0D87]: cursor = 15; substate = 2; memcpy(btnWork, &m[0x0408], 9)
   else if prevCell==8 && RIGHT: if m[0x0D87] { cursor=15; substate=2; memcpy(...) }
                                 else         { cursor = 3; $0417 = 3; }
   rebuildLiveMap()            // C2/C54F, ALWAYS
9  if A: m[0x0D8F] = (m[0x0D8F]+1)&1; m[0x0F12] = m[0x0D8F]
10 cycle(&m[0x0D8B]);  if A||B { cursor = 5; $0417 = 5; m[0x0F0E] = 0; }
11 cycle(&m[0x0D90]);  if A||B { cursor = 5; $0417 = 5; m[0x0F0E] = 0; }
```
where `cycle(p)`: if RIGHT `*p = (*p+1)&7` / if LEFT `*p = (*p-1)&7`, then redraw + blip; UP/DOWN do
nothing here (they are consumed by the nav table). After the dispatch: `$7F = $0417`, recompute
`$0DCE/$0DCF`, redraw the checkboxes, and **blip if A was pressed at all** (`C2/BFFA`).

**S5. Substate 3 (cells 12-14).** Nav only; on a cursor change `m[0x0D8A] = cursor - 12` + blip.
On A or B: `m[0x0F0D] = 0`, `cursor = $0417` (still 7), substate = 1, blip.

**S6. Substate 2 (cells 15-20).** LEFT or B → `memcpy(&m[0x0408], btnWork, 9)`, `cursor = $0417`
(= 8), substate = 1, `rebuildLiveMap()`, blip. START → copy the ROM defaults `C2/FCA9` into both
`m[0x0408]` and `btnWork`, `rebuildLiveMap()`, blip. UP/DOWN → `memcpy(&m[0x0408], btnWork, 9)`,
`rebuildLiveMap()`, blip (the nav table already moved the cursor). LEFT/RIGHT (`dirIdx < 2`) on row
`r = cursor - 15`: find `i` with `C2C207[i] & btnWork[r]`, step `i` ±1 wrapping over 0..6, then
run the assignment of §2.6 — reset `btnWork` from `m[0x0408]`, assign, swap every row in
`conflict[r]` that holds the new mask to `m[0x2993 + r]`, re-run once for the last swapped row, then
the `C2/C286` L/R repair — and blip.
`rebuildLiveMap()` = copy `C2/FCA9` into `m[0x2993..0x299B]`, then, if `m[0x0D87] != 0`, copy
`m[0x0408..0x0410]` over it.

**S7. Exit.** B on substate 1 with `m[0x0F0E] == 0 && m[0x0F0F] == 0`: run the pack of §2.10 into
`m[0x2990..0x2992]`, then return to the ribbon. B with a sub-mode flag set is forwarded to the cell
handler instead. Nothing else exits: X/L/R/Select/Start must be inert on screen 4.

**S8. Render.** `CtMenuRenderConfig` should keep its layout but source the ten checkboxes from
`$0F09..$0F12` (with `$0F09`'s polarity inverted, `C2/C42F[0] == 1`) rather than from the setting
values, so cells 5/6/7 show their sub-mode flag; the bottom bar keeps using `$0417` (which stays 7
or 8 inside the sub-modes) with the value string index `$0D86[C4F9[$0417]] + C505[$0417]`.

**S9. Persistence.** Nothing new. `CtMenuSaveWrite` already packs record 0 (`$7E:2400 + 0x5B0`) and
record 2 (`$7E:0400 + 0x10`); once the toggles write `$2990-92` and `$0408-0410`, the config and the
custom button map are saved and restored for free. New games must seed `$2990..$299B` from
`C2/FCA6` (12 bytes) and `$0408..$0410` from `C2/FCA9` (9 bytes).

**S10. APU.** Only cell 0 posts: `$1E00 = 0xF3`, `$1E01 = m[0x0D86]`, dispatch immediately (not
throttled). Every blip is `$1E00 = 0x18`, `$1E01 = 0x00`, `$1E02 = 0x80`, gated by the 3-frame
throttle `$0D76`. No buzz exists on this screen.

---

## 6. OPEN / BLOCKED

1. **`LDX $51` at `C2/C1CC`.** The scan for a button row's current index starts at `$51`, not at 0.
   I measured `$0051 == 0` on every frame of the button-editor runs (§4.6 monitored `7E0051:1`), so
   the ROM behaves as "start at 0" there — but I did not find what writes `$51` on this screen, so I
   cannot prove it is always 0. Port it as `X = m[0x51]` (cheap and faithful) rather than as 0.
2. **The SPC side of cmd `$F3`.** I proved the store sequence (`C2/C028-C033`) and that
   `audio_commands.md` already classifies `$F3` as a driver-level raw port command, but I could not
   observe the command in flight: `$1E00` is zeroed by `C7/0196` inside the same frame, `--hookpc`
   only matches bank-`$C0` PCs (`visits=0` for `C2/C033`, §confirmed), and `--aram` takes no
   `--btnscript`. *Unblocked by:* an oracle mode that hooks a bank-`$C2` PC, or `--aram` gaining
   `--btnscript`. What the SPC does with the parameter (panning/mono fold) is therefore unmeasured.
3. **Who re-posts `$F3` after a LOAD.** The only two `$F3` sites in the ROM are `C2/C033` (this
   screen) and `C2/E6A1` (menu mode with screen id `$0C`). Neither runs on a save load, so a loaded
   game's stereo bit may only reach the SPC the next time the player touches the setting. I did not
   verify this against a real load (it needs a load-from-title capture, §6.6).
4. **The exact hand-sprite offset from the nav (x,y).** The nav bytes are proven to be the anchor
   (§4.10), but the hand's pixel offset from it is not pinned: OAM in every `--peek` of this screen
   is empty (all `y=224`), so the hand is either a BG-layer draw or is built after the peek's capture
   point. `C2/8A16` uses `x+2` and `y-1+$0D9E`; `$0D9E` is unmeasured.
5. **Two tilemap staging buffers.** The checkbox anchor is measured (cell 0's 2×2 block is screen
   `(40,32)`, §4.10) and `C2/C41B`'s addresses are internally consistent with a 0x40-byte row stride
   (column pitch 12 bytes = 48 px, row pitch 0x100 = 32 px, both matching the nav table). But
   `C2/C456`'s strip base `$2EA6` cannot be in the same linear buffer as `$2ECA` under any single
   base (§4.10 puts both markers on the same 8-px screen row while their addresses differ by 0x24),
   so the checkbox layer and the strip/text layer are **different buffers**. I did not derive either
   base. This does not block the toggles; it blocks only a pixel-exact rewrite of the config
   render, which is out of this gap's scope.
6. **The other config UI (menu mode with screen `$0B`/`$0C`, `C2/E1E3` / `C2/E60B`).** It writes the
   same packed bytes directly (`C2/E253 TSB $2990` / `C2/E258 TRB $2990` toggles the Battle-Mode bit,
   `C2/E699` posts stereo from `$2990 & 08`) and uses the same `$9380` nav machinery. I did not
   derive it — no state in `saves/` reaches it. *Unblocked by:* a capture of the title-screen /
   new-game options screen (a savestate taken with menu mode 2 or 6 active, i.e. `$0067` = `$0B` or
   `$0C`).
7. **`$0D8A == 3`.** `C2/C495` masks with `AND #3` and `C2/92F4` unpacks 2 bits, so a value of 3 is
   representable in a save, but the nav table has only cells 12-14, so the UI can neither produce nor
   repair it. What `CC/FBC5[3]` selects in battle is unmeasured.
8. **A forced menu close from outside the screen.** `C2/E9CD` turns `$0D83` into a synthetic B, which
   routes through the normal `BF5F` exit and therefore packs. Whether any other path can tear the
   menu down while `$0067 == 4` (skipping the pack) was not tested.

---

## ADVERSARIAL VERIFICATION (independent pass, 2026-08-30)

Method: every ROM address the dossier cites was re-disassembled from scratch
(`python tools/dis.py`, plus a raw byte dumper `scratchpad/menu/_av_cfg_dump.py`) and the
load-bearing oracle claims were re-run with `scratchpad/menu/ct_probe.exe`. Findings are marked
**CONFIRMED** / **DEFECT** / **GAP**. Nothing below is taken from the original author's text.

### AV-1. ROM re-disassembly: what held up

Every one of the following was re-read byte-for-byte and matches the dossier exactly:

| dossier claim | re-derived at | result |
|---|---|---|
| screen 4 handler array = `C2/BF35` (`3D BF 45 BF 6D BF 9F BF`) | `C2/826F` does `LDA $C28292,X`, X=`$67`*2; `C2/8292`+8 = `35 BF` | CONFIRMED |
| `C2/BF45` idle loop, B-gate on `$0F0E|$0F0F`, pack at `BF67`, `JMP C2/9A98` | `2BF45` | CONFIRMED |
| `C2/BFD4` dispatcher (`$0417`, `JSR ($BFFF,X)`, `$7F`, `C4BC`/`C3E4`/`8385`, blip on any A) | `2BFD4` | CONFIRMED |
| cell table `C2/BFFF` = `[24 ROM bytes at 0x2BFFF: python tools/dis.py 0x2BFFF 24]` | raw dump | CONFIRMED |
| the six `INC A / AND #01` toggles + APU `$F3` on cell 0 only | `2C017..2C0A6` | CONFIRMED |
| cell 5 header (`$54=10`, `$0F0E=1`), cells 10/11 (`AND #$C0` -> back to 5) | `2C0A7..2C0E8` | CONFIRMED |
| `C2/C164` cycle (`$5A&3`, RIGHT=+1 LEFT=-1, `AND #07`) | `2C164` | CONFIRMED |
| cell 6 in-place edit + `STX $93A4` + `$0D1F = $0F0F` | `2C12C..2C163` | CONFIRMED |
| cell 7 -> substate 3, `$54 = $0D8A + 12`, `INC $0F0D` | `2C080` | CONFIRMED |
| substate 3 `C2/BF9F`: value = `$54 - 12`, A/B -> `$54 = $0417`, `$68 = 1` | `2BF9F` | CONFIRMED |
| cell 8 -> substate 2, `MVN` 9 bytes `$0408`->`$0F00`, RIGHT-on-Standard jumps to cell 3 | `2C0E9` | CONFIRMED |
| substate 2 `C2/BF6D` (LEFT or B commit+exit) and `C2/C183` (START defaults, UP/DOWN commit) | `2BF6D`, `2C183` | CONFIRMED |
| `LDX $51` really is the scan start at `C2/C1CC` | `2C1CC` | CONFIRMED (still a GAP, see AV-8) |
| `C2/C207` = `80 08 40 04 10 20 02 FF` | raw dump | CONFIRMED |
| `C2/C2BD` = identity `00..07`; `C2/C2C5` = 7 pointers `C2D3 C2DB C2E2 C2E8 C2EC C2F1 C2F7` | raw dump | CONFIRMED |
| all 7 conflict lists (`07 01 02 03 04 05 06 07` ... `03 00 01 07`) | raw dump `C2/C2D3..C2FA` | CONFIRMED |
| `C2/C20F` / `C2/C23A` / `C2/C26C` / `C2/C286` bodies | `2C20F..2C2BC` | CONFIRMED |
| `C2/C54F`->`C2/C555` rebuild (ROM `FCA9`->`$2993`, then `$0408`->`$2993` iff `$0D87`) | `2C54F` | CONFIRMED |
| `C2/C57A` walks rows 0..5 only (`CMP #0006`), pos table `C2/C5A7` has exactly 6 words | `2C57A`, `2C5A7` | CONFIRMED |
| `C2/C5B3` index = `2*(7-bit)`; `FF/D554` = `A0 00 B7 00 AB 00 B1 00 A1 00 B8 00 B2 00` | `2C5B3`, raw | CONFIRMED |
| `C2/E9E2` cursor mover, `$0D1F` freeze, dir order R/L/D/U, `C2/EA27` = A*6 | `2E9E2`,`2EA27` | CONFIRMED |
| `C2/EA1F` = `45 EA 40 EA 3B EA 36 EA`; UP=+2 DOWN=+3 LEFT=+4 RIGHT=+5 | raw + `2EA36..2EA52` | CONFIRMED |
| `C2/E984` A/B decode through `$2993`/`$2994` + `$68`/`$0D17` guard + `$0D83` forced B | `2E984` | CONFIRMED |
| `FF/B5A0` 21x6 nav table, every record | raw dump `3FB5A0` | CONFIRMED |
| `C2/934F` pack -> the three bit formulas for `$2990/$2991/$2992` | `2934F` | CONFIRMED |
| `C2/92F4` unpack (incl. the overlapping `MVN` that clears `$0D86..$0D91`) | `292F4` | CONFIRMED |
| pack has exactly ONE caller (`C2/BF67`), unpack exactly one in bank C2 (`C2/8133`) | ROM scan `20 4F 93` / `20 F4 92` | CONFIRMED |
| `C2/C43A` mirror `$0D86..$0D8F`->`$0F09..$0F12` then `STZ $0F0D/0E/0F` | `2C43A` | CONFIRMED |
| `C2/C3E4` checkbox loop `tile = $1530 + 4*((C42F[i] ^ $0F09[i])&1)`, 2x2 at `+0,+2,+$40,+$42` | `2C3E4` | CONFIRMED |
| `C2/C41B` = `CA2E D631 CA30 CA2F D630 D62E D62F CA31 CA32 D632`, `C2/C42F` = `01 00 ...` | raw dump | CONFIRMED |
| the checkbox->cell mapping of 2.9 (B rows 0/1/2 driven by the sub-mode flags) | derived from the two tables above | CONFIRMED |
| `C2/C4BC` bottom bar `$0DCE=$0417`, `$0DCF=$0D86[C4F9[$0417]] + C505[$0417]` | `2C4BC` | CONFIRMED |
| `C2/C4F9` = `00 03 02 07 08 0B 06 04 01 09 05 0A`; `C2/C505` = `28 18 0C 1C 0C 1E 0E 1F 16 1A 20 2A` | raw dump | CONFIRMED |
| `FF/FA7B` save-record table, all 8 records | raw dump `3FFA7B` | CONFIRMED |
| new-game seed: 12 bytes `C2/FCA6`->`$2990`, 9 bytes `C2/FCA9`->`$0408` | `C2/95D6..960A` (raw; `dis.py` desyncs on the `MVN`s) | CONFIRMED |
| `C2/FCA6` = `84 A0 01 80 08 40 08 02 04 10 20 40` and the decoded default settings | raw + the unpack | CONFIRMED |
| battle readers `CC/E387 / E391 / E39C / E3A4 / E3AC` | `CE387..CE3B3` | CONFIRMED |
| ATB rate `FD/B814` and `FD/B88C` (`LDA $2990 / AND #07 / TAX / STX $28`) | `3DB814`, `3DB88C` | CONFIRMED |
| `C2/983F` = `LDA $2991 / AND #07 / STA $0D8C / JMP C2/C511` | `2983F` | CONFIRMED |
| `C2/C511` copies `0x281` bytes from `FF/(9E10 + ($0D8C&7)*0x280)` -> `$7E:8800` | `2C511` | CONFIRMED (see AV-6 for the *instruction* it uses) |

### AV-2. STRONGER than the dossier: the nav-table copy is proven, length and all

The dossier asserts the `FF/B5A0`->`$7E:9380` copy is "issued by the screen builder `C2/C3B5` ->
`LDX #$C1F6 / JSR C2/ED31`" but never opened the display list. I did:

```
C2/C3B5: LDA $0417 / STA $54 / STA $55 / LDX #C1F6 / JSR C2/ED31
FF/C1F6: 0C 60 43 00 2C | 0E CE BA | 0C 50 43 00 29 | 0E 60 BB
         | 08 80 93 | 0E 9F B5 | 08 80 96 | 0E B2 BC | FF
```
`C2/ED31` reads an opcode byte, gets its operand length from `FF/BD18[op]`, `MVN`s the operand to
`$7E:005B`, then `JSR ($ED59,X)` with X = **the opcode itself** (so opcodes are even).
`FF/BD18[$08] = 01` (2 operand bytes), `FF/BD18[$0E] = 01`. Handlers: `$08` -> `C2/F332`
(`LDX $5B / STX $61` = "set destination"), `$0E` -> `C2/F364`:

```
C2/F364: LDA #FF / PHA / PLB / TDC / LDX $5B / LDA $0000,X / DEC A / INX
         LDY $61 / MVN $7E,$FF
```
So the `0E` record's operand is a **length-prefixed** ROM block pointer. `FF/B59F = 0x7E`, i.e.
**126 bytes = 21 records x 6**, copied from `FF/B5A0` to `$61` = `$9380`. The 21-record count and
the 6-byte stride are therefore proven from the ROM, not merely inferred from the RAM image.
(The second pair, `08 80 96` + `0E B2 BC`, loads `FF/BCB2`+1 into `$7E:9680` - the screen's other
list, not examined.)

### AV-3. STRONGER: the value-string table decodes cleanly

The dossier decoded `FF/D3FE` "by hand". I decoded all 0x32 entries with the CT menu font mapping
(proved self-consistently by `FF/D554`: `A0`='A', `A1`='B' so `0xA0+n` = 'A'+n, `0xBA+n` = 'a'+n):

```
00 'Stereo'  01 'Battle Mode'  02 'Save Menu Cursor.'  03 'Save Battle Cursor.'
04 'Save Skill/Item Cursor.'  05 'Battle Speed'  06 'Window Color'  07 'Battle Gauge'
08 'Control Pad'  09 'Skill/Item Info.'  0A 'Battle Gauge Speed'  0B 'Battle Mes. Speed'
0C 'N' 0D 'Y' 0E..15 '1'..'8' 16 'Standard' 17 'Custom' 18 'Wait' 19 'Active'
1A 'Off' 1B 'On' 1C 'N' 1D 'Y' 1E '' 1F 'Off' 20..27 '1'..'8' 28 'On' 29 'Off' 2A..31 '1'..'8'
```
Entry `0x1E` really does point at `FF/D462`, a lone `00` terminator - **the empty string is real**,
so 2.10's "cell 5 has no value" is CONFIRMED from the ROM (the dossier only showed it in a render).
The table is exactly 0x32 entries (`FF/D3FE..D461`), so "0x32+ is past the end" is CONFIRMED.
A cross-check that also validates the polarity claims: cell 0 uses base `0x28`, cell 9 base `0x1A`,
and `0x28`==`0x1B`=='On', `0x29`==`0x1A`=='Off', so Stereo 0='On'/1='Off' while Skill/Item Info
0='Off'/1='On'. Both match 3.

### AV-4. DEFECT (FATAL for the port): "edge-triggered, no auto-repeat" is REFUTED

4.8 ends with:

> holding a direction for 30 frames [...] only the **first** frame shows a non-zero `$F0/$F1` and
> `$F4/$F5` [...] the config nav is **edge-triggered, no auto-repeat**.

and S3 turns that into `Edge-triggered only; there is no auto-repeat on this screen.`

**The hold was two frames too short.** The first repeat fires at **+32 frames**. Held to 150 frames,
`$F4/$F5` re-fires at +32 and then **every 5 frames**, and the cursor walks the whole column:

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E0054:1,7E00F0:6" --from saves/com_jogo_salvo.state --frames 260 \
  --btnscript "<P>,20:150,0:20" --diff --writer
```
```
  f167    7E0054=01*(C2:EA4A)  7E00F0=000400040004*(C2:8531)   <- the PRESS ($F0/$F1 and $F4/$F5)
  f168    7E0054=01           7E00F0=000000040000*(C2:8531)   <- held: only $F2/$F3
  f199    7E0054=02*(C2:EA4A)  7E00F0=000000040004*(C2:8540)   <- +32: $F4/$F5 ONLY  <<<<
  f204    7E0054=03*(C2:EA4A)  7E00F0=000000040004*(C2:8540)   <- +5
  f209    7E0054=04*(C2:EA4A)  ...                             <- +5
  f214    7E0054=00*(C2:EA4A)  ...   (wraps)
  f219 f224 f229 f234 f239 ...                                 <- every 5 frames, forever
```

The 8-step strips repeat too (`C2/C164` reads `$5A`, a copy of `$F5`) - hold RIGHT on cell 10:

```
  --monitor "7E0D8B:1,7E0054:1" --btnscript "<P>,80:2,0:12,100:2,0:12,80:130,0:20" --diff --writer
  f195  7E0D8B=05*(C2:C17C)   f227  =06  (+32)   f232 =07   f237 =00   f242 =01   f247 =02 ...
```

And - the other half of the asymmetry - **Confirm/Cancel do NOT repeat**, because `C2/E984` reads
`$F0` (pure edge) while `C2/E9E2` reads `$F5`:

```
  --monitor "7E0D86:1,7E0D1D:1,7E0054:1" --btnscript "<P>,100:150,0:20" --diff --writer
  f167  7E0D86=01*(C2:C025)  7E0D1D=80*(C2:E9AE)      <- ONE toggle in 150 held frames
  f168  7E0D86=01            7E0D1D=00*(C2:E98D)
```

**The mechanism (derived, `C2/84EC`/`C2/84F0`, which the dossier never opened):**

```
C2/84EC: PEA $0000 / PLD            ; DP = 0, falls through
C2/84F0: PEA $0000 / PLB / PLB / SEP #20
         LDA #01 / BIT $4212 / BNE -            ; wait out auto-joypad-read
         LDX $F2 / STX $FE                      ; $FE/$FF = LAST frame's held
         LDA $4218 / AND #F0 / STA $F2          ; $F2 = A X L R  in bits 7..4
         LDA $4219 / AND #0F / STA $F3          ; $F3 = Up Down Left Right in bits 3..0
         LDA $4219 / LSR*4 / TSB $F2            ; OR B Y Select Start into $F2 bits 3..0
         REP #30
         LDA $4218 / CMP #3030 / BNE + / JML $C0FF00      ; the reset combo
       + LDA $F2 / EOR $FE / AND $F2 / STA $F0 / STA $F4  ; NEW PRESS -> BOTH $F0 and $F4
         SEP #10
         LDX $FD                                ; the repeat countdown
         LDA $F2 / AND $FE / BEQ ->reload       ; nothing held across the boundary
         DEX / BNE ->store
         TSB $F4                                ; EXPIRED: OR the 16-bit HELD mask into $F4/$F5
 reload: LDX $FC
 store:  STX $FD / RTS
```
and the rate itself, in `C2/8473` - the menu's own **NMI/vblank** step, called once per frame
from `C2/844D` (the routine that also writes `$420C`, `$2105`, `$2100` and ticks `$0D76`):

```
C2/8473: PHP / SEP #30
         LDA $18 / BEQ + / LDA #01 / STA $00FC / STA $00FD   ; $18 != 0 -> repeat EVERY frame
       + REP #30 / PHB / PHD / PHP
         JSR C2/84EC                     ; <- the read above
         LDA $421A / STA $041F
         JSR C2/85AE
         PLP / PLD / PLB
         LDA $00F2 / AND #FFFE / BEQ ->slow    ; nothing held (Start excluded) -> rate index 1
         LDA $00F4 / BEQ ->apply               ; $F4 did not fire -> leave the index alone
         DEC $19 / BPL ->apply / STZ $19       ; it fired -> step the index DOWN, clamped at 0
         BRA ->apply
  slow:  LDA #$0001 / STA $19
  apply: SEP #30 / LDX $19 / LDA $C284D0,X / STA $00FC / STZ $18
         LDA $7E2994 / ORA #01 / PHA / AND $00F2 / CMP $01,S / BNE + / STA $83
       + PLA / PLP / RTS

C2/84D0:  05 20        <- the repeat-period table: index 0 = 5 frames, index 1 = 32 frames
```

Measured live (`--monitor "7E0018:1,7E0019:1,7E00FC:2" --writer`): `$FC` really does carry `05`/`20`
written by `C2/84B8`, and `$FD` counts down one per frame (`f168 051F`, `f169 051E`, `f170 051D`, ...).

**What the C engine must do instead of S3's "edge-triggered only":**
- keep THREE input pairs, not two: `held` (`$F2/$F3`), `newPress` (`$F0/$F1` = held & ~prevHeld),
  and `navPress` (`$F4/$F5` = newPress, plus the whole `held` pair whenever the countdown expires);
- countdown `$FD`, reload `$FC` from `{5, 32}[$19]`; reload whenever `held & prevHeld == 0`;
- `$19` = 1 while nothing is held, and is decremented (floor 0) on every frame `$F4` fires;
- the cursor mover and `C2/C164` read `$F5`/`$5A` (**repeating**); Confirm/Cancel read `$F0`
  (**never repeating**).

This is a general machine that belongs to the whole menu, not to screen 4 - every screen that calls
`C2/E984` (37 sites, ROM scan of `20 84 E9`) inherits it.

### AV-5. DEFECT: the `$F4/$F5` row of the 2.6 button-byte table is wrong

2.6 states:
```
$00F0/$00F1 = NEW PRESS   $00F2/$00F3 = HELD   $00F4/$00F5 = NEW PRESS
```
`$F4/$F5` is **new press OR the held mask on a repeat tick** (`TSB $F4` at `C2/853E`). Proven above:
at `f199` `$F0/$F1 = 0000` while `$F4/$F5 = 0004`. Everything else in that table is CONFIRMED - I
re-measured every bit individually rather than in one script:

```
for b in 10 20 40 80: --monitor "7E00F0:6" --from saves/com_jogo_salvo.state --frames 40 \
                      --btnscript "0:20,$b:3,0:16" --diff
  0x10 (Up)    f21 000800080008   f22 000000080000
  0x20 (Down)  f21 000400040004   f22 000000040000
  0x40 (Left)  f21 000200020002   f22 000000020000
  0x80 (Right) f21 000100010001   f22 000000010000
```
plus `B=08 Y=04 Select=02 Start=01 A=80 L=20 R=10 X=40` in `$F0`. The `f22` rows (only bytes 2-3
non-zero) are what actually proves `$F2/$F3` = HELD - a role the dossier's single-frame taps could
not separate.

### AV-6. DEFECT: 6.4 is refuted - the hand IS an OBJ sprite, at the nav (x,y) with ZERO offset

6.4 claims *"OAM in every `--peek` of this screen is empty (all y=224), so the hand is either a
BG-layer draw"* and leaves the offset open. The OAM in my peek is **not** empty:

```
scratchpad/menu/ct_probe.exe ... --peek saves/com_jogo_salvo.state scratchpad/menu/_avcfg0.raw \
   --btnscript "<P>"                      -> OAM idx28  x=8   y=27  tile=20 attr=30
   --btnscript "<P>,20:2,0:30"  (cell 1)  -> OAM idx28  x=8   y=59  tile=20 attr=30
```
`nav[0] = (0x08, 0x1B) = (8, 27)` and `nav[1] = (0x08, 0x3B) = (8, 59)`. **Exact, zero offset.**
The ROM path is `C2/E923`:

```
C2/E956: LDA #FF / TRB $0D9B / BNE ->E97A     ; $0D9B != 0 suppresses the hand entirely
C2/E95D: LDA $54 / JSR C2/EA27                ; X = cell*6
C2/E962: REP #20 / LDA $9380,X / STA $0770    ; <- the nav (x,y) WORD straight into OAM slot 28
C2/E96A: LDA $58 / STA $0772                  ; tile + attr
C2/E971: LDA $56 / BEQ + / LDA #40 / TSB $0773
```
`$7E:0700` is the OAM shadow (`C2/E930` parks `$0740..$0880` with `y=$E0`), so `$0770` is sprite
**28** (`(0x770-0x700)/4`). Live: `$0770..$0773 = 08 1B 20 30`, and high-OAM byte 7 = `0xAA`
(sprite 28: xhigh=0, size=large). So: **hand = OAM slot 28, tile `$20`, attr `$30`, large,
position = the nav record verbatim.** 6.4 can be closed; `$0D9E` is not in this path at all.

### AV-7. DEFECT: the "other config UI" screen ids are `$0D`/`$0E`, not `$0B`/`$0C`

6.6 and the 3 reader table both say *"menu mode with screen `$0B`/`$0C`, `C2/E1E3` / `C2/E60B`"*
and *"`C2/E6A1` (the other config UI, screen `$0C`)"*. The dispatcher indexes `C2/8292` by
`$67*2` (`C2/827F: LDA $C28292,X`), and the table reads:

```
C2/8292: CA 99  29 9C  34 AA  53 B3  35 BF  21 C6  D4 CE  A6 C7
C2/82A2: 91 82  91 82  91 82  F0 D4  19 D5  E3 E1  0B E6  0A FE
   $00 99CA  $01 9C29  $02 AA34  $03 B353  $04 BF35  $05 C621  $06 CED4  $07 C7A6
   $08 8291  $09 8291  $0A 8291  $0B D4F0  $0C D519  $0D E1E3  $0E E60B  $0F FE0A
```
So `C2/E1E3` is screen **`$0D`** and `C2/E60B` is screen **`$0E`** (`$0B`/`$0C` are `C2/D4F0` and
`C2/D519`, two different screens). Independent corroboration: the memory index already pins the
name-entry / ATB-selector shell as screen `$0D`, which is exactly `C2/E1E3`.

While re-checking that site I also confirmed the *store* is at `C2/E694`, not `C2/E6A1` (`E6A1` is
the `JSL $C70004`), and that the parameter differs between the two writers:

```
C2/C028: LDA #F3 / STA $1E00 / LDA $0D86  / STA $1E01   ; parameter 0 or 1
C2/E694: LDA #F3 / STA $1E00 / LDA $2990 / AND #08 / STA $1E01   ; parameter 0 or 8
```
A ROM scan for `A9 F3 8D 00 1E` returns **exactly those two sites**, so 6.3's "only two `$F3`
sites" is CONFIRMED - but S10's *"`$1E01 = m[0x0D86]`"* is only correct for cell 0; the other
writer posts `0`/`8` for the same setting. Whatever the SPC does with the parameter must therefore
tolerate both encodings (still unmeasured - 6.2 stands).

### AV-8. 6.1 (`LDX $51`) can be CLOSED: the writer is `C2/80FF`

The dossier says *"I did not find what writes `$51` on this screen"*. A ROM scan of bank `C2` for
every store to direct-page `$51`:

```
STZ $51 [64 51] -> 2 hits: C2/80FF, C2/F419
STA $51 [85 51] -> 0    STX $51 [86 51] -> 0    STY $51 [84 51] -> 0
```
Both writers are `STZ`, and `C2/80FF` sits in the menu-open prologue two instructions before
`STZ $56` in the straight-line run that reaches `C2/8133` (the unpack). Live confirmation - `$51`
is `0x0F` in the overworld state and is zeroed the frame the menu opens:

```
--monitor "...,7E0051:1" --btnscript "<P>,..." --writer
  f0   7E0051=0F*(C2:053A)
  f43  7E0051=00*(C2:8101)          <- the STZ at C2/80FF (writer PC reported +2)
  ...  7E0051=00 for every frame of the button-editor run
```
So within the menu `$51` is provably 0 (no bank-`C2` code can make it non-zero), and porting the
scan start as a literal 0 is safe. Keeping `m[0x51]` is still the more faithful port.

### AV-9. DEFECT (minor): 2.10's `$0410` sentence has the data flow backwards

2.10 says `$0410` *"is only restored via `$2993+8` and `C2/C54F`"*. `C2/C54F`->`C2/C555` copies
**`$0408..$0410` -> `$2993..$299B`**, never the reverse - so nothing restores `$0410` after a load.
The practical impact is nil, and I can now say why: **`$299B` has zero readers in the entire ROM.**
An opcode-aware scan (all abs / abs,X / abs,Y / long forms) over the whole 4 MB gives

```
$2993:6  $2994:3  $2995:5  $2996:8  $2997:2  $2998:3  $2999:4  $299A:4  $299B:0
```
and every indexed site is bounded at row <= 7 (`C2/C2BD` = `00..07`, `C2/E9D9` uses indices 0/1,
`C2/C58A` loops 0..5). So row 8 of the button map is dead storage.

That scan also turns up the mechanism the 3 table hand-waves as *"every field/battle input site
that consults `$2993..$299B`"*. It is **one** routine, `C2/8555`, called twice from `C2/8545`
(`LDY #00 / TYX`, then two `JSR`s; the routine ends `INX INX`, so pass 1 runs with X=0 and pass 2
with X=2):

```
C2/8555: STZ $F6,X
         LDA $F0,X / AND $2998,Y / CMP $2998,Y / ROR $F6,X     ; row 5  Warp   -> bit0
         ... $2996 (row 3 Dash) -> bit1 ... $2997 (row 4 Map) -> bit2
         ... $2994 (row 1 Cancel) -> bit3 ... $2999 (row 6 page-R) -> bit4
         ... $299A (row 7 page-L) -> bit5 ... $2995 (row 2 Menu) -> bit6
         ... $2993 (row 0 Confirm) -> bit7
         LDA $F1,X / STA $F7,X / INX / INX / RTS
```
i.e. it builds a **LOGICAL** button pair (d-pad copied through unchanged) with the fixed layout
`bit7 Confirm | bit6 Menu | bit5 page-L | bit4 page-R | bit3 Cancel | bit2 Map | bit1 Dash |
bit0 Warp`. Pass 1 (X=0) reads the **new-press** pair `$F0/$F1` into `$F6/$F7`; pass 2 (X=2) reads
the **held** pair `$F2/$F3` into `$F8/$F9`. (Note it does *not* remap `$F4/$F5`, the repeating
pair.) Those are the bytes the rest of the game reads, and they belong in the RAM contract.

### AV-10. 2.8 CONFIRMED behaviorally: the screen's "A" really is the *configured* Confirm

The dossier proves this from the disassembly only. I drove it end to end: remap row 0 (Confirm)
from `A` to `B` inside the button editor, leave, and then use the screen.

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E0067:1,7E0054:1,7E2993:9,7E0D86:12,7E2990:3" --from saves/com_jogo_salvo.state \
  --frames 400 --btnscript "<P>,80:2,0:12,20:2,0:12,20:2,0:12,20:2,0:12,100:2,0:16,\
80:2,0:16,40:2,0:20,40:2,0:14,01:2,0:20,100:2,0:40" --diff --writer
```
```
  f223  7E0054=0F*(C2:C11A)  7E0D86=0001...*(C2:C111)              A on cell 8 -> Custom, editor
  f259  7E0054=08*(C2:BF94)  7E2993=088040800204102040*(C2:C578)   LEFT commits: Confirm=08 (B),
                                                                   Cancel AND Dash inherit 80 (A)
  f281  7E0054=03*(C2:EA4A)                                        LEFT off cell 8 -> cell 3
  f297  7E0D86=...0101...*(C2:C058)                                *** B *** toggles $0D8D  = Confirm
  f319  7E0067=00*(C2:9A9E)  7E2990=D4E001*(C2:93A6)               *** A *** exits + PACKS = Cancel
```
Pressing **B** now toggles a checkbox and pressing **A** now leaves the screen. The mechanism is the
`$2993`/`$2994` RAM bytes, exactly as 2.8 says - no hard-coded button anywhere.

### AV-11. Reproductions: the dossier's oracle runs are honest

I re-ran the load-bearing measurements from scratch and got **frame-for-frame identical** output,
including the writer PCs:

| dossier | my re-run | verdict |
|---|---|---|
| 4.1 (working block, `$0408`, `$2990-92`, `$0DCE/0DCF`, `$0D13/1D/1E/1F`) | `--peek` -> `_avcfg0.raw.wram` | identical |
| 4.2 (`$7E:9380` == `FF/B5A0`) | byte-compared in Python: `nav == FF/B5A0 : True` | identical |
| 4.3 (5 toggles + B pack, f167..f307, `$2990` -> `24 60 01`) | identical frames AND writer PCs | identical |
| 4.5 (cell 7 row, cell 8 editor, f195..f301) | identical | identical |
| 4.6 (editor commit + the `80`->`08` swap onto rows 1 AND 3 at f317) | identical | identical |
| 4.7 (X/Select/Start/L/R inert; one row only) | identical - `$0067` never leaves `04` | identical |
| 4.9 (`$0DCE/$0DCF` on cells 2/7/8/9 = `020C 0720 0816 091B`) | identical | identical |
| 4.13 (10 `JSR C2/EAC2` + 1 `JMP`, zero `C2/EACC`) | same 11 sites, zero buzz | identical |

### AV-12. STRONGER: the checkbox mirror is now measured directly, not inferred

4.10 grades the `$0F0D/$0F0E/$0F0F` overlap through a *render diff*. The `--peek` `.wram` settles
it outright. On the freshly-built screen with `$0D86..$0D8F = 00 00 00 01 01 04 00 00 01 01`:

```
$0F00..$0F13 = 00 00 00 00 00 00 00 00 00 | 00 00 00 01 | 00 00 00 | 00 01 01 | 00
               \___ button working copy ___/  0F09..0F0C   0F0D-0F   0F10..0F12
```
`$0F09..$0F0C` and `$0F10..$0F12` mirror `$0D86..$0D89` / `$0D8D..$0D8F` exactly, while
`$0F0D/$0F0E/$0F0F` are **0** although `$0D8A/$0D8B/$0D8C` are `01/04/00` - the `STZ`s at
`C2/C44B..C2/C451`. 2.9 CONFIRMED from RAM. The same dump also shows `$0F00..$0F08` all zero,
confirming the button working copy is seeded only on entry to the editor (2.6), and `$0D91 = 00`.

### AV-13. Smaller defects and wording hazards

1. **S7 over-reaches.** *"Nothing else exits: X/L/R/Select/Start must be inert on screen 4."*
   START is **not** inert - in substate 2 it restores the factory button map (`C2/C186:
   LDA $F0 / BIT #01`). S6 has it right; S7's blanket sentence contradicts S6. Note also that this
   START test reads the **physical** `$F0` bit0, not the remapped map (Start is not in `C2/C207`,
   so it is unassignable by construction).
2. **`$7E:0068` in the 3 RAM contract is task-local.** At a frame boundary it reads **0**; the
   live substate is slot 1's saved `$68` at **`$7E:00BE`** (the shell dossier's rule). Measured:
   with the cursor inside the gauge row, `7E0068=00` while `7E00BE=03`. Three of the dossier's own
   monitor runs asked for `7E0068:1` and it never once appeared in the `--diff` output; the author
   did not notice. Any port-vs-oracle check must watch `$00BE`, not `$0068`.
3. **`CD/01A3` is off by two.** The Battle-Mes.-Speed reader is `CD/01A5`
   (`AD 91 29 4A 4A 4A 29 07 AA AF...`); `CD/019D..01A4` is the 8-byte dwell table
   `20 40 60 80 A0 C0 E0 FF` (which the dossier separately cites correctly).
4. **2.3 says "All five share one shape" over a six-row table** (cells 0,1,2,3,4,9). Cosmetic.
5. **`C2/C511`'s instruction is not `LDA $0D8C`.** It is `LDA $0D8B / AND #$0700` with a 16-bit A -
   the word load picks up `$0D8C` as the high byte and the mask isolates its low 3 bits. The
   dossier's *effect* (`FF/(9E10 + ($0D8C&7)*0x280)`, `0x281` bytes, to `$7E:8800`) is exactly
   right; only a porter transcribing the opcode would trip.
6. **6.5's phrasing.** `C2/C41B`'s entries and `C2/C456`'s `$61` are absolute WRAM addresses in
   one address space, so "cannot be in the same linear buffer under any single base" is not the
   right framing; what is true is that the screen issues **two** tilemap uploads
   (`C2/C3D4: LDX #$FBE3 / JSR C2/8385` and `C2/C3DA: LDX #$FBFF / JSR C2/8385`) and only the
   `$FBE3` one is re-queued per frame. Still OPEN, still out of scope.
7. `C2/C41B` is printed as byte pairs (`CA2E D631 ...`). The byte sequence is right, but as
   little-endian words the entries are `2ECA 31D6 30CA 2FCA 30D6 2ED6 2FD6 31CA 32CA 32D6` - worth
   writing that way, since those are the addresses the code stores to. The cell mapping the
   dossier derives from them is CONFIRMED.

### AV-14. Also STRONGER: the pack/unpack and defaults were re-derived, not trusted

`C2/934F` and `C2/92F4` were re-disassembled instruction by instruction and the three bit formulas
re-derived from the `LSR/ROR` and `ASL/ROL` chains; they match. Independently, the live
`$2990/$2991/$2992` was recomputed from `$0D86..$0D90` in three different runs (`C4 A0 01`,
`24 60 01`, `D4 E0 01`) and matched the ROM's own value every time. The new-game seed
(`C2/95D6..960A`, which `dis.py` mis-decodes because of the `MVN`s) really is `12 bytes C2/FCA6 ->
$2990` and `9 bytes C2/FCA9 -> $0408`, and running `C2/FCA6 = 84 A0 01` through the unpack gives
exactly the eleven defaults 2.10 lists.

### AV-15. NEW OPEN / BLOCKED items this pass raises

1. **The `$18` fast path in `C2/8473`** (`$18 != 0` -> `$FC = $FD = 1`, i.e. a repeat on *every*
   frame) is derived but never observed: `$7E:0018` reads `$C2` at every frame boundary because
   `C2/8273` uses the same direct-page byte as the bank of the `[$16]` screen-handler pointer, and
   `C2/84BB` `STZ $18` clears it inside `C2/8473`. Whichever menu screen sets `$18` deliberately
   (candidates: `C2/0A76`, `C2/6AF5`, `C2/F578`, `C2/94D3`, `C2/77EF`) was not identified.
   *Unblocked by:* a `--hookpc`-style trace of bank `C2` (the existing `--hookpc` is bank-`C0`
   only, the same wall 6.2 hit).
2. **`$7E:0083`.** `C2/84C4..84CB` sets it when Cancel **and** Start are held together
   (`LDA $7E2994 / ORA #01 / AND $00F2 / CMP`). I did not find its reader, and it is *not* the
   `$0D83` forced-B of `C2/E9CD` (different address). It is the obvious candidate for 6.8's
   "forced menu close", so 6.8 should be re-opened around `$0083`, not only `$0D83`.
3. **`C2/C20F` pass 2 with a row-7 swap.** `C2/C26C` writes the swapped row index to `$04`, and
   row 7 (page-L) *is* in rows 0/1/6's conflict lists, so `$04` can be 7. `C2/C20F` then re-enters
   `C2/C23A` with X=7, and `LDA $C2C2C5,X` (X doubled to 14) reads **past the 7-entry pointer
   table** into the first list's own bytes (`C2/C2D3` = `07 01` -> pointer `$0107`), after which
   `LDA $C20000,X` takes a length from `C2/0107` and `C2/C26C` indexes `$0F00,Y` with arbitrary
   bytes - i.e. it can write outside the 9-byte map. I did not reach the state that triggers it
   (it needs an assignment whose *only* conflicting holder is row 7). *Unblocked by:* a driven
   run that assigns `L` (`0x20`) to Confirm inside the editor and dumps `$0F00..$0FFF`.
   A faithful port must reproduce whatever the ROM does here, so this needs settling before S6 is
   implemented as written.
4. **6.7 (`$0D8A == 3`) and 6.2 (the SPC side of `$F3`) stand unchanged** - I found no new lever.


### AV-16. The engine has MOVED since this dossier was written - and it inherited the one defect

The dossier's opening gap (*"the CONFIG screen is read-only - no setting can be changed"*) and S1's
*"add `cfgCursor` / `cfgSubstate`"* are now stale: `src/menu.c` already carries
`CtMenuConfigFrame` (line 1955), `CtMenuConfigNav`, `CtMenuConfigRebuildLive`, `CtMenuConfigPack`,
`cfgAssign`, and the ROM constants `ROM_CFG_NAV 0x3FB5A0` / `ROM_CFG_BTNCYCLE 0x02C207` /
`ROM_CFG_CONFLICT 0x02C2C5` / `ROM_CFG_DEFAULT 0x02FCA6`, and `src/field.c:7148` already has
`menuCfgCur` / `menuCfgSub`. The implementation reads as a faithful transcription of 2.1-2.6
(I checked `mCfgUnpack`'s eleven bit extractions against `C2/92F4` - all correct).

**But it took S3 literally.** `src/field.c:8557`:

```c
cio.dir = (rHeld && !prevRBtn) ? 0 : (lHeld && !prevLBtn) ? 1     // C2/E9F4's own scan order,
        : (dHeld && !prevDBtn) ? 2 : (uHeld && !prevUBtn) ? 3 : -1;  // Right, Left, Down, Up
```

That is `held && !prev` - a pure edge, i.e. exactly the "no auto-repeat" AV-4 refutes. Against the
oracle, holding a direction on screen 4 steps the cursor once in the port and **once + every 5
frames after a 32-frame delay** in the ROM. The scan ORDER is right; the EDGE is wrong. The fix is
not in `menu.c` - `CtMenuConfigFrame` already takes `dir` as a parameter - it is in whatever feeds
`io.dir`, and it must become the `$F4/$F5` pair of AV-4 (shared by every menu screen, since all 37
`JSR C2/E984` sites read `$F5` through `C2/E9E2`).

Two smaller notes on the same call site, for whoever picks this up:

- `cio.start = 0` with the comment *"Belthasar does not bind START"*, so `C2/C183`'s
  restore-factory-defaults is currently unreachable in the port. That is an honest gap, correctly
  labelled, but it means S6's START branch is dead code today.
- `cio.a` / `cio.b` come from `aHeld` / `bHeld`, which are physical keys
  (`src/field.c:8217-8218`, Z/Space/Return and X/Backspace). The ROM reads Confirm/Cancel through
  `$2993` / `$2994` (AV-10 drives this end to end: after remapping, physical **B** toggles the
  checkbox and physical **A** exits). This is a *host input-layer* boundary that applies to the
  whole menu, not a screen-4 bug - but it is the mandate's "a heuristic where the ROM branches on
  a RAM byte" shape, and this dossier's 2.8 is the right place for it to be recorded.

I did not modify any file under `src/`.

### AV-17. VERDICT

The dossier is, on the ROM side, **unusually accurate**: every table, every routine body and every
address I re-read matched, and every oracle run I repeated reproduced frame-for-frame. Its cell
dispatch, per-cell arithmetic, nav table, sub-mode machinery, conflict/swap algorithm, pack/unpack
and persistence story can be implemented as written.

It has **one fatal hole**: `4.8`/`S3` declare the screen edge-triggered with no auto-repeat, on a
30-frame hold that stopped 2 frames before the first repeat. The real machine
(`C2/84EC`/`C2/84F0` + the `{5, 32}` table at `C2/84D0`) auto-repeats the d-pad - 32 frames, then
every 5 - while leaving Confirm/Cancel edge-only. Implementing S3 as written would make every menu
screen in the port feel wrong, not just this one.

Two further claims are refuted (`6.4`'s empty OAM; the `$0B`/`$0C` screen ids), one is backwards
(`$0410`), one is over-broad (`S7` vs START), and three open items can be closed or narrowed
(`$51`, `6.4`, the `$299B` dead slot). Nothing in the dossier stops mid-sentence and no cited
routine turned out to be undisassembled - the one routine family it *names but never opens*
(`C2/ED31` + `FF/BD18`, and the input path behind `$F0..$F5`) is exactly where its single fatal
error is hiding.

## 8. PORTED 2026-09-24: the screen's frames (tools/menu_audit_cmp.py tour f877-f1130 = 0 frames differ)

* **The build frame keeps the ribbon's colour window.** Substate 0 (C2/BF3D -> C2/C3B5) never writes `$0D13`; substate 1
  (C2/BF45 `LDA #5D / STA $0D13`) does, every frame. So on the build frame HDMA channel 1 still carries the ribbon's
  window table (the builder C2/9B92: `$FF:BD2E` programs channel 1 as mode 4 onto `$2126-$2129` from `$9080`; the NMI
  thunk copies `$9750` there; the table is raw at FF/BAAA: W1 = 16..239, W2 = 64..191 on lines 96..143) and colour math
  stays off under the command box (W1 XOR W2): MEASURED with `--hdma`, W2 64..191 on that frame, 80..0 from the next;
  the Config tiles there are brighter by the ramp for one frame (tour f907). The Equip (C2/A11A), Item (C2/ADD0),
  Tech (C2/B979), Form (C2/CAF3) and Save (C2/D0E5) builds all write `$0D13` in their first substate.
* **The bottom bar arrives with substate 1** (C2/BFD4 -> C2/C4BC -> C2/F943, every frame) and is TYPED: C2/F943 caches
  the name id (`$0DCE`) in `$0D77` and the value id (`$0DCF`) in `$0D78` and spawns its own task (C2/F9B2 -> C2/FAA8,
  `$0DC9` = 1): the name one character a frame, then -- in the step that reads its terminator -- the value, placed from
  the whole name's pen. MEASURED: 'S' at f908 .. 'Stereo' at f913, 'O' at f914, 'n' at f915. A value change alone
  (`$00C0` idle, `$0DCF` != `$0D78`) clears and retypes the value.
* The icon grid (menu_configgrid.h) had been captured with v*255/31; the PPU expands (v<<3)|(v>>2).

## 9. PORTED 2026-09-24: the WINDOW COLOUR (the menu's windows follow cell 6 and the saved colour)

Graded by `menu-config-window-colour-vs-oracle` (the config drive: 0 of 381 frames, `$2990-92` = CC A2 01 on both
sides), `menu-window-colour-saved-vs-oracle` (the wincolour drive: `$2991` = A3 poked, 0 of 211) and
`menu-window-colour-dim-rebuild-vs-oracle` (the wincolour2 drive: two changes inside the menu, 0 of 821); ten mutations,
all killed. The port drew every menu in colour 0 whatever the setting -- the strip stepped, the windows never changed.

* **C2/C511** (cell 6's edit every frame, C2/C13D; and every open, C2/983F `LDA $2991 / AND #07 / STA $0D8C / JMP
  C2/C511`): `MVN` 0x281 bytes FF/(9E10 + v * 0x280) -> `$7E:8800`; C2/962E sets plane 3 of each of the 20 tiles to
  p0 | p1 | p2 (every inked pixel takes colours 8-15); the list C2/C548 = `C0 3D 00 88 7E 80 02` uploads them to VRAM
  word 3DC0 = tiles 0x3DC-0x3EF, the 9-slice window's frame and fill. Then **C2/ED08**: 16 bytes FF/(B210 + v * 16) ->
  `$7E:9490 + ($0D48 & 1C) * 8`, i.e. CGRAM row ($0D48 & 1C) / 4 colours 8-15; C2/9839 `LDX #0010 / STX $0D47` makes it
  row 0. MEASURED: VRAM 3DC0.. and `$9490..` are exactly those bytes at colours 0 and 2; no other row moves. The captured
  chrome IS colour 0 (tiles and row 0's colours 8-15 byte-identical to the decode).
* **The dim row, C2/A0E7**: `LDA $9480,X / LSR A / AND #3DEF / STA $0000,Y`, X from `$51` = 0 to 0x30, Y = `$9500`:
  row 0 and row 1's first half, halved, into row 4 / row 5's first half. Four callers only -- the Equip tab's builder
  (C2/A0E2, after its yields), the Form tab's (C2/CA27, before them), the Form screen's build (C2/CB71) and the shop's
  (C2/DB46) -- which are exactly the views whose maps carry palette 4. MEASURED (colour 3): row 4 = row 0 >> 1 from the
  open's Equip build, zero before it (C2/940D's fill); a change inside Config moves row 0, not row 4.
* The port: `CtMenuWindowColourLoad(h, v, dimV)` decodes the tiles and the two rows from the ROM; field.c keeps
  `menuWinColour` (the last C2/C511: the open, cell 6) and `menuWinDim` (the last C2/A0E7: the four sites above).
* Found on the way: through the next tab's builder yields the ribbon keeps the Form hover up, and the port dropped its
  C2/CC4F map (the dim predicate read the NEW tab). Invisible in colour 0; the capture's roster showed in colour 3.

**Open.** The field's text window reads the same colour -- C0/6E5C loads FF/(C0:6EA0[$2991 & 7]) (= 9E10 + v * 0x280)
to VRAM $1F00 (0x200 bytes) and $4FC0 (0x80), and C0/70E9 puts FF/B210 + v * 16 + 2 into CGRAM row 0 -- but the port's
field window is the baked colour-0 bitmap CT_ASSET_WINDOW, so a changed colour does not reach the dialogue boxes yet.
The Save screen's slots draw in each save's OWN colour (C2/D329: `$0D8C` = `$0D79,X`, C2/ECDB -> ED08 on rows 1-3,
C2/D362 restores `$2991 & 7`); the port's captured rows are colour 0.

## 10. PORTED (2026-09-25): "Save Menu Cursor?" -- the cursor memory, and the '?' in its name

tools/menu_audit_cmp.py cursormem on party3_field, the logical oracle (checks `menu-config-cursor-memory-toggle-vs-oracle`,
`menu-ribbon-reopens-on-remembered-tab-vs-oracle`):

* **Cell 2 is `$0D88`** (C2/BFFE's handler table: cell 2 -> C2/C038, `LDA $0D88 / INC / AND #01 / STA $0D88 / STA
  $0F0B`); C2/9350 packs it as bit 5 of `$2990` (MEASURED C4 -> E4). The Config grid's columns are cells 0-4 and 5-9:
  Right crosses a column (0 <-> 5), Down walks it.
* **What it does.** C2/99D9 at the menu's open: `LDA $0D88 / BNE` -- off: `STZ $0411 $0412 $0413 $041A` (the ribbon opens
  on Equip, the member cursor on the lead); on: they stand, so the menu reopens on the tab it closed on. And C2/9A48 on a
  ribbon move: off: `STZ $0415 $0416 $0412 $0413 $0417 $0418 $0419 $041A` (every screen's cursor forgotten); on: kept.
  C2/9A45 stores the new tab in `$0411` on every move. MEASURED: closed on Config with the memory on, the menu reopens on
  option 3 (`$0411` = 3); the reopened menu 0 frames. The port opened on option 0 always and reset only its own member
  cursor on a move.
* **The '?'.** The three cursor settings' names end in 0xDF (FF:D476 `.. CB DF 00` = "Save Menu Cursor?") -- the font's
  '?'. The name decoder skipped 0xDF as a "cursor icon"; the oracle's bar draws it (f187-f240).

## 11. PORTED (2026-09-25): the sub-editors worked -- the pad editor's labels and arrows, the 3-option row's mark

`tools/menu_audit_cmp.py cfgpad` (the editor entered, rows walked) and `cfgpad2` (the Menu row re-assigned twice, a
commit, Dash re-assigned, the exit; then the 3-option row entered, stepped, left) -- f20-f686 0 frames differ
(regress `menu-config-pad-editor-and-option-row-vs-oracle`). Three things were missing:

* **The labels are the LIVE map.** `C2/C57A` (the tail of every `C2/C54F`, and `C2/C349` in the build) writes the six
  label words from `$2993+row` through `C2/C5B3` (SEC, `INX INX / ROL A` until the carry: X = 2 x (7 - bit)) and
  `FF/D554`, at `$7E:2E00 + C2:C5A7[row]` (`0374 03F4 0474 04F4 0574 05F4`: tile column 26, every second tile row),
  and `00FF` into the two words after each. The port drew a hardcoded `A B X B S Y` (the factory map) -- a Custom
  map showed the wrong letters.
* **`C2/C5C7`, the tail of an assignment** (`C2/C20F ... JMP C2/C5C7`, its only caller): `MVN $0408..$040F ->
  $9890..`, `$9890[$54 - 15] = FF` (so the row being edited always differs), then for rows 5..0: working `$0F00[row]`
  equal to `$9890[row]` -> `FFFF` in both words after the label, else `0031` (the arrow) and the working mask's glyph.
  The screen shows `X->Y` on the edited row and `Y->X` on the row the conflict pass handed the old button to, until
  a row change / the exit commits and `C2/C54F` -> `C2/C57A` lays the plain labels again.
* **Tile 0x31 is not in FF/8C60.** The menu program's graphics build `C2/968D` (from the init chain `C2/8096`, every
  `C2:8000` entry) copies `FF/8E60` (the font from tile 0x20) to `$7E:5000` and then **`FF/C9FC` (0x100 bytes) to
  `$7E:5100` = tiles 0x30-0x3F** -- `C2/9542` is a plain DMA from bank FF into WRAM (`$4302/$2181/$4305`, B-bus
  `$2180`), not a decompressor. MEASURED: the oracle's menu VRAM (BG3 2bpp at `$E000`, BG1 4bpp at 0) holds FF/C9FC
  at tiles 0x30-0x3F, FF/8C60 everywhere else from 0x20; the battle HUD's VRAM is the plain FF/8C60. The port's menu
  hosts now draw with that font (`field.c` `menuFontC2`; `CtMenuFontOverlayC9FC`), the battle HUD keeps the plain one.
* **The checkboxes are drawn state.** `C2/C3E4` (the ten 2x2 blocks at `C2:C41B[i]`, tile `$1530 + 4 x ((C2:C42F[i] ^
  $0F09[i]) & 1)`) runs from `C2/BFD4` after the cell's handler and from the build (`C2/C340`) -- never from the
  substate-3 / substate-2 handlers. So `C2/BFC5`'s `STZ $0F0D` (leaving the 3-option row) leaves cell 7's mark on
  screen until the next frame's `C2/BFD4`: the cartridge showed it one frame longer than the port, which read
  `$0F09` live. The port now keeps the words where the ROM does and the render reads them.

## 12. PORTED (2026-09-25): the bar icon on the strips, and every cell graded

`C2/C4BC` lays the bottom bar's 2x2 icon from `FF/D562[$0417]` (`153C 1544 1558 155C 1960 1538 1548 1554 154C 1540
1538 1538`): cells 10 and 11 -- the Battle Speed / Message Speed strips, where `C2/BFDA` leaves `$0417` = the strip --
carry cell 5's word 1538 (the speed icon). The port drew no icon for a bar >= 10. `tools/menu_audit_cmp.py cfgall`
(every cell toggled, both strips stepped, the colour edit) 0 of 801 frames differ and `$2990-92` after the close the
cartridge's (regress `menu-config-every-cell-vs-oracle`).
