# The rest of the SAVE screen — overwrite prompt, `$299E`, the post-save state, and the real text engine

Companion to `disasm/notes/save_write.md` (the write itself: `C2/CF37` → `FF/F9FB`, the record table,
the carry-propagating checksum) and `disasm/notes/menu_sfx.md` (the mailbox / `$0D76` throttle).
**Nothing derived there is re-derived here**; where this pass *contradicts* an earlier note the
contradiction is called out with a ⭐ and the measurement that settles it.

Everything below was read out of the cartridge with `tools/dis.py` or measured on the LakeSnes oracle
(`scratchpad/menu/ct_probe.exe`) **in this pass**. Every command is pasted verbatim in §6.

---

## 1. VERDICT

The overwrite prompt is **not** the field's decision box and **not** a new screen. It is screen 6's own
sub-state 2, and it is built entirely out of the menu's **generic display-list interpreter `C2/ED31`**
running two bank-`$FF` lists: `$FF:C16F` opens it, `$FF:C191` closes it. Opening = (a) back up the
28×12-tile region of the BG2 chrome buffer `$7E:3E00` into the off-screen area at tile (2,32), (b) draw
an 8×10-tile window at tile **(22,2)**, (c) re-arm three HDMA channels; closing = copy the backup back.
**The prompt's text is never drawn at prompt time** — `Erase old save? / Yes / No` (string id `$0011`,
`$FF:C787`) is written into the BG1 tilemap buffer `$7E:2E00` at tile (23,2) when the *slot list* is
built (`C2/D0E5` → `$FF:C138`), and it is invisible only because a per-scanline `$212C` HDMA keeps BG1
off for lines 0..128; the prompt frame simply stops running that channel, so BG1 shows everywhere.
The Yes/No cursor is the same `$7E:9380` node graph as the slot list — nodes **3 (Yes, OAM 184,63)** and
**4 (No, OAM 184,79)** — with `$54` pre-seeded to 3. `A` saves **only** when `$54 == 3`; anything else,
and `B`, cancel. After a save (empty slot *or* Yes) the ROM **stays on screen 6 sub-state 1** — it never
returns to the ribbon and never closes the menu. `$7E:299E` is the playthrough's **file id**, seeded on
new game from the SRAM allocator `$30:7FE1`; the `FF/FA6B` compare bumps the allocator exactly once per
playthrough. And ⭐ **`C2/EF65` is not a VWF** — it is a fixed 8×8 tilemap writer (tile index == the
character code). The real proportional renderer is **`C2/5DC4`**, advance table **`$C2:60E6`**.

**What the C engine must do:** on `A` over an occupied slot, enter a Save-screen-local sub-state 2 that
draws an 8×10-tile window at tile (22,2) over the slot list, reveals the pre-written text
`Erase old save?` / `Yes` / `No` at tile (23,2)/(23,4)/(23,6)/(23,8)/(23,10), puts the hand at
(184,63) with `$54 = 3`, lets Up/Down toggle 3↔4, runs the *same* `CtMenuSaveWrite` + jingle on `A`
when `$54 == 3`, and on every other exit restores `$54` from the saved slot and returns to sub-state 1
without leaving screen 6.

---

## 2. ROM TRACE

### 2.1 Screen 6's four sub-state handlers (`C2/CED4`)

`d[0x2CED4:0x2CEDC]` = `dc ce fd ce 61 cf 92 cf` → `$CEDC`(0) `$CEFD`(1) `$CF61`(2) `$CF92`(3).
(`dis.py` mis-decodes `2CED4` as code — it is data.) Screen `$0B`'s table `d[0x2D4F0:0x2D4F8]` =
`f8 d4 06 d5 61 cf 92 cf` **shares entries 2 and 3 verbatim** — a port must share, not copy.

`python tools/dis.py 2CED4 0x100 1 0`:

```
  2CEDC: LDA $0D34 / ORA $0294 / ORA $0299 / ORA $029E / AND #40   ; sub 0 = the entry gate
  2CEEA: BEQ  ->2CEF5
  2CEEC: STZ $67 / LDA #01 / STA $68 / JMP C2/EACC                 ; refused -> ribbon + buzz
  2CEF5: JSR C2/EAC2 / INC $68 / JMP C2/D0E5                       ; allowed -> blip, sub 1, build

  2CEFD: JSR C2/E984 / BIT $0D1D                                   ; sub 1 = the slot list
  2CF03: BMI ->2CF0A          ; A
  2CF05: BVS ->2CF5E          ; B  -> JMP C2/9A98 (ribbon)
  2CF07: JMP C2/CFA2          ; neither -> cursor commit + redraw
  2CF0A: LDX $79
  2CF0C: LDA $0D49,X          ; the OCCUPANCY byte
  2CF0F: BEQ ->2CF1E          ; EMPTY -> save now
  2CF11: JSR C2/EAC2          ;   blip id $00 (THROTTLED)
  2CF14: LDA $54 / STA $0F00  ;   stash the slot for the cancel path
  2CF19: INC $68              ;   -> sub-state 2
  2CF1B: JMP C2/D4BB          ;   = OPEN THE PROMPT
  2CF1E: JSR C2/CF37          ; THE SAVE (jingle + FF/F9FB)
  2CF21: JSR C2/D32C          ; --- the POST-SAVE redraw, see 2.6 ---
  2CF24: LDX #FC0D / JSR C2/8385
  2CF2A: LDX #CF30 / JMP C2/8385

  2CF61: JSR C2/E984 / BIT $0D1D                                   ; sub 2 = THE PROMPT
  2CF67: BMI ->2CF75          ; A
  2CF69: BVS ->2CF88          ; B  -> cancel
  2CF6B: LDA $54 / CMP $81 / BEQ ->2CF74 / JSR C2/EAC2             ; cursor moved -> blip
  2CF74: RTS                                                       ; (no per-frame redraw)
  2CF75: LDA $54 / CMP #03
  2CF79: BNE ->2CF88          ; NOT node 3 -> cancel  (so A on "No" == B)
  2CF7B: JSR C2/CF37          ; THE SAME SAVE ROUTINE
  2CF7E: LDA #01 / STA $68 / JSR C2/D4D5 / JMP C2/CF21             ; sub 1, close prompt, redraw
  2CF88: JSR C2/EAC2 / LDA #01 / STA $68 / JMP C2/D4D5             ; cancel: blip, sub 1, close
```

⚠ Sub-state 3 (`$CF92`) is unreachable on screens 6/11 — settled in `save_write.md` §3.5, not re-derived.

⚠ The dispatcher `C2/8275-828E` does `REP #30` then only `SEP #20`, so `LDX $79` at `CF0A` runs with
**X 16-bit**: the slot index is the *word* `$79/$7A`. `$7A` measured 0 throughout.

### 2.2 `C2/D4BB` — OPEN the prompt, and `C2/D4D5` — CLOSE it

`python tools/dis.py 2D4BB 0x60 1 0`:

```
  2D4BB: PHP / SEP #20
  2D4BE: LDA #03 / STA $54          ; cursor pre-seeded to node 3 = YES
  2D4C2: LDX #C16F / JSR C2/ED31    ; run display list $FF:C16F
  2D4C8: LDA #75 / STA $0D13        ; arm VBlank job mask $75   (NOT a message id -- see 3.3)
  2D4CD: LDX #FBFF / JSR C2/8385    ; queue the VRAM upload descriptor $C2:FBFF
  2D4D3: PLP / RTS

  2D4D5: PHP / SEP #20
  2D4D8: LDX #C191 / JSR C2/ED31    ; run display list $FF:C191  (the RESTORE)
  2D4DE: LDA #59 / STA $0D13        ; back to the slot-list job mask
  2D4E3: LDA $0F00 / STA $54        ; cursor back to the slot that was pressed
  2D4E8: LDX #FBFF / JSR C2/8385
  2D4EE: PLP / RTS
```

### 2.3 `C2/ED31` — the menu's display-list interpreter (this is the whole prompt mechanism)

`python tools/dis.py 2ED31 0x40 1 0`:

```
  2ED31: PHB / PHP / SEP #20 / REP #10
  2ED37: LDA #FF / PHA / PLB        ; DB = $FF -- the lists live in bank $FF
  2ED3B: TDC
  2ED3C: LDA $0000,X                ; the command byte
  2ED3F: BMI ->2ED56                ; >= $80 (the lists use $FF) -> END
  2ED41: INX / TAY
  2ED43: LDA $BD18,Y                ; A = (parameter count - 1) from $FF:BD18[cmd]
  2ED46: PHY / LDY #005B
  2ED4A: MVN $7E,$FF                ; copy A+1 parameter bytes to $7E:005B.. (DP $5B..)
  2ED4D: PLY / PHX / TYX
  2ED50: JSR ($ED59,X)              ; dispatch through $C2:ED59[cmd]  (cmds are EVEN)
  2ED53: PLX / BRA ->2ED33
```

Parameter-count table `d[0x3FBD18:0x3FBD24]` (even indices):

| cmd | params | handler | what it does |
|---|---|---|---|
| `00` | 4 | `C2/ED77` | **FILL** buffer `$61` with word `$5D`, `$5B` bytes |
| `02` | 6 | `C2/EF05` | **BLOCK COPY** tilemap rect: src pos `$5B`, dst pos `$5D`, w `$5F`, h `$60` |
| `04` | 6 | `C2/EDAA` | **DRAW WINDOW**: style `$5B`, palette `$5C`, pos `$5D`, w `$5F`, h `$60` |
| `06` | 5 | `C2/EF48` | **DRAW STRING**: string id `$5B`, pos `$5D`, flags `$5F` |
| `08` | 2 | `C2/F332` | `$61 := $5B` — set the destination buffer base |
| `0A` | 2 | `C2/F364` | (same handler as `0E`) |
| `0C` | 4 | `C2/F337` | **PROGRAM AN HDMA CHANNEL**: `Y=$5B` = `$43x0`; `$43x4`=`$43x7`=`$7E`; `$43x0/1` := `$5D`; `$43x2/3` := `$61` := `$9000 + ((x0 & 0xF0) << 3)` |
| `0E` | 2 | `C2/F364` | **LOAD A BLOB**: length byte at `$FF:$5B`, then that many bytes → `$7E:$61` |
| `10` | 5 | `C2/F378` | (tilemap rect op, not used by screen 6) |
| `12`/`14` | 5 | `C2/EDB0`/`EDB6` | window sub-ops |

`C2/ED90` is the shared address maths: `addr = $61 + (pos>>8)*64 + (pos & 0x1F)*2` — a plain 32-entry-wide
tilemap, 2 bytes per entry.

### 2.4 The three lists screen 6 runs

**`$FF:C138` — build the slot list (`C2/D0EC LDX #C138`).**
`d[0x3FC138:0x3FC15E]` = `08 00 2e | 00 00 04 00 00 | 06 11 00 17 02 00 | 0c 60 43 40 31 | 08 10 97 | 0e 68 bb | 08 80 93 | 0e 1e b6 | 08 80 96 | 0e e5 bc | ff`

| decoded |
|---|
| `08` → `$61 = $2E00` (the **BG1 text** tilemap buffer) |
| `00` → fill `$0400` bytes with `$0000` — **clear BG1** |
| `06` → **draw string id `$0011` at tile pos `$0217` = (23,2), flags `$00`** |
| `0C` → HDMA ch6 (`$4360`): `$4360/1 := $3140`, table `$61 = $9300` |
| `08`/`0E` → `$7E:9710` ← `$FF:BB68` (12 bytes) |
| `08`/`0E` → `$7E:9380` ← `$FF:B61E` (**the 30-byte cursor node graph**) |
| `08`/`0E` → `$7E:9680` ← `$FF:BCE5` (8 bytes) |
| `ff` → end |

**`$FF:C16F` — open the prompt.**
`d[0x3FC16F:0x3FC191]` = `08 00 3e | 02 02 02 02 20 1c 0c | 04 8b 03 16 02 08 0a | 0c 50 43 00 29 | 0e e2 ba | 0c 20 43 03 0d | 0e 39 b8 | ff`

| decoded |
|---|
| `08` → `$61 = $3E00` (the **BG2 chrome** tilemap buffer) |
| `02` → copy the rect at **(2,2) 28×12 tiles → (2,32)** = **BACK UP** what the window will cover |
| `04` → **window: style `$8B`, palette `$03`, pos `$0216` = (22,2), 8 wide, 10 tall** |
| `0C` → HDMA **ch5** (`$4350`): `$4350/1 := $2900` → target `$2129` (WH3, window-2 right), table `$9280` |
| `0E` → `$7E:9280` ← `$FF:BAE2` (256 bytes) |
| `0C` → HDMA **ch2** (`$4320`): `$4320/1 := $0D03` → mode 3 target `$210D` (BG1 H+V scroll), table `$9100` |
| `0E` → `$7E:9100` ← `$FF:B839` (252 bytes) |
| `ff` → end |

**`$FF:C191` — close the prompt.**
`d[0x3FC191:0x3FC19C]` = `08 00 3e | 02 02 20 02 02 1c 0c | ff` →
`$61 = $3E00`, then copy **(2,32) 28×12 → (2,2)** = the exact inverse of the backup. Nothing else.

### 2.5 The prompt string

`C2/EF48` (cmd `06`): `LDA $5B / ASL A / TAX / LDA $FFC457,X / TAY` — **the menu string-pointer table is
`$FF:C457`, indexed by `id*2`, pointers relative to bank `$FF`.**

`$FF:C457 + 0x11*2` → **`$FF:C787`**:

```
0a 20 | a4 cb ba cc be | 01 | c8 c5 bd | 01 | cc ba cf be df | 01 | ff ff b8 be cc | 01 | ff ff ad c8 | 0a 00 | 00
{0A}{20}  E r a s e     nl   o l d      nl   s a v e ?        nl   _  _  Y e s      nl   _  _  N o    {0A}{00} END
```

Entry `$25` → `$FF:C912` is the same text without the leading `{0A}{20}` (used elsewhere).
The other two ids screen 6 sets in `$0D13` are **not** strings — see §3.3.

`C2/EF65` — ⭐ **THIS IS NOT A VWF.** `python tools/dis.py 2EF65 0x120 1 0`:

```
  2EF65: PHB / PHP / SEP #20
  2EF69: STA $88 / AND #7F / STA $87   ; $88 = flags ($5F), $87 = char budget (0 -> 256)
  2EF6F: STX $65                       ; $65 = the tilemap destination
  2EF71: XBA / PHA / PLB               ; DB = the string bank ($FF)
  2EF74: JSR C2/EF7E
  2EF7E: LDA $0000,Y / BEQ done / INY
  2EF86: CMP #10 / BCC ->EFBE          ; < $10 = a control code
  2EF8F: JSR C2/EF97 / DEC $87 / BNE loop
  2EF97: BIT $88 / BMI ->EFB0          ; flags bit7 set -> skip the space special
  2EF9E: CMP #FF / BNE ->EFB0
  2EFA2:   LDA #FF / STA $7DFFC0,X     ;   a SPACE also writes a space ONE ROW ABOVE (X-$40)
  2EFA9:   LDA $7E  / STA $7DFFC1,X
  2EFB0: STA $7E0000,X                 ; TILE INDEX LOW BYTE = THE CHARACTER CODE
  2EFB4: LDA $7E / STA $7E0001,X       ; ATTRIBUTE BYTE = DP $7E
  2EFBA: INX / INX
```

Control-code jump table `$C2:EFD4` (`d[0x2EFD4:0x2EFF4]` = `[26 ROM bytes at 0x2EFD4: python tools/dis.py 0x2EFD4 26] ...`):

| code | handler | meaning (the two screen 6 uses) |
|---|---|---|
| `00` | `EFD3` | terminator (handled by the loop) |
| `01` | `EFF4` | **NEWLINE.** `LDA #$0080 / BIT $87 / BPL ->EFFE / LDA #$0040` — BPL **skips** the `#$0040`, so with flags bit15 clear the destination advances **`$80` = two tilemap rows**; with it set, one row. |
| `0A` | `F068` | **SET ATTRIBUTE:** `LDA $0000,Y / STA $7E / INY` — one parameter byte into DP `$7E`, the attribute EF97 writes with every tile. |

So `{0A}{20}` = "attribute `$20`" = priority 1, palette 0, tile page 0 — **byte-for-byte what the
oracle's BG1 tilemap holds** (§4.2). Codes `02..0C` (position, indirect string, sub-string, number,
…) are not used by the prompt and are NOT DERIVED here.

### 2.6 The post-save redraw `C2/CF21` — and what it proves

```
  2CF21: JSR C2/D32C                  ; rebuild the selected slot's summary panel
  2CF24: LDX #FC0D / JSR C2/8385      ; queue VRAM job $C2:FC0D
  2CF2A: LDX #CF30 / JMP C2/8385      ; queue VRAM job $C2:CF30   (JMP -- the handler ENDS here)
```

`C2/8385` = "push an 8-byte transfer descriptor into the queue at `$7E:0C00 + $0D11`, then `$0D11 += 8`"
(`python tools/dis.py 28385 0x60 1 0`). The two descriptors:

* `d[0x2FC0D:0x2FC15]` = `40 60 80 3e 7e 00 03 00` → VRAM word **`$6040`** ← `$7E:3E80`, `$0300` bytes.
  `$6040` = BG2's map base `$6000` + row 2×32 → **the 12 slot-list rows of BG2**.
* `d[0x2CF30:0x2CF38]` = `00 79 00 70 7e 00 06` → VRAM word **`$7900`** ← `$7E:7000`, `$0600` bytes.
  BG3's char base is `$7000`, so `$7900` is **tile `$120`**, and `$7E:7000` is exactly
  `$6E00 + (0x120-0x100)*0x10` — an independent confirmation of the glyph-buffer mapping in §5.2.

`$67` is never touched and `$68` is left at 1. **The ROM stays on the Save screen slot list.**

### 2.7 The cursor: node graph + direction dispatch

`C2/E9E2` (run from the top of `C2/E984` every frame):

```
  2E9E5: LDA $54 / STA $81            ; $81 = the PRE-MOVE cursor (sub 2 blips off this)
  2E9EE: LDX #00
  2E9F0: LDA $F5 / STA $5A            ; $F5 = the d-pad NEW-PRESS byte
  2E9F4: LSR A / BCS ->2E9FE / INX / CPX #04 / BCC ->2E9F4    ; X = lowest set bit index
  2E9FE: STX $0D1E
  2EA01: LDA $0D1F / BNE ->2EA1A      ; a one-frame suppress latch
  2EA06: LDA $54 / JSR C2/EA27        ; X = $54 * 6
  2EA0B: TXY
  2EA10: LDA $0D1E / ASL A / TAX / JSR ($EA1F,X)
```

⭐ **The dispatch table is in REVERSE bit order.** `d[0x2EA1F:0x2EA27]` = `45 ea 40 ea 3b ea 36 ea`:

| `$F5` bit | dir idx | handler | node byte |
|---|---|---|---|
| 0 (**Right**) | 0 | `C2/EA45 LDA $9385,Y` | +5 |
| 1 (**Left**)  | 1 | `C2/EA40 LDA $9384,Y` | +4 |
| 2 (**Down**)  | 2 | `C2/EA3B LDA $9383,Y` | +3 |
| 3 (**Up**)    | 3 | `C2/EA36 LDA $9382,Y` | +2 |

`d[0x2EA48:0x2EA53]` = `85 54 10 06 a9 80 04 5a 14 54 60` = `STA $54 / BPL ->EA52 / LDA #80 / TSB $5A /
TRB $54 / RTS` — a link byte with bit7 set records the flag in `$5A` and strips it from `$54`.

**Node record = 6 bytes `{cursorX, cursorY, Up, Down, Left, Right}` at `$7E:9380 + n*6`.**
`C2/E923` copies `$9380,X` (the X/Y pair) straight into the OAM shadow at `$7E:0770`.

`$FF:B61E` (loaded into `$7E:9380` by the `$FF:C138` list) — `d[0x3FB61E]` = `1e` (length 30), then:

```
  node0  08 1D  U=2 D=1 L=0 R=0     slot 1   hand OAM (8,29)
  node1  08 3D  U=0 D=2 L=1 R=1     slot 2   hand OAM (8,61)
  node2  08 5D  U=1 D=0 L=2 R=2     slot 3   hand OAM (8,93)
  node3  B8 3F  U=4 D=4 L=3 R=3     YES      hand OAM (184,63)
  node4  B8 4F  U=3 D=3 L=4 R=4     NO       hand OAM (184,79)
```

Up/Down wrap over the three slots and toggle Yes↔No; **Left/Right are self-links everywhere on this
screen**, so they move nothing and (via `CFAD`/`CF6F`) make no sound. The shoulder buttons are dead:
`CEFD`/`CF61` test only `BMI`/`BVS`.

### 2.8 `$0D49` — the occupancy array, and WHEN it is built

`python tools/dis.py 2D156 0x44 1 0`:

```
  2D159: LDX $51 / STX $0D79 / STZ $0D7B / STX $79
  2D163: LDA #FF / STA $0D4C            ; a 4th sentinel byte after the 3 slots
  2D168: LDX $79 / STZ $0D49,X
  2D16D: JSL FFF9C4                     ; the ROM's validity test (signature AND checksum)
  2D171: LDA $00 / BEQ ->2D17F
  2D175:   LDA #1A / STA $020C / JSR C2/D296   ; EMPTY -> the "No data" marker
  2D17F:   INC $0D49,X / JSR C2/D19F           ; USED  -> $0D49[slot] = 1, read the summary
  2D185: JSR C2/D2C4 / INC $79 / CMP #03 / BCC ->2D168
```

⭐ **`C2/D156` is called from `C2/8127` — the MENU BOOT, not the Save screen.** (`JSR $20 56 D1`
appears exactly once in bank `$C2`.) Measured: writers `C2:D16D` (`STZ $0D49,X`) and `C2:D18A`
(`INC $79`) fire at **f50-f53**, ~65 frames before screen 6 is ever entered (§6.1). So the empty/used
verdict is a *menu-open* snapshot; the only thing that changes it afterwards is `C2/CF4C`
(`LDA #FF / STA $0D49,X`) inside the save. A port must not recompute it on Save-screen entry.

### 2.9 `$299E` and `$30:7FE1` — settled

A full-ROM operand scan (§6.5) finds **exactly one writer of `$299E`** in 4 MB:

```
  C2/E666: LDA $307FE1        ; NEW GAME (C2/E65D..: cursor $54 >= 3 on the title screen)
  C2/E66A: STA $299E          ; <-- the ONLY STA $299E in the ROM
  FF/FA6F: CMP $299E          ; the only other reference: the allocator compare
```

(`C1/33BC "STZ $299E"` is a false positive — it is `9E 29 98` = `STZ $9829,X` at `C1/33BD`.)

`$299E` lies at `$7E:299E`, **inside save record 0** (`$7E:2400 len $05B0`) at slot offset `+0x059E`,
so it is written into every save and restored by every load. Therefore:

> **`$7E:299E` is the playthrough's FILE ID.** It is seeded once, on New Game, from the SRAM allocator
> `$30:7FE1`; it then travels with the save file. `FF/FA6B..FA78` (`LDA $30:7FE1 / CMP $299E / BNE /
> INC A / STA $30:7FE1`) bumps the allocator **only while it still equals this playthrough's id** —
> i.e. exactly once, on this playthrough's *first* save. Every later save of the same file leaves the
> allocator alone; a *new* playthrough picks up the bumped value and becomes a distinct file.

Both `E66A` and `FA6F` run with `SEP #20`, so **only the low byte participates**. Boot initialises the
pair: `python tools/dis.py 28030 0x40 1 0` →

```
  28042: LDA $7F0061 / BNE ->28064       ; no game has ever started
  28048: JSR C2/956E
  2804B: LDA #01 / STA $7F0061
  28051: LDA #00
  28053: STA $307FE2
  28057: STA $307FE1                     ; allocator starts at 0
```

**`$30:7FE2`** (open in `save_write.md` §4.1) is now identified as a **title-screen flag**, not part of
the save: `C2/E8DA` clears it when all three `$0D49` bytes are 0 (`LDA $0D49 / ORA $0D4A / ORA $0D4B /
BNE ->E8E3 / STA $307FE2 / LDA #08 / STA $9392`), and `C2/E66D`, `C2/E75D`, `C2/E7F9` gate the title
screen's fourth option (`$54 == 3` there, extra display list `$C2:C36B`, and a `$9392 ^= $70` blink) on
it. It is never written by `FF/F9FB`. What *sets* it (as opposed to clearing it) is still NOT DERIVED
(`C2/8D65` is the remaining `STA $307FE2` and was not traced).

---

## 3. RAM CONTRACT

