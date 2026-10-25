# AUDIO front 1 — the sound-driver BOOT (bank $C7 → SPC700 ARAM)

Supersedes / corrects `disasm/notes/apu_upload.md` (that note was right in outline, wrong or
silent on four load-bearing points — see §0). Everything below is either **disassembled**
(`python tools/dis.py <fileoffset> <len> [m] [x]`, HiROM flat offsets: bank $C7 = file
`0x70000 + addr`) or **observed in the oracle** (`build/ct.exe`, savestate payloads).

ROM: `Chrono_Trigger_USA_SNES.sfc`, 0x400000 bytes, headerless (verified: `len%1024 == 0`,
exactly 4 MB).

---

## 0. Corrections to `apu_upload.md`

| old claim | verdict |
|---|---|
| "`$C7:001C` = `$0200` — *initial* ARAM target address" | **WRONG/incomplete.** `$001C` is a **6-entry destination table** parallel to the 6-entry source table at `$0010`. Every block writes its own destination to `$2142/$2143` (`$C7:00C6`/`$C7:00CD`), confirmed live: oracle `apu->inPorts[2..3]` = `00 02`→`00 2F`→`00 47` at frames 4/16/22. |
| "Block 0 … the main sound driver/data" (only block described) | **incomplete.** There are exactly **6 blocks / 13884 bytes**; loop terminator `CPX #$000C` at `$C7:00C0`. |
| "wait for … `$BBAA` … per byte … increment until the block size" | correct, but says **nothing** about the multi-block continuation, which is the part a port gets wrong. See §4. |
| "reg-init (`$FD:C000`) → WRAM-init (`$C0:000E`) → sound-driver upload (`$C7`)" | **correct**, and now cited instruction-by-instruction (§1) + observed in a live trace. |
| implied the upload is instantaneous | **WRONG in practice.** The upload takes **~23 emulated frames (~380 ms)** of wall time; the game runs 22 frames of boot with no sound driver present (§6). |

---

## 1. The entry chain (who calls `$C7:0000`, and when)

Disassembled, and then **observed** with `build/ct.exe --trace 40000` (first 40000 executed
65816 instructions from reset; `KK PPPP DP M X E` per line).

```
$00:FFFC (file 0xFFFC)  reset vector = $FF00          [raw bytes, HiROM bank $00 = $C0]
  FF00: SEI / CLC / XCE / JML $FD:C000                (tools/dis.py FF00 0x20)
$FD:C000 (file 0x3DC000)  hardware init               (tools/dis.py 3DC000 0xE0)
  C000: REP #$10 / SEP #$20 / LDX #$06FF / TXS        stack = $06FF
  C00E: LDA #$4200 / TCD                              D = $4200
  C014: LDA #$01 / STA $0D                            -> $420D = 1  **FastROM ON**
  C01A: STA $00 ($4200=0), $420B/$420C = 0            NMI/IRQ/HDMA off
  C038: LDA #$2100 / TCD  ... full PPU register init ...
  C0D3: JML $C0:000E
$C0:000E (file 0x000E)  WRAM/system init              (tools/dis.py 0 0x80)
  0012: JSR $C0:0B64      install JML stub at $00:0500 -> $C0:EA63   (NMI trampoline)
  0015: JSR $C0:0B75      install JML stub at $00:0504 -> $C0:ECCC   (IRQ trampoline)
  001A: LDA #$0100 / TCD  D = $0100
  0020: JSR $C0:0B4E      SEI, DBR=0, $2100=$80 (forced blank), $4200/$420B/$420C = 0
  0023..0031: JSR $C0:2DF1  x1   clear $7E:0000+0x0500
  0034..0042: JSR $C0:2DF1  x2   clear $7E:0700+0xE900
  0045..0050: JSR $C0:2DF1  x3   clear $7F:5080+0x5080
  0053: JSL $C7:0000       ***** THE SOUND-DRIVER UPLOAD *****
  0057: LDA #$09 / JSL $C2:8004   (party/init, unrelated to audio)
  005D: (falls into the soft-restart entry $C0:0000 -> BRA $005D)
```

**`JSL $C7:0000` occurs exactly ONCE in the entire 4 MB ROM**, at `$C0:0054` (operand bytes
`22 00 00 C7` at file 0x000053; exhaustive byte scan of the ROM for `22 00 00 C7`). Note
`$C0:0000 = BRA $005D` — the **soft-restart entry deliberately skips the APU upload**, so the
driver is uploaded once per power-on and never again.

Live trace confirmation (`--trace 40000`, scratch `trace40k.txt`):
```
00 0000 / 00 FF00 FF01 FF02 FF03      (reset stub)
FD C000 ... FD C0D3                   (109 instructions)
C0 000E .. 0B64 .. 0B75 .. 0B4E .. 2DF1(x3) .. C0 0053   (109 instructions)
C7 0000  (trace line 227)  ->  C7 0038 ...
```
Bank histogram over those 40000 instructions: **C7 = 39777, FD = 109, C0 = 109, 00 = 5.**
Only ~223 CPU instructions execute between reset and the APU upload; the upload then dominates
the entire boot.

