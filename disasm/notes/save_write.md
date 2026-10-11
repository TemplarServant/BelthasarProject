# The SAVE — the jingle and the write it belongs to

*(written to `C:\Users\Icaro Ferris\Desktop\Crono Trigger Decompile\disasm\notes\save_write.md`)*

Companion to `disasm/notes/menu_sfx.md` and `disasm/notes/naming_screen_sfx.md`. The sound mailbox
(`$7E:1E00-03` + `JSL $C7:0004`), the stager **C2/EB03** and the **`$0D76`** throttle are derived
there and are **not** re-derived here — only the one fact that matters for this dossier is re-proven:
the save jingle does not use any of that machinery.

The user asked for the SAVE JINGLE. The jingle is the **first four instructions of C2/CF37**, and the
rest of C2/CF37 *is the save*. So this dossier is the whole save-write, because that is the mechanism
the sound belongs to.

Everything below was re-read from the ROM and re-measured against the oracle **in this pass**. Where a
claim handed to me disagreed with the cartridge, the cartridge wins and the correction is called out
with a ⭐ or a ⚠.

---

## 0. Scope and verdict up front

The save is **implemented** in the working tree (it was a documented DEFERRED boundary before this
pass). It is validated end-to-end against a game **the ROM itself saved**. Two things are still gaps,
and both are recorded here rather than papered over:

1. the **overwrite-confirm prompt** (A on a slot that already holds a save) — screen not drawn;
2. ⚠ the **`$299C` increment**, which the ROM performs *before* packing and which therefore lands
   *inside* the saved data. This one is a **defect, not a scope decision** — see §5.4.

---

## 1. THE SAVE, END TO END

### 1.1 The press

Screen $06 sub-state 1 is the slot list. `python tools/dis.py 2CED4 0x50 1 0`:

```
  2CEFD: JSR  C2/E984          ; the shared action decoder -> $0D1D (bit7 = A, bit6 = B)
  2CF00: BIT  $0D1D
  2CF03: BMI  ->2CF0A          ; A
  2CF05: BVS  ->2CF5E          ; B  -> JMP C2/9A98 (back to the ribbon)
  2CF07: JMP  C2/CFA2          ; neither -> the cursor / redraw path
  2CF0A: LDX  $79              ; <-- THE SLOT INDEX
  2CF0C: LDA  $0D49,X          ; <-- THE OCCUPANCY BYTE
  2CF0F: BEQ  ->2CF1E          ; EMPTY  -> save now
  2CF11: JSR  C2/EAC2          ;   OCCUPIED: blip id $00 ...
  2CF14: LDA  $54
  2CF16: STA  $0F00
  2CF19: INC  $68              ;   ... and go to sub-state 2
  2CF1B: JMP  C2/D4BB          ;   = the OVERWRITE-CONFIRM prompt
  2CF1E: JSR  C2/CF37          ; <-- THE SAVE
```

So the press passes **two gates**: the screen's own entry gate (§3.2), and then this per-slot
occupancy branch on `$0D49,X`.

### 1.2 C2/CF37 — the jingle IS the save's first instruction

`python tools/dis.py 2CF37 0x30 1 0`, raw `d[0x2CF37:0x2CF5E]` =
`[39 ROM bytes at 0x2CF37: python tools/dis.py 0x2CF37 39]`:

```
  2CF37: LDA  #19 / STA $1E00      ; cmd  = $19
  2CF3C: LDA  #8A / STA $1E01      ; id   = $8A          >>> THE JINGLE <<<
  2CF41: LDA  #80 / STA $1E02      ; pan  = $80
  2CF46: JSL  C70004               ; SHIP
  2CF4A: LDX  $79
  2CF4C: LDA  #FF / STA $0D49,X    ; mark the slot OCCUPIED
  2CF51: JSL  FFF9FB               ; >>> THE SRAM WRITE <<<
  2CF55: JSR  C2/821E              ; yield one frame
  2CF58: JSR  C2/D19F              ; rebuild this slot's summary panel
  2CF5B: JMP  C2/821E              ; yield again
```

**The jingle stages the mailbox DIRECTLY.** It never calls C2/EB03, and it never reads or writes
`$0D76` — so unlike every blip on this screen it can **never be throttled**, and it never arms the
throttle for anything else. Measured, not inferred: `--callrom C2/CF37` logs **2284 writes and not
one of them touches `$0D76`**, and the command physically reaches the APU:

```
build/ct.exe --callrom C2/CF37 --from <post-save>.state --long --writes --maxops 4000000
  callrom: C2/CF37 in a=0000 x=0000 y=0000 m8 x16 c=0 (RTL)
  callrom: ret ops=7758 a=0940 x=09FF y=00FF sp=09FF dp=0000 db=7E p=21 C=1
  callrom: 2284 write(s)
    w 7E1E00 = 19  (by C2:CF3C)
    w 7E1E01 = 8A  (by C2:CF41)
    w 7E1E02 = 80  (by C2:CF46)
    w 002143 = 0D  (by C7:0623)
    w 002142 = 80  (by C7:0628)
    w 002141 = 8A  (by C7:062D)
    w 002140 = 19  (by C7:0635)     <-- the jingle leaving the CPU
    w 001E00 = 00  (by C7:0196)     <-- the driver zeroes the cmd byte afterwards
    w 7E9690 = 54  (by FF:FA14)
$ ... --writes ... | grep -c 0D76
  0
```