| address | width | who writes it | what it is |
|---|---|---|---|
| `$7E:0054` | b | `C2/D0EA` (from `$7F`), `C2/EA48` (nav), **`C2/D4C0` := 3** (prompt open), **`C2/D4E6`** (from `$0F00`, prompt close) | the working cursor / node index. Measured writers `C2:D4C2`, `C2:EA4A`, `C2:D4E8` (§6.2, §6.3) |
| `$7E:0067` | b | `C2/CEEC` only (refuse) | screen id. **Measured `06` for the whole prompt round-trip** |
| `$7E:0068` | b | `C2/CF19` (→2), `C2/CF7E`/`CF8B` (→1) | sub-state. Measured `0602` at f324 (§6.3) — the prompt *is* sub-state 2 |
| `$7E:0079` | b(w) | `C2/CFA7` (`$54`→`$79`), `C2/D18A` | the slot index. **Unchanged while the prompt is up** (measured) |
| `$7E:007F` | b | `C2/CFA9` | the previous-frame slot latch (sub 1's blip compare) |
| `$7E:0081` | b | `C2/E9E7` | the pre-move cursor (**sub 2's** blip compare, `CF6D`) |
| `$7E:0D13` | b | `C2/D0F7` (`$19`), **`C2/D4CA` (`$75`)**, `C2/D4E0` (`$59`), and cleared bit-by-bit in the NMI | **a VBlank job mask, NOT a message id** — see §3.3 |
| `$7E:0D49..4B` | 3b | `C2/D16D`/`D17F` at menu boot, `C2/CF4C` (`$FF`) after a save | per-slot occupancy |
| `$7E:0D4C` | b | `C2/D165` | `$FF` sentinel past the three slots |
| `$7E:0F00` | b | `C2/CF16` | the slot the prompt was opened over; `C2/D4E3` restores `$54` from it |
| `$7E:0770/71` | 2b | `C2/E964` | the hand's OAM X/Y = `node[+0]`,`node[+1]`. **Measured `08 1D` on the list, `B8 3F` at the prompt** (§6.4) |
| `$7E:0772/73` | 2b | `C2/E96A` | hand OAM tile `$20`, attr `$30` |
| `$7E:2E00..31FF` | 1 KB | `$FF:C138` cmd `00`+`06` | **BG1 tilemap buffer** → VRAM word `$5800`. Holds the prompt text permanently |
| `$7E:3E00..41FF` | 1 KB | `$FF:C16F`/`C191` cmds `02`/`04` | **BG2 tilemap buffer** → VRAM word `$6000`. The window lives here |
| `$7E:4600..47FF` | | cmd `02` | the off-screen (2,32) **backup** of the covered region |
| `$7E:6E00..` | | `C2/5DC4` | **the proportional-glyph tile buffer**, 16 B/tile, tile `$100 + (off/16)` |
| `$7E:9100` | 252 B | cmd `0E` ← `$FF:B839` | HDMA ch2 table (`$210D/$210E`, BG1 scroll) |
| `$7E:9180` | 9 B | (not by the prompt) | HDMA ch3 table (`$212C` = TM). **`10 12 70 16 50 17 10 16 00`** |
| `$7E:9280` | 256 B | cmd `0E` ← `$FF:BAE2` | HDMA ch5 table (`$2129` = WH3) |
| `$7E:9380..939D` | 30 B | cmd `0E` ← `$FF:B61E` | the 5-node cursor graph |
| `$7E:299C` | w | `FF/FA05..FA0B` | save counter; **measured `0100` → `0200` on the overwrite** (§6.3) |
| `$7E:299E` | b | `C2/E66A` **only** | the playthrough file id (§2.9) |
| `$30:7FE1` | b | `C2/8057` (init 0), `FF/FA75` | the file-id allocator |
| `$30:7FE2` | b | `C2/8053`, `C2/E8DA` (clear), `C2/8D65` (untraced) | title-screen "saves exist" flag; **not part of the save** |

### 3.3 ⭐ `$0D13` is a VBlank job mask, not a message id

`save_write.md` §3.4 calls `$0D13 := $75` "the message id for the overwrite prompt". That is wrong, and
the oracle names the routine that disproves it. `$0D13` is never read by an absolute `LDA` anywhere in
4 MB (§6.5); it is read **DP-relative with `D = $0D00`** inside the NMI:

```
python tools/dis.py 28620 0x20 1 0
  28626: LDA #80
  28628: TRB $13                 ; = TRB $0D13, DP = $0D00
  2862A: BEQ ->28651
  2862C: PHB / PHP / PEA $7E7E / PLB / PLB / REP #30
  28635: LDX #7E54 / STX $9690 / LDX #6B7E / STX $9692     ; build MVN $7E,$7E / RTL
  28641: LDX $9694 / LDY $9696 / LDA $9698 / DEC A
  2864B: JSL 7E9690                                        ; run the queued $7E->$7E block copy
```

The oracle confirms the address resolution directly: `--monitor --writer` names **`C2:862A`** as the
last writer of `$7E:0D13` on frame after frame, and the values it produces are the ROM's own masks with
**bit 7 cleared**: `$D9→$59`, `$9B→$1B`, `$83→$03`, `$9F→$1F`, `$E7→$67` (§6.1, §6.2). Bit 7 = "a
`$7E→$7E` MVN described by `$9694/$9696/$9698` is pending"; the low bits are the other per-frame upload
jobs. `C2/CFFB` sets bit 7 with `LDA #C0 / TSB $0D13`.

The practical consequence for a port: `$19`/`$75`/`$59` are **not** three strings. The prompt's text
comes from the string table (§2.5); `$0D13` only says which uploads run next VBlank.

---

## 4. HOW THE PROMPT BECOMES VISIBLE (the part no disassembly alone would give)

### 4.1 What actually changes in RAM when the prompt opens

WRAM diff between the two `--peek` dumps (§6.6), pre-prompt vs prompt:

```
7E/3EAC..3EBB  16   |
7E/3EEC..3EFB  16   |  ten 16-byte runs, stride $40  =  8 tiles wide x 10 rows,
7E/3F2C..3F3B  16   |  starting at $3E00 + 2*$40 + 22*2 = $3EAC
   ... 10 runs ...  |  ==> EXACTLY the cmd-04 window at tile (22,2), 8 x 10
7E/40EC..40FB  16   |
7E/4605..48FB  759     the cmd-02 backup region at (2,32)  ($3E00 + 32*$40 = $4600)
7E/9100..9119  26      the ch2 HDMA table
7E/9281..9286   6      the ch5 HDMA table
7E/0770..0771   2      the hand OAM  08 1D -> B8 3F
7E/0D00..0D17  24      $0D13 / $0D17 / $0D1x
```

**`$7E:2E00` does not change at all** — the text is already there.

VRAM diff over the same pair: **one span**, bytes `0C0AC..0C2FB` = words `$6056..$617D` = BG2's map
`$6000` + rows 2..11, cols 22..29. **No tile-graphics upload, no BG1 tilemap change.**

### 4.2 The BG1 tilemap already holds the prompt, both before and after

`--peek` VRAM, BG1 map word `$5800`, tile low byte decoded as a CT character code (identical in the
pre-prompt and prompt dumps):

```
  r02 |.......................Erase....|  attrs = 00,20
  r04 |.......................old......|
  r06 |.......................save?....|
  r07 |.......................  .......|   <- the $FF space special (EF97) wrote these
  r08 |.......................  Yes....|
  r09 |.......................  .......|
  r10 |.......................  No.....|
```

Attribute `$20` = priority 1, palette 0, tile page 0 — the `{0A}{20}` at the head of string `$11`.
Rows two apart = the `#$0080` newline advance. Rows 7/9 = the space duplication. Every byte of §2.5 is
visible in the oracle's VRAM.

### 4.3 Why it is invisible until the prompt opens: a per-scanline `$212C` HDMA

`--hdma` on the pre-prompt frame:

```
  ch3: hdmaActive=1 bAdr=$212C mode=0 table=7E:9180
  ...
  lines 113..128  TM=16 TS=00
  lines 129..133  TM=17 TS=00
  ...
  lines 204..208  TM=17 TS=00
  lines 209..224  TM=16 TS=00
```

`$7E:9180` = `10 12 | 70 16 | 50 17 | 10 16 | 00` = 16 lines `TM=$12`, 112 lines `$16`, 80 lines `$17`,
16 lines `$16` (224 total). `TM` bit0 = BG1, so **BG1 is on only for lines ~129..208** — the bottom
info boxes ("Crono LV 1", "TIME", "600G", "Save #"), which is why those *are* visible on BG1 while
"Erase old save?" at rows 2..10 is not.

`--hdma` on the prompt frame:

```
  ch2: hdmaActive=1 bAdr=$210D mode=3 table=7E:9100
  ch3: hdmaActive=0 bAdr=$212C mode=0 table=7E:9180
  ch5: hdmaActive=1 bAdr=$2129 mode=0 table=7E:9280
  ...
  lines   0.. 16  TM=17
  lines  17.. 18  TM=17
  ...                       (TM = 17 on every traced line)
```

**Channel 3 is no longer running**, so `$212C` holds its CPU-written `$17` for the whole frame and BG1
shows everywhere — and BG1 rows 0..16 contain *nothing but* the prompt text (§4.2), so exactly the
prompt appears. Channels 2 and 5 are the two the `$FF:C16F` list armed (§2.4).

⚠ **Inferred, not proven:** that the ROM achieves this by writing `$420C` without bit 3. What is
*measured* is `ch3: hdmaActive=0` and `TM=17` on every line. The `$420C` store was not traced.

### 4.4 The on-screen rectangle

Framebuffer diff of the same pair (§6.7), excluding the hand (`x >= 100`):

```
  x 176..239   y 24..103        = 64 x 80 px = 8 x 10 tiles
  (the hand's vacated ink rows at x<100 were y 39..50)
```

`x 176..239` is tile columns 22..29 exactly. The `y` origin is 8 px lower than `src/menu.h`'s field-menu
reference frame for the same tile rows; BG2's vertical position is HDMA-driven (`ch4: bAdr=$2112 mode=2
table=7E:9200`), so **the canonical geometry is the tilemap coordinate (22,2)+8×10, not a pixel Y**.
The hand's canonical position is the node byte pair, `$7E:0770/71` (measured `B8 3F`).

### 4.5 The rendered result

`scratchpad/menu/ovw_prompt.png` / `ovw_prompt_crop.png`: a window on the right holding, on an 8-pixel
grid, `Erase` / `old` / `save?` / `☞Yes` / `No`, with the hand on **Yes**.
`scratchpad/menu/post_yes.png` (after A on Yes): the prompt is gone, the slot list is back with the hand
on slot 1, and the summary now reads **"Save # 2"**.

---

## 5. THE REAL TEXT ENGINES

### 5.1 Two of them, and they are not the same routine

| engine | entry | output | used for |
|---|---|---|---|
| **fixed 8×8 tilemap** | `C2/EF65` (via display-list cmd `06`) | one tilemap entry per char, `tile = charcode`, attr from DP `$7E` | the prompt text, and everything else the display lists draw on BG1 |
| **proportional (VWF)** | glyph: `C2/5DC4`; string loop: `C2/58B2`; screen entry: `C2/F90C` → `JSL $C2:0003`/`$C2:0009` with the `$020C..$0215` block | 2 bpp pixels into `$7E:6E00+`, referenced by a run of consecutive tile ids | slot summaries ("The Millennial Fair", "No data"), names, numbers — BG3, palette 4 |

⭐ The `ct-menu-save-screen` memory records "the ROM uses the VWF blitter C2/EF65". `C2/EF65` contains
no shifter, no width table and no pixel writes — it stores `charcode` as a tile index (§2.5). The 1432
differing pixels on the Save screen are the **`C2/5DC4`** engine's output.

### 5.2 `C2/5DC4` — the per-glyph rasteriser, fully derived

`python tools/dis.py 25DC4 0x74 1 0`:

```
  25DC4: REP #20
  25DC6: LDA $35 / ASL/ASL/ASL / STA $6C / ASL A / ADC $6C   ; A = code * 24
  25DD1: CLC / ADC #2060 / STA $76        ; PLANE-A src  = $FF:2060 + code*24
  25DD6: LDA $35 / LSR A / ASL/ASL/ASL / STA $6C / ASL A / ADC $6C
  25DE1: CLC / ADC #3860 / STA $79        ; PLANE-B src  = $FF:3860 + (code>>1)*24
  25DE7: LDA $34 / AND #0007 / STA $6A    ; $6A = pen X & 7  -> the shift case
  25DEE: LDA $10 / CLC / ADC #0008 / STA $73   ; dest = tile-strip base + 8
  25DF8: LDA $12 / STA $75                ; dest bank
  25DFC: LDA #FF / STA $78 / STA $7B      ; both glyph planes live in bank $FF
  25E02: LDA $34 / AND #F8 / LSR / LSR / STA $6E   ; $6E = 2 * (penX >> 3)  -> the column table index
  25E0C: LDA $35 / STA $62                ; the code's bit0 selects the plane-B nibble
  25E10: LDA $14 / AND #0F / BNE ->25E1B
  25E16:   JSR C2/5E36     ; 2 bpp destination
  25E1B:   JSR C2/5F07     ; 4 bpp destination
  25E1E: SEC / LDA $35 / SBC #A0 / TAX
  25E28: CLC / LDA $C260E6,X / ADC $34 / STA $34   ; <<< THE ADVANCE TABLE >>>
  25E30: INC $17
```

**Row loop** (`C2/5E36`, the 2 bpp form): 4 iterations × 2 rows = **8 source bytes**, then
`$73 += $00F0`, then 8 iterations × 2 = **16 more** — **24 bytes = 12 rows × 2 bitplanes**. Since
`$73` starts at `base + 8`, the glyph occupies bytes 8..15 of the upper tile (rows 4..7) and all 16
bytes of the tile directly below (rows 0..7): **a 12-scanline glyph in a 2-tile-tall strip.**
`C2/5F07` is the identical loop with `$73 += $01F0` and the 4 bpp column tables.

**Per-row assembly** (`C2/5E89`): `A_high = $FF:[plane-A + Y]` (the left 8 pixels of that plane-row);
`A_low = ($FF:[plane-B + Y] & $F0)` for an **even** code, or `<< 4` of the low nibble for an **odd**
code (the two glyphs of a pair share plane-B's byte). So `A` is a **12-bit plane-row, left-justified**.

**Shift + store** — jump table `d[0x25EAF:0x25EC1]` = `bf 5e d2 5e d1 5e d0 5e cf 5e e5 5e e8 5e eb 5e`,
indexed by `$6A*2`:

| `$6A` | entry | operation | stores (column offsets from `$C2:5FE6`, stride `$10`) |
|---|---|---|---|
| 0 | `5EBF` | none | low → col `k+1`, high → col `k` (plain `STA`) |
| 1..4 | `5ED2/D1/D0/CF` | `LSR A` × `$6A` | low → col `k+1`, high **OR**→ col `k` |
| 5..7 | `5EE5/E8/EB` | `ASL A / ROL $63` × (8−`$6A`) | low → col `k+2`, high → col `k+1`, `$63` **OR**→ col `k` |

Column-offset tables: `d[0x25FE6:0x26016]` = `00 00 10 00 20 00 … f0 00 00 02 10 02 …` (2 bpp: 16
columns of stride `$10`, then a jump to `$0200` — because the strip is **16 tiles wide and 2 tiles
tall**, so column 16 starts after both rows of the first 16); `d[0x26066:...]` = the same with stride
`$20` (4 bpp).

**Advance table `$C2:60E6`, index `= charcode − $A0`** (`d[0x260E6:0x26146]`):

```
07 07 07 07 06 06 07 07 05 07 08 06 09 08 07 07 07 07 06 07 07 07 0b 07 07 07   A..Z
07 07 06 07 07 06 07 07 03 06 07 03 0b 07 07 07 07 06 06 05 07 07 0b 07 07 07   a..z
07 04 07 07 08 07 07 07 07 07                                                   0..9
[26 ROM bytes at 0x26124: python tools/dis.py 0x26124 26]   $DE..$F7
00 00 00 00 00 00 00 04                                                         $F8..$FF
```

i.e. `A`=7 `E`=6 `I`=5 `M`=9 `W`=11 `i`=3 `l`=3 `m`=11 `w`=11 `t`=5 `1`=4, **space (`$EF` and `$FF`) = 4**.
Cross-checked against the render: `The Millennial Fair` = 7+7+7+4+9+3+3+3+7+7+7+3+7+3+4+6+7+3+6 = **103 px**,
and the rendered string measures ~104 px wide in `pre_prompt.png`.

**String loop** (`C2/58B2`):

```
  258B2: LDA [$31] / INC $31          ; the string cursor
  258BA: CMP #A0 / BCC ->258D0        ; < $A0 = a control / dictionary code (table $DE:FA00 for $21..$9F)
  258BE: STA $35 / JSR C2/5DC4        ; >= $A0 = a glyph
  258C7: DEC $13 / BNE ->258B2
```

— which is exactly why the advance table's domain starts at `$A0`.

**Buffer→VRAM mapping, proven twice.** The oracle's `--monitor --writer` names `C2:5EFD`/`C2:5F06` (the
`STA [$73],Y` at `5EFA`/`5F04`) as the writer of `$7E:71A0`, and `$7E:71A0` holds the byte-identical
copy of VRAM tile `$13A` (word `$79D0`). `$71A0 = $6E00 + (0x13A−0x100)*0x10` ⇒ **`$7E:6E00` ↔ VRAM tile
`$100`, 16 bytes per tile**. Independently, `C2/CF21`'s own descriptor uploads `$7E:7000 → $600` bytes →
VRAM word `$7900` = tile `$120`, and `$7000 = $6E00 + (0x120−0x100)*0x10`. The two agree.

### 5.3 What remains open in the VWF

* The **glyph-height/plane packing is derived from the loop shape and the pointer arithmetic** (24 bytes
  = 12 rows × 2 planes, left 8 px in plane A, right 4 px nibble-packed in plane B) and is consistent
  with the advance widths up to 11 — but I did **not** render a full glyph sheet and compare it against
  the oracle's VRAM pixel-for-pixel. That comparison is the check that would close it.
* The **4 bpp variant** `C2/5F07` (selected by `$14 & $0F != 0`) was read structurally only.
* Control/dictionary codes `< $A0` in the VWF path (`C2/58D0`, table `$DE:FA00`) are NOT DERIVED.
* Who sets `$10/$12` (the strip base + bank), `$14`, `$34` (the pen X origin) per string is NOT DERIVED
  beyond `C2/D296`'s `$0DC5 = $7000 + ($78 & $0300)*2` and `C2/F90C`'s `$0210/$0212 := $0DC5/$7E`.

---

## 6. ORACLE MEASUREMENTS (exact commands, real output)

All runs use `saves/ow_w0.state` (overworld — the Save entry gate `C2/CEDC` always passes there) and
the shared prefix `0:20,200:2,0:60` (X opens the menu) `,80:2,0:4` ×5 (Right ×5 → the Save icon)
`,0:8,100:2` (A → screen 6). Button grammar: **A = `0x100`, X = `0x200`, B = `0x01`, Up `0x10`,
Down `0x20`, Left `0x40`, Right `0x80`.**

### 6.1 The prompt is reached; the `$0D49` scan runs at MENU BOOT

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E0067:2,7E0054:1,7E0079:1,7E007F:1,7E0081:1,7E0D49:3,7E0D13:1,7E0F00:1,7E1E00:4" \
  --from saves/ow_w0.state \
  --btnscript "0:20,200:2,0:60,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:8,100:2,0:150,100:2,0:120,100:2,0:90" \
  --diff --writer
```

```
  f50   7E0067=0000 7E0054=00 7E0079=01*(C2:D18A) 7E0D49=000000(C2:D16D) ...   <- the 3-slot scan,
  f52   ...                   7E0079=02*(C2:D18A) 7E0D49=000000(C2:D16D) ...      65 frames BEFORE
  f53   ...                   7E0079=03*(C2:D18A) ...                             screen 6 is entered
  f117  7E0067=0600*(C2:8211) ...                                              <- screen 6
  f118  7E0067=0600  7E0054=00*(C2:D0EC) ... 7E0D13=D9*(C2:D03A)
  f269  7E0067=0600  7E0054=00 7E0079=00 7E0D49=FF0000*(C2:CF51) 7E0D13=59*(C2:862A) 7E1E00=008A800D*(C7:0196)
  f391  7E0067=0600  7E0054=03*(C2:D4C2) 7E0079=00 7E0D49=FF0000 7E0D13=75*(C2:D4CD) 7E0F00=00(C2:CF19)
  f392  7E0067=0600  7E0054=03 7E0081=03*(C2:E9E9) 7E0D13=75(C2:862A)
```

Reads: the save lands at f269 (`$0D49 = FF 00 00` by `C2:CF51`, mailbox id `$8A`); the second A at
f390-391 takes the **occupied** branch — `$54 := 3` by `C2:D4C2`, `$0D13 := $75` by `C2:D4CD`, and
`$79` stays 0. `$0D13` alternates `$D9 → $59`, `$75` writer `C2:862A` = the NMI clearing bit 7 (§3.3).

### 6.2 Slot nav, Yes/No nav, A-on-No, B — one drive

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E0054:1,7E0067:2,7E0079:1,7E0081:1,7E0D49:3,7E0F00:1,7E0D13:1,7E1E00:3,7E299C:2" \
  --from saves/ow_w0.state \
  --btnscript "0:20,200:2,0:60,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:8,100:2,0:30,100:2,0:120,20:2,0:20,10:2,0:20,100:2,0:20,20:2,0:20,100:2,0:20,100:2,0:20,1:2,0:40" \
  --diff --writer
```

```
  f149  7E0D49=FF0000*(C2:CF51)  7E1E00=008A80*(C7:0196)  7E299C=0100*(FF:FA0E)   A on EMPTY slot -> save
  f271  7E0054=01*(C2:EA4A)  7E0079=01*(C2:CFA9)                                  DOWN  -> slot 2
  f293  7E0054=00*(C2:EA4A)  7E0079=00*(C2:CFA9)                                  UP    -> slot 1
  f315  7E0054=03*(C2:D4C2)  7E0D13=75*(C2:D4CD)  7E0F00=00(C2:CF19)              A     -> PROMPT, Yes
  f337  7E0054=04*(C2:EA4A)                                                       DOWN  -> No
  f359  7E0054=00*(C2:D4E8)  7E0D13=59*(C2:D4E3)                                  A on No -> CANCEL
  f381  7E0054=03*(C2:D4C2)  7E0D13=75*(C2:D4CD)                                  A     -> PROMPT again
  f403  7E0054=00*(C2:D4E8)  7E0D13=59*(C2:D4E3)                                  B     -> CANCEL
```

`$299C` stays `0100` across both cancels — **no save happens on the No / B paths**, and `$54` comes
back from `$0F00` through `C2:D4E8` exactly as `C2/D4E6` says.

Node-direction check, `--monitor "7E00F0:10,7E0054:1"` on the same screen: Down → `$00F5 = 04`
(bit 2) and `$54` 0→1 = `node0[+3]`; Up → `$00F5 = 08` (bit 3) and `$54` 1→0 = `node1[+2]`; Left
(`$00F5 = 02`) and Right (`$00F5 = 01`) leave `$54` alone. That is the reversed dispatch table of §2.7.

### 6.3 A on YES — the overwrite really saves, and the screen does not change

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc \
  --monitor "7E0054:1,7E0067:2,7E0079:1,7E0D49:3,7E0F00:1,7E0D13:1,7E1E00:3,7E299C:2,7E0D76:1" \
  --from saves/ow_w0.state \
  --btnscript "0:20,200:2,0:60,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:8,100:2,0:30,100:2,0:150,100:2,0:20,100:2,0:100" \
  --diff --writer
```

```
  f149  7E0D49=FF0000*(C2:CF51) 7E1E00=008A80*(C7:0196) 7E299C=0100*(FF:FA0E) 7E0D76=00   1st save
  f301  7E0054=03*(C2:D4C2)  7E0D13=75*(C2:D4CD)  7E1E00=000080  7E0D76=03*(C2:EB15)      prompt opens
  f302..f304                                             7E0D76=02,01,00*(C2:8466)        the blip drains
  f323  7E0D49=FF0000(C2:CF51) 7E1E00=008A80*(C7:0196) 7E299C=0200*(FF:FA0E) 7E0D76=00    A on YES -> SAVE
  f324  7E0067=0602*(C2:8211)                                                             $68 == 2 == the prompt
  f326  7E0054=00*(C2:D4E8)  7E0D13=59*(C2:D4E3)                                          prompt torn down
```

Three facts land here: the **occupied-slot blip is throttled** (`$0D76 := 3` by `C2:EB15` at f301, the
`C2/CF11 JSR C2/EAC2` site), the **jingle is not** (`$1E01 = 8A` with `$0D76 = 00` at f323), and
`$299C` goes `1 → 2`, so the overwrite is a full second save with an incremented Save #. **`$0067`
never leaves `06`.** `$0068` is briefly observable as `2` — the prompt sub-state.

### 6.4 The cursor OAM shadow

```
pre_prompt  $7E:0770 = 08 1d 20 30      (node0: slot 1)
ovw_prompt  $7E:0770 = b8 3f 20 30      (node3: YES,  = $FF:B61E's node-3 bytes)
post_yes    $7E:0770 = 08 1d 20 30      (back to slot 1)
```

### 6.5 The `$299E` / `$30:7FE1` / `$30:7FE2` operand scan

A full-4 MB scan for every absolute/long opcode carrying those operands (script in
`scratchpad/menu/`, results reproduced in §2.9):

```
  $299E     : C1/33BC (false positive: STZ $9829,X)  C2/E66A STA   FF/FA6F CMP
  $30:7FE1  : C2/8057 STA   C2/E666 LDA   FF/FA6B LDA   FF/FA75 STA
  $30:7FE2  : C2/8053 STA   C2/8D65 STA   C2/E66D LDA   C2/E75D LDA   C2/E7F9 LDA   C2/E8DA STA
  $0D13     : 41 x "STA $0D13" and ZERO "LDA $0D13"  ->  it is read DP-relative (D = $0D00)
```

### 6.6 Screen captures + dumps

```
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/ow_w0.state \
  scratchpad/menu/pre_prompt.raw --btnscript "<prefix>,0:150,100:2,0:120"
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/ow_w0.state \
  scratchpad/menu/ovw_prompt.raw --btnscript "<prefix>,0:150,100:2,0:120,100:2,0:30"
scratchpad/menu/ct_probe.exe Chrono_Trigger_USA_SNES.sfc --peek saves/ow_w0.state \
  scratchpad/menu/post_yes.raw  --btnscript "<prefix>,0:30,100:2,0:150,100:2,0:20,100:2,0:100"
python tools/_fbpng.py scratchpad/menu/<name>.raw scratchpad/menu/<name>.png
```

Artefacts: `pre_prompt.png`, `ovw_prompt.png` (+ `*_crop.png`, 4× of x168..255 y8..103), `post_yes.png`,
and each run's `.wram` / `.vram` / `.oam`. Also `PEEK_ISOBG=0/1/2` variants (`pre_iso0.png` shows BG1
alone: the info boxes render, "Erase old save?" does not — the finding that led to §4.3).

### 6.7 PPU

```
scratchpad/menu/ct_probe.exe ... --ppuregs saves/ow_w0.state --btnscript "<prefix>,0:150,100:2,0:120"
  mode=1 brightness=15
  BG1 tilemapAdr=5800(w) tileAdr=0000(w)   <- fed by $7E:2E00
  BG2 tilemapAdr=6000(w) tileAdr=0000(w)   <- fed by $7E:3E00
  BG3 tilemapAdr=6800(w) tileAdr=7000(w)   <- the proportional strips, fed by $7E:6E00

scratchpad/menu/ct_probe.exe ... --hdma  saves/ow_w0.state --btnscript ...     (output in §4.3)
```

---

## 7. IMPLEMENTATION SPEC (engine terms)

Everything below is an addition to `src/field.c`'s `menuScreen == 6` block and `src/menu.c`. No existing
derived behaviour changes except where marked ⭐.

### 7.1 New state

```c
static int menuSaveSub;      /* $0068 for screen 6: 1 = slot list, 2 = overwrite prompt */
static int menuSaveCell;     /* $0054 : 0..2 = slot, 3 = YES, 4 = NO   (the node index) */
static int menuSavePrev;     /* $0081 : the pre-move cell, for the sub-2 blip           */
static int menuSaveSlotSave; /* $0F00 : the slot the prompt was opened over             */
static uint8_t menuSaveOcc[3]; /* $0D49 : built ONCE at menu open (C2/8127 -> C2/D156)  */
```

`menuSaveCur` (the existing slot cursor) becomes `$0079` and is committed from `menuSaveCell` only in
sub-state 1 (`C2/CFA5-CFA7`); the prompt must **not** move it (measured: `$79` unchanged, §6.1).

### 7.2 The node graph — put the ROM table in, do not re-express it as `%3`

```c
/* $FF:B61E, copied to $7E:9380 by the $FF:C138 display list. 6 bytes/node:
   { cursorX, cursorY, Up, Down, Left, Right }.  Direction dispatch C2/EA1F is
   REVERSED against the $F5 bit index: bit0 Right->+5, bit1 Left->+4, bit2 Down->+3, bit3 Up->+2. */
static const uint8_t CT_SAVE_NODES[5][6] = {
  { 0x08,0x1D, 2,1,0,0 },   /* slot 1 */
  { 0x08,0x3D, 0,2,1,1 },   /* slot 2 */
  { 0x08,0x5D, 1,0,2,2 },   /* slot 3 */
  { 0xB8,0x3F, 4,4,3,3 },   /* YES    */
  { 0xB8,0x4F, 3,3,4,4 },   /* NO     */
};
```

Up → `node[2]`, Down → `node[3]`, Left → `node[4]`, Right → `node[5]`. This replaces the current
`(cur ± 1) % 3` in `field.c` — same behaviour on the slot list, and it is the *only* thing that makes
the Yes/No toggle correct without a second special case. The hand is drawn at `(node[0], node[1])`
(this is `$7E:0770/71`, the OAM X/Y), so the existing `mHandCursor` anchor for the slots keeps working
and Yes/No come for free.

### 7.3 The A branch (replaces the `menuSfxSilent("save-overwrite-prompt-not-ported")` arm)

```
sub 1, A:
    if (menuSaveOcc[slot] == 0)                       /* C2/CF0F BEQ */
        -> the existing empty path (jingle + CtMenuSaveWrite + fieldSramStore)
           then the POST-SAVE REDRAW, and STAY in sub 1               (C2/CF1E -> CF21)
    else                                              /* C2/CF11..CF1B */
        menuSfx(id 0x00, "save-overwrite-open");      /* THROTTLED (measured $0D76:=3) */
        menuSaveSlotSave = menuSaveCell;              /* $0F00 */
        menuSaveSub = 2;
        menuSaveCell = 3;                             /* C2/D4C0: YES pre-selected */
        menuSavePromptOpen = 1;

sub 2, A:
    if (menuSaveCell == 3) {                          /* C2/CF77 CMP #03 */
        ... the SAME save path as the empty branch (jingle, CtMenuSaveWrite, fieldSramStore) ...
        menuSaveSub = 1; closePrompt(); postSaveRedraw();
    } else goto cancel;                               /* A on NO behaves exactly like B */
sub 2, B:
cancel:
    menuSfx(id 0x00, "save-overwrite-cancel");        /* C2/CF88 */
    menuSaveSub = 1; closePrompt();

closePrompt():                                        /* C2/D4D5 */
    menuSaveCell = menuSaveSlotSave;                  /* $54 := $0F00 */
    menuSavePromptOpen = 0;

sub 2, Up/Down: step the node graph, and blip iff menuSaveCell != menuSavePrev   (C2/CF6B-CF71)
sub 2, per frame: NOTHING else -- C2/CF74 is a bare RTS, there is no redraw
```

After a successful save, mark `menuSaveOcc[slot] = 0xFF` (`C2/CF4C`) so a second A on the same slot
takes the prompt branch. **Do not** recompute the occupancy from the signature on Save-screen entry —
the ROM builds it once at menu open (§2.8). ⭐ The engine currently derives occupancy from
`CtMenuSaveSlot()` on every press; that agrees today only because the writer maintains the signature.

### 7.4 Rendering the prompt (`CtMenuRenderSave`, new `promptOpen` argument)

The engine composes a framebuffer, so it does not need the HDMA plumbing — it needs the *result*:

1. Draw the slot list exactly as now, **including** the half-color-math dim band for the non-cursor
   rows (when the prompt is up the ROM still shows the slot list underneath — see `ovw_prompt.png`).
2. Draw a window over tile columns 22..29, tile rows 2..11 — **x 176..239, 80 px tall, top edge level
   with slot button 0's top** (the same 9-slice chrome the Save buttons use; the ROM's cmd-04 style
   byte is `$8B`, palette `$03`).