---

## 2. The block table at `$C7:0010` — SIX blocks, each with its OWN destination

Raw bytes `$C7:0000-$003F` (file 0x70000):
```
4C 38 00 EA  4C 40 01 EA  4C 26 03 EA  4C 7B 03 EA   ; 4 entry JMPs, EA padding
C3 24 09 43  B4 40 B7 42  DD 42 F3 42                 ; $0010 SOURCE table  (6 words, bank $C7)
00 02 00 2F  00 47 00 1E  80 1F 00 1F                 ; $001C DEST table    (6 words, ARAM)
18 0D EA 0A  A4 0B 9C 0C  20 0C 11 0E  E9 0A          ; $0028 (NOT part of the upload — see below)
0B 5B                                                  ; $0036 (PHD/TCD, unreached padding)
8B 0B 08 C2 20 C2 10 48 ...                            ; $0038 = the upload routine
```

The loop walks **X = 0,2,4,6,8,$0A** and terminates on `CPX #$000C` (`$C7:00C0`), so the tables
are exactly 6 entries. Source pointer is read long from `$C7:0010,X` / `$C7:0011,X`
(`$C7:0080`/`$C7:0086`); destination long from `$C7:001C,X` / `$C7:001D,X` (`$C7:00C6`/`$C7:00CD`).
The first block's destination is written *before* the loop from `$C7:001C`/`$C7:001D`
(`$C7:0062`/`$C7:0069`) — same table, index 0.

Each block in ROM is `[size:word][payload]`:

| # | src (bank $C7) | file offset | size word | payload bytes | ARAM dest | ARAM range |
|---|---|---|---|---|---|---|
| 0 | `$24C3` | 0x0724C3 | `$1BEF` | 7151 | `$0200` | `$0200-$1DEE` |
| 1 | `$4309` | 0x074309 | `$1800` | 6144 | `$2F00` | `$2F00-$46FF` |
| 2 | `$40B4` | 0x0740B4 | `$0201` | 513  | `$4700` | `$4700-$4900` |
| 3 | `$42B7` | 0x0742B7 | `$0024` | 36   | `$1E00` | `$1E00-$1E23` |
| 4 | `$42DD` | 0x0742DD | `$0014` | 20   | `$1F80` | `$1F80-$1F93` |
| 5 | `$42F3` | 0x0742F3 | `$0014` | 20   | `$1F00` | `$1F00-$1F13` |

**Total uploaded: 13884 bytes.** No block overlaps another. In ROM the blocks are stored
contiguously but in a different order than the upload order: 0(24C3) → 2(40B4) → 3(42B7) →
4(42DD) → 5(42F3) → 1(4309), spanning file 0x0724C3-0x075B0A.

**Observational proof that each block gets its own destination** — `apu->inPorts[2..3]`
(= the last values the CPU wrote to `$2142/$2143`) pulled out of `--savestate` payloads:

| frames | inPorts[2..3] | = ARAM dest | which block was streaming |
|---|---|---|---|
| 4, 8   | `00 02` | `$0200` | block 0 |
| 16,18,20,21 | `00 2F` | `$2F00` | block 1 |
| 22     | `00 47` | `$4700` | block 2 |

### `$C7:0028-$0037` is NOT part of the upload
Seven words follow the dest table: `$0D18, $0AEA, $0BA4, $0C9C, $0C20, $0E11, $0AE9`. These are
**bank-$C7 ROM table addresses**, not ARAM/upload data — three of them are dereferenced directly
by the runtime dispatcher (`CMP $C7:0AE9` at `$C7:01A5`; `LDA $C7:0E11,X` at `$C7:01EA`;
`ADC $C7:0BA4,X` at `$C7:03AC`). Exhaustive scan: **no instruction anywhere in the ROM long-reads
`$C7:0028..$0037`** (all `xx 00 C7` byte hits land in data/graphics banks $C5-$FA). **NOT
DERIVED: what, if anything, consumes this list.** It does not affect the upload.

---

## 3. The four entry points at `$C7:0000`

```
$C7:0000  JMP $0038   upload  (1 JSL site: $C0:0053)
$C7:0004  JMP $0140   runtime sound-command service  (59 JSL sites)
$C7:0008  JMP $0326   voice/slot table compaction
$C7:000C  JMP $037B   parameter-block push to ARAM $1E80
```

