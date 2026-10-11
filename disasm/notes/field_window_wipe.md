# The field's WINDOW WIPE -- op 0xFF 0x92-0x95, C0/21E1 -> C0/22D8 -> C3/01E4

Ported 2026-09-14 (flow and clock); 2026-09-16 the routine itself, translated, and the window drawn (section 5; the seed's H counter is the one input left, section 4). Code: `src/ct_vm.c` ctWipeOp / ctWipeFrame;
the C0/2848 `$011E` skip in `src/field.c`. Check: `attract-window-wipe-flow-vs-oracle`. Trace: `CT_WIPETRACE=1`.

## 1. The op (C0/3FAF's `>= $90` half, table C0/400E)

`FF 92` = C0/405D, `FF 93` = C0/40EF, `FF 94` = C0/40B3, `FF 95` = C0/4145. C0/4009 `JSR ($400E,X)` then `400C CLC / RTS`:
every visit ENDS on this op, and a handler that does not `INX INX` leaves the ip on it (a park).

```
state $0139 (DP $0100 -> $39) = 0:
    LDA $0350 / BEQ ->arm / TYX / RTS                 another wipe still running: park
arm: STZ $0351 / LDA #mode / STA $0350                92, 94: mode 1     93, 95: mode 2
     LDX #17C8 / STX $0352 / LDA #7F / STA $0354      the HDMA table pair at $7F:17C8
     LDA #03 / STA $39 / LDA #kind / STA $013C        92: 0   93: 1   94: 3   95: 2
     TYX / RTS                                        park
state $0139 != 0:
    CMP #03 / BNE park / LDA $0350 / BEQ ->done / park
done 92/93: STZ $39 / $7F1522 = $7F1523 = 0 / $0BD9 = $0BDA = $0BDB = $0BDD = 0 / LDA #80 / TRB $28 / INX INX
done 94/95: STZ $19 / STZ $39 / INX INX               ...and the screen is BLACK ($0119 = 0)
```

## 2. The frame (C0/00B1 `JSR C0/21E1`, after the box machine C0/00AD and before the object pass C0/00B7)

C0/21E1: `LDA $39 / BEQ / DEC A / ASL / TAX / JSR ($21EE,X)` -- entry 3 is C0/22D8 (1 and 2 are the iris).

```
22D8 LDA $0350 / BEQ ->2307
     $7F1522 = $7F1523 = 17 / $0BD9 = $0BDA = $0BDB = 88 / STZ $0BDC / $0BDD = FF      the window shadows
     JSL C3/000E  (= JMP C3/01E4, DP $0300)
     LDA #80 / TSB $28 / LDA #0F / STA $19 / RTS                                       full brightness under the window
2307 LDA $3C / CMP #02 / BCS ->RTS / (clear the shadows, TRB $28 #80)                  only for kinds 0/1 (92/93)
```

C3/01E4 per call: `$50` 0 -> nothing; `$51` 0 -> fill the working table (`[$54:$52 + $1C0]`, $E0 bytes) with 00 (mode 1) or
FF (mode 2); then `INC $51`; at `$51 = $20` it clears `$51` and `$50` -- the thirty-second call ends the wipe -- else it runs
the step (section 4) into the table `$52` (odd step) or `$52 + $E0` (even step) through WMADD/WMDATA ($2181/$2180).

## 3. Measured

`build/ct.exe --monitor 7E0119:1,7E0350:2,7E0139:1,7E011E:1 --from saves/attract_cena_inicial.state --diff --writer`:

| loc | op | armed | C0:2306 `$19 = 0F` + C3:0237 `$0351` | C3:023F clears `$0350` | the visit completes |
|---|---|---|---|---|---|
| 16E | `FF 94` | f578 | f579..f609 (01..1F) | f610 | f610: `$0119 = 00` (C0:40C4), `$0139 = 00` (C0:40C6) |
| 01C | `FF 93` | f769 (C0:412F / C0:413E) | f770..f800 | f801 | f801: `$0139 = 00` (C0:4100), `$0119` stays 0F |

All the attract's field wipes (`$0350` != 0 windows): 16E f578, 01C f769, 0AD f2276, 112 f2477, 0A5 f3503, 07D f5256,
0E6 f5495, 087 f7128 -- each 32 frames. The overworld's (1F5 f105, 1F7 f3283, 1F2 f6106, 1F0 f8850) are its own window
objects (op 4E -> $C6:E241), not these.

Port (CT_WIPETRACE, --scene 433 --selftest): 16E armed t2320, complete t2352 with $0119 = 00; 01C armed t2462, complete
t2494 with $0119 = 0F. Loc 16E's leg is now the cartridge's to the frame (275/275); 01C 460/486 (was 428), 0A5 897/925 (857),
0E6 476/499 (432).

**C0/2848's skip is taken now.** `JSR C0/286C / BNE ->2866`: a location entered by op 0xDE/0xDF (`$011E = 1`) runs no
brightness ramp. Before the wipe existed, taking it turned 01C black (01C is lit by its `FF 93`); now 01C stays black until
its wipe, as the cartridge's does, and 0AD (01B's `DF`) is lit by its own `F0 F4` -- which moves the demo battle to +320
frames from the 0AD entry against the cartridge's +331 (it was +252).

