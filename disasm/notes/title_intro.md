# THE TITLE INTRO — the $C3 overlay's animation (op 0xFF 02 → $7E:5978)

`boot_loader.md` §3 stops where this file starts: it derived that `FF 02` LEAVES the field engine for
the `$C3` sequence engine, that the 34175-byte overlay decompresses to `$7E:3000`, and that the port
BAKED the settled frame instead of running it. This file derives the ANIMATION the ROM plays into
that frame — **all of it**, from the first pendulum frame to the settled logo — and the port now runs
that animation rather than baking anything.

Everything here was measured against the oracle from a cold boot; the standard fixtures are

```
ct.exe ROM --fb build/_t150.raw --frames 150      + env PEEK_SAVE=build/_title150.state
ct.exe ROM --peek build/_title150.state OUT.raw --btnscript "0:N"     (fb + .wram + .vram + .oam)
ct.exe ROM --ppuregs build/_title150.state --btnscript "0:N"          (m7 + math)
ct.exe ROM --monitor "7E005A:8,..." --from build/_title150.state --btnscript "0:N" --diff --writer
```

The overlay runs FROM WRAM, so `tools/dis.py` (a ROM disassembler) cannot read it. **`tools/wdis.py
<wram.bin> <hexaddr> <len> [m] [x]`** is the same decoder pointed at a `.wram` sidecar; **`tools/
c3script.py <wram.bin> <hexaddr> [len]`** decodes the sequence byte-code (§5.1);
**`tools/ti_sim.py [frames]`** is the derivation harness — it seeds the whole machine from the
oracle's WRAM at boot frame 140 and then diffs every cell it owns against a `--monitor` stream.

---

## 0. What the intro IS, measured