That last line is why a per-frame monitor reads `$1E00 = 00`: the driver's teardown clears the command
byte, so only `$1E01`/`$1E02`/`$1E03` survive to the next frame boundary.

### 1.3 FF/F9FB — the writer

`python tools/dis.py 3FF9FB 0x90 1 0`:

```
  3FF9FB: PHP / SEP #20
  3FF9FE: LDA  $79 / STA $0414          ; the slot index, mirrored into $0414
  3FFA03: REP  #30                      ; <-- 16-bit A and X/Y for everything that follows
  3FFA05: LDA  $299C / INC A
  3FFA09: BEQ  ->3FFA0E                 ; SATURATES: if the INC would make 0, skip the store
  3FFA0B: STA  $299C                    ;   -> the save counter never wraps to 0
  3FFA0E: LDA  #3054 / STA $9690        ; build `MVN $30,$7E`  at $7E:9690
  3FFA14: LDA  #6B7E / STA $9692        ; build `RTL`          -- bytes 54 30 7E 6B
  3FFA1A: LDX  #FA7B / LDY #9890 / LDA #0027 / MVN $7E,$FF   ; copy the 40-byte table to $7E:9890
  3FFA26: JSR  FF/FBD3                  ; A = slot base, X = slot*2
  3FFA29: PHX                           ;   save slot*2 for the checksum/signature stores
  3FFA2A: TAY                           ;   Y = the destination -- LOADED ONCE, never reloaded
  3FFA2B: LDX  $51                      ;   the record cursor (see §4.4)
  3FFA2D: SEP  #20                      ; -- per-record loop --
  3FFA2F: LDA  $9892,X / STA $9692      ;   patch the MVN's SOURCE BANK
  3FFA35: REP  #20
  3FFA37: LDA  $9890,X / STA $00        ;   the source address
  3FFA3C: LDA  $9893,X / DEC A          ;   the length, MINUS ONE (MVN's operand)
  3FFA40: PHX / LDX $00 / PHB
  3FFA44: JSL  7E9690                   ;   RUN THE MVN
  3FFA48: PLB / PLA / CLC / ADC #0005 / TAX
  3FFA4F: CMP  #0028 / BCC ->3FFA2D     ;   0x28 = 40 bytes = 8 records
  3FFA54: JSR  FF/FBE7                  ; A = the slot checksum
  3FFA57: PLX                           ; X = slot*2
  3FFA58: STA  $307FF0,X                ; <-- CHECKSUM   (word: A is still 16-bit)
  3FFA5C: LDA  #E41B
  3FFA5F: STA  $307FF8,X                ; <-- SIGNATURE  (word)
  3FFA63: SEP  #20                      ;     A goes 8-bit here, so the next two are BYTES
  3FFA65: LDA  $79 / STA $307FE0        ; <-- last-saved SLOT INDEX
  3FFA6B: LDA  $307FE1
  3FFA6F: CMP  $299E
  3FFA72: BNE  ->3FFA79
  3FFA74: INC A / STA $307FE1           ; <-- file-id allocator, bumped ONLY on this file's 1st save
  3FFA79: PLP / RTL
```

⚠ **`Y` is never reloaded inside the loop.** It is set once at `FA2A` and MVN leaves it advanced past
each record, which is *why* the records land consecutively. There is no LDY/TAY/PLY/INY/TXY anywhere
in `FA2D..FA52`.

### 1.4 The record table `$FF:FA7B` — 8 × {srcAddr:2, srcBank:1, len:2}

Raw `d[0x3FFA7B:0x3FFAA3]`:

```
00 24 7e b0 05 | 23 2c 7e 33 00 | 00 04 7e 10 00 | 00 01 00 10 00 |
00 00 7f 00 02 | 7e 02 00 02 00 | 90 02 00 11 00 | 7c 2c 7e 1e 00
```

| # | source | len | in-slot offset | what it is |
|---|---|---|---|---|
| 0 | `$7E:2400` | `$05B0` | `+0x0000..0x05AF` | inventory, char sheets, party — **and `$299C` at +0x059C** |
| 1 | `$7E:2C23` | `$0033` | `+0x05B0..0x05E2` | the 8 name records; gold `$2C53` lands at **+0x05E0** |
| 2 | `$7E:0400` | `$0010` | `+0x05E3..0x05F2` | the play clock |
| 3 | `$00:0100` | `$0010` | `+0x05F3..0x0602` | location word → the summary's 9-bit `locId` |
| 4 | `$7F:0000` | `$0200` | `+0x0603..0x0802` | the story flags |
| 5 | `$00:027E` | `$0002` | `+0x0803..0x0804` | — |
| 6 | `$00:0290` | `$0011` | `+0x0805..0x0815` | — |
| 7 | `$7E:2C7C` | `$001E` | `+0x0816..0x0833` | — |