## 4. The per-line step and its one underived input

```
0242 $56/$57 = FE/C0  /  JSL C3/02DD  /  STA $55          the seed
0280 each of 224 lines: when the run counter Y expires,
        LDA [$55] / STA $55 / AND #1F / ADC #03 / TAY        next run length, 3..34 lines
        LDA [$55] / STA $55 / AND #03 / ADC #08 / STA $F2    its increment, 8..11
     value += $F2 (mode 1, saturating at FF) / -= $F2 (mode 2, floor 0); written to the table and the working copy
C3/02DD ROL / ADC $0386 / STA $0386 / TXA / ADC $0386 / ADC $0008 / CMP $2137 / ADC $213C / SBC $213D / ADC $0386 /
        ADC $00F0 / EOR $00F2 / ADC $00F4 / SBC $213D / STA $0386 / RTL
```

The seed folds in `$2137` (latching the counters), `$213C` (OPHCT) and `$213D` read twice (OPVCT, both bytes): the H and V
counters at the instant C3/02DD runs -- the CPU's position in the frame after that frame's NMI and main-loop head. MEASURED
(`--hookpc C302DD`, hPos added to the hook): V is constant over a wipe's 32 calls (255 on 16E, 253 on 01C); H is NOT --
298..396 master cycles into the line on 16E, 1100..1284 on 01C, frame to frame -- because the field NMI's uploads and the
main loop's head do different work each frame. The port has no instruction-level clock for the field engine, so that H is
the ONE input of the wipe it cannot derive. User decision 2026-09-16: translate the routine anyway (section 5) and start
every call on the cartridge's first-call clock (seq_xlat.h CT_XLAT_WIPE_V/H), as the race does with its handoff: the
walk's law, the fill, the saturation, the 32 calls and `$0386` are exact by construction; WHICH of the 256 walks through
$C0:FE00 a given call takes is the cartridge's own on the first call and a cartridge-possible one on the others [WRONG on both counts, 2026-09-18: the first-call beam is 16E's only -- 01C's first call reads at v=261 h=1240 -- and with a fixed beam `$0386` falls onto the fixed point 7E within a few calls, so every later call takes one walk; section "The curtain turns SQUARE"]. Deriving
H is the raster-clock item. The 2300 A.D. haze (C3/01E4 through op 4E -> $C6:E27E) is the same routine and the same wall.

## 5. The routine translated, the window drawn (2026-09-16)

**The routine.** `tools/gen/xlat.py --name wipe_c3 --from C3:000E --stop C0:22FE` over `--cputrace` (CPUTRACE_UNIQUE=1) of both
wipes -> `src/xlat/wipe_c3.c`: 150 instructions (140 executed, 10 static), C3/000E -> C3/01E4 .. C3/0306 and the exit tail
C3/01BD. (The generator now re-arms `--from` after a `--stop`, so a routine called once a frame is collected from every
call.) Lockstepped against the cartridge, `XLATCMP_AT=C3000E XLATCMP_PROG=wipe ct.exe --xlatcmp <state> 6`: IDENTICAL on
both wipes -- registers, flags, clock and beam, every instruction to the RTL.