- **`$C7:0004` (59 JSL sites)** — `22 04 00 C7` occurs 59 times: `$C0:1B73…1CF5` (16),
  `$C0:3C26…3CB4` (6), `$C1:1B50/1B62/40DD/61B5/61DF/620F`, `$C2:0462/23F9/24F0/2501/264E/2F49/
  6BE9/80B2/80C5/80CE/80E1/80EA/C033/CBF3/CC09/CF46/E6A1/EB1A`, `$C3:09D1/0BE9/1647`,
  `$CD:03AB/0D70/0D81/3B7D/3E54/3E70`, `$FF:FC82/FC97/FF24/FF39`. **This is the whole sound API
  of the game — front 2's entry point.**
  Structure (`tools/dis.py 70140 0x60`): DBR=0, `D=$1E00`, then
  ```
  $0157: LDA $05 / BPL $0192        ; $00:1E05 bit7 = "driver present" latch (set ONLY by the upload)
  $015B: LDA $00 / BEQ $0192        ; $00:1E00 = pending command byte, 0 = nothing to do
  $015F: BMI $061C                  ; cmd >= $80  -> $C7:061C
  $0161: CMP #$10 / BCC $0192       ; cmd < $10   -> ignore
  $0165: CMP #$18 / BCS $0176
  $0169: AND #$0F / ASL / -> X ; JMP ($C7:0AD9,X)   ; cmd $10-$17: 8-entry jump table @ $C7:0AD9
  $0176: CMP #$30 / BCC $061C       ; cmd $18-$2F -> $C7:061C
  $017D: CMP #$40 / BCC $071D       ; cmd $30-$3F -> $C7:071D
  $0184: CMP #$70 / BEQ $08E3
  $018B: CMP #$71 / BEQ $0755
  $0192: SEP #$20 / STZ $00 ; restore ; RTL          ; clears the command byte on the way out
  ```
  `$C7:061C` is the raw mailbox flush: `$1E03→$2143, $1E02→$2142, $1E01→$2141, $1E00→$2140`
  (`tools/dis.py 7061C 0x18`). Confirmed live at frames 23/24: WRAM `$1E00..$1E03 = 10 00 FF 05`
  and `apu->inPorts[0..3] = 10 00 FF 05` — byte-identical mirror.
- **`$C7:0008` / `$C7:000C` are never JSL'd** (`22 08 00 C7` / `22 0C 00 C7`: **zero** occurrences
  in 4 MB). Their bodies are reached only by internal jumps: `JMP $C7:0326` at `$C7:0257`, and
  `JMP $C7:037B` at `$C7:0214`. The 3-byte stubs are vestigial.
  - `$C7:0326` (`tools/dis.py 70326 0x58 1 0`): compacts the `$1E20/$1F20/$1E60/$1E40` 16-word
    slot arrays and calls `$C7:0655`; ends `LDA #$E0 / STA $84`.
  - `$C7:037B` (`tools/dis.py 7037B 0x70 1 0`): builds a word list at `$00:1F80` from `$1F00`,
    `$1E20`, `$1E60` and the ROM table `$C7:0BA4,X`; then
    `$2143=$1E, $2142=$80, $2141=$02` (i.e. ARAM parameter address `$1E80`, driver command `$02`),
    then the rotating-token handshake `$C7:09DA`.
  - `$C7:09DA` is the **runtime token handshake** (distinct from the IPL one):
    `STA $2140 / CMP $2140 / BNE -3 / INC A / AND #$7F / RTS` — write token, spin until the SPC
    echoes it, then advance the token modulo $80. `$C7:09EA` re-sends `$84` and resets it to `$E0`.
  **These runtime protocols are FRONT 2 scope; they are documented here only to fence the boot.**

---

## 4. The IPL handshake, byte-exact (including the multi-block continuation)

### 4a. The SPC700 side — the IPL boot ROM the port must drive
`snes/apu.c:14-19` embeds the 64-byte boot ROM at `$FFC0` (`apu->romReadable` gates it; the
reset vector at `$FFFE/$FFFF` = `C0 FF` → `$FFC0`). Decoded:
```
FFC0 CD EF     MOV  X,#$EF          FFDE E4 F5     MOV  A,$F5     ; data byte
FFC2 BD        MOV  SP,X            FFE0 CB F4     MOV  $F4,Y     ; ECHO the index
FFC3 E8 00     MOV  A,#$00          FFE2 D7 00     MOV  [$00]+Y,A ; store to ARAM
FFC5 C6        MOV  (X),A           FFE4 FC        INC  Y
FFC6 1D        DEC  X               FFE5 D0 F3     BNE  $FFDA
FFC7 D0 FC     BNE  $FFC5           ; clears $01-$EF ($00 untouched)
FFC9 8F AA F4  MOV  $F4,#$AA        FFE7 AB 01     INC  $01       ; dest += 0x100
FFCC 8F BB F5  MOV  $F5,#$BB        FFE9 10 EF     BPL  $FFDA
FFCF 78 CC F4  CMP  $F4,#$CC        FFEB 7E F4     CMP  Y,$F4
FFD2 D0 FB     BNE  $FFCF           FFED 10 EB     BPL  $FFDA
FFD4 2F 19     BRA  $FFEF           FFEF BA F6     MOVW YA,$F6    ; new target from ports 2/3
FFD6 EB F4     MOV  Y,$F4           FFF1 DA 00     MOVW $00,YA    ; dest pointer := it
FFD8 D0 FC     BNE  $FFD6           ; wait for port0 == 0
FFDA 7E F4     CMP  Y,$F4           FFF3 BA F4     MOVW YA,$F4    ; A=port0, Y=port1
FFDC D0 0B     BNE  $FFE9           FFF5 C4 F4     MOV  $F4,A     ; ECHO port0
                                    FFF7 DD        MOV  A,Y
                                    FFF8 5D        MOV  X,A       ; X = port1
                                    FFF9 D0 DB     BNE  $FFD6     ; port1 != 0 -> transfer
                                    FFFB 1F 00 00  JMP  [$0000+X] ; port1 == 0 -> EXECUTE (X=0)
```
Three rules fall out, and the CT uploader obeys all three:
1. **port1 (`$2141`) != 0 ⇒ another transfer; port1 == 0 ⇒ jump to the address in ports 2/3.**
2. **A new transfer's first data byte must carry index 0** (`$FFD6: MOV Y,$F4 / BNE $FFD6`).
3. **A block boundary is signalled by bumping the index so that `port0 - Y` lands in 1..127**
   (`$FFDA/$FFE9/$FFEB`); the CPU may not send index 0 as the boundary token, because rule 2
   would then consume it.