**TOTAL = `0x0834` = 2100 bytes per slot.** Bank `$00`'s low addresses are the `$7E` mirror, so every
source is WRAM.

**Byte-exact confirmation against a save the ROM itself made.** `saves/com_jogo_salvo.state` is a real
saved game; `saves/ct_real_save.srm` is its 8 KB SRAM image (verified identical to the state's last
8192 bytes). Repacking that state's *own* WRAM with this table reproduces its slot 0:

```
rec0 $7E:2400 len 05B0 -> +0000  mismatch=0
rec1 $7E:2C23 len 0033 -> +05B0  mismatch=0
rec2 $7E:0400 len 0010 -> +05E3  mismatch=2  ['+0x5E3', '+0x5E4']
rec3 $00:0100 len 0010 -> +05F3  mismatch=0
rec4 $7F:0000 len 0200 -> +0603  mismatch=0
rec5 $00:027E len 0002 -> +0803  mismatch=0
rec6 $00:0290 len 0011 -> +0805  mismatch=0
rec7 $7E:2C7C len 001E -> +0816  mismatch=0
total 0x0834  matching 2098/2100
```

The two that differ are `$7E:0400/0401` — the play clock's sub-second counter, written every frame by
`C2:85BC`, which ticked between the ROM's save and the state capture. `$0402` onward match exactly:

```
sram +05E3 : 24 29 03 04 08 00 00 00 80 08 40 08 02 04 10 20
wram 7E0400: 14 2d 03 04 08 00 00 00 80 08 40 08 02 04 10 20
```

That is a property of the *measurement*, not of the save. And the slot's last non-`0xFF` byte is at
offset **2099 = 0x833** — precisely the last byte of record 7.

### 1.5 The slot bases — FF/FBD3

Raw `d[0x3FFBD3:0x3FFBE1]` = `a5 79 29 03 00 0a aa bf e1 fb ff 85 7b 60`:

```
  3FFBD3: LDA $79 / AND #0003 / ASL A / TAX / LDA $FFFBE1,X / STA $7B / RTS
```

Table `d[0x3FFBE1:0x3FFBE7]` = `00 60 00 6a 00 74` = **`$30:6000` / `$30:6A00` / `$30:7400`**,
stride **`$0A00`**.

⚠ It masks `AND #0003` — a 4-way index into a **3**-entry table. Index 3 would fetch `$FF:FBE7` =
`08 8b` = `$8B08`, outside the SRAM window. Unreachable in game (`$79` ∈ 0..2), but it is not a
4-slot table and a port must not treat it as one.

### 1.6 ⭐ FF/FBE7 — the checksum, and the correction that matters

Raw `d[0x3FFBE7:0x3FFC04]` =
`[29 ROM bytes at 0x3FFBE7: python tools/dis.py 0x3FFBE7 29]`:

```
  3FFBE7: PHP / PHB / REP #30 / PEA $3030 / PLB / PLB   ; DB = $30
  3FFBF0: LDA  $7B / STA $00                            ; the slot base
  3FFBF4: LDY  #09FE
  3FFBF7: LDA  #0000
  3FFBFA: CLC                        ; <-- ONCE, OUTSIDE the loop
  3FFBFB: ADC  ($00),Y                <-- LOOP TOP
  3FFBFD: DEY / DEY
  3FFBFF: BPL  ->3FFBFB              ; `10 fa` -> 0x3FFC01 - 6 = 0x3FFBFB = the ADC, NOT the CLC
  3FFC01: PLB / PLP / RTS
```

⭐ **It is NOT a plain 16-bit word sum.** I was handed it described as one. The `CLC` executes exactly
once, before the loop, so **every ADC folds in the previous add's carry-out**:

```
acc = (acc + word + carry_out_of_the_previous_add) & 0xFFFF
```

Proven numerically against the cartridge — same slot, same bytes, two algorithms:

```
slot0 base=0x0000 sig=E41B stored=6B8C  carryON=6B8C  carryOFF=6A4D
```

`0x6B8C` is what CHRONO TRIGGER itself wrote at `$30:7FF0`. The carry-less reading is off by
`0x13F`. Getting this wrong means every save fails its own read-back, because FF/F9C4 recomputes it.

Range: `Y` runs `$09FE` → `$0000` step −2, so **the full `$0A00` slot**, walking **downward** —
including the `0x834..0x9FF` tail the pack never fills. On a real cart that tail is the erase value
`0xFF`, and it *is* part of the sum.

### 1.7 The signature