3. Draw, in the fixed 8×8 menu font, **one glyph per 8-px cell**, at tile column 23 (x = 184):
   `Erase` row 2, `old` row 4, `save?` row 6, `Yes` at column 25 row 8, `No` at column 25 row 10
   (rows are 2 tile rows = 16 px apart; the two leading `$FF` spaces before `Yes`/`No` are why those
   two start at column 25). Bright palette, i.e. the same ink `mText` already uses for the cursor row.
4. Draw the hand at `(184, 63)` for `menuSaveCell == 3` and `(184, 79)` for `4`, through the existing
   `mHandCursor` (same +13/−? anchor convention as the slot rows — the node bytes are the OAM X/Y).

The text is a **fixed-width** run — do not route it through any proportional path.

### 7.5 `$299E` / `$30:7FE1` — now portable (`src/menu.c:1626` "NOT PORTED" can go)

Inside `CtMenuSaveWrite`, after the signature store, append `FF/FA6B..FA78`:

```c
/* FF/FA6B: bump the SRAM file-id allocator only while it still equals THIS playthrough's
   file id.  $7E:299E is the playthrough file id, seeded on New Game from $30:7FE1 by
   C2/E666-E66A and carried inside record 0 at +0x059E; both compares are 8-bit. */
if (sram[0x1FE1] == wram[0x299E])
    sram[0x1FE1] = (unsigned char)(sram[0x1FE1] + 1);
```