**The call** (seq_xlat.c CtXlatWipeCall, from ct_vm.c ctWipeFrame in place of the fill/count model, which survives only
as the ROM-less harness fallback): one cached vendored machine; the host's 128 KB copied in and back; the registers
C0/22FA leaves -- A = 00FF (`LDA #FF / STA $0BDD`), X = 4 (C0/21EA `DEC A / ASL A / TAX` of state 3), D = $0100, B = 0,
M = 1, X = 0, I = 0, S = $06F8 with `C0 22 FD` pushed (C0/00B1 JSR, C0/21EA JSR, C0/22FA JSL); every flag the routine
reads it sets first (PHP; `REP #21`; CMP before ROL). The counter latch as the field leaves it before EVERY call: C0/5A46
and C0/CA82 read $213F (both toggles cleared), then $2137 and $213D twice -- `--wipedump` shows latched=1 hSecond=0
vSecond=0 at each call. The beam: CT_XLAT_WIPE_V/H = 254 / 1240 at C3/000E (16E f579, the attract's first wipe from
power-on; C3/02DD's reads then fall on v=255, h=306). Y is a main-loop leftover (001F on both wipes), only pushed and
popped: the port passes 0, and the two stack bytes of the PHY image are the only bytes of the 128 KB that differ.

**The channel** (field.c fieldWipeWindow / fieldWipeMask). FD/C1EE (the NMI's C0/EC0F) programs channel 7 as DMAP $40
(indirect, one byte a line), B-bus $2128 (WH2, window 2 left) or $2129 (WH3, right) by `$013C` bit 0 (FD/C233), from the
table SET `$0153 & 0F` names: clear -> set A ($0F80..$11E1), set -> set B ($1238..$1499). The sets are built by C0/00C7
`JSL FD/C2C1` every main-loop frame AFTER the wipe call: bit clear -> set B through ($C2E5,X) -> FD/C477, then TSB; bit
set -> set A through ($C2DF,X) -> FD/C439, then TRB -- $0153 alternates 00/01 every frame (measured, `--read --settle`).
Each builder writes the channel's entries `E4 <T> F0 <T+64> 00` (100 lines then 112, then the channel ends and the
register holds) with OPPOSITE parity rules: FD/C439 (set A) `$0351` even -> $18A8, odd -> $17C8; FD/C477 (set B) even ->
$17C8, odd -> $18A8. C3/025F-0269 writes $17C8 on odd calls. So the frame after call k draws from: the set the NMI named
with the PRE-flip bit, built one main loop earlier with `$0351` = k-1. MEASURED first (`--wipedump`'s per-line window 2):
16E call 3 (bit = 0 at the call) draws call 2's $18A8; 01C call 5 (bit = 1) draws its own $17C8. The port reads $0153
after its own C0/00C7 (CtSceneTick flips it), so `bit set -> set A`, and `set A == $0351 odd -> $18A8, else $17C8`.
The mask: W12SEL/W34SEL/WOBJSEL = $88 (C0/22E7: every layer in window 2) with TMW = TSW = $7F:1522/$1523 = $17 (C0/22DD,
the TM/TS/TMW/TSW HDMA data) -- inside [l, r] the main screen is the backdrop, CGRAM 0; the other edge is $0BDC / $0BDD.
Applied before the INIDISP brightness, as the PPU does.

**Graded** (tools/wipe_step_cmp.py, check field-window-wipe-routine-and-mask-vs-oracle): `ct.exe --wipedump` hands the
cartridge's own WRAM and beam at a call to `ct_field --wipecall`; the two tables + working copy (480 bytes), DP $0300's
page and `$0386` are identical on 16E's third call (mode 1) and 01C's fifth (mode 2); `--wipemask` on the WRAM after
C0/00C7 agrees with the cartridge's window 2 on every sampled line (189 / 189, 188 / 188). Five mutations killed (X, the
$213C toggle, the return address, the buffer choice, the 212-line table end); the flow check pins $0153 and kills the
flip's removal. NOT graded, by design: the attract's own wipe picture (section 4).

## 6. The overworld's window object -- the same routine, op 4E (2026-09-16)

The attract's world-5 flyover (loc 1F5 leg, sequence_table.md 9.16) opens with the cartridge's black left-edge wipe: the
object script runs `4E 41 E2 C6` once and `4E 7E E2 C6` a frame -- C2/181C's far call (`LDY #0001 / LDA [$58],Y / STA $5B
/ SEP #20 / LDY #0003 / LDA [$58],Y / STA $5D / JSL C2/1836 = JML [$005B]`, X = op << 1 = $9C from the dispatcher) to:

```
C6/E241  LDA #F0 / STA $7E8A42 / STA $7E8A45 / TDC / STA $7E8A48      channel 2's table: F0 <p1> F0 <p2> 00
         LDY #2740 / STY $4320  LDY #8A42 / STY $4322  LDA #7E / STA $4324 / STA $4327    DMAP $40 -> $2127, bank $7E
         LDA #04 / TSB $4A                                             the HDMAEN shadow (pushed at the next NMI)
         LDX $4E / LDA $0022,X / STA $0350 / STZ $0351 / LDY $0024,X / STY $0352 / LDA #7E / STA $0354
C6/E27E  JSL C3000E                                                    = C3/01E4 on the object's mode and table
         LDX $4E / LDA $0351 / LSR / REP #20 / BCC +E0: $7E8A25 = $0024,X (+$00E0) ; $7E8A27 = that + $0070 ; RTL
```

Translated with the same generator (`--from C6:E241 --from C6:E27E --stop C2:1830`, tools/gen/xlat.py now takes several
entries) into src/xlat/owwin_c6.c, 191 instructions; `XLATCMP_PROG=owwin` lockstep from `XLATCMP_AT=C6E241`: IDENTICAL.
Called from world.c's op 4E through a host hook (field.c owWindowCallHook -> seq_xlat.c CtXlatOwWindowCall) with what
C2/181C leaves: A = the target word's high byte over its bank (E2C6), X = $9C, Y = 3, S = $06E7 with `C2 18 2F` pushed,
D = 0, B = 0, I = 1, N/Z from the bank byte's LDA. MEASURED with `ct.exe --calldump <state> <k> <entry> C21830 in out`
(the generalised --wipedump: the registers, the beam, the latch toggles and PPU2's open bus at the call, the WRAM
before and after, window 1/2 on the visible lines that follow):

* the beam: line 45 at the install, 0 / 1 / 261 / 258 ... at the per-frame calls -- the world VM's position in the
  frame, the same underived input as the field's; every call starts on the first call's (CT_XLAT_OWWIN_V/H = 45 / 768);
* TWO INPUTS THE FIELD'S WIPE DID NOT HAVE. Nothing in the world engine reads $213C/$213D/$213F (the leg's --cputrace:
  C3/02DD's reads are the only ones), so the $213C toggle C3/02DD flips is STILL FLIPPED at the next call (hSecond 0, 1,
  0, 1, 0 over the calls) and the second $213D read -- the high-bit form, `bit0 | (ppu2openBus & FE)` -- carries PPU2's
  open bus, the last byte the previous call read. The port's cached hardware carries both between calls (the install
  resets the toggle: the first call after the world's load); the harness feeds the measured ones (`--owwincall ... hSecond
  bus2`). Without the open bus the per-frame call's seed is 30 where the cartridge's is 14.

**The channel** (field.c owWin1Line / owWindow1Pass; world.c CtWorldAmbientFrame). Channel 2 is indirect: line i reads
p1[i-1] for i <= 112, p2[i-113] after, then holds; p1/p2 are the entries at $8A43/$8A46, which the NMI's C2/02C2 copies
from $8A25/$8A27 every frame (`LDA $7E8A25 / STA $7E8A43 / LDA $7E8A27 / STA $7E8A46`, 16-bit) -- so a frame draws the
half the PREVIOUS call wrote. The port copies them at the start of its object pass and there latches HDMAEN / WH0 / WH1
as the NMI pushes them ($4A, $3B, $3C; C2/025F-0266 and the HDMAEN write): a change the pass makes reaches the registers
at the next NMI. Measured on the install's own frame: W1 = [1, 0] -- WH0 from a $3B the script rewrote to 0 just before
the call, WH1 = 0 with the channel not yet running -- an empty window, which is what the latch reproduces. The leg's
enables: W12SEL 22 / W34SEL 22 / WOBJSEL 02 (every layer in window 1), TMW 13 (BG1, BG2, OBJ masked on the main screen,
BG3 not) and TM 13 (BG3 off the main screen anyway): inside [WH0, right] the main screen is the backdrop, CGRAM 0 -- the
pass keeps the sub screen (the picture before the clouds' math) for the BG3 case the PPU would math, and names any other
set of enables once.

**Graded** (tools/owwin_step_cmp.py, check ow-window-object-routine-and-mask-vs-oracle): the whole 128 KB after the
install and after the first per-frame call -- 0 bytes differ -- and window 1's edges on every sampled line (128 / 128,
180 / 180). Five mutations killed (X, the flags, the open bus, the 112-line entries, the empty window). The world-5
wipe draws in the attract now: the black left region with the jagged edge receding over the 32 calls (port f2170-2203).

## The wipe's FIRST frame: the port blinks, the cartridge does not (2026-09-17, OPEN)

User item, with a savestate at the moment (`build/ct_debug.state`, loc 01C, `$0119 = 00`, `$011E = 01`, no wipe
armed yet): *"the screen blinks before the curtain"*. The port presents **one fully lit, unmasked frame** just
before the curtain starts opening, and the cartridge never does.

MEASURED, loc 01C's `FF 93` (mode 2), `CT_SHOTS_LUMA` against `--peek`'s own frames:

| | arm | +1 | +2 | +3 |
|---|---|---|---|---|
| cartridge | f769 mean 0 | f770 mean **0** | f771 mean **0** | f772 mean **4.6** |
| port | f2836 mean 0 | f2837 mean **46** | f2838 mean 4 | f2839 mean 4 |

46 is the whole scene. 4.6 is the wipe's first slit, and from there both sides open at the same rate.

What the two sides hold on that frame:

* **The port** (`CT_WIPETRACE`'s `wipemask:` line): `$7F1522 = 17`, `$0119 = 0F`, `$0351 = 01`, `$0153 = 01`,
  `$7F:17C8 = F3,F4,F3` and `$7F:18A8 = 00,00,00`. `fieldWipeWindow`'s `setA == odd` picks **$18A8**, which this
  call has not written, so the window is `l = 0, r = 0` -- nothing masked -- over a screen already at 0F.
* **The cartridge** (`ct.exe --wipedump build/_pal/w768.state <k>`): after call 1 the sampled window is
  `W2 = 0..0` on **every** visible line and **HDMA channel 7 is DISABLED** (`regs now -- W1=0..0 W2=0..0 hdma
  ch2=1 ch7=0`); after call 2 it is `W2 = 0..235`, after call 3 `0..226`, after call 4 `0..216`. So the cartridge's
  window is empty on that frame too. Its frame is black because **the brightness has not reached the picture**:
  `$0119 = 0F` is written at f770 by C0:2306 and the first lit frame is f772.

**THE FIX.** The composite shows the brightness ONE PASS LATE (`brShown`, src/field.c): the NMI pushes INIDISP
from `$0119` before the main loop runs, so what a main-loop leg writes this frame is what the NEXT frame is drawn
with -- the same rule the overworld composite already carries for its camera, OAM and palette. The port then reads
**0, 0, 0, 4** across those frames, the cartridge's own shape.

It was almost thrown away: `attract-fade-rate-01B-vs-oracle` dropped from 50425 / 53248 to 0 / 53248 and looked
like a regression. It is not -- the port's picture only arrives a frame later, and at **port f3422** the same
oracle f1384 gives back **exactly 50425 / 53248**. ⚠ A frame-anchored oracle pin cannot tell "wrong" from
"shifted": search the neighbouring frames before believing it. `field-gameover-reboots-to-title` shifted the same
way, with its reset (f1015) and its loc 1B1 (f1080) untouched and only two luma samples inside the ramp moving.

GRADED: `field-wipe-first-frame-not-lit` pins the four `shotluma` frames of loc 01C's wipe in the selftest
(f2805..f2808 = 0, 0, 0, 4), one mutation (take the brightness live).

## ...and the SECOND blink, after the curtain shuts (2026-09-17)

User item, second savestate (`build/ct_debug.state`, loc 16E, `$0350 = 01`, `$0351 = 0C`, `$013C = 03` -- mid-close):
*"after the curtain closes, there is one more blink"*. One frame of the whole scene at full brightness right after the
curtain finished closing -- `shotluma` **mean 168** at the attract's selftest f2647, against 0 on every frame around it.

**The ROM decides which wipes leave the window standing.** The window registers keep the last value HDMA channel 7
gave them once the channel stops, and TMW (`$7F:1522`) is what says whether the layers are windowed at all:

* the **opening** wipes tear it down themselves -- `C0/406C` (op 92) and `C0/40FE` (93) are `STZ $39` then
  `$7F1522 = $7F1523 = 0` and `$0BD9/DA/DB/DD = 0`; `C0/2307` does the same from C0/22D8's else arm, but only for
  `$013C < 2` (`2307 LDA $3C / CMP #02 / BCS ->2329`);