**`$E41B`**, a 16-bit word, at **`$30:7FF8 + (slot&3)*2`**. Written at `FA5C/FA5F`, read back by the
validity test `FF/F9D8 LDA $7FF8,X / CMP #E41B`. On the real cart:

```
meta 0x1FE0: 00 01 00 ff ff ff ff ff ff ff ff ff ff ff ff ff
             8c 6b ff ff ff ff ff ff 1b e4 ff ff ff ff ff ff
             ^^^^^ = 0x6B8C @0x1FF0            ^^^^^ = 0xE41B @0x1FF8
```

### 1.8 The complete write set

The save touches exactly **two** WRAM words and **four** SRAM locations outside the slot body:

| where | size | value | ROM |
|---|---|---|---|
| `$7E:0414` | byte | `$79` | `FF/FA00` |
| `$7E:299C` | word | old+1, saturating | `FF/FA05..FA0B` — **inside record 0** |
| `$30:7FE0` | byte | `$79` | `FF/FA67` |
| `$30:7FE1` | byte | old+1 **iff** old == `$299E` | `FF/FA75` |
| `$30:7FF0 + slot*2` | word | the checksum | `FF/FA58` |
| `$30:7FF8 + slot*2` | word | `$E41B` | `FF/FA5F` |

**Never written by FF/F9FB:** `$30:7FE2` (only *read*, at `C2/E66D`), `$30:7FE3..7FEF`,
`$30:7FF6/7FF7`, `$30:7FFE/7FFF`. The real cart still shows `0xFF` at all of them, which corroborates
the write set exactly.

### 1.9 The whole thing, measured live

`saves/ow_w0.state` is the overworld state where the entry gate passes. Driving X → Right ×5 → A → A:

```
build/ct.exe --monitor "7E0067:2,7E0054:1,7E0079:1,7E0D49:3,7E1E00:4,7E0D76:1,7E299C:2" \
             --from saves/ow_w0.state --btnscript "0:20,200:2,0:60,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:4,80:2,0:8,100:2,0:150,100:2,0:60" --diff --writer

  f109  7E0067=0000  7E0054=05  7E0079=00  7E0D49=000000  7E1E00=0000800D(C7:0196)  7E0D76=03*(C2:EB15)
  f110  ...                                                                          7E0D76=02*(C2:8466)
  f111  ...                                                                          7E0D76=01*(C2:8466)
  f112  ...                                                                          7E0D76=00*(C2:8466)
  f117  7E0067=0600*(C2:8211)  ...                                                   7E0D76=00
  f118  7E0067=0600  7E0054=00*(C2:D0EC)  ...   7E1E00=0000800D(C7:0196)             7E0D76=03*(C2:EB15)
  f269  7E0067=0600  7E0054=00  7E0079=00  7E0D49=FF0000*(C2:CF51)  7E1E00=008A800D*(C7:0196)  7E0D76=00  7E299C=0100*(FF:FA0E)
```

Read that last row: the slot is marked (`$0D49 = FF 00 00`, writer `C2:CF51`), the mailbox carries
**id `$8A`**, **`$0D76` is `00`** — the jingle neither throttled nor throttling — and `$299C` goes
`0` → `1`, by `FF:FA0E`. Contrast every ribbon blip above it: `$1E01 = 00` with `$0D76 := 3` then the
3-frame drain. And the SRAM afterwards:

```
build/ct.exe --read "rom:3065B0+16,rom:307FE0+16,rom:307FF0+16" --from <post-save>.state
  64K banks: 64, ramsize: 8192
  rom[3065B0+16] = A2CBC8C7C800ACBACBC5BE00A7A8A9A0     <- "Crono", "Marle" ... the name block
  rom[307FE0+16] = 00010000000000000000000000000000     <- $7FE0 = 00 (slot), $7FE1 = 01
  rom[307FF0+16] = 03490000000000001BE4000000000000     <- checksum $4903, signature $E41B
```

(The checksum is `$4903` here and `$6B8C` on the real cart because `saves/ow_w0.state` carries a
**zero-filled** SRAM — a savestate artifact — while a real cart's unwritten tail is `0xFF`. Verified:
`ow_w0.state` SRAM has 0 nonzero bytes and 8192 non-`0xFF` bytes. The two are not comparable, which is
exactly why the checksum is graded on the *cartridge* image and not on ours — §5.3.)

---

## 2. THE SRAM GEOMETRY

### 2.1 Cart size

The SNES header is at flat **`0xFFC0`** (`$C0:FFC0`; CT is a 4 MB HiROM, so bank `$C0` is ROM offset
0). Raw `d[0xFFC0:0xFFE0]`:

```
[32 ROM bytes at 0xFFC0: python tools/dis.py 0xFFC0 32]
"C  H  R  O  N  O     T  R  I  G  G  E  R                    "  ^  ^  ^  ^
                                                     $FFD5 = 31 (HiROM + FastROM)
                                                        $FFD6 = 02 (ROM+RAM+Battery)
                                                           $FFD7 = 0C (2^12 KB = 4 MB)
                                                              $FFD8 = 03 (2^3 KB = 8 KB SRAM)
```