and seed it wherever the engine starts a new game (`C2/E660-E66A`):

```c
wram[0x7F0061 mirror] = 1;          /* the new-game flag, already modelled */
wram[0x299E] = sram[0x1FE1];        /* C2/E666 LDA $30:7FE1 / C2/E66A STA $299E */
```

Without the seed the compare is meaningless — port them together or port neither. A fresh
`CT_SRAM_FRESH` cart must have `sram[0x1FE0] = sram[0x1FE1] = sram[0x1FE2] = 0` for `C2/8053-8057`
to be reproduced; ⚠ note the rest of a real erased cart is `0xFF` (the checksum spans it), so only
these three bytes are zeroed.

`$30:7FE2` is a **title-screen** flag and must stay untouched by the save (§2.9).

### 7.6 Post-save

`C2/CF21` does not change `$67` or `$68`. So: after any save (empty slot or Yes) the engine stays on
screen 6, sub-state 1, cursor on the slot that was written, and **re-reads that slot's summary** so the
Save # / TIME / location update — visible in `post_yes.png` as "Save # 2". The menu does not close and
does not return to the ribbon.

### 7.7 If/when the proportional font is implemented (§5.2)

`CtMenuDrawGlyphVwf(code, penX)` → advance `= rom[0xC260E6 + (code - 0xA0)]`; plane A =
`rom[0x3F2060 + code*24]`, plane B = `rom[0x3F3860 + (code>>1)*24]` (high nibble for even codes, low
for odd); 12 rows × 2 planes; 12-bit rows shifted by `penX & 7` into a 16-tile × 2-tile strip whose
tile data lives at `$7E:6E00 + (tile - 0x100)*16`. Note that the *prompt* does not need it — only the
slot summaries do.

---

## 8. OPEN / BLOCKED

1. **The `$420C` write that drops HDMA ch3.** §4.3 measures `ch3: hdmaActive=0` and `TM=$17` on every
   line during the prompt, but the store that clears the enable bit was not traced. Unblocked by
   `--loadcensus --censusat 00420C` across the A press, or a `--hookpc` on the writer.
2. **`$FF:BAE2` (256 B → ch5 `$2129`) and `$FF:B839` (252 B → ch2 `$210D/$210E`)** are loaded verbatim
   but their *effect* was not decoded. The `$9100` table observed live begins `24 fc 00 f3 01 | 0c fc
   00 f7 01 | …` while the blob begins at its third byte, so **two leading bytes are written by
   something else** — probably a per-frame slide-in. A frame-by-frame `--monitor "7E9100:8" --diff
   --writer` across the A press would settle it. Irrelevant to a framebuffer engine; relevant to any
   claim of a byte-exact prompt *animation*.
3. **Display-list cmds `02`/`04`'s exact tile output.** The rect copy and the window frame are
   structurally derived (`C2/EF05`, `C2/EDAA` → `EDD3`/`EDF6`/`EE7F`, base tile `= style*20` into
   `$0D47`, 4-tile repeating pattern from `$C2:EEF5`), but I did not reduce `EDF6`/`EE7F` to a
   9-slice specification. The measured tilemap (§4.1) gives the *result* for this one window
   (`dc dd de dd de dd de df / e0 e8 e9 ec ed e8 e9 e1 / e2 ea eb ee ef ea eb e3 / … / e4 e5 e6 e5 e6
   e5 e6 e7`, attr `$03`) which is enough to reproduce it; the general rule is not derived.
4. **What SETS `$30:7FE2`** (`C2/8D65`) — only the clear (`C2/E8DA`) and the init (`C2/8053`) were read.
5. **The VWF items in §5.3** — the glyph-sheet pixel comparison, the 4 bpp variant, the `< $A0`
   control codes, and the callers that set `$10/$12/$14/$34`.
6. **`$00F5`'s composition.** The nav reads `$F5`; measurement (§6.2) pins bit0=Right, bit1=Left,
   bit2=Down, bit3=Up and shows `$F0/$F1` and `$F4/$F5` are the edge while `$F2/$F3` are held. But
   `C2/8545` builds `$F6..$F9` by permuting `$F0/$F1` through the **config button-remap table
   `$7E:2993-299A`**, so a port that hardcodes the pad layout will diverge once the Config screen's
   remapping is implemented. Which of `$F4/$F5`, `$F6/$F7` feeds which consumer is NOT DERIVED.
7. **`$0D13`'s low bits.** Bit 7 is proven (`C2/8628 TRB $13`, DP `$0D00`, writer `C2:862A` measured).
   The meaning of bits 0..6 in `$19` / `$75` / `$59` / `$1B` was not enumerated — it needs the rest of
   the NMI (`C2/8500-8660`) read with the DP-`$0D00` insight applied.
8. **A field-map capture of the Save screen.** Everything here is measured on `saves/ow_w0.state`
   (overworld), where the whole Save screen sits 8 px lower than `src/menu.h`'s field-menu reference
   (§4.4). *Blocked on a capture:* a **field** state whose map has `$7F01CE` bit15 set (a save point),
   with a slot already occupied, would let the prompt's absolute pixel Y be pinned in the same frame
   as the existing `menu.h` numbers. `saves/com_jogo_salvo.state` is a field state that already holds
   a save, but I did not verify its map passes `C2/CEDC`; the overworld route was used instead
   precisely because it always passes.

---

## ADVERSARIAL VERIFICATION (independent pass, 2026-08-30)

A second agent re-derived every load-bearing claim above from the cartridge and the oracle, taking
**no** citation on trust: every ROM address was re-disassembled or re-dumped, and every key probe was
re-run against a freshly generated set of `--peek` artefacts (`scratchpad/menu/_av2_*`). The commands
are in §AV.7.

### AV.0 VERDICT

**The dossier's core mechanism is CONFIRMED.** The overwrite prompt is screen 6 sub-state 2, built by
the generic display-list interpreter `C2/ED31` out of `$FF:C16F` (open) / `$FF:C191` (close); the text
is pre-written into the BG1 buffer by the slot-list list `$FF:C138` and is revealed by an **HDMA
channel-3 drop**, not by any drawing; the cursor is the `$FF:B61E` node graph with `$54` pre-seeded to
3; `A` saves iff `$54 == 3`; `A`-on-No is byte-identical to `B`; and after any save the ROM stays on
screen 6 sub-state 1. Every one of those was reproduced byte-for-byte or frame-for-frame below, and
several were strengthened with evidence the original pass did not have.

**Two claims are REFUTED, and both are consequential:**

* **D1 — every `cmd 0E` blob length in §2.4 and §3 is wrong**, which makes **§8 OPEN item 2 a
  phantom.** The handler copies `rom[$FF:$5B]` bytes starting at `$5B + 1`; measured against live WRAM
  the copies are 26 / 7 / 13 / 16 bytes, not 252 / 256 / 12 / 8. The "two leading bytes written by
  something else, probably a per-frame slide-in" is just the MVN length header the dossier forgot to
  skip.
* **D2 — §3.3's account of `$0D13`'s LOW bits is wrong**, and it is exactly the mechanism §4.3 leaves
  "inferred, not proven" and §8 item 7 leaves open. `C2/8442: LDA $13 / AND #7F / STA $420C` (DP =
  `$0D00`) writes **`$0D13 & $7F` straight into HDMAEN, every frame**. Bits 0..6 are not "the other
  per-frame upload jobs" — they *are* the HDMA enable mask. This closes §8 items 1 and 7.

Nine further defects (D3..D11) are annotation-grade: dropped instructions, off-by-ones, one wrong
source line. None changes the mechanism; two of them (D3, D8) would mislead an implementer.

Structural check (the author died mid-write, so this was looked for explicitly): the file does **not**
stop mid-sentence, and contains no TODO/TBD. It has one numbering artefact — **§3 carries a `### 3.3`
with no `3.1` or `3.2`** — and one routine that is named in the spec but never disassembled
(`C2/D32C`, see D8).

### AV.1 DEFECT TABLE