* the **closing** ones (94/95, `$013C` 3 and 2) do not. Their completion writes `$19 = 0` and `$0139 = 0` and leaves
  TMW at `$17` with the edge fully closed. That is why the screen stays black after the curtain shuts.

`fieldWipeWindow` gated on `$0350` and `$0139 == 3`, so the port threw the window away on the completion frame; with
the brightness one pass behind (`brShown`, above) that frame drew the whole scene. The gate is **TMW alone** now.

MEASURED, loc 16E's `FF 94`:

```
cartridge  f596 71.5  f598 60.2  f600 47.3  f602 33.8  f604 20.6  f606 6.6  f608 0.1  f610 0  f612 0 ...
port before      ...  f2639 20   f2641 7    f2643 0    f2645 0    f2647 168  f2648 0
port after       ...  f2639 20   f2641 7    f2643 0    f2645 0    f2647 0    f2648 0
```

GRADED: `field-wipe-close-leaves-the-window-standing` pins the close's tail (f2641..f2648 = 7, 7, 0, 0, 0, 0, 0, 0),
one mutation (put the `$0350` / `$0139 == 3` gate back). Suite 550/550; no other check moved.

## The curtain turns SQUARE: the constant beam has a fixed point (2026-09-18, OPEN -- the H input of section 4)