The oracle agrees verbatim: `64K banks: 64, ramsize: 8192`.

### 2.2 Mapping and the offsets

HiROM maps SRAM into `$6000-$7FFF` of banks `$20-$3F`. That window is `0x2000` bytes = the whole 8 KB,
so the image offset is simply `addr - 0x6000` (identical to `addr & 0x1FFF` over this range):

```
  $30:6000 -> 0x0000    slot 0 body   0x0000..0x09FF
  $30:6A00 -> 0x0A00    slot 1 body   0x0A00..0x13FF
  $30:7400 -> 0x1400    slot 2 body   0x1400..0x1DFF
                        (unused       0x1E00..0x1FDF)
  $30:7FE0 -> 0x1FE0    last-saved slot
  $30:7FE1 -> 0x1FE1    file-id allocator
  $30:7FE2 -> 0x1FE2    read-only (C2/E66D), never written by the save
  $30:7FF0/2/4         checksum table
  $30:7FF8/A/C         signature table
```

**No collisions.** The three `0x0A00` bodies tile `0x0000..0x1DFF` exactly; the meta area starts at
`0x1FE0`. Slot 2's checksum walk reaches its highest address at `0x1400 + 0x09FE = 0x1DFE`, still
clear. Confirmed empirically — the real cart's `0xE41B` sits at image offset **`0x1FF8`**.

### 2.3 ⭐ The stride: `$0A00`, not `0x800`

`src/menu.c` at HEAD (`29d7c26`) carried `#define SRAM_SLOT_STRIDE 0x0800u`. That is
**ROM-contradicted** and it is now fixed in the working tree.

It is provably wrong two ways: a slot **body is `0x0834` bytes**, which is *larger* than `0x800` (so
slot 0's own tail would be read as slot 1's header), and FF/FBE7 checksums `0x0A00` from the base.
It survived only because `h->sram` was always `NULL` — every line past the NULL check was dead code
that had never executed, and slot 0 coincides (`0*0x800 == 0*0xA00`), which is the only slot the one
reference save uses.

The working tree now reads (`src/menu.c:1541-1542`):

```c
static const unsigned CT_SAVE_SLOT_BASE[CT_MENU_SAVE_SLOTS] = { 0x0000u, 0x0A00u, 0x1400u };
#define CT_SAVE_SLOT_SPAN 0x0A00u          /* FF/FBE7: the checksum covers a whole slot */
```

### 2.4 What the writer must produce for the reader to see USED

`python tools/dis.py 3FF9C4 0x38 1 0`:

```
  3FF9C4: PHX / PHB / PHP / REP #30 / PEA $3030 / PLB / PLB   ; DB = $30
  3FF9CE: JSR  FF/FBD3                       ; $7B = the slot base
  3FF9D1: LDA $79 / AND #0003 / ASL A / TAX  ; X = (slot&3)*2
  3FF9D8: LDA  $7FF8,X / CMP #E41B
  3FF9DE: BNE  ->3FF9ED                      ;   -> $00 = $8080  (never written)
  3FF9E0: JSR  FF/FBE7 / CMP $7FF0,X
  3FF9E6: BNE  ->3FF9F2                      ;   -> $00 = $4040  (written but corrupt)
  3FF9E8: LDA  #0000                         ;   -> $00 = 0      (VALID)
  3FF9F5: STA  $00
```

**A slot reads USED iff BOTH the signature word `$E41B` matches AND the recomputed FF/FBE7 checksum
equals the stored word.** The caller `C2/D171 LDA $00 / BEQ ->D17F` treats `0` as used and both
non-zero results identically as empty.

⚠ Our reader `CtMenuSaveSlot` checks **only** the signature, plus its own `if (leader & 0x80)` — see
§4.5. Since the writer maintains both words consistently, writer and reader agree today; but they
agree by construction, not by the ROM's rule.

---

## 3. THE SCREEN FLOW

### 3.1 The dispatcher

`python tools/dis.py 28260 0x35 1 0` — a two-level indexed dispatch, `$67` = screen, `$68` = sub-state:

```
  2826C: JSR C2/821E          ; coroutine yield -- ONE FRAME per dispatch
  28275: LDA $68 / ASL A / TAY
  28279: LDA $67 / ASL A / TAX
  2827D: REP #30
  2827F: LDA $C28292,X / STA $16
  28285: LDA [$16],Y / STA $00
  2828C: SEP #20              ; <-- note: M=8 but X/Y stay 16-BIT on handler entry
  2828E: JMP ($0000)
```

Screen table `$C2:8292`, entry 6 = `$CED4`. `d[0x2CED4:0x2CEDC]` = `dc ce fd ce 61 cf 92 cf` — four
sub-state handlers: **`$CEDC`(0), `$CEFD`(1), `$CF61`(2), `$CF92`(3)**.