### 4b. The 65816 side — `$C7:0038` transcribed (`tools/dis.py 70038 0x108`)
```
0038  PHB/PHD/PHP, REP #$20, REP #$10, PHA/PHX/PHY
0042  SEP #$20 ; LDA #$00 / PHA / PLB           DBR = $00
0048  LDX #$1E00 / PHX / PLD                    D  = $1E00
004D  LDX #$BBAA
0050  LDA #$F0 / STA $2141                      \  IPL-ready poll
      LDA #$FF / STA $2140                       |  (X is 16-bit, so CPX reads $2140|$2141<<8)
      CPX $2140 / BNE $0050                     /
005F  LDX #$0000                                table index
0062  LDA $C7:001C -> $2142 ; LDA $C7:001D -> $2143     dest[0] = $0200
0070  LDA #$CC / STA $2141 / STA $2140          kick (port1=$CC != 0, port0=$CC)
0078  CMP $2140 / BNE $0078                     wait for the $CC echo
--- per-block head ($007D) -------------------------------------------------
007D  LDA #$00 / XBA                            ***** running index := 0 (EVERY block) *****
0080  $12/$13/$14 := $C7:src[X]                 24-bit source pointer
0090  LDY #$0000
0093  LDA [$12],Y / CLC / ADC #$02 -> $0C       $0C/$0D = size + 2  (16-bit, with carry)
009A  INY / LDA [$12],Y / ADC #$00 -> $0D
00A1  INY                                       Y = 2 (skip the size word)
--- per-byte loop ($00A2) --------------------------------------------------
00A2  LDA [$12],Y   ; data
00A4  STA $2141     ; port1 = data      (written BEFORE the index -> no race)
00A7  XBA           ; A.low = index
00A8  STA $2140     ; port0 = index
00AB  CMP $2140 / BNE $00AB             ; spin on the SPC's echo
00B0  INC A ; XBA                       ; index++ (8-bit, wraps at 256 exactly like the IPL's Y)
00B2  INY / CPY $0C / BNE $00A2
--- block boundary ($00B7) -------------------------------------------------
00B7  XBA ; INC A ; INC A ; INC A       ; index += 3
00BB  BNE $00BE / INC A                 ; ...+1 more if it wrapped to 0 (rule 3 above)
00BE  INX / INX / CPX #$000C / BEQ $00E2
00C5  XBA
00C6  LDA $C7:001C,X -> $2142 ; LDA $C7:001D,X -> $2143   ***** dest[i] per block *****
00D4  XBA / STA $2141 / STA $2140       ; port1 = port0 = index+3 (non-zero => "another transfer")
00DB  CMP $2140 / BNE $00DB             ; wait for the echo
00E0  BRA $007D                         ; NOTE: NO re-wait for $BBAA, NO re-send of $CC
--- all six blocks done ($00E2) --------------------------------------------
00E2  XBA
00E3  LDA #$02 / STA $2143              ; exec addr high
00E8  LDA #$00 / STA $2142              ; exec addr low  -> $0200
00ED  LDA #$00 / STA $2141              ; port1 = 0  => EXECUTE
00F2  XBA / STA $2140                   ; port0 = index+3
00F6  CMP $2140 / BNE $00F6             ; wait for the echo (IPL's last act before JMP [$0000])
00FB  XBA / STA $2140                   ; port0 = 0   (post-handshake with the now-running driver)
```

**Answer to "do later blocks redo the `$BBAA` wait?" — NO.** The `$BBAA` poll lives only at
`$0050-$005D`, before `LDX #$0000`; the `$CC` kick only at `$0070-$0078`. The boundary protocol
for blocks 1..5 is *exactly*: `dest → $2142/$2143`, then `(index+3, +4 if that wrapped to 0)` →
`$2141` then `$2140`, spin on the `$2140` echo, and **reset the running index to 0** at `$007D`.