| # | severity | where | the dossier's claim | what the ROM / oracle actually says |
|---|---|---|---|---|
| D1 | **major** | §2.4, §3, §8.2 | blob sizes 252 / 256 / 12 / 8 B; "two leading bytes are written by something else" | 26 / 7 / 13 / 16 B, and byte 0 of each blob is the MVN count. §8 item 2 dissolves |
| D2 | **major** | §3.3, §4.3, §8.1, §8.7 | `$0D13` low bits = "the other per-frame upload jobs"; the `$420C` store "was not traced" | `C2/8442 LDA $13 / AND #7F / STA $420C` — the low 7 bits ARE HDMAEN |
| D3 | minor (port hazard) | §2.7 | the `C2/E9E2` listing | drops `LDA #80 / STA $0D1E`, drops the **no-press guard `2E9FC BRA ->2EA1A`**, drops `STA $55`, drops `REP #10` |
| D4 | minor | §2.3 | cmds `10`/`12`/`14` take "5" params | `$FF:BD18` holds 5 there and the MVN copies **6**; the dossier's own convention (`BD18 + 1`) is applied to every other row |
| D5 | minor | §2.5 | `2EF86: CMP #10 / BCC ->EFBE` | the ROM branches `BCS ->2EF8F`; `2EF77 LDA #DC / TRB $7E` is omitted |
| D6 | minor | §2.6 | `d[0x2CF30:0x2CF38]` printed as 7 bytes | `C2/8385` copies **8** (`C2/83A2` returns `A = 7`); byte 8 is `a9` = the opcode of `LDA #19` at `C2/CF37`, i.e. the descriptor overlaps code |
| D7 | minor | §6.5 | three `$299E` operand hits | a 4 MB scan finds **four**; `C1/3795` is a second `9E 29 98` false positive. Verdict (one writer) unchanged |
| D8 | minor (port hazard) | §2.6, §7.3, §7.6 | the post-save summary refresh is `C2/CF21` -> `C2/D32C` | the re-read is **`C2/CF58 JSR C2/D19F`, inside `C2/CF37`**, and `C2/CF37` **yields twice** (`C2/821E` at `CF55` and `CF5B`). `C2/D32C` is named but never disassembled |
| D9 | minor | §7.5 heading | "`src/menu.c:1626` NOT PORTED" | the comment is at **src/menu.c:2112** |
| D10 | minor | §3 table | `$7E:007F` writer `C2/CFA9` | `STA $7F` is at `2CFAB` (post-PC `2CFAD`); `C2/CFA9` is the post-PC of `STA $79` |
| D11 | refinement | §8.6 | "a port that hardcodes the pad layout will diverge once Config remapping is implemented" | `$F5` is **raw** (`C2/8527-852F`, `$00F2` layout, never remapped). Only A/B diverge: `C2/E9A2 LDA $2993,Y / AND $F0`, live `$2993 = 80` (A), `$2994 = 08` (B) |
| S1 | structural | §3 | — | a `### 3.3` with no `3.1` / `3.2` |
| S2 | minor | §8.8 | "`saves/com_jogo_salvo.state` is a field state" | it is an **overworld** state: measured `$7E:0100 = $01F1` |

### AV.2 D1 — the display-list blob loader, settled

`C2/F364` (cmd `0E`; cmd `0A` shares the handler) re-disassembled:

```
  2F364: PHP / LDA #FF / PHA / PLB      ; (the linear decoder desyncs here -- it prints "LDA #48FF";
  2F369: TDC                            ;  the bytes are A9 FF / 48 / AB = LDA #FF / PHA / PLB)
  2F36A: LDX $5B                        ; X = the blob pointer operand
  2F36C: LDA $0000,X                    ; A = the LENGTH BYTE at $FF:$5B
  2F36F: DEC A
  2F370: INX                            ; source = $5B + 1
  2F371: LDY $61                        ; destination = the current buffer base
  2F373: MVN $7E,$FF                    ; copies A+1 = lengthbyte bytes
```

Length bytes, and a byte-for-byte comparison against the live `$7E` image
(`scratchpad/menu/_av2_prm.raw.wram`, the frame with the prompt up):

```
B839->9100 lenbyte=0x1a(26)  src=24 fc 00 f3 01 0c fc 00 f7 01 0c fc 00 fb 01 1c   MATCH=True (26 B)
BAE2->9280 lenbyte=0x07(7)   src=10 00 50 ef 01 00 00                              MATCH=True (7 B)
BB68->9710 lenbyte=0x0d(13)  src=10 0c 93 a0 20 97 a0 20 97 a0 20 97 00            MATCH=True (13 B)
B61E->9380 lenbyte=0x1e(30)  src=08 1d 02 01 00 00 08 3d 00 02 01 01 08 5d 01 00   MATCH=True (30 B)
BCE5->9680 lenbyte=0x10(16)  src=00 08 a0 10 ef b0 00 00 08 17 00 04 00 10 00 e0   MATCH=True (16 B)
```

Only `$FF:B61E` (30 B, the node graph) was right. And the dossier's own §4.1 already contradicted its
§2.4 — it lists `7E/9100..9119 = 26` and `7E/9281..9286 = 6` — and the contradiction went unnoticed.
My independent diff (§AV.4) reproduces both runs exactly.

> **§8 OPEN item 2 is withdrawn.** `$7E:9100` begins `24 fc 00 f3 01 ...` because `$FF:B83A` begins
> `24 fc 00 f3 01 ...`; `$FF:B839 = 1a` is the count consumed by `C2/F36C`. Nothing else writes those
> bytes and there is no slide-in to look for. The two tables are 26 B and 7 B, and both are copies of
> ROM, so a port that wants them has them.

### AV.3 D2 — `$0D13 & $7F` IS HDMAEN

```
python tools/dis.py 28430 0x30 1 1
  2843C: STZ $420C
  2843F: JSR C2/85D6
  28442: LDA $13            ; DP = $0D00  ->  $7E:0D13
  28444: AND #7F            ; strip bit 7 = the pending $7E->$7E MVN flag of §3.3
  28446: STA $420C          ; <<< HDMAEN
  28449: LDA #FF / STA $01
  2844D: JSR C2/8473
  28450: LDA #09 / STA $2105       ; mode 1 + BG3 prio -- agrees with --ppuregs "mode=1"
```

Proof it resolves to `$0D13`, and that the low bits are the mask, on the oracle's own bytes. The two
`--peek` dumps hold `$0D13 = $D9` (slot list) and `$0D13 = $75` (prompt); `--hdma` on the same two
frames reports:

```
  $D9 & $7F = $59 = 0101_1001  ->  predicts ch 0,3,4,6
  measured    ch0=1 ch1=0 ch2=0 ch3=1 ch4=1 ch5=0 ch6=1 ch7=0        EXACT, all 8

  $75 & $7F = $75 = 0111_0101  ->  predicts ch 0,2,4,5,6
  measured    ch0=1 ch1=0 ch2=1 ch3=0 ch4=1 ch5=1 ch6=1 ch7=0        EXACT, all 8
```

Consequences:

* **§8 OPEN item 1 is CLOSED.** The store that drops channel 3 is `C2/8446 STA $420C`, fed by
  `$0D13`. `C2/D4CA LDA #75 / STA $0D13` (open) and `C2/D4E0 LDA #59 / STA $0D13` (close) are the
  entire visibility mechanism. `--loadcensus --censusat 00420C` is not needed.
* **§8 OPEN item 7 is CLOSED** for the values screen 6 uses. `$59` = ch 0,3,4,6 (colour math `$2131`,
  the `$212C` BG1 window, BG2 vscroll `$2112`, indirect `$2131`); `$75` = ch 0,2,4,5,6 (the same minus
  ch3, plus ch2 `$210D` and ch5 `$2129` — exactly the two channels `$FF:C16F` arms). `$D9` / `$F5` are
  the same masks with bit 7 set.
* §4.3's warning "Inferred, not proven: that the ROM achieves this by writing `$420C` without bit 3"
  should be deleted — it is traced now.
* §3.3's *headline* stands: `$0D13` is not a message id, bit 7 is the MVN-pending flag cleared by
  `C2/8628 TRB $13` (raw bytes at `28626` are `a9 80 | 14 13 | f0 25` = `LDA #80 / TRB $13 / BEQ
  ->28651`, and the linear decoder's garbage there is a desync, not a wrong address), and an operand
  scan of the whole 4 MB finds **41** `STA $0D13` and **zero** absolute `LDA $0D13` in bank `$C2` —
  the dossier's count is exact. Only "the low bits are the other per-frame upload jobs" is wrong. The
  upload jobs are the 8-byte descriptor queue at `$7E:0C00` with `$0D11` as its tail (`C2/8385`).

### AV.4 The remaining defects, in full

**D3 — §2.7's `C2/E9E2` listing is not a faithful transcription.** The real routine
(`python tools/dis.py 2E9E2 0x50 1 0`):

```
  2E9E2: PHP / SEP #30
  2E9E5: LDA $54 / STA $81
  2E9E9: LDA #80 / STA $0D1E        ; <<< DROPPED: $0D1E pre-seeded to the "no direction" sentinel
  2E9EE: LDX #00
  2E9F0: LDA $F5 / STA $5A
  2E9F4: LSR A / BCS ->2E9FE / INX / CPX #04 / BCC ->2E9F4
  2E9FC: BRA ->2EA1A                ; <<< DROPPED: the NO-PRESS GUARD (all four bits clear)
  2E9FE: STX $0D1E
  2EA01: LDA $0D1F / BNE ->2EA1A
  2EA06: LDA $54 / JSR C2/EA27      ; X = $54 * 6   (EA27: AND #00FF / ASL / PHA / ASL / ADC $01,S)
  2EA0B: TXY
  2EA0C: LDA $54 / STA $55          ; <<< DROPPED
  2EA10: LDA $0D1E / ASL A / TAX
  2EA15: REP #10                    ; <<< DROPPED
  2EA17: JSR ($EA1F,X)
  2EA1A: STZ $0D1F / PLP / RTS
```

Read literally, the dossier's version falls out of the bit-scan loop with `X = 4`, stores 4 into
`$0D1E`, and dispatches through `$EA1F + 8` = `$EA27` — the multiply helper — on **every frame with no
d-pad press**. The ROM branches away first. A port must reproduce the guard. Everything else in §2.7 is
correct, including the hand-decoded `d[0x2EA48]` = `85 54 10 06 a9 80 04 5a 14 54 60` =
`STA $54 / BPL / LDA #80 / TSB $5A / TRB $54 / RTS`, which I re-dumped and re-decoded.

**D4 — the param table.** `d[0x3FBD18:0x3FBD38]` = `03 00 05 00 05 00 04 00 01 00 01 00 03 00 01 00
05 00 05 00 05 00 ...`. `C2/ED43 LDA $BD18,Y` then `MVN` copies `A + 1`. So the byte counts are
00->4, 02->6, 04->6, 06->5, 08->2, 0A->2, 0C->4, 0E->2, **10->6, 12->6, 14->6**. The dossier's first
eight rows apply `BD18 + 1`; the last three print the raw table value. Unused by screen 6.

**D6 — the 8-byte descriptor, confirmed live.** `C2/8385` re-disassembled:

```
  28385: PHP / JSR C2/83A2 / MVN $7E,$C2 / BRA ->28395
  28395: SEP #20 / LDA $0D11 / CLC / ADC #08 / STA $0D11
  283A2: REP #31 / LDA $0D11 / AND #00FF / ADC #0C00 / TAY / LDA #0007 / RTS
```

`A = 7`, so `MVN` moves **8** bytes. The prompt-frame dump has `$7E:0C00 = 40 60 80 3e 7e 80 06 00`,
which is all eight bytes of `$C2:FBFF` verbatim (`C2/D4CD LDX #FBFF / JSR C2/8385`), and
`$7E:0C08 = 40 60 80 3e 7e 00 03 00` = `$C2:FC0D`. `d[0x2CF30:0x2CF38]` is `00 79 00 70 7e 00 06 a9`
and the trailing `a9` is `C2/CF37`'s own `LDA #19` opcode byte — worth stating, because a port that
copies "the descriptor" must know it is 8 wide and that this one runs into code.

**D8 — where the post-save summary actually comes from.** `C2/CF37` in full
(`python tools/dis.py 2CF37 0x2A 1 1`):

```
  2CF37: LDA #19 / STA $1E00 / LDA #8A / STA $1E01 / LDA #80 / STA $1E02 / JSL C7/0004   ; the jingle
  2CF4A: LDX $79 / LDA #FF / STA $0D49,X          ; occupancy := $FF   (measured: $0D49 = FF FF 00)
  2CF51: JSL FF/F9FB                              ; the SRAM write
  2CF55: JSR C2/821E                              ; <<< coroutine YIELD, one frame
  2CF58: JSR C2/D19F                              ; <<< the slot summary RE-READ (reads $30:0580,X)
  2CF5B: JMP C2/821E                              ; <<< coroutine YIELD, one frame
```

So the "Save # 2" the dossier photographs comes from `C2/D19F`, inside the save, not from
`C2/CF21 -> C2/D32C`. (`save_write.md` lines 68-73 already had this; §2.6 here re-tells the story
without it and lands on the wrong routine.) `C2/D32C` — which §2.6 glosses as "rebuild the selected
slot's summary panel" and which §7.6 leans on — is never disassembled in this dossier. It is:

```
  2D32C: PHP / SEP #30 / LDX $79 / LDA $0D79,X / STA $0D8C
  2D337: REP #31 / LDA $78 / AND #0300 / ADC #3E8C / STA $63     ; a BG2-chrome ($7E:3E00) address
  2D343: LDX #0418 / STX $5F / JSR C2/ECDB
  2D34B: LDA $63 / AND #FFC0 / ORA #0004 / STA $63
  2D355: LDA #0404 / STA $5F / JSR C2/EDF6 / JSR C2/EE7F         ; the window redraw pair
  2D360: SEP #20 / LDA $2991 / AND #07 / STA $0D8C
```

i.e. it repaints BG2 chrome; it does not read SRAM. **Implementation consequence:** §7.3's pseudo-code
runs the save as one straight-line call. In the ROM it spans **three** dispatcher frames (write, yield,
re-read, yield), which is why §6.3 sees `$299C` change at f323 and the prompt torn down at f326.

**D11 — what is and is not remappable.** `C2/8510-8544`:

```
  28527: LDA $F2 / EOR $FE / AND $F2 / STA $F0 / STA $F4     ; 16-bit: the NEW-PRESS word, RAW
  28531: SEP #10 / LDX $FD / LDA $F2 / AND $FE / BEQ ...     ; the auto-repeat assist, TSB $F4
  28545: ... JSR C2/8555
  28555: STZ $F6,X / LDA $F0,X / AND $2998,Y / CMP $2998,Y / ROR $F6,X   ; the REMAPPED copy
```

`$F4/$F5` is the raw new-press word in the `$00F2` layout (`$F4` = A `$80`, X `$40`, L `$20`, R `$10`,
B `$08`, Y `$04`, Sel `$02`, St `$01`; `$F5` = Right `$01`, Left `$02`, Down `$04`, Up `$08`). Measured
writer of `$7E:00F5` is `C2:8531` — the post-PC of `STA $F4` at `2852F`, i.e. the raw path.
`$F6..` is the remapped copy and the nav never touches it. What *does* go through Config is the
confirm/cancel pair, in `C2/E984`:

```
  2E99D: LDA $C2E9D9,X / TAY / LDA $2993,Y / AND $F0 / BEQ / LDA $00 / TSB $0D1D
         d[0x2E9D9] = 00 01 ...   ->  bit7 (A) uses $7E:2993, bit6 (B) uses $7E:2994
  live:  $7E:2993..299A = 80 08 40 08 02 04 10 20     (A=$80, B=$08, ... R=$10, L=$20)
```

So §8 item 6's warning is right about `A`/`B` and wrong about the d-pad. (`$0D1D`'s bit layout itself
was already pinned in `menu_sfx.md` lines 64-66 and is not re-derived here.)