| oracle boot frame | swing frame | on screen |
|---|---|---|
| 0..~140 | — | black (the boot's own load) |
| 141 | 0 | the copyright block + the pendulum entering top-left |
| 141..250 | 0..109 | ⭐ the copyright block ("CHRONO TRIGGER (TM) / (c) 1995 SQUARE Co., Ltd.") |
| 141..884 | 0..743 | ⭐ **the PENDULUM SWING — BG MODE 7**, rotating and growing, swinging right |
| 730..798 | 589..657 | ⭐ the CLOCK FACE spirals out of the pendulum's bob |
| 884..936 | 743..795 | ⭐ "HRONO" slides out of the clock, five packages at five speeds |
| 885 | 744 | ⭐ `$2105 = 2` — the screen switches to **MODE 2** |
| 1068..1172 | 927..1031 | ⭐ the TRIGGER logo scrolls in on BG1 (reg1 += 2 per frame, 104 times) |
| 1173..1236 | 1032..1095 | ⭐ the clock HANDS appear and flash from yellow down to their own colour |
| 1293..1515 | 1152..1374 | ⭐ the offset-per-tile RIPPLE damps out (reg6: 7 → 0, one per 32 frames) |
| 1413..1476 | 1272..1335 | ⭐ "© 1995 SQUARE" fades up (CGRAM $C0, level 0 → 255 by 4) |
| 2133 | 1992 | the script sets `$006E = 1` and installs `$7E:34AB`, the fade-out |
| 2135..2149 | 1994..2008 | ⭐ the FADE: `$038D` walks 15 → 0 while every other task keeps drawing |
| 2149 | 2008 | `$7E:0000 = 1`; the main loop tears the sequence down and reboots into the demo |

⭐ **The pendulum is not sprites.** OAM holds ZERO entries with y < 224 across the swing (`--peek`
`.oam`, low table). It is BG1 in mode 7: `--ppuregs` reports `mode=7` from the start of the swing
until f884, then `mode=2` for the logo.

---

## 1. The sequence engine's frame loop (`$7E:3000`)

```
3000  JSR $3063                     common init
3003  LDA $0384 / AND #1F / ASL / TAX
300A  JSR ($3043,X)                 the SEQUENCE TABLE -- entry 2 = $7E:5978 = the title's SETUP
300D  JSR $388B / JSR $31B0 / JSL $C3:08D0
3017  LDA #A1 / STA $4200           NMI + auto-joypad ON
301C  REP #20 / STZ $00 / STZ $0392
3023  JSR $3176                     ⭐ the main-loop body -- it is a bare RTS
3026  LDA $00 / BEQ ->3023          spin until $7E:0000 != 0  (the sequence's DONE flag)
302A  teardown: NMITIMEN=0, HDMAEN=0, INIDISP=$80, JSR $3177
303B  LDX #06FF / TXS / JML $C0:0000    the reboot boot_loader.md §3.1 already pinned
```

⭐ **The whole animation runs in the NMI.** `$3176` is `RTS`; the main loop does nothing but wait.

The NMI vector is `$00:FFEA = FF10 → JML $00:0500`, and `$7E:0500` is `JML $C3:08B3`. `C3/08B3`
pushes and calls `C3/08D0` (graphics) then `C3/09A4` (sound). **`C3/08D0` IS THE FRAME**, and its
order is the port's order:

```
308EF  LDA #80 / STA $2100           forced blank
308F7  JSL $7E:39BE                  ⭐ 1. the VBLANK hook list ($0940, 8 slots)
30900  $420C = $038C ; $2100 = $038D HDMAEN + brightness
3090F  INC $08                       ⭐ 2. the sequence's own frame counter $7E:0008
30911  MVN $00,$00  $0920 -> $0950   ⭐ 3. the task list is DOUBLE-BUFFERED
3091F  JSR $C3:0A8E                  ⭐ 4. the joypad -> $7E:0002
30938  JSL $7E:398A                  ⭐ 5. the TASK list ($0950 snapshot, 16 slots)
```

```
7E:39BE   X = 0; while (X != $10) { if ($0940+X) JSR ($0940,X); X += 2 }     8 VBLANK slots
7E:398A   X = 0; while (X != $20) { if ($0950+X) JSR ($0950,X); X += 2 }    16 TASK slots
7E:395E / 7E:3974   install into $0920 / $0940 (first free slot)
7E:39A7 / 7E:39DE   the two "task removes itself" exits (pop B/X/P/D, STZ the slot)
7E:39B1             "replace me with X" -- the same pops, then STA $0920,X
```

⚠ **Installs go to `$0920`, the dispatcher runs `$0950`.** They look identical in any WRAM dump
because the NMI copies one to the other every frame; a task installed during frame N first runs at
frame N+1, and a task that deletes itself still runs to the end of the frame it deleted itself in.

⭐ `boot_loader.md` §3.2's `7E:5BBB JMP $39A7` is exactly this: START does not "return", it DELETES
its own task.

---

## 2. ⭐ THE PENDULUM IS MODE 7, AND ITS DRIVER IS SIX WRAM CELLS

`--monitor ... --diff --writer` over the swing gives the live cells and their writers directly:

| cell | meaning | writer |
|---|---|---|
| `$7E:0066` | ⭐ the swing ANGLE, `+= 7` every frame | `7E:5BD7` |
| `$7E:006C` | ⭐ the swing AMPLITUDE, `-= 1` every frame, floored at 1 | `7E:5C52` |
| `$7E:0A00` | ⭐ the SCALE (zoom) — the script's reg0, ramped by the script itself (§5.2) | `C3:0B53` |
| `$7E:005A/5C/5E/60` | the mode-7 matrix A / B / C / D | `7E:5C36` |
| `$7E:0A02` | BG1HOFS — the script's reg1 | `7E:5C50` |
| `$7E:0A04` | BG1VOFS — the script's reg2, derived below | `7E:5C50` |

The per-frame routine (`$7E:5BCF`, DP = 0):

```
5BCF  LDA $66 / CLC / ADC #$0007 / STA $66      ⭐ angle += 7
5BD7  JSR $3461                                  cos(angle)      (see §2.1)
5BDA  PHP / BPL / EOR #FFFF / INC A              |value|, sign AND CARRY kept on the stack
5BE1  STA $F0 / LDA $6C / STA $F2 / JSR $31BF    |cos| * amplitude   (16x16 -> 32)
5BEA  LDA $F5 / LSR / LSR                        take the MIDDLE word, >> 2
5BEE  PLP / BPL / EOR #FFFF / INC A              re-apply the sign
5BF5  CLC / ADC #$0200 / STA $FE                 ⭐ theta = $200 + (cos(angle)*amp >> 18)
5BFB  JSR $3461 (cos theta) / STA $F0
5C00  LDA $0A00 / STA $F2 / JSR $31BF            cos(theta) * scale
5C08  LDA $F5 / INC A / STA $5A / STA $60        ⭐ M7A = M7D = (that >> 16) + 1
5C0F  LDA $FE / JSR $3465 (sin theta) ...
5C22  JSR $31BF / LDA $F5                        sin(theta) * scale
5C2E  STA $5C                                    ⭐ M7B = that
5C30  EOR #FFFF / INC A / STA $5E                ⭐ M7C = -M7B
5C36  STZ $F0 / STZ $F6 / LDA #0001 / STA $F2
5C3F  LDA $0A00 / STA $F4 / JSR $339F            ⭐ 0x00010000 / scale
5C47  LDA $F8 / SEC / SBC #$0100 / STA $0A04     ⭐ BG1VOFS = 65536/scale - 256
5C50  DEC $6C / BNE / INC $6C                    ⭐ amplitude -= 1, floor 1
5C56  RTS
```

Checked against the live cells: `$0A00 = 0x84` → `65536/132 - 256 = 240 = $00F0` = the measured
`$0A04`. Exact.

### 2.1 sin / cos = `$7E:3461` / `$7E:3465` over a ROM table

```
3461  CLC / ADC #$0100          ⭐ the "+90 degrees" entry: $3461 = COS, $3465 = SIN
3465  AND #$03FF / TAX          a full circle is $400 units
3469  LDA $C0:F900,X / AND #$00FF     ⭐ a 1024-BYTE table in ROM at $C0:F900
3470  CPX #$0200 / BCS ->3479         ⭐ this comparison LEAVES A CARRY the callers use
3475  EOR #$FFFF / INC A        the first half of the table is NEGATED
3479  RTS
```

⚠ **That `CPX #$0200` leaves a carry the callers ADD.** `$7E:5C98`'s `ADC #$FFE0` / `ADC #$FFC0`
(§6.2) pick it up, so the port carries the term — but **in this sequence it is always 0 and cannot
be graded**: the spiral's angle walks `$F6 -> $70`, so neither lookup index ever reaches `$200`
(counted: 0 of 68 frames for both the sine and the cosine call). It is written because that is what
the ROM's `ADC` does, not because the title needs it, and the mutation that drops it SURVIVES every
frame of both checks. The pendulum driver has no such term at all: its `ADC #$0200` is preceded by an
explicit `CLC`, and the `PLP` before it restores the flags its own `PHP` pushed.

### 2.2 ⭐⭐ `$7E:31BF`, AND THE CARRY THAT LOOKED UNDERIVABLE

`$31BF` is the overlay's 16x16→32 multiply, built from four hardware 8x8s, and `$339F` its 32/16
divide (quotient in `$F8`). This note used to say that one bit of the matrix could not be derived,
because `$31BF` does

```
31D5  STZ $F6                   ⭐ 16-bit: this clears $F6 AND $F7
31EE  CLC / ADC $4216           the middle partial products
31F2  ROL $F7                   ⭐ 16-bit: it rotates the WORD at $F7, i.e. $F7 | ($F8 << 8)
31F7  ADC $F5                   ...so BIT 7 OF $F8 has just been rotated INTO this carry
3207  PLP / RTS                 (and the carry the routine EXITS with is the one it entered with)
```

so the result depends on stale scratch at `$F8`. ⭐ **It is not stale, it is the divide's own
quotient.** `$F8` is written by `$339F` (the `65536/scale` at the end of the same driver) and then
shifted ONE PLACE LEFT by each following `$31BF` — the `ROL` leaves `$F8 = ($F8 << 1) & 0xFF` and
never anything else. The measurement that used to be quoted as proof of unpredictability is exactly
this: `--hookpc 7E5C47 --hookwatch F8` reported `$01F4` leaving the divide and `--hookpc 7E5BCF
--hookwatch F7` `$..E8` at the next entry — and `0xF4 << 1 = 0x1E8`, one multiply later.

So the matrix is computable, and the port computes it. `tools/ti_sim.py` grades that end to end.

### 2.3 The VBLANK hook that writes the registers (`$7E:5C57`)

```
5C57  $5A/$5B -> $211B (M7A)      one byte at a time: these are the 8+8 write-twice registers
5C61  $5C/$5D -> $211C (M7B)
5C6B  $5E/$5F -> $211D (M7C)
5C75  $60/$61 -> $211E (M7D)
5C7F  $0A02/$0A03 -> $210D (BG1HOFS / M7HOFS)
5C8B  $0A04/$0A05 -> $210E (BG1VOFS / M7VOFS)
```

The other four vblank hooks are the rest of the frame's plumbing, and the port runs all five:

| hook | what it is |
|---|---|
| `$7E:388B` | DMA `$7E:2200` (512 B) → CGRAM. ⭐ The palette shadow, uploaded every frame. |
| `$7E:380B` | `JSR $3870` finishes the current high-OAM byte, DMA `$7E:0700` (0x220 B) → OAM, then resets the drawers' cursors `$11 = 0`, `$13 = $0900`. |
| `$7E:3843` | `M7A = 1`, `M7B = $E0`, then a **B→A DMA of `$2134` (MPYL = 1 × −32)** over `$7E:0700..08FF`: the OAM shadow is cleared to `$E0`, i.e. every sprite parked off-screen. |
| `$7E:5C57` | the matrix + BG1's scroll (above). |
| `$7E:6212` | DMA the 64 words at `$7E:0C00` → VRAM word `$7000` (BG3's tilemap = the offset-per-tile table, §7.1), then `BG3HOFS = $0044`. |

The rest of the mode-7 state is set ONCE, in the setup, with DP pointed AT the register file
(`5A59 LDA #$2100 / TCD`, so `STA $05` is `$2105`):

```
$2105 = 07     BG MODE 7
$211A = 80     ⭐ largeField (screen-over -> transparent outside the 1024x1024 field), no flips
$212C = 01     TM = BG1 only          $212D = 10   TS = OBJ only
$2107 = 65  $2108 = 60  $2109 = 70  $210B = 55      BG1 map $6400 (64x32), BG2 map $6000,
$2130 = 02  $2131 = 21     CGWSEL / CGADSUB          BG3 map $7000, BG1/BG2 chr $5000
$211F = 0020   ⭐ M7X = 32            $2120 = FFC0   ⭐ M7Y = -64
$210F = FF40   BG2HOFS
```

⚠ M7X/M7Y are WRITE-ONLY with no WRAM shadow, so they cannot be read back out of a `--peek`; the
oracle reports them (`--ppuregs` has an `m7:` and a `math:` line).

---

## 3. ⭐ THE RENDER MODEL IS PROVEN BYTE-EXACT

A standalone renderer (mode-7 affine sampling + CGRAM) fed ONLY by the oracle's own VRAM, CGRAM and
`m7:` line reproduces the oracle's framebuffer with **0 mismatching pixels of 57344**. The three
things that had to be right, each of which cost a wrong answer first:

1. **The sampling formula** is LakeSnes's (`snes/ppu.c` `ppu_calculateMode7Starts` /
   `ppu_getPixelForMode7`): per line, `startX = ((A*clippedH) & ~63) + ((B*ry) & ~63) + ((B*clippedV)
   & ~63) + (M7X<<8)` (and the C/D pair for Y), then per pixel `+= A` / `+= C` and `>> 8`; the
   tilemap is the LOW byte of VRAM word `(yp>>3)*128 + (xp>>3)` and the texel the HIGH byte of word
   `tile*64 + (yp&7)*8 + (xp&7)`. `largeField` clips outside the field to index 0.
2. ⚠ **The scanline is 1-BASED.** `ppu_runLine` is called for lines 1..224 and it is `line`, not
   `line-1`, that goes into the matrix — so the fb's first row is rendered with `ry = 1`.
3. ⚠⚠ **The oracle's 512x480 fb is not a plain 2x image of the frame.** `ppu_putPixels` writes line
   `y` to buffer row `y*2 + 16`, so the 224 visible lines occupy IMAGE rows **8..231** of the 256x240
   that `ctlib/fb.py` decodes. Comparing row 0 to row 0 leaves ~6000 "mismatches" that look exactly
   like a rendering defect, and no amount of matrix-phase hunting fixes them.
4. ⚠ **Phase**: the picture of boot frame N is drawn with the registers the vblank hooks pushed at
   N, i.e. **the shadow cells as the tasks left them at the end of N−1**. Measured directly:
   `--ppuregs` at boot 141 reports `(126,−28,28,126)`, which is the `--fb --frames 140` WRAM's
   `$005A..`. Using the cells of the frame being drawn is a 5000-pixel error, and it is why
   `CtTitleIntroTick` latches first and runs the tasks after.

Colour math during the swing is `CGWSEL = $02` / `CGADSUB = $21`: no clipping, ADD, no halving,
fixed colour (0,0,0), enabled for BG1 and the backdrop. `--hdma` reports **no active HDMA channels**
and one flat band for the whole frame.

---

## 4. The texture: 160 tiles converted to mode-7 format at boot

```
5978 setup, in order (VMAIN = $80):
  VRAM $4000 <- blob $48 ($FE:6002, 5632 B)      OBJ chr
  VRAM $5000 <- blob $45 ($FE:5307, 6144 B)      BG chr
  VRAM $0000 <- JSR $5E3C with Y = $A0           ⭐ the MODE-7 map+chr, 160 tiles
  VRAM $6400 <- blob $4E ($FE:6D6D, 256 B)       BG1 tilemap
  VRAM $6000 <- blob $4B ($FE:6C4F, 2048 B) via $5AB2
  $7E:2000   <- blob $51 ($FE:6E34, 480 B)       ⭐ the BASE PALETTE the fades blend from
```

`$7E:5E3C` reads 4bpp planar tiles from **bank `$7F` starting at `$7F:0200`** and writes them to
`$2119` (VMDATAH — the high byte, i.e. mode-7 char data) after transposing bitplanes.

⭐ **Nothing writes VRAM again for the rest of the sequence** except the 64-word offset table at
word `$7000`. Verified by the generator on every run: it diffs the whole 64 KB between two
logo-phase frames and refuses on any other difference.

---

## 5. ⭐⭐ THE MACHINE: the $C3 SCRIPT INTERPRETER (C3/0B03)

The task list is not hand-managed — a **byte-code script** installs and removes tasks, waits frames
and pokes registers. `$7E:3749`, the first task in the list, is its runner:

```
3749  LDA #0A00 / TCD / PHK / PLB / JSL $C3:0AFF     ⭐ DP = $0A00, then run the script
```

⭐ **Because DP is `$0A00`, the interpreter's sixteen "registers" ARE the PPU shadow cells**:
`reg0 = $0A00` the mode-7 scale, `reg1 = $0A02` BG1HOFS, `reg2 = $0A04` BG1VOFS, `reg6 = $0A0C` the
ripple amplitude. `$0A20` is the script pointer, `$0A30` the CALL return slot, `$0A22+2n` sixteen
loop counters.

### 5.1 The opcode set

Fetch is ONE byte (`LDA ($20) / INC $20` — a 16-bit read, only the low byte is the opcode), and
operands are read as 16-bit words from the next byte.

| opcode | operands | meaning |
|---|---|---|
| `0R` | `imm16` | `reg[R] = imm16` |
| `1R` (any high nibble with bit 5 clear, bit 4 set) | `imm16` | `reg[R] += imm16` |
| `2R` | `bb` | `reg[R] = reg[(bb & $1E)/2]` |
| `3R` | `bb` | `reg[R] += reg[...]` |
| `8n` | `cnt16 tgt16` | counter n: `++`, jump `tgt` until it reaches `cnt`, then fall through — **no yield** |
| `9n` | `cnt16 tgt16` | the same, **yielding every iteration** ⇒ `9n cnt <self>` is WAIT `cnt` FRAMES |
| `..0` | `addr16` | `$00F0 = addr; JSL $7E:37F3` ⇒ **CALL the overlay subroutine at `$7E:addr`** |
| `..1` | `addr16 vv` | byte `[$00:addr] = vv` |
| `..2` | `addr16 vvvv` | word `[$00:addr] = vvvv` |
| `..3` | `addr16` | CALL (return slot `$0A30`) |
| `..4` | — | RETURN |
| `..5` | `vv` | wait until the APU's `$2142` low nibble < `vv` |
| `..6` | `cccc pppp` | sound command (`$1E00/$1E02` → `C7/0004`) |
| `..7` / `..8` | `addr16` | remove / add `addr` in the **$0940 VBLANK list** |
| `..9` / `..A` | `addr16` | remove / add `addr` in the **$0920 TASK list** |
| `..B` | `vv rr` | `$21rr = vv` |
| `..C` | — | YIELD (end this frame's run) |
| `..D` | `addr16` | JUMP + yield |
| `..E` | `addr16` | JUMP (keep running) |
| `..F` | — | END: `$7E:0000 = 1`, the sequence's done flag |

The dispatch is `A = op & $F0`; `$80`/`$90` go to the counter opcode (C3/0C92), everything else
indexes the 16-entry table at `C3:0B74` by the LOW nibble.

### 5.2 ⭐ The title's script, decoded (`tools/c3script.py`)

**Part one, `$7E:5B05`**, armed by the setup:

```
5B05: FA 3749 / FA 5BCF / FA 37FA / FA 5FFE        install the four resident tasks
5B11: F1 0396 01                                   $0396 = 1
5B15: F2 0072 006E                                 ⭐ $0072 = 110 -- the copyright block's lifetime
5B1A: FA 5D32                                      install the copyright task
5B1D: F8 388B / F8 380B / F8 3843 / F8 5C57 / F8 6212   the five VBLANK hooks
5B2C: 00 0040     reg0 = $40
5B2F: 06 0007     ⭐ reg6 = 7      -- the ripple amplitude, and the damping task's countdown
5B32: 01 FF40     reg1 = $FF40    -- BG1HOFS
5B35: 00 0080     reg0 = $80      -- the mode-7 scale starts at 128
5B38: 10 0001 / FC / FC / FC / 90 007F 5B38        ⭐ scale += 1 every FOUR frames, 128 times -> 256
5B43: 91 004D 5B43                                 wait 77 frames
5B48: FA 5D01 / FA 5C98                            ⭐ the clock: draw task + spiral task
5B4E: FD 5B4E                                      park
```

**Part two, `$7E:5B51`**, armed by the spiral task when it finishes:

```
5B51: 91 0054 5B51      wait 84 frames
5B56: 01 FF00           reg1 = $FF00        -- BG1HOFS, the TRIGGER logo off to the side
5B59: FA 5D71           ⭐ install the letter-slide task
5B5C: F9 5BCF           ⭐ remove the pendulum driver: the swing stops here
5B5F: FC
5B60: F0 5DA7           ⭐ CALL $5DA7: `$2105 = 2` (BG MODE 2) and reg2 = $FFB9 (BG1VOFS)
5B63: 91 00B7 5B63      wait 183 frames
5B68: 11 0002 / 90 0067 5B68   ⭐ reg1 += 2, 104 times -- the TRIGGER logo scrolls in
5B70: F0 5DE9           CALL $5DE9: arm a palette job -- CGRAM $B0, 11 colours, $80 -> 0 by 2, kind 4/3
5B73: FA 5D20           install the clock-hands task
5B76: 91 0077 5B76      wait 119 frames
5B7B: FA 5BBF           ⭐ install the ripple's damping task
5B7E: 91 0077 5B7E      wait 119 frames
5B83: F0 5DB4           CALL $5DB4: arm a palette job -- CGRAM $C0, 16 colours, 0 -> 255 by 4, kind 5/0
5B86: FA 5E1E           install the "© 1995 SQUARE" task
5B89: 91 02CF 5B89      wait 719 frames
5B8E: F1 006E 01        ⭐ $006E = 1 -- START stops working
5B92: FA 34AB           install the fade-out into the attract demo
5B95: FD 5B95           park
```

Every install in that script lands on the frame `--monitor "7E0920:32" --diff` says it does, to the
frame.

---

## 6. The tasks

| task | frames (boot) | what it does |
|---|---|---|
| `$7E:3749` | always | the script runner (§5) |
| `$7E:5BCF` | 139..884 | the mode-7 pendulum driver (§2) |
| `$7E:37FA` | always | parks `$4202` around a ROM call — no state of its own |
| `$7E:5FFE` | always | the offset-per-tile RIPPLE (§6.1) |
| `$7E:5D32` | 139..249 | the copyright block + the sequence's opening timer (§6.3) |
| `$7E:5B99` | 189.. | the button (§6.6): installs the fade, `$7F0060 = 1` unless L/R is held |
| `$7E:5D01` | 729.. | draws the clock face at `($62, $64)`, two packages 64 px apart |
| `$7E:5C98` | 729..797 | the clock's SPIRAL (§6.2) |
| `$7E:5D71` | 884.. | the five CHRONO letters sliding (§6.4) |
| `$7E:5D20` | 1173.. | the clock HANDS, a static package at (58, 100) |
| `$7E:5BBF` | 1293..1514 | `if (($08 & $1F) == 0) { if (--reg6 == 0) delete me; }` — the ripple's damping |
| `$7E:5E1E` | 1413.. | "© 1995 SQUARE", two packages at y = 208 |
| `$7E:34AB` | 2133 | the exit: one sound command, then it becomes `$34C2` in its own slot (§6.6) |
| `$7E:34C2` | 2134..2149 | the FADE-OUT, and the only thing that ends the sequence (§6.6) |

### 6.1 ⭐ `$7E:5FFE` — the ripple is OFFSET-PER-TILE, not a scroll

```
5FFE  X = $7C; do { $0C02,X = $0C00,X; X -= 2 } while (X != $3E)   shift words 32..63 up by one
600E  LDA $08 / ASL A x4 / JSR $3465                               sin(frame * 16)
601E  |sin| * $0A0C (reg6) through $31BF, sign re-applied
6031  SEC / SBC #$004A / AND #$03FF / ORA #$2000 / STA $0C40       ⭐ a new entry at the left
```

The table at `$7E:0C00` is **BG3's tilemap**, uploaded to VRAM word `$7000` by the vblank hook
`$6212`. In BG mode 2 the PPU does not draw BG3 — it reads it as **offset-per-tile**: for each
column it fetches row 0 (a horizontal offset) and row 1 (a vertical offset), and bit 13 means "this
applies to BG1". Words 0..31 (row 0) stay zero for the whole sequence; words 32..63 (row 1) are the
ripple. So each COLUMN of the TRIGGER logo gets its own vertical offset, the table shifts one column
per frame (the wave travels left to right), and the amplitude is reg6 — which the damping task walks
down to zero, at which point the logo goes still.

⚠ It runs from frame 139, long before mode 2 exists; in mode 7 the upload is simply unread.

### 6.2 `$7E:5C98` — the clock's spiral

```
5C98  A = $68 - 2 ; if (A < $70 signed) -> finish
5CBA  $68 = A
5CBC  cos($68) -> |.| * $00D8 -> $F5, sign re-applied, + $FFE0 + CARRY   ⭐ $62 (x)
5CDE  sin($68) -> |.| * $00C3 -> $F5,               + $FFC0 + CARRY      ⭐ $64 (y)
finish (5CA1):  $212C = $0013 (16-bit: TM = BG1+BG2+OBJ, TS = 0)
                $0A20 = $5B51 (arm part two of the script)
                $62 = $38, $64 = $3A ; JMP $39A7 (delete me)
```

The angle walks `$F8 → $70` in steps of 2, so the clock takes 68 frames to spiral out of the
pendulum's bob (its starting point, `(216, 130)`, IS the bob) and park at (56, 58). The CARRY in both
adds is the one `$3465`'s `CPX #$0200` left — ⚠ always 0 here, see §2.1. Computed, all 68 frames
match the oracle's `$62`/`$64` exactly; the port captures none of them.

### 6.3 `$7E:5D32` — the copyright block, and why it vanishes at frame 110

```
5D32  $15 = $90 ; $17 = $B0 ; JSL $C3:0D5E with package $61D9     14 sprites, 16x16, palette 5
5D43  DEC $72 / BNE -> ; JMP $39A7                                delete me at zero
5D4A  if ($72 == $3C) { install $5B99 ; $0396 = 0 }               ⭐ START becomes live 50 frames in
```

The script seeds `$0072 = 110`, so the block is drawn for exactly 110 frames and is gone on the
111th — the "why" this note used to list as open. Nothing else changes at that frame.

### 6.4 `$7E:5D71` — the five CHRONO letters

```
5D71  $17 = $52
      for X = 0,2,4,6,8:
        $0046,X += $7E:5D67,X                  per-letter speed: 4, 9, 14, 20, 24 (1/16 px)
        if >= $0050,X: = $0050,X               per-letter target: $670,$770,$860,$9A0,$A90
        $15 = that >> 4
        JSL $C3:0D0E with package $7E:5D5D,X   $60F1, $610A, $6123, $6148, $6161
```

All five start at `$05A0` (x = 90, inside the clock) and fan out to x = 103/119/134/154/169 in about
52 frames. The clock face is the "C".

### 6.5 The sprite drawers, `C3/0D0E` (8x8) and `C3/0D5E` (16x16)

A PACKAGE is `[count+1]` then `[tile, attr, dx, dy]` per sprite, read through the WRAM port
(`STA $2181 / STZ $2183`, so bank `$7E`). Both drawers append at the shadow cursor `$11` (a byte
index into `$7E:0700`) and advance the high-table pointer `$13` every four sprites:

* `0D0E`: `x = dx + $15` and `y = dy + $17` in **8 bits** (they wrap), and the high byte is shifted
  right two — zeros in from the top, so every sprite is 8x8 with x < 256.
* `0D5E`: `x = dx + $15` in **16 bits**, clamped to `$9801` when it reaches 256, then `ASL` puts bit
  15 in the carry and `ROR / SEC / ROR` rotates x-high and then a SET bit into the top of the high
  byte — so every sprite it emits is **16x16**, and an off-screen one gets x = 257.

⭐ The high-OAM byte is therefore built **from the top down**: the first sprite of a group of four
ends at bits 1:0, which is exactly the hardware's layout. The vblank hook `$3870` finishes a partial
group by shifting in `(A >> 2) | $40` per unused slot — x-high 1, i.e. off screen.

The eleven packages, `$603F..$6211`: clock face (31 + 13 sprites), the five HRONO letters (6/6/9/6/9),
"© 1995" (8), "SQUARE" (3), the clock hands (9), the copyright block (14).

### 6.6 ⭐ `$7E:5B99` + `$7E:34AB` + `$7E:34C2` — the exit, and the fade into the demo

The sequence has exactly ONE way out, and both of its triggers go through the same task:

```
5B99  LDA $6E / BNE ->RTS       ⚠ after the script's handover ($006E = 1) this task does nothing
5B9D  LDA $70 / BNE ->RTS       it can only fire once
5BA1  LDA $02 / BEQ ->RTS       ⭐ ANY button -- $0002 is the whole 16-bit auto-read word
5BA7  INC $70 ; LDX #$34AB / JSR $395E      install the fade
5BAF  LDA $02 / BIT #$30 / BNE ->5BBB       ⭐ L or R held -> skip the next two lines
5BB5  LDA #$01 / STA $7F0060                the flag scene 433 branches on
5BBB  JMP $39A7                             delete me

34AB  $1E01 = $0040 ; $1E00 = $81 ; JSL $C7:0004     tell the driver to fade the music
34BC  LDX #$34C2 / JMP $39B1                ⭐ REPLACE myself, in my own slot, with $34C2

34C2  DEC $038F / BNE ->RTS                 the step counter
34C9  $038F = $038E                         reload the period
34CF  DEC $038D / BNE ->RTS                 ⭐ the BRIGHTNESS the NMI pushes to $2100
34D5  $1E00 = $F0 ; JSL $C7:0004            stop the music
34DE  INC $00                               ⭐ $7E:0000 -- the sequence's DONE flag
34E0  JMP $39A7                             delete me
```

⭐ The FADE IS THE EXIT. `$038D` is the value `C3/08D0` writes to `$2100` every frame (or `$80` —
forced blank — once it is 0), so the whole screen dims 15 → 0 over fifteen frames **while every
other task keeps drawing**: the clock, the letters and the copyright are all still being rebuilt into
the OAM shadow, just darker each frame. Only then does `$7E:0000` go up, the main loop at `$7E:3026`
notice, and `$303B` reboot.

⭐ **The title has its OWN SONG, and the setup starts it.** `$7E:5AA5 LDA #$30 / JSR $375F`, the
last thing `$7E:5978` does before it returns. `$375F` is "play song A": the id into `$1E01`, `$10`
(LOAD + PLAY) into `$1E00`, `LDX #$FFFF / STX $1E02` over the other two, then `JSL $C7:0004`. The
loader's own record lookup (`C7/0A39`) then overwrites `$1E02/$1E03` with **song $30's record**,
`$C7:241D + $60` = `06 C0` → `$1E03 = $06`, `$1E02 = $C0`. Measured on the oracle: `$1E00 = 10 30`
staged at boot f97 by `$7E:376A`, `C0 06` written by `C7:0A53` in the same frame, and the mailbox
reads `30 C0 06` for the rest of the sequence.

⭐ **`$375F` writes the id into `$0389` AND `$038A`, and that second write has a reader.**
`C3/09A4` — the NMI's OTHER half, called by `C3/08B3` right after `C3/08D0` — is a SONG-CHANGE
WATCHER:

```
09A4  LDA $0388 / BNE ->09D8              a busy latch
09AB  LDA $0384 / BIT #$20 / BNE ->09D8   the SEQUENCE id gates it off for entries $20-$3F
09B2  LDA $038A / CMP $0389 / BEQ ->09D8  ⭐ the song WANTED vs the song PLAYING
09BA  STA $0389 / INC $0388               latch it, mark busy
09C0  $1E00 = $10 ; $1E01 = $038A ; $1E02/$1E03 = $FF/$FF ; JSL $C7:0004
09D5  STZ $0388
```

So `$038A` is a REQUEST and `$0389` the acknowledgement: two other callers in the overlay write only
`$038A`, and this is what services them on the next NMI. `$375F` writes both, which is exactly why
the title loads its song ONCE instead of every frame. ⚠ The title never changes `$038A` again
(measured: `$0388/$0389/$038A` are `00/30/30` from boot f97 to the end), so the watcher is written
out and compared but its body never runs here — see §9.

⭐ **The setup then WAITS: `$5AAA LDA $2143 / AND #$0F / BEQ ->5AAA`**, and it is not a formality.
`--hookpc 7E5AAA` counts ~33000 iterations across boot frames **133..139** — just over SIX FRAMES,
and the last thing that happens before the sequence's first NMI. The port runs the same spin on the
loader's own clock (`CtAudioWaitReplyNibble`, `src/audio.c`), and its emulated driver answers after
**6 frames** — measured by the port, not baked. ⚠ The ROM's loop is unbounded; the port's stops at
`SPIN_LIMIT` and says so. ⚠ The port does not make the ENGINE lose those frames the way the ROM's
blocked CPU does — the SPC time is debited so it is not advanced twice, but nothing pins the title's
start to a boot frame number.

⭐ **The two SOUND commands are the exit's, and they go out through the one mailbox.** `$1E00-$1E03`
at `$7E:1E00` is the same mailbox the field VM and the battle stage into, and `$81` / `$F0` are both
`>= $80`, so `C7/061C` writes all four bytes straight to `$2140-$2143` (audio_commands.md §3/§4).
What each routine writes is exactly:

| | `$1E00` | `$1E01` | `$1E02` | `$1E03` |
|---|---|---|---|---|
| `$34AB` | `$81` | `$40` | `$00` | ⚠ untouched |
| `$34D5` | `$F0` | untouched | untouched | ⚠ untouched |

`LDX #$0040 / STX $1E01` is a 16-BIT store, which is where `$1E02 = $00` comes from. ⚠ **`$1E03` is
persistent RAM** (audio_commands.md §10.4): neither routine writes it, so what reaches `$2143` is
what the last command left — and the last command was the SETUP'S OWN SONG LOAD, so it is `$06`,
song `$30`'s record byte. The oracle reads `81 40 00 06` at the exit and so does the port.

⚠ **A correction worth keeping.** This section first said the `$06` was "the boot's, not the
title's", and that the port's `$05` came from `C7/01A1`'s record write-back not being modelled.
BOTH halves were wrong. The write-back has been in `src/audio.c` all along (`$C7:0A39`, right after
`CtAudioSongRecord`), and the `$05` was simply **song 0's** record — the port had never played the
title's song at all, so it was still carrying the field's. One wrong attribution, and the fix was in
a different file from the one named.

⚠ **`$F0`'s staging is not observable at a frame boundary.** The oracle's `$1E00` is cleared by the
service (`C7/0192`) inside the same frame, and by the end of that frame the main loop has already
rebooted and staged the NEXT stage's command over it (`00 00 FF 05`). The `$81` is observable — the
frames before it read `$1E01/$1E02 = 30/C0` — and that is the one the WRAM check grades.

Three things measured rather than assumed:

* **The button is not START and it is not the host's.** `$0002` is the whole joypad word, so any
  button leaves; and the task does not exist for the sequence's first fifty frames (§6.3 installs it
  when `$0072` reaches 60). A press before that does nothing at all — `--btnscript "0:30,1:4,0:120"`
  from boot 120 leaves `$7F0060` and `$0070` at zero for the whole run.
* **`BIT #$30` is L or R.** Held, the fade still runs but `$7F0060` stays 0, so the reboot falls
  through scene 433's test into the attract demo instead of the game. Measured with mask `$400`
  (which puts `$0020` in `$0002`): `$038D` ramps, `$7F0060` never moves.
* **The window is sixteen frames**, and it is the same sixteen `boot_loader.md` §3.2 always quoted:
  `$7F0060` at boot f61 of the fixture, `$7E:0000` at f77, the task list wiped at f79.

---

## 7. The logo phase's render

After `$5DA7` the PPU is in **mode 2**: BG1 = the TRIGGER logo (map `$6400`, a 64x32 map, scroll
`reg1`/`reg2`), BG2 = the pendulum at rest (map `$6000`, scroll `$FF40`/0 — **completely static from
frame 885 on**, measured: the same 3280 non-black pixels in the same box at boot 900/1000/1230/1400/
1600), OBJ = the clock, the letters, the hands and the copyright line. `--peek PEEK_ISOBG` isolates
each of the three and says exactly that.

### 7.1 Offset-per-tile

`ppu_handleOPT` / `ppu_getOffsetValue` in `snes/ppu.c` is the model the port transliterates: for
column `c` (derived from the layer's own hScroll), the entries fetched are BG3's map words at
`row 0, col c-1` and `row 1, col c-1`; bit 13 makes them BG1's, bit 14 BG2's. Ours only ever carry
bit 13, on the vertical entry.

### 7.2 The sprite limits are transliterated, and they never fire

Ninety-odd sprites are live in the logo phase, and `ppu_evaluateSprites` drops everything past **32
sprites** or **34 8x1 slivers** per line, so the port carries both counters. ⚠ **Neither limit is
ever reached**: the densest scanline of the whole sequence holds **20** sprites and 20 slivers
(counted from the oracle's own OAM at boot 1230, the frame with the most live sprites). So this is
not pinned with a mutation — raising the cap to 999 changes no pixel at any frame, and was confirmed
to SURVIVE. The counters stay because that is what the PPU does, not because this sequence needs
them.

---

## 8. The palette jobs — `$7E:0520`, run by `FD:E39C`

Twelve 12-byte records, walked every frame by a ROM routine with DP = `$0500`:

```
+0 kind (high nibble; 0 = the slot is free)   +1 first CGRAM index   +2 count
+3 step   +4 tick   +5 period   +6 blend mode   +7 level   +8 target
```

`+4` counts up to `+5` and then the level walks from `+7` toward `+8` by `+3`. ⭐ **The frame the
level would pass the target, the record copies `$7E:2000` (the base palette) straight over
`$7E:2200` and frees itself** (`FD/E624`, `FD/E8AC`) — which is why the settled screen shows the
untouched palette and not the last blended one. The kinds dispatch at `FD/E3EA`; the title uses two:

* **kind 5, mode 0 (`FD/E64F`)** — scale every component by `level/256` through the hardware
  multiplier. The title's job is CGRAM `$C0`, 16 colours, 0 → 255 by 4: "© 1995 SQUARE" fading up
  over 64 frames.
* **kind 4, mode 3 (`FD/EA5C`)** — blend RED and GREEN toward 31 by `level/256` and write BLUE as
  ZERO. The title's job is CGRAM `$B0`, 11 colours, `$80` → 0 by 2: the clock hands arriving in a
  yellow flash that decays to their own colour over 64 frames, the blue restored by the final copy.

Measured: CGRAM changes in exactly two windows across the whole logo phase, rows `$B0` (boot
1173..1236) and `$C0` (boot 1413..1476), and nowhere else.

---

## 9. ⭐⭐ WHAT IS PORTED, AND HOW FAR IT IS EXACT

`src/title_intro.c` is the machine above — the interpreter, both lists, every task, both drawers,
the palette-job runner and a renderer that is LakeSnes' own fetch for mode 7 and for mode 2 with
offset-per-tile. `src/data/title_intro.h` holds only what a machine cannot compute: the graphics the
boot load built (captured from the oracle's VRAM, with refusals), the overlay's own byte-code and
package tables, the base palette, one seed state, and `$C0:F900`.

⭐ **Nothing about the animation is captured any more.** The per-frame mode-7 matrix stream this
file used to describe is gone: §2.2 made it computable.

Two independent gradings:

* `tools/ti_sim.py` — the machine against the oracle's live WRAM, cell by cell, in the three ways
  the sequence can run: the script's own handover (**boot 141..2148**), a button press (**141..757**)
  and the same press with L held (**141..757**). ⭐ **97347 values checked, 0 mismatches** — the
  registers, the matrix, the spiral, the letter slide, the ripple, the frame counter, the task list,
  the palette shadow, the brightness trio `$038D/$038E/$038F`, `$0070`, `$7F0060` and the sound
  mailbox `$1E00..$1E03` (stale `$1E03` included). Each window
  ends where the SEQUENCE does (the frame `$34C2` sets `$7E:0000`), which the harness derives from
  its own done flag rather than hard-coding; past it the ROM has wiped its task list and is loading
  the next stage. ⚠ This grades the DERIVATION, not the shipped C.
* `tools/title_intro_cmp.py` — the ENGINE's composed frame against the ORACLE's own framebuffer, all
  57344 pixels (not a hash of our own output — memory `check-vs-own-reference`). **0 mismatching
  pixels at every frame tried across the whole sequence**, frame 0 to frame **2007**: the swing, the
  copyright window, the clock's spiral, the mode switch, the letter slide, the logo scroll-in, the
  hands' flash, the ripple and its damping, the settled title, and every frame of the fade-out.

⚠ **Frame 2008 is the boundary, and it is a raster effect, not a defect.** `$34C2` sets `$7E:0000`
during the NMI; the main loop, which is running during the ACTIVE DISPLAY, sees it and writes
`INIDISP = $80` about 37 scanlines in — so the oracle's last frame is the top of a dim title over
black (132 non-black pixels, two per line for the pendulum's rod, then nothing). This engine has no
scanline model for a mid-frame blank, and it has already rebooted by then.

⭐ **The settled-frame bake is deleted.** `drawTitleScreen` + `src/data/title_screen.h` +
`tools/gen_title.py` are gone: the machine draws that frame, byte for byte, on its own.

⚠ **Three things in the port are transliterated but cannot be graded by any frame of this
sequence**, and none of them is pinned — a mutation nothing can kill is not a grade:

* the renderer's `largeField` clip (M7SEL = `$80`) never fires: the sampled coordinates never leave
  the 1024x1024 field (measured at frames 110/130/200/400/580);
* the PPU's 32-sprite / 34-sliver per-line limits never bind: 20 is the most any scanline carries
  (§7.2);
* the sine routine's `CPX #$0200` carry is always 0, because the spiral's angle never reaches `$100`
  (§2.1);
* the fade's step PERIOD `$038E` is 1, so reloading `$038F` from it is indistinguishable from
  hard-coding 1 (§6.6);
* `C3/09A4`'s body never runs: the title latches `$0389 = $038A` in its setup and never changes the
  request, so the watcher only ever finds them equal (§6.6). Worse for grading, the port SEEDS
  `$0389` from the oracle's WRAM — the seed is taken after the setup — so even removing `$375F`'s
  own `STA $0389` changes nothing. Written because it is the NMI's other half, not because this
  sequence exercises it.

Each was confirmed by a mutation that SURVIVED, and each is written the way the hardware/ROM does it
rather than the way this sequence would allow.

---

## 10. Open

- The other 15 sequence-table entries — now MAPPED rather than hand-waved: `sequence_table.md` has
  the table's real extent, an instruction-aligned census of every `FF` site in all 512 scenes, the
  attract chain measured frame by frame (`02 → 61 → 60 → 61 → 63 → 02`), what each reachable stage
  puts on screen, and the finding that prices the rest: ⭐ the ENGINE in `src/title_intro.c` is what
  they all run on, and what each entry adds is its own setup, tasks and graphics. Entry 1 (the staff
  credits) proves it — same `$0920` task list, no script and no vblank hooks at all.
- ~~`$7E:34AB`'s fade into the demo~~ — PORTED (§6.6), and with it the button handling: the host no
  longer decides the exit, it hands the machine the pad and reads what `$5B99` decided.
- ~~The SOUND the exit asks for~~ — PORTED (§6.6), and with it the setup's own song load, so the
  whole mailbox matches the oracle byte for byte (`30 C0 06` through the sequence, `81 40 00 06` at
  the exit). What is still NOT modelled is `$375F`'s `$0389/$038A` shadows and the setup's
  `$2143 & $0F` handshake spin; nothing in this sequence reads either.
- The sound commands the script itself issues (`..6`) are parsed and skipped, for the same reason.

---

## 11. ⭐ THE SONG COMES 43 FRAMES BEFORE THE ANIMATION (the setup hold)

Reported as "the sound in the intro is out of sync with the picture", and it was. The port issued
`$7E:5AA5`'s song and started ticking the machine in the same instant. The ROM does not:

| boot | what happens |
|---|---|
| f70 | `$0384 = 2` — the sequence is requested |
| f98 | ⭐ the song lands (`$7E:0389` goes `00` -> `$30` between f97 and f98) |
| f99 | the task list is already up (`$0920 = 3749 5BCF`) — but `$7E:0008` is **0** |
| f99..f139 | ⚠ **the machine does not tick and the screen is BLACK** |
| f139/140 | the first NMI tick (`$0008` 0 -> 1) |
| f141 | the frame the port calls swing frame 0 |

`$7E:5978` keeps working after it loads the song, with the NMI still disabled (`$7E:3017 LDA #$A1 /
STA $4200` comes after `$300D JSR $388B`, `$3010 JSR $31B0` and `$3013 JSL $C3:08D0`). The port jumps
straight to the seed state, so all 43 of those frames were collapsed and the music ran that far ahead
of the picture it belongs to. `CT_TITLE_SETUP_HOLD` restores them: the song, then 43 black frames,
then the first tick.

⚠ **This re-timed four boot checks, for the second time and for the same reason.** They press START
at a fixed boot frame, and the press only counts once `$7E:5B99` is in the task list — measured at
MACHINE frame 48 (`--monitor 7E0920:12 --from build/_ti/f140.state --diff` shows it appear at f48).
At boot frame 90 the press used to land at machine frame ~65; with the hold it landed at ~22 and the
ROM ignores it, exactly the trap that bit these checks the first time. All four moved +43, with the
cursor-press spacing unchanged.

---

## 12. ⭐ THE DRIVER RESET `$7E:3177`, and the title that came back SILENT (2026-09-19)

Reported as "when the intro repeats, the pendulum has no sound". Two defects, one inside the other.

**The silence.** `CtTitleIntroResetStage` (a native entry's opening, the credits' `FF 61`) cleared the
whole machine with a `memset`, and the machine carries the HOST's mailbox attachment (`t->snd`). The
attract's first credits (f2680) wiped it, and every title after that posted nothing: no song `$30`
(whose ticks ARE the pendulum's sound) and no exit fade. `CtTitleIntroReset` already kept the
attachment for this reason; the stage reset now does too.

**The reset the port never made.** The overlay's common init runs `JSR $3177` at the opening
(`30E9 BIT #$20 / BNE ->30F6 / STZ $038A / JSR $3177`) and the teardown runs it again before the reboot
(`302A ... JSR $3177`). `$3177`:

```
317A  BIT #$20 / BNE ->3176 (RTS)      $0384 bit 5: the credits $61, the race $60, entry 3 $63 skip it
317E  STZ $1E02
3181  LDX #$0085 / STX $1E00 / JSR $377E       85 00 00
318A  LDA #$89 / STA $1E00 / JSR $377E         89
3192  LDA #$F2 / STA $1E00 / JSR $377E         F2 00
319A  LDA #$01 / STA $1E01 / JSR $377E         F2 01   <- only $1E01 is rewritten
31A2  LDX #$FF00 / STX $1E01
31A8  LDA #$81 / STA $1E00 / JMP $377E         81 00 FF
```

`$377E` is NOT `C7/0004`: it copies `$1E03..$1E00` to `$2143..$2140`, spins on `CMP $2140` for the
echo, and never clears the mailbox. That is why the fourth command can rewrite `$1E01` alone and still
send `F2 01`. The port's post is C7/0004's (the driver's consumption zeroes `$1E00`), so
`CtTitleIntroDriverReset` writes the three bytes again before each send. What `$377E` leaves behind
(`81 00 FF`) is NOT put back after the last send: the ROM's next writer overwrites it inside the same
frame, and the field's per-frame catch-all service would otherwise send a leftover `81` twice.

ORACLE (`--hookpc 7E377E --from saves/attract_cena_inicial.state`): five visits at the second title's
opening, f9108 (A = `02/89/F2/01/81`, X = `0085 .. FF00`), and five at its exit, f11155. The boot's
title now posts 13 commands: the five resets, the song, the exit's `81 40` and `F0 40`, the five resets.

Output, the attract's second title, cartridge `PEEK_PCM` against port `CT_AUDIO_PCM`: the pendulum's
ticks at cartridge f9144 9150 9156 .. 9218 and port f11068 11074 11080 .. 11142. They have the same
6-frame cadence and envelope. Checks: `title-sequence-sound-reaches-driver` (re-pinned 3 -> 13 posts)
and `attract-title-after-credits-posts-its-song`.

### 12.1 The title's opening price: the song-to-tick half PORTED (2026-09-27), the part before the post OPEN

With the sound back, the repeated title's picture and audio can be measured against each other. They
disagree. The picture is at port = cartridge +1940, identical frame for frame (thumbnails, f9150..f9635).
The audio is at +1924: **the ticks come 16 frames ahead of the swing.** Cause: `CT_TITLE_SETUP_HOLD` (43)
is the BOOT's song-to-first-tick gap, and the port posts the song on the op's own frame. The ROM's
opening is a chain of CPU work, and each stage has its own price:

| oracle | boot | the title after the credits (abort, `--btnscript 0:900,100:10`) |
|---|---|---|
| `$0384 = 02` (C3/0038) | f70 | f935 |
| `JML $7E:3000` (after C3/0557's decompression) | — | f948 |
| the five `$377E` resets | — | f948 |
| `$5AA5` the song's `JSL $C7:0004` | f98 | f964 |
| the upload ends, `$5AAA` spins on `$2143 & $0F` | f133..f139 | f979..f985 |
| the first NMI tick | f139/140 | ~f986 |

The song-to-tick gap is the upload's price plus the spin's price: 35 + 6 on the boot (a cold driver) and
15 + 6 after the credits. The part before the post, ~28 frames, is the same both times. The port has the
clocks for all of it: the loader clock prices a post (`CtAudioLastPostRaster`), the spin is
`CtAudioWaitReplyNibble`, and the decompression is `ct_lz_rom_cycles`, which is how a native entry's
opening is already priced. Porting it re-times the title that opens `--scene 433` too, and with it the
frame number of every attract check. Separately, the loc 1B1 visit before the second title is 11 frames
short (the cartridge's visit reaches `$0384 = 02` 206 frames after loading, the port's reaches its op after 195).

**2026-09-27, the song-to-tick half.** The user heard it again after the demo ("after the demo, when the pendulum repeats, the
sound is still out of sync"): the attract's second title, cartridge post c9123, spin c9138-9144, first tick c9144,
picture c9147; the port's tick 22 frames before its picture. The hold after the post is now the setup's own two waits,
priced by the port's clocks (src/field.c, the title's opening): the song post's NMIs on the loader clock
(`CtAudioLastPostRaster`, kept by `titleSoundPost` as `g_titlePostFrames`) plus the spin's (`CtAudioWaitReplyNibble`,
`g_titleSpinFrames`). Measured: 37 + 6 on the power-on title (the boot's 43, so that title and every attract frame
after it are where they were) and 16 + 6 after the demo (the cartridge 15 + 6). Charged under lockstep or a paced host,
`fieldBattleSndPost`'s gate; an unpaced run keeps the constant. The first tick against the first lit frame: port -2 on
both titles, cartridge -3 after the demo (tools/attract_title_sound_sync_cmp.py, check
`attract-title-tick-with-its-pendulum-vs-cartridge`).

The part BEFORE the post (the overlay's decompression, the driver reset, the setup's work: ~28 frames on the boot, 33
after the demo) is still not priced: the port posts on the op's own frame, so every title opens that much short of the
ROM. Pricing it re-times the power-on title and every attract check after it.