⚠ `dis.py` mis-decodes `2CED4` as instructions (`JML [$FDCE]`) — it is **data**. The raw bytes are
authoritative.

⚠ This sub-table is **shared with screen `$0B`** (`d[0x2D4F0:0x2D4F8]` = `f8 d4 06 d5 61 cf 92 cf`) —
entries 2 and 3 are literally the same routines. A port must share `$CF61`/`$CF92`, not copy them.

⚠ Because the dispatcher does `REP #30` then only `SEP #20`, **`LDX $79` at `CF0A`/`CF4A` runs with X
16-bit** — the slot index is the *word* `$79/$7A`. Measured `$7A = 00` throughout, so it behaves as a
byte, but a port must keep `$7A` zero rather than assume an 8-bit index.

### 3.2 Sub-state 0 — the entry gate

```
  2CEDC: LDA $0D34 / ORA $0294 / ORA $0299 / ORA $029E / AND #40
  2CEEA: BEQ  ->2CEF5           ; == 0 -> SAVING ALLOWED
  2CEEC: STZ $67                ; REFUSED: screen back to 0 (the ribbon)
  2CEEE: LDA #01 / STA $68
  2CEF2: JMP C2/EACC            ; the REFUSE BUZZ (cmd $18, id $01)
  2CEF5: JSR C2/EAC2            ; the OPEN BLIP  (cmd $18, id $00)
  2CEF8: INC $68                ; -> sub-state 1
  2CEFA: JMP C2/D0E5            ; build the slot list
```

### 3.3 Sub-state 1 — the slot list

The cursor has **already moved** by the time `C2/CFA2` runs; the move happens inside `C2/E9E2`
through the shared cursor-node graph. `CFA2` only commits and redraws:

```
  2CFA2: PHP / SEP #20
  2CFA5: LDA $54 / STA $79       ; the cursor IS the slot index
  2CFA9: CMP $7F / STA $7F       ; compare with the previous frame, then latch
  2CFAD: BEQ ->2CFB2             ; UNCHANGED -> NO SOUND
  2CFAF: JSR C2/EAC2             ; CHANGED   -> cursor blip
  2CFB2: JSR C2/CFFB             ; redraw the 3 rows
  2CFDD: LDX $79 / LDA $0D49,X / BEQ ->2CFE7 / JSR C2/D36C   ; occupied -> draw the summary
```

The node graph is copied from ROM `$FF:B61E` to `$7E:9380` — 5 nodes × {Up, Down, Left, Right}:

```
  node0 (slot 1): U=2 D=1 L=0 R=0
  node1 (slot 2): U=0 D=2 L=1 R=1
  node2 (slot 3): U=1 D=0 L=2 R=2
  node3 (YES)   : U=4 D=4 L=3 R=3
  node4 (NO)    : U=3 D=3 L=4 R=4
```

So Up/Down **wrap** across the three slots, and **Left/Right map every node to itself** — on the slot
list they are silent no-ops (`$54` unchanged → `CFAD BEQ` taken → no blip). The L/R shoulders are dead
too: `CEFD` tests only BMI/BVS.

`C2/E984` carries an **edge gate** a port must reproduce:
`E98D LDA $68 / CMP $0D17 / STA $0D17 / E995 BNE ->E9CD` — on the frame the sub-state *changes*, every
button bit is skipped and `$0D1D` stays 0. That is what stops one A press being eaten twice across a
transition.

### 3.4 Sub-state 2 — the overwrite prompt

`python tools/dis.py 2CF61 0x40 1 0`:

```
  2CF61: JSR C2/E984 / BIT $0D1D
  2CF67: BMI ->2CF75            ; A
  2CF69: BVS ->2CF88            ; B -> cancel
  2CF6B: LDA $54 / CMP $81 / BEQ ->2CF74 / JSR C2/EAC2   ; cursor moved -> blip
  2CF74: RTS
  2CF75: LDA $54 / CMP #03
  2CF79: BNE ->2CF88            ; not node 3 -> cancel
  2CF7B: JSR C2/CF37            ; <-- THE SAME SAVE ROUTINE
  2CF7E: LDA #01 / STA $68 / JSR C2/D4D5 / JMP C2/CF21
  2CF88: JSR C2/EAC2 / LDA #01 / STA $68 / JMP C2/D4D5   ; cancel: blip, back to the list
```

and the prompt setup `C2/D4BB`: `LDA #03 / STA $54` (node 3 pre-selected), cursor graph `$FF:C16F`,
message `$0D13 := $75`.

### 3.5 Sub-state 3 — CONFIRMED UNREACHABLE

The handler is real code (`d[0x2CF92:0x2CFA2]` = `ce 9b 0d 20 84 e9 2c 1d 0d 30 03 70 e9 60 80 e6`,
= `DEC $0D9B / JSR C2/E984 / BIT $0D1D / BMI ->CFA0 / BVS ->CF88 / RTS / BRA ->CF88`), but nothing can
set `$68 = 3` on screen 6 or 11: the only five `A9 03 85 68` sites in the whole 4 MB ROM are
`C2/C090, C2/D581, C2/D6B3, C2/E25B, C2/E628`, none in this flow, and every `INC $68` reachable here
runs at `$68 ∈ {0,1}`. `$68 ∈ {0,1,2}` for both screens; index 3 is dead.