### AV.5 WHAT WAS RE-DERIVED AND HELD (independent confirmations)

Every item below was produced from scratch in this pass and matched the dossier.

**ROM, byte-exact re-dumps**

* `d[0x2CED4:0x2CEDC]` = `dc ce fd ce 61 cf 92 cf`; `d[0x2D4F0:0x2D4F8]` = `f8 d4 06 d5 61 cf 92 cf`
  — screen `$0B` really does share entries 2 and 3. A port must share, not copy.
* `C2/CEDC`, `C2/CEFD`, `C2/CF61-CF8F`, `C2/D4BB`, `C2/D4D5` disassemble **line for line** as printed
  in §2.1 / §2.2, including `CF77 CMP #03 / BNE ->2CF88` (A-on-No == B) and `CF74 RTS` (no per-frame
  redraw in sub 2).
* `C2/ED31` and its two tables: `d[0x2ED59:0x2ED7D]` = `77 ed 05 ef aa ed 48 ef 32 f3 64 f3 37 f3 64
  f3 78 f3 b0 ed b6 ed` — every handler in §2.3's table is right. `C2/ED77` (fill), `C2/ED90`
  (`addr = $61 + (pos & $FF00) >> 2 + (pos & $1F) * 2`), `C2/F332` (`$61 := $5B`) and `C2/F337`
  (`$43x0/1 := $5D`, `$43x4 = $43x7 = $7E`, `$43x2/3 := $61 := $9000 + (($5B & $F0) << 3)`) all read
  exactly as described.
* The three display lists are byte-identical to §2.4: `$FF:C138` = `08 00 2e | 00 00 04 00 00 | 06 11
  00 17 02 00 | 0c 60 43 40 31 | 08 10 97 | 0e 68 bb | 08 80 93 | 0e 1e b6 | 08 80 96 | 0e e5 bc |
  ff`; `$FF:C16F` and `$FF:C191` likewise. Decodes check out: window at pos `$0216` = (22,2) 8x10,
  backup (2,2) 28x12 -> (2,32), restore the inverse.
* The string path: `C2/EF48 LDA $5B / ASL A / TAX / LDA $FFC457,X`; `d[0x3FC479]` = `87 c7` -> `$FF:C787`;
  `d[0x3FC787]` = `[28 ROM bytes at 0x3FC787: python tools/dis.py 0x3FC787 28]
  0a 00 00` — every byte of §2.5. Entry `$25` -> `$C912` also confirmed.
* **`C2/EF65` is confirmed NOT a VWF**: `STA $7E0000,X` stores the character code as the tile index and
  `LDA $7E / STA $7E0001,X` the attribute; the `$FF` special writes a second space at `X - $40` (one
  32-entry row back). The ⭐ correction to the `ct-menu-save-screen` memory stands.
* The node graph `d[0x3FB61E]` = `1e | 08 1d 02 01 00 00 | 08 3d 00 02 01 01 | 08 5d 01 00 02 02 |
  b8 3f 04 04 03 03 | b8 4f 03 03 04 04` — §2.7 and §7.2's `CT_SAVE_NODES` are exact, including the
  self-links on Left/Right for all five nodes.
* `d[0x2EA1F:0x2EA27]` = `45 ea 40 ea 3b ea 36 ea` — the reversed dispatch is real.
* `C2/D156` re-disassembled; `JSR $D156` (`20 56 D1`) occurs **exactly once in 4 MB**, at `C2/8127`.
  §2.8's "menu boot, not the Save screen" holds.
* `$299E`: `C2/E66A` is the only writer (`C1/33BC` and `C1/3795` are both `9E 29 98` = `STZ $9829,X`
  inside other instructions — I disassembled both windows). `$30:7FE1`: 4 sites, `$30:7FE2`: 6 sites,
  identical lists to §6.5. `C2/8042-8057` and `C2/E65D-E66A` read as printed; the new-game gate the
  dossier asserts parenthetically is real — `C2/E64C LDA $54 / CMP #03 / BCS ->2E65D`.
* `FF/FA65-FA79` re-read: `LDA $79 / STA $307FE0 / LDA $307FE1 / CMP $299E / BNE / INC A / STA $307FE1`.
  §7.5's "append `FF/FA6B..FA78`" is correct as far as it goes; the slot-index store `$30:7FE0 := $79`
  lives one instruction earlier and is documented in `save_write.md` (line 268), so a port following
  §7.5 alone would drop it. Not scored as a defect — the dossier declares that companion.
* The VWF: `C2/5DC4` matches §5.2 line for line; the advance table `d[0x260E6:0x26146]` is **byte-exact**
  to the dossier's transcription, and an independent sum of `The Millennial Fair` from that table gives
  **103**, the dossier's number. `d[0x25EAF]` = `bf 5e d2 5e d1 5e d0 5e cf 5e e5 5e e8 5e eb 5e` and
  the 2 bpp column table `d[0x25FE6]` = `00 00 10 00 ... f0 00 00 02 10 02 ...` both confirmed.
  `C2/5E36` really is 4x2 + 8x2 = **24** calls to `C2/5E89` with `$73 += $00F0` in between, i.e.
  12 pixel rows x 2 bitplanes in a 2-tile-tall strip.
  * one correction: the 4 bpp column table `d[0x26066]` is **not** "the same with stride `$20`" — it is
    `00 00 20 00 .. e0 00 | 00 01 20 01 .. e0 01 | 00 04 ..`, i.e. 8 columns per `$0100` block with a
    `$0400` jump. §5.2 already flags `C2/5F07` as read structurally only.
  * `$7E:6E00 <-> VRAM tile $100, 16 B/tile` is confirmed *directly*: I compared 96 tiles from my own
    dumps, tile `$13A` is byte-identical (`ff fb ff 9b df 9b ff fb fb 04 00 00 00 00 00 00`) at both
    `$7E:71A0` and VRAM word `$79D0`.
* The dispatcher width note in §2.1 is right: `C2/827D REP #30 ... 2828C SEP #20 / JMP ($0000)`, so
  `LDX $79` at `CF0A` and `CF4A` runs 16-bit.

**Oracle, re-measured**

* §6.1 / §6.2 / §6.3 all reproduce **frame for frame** on `saves/ow_w0.state` with the dossier's own
  scripts: the `$0D49` scan at f50/f52/f53, screen 6 at f117, the first save at f149
  (`$0D49 = FF0000` by `C2:CF51`, `$1E00 = 008A80` by `C7:0196`, `$299C = 0100` by `FF:FA0E`), the
  prompt at f301 (`$54 = 03` by `C2:D4C2`, `$0D13 = 75` by `C2:D4CD`, `$0D76 = 03` by `C2:EB15` = the
  throttled blip), the overwrite at f323 (`$299C = 0200`, jingle with `$0D76 = 00` = un-throttled),
  `$0067 = 0602` at f324, teardown at f326 (`$54 := 00` by `C2:D4E8`, `$0D13 := 59` by `C2:D4E3`).
  `$0079` never moves while the prompt is up.
* §4.1's WRAM diff reproduces: ten 16-byte runs at `7E/3EAC` stride `$40`, the `7E/4605..48FB` backup,
  `7E/9100..9119` (26), `7E/9281..9286` (6), `7E/0770..0771`, and **`$7E:2E00` unchanged**.
* §4.2 reproduces exactly, decoded from my own VRAM dump — the BG1 map at word `$5800` is
  **bit-identical before and during the prompt** and reads `Erase` (r02, col 23), `old` (r04),
  `save?` (r06), `Yes` (r08, col 25), `No` (r10, col 25), with the `$FF` duplicate spaces on r07/r09
  and attributes `00`/`20`.
* §4.3 reproduces: `$7E:9180` = `10 12 70 16 50 17 10 16 00`, and the collapsed per-line trace is
  `0..0 TM=17 | 1..16 TM=12 | 17..128 TM=16 | 129..208 TM=17 | 209..224 TM=16` — BG1 on for lines
  129..208 only. On the prompt frame ch3 is off and TM is `$17` on every line.
* §4.4 reproduces to the pixel: excluding `x < 100`, the framebuffer differs over **x 176..239,
  y 24..103** = 64 x 80; the hand's vacated ink is x 8..23, y 39..50.
* §6.4 reproduces: `$7E:0770` goes `08 1d 20 30` -> `b8 3f 20 30`, i.e. node 0 -> node 3 of `$FF:B61E`.
* §4.5 reproduces: my own render of the prompt frame shows the window with `Erase / old / save? /
  Yes / No`, hand on Yes, slot list still visible underneath; and after `A` on Yes the panel reads
  **`Save # 2`** with the hand back on slot 1 and `$0067` still `06`.
* The engine-side assertions in §7 are true today: `src/field.c:8613`
  `menuSfxSilent("save-overwrite-prompt-not-ported")`, `src/field.c:8550-8551`
  `(menuSaveCur +- 1) % CT_MENU_SAVE_SLOTS`, and `src/field.c:8560` deriving occupancy from
  `CtMenuSaveSlot()` on every press (the ⭐ in §7.3). Only the `src/menu.c` line number is stale (D9).

### AV.6 EVIDENCE THE ORIGINAL PASS DID NOT HAVE

Four measurements that strengthen or complete claims the dossier could only assert:

1. **`$7E:00BE` = `01` -> `02`.** The sub-state claim rested on a single frame of `$0068 = 02` (f324),
   which is weak because `$0068` is task-local and is swapped by the coroutine scheduler. The *saved*
   copy is what matters: the task table at `$7E:00B0` holds slot 1 = `02 01 6f 82 76 0a [01|02] 00` —
   status `02`, delay `01`, entryPC `$826F` (the screen dispatcher of `menu2_shell_exit.md`), savedSP
   `$0A76`, and **saved `$68` = 1 on the slot list, 2 while the prompt is up**. That is the durable
   proof, and it is visible in both `--peek` WRAM dumps.
2. **A non-zero `$0F00` round-trip.** Every measurement in §6 opened the prompt over slot 1, where
   `$0F00 = 0` and the restore is indistinguishable from doing nothing. Driving to slot 2 first:
   `f385 $0054=03*(C2:D4C2) $0079=01 $0F00=01*(C2:CF19)` then on `B`
   `f447 $0054=01*(C2:D4E8) $0079=01 $299C=0200 (unchanged)`. The `$54 := $0F00` restore is real.
3. **All four directions, and the silence of Left/Right.** One drive over the prompt gives
   `Down -> $00F5 = 04` (`$54` 3->4), `Left -> $00F5 = 02` (no move, `$0D76` stays 00 = **no blip**),
   `Right -> $00F5 = 01` (no move, no blip), `Up -> $00F5 = 08` (`$54` 4->3), `A on No ->
   $54 := 00 (C2:D4E8), $299C unchanged`, `B -> the same`. §2.7's table and §6.2's spot checks are
   complete and correct.
4. **`$0D47/$0D48` = `$DC`/`$03` while the prompt is up.** §8 item 3 guesses "base tile = style * 20
   into `$0D47`". Style `$8B` = 139, 139 * 20 = 2780 = `$0ADC`, low byte `$DC` — and the measured
   window tilemap does start `dc dd de dd ...` with attribute `$03` = the list's palette parameter.
   The guess is corroborated (the general 9-slice rule is still not derived).

**§8 item 8 — the field capture — is CORRECT, was uncited, and is still blocked.** The dossier asserts
"a field state whose map has `$7F01CE` bit15 set (a save point)" with no ROM address anywhere in the
file. `LDA $7F01CE` occurs exactly **once in 4 MB**, and it is the gate builder:

```
python tools/dis.py 2989B 0x1C 0 1
  29895: STZ $0D32 / STZ $0D34
  2989B: LDA #FFFF / STA $0D36
  298A1: LDA $0100 / CMP #01F0 / BCS ->298B5      ; loc >= $1F0 = OVERWORLD -> $0D34 stays 0 = ALLOWED
  298A9: DEC $0D32
  298AC: LDA $7F01CE / BMI ->298B5                ; bit15 SET -> $0D34 stays 0 = ALLOWED
  298B2: DEC $0D34                                ; bit15 CLEAR -> $0D34 = $FFFF -> C2/CEE8 AND #40 -> REFUSED
```