User item, third savestate (`build/ct_debug.state`, loc 01C -- Crono and the Dragon Tank on the bridge -- `$0350 = 02`,
`$0351 = 0D`, `$013C = 01`, `$0386 = 7E`): *"the curtain is turning square"*. It is.

**The picture.** The cartridge's edge moves on EVERY line (loc 01C, cartridge f785, the left edge per line: 95 94 93 94 93
91 92 93 92 95 96 99 100 98 95 96 98 95 ... -- runs of 1 to 9 lines, a few pixels apart). The port's edge is the runs of
ONE walk (port f2821: 127x20 128x8 115x14 89x3 88x6 89x9 102x17 115x7 117x25 130x11 ...): rectangles 3 to 34 lines tall,
up to 40 px apart.

**Why.** A line's value is the sum of the increments every call gave the run that held it (section 4). When the calls take
DIFFERENT walks the run boundaries differ call to call and the sums break up line by line; when every call takes the SAME
walk, each run keeps one value and the edge is that walk's blocks. The walk is picked by `$55` = C3/02DD's result, and
C3/02DD's only memory is `$0386` (only C3:0306 writes it -- `--monitor 7E0386:1 --writer` over the whole wipe). With the
beam fixed (every call starts on CT_XLAT_WIPE_V/H, section 5) the call is a map `$0386 -> g($0386)`, and g has an
ATTRACTOR at 7E: the port's `$0386` per call (CT_WIPETRACE's `wipemask:` line now prints it; the port rows start with
the value before the first call, the cartridge row with the value after it)

```
16E  4B 92 A6 CE 1F BF 00 81 84 8A 96 AE DE 3F FF 80 82 86 8E 9E BE FE 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E    (7E from call 22)
01C  7E FE 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E 7E    (7E from call 2)
cart 40 2C 0C C3 37 1F E9 84 B9 EF 8C C2 3A 21 F2 93 D4 55 56 56 5D 62 7A A3 F3 90 D4 55 59 60 74       (01C, f770-f800)
```

(`--wipecall` on the F5 state's WRAM confirms it: at the fixed beam 7E maps to 7E and the step is the same run list every
call; the same WRAM at h = 1100 / 1150 / 1200 / 1284 takes four different walks.) Section 5's claim that the other calls
take "a cartridge-possible walk" is therefore WRONG past the first few calls: every field wipe in the port collapses onto
the single walk of `$0386 = 7E`.

**The cartridge's beam at C3/02DD** (`--hookpc C302DD`, loc 01C): the first call at v = 261 h = 1240, then v = 253 on
every call with h = 1118 1150 1116 1138 1138 1116 1142 1138 1284 1124 1108 1158 1122 1138 1130 1124 1122 1116 1108 1138
1100 1158 1130 1128 1108 1150 1122 1128 1126 1150 (14 distinct values over 30 calls).