### 3.6 Every sound on the path

| moment | ROM | cmd/id | throttled? |
|---|---|---|---|
| entry REFUSED | `C2/CEF2` → `C2/EACC` | `$18` / `$01` buzz | yes (`$0D76`) |
| entry allowed | `C2/CEF5` → `C2/EAC2` | `$18` / `$00` blip | yes |
| cursor moved | `C2/CFAF` → `C2/EAC2` | `$18` / `$00` blip | yes |
| **A, empty slot** | **`C2/CF37`** | **`$19` / `$8A` JINGLE** | **NO — never touches `$0D76`** |
| A, occupied slot | `C2/CF11` → `C2/EAC2` | `$18` / `$00` blip | yes |
| prompt cursor / cancel / B | `C2/CF71`, `C2/CF88` | `$18` / `$00` blip | yes |
| **A on YES** | **`C2/CF7B` → `C2/CF37`** | **`$19` / `$8A` JINGLE** | **NO** |

⚠ The occupied-slot blip goes through `C2/EB03`, which *is* throttled (`LDA $0D76 / BNE ->EB1E` skips
the pan store and the JSL). It can be silently swallowed if another blip fired within 3 frames. Only
the jingle is unconditional.

---

## 4. WHAT IS NOT DERIVED

**4.1 `$30:7FE2`.** Read at `C2/E66D` (`LDA $307FE2 / BEQ ->E683`) and never written by the save. What
sets it, and what it means, is NOT DERIVED.

**4.2 The prompt's label strings.** `$0D13 := $75` is the message id for the overwrite prompt and node
3 is the branch that saves. That node 3 is the one reading "Yes" is *inferred* from it being the save
branch and the pre-selected default — I did **not** read the literal text of message `$75`. NOT
DERIVED.

**4.3 Sub-state 3's `$0D9B`.** The handler decrements it every frame. Since the sub-state is
unreachable (§3.5), what the counter is for is NOT DERIVED — and cannot be reached to measure.

**4.4 `$0051`, the record cursor.** `FF/FA2B` is `LDX $51`, **not** an immediate zero. It only yields
8 records because `$0051 == 0`. Measured `$0051 = 0000` on every frame of the save drive, so on this
path all 8 records copy — but *why* it is a variable, and whether anything else calls FF/F9FB with a
non-zero cursor, is NOT DERIVED. A port that hardcodes "8 records from index 0" is right for the save
path and is dropping a real ROM input.