**Determinism note (important for the port):** every byte is a two-way rendezvous — the CPU
blocks on the echo, and the SPC blocks on the index — and port1 (data) is always written before
port0 (index), while the IPL latches the data (`MOV A,$F5`) *before* echoing (`MOV $F4,Y`).
Therefore the resulting ARAM image is **independent of the CPU/APU speed ratio**; only the
elapsed time changes. A port may advance the APU on any schedule as long as it advances it
*at all* inside each spin.

### 4c. The tail: what the upload leaves in WRAM (`$C7:00FF-$013D`)
```
00FF  LDX #$0100 / STA $1DFF,X / DEX / BNE      zero $00:1E00-$1EFF (A = 0 here)
0108  REP #$20 / LDA $C7:40B4 / CLC / ADC #$4700
        -> STA $F8 ($1EF8), STA $60 ($1E60), STA $F6 ($1EF6)
        = dest[2] + size(block 2) = $4700 + $0201 = $4901   (first free ARAM after block 2)
0118  LDX #$0800 / DEX / BNE                     ***** 2048-iteration DELAY (driver boot) *****
011E  SEP #$20 ; $1EFA = $00 ; $1EFB = $C7
0128  LDA #$05 / STA $F0                         $1EF0 = 5
      ASL/ASL/ASL (=40) / EOR #$FF / INC A (=-40) / CLC / ADC #$F1  -> $C9
        -> STA $F2                               $1EF2 = $C9
0137  STZ $F1                                    $1EF1 = $00
0139  LDA #$E0 / STA $05                         $1E05 = $E0  <<< "driver present" latch
013D  JMP $C7:0192                               STZ $1E00 ; PLY/PLX/PLA/PLP/PLD/PLB ; RTL
```
**Verified against the oracle** (`ct.exe --read wram:1E00+16,wram:1E60+4,wram:1EF0+16 --from
b24.state --settle 0`):
```
wram[001E00+16] = 10 00 FF 05 00 E0 00 00 ...      $1E05 = E0  ✔
wram[001E60+4]  = 01 49 00 00                      $1E60 = $4901 ✔
wram[001EF0+16] = 05 00 C9 00 00 00 01 49 01 49 00 C7 00 00 00 00
                  $1EF0=05 ✔  $1EF1=00 ✔  $1EF2=C9 ✔  $1EF6=$4901 ✔  $1EF8=$4901 ✔  $1EFA/B=$C700 ✔
```
(`$1E00..$1E03 = 10 00 FF 05` at frame 24 is already a *runtime* sound command sitting in the
mailbox, not upload output.)

The 2048-iteration delay is the **only** synchronization between "IPL jumped to `$0200`" and
"the CPU starts issuing commands" — there is no readiness poll. With `$420D=1` (FastROM, set at
`$FD:C016`), bank $C7 runs at 6 master cycles/CPU cycle, and `DEX`+`BNE(taken)` = 5 CPU cycles,
so 2048 × 5 × 6 = 61440 master cycles ≈ 61440 × `apuCyclesPerMaster` (= (32040·32)/(1364·262·60)
= 0.0478162, `snes/snes.c:18`) ≈ **2938 APU cycles ≈ 2.9 ms**. The port must reproduce an equivalent APU
advance before its first `$2140-$2143` command write.

---

## 5. Reading the oracle's ARAM

**There is NO `ct.exe` mode that dumps ARAM.** `grep -n "apu\|aram\|spc\|dsp" src/*.c src/*.h`
returns zero APU references in the whole harness; `docs/DEBUG_SURFACE.md` lists 84 modes, none
of them APU-related.

**Two ways to get it today:**

1. **(used here, no code change) The LakeSnes savestate already contains ARAM.**
   `apu_handleState` (`snes/apu.c:56-71`) serializes `apu->ram[0x10000]`. For the current
   `snes_handleState` field order, in a `--savestate` file the ARAM sits at
   **payload offset `0x30810`** (payload = file minus the 16-byte `CTLS` header). Structural
   check: `0x30810 = 69 (Snes header) + 0x20000 (WRAM) + 67531 (cpu+dma+ppu incl. VRAM/CGRAM/OAM)`;
   and the 31 bytes immediately before it are `romReadable, dspAdr, inPorts[0..5], outPorts[0..3],
   cycles(u32), 3×{enabled, cycles, divider, target, counter}`. **This offset is fragile** — any
   `handleState` edit moves it. A robust locator (what I used) is to search the payload for the
   first 64 bytes of block 0 (`ROM[0x724C5:+64]`) and subtract `0x200`.

2. **(recommended) One-line harness addition.** In `runSaveState` (`src/main.c:2794`), after the
   frame loop and before `sh_init`:
   ```c
   { FILE* af = fopen("build/aram.bin", "wb"); if (af) { fwrite(s->apu->ram, 1, 0x10000, af); fclose(af); } }
   ```
   `s` is the `Snes*`; `snes->apu->ram` is public (`snes/apu.h:27`). A cleaner form is a
   `--aram <out>` mode in the `harness_cli.c` table (`src/harness_cli.c:75-105`) that boots
   `--frames N` and writes the 64 KB.

### The ARAM fingerprint (THE pin for the port)