**What sets that h** (new: `CPUTRACE_BEAM=1` puts `v= h= c=` on every `--cputrace` line; three frames from f774):

* the NMI enters at v = 225 h = **76, 86, 72** -- the phase of the poll loop `C0/EC71 LDA $0152 / BNE` it interrupts, i.e.
  the whole main loop's cycle count that frame modulo that loop's period;
* the NMI handler C0/EA63 .. RTI C0/EC5F is ~19,100 master cycles (the DMAs at C0/7153, 4282, and C0/ECCB, 4576; FD/C1EE's
  HDMA set-up; C2/8002's pad read), then the main-loop head C0/00A1 .. C0/00AD (C0/882C's C0/93E0 block ~2,300; the
  layer streamer C0/7FA9 .. C0/813C ~12,000) -- NMI to C0/00B1 = **38212, 38206, 38194** master cycles;
* C0/00B1 to C3/02DD is 1038 on every frame.

So the h is the frame's own clock twice over: the path cost of the NMI and the head (data-dependent -- the DMA sizes, the
streamer's columns), and the poll phase, which needs the cycle count of EVERYTHING the main loop ran that frame. The port
has neither (its field engine is C with no cycle ledger). Deriving it is the field's CPU clock -- the same item as the
2300 A.D. haze, the overworld window object's beam and the battle ATB's walk-dependent rate. Until then the curtain is the
fixed-beam curtain above; no per-call h was invented to hide it.

### What a field CPU clock would have to reproduce (measured 2026-09-18, after the user chose to build it)

* **No beam reset.** 180 frames traced across the 01C load (f600-f780): no `WAI` anywhere. The only beam waits are the
  IRQ handler's hblank poll (C0/ECCC, vTimer 211: `LDA $4212 / BIT #40 / BEQ`, 54 cycles a turn, then `$2100 = 80` and
  FD/E022's forced-blank DMA band to line 224) and the main loop's NMI poll (C0/EC71 `LDA $0152 / BNE`, 44 cycles a turn).
  Both FOLD the phase to their period; neither resets it.