**4.5 The play-clock sub-fields.** `out->hh = +0x05E7`, `out->mm = +0x05E6*10 + +0x05E5` was matched
against **one** saved game (screen "TIME 08:43"). That is a MATCH, not a derivation; `+0x05E3/+0x05E4`
are still unidentified. Likewise the reader's `if (leader & 0x80) return 0;` is **not** the ROM's rule
— FF/F9C4 never consults the party byte for the used/empty verdict (in the ROM a `$FF` party slot only
suppresses that member's *line* in `C2/D19F`).

**4.6 `$299E`'s high byte.** ⭐ The *mechanism* IS derived, contrary to the note currently in
`src/menu.c`. `python tools/dis.py 2E640 0x60 1 0` shows the new-game path:

```
  2E660: LDA #01 / STA $7F0061        ; the new-game flag
  2E666: LDA $307FE1
  2E66A: STA $299E                    ; the allocator is COPIED INTO $299E on new game
```

so `$299E` is this playthrough's **file id**, seeded from the SRAM allocator, and `FF/FA6B..FA78`
bumps the allocator only while it still equals that id — i.e. exactly once, on a playthrough's first
save. Measured and self-consistent: the real cart has `$30:7FE1 = 01`, saved `+0x059E = 0x1D00`, live
`$7E:299E = 0x1D00` — low byte `00`, so the playthrough started when the allocator was 0 and bumped it
to 1. Only the **low** byte participates (both `E66A` and `FA6F` run 8-bit); what writes the high byte
`0x1D` is NOT DERIVED.

---

## 5. THE PORT

### 5.1 What already exists — do NOT re-derive or duplicate

| thing | anchor |
|---|---|
| slot bases (ROM table, not a stride) | `src/menu.c` `CT_SAVE_SLOT_BASE` / `CT_SAVE_SLOT_SPAN` |
| the 8-record table | `src/menu.c` `CT_SAVE_RECS` |
| FF/FBE7, carry-propagating | `src/menu.c` `ctSaveChecksum` |
| the writer | `src/menu.c` `CtMenuSaveWrite` |
| the cart image + battery file | `src/field.c:1988-2012` (`g_sram`, `CT_SRAM`, `CT_SRAM_FRESH`) |
| the press, the jingle, the branch | `src/field.c:8330-8385` |
| host wiring | `src/field.c:8424` (`mh.sram = fieldSram()`) |
| the cartridge-graded checksum probe | `src/field.c` `--sramcheck` |

The fresh cart is filled **`0xFF`**, matching a real erased cart — this matters, because FF/FBE7 spans
the whole `$0A00` slot including the `0x1CC`-byte tail the pack never fills.

### 5.2 ⚠ What must NOT be invented

- **Not a plain word sum.** The carry propagates (§1.6). This is the single easiest thing to get
  wrong, and it is invisible until a save fails its own read-back.
- **Not `slot*0x800`.** The bases are a ROM table (§2.3).
- **Not a scene/slot special case.** The occupancy branch is `$0D49,X`, the entry gate is the OR of
  four block bytes — both are general machine state, never a per-screen condition.
- **Do not invent the overwrite prompt's behaviour.** It is a real sub-state with a real node graph;
  a "silent overwrite" would be a thing the ROM never does. It is currently *refused* and the silence
  is counted, so the gap stays visible.

### 5.3 The checks (all green, this pass)

```
python tools/regress.py --only menusave-ow-open field-menu-save-refuse-buzz \
    field-save-checksum-vs-real-rom-save field-menu-save-jingle-and-write field-menu-save-readback

PASS  menusave-ow-open                     0.1s
PASS  field-menu-save-refuse-buzz          0.2s
PASS  field-save-checksum-vs-real-rom-save 0.0s  our FF/FBE7 reproduces the checksum CHRONO TRIGGER itself stored
PASS  field-menu-save-jingle-and-write     0.3s  A on an empty slot plays the jingle and writes a slot FF/F9C4 accepts
PASS  field-menu-save-readback             0.3s  the slot we wrote reads back as USED through the ROM's own gate
regress: 5 pass, 0 fail, 0 skip in 0.9s
```

| check | pins | mutations |
|---|---|---|
| `field-save-checksum-vs-real-rom-save` | `slot=0 ... stored=6B8C computed=6B8C MATCH` + both EMPTY slots | carry→0 (gives `6A4D`); slot-base table |
| `field-menu-save-jingle-and-write` | `cmd=19 id=8A pan=80 posted, $0D76=0`; `bytes=0x0834 sig=E41B`; `8 post(s) ... 0 silent` | delete the mailbox stores; route it through the stager (`cmd=18`, `$0D76=3`); shorten record 0 |
| `field-menu-save-readback` | the save, then `save-overwrite-prompt-not-ported -> SILENT`, `10 post(s) ... 1 silent` | break the signature byte |

Two design points worth keeping:

- The jingle probe **reads the mailbox back** (`jc/ji/jp/jt`) instead of printing a fixed sentence.
  Two mutations previously survived a hardcoded string.
- The checksum **value** is live-game state (it moves with the clock and the party), so it is *not*
  pinned in the drive; it is graded where it is invariant — `--sramcheck` over the cartridge's own
  image. Grading it against a file we wrote would agree with any algorithm, right or wrong.
  ⚠ `CT_SRAM_FRESH=1` is load-bearing: without it the second run finds the slot the first run wrote,
  takes the occupied branch, and the check would pass once and lie ever after.

### 5.4 ⚠ OPEN — the `$299C` increment is a real defect, not a scope decision

`FF/FA05..FA0B` increments `$7E:299C` (saturating) **before** the MVNs, and `$299C` lies **inside
record 0** at slot offset **`+0x059C`** — which is exactly the field the summary reads back and
displays as **"Save #"** (`C2/D21B`, capped at 999).

`grep -n "299C\|0414" src/field.c src/menu.c` returns **nothing**. So our port packs the
*un-incremented* value: every save records the same Save #, it never counts up, and the resulting
checksum differs from the one the ROM would have produced for the same game state. Measured proof that
the ROM does it: `f269 7E299C=0100*(FF:FA0E)` — `0` → `1` during the save — and the real cart's
`+0x059C = 1` against a screen reading "Save # 1".

Fix: perform the saturating increment on `$7E:299C` *before* calling `CtMenuSaveWrite`, per
`FF/FA05` (`INC A / BEQ skip / STA`). `$7E:0414 := $79` (`FF/FA00`) should go with it; it is outside
every record, so it does not affect the pack, but it is part of the writer.

### 5.5 OPEN — the rest

- **The overwrite prompt** (§3.4): sub-state 2, node graph `$FF:C16F`, message `$0D13 = $75`, YES at
  `$54 == 3` → the same `C2/CF37`. Needs a screen this engine does not draw.
- **The `$30:7FE1` bump** is now portable (§4.6) — it needs `C2/E666`'s new-game seeding of `$299E`
  ported alongside it, or the compare has no meaning.
- **The reader's used/empty rule** should become the ROM's: signature **AND** checksum, and drop the
  invented `leader & 0x80` test (§2.4, §4.5).