Measured with `build/ct.exe --savestate <out> --frames N` for N = 1,2,4,8,16,18,20,21,22,23,24,26,30,40:

| frames | bytes of the 13884 still WRONG (per block 0..5) |
|---|---|
| 1, 2 | 13884 — `[6772, 5546, 435, 34, 20, 8]` (block 0 not yet started) |
| 4  | 11520 — `[5477, …]` |
| 8  | 8886 — `[2838, …]` |
| 16 | 4058 — `[0, 3561, 435, 34, 20, 8]`  (block 0 DONE) |
| 20 | 1460 — `[0, 963, 435, 34, 20, 8]` |
| 22 | 226 — `[0, 0, 164, 34, 20, 8]` |
| **23** | **0 — ALL SIX BLOCKS BYTE-IDENTICAL TO THE ROM** |
| 24, 25, 26 | 0 (still clean) |
| 30 | 14 (block 0 only — the driver's own variables) |
| 40 | 22 (block 0 only) |

**Canonical fingerprint — what the port must reproduce:**
```
uploaded image = concat(payload[block0..block5]) in UPLOAD (table) order
  length  = 13884 bytes
  sha256  = b56fa7e7e960d3e97392b7170f84cf0f72452a6aa094632c7db8229d7bbf54df
  crc32   = 021CEA18
placed at ARAM $0200 / $2F00 / $4700 / $1E00 / $1F80 / $1F00 respectively.
ARAM[$0200..$021F] = 20 C0 CD FF BD E8 00 5D AF C8 F0 D0 FB 8B 80 1A
                     BB 8D 2C 3F A3 07 8D 3C 3F A3 07 CD 40 D5 6E F1
  (identical to ROM $C7:24C5; SPC700: CLRP / DI / MOV X,#$FF / MOV SP,X / MOV A,#$00 /
   MOV X,A / MOV (X)+,A / CMP X,#$F0 / BNE -5   = clear ARAM $00-$EF, then driver init.
   NOT DERIVED beyond that byte: no SPC700 disassembler exists in tools/.)
```
Whole-64 KB hashes are **not** a stable pin (the driver mutates page 0 / stack / `$F1xx` every
frame): `--frames 23` → sha256 `88383cf2…`, `--frames 24` → `5fed11c7…`, `--frames 26` →
`fc57ba54…`. Pin the 13884-byte concat, not the 64 KB image.

At frames 23-26, the **only** non-zero ARAM outside the six blocks is driver scratch:
`$0080-$0081, $008E, $00BB-$00BC, $00D7, $00F1-$00F2, $00F4-$00F5, $00FA-$00FB` (page 0 / SPC
hardware-register mirrors), `$01F7-$01FF` (stack), `$F172, $F174-$F175`. Everything else is 0 —
consistent with `apu_reset` (`snes/apu.c:41` `memset(apu->ram, 0, …)`) plus the IPL's `$01-$EF`
clear.

### The upload's cost in real time (a real constraint, not a footnote)
`apu->cycles` from the same savestates: 65940 (f4) → 373521 (f22) → 390610 (f23); Δ = **17089 APU
cycles/frame** ≈ 32040×32/60 = 17088 ✔. Upload rate ≈ **650 bytes/frame ≈ 26.3 APU cycles/byte**
(the IPL inner loop `CMP Y,$F4 / BNE / MOV A,$F5 / MOV $F4,Y / MOV [$00]+Y,A / INC Y / BNE`).
13884 bytes ⇒ **≈ 21.4 frames of pure transfer, complete during frame 23 (~380 ms)**.
`apu->romReadable` flips 1 → 0 between f22 and f23 — that is the driver at `$0200` writing `$F1`
with bit 7 clear, i.e. **the observable "driver is running" event**.

---

## 6. The vendored APU core the port must drive (`snes/apu.{h,c}`, `spc.*`, `dsp.*`)

### (a) Instantiate
```c
Apu* apu_init(Snes* snes);      // snes/apu.h:36 ; apu.c:23 — allocates, spc_init + dsp_init
```
`apu_init` stores the `Snes*` and nothing more, **but `dsp.c:586` dereferences
`dsp->apu->snes->palTiming`** inside `dsp_getSamples`. So the port must pass a real, live struct
with a valid `palTiming` (false = NTSC ⇒ 534 samples/frame). It needs no other `Snes` field and
calls **no** `snes_*` function.

### (b) Reset so the IPL runs
```c
apu_reset(apu);                 // snes/apu.c:37
```
does `spc_reset(spc, true)` (`spc.c:44` → zeroes regs, sets `resetWanted`), `dsp_reset`,
`memset(ram, 0, 0x10000)`, `romReadable = true`, `cycles = 0`, clears `inPorts`/`outPorts`/timers.
On the next `spc_runOpcode` the `resetWanted` path (`spc.c:74-85`) fetches the vector from
`$FFFE/$FFFF`, which — because `romReadable` is true — reads `bootRom[0x3E..0x3F] = C0 FF`
⇒ **PC = `$FFC0`, the IPL.** No extra work needed.

### (c) Advance it in lockstep with the CPU-side busy-waits
```c
int  apu_runCycles(Apu* apu, int wantedCycles);  // apu.c:73 — runs whole opcodes until >= wanted
void apu_write(Apu*, uint16_t adr, uint8_t val); // NOT the port path
uint8_t apu_read (Apu*, uint16_t adr);           // NOT the port path
```
The **CPU-side port path is direct field access**, exactly as `snes.c` does it
(`snes_readBBus`/`snes_writeBBus`, `snes.c:259-260, 276-277`):
```c
// CPU writes $2140+k :               catchup(); apu->inPorts[k]  = val;
// CPU reads  $2140+k :               catchup(); return apu->outPorts[k];
```
`snes_catchupApu` (`snes.c:231-235`) is the model: accumulate
`apuCyclesPerMaster = (32040*32)/(1364*262*60) = 0.0478162` (`snes.c:18`) per master cycle and
call `apu_runCycles` with the integer part. **A field port has no master clock**, so the
mandate-safe equivalent is: run the APU by a fixed quantum inside every spin, and by
`32040*32/60 = 17088` cycles per rendered frame overall (which reproduces the observed
17089 cycles/frame). Because of the rendezvous property (§4b), the ARAM outcome is identical
either way; only the number of host frames the upload takes changes.

### (d) Pull samples
```c
void dsp_getSamples(Dsp* dsp, int16_t* sampleData, int samplesPerFrame);   // dsp.h / dsp.c:584
// wrapper: snes_setSamples(snes, buf, n) -> dsp_getSamples(snes->apu->dsp, buf, n)  (snes_other.c:151)
```
`dsp_cycle` runs every 32 APU cycles (`apu.c:84-87`) ⇒ 32040 Hz ⇒ 534 samples/frame NTSC, held in
`dsp->sampleBuffer[0x400*2]`; `dsp_getSamples` resamples 534 → `samplesPerFrame`. The vendored
SDL driver (`_vendor_LakeSnes/main.c:115-130, 143, 321-326`) is the reference:
```c
audioFrequency = 48000;  want.format = AUDIO_S16;  want.channels = 2;  want.samples = 2048;
audioBuffer = malloc(audioFrequency / 50 * 4);          // *2 stereo, *2 sizeof(int16)
wantedSamples = audioFrequency / 60;                    // 800 for NTSC
// once per emulated frame:
snes_setSamples(snes, audioBuffer, wantedSamples);
if (SDL_GetQueuedAudioSize(dev) <= wantedSamples*4*6) SDL_QueueAudio(dev, audioBuffer, wantedSamples*4);
```
`Belthasar` already links SDL2 (`CMakeLists.txt`), but currently only `SDL_INIT_VIDEO`-class use —
`SDL_INIT_AUDIO` must be added to its `SDL_Init`.

### (e) What must change in `CMakeLists.txt`, and the coupling cost
`Belthasar` today: `src/field.c scene.c lz.c assets.c ct_vm.c collision.c player.c world.c
battle.c menu.c oam.c` + `target_include_directories(Belthasar PRIVATE src ${SDL2_DIR}/include)`.
**No `snes/*.c` at all** — confirmed by CMakeLists; `${SNES_SRC}` is only in the `ct` target.

Minimal addition — **4 files, not the whole core**:
```cmake
add_executable(Belthasar src/field.c ... src/oam.c
               snes/apu.c snes/spc.c snes/dsp.c snes/statehandler.c)
target_include_directories(Belthasar PRIVATE src snes ${SDL2_DIR}/include)
```
Dependency audit (grep of `#include` + cross-module symbol use):
- `apu.c` → `spc_*`, `dsp_*`, `sh_*` only.
- `spc.c` → `sh_*` + the three function-pointer callbacks. Nothing else.
- `dsp.c` → `sh_*`, plus **data-only** access to `apu->ram[]` (BRR/echo) and
  `apu->snes->palTiming` (`dsp.c:586`).
- `statehandler.c` → libc only.
- **Zero `snes_*` calls anywhere in these four.** `snes.c`, `cpu.c`, `ppu.c`, `dma.c`, `cart.c`,
  `input.c` are **not** required. `snes.h` is required as a *header* (for `struct Snes`), and it
  transitively includes `cpu.h/dma.h/ppu.h/cart.h/input.h` — headers only, no link edge.
- Header-name collision: `snes/input.h` vs `src/input.h`. **Not a real hazard** — quoted includes
  resolve relative to the including file first on both MSVC and GCC, and `ct.exe` already compiles
  `src/input.c` *and* `snes/input.c` with `target_include_directories(ct PRIVATE snes src)`
  today. Keep `src` before `snes` in the list.
- `statehandler.c` is only needed because `apu_handleState`/`spc_handleState`/`dsp_handleState`
  reference `sh_*`; if `Belthasar` never saves an APU state those functions are dead code but the
  linker still needs the symbols, so include the file (it is ~100 lines, libc-only).
- **Binary/behavioral cost:** `sizeof(Apu)` ≈ 64 KB ARAM + small; a `Snes` shell is 128 KB+ if
  allocated whole (it embeds `ram[0x20000]`) — the port can allocate one and set only
  `palTiming = false`, or add a 2-field shim. No new global state is shared with the field engine.

---

## 7. What the C port has to do, in order (the boot half)

```c
// 1. once, at Belthasar startup, in place of $C0:0053's JSL $C7:0000
Apu* apu = apu_init(snesShell);      // snesShell->palTiming = false
apu_reset(apu);                      // -> SPC PC = $FFC0 (IPL)

// 2. $C7:004D: wait for $BBAA
do { W(1,0xF0); W(0,0xFF); step(); } while (!(R(0)==0xAA && R(1)==0xBB));
// 3. $C7:0062: dest[0] -> ports 2/3 ; $CC kick ; wait echo
W(2, dst[0]&0xFF); W(3, dst[0]>>8); W(1,0xCC); W(0,0xCC);
while (R(0)!=0xCC) step();
// 4. six blocks
for (int b = 0; b < 6; b++) {
  uint8_t idx = 0;                                  // $C7:007D — reset for EVERY block
  const uint8_t* p = rom + 0x70000 + src[b];
  uint16_t n = p[0] | (p[1]<<8);
  for (uint16_t y = 0; y < n; y++) {
    W(1, p[2+y]); W(0, idx);                        // data BEFORE index
    while (R(0) != idx) step();
    idx++;
  }
  idx += 3; if (idx == 0) idx = 1;                  // $C7:00B8-00BD
  if (b == 5) break;
  W(2, dst[b+1]&0xFF); W(3, dst[b+1]>>8);           // $C7:00C6 — per-block destination
  W(1, idx); W(0, idx);                             // port1 != 0 => another transfer
  while (R(0) != idx) step();
}
// 5. $C7:00E2: execute
W(3,0x02); W(2,0x00); W(1,0x00); W(0, idx);
while (R(0) != idx) step();
W(0, 0x00);
// 6. $C7:00FF tail — mirror into the port's own $00:1E00 page
memset(mem+0x1E00, 0, 0x100);
w16(0x1EF8) = w16(0x1E60) = w16(0x1EF6) = 0x4901;   // dst[2] + size[2]
apu_runCycles(apu, 2938);                           // the $C7:0118 delay, in APU cycles
mem[0x1EFA]=0x00; mem[0x1EFB]=0xC7;
mem[0x1EF0]=0x05; mem[0x1EF2]=0xC9; mem[0x1EF1]=0x00;
mem[0x1E05]=0xE0;                                   // the "driver present" latch
mem[0x1E00]=0x00;
// where W(k,v){ apu->inPorts[k]=v; }  R(k){ return apu->outPorts[k]; }
//       step(){ apu_runCycles(apu, Q); }   // Q = any quantum; outcome is rendezvous-invariant
```

Validation: dump `apu->ram`, slice the six destination ranges, concatenate in table order,
and require `sha256 == b56fa7e7e960d3e97392b7170f84cf0f72452a6aa094632c7db8229d7bbf54df`
(13884 bytes, crc32 `021CEA18`) — identical to the oracle at `--frames 23..26`.

---

## 8. NOT DERIVED (explicit gaps)

1. **The SPC700 driver itself** (ARAM `$0200+`). Beyond the first 9 instructions there is no
   SPC700 disassembler in `tools/`. Closing it: add `tools/dis_spc.py` (SPC700 opcode table) and
   run it over `ROM[0x724C5 : 0x724C5+7151]` at base `$0200`. Needed for front 3, not for boot.
2. **`$C7:0028-$0037`** (7 words). No reader found anywhere in the ROM. Closing it: a CPU
   write/read watch on `$C7:0028` (the harness has `--monitor --writer` for WRAM only; would need
   a ROM-read hook), or an exhaustive decode of bank $C7.
3. **Meaning of `$1EF0=5 / $1EF2=$C9 / $1EF6=$1EF8=$1E60=$4901 / $1EFA/B=$C700`.** Derived that
   the upload writes them and with what values; **not** what the driver/dispatcher does with
   them. Closing it: disassemble `$C7:0140`'s sub-handlers (`$0AD9` table, `$061C`, `$071D`,
   `$0755`, `$08E3`) — front 2.
4. **Exact master-cycle count of the `$C7:0118` delay.** The 2048×5×6 = 61440 figure uses
   standard 65816 `DEX`/`BNE` timings plus `$420D=1`; it is not measured. Closing it: a cycle
   counter around the loop in a trace, or `apu->cycles` deltas across a breakpoint at `$C7:0118`
   / `$C7:011E`. Only affects *when* the driver is ready, never the ARAM image.
5. **Whether any later code re-uploads or patches ARAM outside the driver's own runtime protocol.**
   Only the single `JSL $C7:0000` was found; the runtime path (`$C7:037B` → ARAM `$1E80`,
   `$C7:061C` → mailbox) writes through the *driver*, not the IPL. Closing it: audit the
   `$C7:0AD9` jump-table handlers for further `$2142/$2143` address pushes — front 2.