* **The phase is carried.** `ct.exe --hookpc C302DD` with the new `CT_HOOK_STALL=<frame>:<cycles>` (the CPU idles N master
  cycles at that frame's start): a stall in the 01C wipe's frame 8 of 2..44 cycles leaves the wipe untouched for 8 values
  (6, 12, 20, 24-30, 40) and changes that frame's call and every later one for the other 14 (five distinct outcomes). A
  stall at f601 moves the whole 01C wipe too -- through `$0386` (the 16E wipe's last calls take other walks) and through
  the wipe routine's own data-dependent cost, which is part of the next frame's work.
* **Access order, not totals.** LakeSnes runs each visible line's HDMA at the first CPU access after h = 1104
  (dma_handleDma before every read/write), aligned to 8 master cycles of the absolute clock and then to that access's
  length (6 / 8 / 12) -- snes_syncCycles. The field runs HDMA on every visible line (14.4k-18k master cycles a frame), so
  the work's end time depends on the SEQUENCE of accesses of the whole main loop (~15,000 instructions a frame on 01C, of
  which ~7,000 are the NMI poll; 2,200 distinct PCs and ~70 routines a frame, 5,865 PCs / 143 routines over 20 frames),
  not on per-routine cycle totals.
* **The port's WRAM is not the cartridge's.** At the same wipe call (port f2821 / cartridge f785, `$0351 = 0F` on both)
  29,333 of the 65,536 bytes of $7E differ (the low pages included: $0500-$06FF, $0F00, $1D00-$1FFF, $2100, $2300), so the
  ROM's own code run over the port's state would not take the cartridge's paths.

* **Not even the scene load resets it.** The same sweep (28 stall values, 2..56 cycles) BEFORE the 01C load (f700) moves the
  01C wipe for 2 of them, and INSIDE the load with the NMI off (f740) for 5. So the wipe's H on the cartridge is a function
  of every cycle the machine ran since power-on: the title, the overworld, the sequences, the loads, the field.

**Decision (2026-09-18, the user left the architecture to me after choosing to build the clock).** Every exact route --
the ROM's code as a shadow clock over the port's WRAM, the field translated, or a hand-written access ledger -- has to be
cycle-exact over the whole run from power-on, in every mode, with the port's WRAM equal to the cartridge's at every frame
(the shadow needs it to take the ROM's paths; the ledger needs the port to take them). That is the port becoming a
cycle-exact emulator, not a field clock. NOT PURSUED: the wipe keeps the fixed first-call beam and its fixed-point curtain,
documented here; nothing per call is invented. Reopen only if the port's WRAM ever matches the cartridge's across the
attract from power-on -- the shadow clock of the ROM's own code is then the route (the three instruments above measure it).


## The virtual beam (2026-09-18, the zelda3 model -- CLAUDE.md's one exception, user decision)

After the measurements above the user chose the model snesrev/zelda3 verifies itself with: Zelda 3's generator 8D:BA71
reads the H counter exactly like C3/02DD (`LDA $2137 / LDA $213C / ADC $1A / ADC $0FA1`), and zelda3 does not clock it --
`zelda_cpu_infra.c` PatchRom swaps it on the ROM it verifies against for a deterministic routine ("Better random
numbers"), and the C runs the same one (misc.c GetRandomNumber). Here the routine itself stays the ROM's; only its beam
reads become a VIRTUAL BEAM, the same on both sides (snes/snes.c g_ctVirtualBeam, answered in snes_readBBus before
ppu_read): `$213C` = `$0351` (the routine's own step, 1..31), both `$213D` reads = 00; no toggle, no PPU2 open bus.

* The port: seq_xlat.c CtXlatWipeCall and CtXlatOwWindowCall run the translation with g_ctVirtualBeam = 1. The attract's
  16E and 01C curtains now change seed every call (01C: `$0386` 45 ED DC BB 7A F8 ..., no fixed point) and the edge moves
  line by line again (01C port f2821: runs of 1..9 lines, as the cartridge's f785) -- the rectangles are gone.
* The oracle: CT_ORACLE_VIRTUAL_BEAM=1 (g_ctVirtualBeam = 2: only while the CPU is inside C3/02DD-0306). The comparison
  tools run it that way from the capture on: wipe_step_cmp 480 / 480 + 189 / 189 + 188 / 188, owwin_step_cmp 672 / 672 on
  all three calls. A translated sequence stage keeps the real counters: it runs on the real clock (the race is lockstep
  IDENTICAL to the cartridge), so there is nothing to replace.
* NOT YET the cartridge's attract curtain, and why: the seed also folds `$0008` and `$0386`, and the port's WRAM does not
  carry them as the cartridge does. On the virtual oracle `$0008` is the world engine's sprite scratch on 1F5 (C2/0D92
  `STA $08` -- the screen X of the last unit drawn, three times a frame: 69, 6A, 6B ...), which 16E inherits (51); and in
  a loaded field (01C, 112, 0A5) the host clears it with the low page (f628, no CPU writer) and the $C3 engine's NMI
  counts it (C3/090F `INC $08`, 01, 02, 03 ... one a frame). The port leaves `$0008` at 00, so its world window calls
  take other seeds, `$0386` differs by 16E (port 32 before the wipe) and every curtain after. That is WRAM exactness of
  two known mechanisms -- not the clock -- and it is the next item for this curtain.
* Checks: field-window-wipe-routine-and-mask-vs-oracle and ow-window-object-routine-and-mask-vs-oracle graded on the
  virtual beam (their $213C-toggle / open-bus mutations RETIRED: neither is read any more; a "back on the real counters"
  mutation each, killed); field-wipe-virtual-beam-walk pins the port's walk (two mutations, killed).
* (2026-09-30) THE CALL'S BEAM AND BUS ARE CARRIED FROM THE FIELD, so every virtual read upstream moves them. The world
  call's printed inputs went h 768 -> 760, bus2 04 -> C3 at a07ca0f (the drain's virtual beam: 760 / E3) and 30ccdce
  (the runner's V = 0: C3); the graded 128 KB did not move (nothing of them reaches the seed). --cputrace with
  CPUTRACE_BEAM=1 from the capture, old oracle against new: the first difference is f32 C0/CA88 `LDA $213D` = 86 (real,
  >= $6B: cut at CAD6) against 38 (virtual: the pass goes on to an empty queue); at the install the WRAM differs only in
  a stack byte ($06FB) and the call comes one instruction sooner. bus2 = the last PPU2 read before the leg (f67, line 4):
  the old real $213D pair at CA88 / CA8C (V = 4 -> 04); now those and the runner's 5A4C / 5A50 return before ppu_read, so
  it is CA82's `$213F` -- STAT78 03 | latched 40 | even frame 80 | the bus's bit 5, which the runner's real V = FE set
  until 30ccdce (E3). A TRAP this hid behind: tools/owwin_step_cmp.py kept its f100 state from its first run, and the
  main checkout's (2026-09-18) replayed the old rule's history for three days; the tool now makes it on every run. Any
  harness that caches an oracle-made state outlives the oracle that made it (wipe_step_cmp, reload_cost_cmp,
  stage_rule_cmp and naming_cmp keep the same `if not os.path.exists(st)`). Later the same day those four make theirs per
  run (wipe, reload, naming's boot drives) or per ct.exe sha1 (stage_rule: two power-on runs); all eight of their checks
  pass on the current oracle's states with their pins (docs/TESTING.md states the rule; the stage hand-offs moved 13 frames
  earlier from power-on, sequence_table.md 11.13).

## The curtain moves EVERY frame; which table the channel reads (2026-09-19)

The two HDMA sets' pointers never move: `$7F:11E1 = E4 A8 18 F0 0C 19` (set A: $18A8 then $190C) and `$7F:1499 = E4 C8
17 F0 2C 18` (set B: $17C8 then $182C), through every wipe of the attract (oracle `--monitor 7F11E1:6,7F1499:6`). The
calls write their window into $17C8 (odd $0351) and $18A8 (even), and $0153 flips every frame (FD:C2D2 / FD:C2DE), so
the channel shows a new step every frame. field.c chose `set A == odd $0351`, which is constant while both flip
together: the curtain moved every OTHER frame and each port pair matched one even cartridge frame. It now chooses by
$0153 alone. 0A5 against the cartridge: every frame pairs (98-100%), the edge advancing 8-10 px a frame on both sides;
16E's close: cartridge brightness 84.6 79.2 .. 12.6 6.1 1.6 0.1, port .. 12 6 1 0. The edge's raggedness still
differs where the cartridge frames come from the real-beam oracle (the seed's beam reads, the virtual-beam exception).

**The phase of $0153 at a wipe.** Every location load zeroes it (C0:0BD4) and C0/2848's entry frame flips it once --
`JSR C0/286C` runs before the $011E test and ends in C0/0B28 `JSL FD/C124 / JSL FD/C2C1 / JSL FD/C1EE`; the loop then
returns to C0/284D, so every later flip is the main loop's C0/00C7. Logical oracle: the first flip after each load has
$0119 = 00 (16E f354, 01C f766, 0A5 f3470, 0E6 f5445). field.c now flips on the entry frame. At the first call the
cartridge reads $0153 = 01 on the FF 92/94 wipes (16E, 0AD, 112, 0A5) and 00 on the FF 93 01C; with the entry flip the
port's FF 93 wipes (01C, 0E6) agree, and the FF 92/94 ones do not, because those legs reach their wipe op 3 main-loop
iterations LATER than the cartridge (16E: 210 against 207 iterations after the ramp; 0A5: 8 against 5 after the load)
-- an odd surplus the missing flip used to cancel. OPEN: that 3-iteration residual; until it is derived those curtains
show the neighbouring call's table, one step (one frame) off. Also measured, not modelled: on 0A5 and 0E6 the cartridge
has one frame with no flip between the entry frame and the first iteration (f3471, f5446), on 01C none.

**CLOSED 2026-09-28: the 3 iterations were the scheduler's count after the load** (frame_wiring.md, "RETRACTED
2026-09-28"; build/_divergence/audit_2026-09-28.md item 1d). The load leaves every `$1001,X` at the fill's 00 and the
first build visits every object; the port wrote the period there, so the object that runs a FF 92/94 (period 2 or 4)
armed it 3 frames late -- on the other parity. With the count the ROM's, 16E's `FF 94` meets $0153 = 01 on every visit of
its object and the calls read `$0153 = n & 1` as on the logical oracle (rule_s2700 +34..+65: the arm with FD:C2DE's 00 of
its frame standing, call 1 with 01, call 2 with 00): call 1 shows the empty $18A8 set and call 2 the first slice, 112's
and 0A5's too (the audit's lost first slice). field-wipe-virtual-beam-walk and attract-window-wipe-flow-vs-oracle pin it.

## The first frame again, and the brightness back on the frame's own $0119 (2026-09-29)

The "one pass late" brightness above (`brShown`) made the first frame right and every field fade wrong: measured against
the project's one alignment (ct-frame-alignment-p2: the field's logic P is the cartridge's thumbnail P+2, the port's frame
P), the sprites, the palette and the iris land on P+2 while every brightness ramp landed one frame later -- the Telepod's
`FF 80` fade from the cartridge's own WRAM (gate_warp_fd_family.md sec. 10) dims at P+1. The NMI uploads INIDISP with OAM
and CGRAM; the brightness has no latency of its own.

What the first frame lacked is the window's timing. The rule's attract, loc 01C's `FF 93` from black (rule_s2700 + 226):
C0/22D8's first call writes `$0119 = 0F` (C0:2306) and `$0128` bit 7 (C0:2302) at logic 227, and thumbnail 229 -- the
display after that NMI -- is lit with CALL 2's slit (leftmost lit x 232, $18A8's E8 at line 120), 230 with call 3's, 231
with call 4's (means 4.4, 6.7, 8.9, 10.8 ...). Channel 7 reads the data of the set that NMI programmed ($0153 after this
frame's C2C1 flip: clear -> set A, data $18A8; set -> set B, $17C8) LIVE, while the next iteration runs, and that
iteration's head is C0/00B1 -> C0/22D8 -> C3/01E4, before the lines are drawn: the data is the buffer after the NEXT call.
The shadows the gates read (TMW, $0BD9, the fixed edges $0BDC/$0BDD, $0128) are uploaded by the NMI: this frame's.

field.c now draws exactly that: fieldWipeMask runs the next call (C3/01E4 translated, CtXlatWipeCall, the rule's fixed
beam) on a copy of the WRAM when 22D8's first arm will run ($0139 = 3, $0350 set), reads the set the NMI after this frame
programs, and takes the gates from the live WRAM; the brightness is the frame's own `$0119`. The port's 01C opening:
f2808..f2814 = 0, 4, 6, 8, 10, 12, 15 -- the cartridge's 228..234 (0, 4.4, 6.7, 8.9, 10.8, 12.8, 15.1) on the P+2 rule,
no flash. (--wipemask keeps the old reading: it is handed a state from inside the call's iteration and prints THAT
iteration's display.)