Measured across every menu fixture in `saves/`:

```
com_jogo_salvo    $0100=01F1  $7F01CE=0001  $0D34=01   (OVERWORLD -- the dossier calls it a field state: S2)
menu_open_gato2   $0100=0006  $7F01CE=0001  $0D34=FF
menu_open         $0100=0006  $7F01CE=0001  $0D34=FF
menu_open_fair    $0100=01B7  $7F01CE=0001  $0D34=FF
menu_open_fair2   $0100=01B7  $7F01CE=0001  $0D34=FF
ow_w0             $0100=01F0  $7F01CE=0001  $0D34=00
```

and driven live on `menu_open_gato2` the refusal is visible as a one-frame bounce:
`f131 7E0067=0600*(C2:8211)` then `f132 7E0067=0000*(C2:8211)` — screen 6 is entered and `C2/CEEC
STZ $67 / LDA #01 / STA $68 / JMP C2/EACC` throws it straight back to the ribbon. **No field fixture we
have can reach the Save screen**, so the absolute pixel-Y question of §4.4 / §7.4 stays open. The
capture that unblocks it is now specified precisely: any field state (`$0100 < $01F0`) whose
`$7F01CE` bit 15 is **set**, with at least one slot occupied.

### AV.7 COMMANDS RUN IN THIS PASS

```
python tools/dis.py 2CEDC 0x40 1 0        2CF1E 0x20 1 0     2CF5E 0x40 1 0
python tools/dis.py 2D4BB 0x40 1 0        2ED31 0x30 1 0     2ED77 0x40 1 0
python tools/dis.py 2F332 0x60 1 0        2EF48 0x20 1 0     2EF65 0x60 1 0
python tools/dis.py 2E9E2 0x50 1 0        2EA27 0x30 1 0     2E984 0x30 1 0
python tools/dis.py 2E9B5 0x30 1 0        28385 0x40 1 0     2D156 0x40 1 0
python tools/dis.py 28620 0x40 1 0        28030 0x40 1 1     2E65D 0x20 1 1
python tools/dis.py 3FFA60 0x30 1 1       28430 0x30 1 1     25DC4 0x74 1 1
python tools/dis.py 25E36 0x60 1 1        258B2 0x20 1 1     2D32C 0x40 1 1
python tools/dis.py 2D19F 0x30 1 1        28275 0x20 1 1     2CFA2 0x20 1 1
python tools/dis.py 2CF37 0x2A 1 1        28510 0x50 1 1     2989B 0x1C 0 1
python tools/dis.py 133B0 0x18 1 1        13788 0x18 1 1

# byte dumps + full-4MB operand scans (scratchpad/menu/_av2_d.py, _av2_scan.py)
python scratchpad/menu/_av2_d.py 2CED4:10 2D4F0:10 3FBD18:20 2ED59:24 3FC138:2A 3FC16F:24 \
        3FC191:10 3FB61E:22 3FC457:8 3FC479:4 3FC787:26 2FC0D:8 2CF30:8 2FBFF:8 2EA1F:8 2EA48:C
python scratchpad/menu/_av2_scan.py 299E abs | 307FE1 long | 307FE2 long | 0D13 abs | 420C abs | 0D34 abs

# oracle -- all from the repo root, ROM = Chrono_Trigger_USA_SNES.sfc
# PREFIX = 0:20,200:2,0:60,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:8,100:2
scratchpad/menu/ct_probe.exe <ROM> --monitor "7E0054:1,7E0067:2,7E0079:1,7E0D49:3,7E0F00:1,7E0D13:1,7E1E00:3,7E299C:2,7E0D76:1" \
  --from saves/ow_w0.state --btnscript "<PREFIX>,0:30,100:2,0:150,100:2,0:20,100:2,0:100" --diff --writer
scratchpad/menu/ct_probe.exe <ROM> --monitor "7E0054:1,7E0067:2,7E0079:1,7E0081:1,7E0D49:3,7E0F00:1,7E299C:2" \
  --from saves/ow_w0.state --btnscript "<PREFIX>,0:30,100:2,0:60,20:2,0:20,100:2,0:150,100:2,0:60,1:2,0:60" --diff --writer
scratchpad/menu/ct_probe.exe <ROM> --monitor "7E0054:1,7E00F5:1,7E0081:1,7E0F00:1,7E299C:2,7E0D76:1,7E0067:2" \
  --from saves/ow_w0.state --btnscript "<PREFIX>,0:30,100:2,0:120,100:2,0:30,20:2,0:20,40:2,0:20,80:2,0:20,100:2,0:40,100:2,0:30,10:2,0:20,1:2,0:40" --diff --writer
scratchpad/menu/ct_probe.exe <ROM> --monitor "7E0067:2,7E0054:1,7E0D34:1,7E0294:1,7E0299:1,7E029E:1,7E0D49:3,7E0079:1" \
  --from saves/menu_open_gato2.state --btnscript "0:20,80:2,0:20,80:2,0:20,80:2,0:20,80:2,0:20,80:2,0:20,100:2,0:120" --diff --writer
scratchpad/menu/ct_probe.exe <ROM> --peek saves/ow_w0.state scratchpad/menu/_av2_pre.raw --btnscript "<PREFIX>,0:150,100:2,0:120"
scratchpad/menu/ct_probe.exe <ROM> --peek saves/ow_w0.state scratchpad/menu/_av2_prm.raw --btnscript "<PREFIX>,0:150,100:2,0:120,100:2,0:30"
scratchpad/menu/ct_probe.exe <ROM> --peek saves/ow_w0.state scratchpad/menu/_av2_yes.raw --btnscript "<PREFIX>,0:150,100:2,0:120,100:2,0:30,100:2,0:120"
scratchpad/menu/ct_probe.exe <ROM> --hdma saves/ow_w0.state --btnscript "<PREFIX>,0:150,100:2,0:120"
scratchpad/menu/ct_probe.exe <ROM> --hdma saves/ow_w0.state --btnscript "<PREFIX>,0:150,100:2,0:120,100:2,0:30"
python tools/_fbpng.py scratchpad/menu/_av2_{pre,prm,yes}.raw scratchpad/menu/_av2_*.png
# analysis helpers: scratchpad/menu/_av2_cmp.py (blob vs WRAM), _av2_an.py (WRAM diff),
#                   _av2_an2.py (key bytes + VRAM diff), _av2_bg1.py (BG1 tilemap decode)
```

### AV.8 WHAT SHOULD CHANGE IN THE DOSSIER

1. §2.4 / §3: replace the four blob byte-counts with 26 / 7 / 13 / 16 and state that byte 0 of a
   `cmd 0E` operand is the MVN count. **Delete §8 item 2.**
2. §3.3: keep the bit-7 finding, replace "the low bits are the other per-frame upload jobs" with
   "bits 0..6 are HDMAEN, written by `C2/8446`". **Delete §8 items 1 and 7**, and delete §4.3's
   "Inferred, not proven" warning.
3. §2.7: restore `LDA #80 / STA $0D1E`, the `2E9FC BRA` no-press guard, `STA $55` and `REP #10`.
4. §2.6 / §7.3 / §7.6: credit the summary re-read to `C2/CF58 JSR C2/D19F` and record that `C2/CF37`
   yields twice (`C2/821E` at `CF55`, `CF5B`).
5. §7.5: the line number is `src/menu.c:2112`, and the `$30:7FE0 := $79` store (`FF/FA67`) belongs in
   the same paragraph as the `$7FE1` bump.
6. §8 item 6: `$F5` is raw; only `$0D1D`'s A/B bits (`$7E:2993`/`$2994`) are remappable.
7. §8 item 8: cite `C2/98A1-298B4` for the gate, correct `com_jogo_salvo.state` to *overworld*, and
   record that all five menu fixtures in `saves/` have `$7F01CE` bit15 clear, so the item is still
   blocked.
8. §3: add the missing `3.1` / `3.2`, or renumber `3.3`.

## 9. PORTED (2026-09-25): the slots' own colours, the save's frames, the prompt from the ROM, the preview after B

Graded by tools/menu_audit_cmp.py against the logical oracle on saves/ow_w0.state (the overworld): `owsavecol` (window
colour 3 set in Config, slot 1 saved, B, B -- f100-f690 0 differ) and `owsave2` (the same, then the overwrite: the prompt,
Yes -- f100-f730 0 differ); checks `menu-save-slot-colours-and-post-save-frames-vs-oracle`,
`menu-save-overwrite-prompt-frames-vs-oracle`, and `menu-save-overwrite-prompt` re-pinned.

* **Each slot in its own colour.** C2/D266 reads every VALID save's colour (`LDA $300591,X / AND #07 / STA $0D79,Y`, the
  save's own $2991); C2/D329 draws the slot with `$0D8C` = that colour: C2/ECDB -> C2/EDD3 `$0D47` = colour * 20 (the open's
  C2/9724 lays all eight colour sets as tiles 0x300 + 20 v), the palette row slot + 1, C2/ED08 putting FF/B210 + 16 v on
  its colours 8-15. MEASURED: `$0D79` 00 -> 03 after the save. Port: menu.c `CtMenuSaveSlotColours` (from the SRAM image
  each frame), `mSlotGlyph` / `mChromeInk`.
* **The save's frames.** C2/CF37 = the jingle, `$0D49,X` = FF, `JSL FF/F9FB` (the write), `JSR C2/821E` (a frame),
  C2/D19F, `JMP C2/821E` (another); then C2/CF21 queues the rows' uploads (FC0D: BG2 rows, CF30: BG3 glyphs) and C2/D32C
  redraws the summary panel. MEASURED: the A at 520 -> the row (title and colour) at f523, the panel at f524 -- two and
  three frames after the port had shown them. The Yes (C2/CF7B) runs the same C2/CF37 IN ITS FRAME and C2/D4D5 closes the
  prompt only after the two yields: MEASURED the Yes at 600 -> the prompt still up at f602, closed at f603, "Save # 2" at
  f604 (the port wrote a frame late and closed at once). Port: field.c `menuSaveShowSram` + the three holds.
* **The prompt is C2/ED31's op 04, not a picture.** C2/D4BB: `$54` = 3, `LDX #C16F / JSR C2/ED31` (op 08 = the BG2 buffer
  $3E00, op 02 = the backup of (2, 2) 28 x 12, op 04 `8B 03 16 02 08 0A` = the window), `$0D13` = 75. C2/EDD3 turns 8B into
  `$0D47` = 0x3DC (bit 7: ((a << 2) & FF) * 5 -- the window colour's own tiles; clear: `$0D8C` * 20); C2/EDF6 lays the
  frame (top t, (t+1, t+2) repeated, t | 3; the sides through `EOR #3`; the bottom from (t' | 3) + 1) and C2/EE7F the fill
  from C2:EEF5 (`0C 0D 10 11 / 0E 0F 12 13 / 10 11 0C 0D / 12 13 0E 0F` + the base, four rows repeated by C2/EF2B) --
  exactly the oracle's BG2 words at (22..29, 2..11). `$0D13` = 75 switches on HDMA channel 5 -> $2129 from FF/BAE3 (`10 00
  50 EF 01 00`): with the Save register record FF:BCE6 ($2123..$212F: W1 16..239, W2 left 0xB0, $212B = 08 = W1 XOR W2)
  x 176..239 of lines 16-95 leave the colour math -- no ramp, no band under the prompt (--hdma: `W2=176..239` lines
  17-96). The prompt holds the screen's ONE nav hand, so the slot list shows none. MEASURED (owsave2): the prompt in
  colour 3's bricks; and on com_jogo_salvo (colour 0) the generated window is 0 px against the oracle over all 64 x 80.
  Port: menu.c `mRomWindowCells`, `CtMenuRenderSavePrompt`, `CtMenuSavePromptUp`; src/data/menu_saveprompt.h removed.
* **The ribbon's Save preview after B** is the screen's last picture: C2/9A98 -> C2/9B71 -> C2:9B83[5] = C2/D0DB only sets
  `$0D13` = 19 (the preview's HDMA, no band channel), so the rows show bright, no hand, and the summary panel of the slot
  the cursor left stays. A preview the ribbon BUILDS (moving onto the tab) ends C2/D10D's row loop with `$79` = 3, which
  C2/D36C draws as no panel. MEASURED (owsavecol, B at 640): "Present / TIME 00:02 / 600G / Save # 1" and the party.
  Port: `CtMenuSetSaveHoverPanel`, field.c `menuSaveHoverPanel` (reset when the ribbon moves or the menu opens).
* Found on the way: tools/menu_audit_cmp.py ran the port on the repo's own `ct_sram.srm`, so the save drive wrote a test
  save there (restored to an erased cart; the drive now gets its own erased battery, CT_SRAM + CT_SRAM_FRESH), and
  `menu-save-overwrite-prompt` had been pinned on whatever that file held (now CT_SRAM_FRESH=1).

* **The boot Load screen (screen $0E) too** (2026-09-25): its rows are the same C2/D266 / C2/D329 -- a saved slot in its
  own colour (tiles 0x300 + 20 v, rows 1-3 of the loader's palette). MEASURED: saves/field_re/colsave3.srm (the logical
  oracle's own battery after the owsavecol drive, cut out of its PEEK_SAVE state) booted on the cartridge with
  CT_ORACLE_SRAM reads $0D79 = 03 00 00 and the bricks; tools/boot_loader_cmp.py grades the whole screen 57344 / 57344
  (and saves/ct_real_save.srm, colour 0, too). Check menu-boot-loader-slot-colours-vs-oracle.
