# Battle EFFECT PROGRAMS -- census (bank D1, runner CD/1609)

Read-only RE, 2026-09-29. Every claim cites a ROM address (disassembly: `tools/dis.py`, the scratchpad's
`fxdis.py` recursive walker) or an oracle observation (`ct_fxprog.exe` = copy of build/ct.exe, under
`CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`). Files beside this one:

| file | what |
|---|---|
| `fxprog_listing.txt` | all 148 programs decoded op by op (every reachable path), `<-- NOT PORTED` marks |
| `fxprog_optable.txt` | the full opcode table + the op-80 / op-88 sub-op tables |
| `fxcensus_actions.txt` | per action (techs 01-74, items BC-CF): config, fx, programs, non-ported ops; ranking; per program |
| `handlers_all.txt`, `subhandlers.txt`, `jsl_targets.txt`, `handlers_linear.txt` | handler disassembly |
| `fxwalk.py` / `fxcensus.py` / `fxoptable.py` | the static walker + census (the length table is `fxwalk.LEN`) |
| `fxdyn.py` / `fxcmp.py` | the dynamic check (oracle `--monitor` + `--hookpc CD16AF`, port `CT_BATTLE_TRACE`) |
| `c*k*_oracle_mon.txt`, `c*k*_oracle_hook.txt`, `c*k*_port.txt`, `c6k6_cputrace.txt` | the raw dynamic runs |

## 1. Selection and addressing law (a)

**Only the effect LOAD arms programs; nothing in the anim script names one.**

1. Config. `C1/4310` clears `$9877..$9886` and stores `$987C := FF` (`C1/4319-431B`), then `JSR (C1:7A63)[$AE92]`:
   `[0]` C1/475A = RTS, `[1]` C1/432C = physical, `[2]` C1/45A0 = tech (PC when `$AE91 < 3` -> C1/45AA, enemy -> C1/4682),
   `[3]` C1/475B = item. The 7-byte record is `+0 $9877` (anim script selector), `+1 $987A`, `+2 $987B` (the two
   sprite-sheet uploads, C1/4833 `JSL CD/0015`, C1/483A `JSL CD/002A`), `+3 $987E`, `+4 $9881`, `+5 $9884`, **`+6 $987C`
   = the effect id "fx"**: PC tech `CD/45A6 + tech*7` (C1/45BB..45E9), item `CD/5C26 + (id-BC)*7` (C1/476F..479D),
   enemy tech `CD/5526 + id*7` (..C1/46C1). The whole ROM has exactly four `STA $987C` (C1/431B, 45E9, 46C1, 479D; no
   STX/STY/STZ/long/16-bit store reaches it in code -- the one `9C 7C 98` byte match, D2/90ED, sits in a data bank) so
   **a physical attack (C1/432C PC, C1/4497 enemy) never loads an effect: `$987C` stays FF** -> no program.
2. Gate. `C1/4844 LDA $AE93 / CMP #37 / BEQ 4850 / LDA $AE94 / BNE 485B`, `C1/4850 LDA $987C / CMP #FF / BEQ 485B`,
   `C1/4857 JSL CD/0018` (the only caller in the ROM) with A = fx.
3. Load. `CD/0018 -> CD/0D28`: `STZ $5D9B`, `CD/0EBD` (effect graphics; `CD/0F4F-0F5C` copies **D1:6240[fx*4]** to
   `$CA93..$CA96` = program id of slot 0, program id of slot 1 (FF = none), palette id A, palette id B), then `CD/0D33`:
   16-bit `STZ $CA32,X / $CA52,X / $CA72,X` for X = 0..1E (the two slot records and their stacks), and for each slot
   Y = 0,1 with `$CA93,Y != FF`: `CD/15D5` (the only `JSR 15D5` in the ROM is CD/0D53).
4. Arm, `CD/15D5` (X = 0 / $10): `ptr = D1:646C[id*2]` (`CD/15DC`), `$CA37,X := D1` (the bank is fixed), `$CA38,X :=
   [D1:ptr] & 0F` (the header byte's low nibble = the per-frame divider; headers seen: 01 x145, 03 x2, 05 x1),
   `INC $CA39,X`, `$CA35,X := ptr+1` (code starts after the header), `INC $CA32,X` (active).
   Tables: D1:6240 = 139 records (fx 00-8A; it ends where D1:646C begins); D1:646C = 148 pointers (programs 00-93; the
   lowest pointer, D1/6594, ends the table). Every id 00-93 is used by some fx.
5. Start. The runner does nothing while `$5D9B == 0` (`CD/160E`). The action's anim script starts it with `34 1B`
   (`INC $5D80[$1B]` = `$5D9B`): oracle, Lightning: `$5D9B = 01` written by **C1:5938** (op 34's handler) at f825,
   100 frames after the arm (f725, `CD:1600/1606`). Script <-> program handshakes go through `$5D80[]` (script `22 xx yy`
   waits, `30/34`; program `A9`/`D0`/`D1` read it, `E5`/`D8` write it) and `$CFFF` (program `E6`, script `2D/2E`).
6. End. `FF` sleeps a slot (`CD/16A1 STZ $CA32,X`); when both are idle `CD/1648 CPY #0002 / BNE / CD/164D STZ $5D9B / CD/1650 INC $CCEA` (Lightning:
   f943 `$CA32 = 0` CD:16A4, f944 `$5D9B = 0` CD:1650, `$CCEA = 1` CD:1653).
7. The anim script's `80 nn` (C1/621B -> `JSL CD/001B` -> CD/003E -> `JSL D1/F67A`) is NOT a program: D1/F67A indexes
   D1:F686 by `$53 & F0 >> 3` = F74D F74D F74D F74D (palette-fade staging) F6B4 (shake arm) F692 (shake stop); none touches
   the slot records (they are bank-D1 routines, CD/15D5 is an RTS routine, and its only caller is CD/0D53).
8. Other load entries, not reached by a PC tech or item: CD/0D62 (X = fx, loads and `INC $5D9B` itself) from CD/0D93
   (fx 87) / CD/0DB1 (fx 84) on the battle-end path CD/03FF/040A; CD/0E05 (A = fx, waits for the previous one, loads,
   starts and waits) from CD/0075 (fx 40, the CD/0024 vector, called at CF/FE76).

## 2. The runner and the length law (b)

`CD/1609` (frame service CD/09AF, every frame): return if `$CCEA` or `!$5D9B`; `$CA92 := CD/0E9D` (0 for `$CCA2` in
{86, 7B..7F}, else 1); per slot `DEC $CA39,X / BNE` -> reload from `$CA38,X`, `JSR CD/1654`. `CD/1654`: `$40/$41 :=
$CA35,X`, `$42 := $CA37,X`, then per op `LDA [$40]` (CD/165F):

* `00-7E` **block request** -- `$CA18 := D000 + ((op + $CA3A,X) << 9)`, `$CA35,X := $40+1`, `INC $CA17` (CD/1674-168C);
  the slot's frame ENDS (CD/0607 in the NMI uploads 16 rows to the quadrant CD:05FF[`$CA16`*2]).
* `7F` **idle** -- `$CA35,X := $40+1` (CD/1667-166C); the frame ends.
* `FF` **end** -- `STZ $CA32,X` (CD/16A1).
* `80-F9` -- `CD/16A6`: `$43 := X`, `$40 += 1`, `JSR (CD:16B5)[(op << 1) & FF]`, then `CD/1698-169B $40 += 1`; the loop
  continues in the same frame (**no negative op ends a frame**; C1 only spins on the beam inside the same visit).

The dispatch table CD:16B5 has **122 entries, 80..F9**: F9's handler is CD/17A9, the first byte after the F9 entry, so
"entries" FA..FF are handler code bytes (FA=40A7 FB=A048 FC=0001 FD=20C2 FE=40B7); no program contains FA-FE.
**Length law:** a negative op spans 2 bytes (the pre- and post-INY), whatever it reads, unless its handler moves `$40`:
84 86 C3 C9 E1(D1/E70A `DEC $40`) EF = 1; 9C A9 AC B1 CB D5 D6 D8 D9 DA EA F8 F9 = 3; 8F 93 97 DF(D1/F47C) = 4;
89 8A A2 A7 = 5; BC C2 = 6; 8E A3 A4 C6 = 8; C0 = 9; A8 = 10; control: 91 CALL (word, `$CA75,X := $40+1`), 92 RETURN,
CA JUMP (word), CB conditional jump (word; 3 when not taken), D0/D1 computed jump through a word table after the index
byte, backward branches A9 D5 D6 (by operand 2) and BF F1 F6 (by operand 1): target = op address + 2 - d.
Sub-dispatches read an operand and do not move `$40`: 80 (CD:2A51, 29 subs 00-1C), 82 (CD:2DEA, 8 directions),
8C (CD:2961, 4), 8D (CD:29A2, 4), 88 (D1:FBD2 via D1/FBCE, 4); 8B dispatches on RAM (`$CAD1,X`, set by 8A/BD) through
CD:319E (6 shapes 00-05). No JSL target except D1/F47C and D1/E70A touches `$40` (walked: D1/F559 FD7E F5F5 F5CD
FB72 FB68 EFE3 EEA5 EEC4 EDCD EFD0 EBFF F9AF F108(own DP) E984 E91A E899 E8C1, CE/E000 EA7E E7CF E7C8 -> the unrolled
copy below CE/E7C7).
**Validation of the length table:** decoding the 148 programs along every path (CALL/JUMP/branch/D0-D1 targets)
gives 6136 op starts that tile D1:6594..D1:A232 **with 0 overlaps**; the only bytes no pointer reaches are
D1/7830-7831 (`01 FF`, an empty program) and D1/9BF7-9C83 (a header-01 program that decodes cleanly to its own FF).
F9 (whose word operand would overlap the next op) is used by no program; nor are 8C 8D 9E 9F B6 C7 CC CF DB F7.

## 3. Opcode table (b)

`len` = bytes including the op; `*` = control transfer. `port` = has a case in `battleFleeProgOp` (src/battle.c;
80 by sub-op {00 01 09 0A 0B 10 1B}, 88 by sub-op {00}); every other op STOPs the slot. `progs` = of the 148
programs, how many contain it; `PC acts` = of the 79 PC actions that run programs, how many reach it.

| op | handler | len | port | progs | PC acts | meaning |
|---|---|---|---|---|---|---|
| 80 | CD/2A4A | 2 | part | 128 | 79 | sub-dispatch on operand via CD/2A51 (29 subs 00-1C, see the sub table) |
| 81 | CD/2DCB | 2 | yes | 123 | 79 | $CA38,X = $CA39,X := b -- the slot's divider, reloaded now |
| 82 | CD/2DD6 | 2 | yes | 67 | 46 | step the slot's point ($CA5E/$CA60,X) by (b&1F)+1 in direction b>>5 (CD/2DEA) |
| 83 | CD/2EA2 | 2 | yes | 136 | 68 | $CA74,X := b+1, $CA72,X := address of b (loop head) |
| 84 | CD/2EB5 | 1 | yes | 136 | 68 | DEC $CA74,X; <>0: $40 := $CA72,X (back to the body) else DEY (loop tail) |
| 85 | CD/2EC8 | 2 | no | 12 | 10 | = 83 and STZ $CA3A,X (block-offset loop head) |
| 86 | CD/2EDE | 1 | no | 12 | 10 | = 84 and INC $CA3A,X per pass, STZ $CA3A,X on exit (the positive ops show block+offset) |
| 87 | CD/2687 | 2 | yes | 16 | 9 | $CA25 := b, INC $CA24 (HUD-band request) |
| 88 | CD/29DA | 2 | part | 125 | 79 | JSL D1/FBCE -> (D1:FBD2)[b]: window buffer being drawn: 00 both pairs off, 01 W1 off, 02 W2 off, 03 W1 full width |
| 89 | CD/2FBC | 5 | no | 59 | 32 | X=b0: $CAA6,X += b1, $CAA8,X += b2 (capped D3), $CAA4,X += b3 (shape centre/radius step) |
| 8A | CD/2FED | 5 | no | 67 | 35 | X=b0&1: $CAD1,X := b0>>4 (shape), $CAA6,X := b1, $CAA8,X := b2, $CAA4,X := b3 (shape set) |
| 8B | CD/2FB7 | 2 | no | 66 | 35 | X=b: if $CAA4,X&7F: CD/31AA half-chord table at $7E:B800 (WMDATA $7E:BA00), JMP (CD:319E)[$CAD1,X] = shape 0-5 (CD/33B5..33D3) into the window buffer |
| 8C | CD/2958 | 2 | no | 0 | 0 | sub-dispatch (CD:2961, 4 subs) -- no program uses it |
| 8D | CD/2999 | 2 | no | 0 | 0 | sub-dispatch (CD:29A2, 4 subs) -- no program uses it |
| 8E | CD/29E3 | 8 | yes | 100 | 54 | objslot spawn: record $0550+CD:2A42[b5&7] from b0..b6 |
| 8F | CD/253B | 4 | no | 22 | 14 | line-scroll wave: amplitude b1, rate mask b0&1F, b2 (bit7 = step per frame), table $0A00.. from CE:F48E; $CA92 := 3/2; layer flags $CC91/93/95 from b0's top bits |
| 90 | CD/2EF7 | 2 | yes | 1 | 0 | MOSAIC shadow $BE9A: low nibble := b>>4, size nibble +/- (b&0F), saturating |
| 91 | CD/2F4E | 3* | yes | 134 | 79 | CALL: $CA75,X := $40+1, $40 := word-1 (1-deep) |
| 92 | CD/2F61 | 1* | yes | 134 | 79 | RETURN: $40 := $CA75,X |
| 93 | CD/2F6E | 4 | yes | 125 | 79 | $BE8A (TS) := b0, $BE91 (CGWSEL) := b1, $BE92 (CGADSUB) := b2 & $CD46 |
| 94 | CD/2F8B | 2 | yes | 123 | 79 | W12SEL $C861 := b; top window record by bit 3 |
| 95 | CD/2FAB | 2 | yes | 122 | 79 | W34SEL $C862 := b |
| 96 | CD/2FB1 | 2 | yes | 124 | 79 | WOBJSEL $C863 := b |
| 97 | CD/30BE | 4 | no | 2 | 3 | X=slot (+1 if b2 bit1): b2 bit7 -> $CA77,X := b0 only; else $CA77,X += b1, vector CE:F28E[angle] -> line $CA9B..$CA9E, JSL CE/EA7E with b2&7F |
| 98 | CD/3092 | 2 | yes | 124 | 79 | WBGLOG $C864 := b&3F; top window record by b's bits 7/5-4 |
| 99 | CD/30B8 | 2 | yes | 124 | 79 | WOBJLOG $C865 := b |
| 9A | CD/308C | 2 | yes | 124 | 79 | CGWSEL $BE91 := b |
| 9B | CD/3083 | 2 | yes | 126 | 79 | CGADSUB $BE92 := b & $CD46 |
| 9C | CD/303B | 3 | yes | 128 | 79 | COLDATA $C866: := b0 (b1 = 0) or its 5-bit field += b1 saturating |
| 9D | CD/3032 | 2 | yes | 129 | 79 | TM $BE89 := b & $CD46 |
| 9E | CD/3029 | 2 | no | 0 | 0 | TS $BE8A := b & $CD46 (unused) |
| 9F | CD/3020 | 2 | no | 0 | 0 | TMW $BE8B := b & $CD46 (unused) |
| A0 | CD/3017 | 2 | yes | 122 | 79 | TSW $BE8C := b & $CD46 |
| A1 | CD/2952 | 2 | no | 11 | 4 | CD/3657: fill window n=b from the triangle $CAEA..$CAF4 (x0 y0 x1 y1 x2 y2) |
| A2 | CD/278E | 5 | no | 23 | 9 | X=b0: 3-D object init: $CAD4,X := b1, $CAD6,X := b2, $CADA/$CADC/$CADE,X := b3, $CAE4/6/8,X := C0 15 6A, $CAE0/$CAE2,X := 40 |
| A3 | CD/2717 | 8 | no | 7 | 3 | X=b0: $CAD4 += b1, $CAD6 += b2, $CADA/$CADC/$CADE += b3, $CAE0 += b4, $CAE2 += b5, $CAE4/6/8 += b6 (,X), then CD/28D2 |
| A4 | CD/26E0 | 8 | no | 4 | 1 | X=b0: $CAE4/6/8,X := b1..b3, $CADA/$CADC/$CADE,X := b4..b6 |
| A5 | CD/2690 | 2 | no | 12 | 7 | save 42 bytes $CAD4.. -> $CB0E + CD:26B0[b] |
| A6 | CD/26C0 | 2 | no | 12 | 7 | restore 42 bytes $CB0E + CD:26B0[b] -> $CAD4.. |
| A7 | CD/2660 | 5 | no | 14 | 6 | X=b0: $CAF6/$CAF8/$CAFA,X := b1..b3, $CAD8,X := 0, $CAFC,X := FF |
| A8 | CD/27CA | 10 | no | 4 | 3 | X=b0: deltas b1..b8 onto the object (incl. $CAD8, $CAFC), then CD/2843 (project) |
| A9 | CD/2518 | 3 | yes | 36 | 31 | branch back by b1 while $5D80[b0] == 0 |
| AA | CD/1C32 | 2 | yes | 26 | 20 | anchor $CA5A/$CA5C,X := -(bound point b) (points clamped by CD/2436) |
| AB | CD/24F3 | 2 | no | 43 | 28 | clamp points (CD/2436); ($CAA6,$CAA8) -- or ($CAA7,$CAA9) with b bit7 -- := bound point b&7F |
| AC | CD/2460 | 3 | no | 3 | 6 | CD/2460: quad of bound point b1 rotated by the 0->1 angle ($CD08, CE/E7CF) and radius $5D8E into $CD0D+b0, JSL D1/F9AF raster |
| AD | CD/21C0 | 2 | no | 4 | 0 | CD/21C0: X=b: $CAFC,X := FF, triangle midpoints from $CAEA.. (object triangle build) |
| AE | CD/2173 | 2 | no | 3 | 0 | CD/2173: draw object b&1's triangle ($CC64 := b&1) |
| AF | CD/20F3 | 2 | yes | 4 | 2 | X=b: triangle ($CAEA/$CAEE/$CAF2 ; $CAEC/$CAF0/$CAF4) := ($1D0C,$1D0D,$1D0E ; $1D23,$1D24,$1D25) |
| B0 | CD/20ED | 2 | yes | 24 | 15 | BGMODE shadow $BE99 := b |
| B1 | CD/20D8 | 3 | no | 9 | 3 | X=b0: $CAFC,X += b1 |
| B2 | CD/20D3 | 2 | yes | 15 | 19 | CE/E000: carry window b's pair of 216 lines from the shown buffer into the drawn one |
| B3 | CD/1FC8 | 2 | yes | 1 | 1 | CD/1FC8: per-line BG3 scroll table in $C15D.. (H = +-CE:F48E[$7C*4+i]*32>>8, V = +-CE:F48E[$7C+i]*15>>8 + 20 + $CA5C+$CA60; negative for an angle >= 80), two lines a pass; the operand is stored to $45 and overwritten unread (2026-09-30) |
| B4 | CD/1FA0 | 2 | no | 1 | 0 | CD/1FA0: CD/21C0, then anchor := -($CAD4/$CAD6,b) |
| B5 | CD/1F53 | 2 | no | 1 | 0 | CD/1F53: circle window from $CAA6,X / $CAA4,X (X=b) |
| B6 | CD/1F53 | 2 | no | 0 | 0 | = B5 (same handler; unused) |
| B7 | CD/1F4D | 2 | no | 21 | 16 | $CA16 := b (the BG3 upload quadrant CD:05FF[$CA16*2] of CD/0607) |
| B8 | CD/1F47 | 2 | yes | 3 | 2 | CD/3924 -> CD/3952: A1's fill with point 2 := the corner of points 0-1 ((x1,y0) if y0<y1 else (x0,y1)), no CD/3750 arm, and CD/3B10 filling lines 0..apex-1 with the start pair |
| B9 | CD/1F3C | 2 | no | 11 | 7 | $CA95 = $CA96 := b, JMP CD/1004 (effect palette reload, CGRAM 25-27/29-31 both pages) |
| BA | CD/1E91 | 2 | no | 1 | 1 | CD/1E91: spin object b's triangle (CD/1EAA/1EF7), $CAF6/$CAF8/$CAFA,X steps |
| BB | CD/1E7C | 2 | no | 3 | 0 | CD/1E7C: spin object b&1 by b<<4 (CD/1EAA) |
| BC | CD/1E45 | 6 | no | 3 | 2 | X=b0: $CAF6/$CAF8/$CAFA/$CAD8,X += b1..b4 |
| BD | CD/1E36 | 2 | no | 6 | 2 | X=b&1: $CAD1,X := b>>4 (select op 8B's shape) |
| BE | CD/1E27 | 2 | no | 2 | 2 | CD/1E27: spin object b by 20 (CD/1EAA) |
| BF | CD/1E07 | 2 | yes | 124 | 79 | branch back by b while objslot record 8..11 ($0580/$058C/$0598/$05A4) is live |
| C0 | CD/1D98 | 9 | yes | 5 | 4 | arm/clear the two scroll pairs $CCB0..$CCC0 (7 operands) |
| C1 | CD/1D93 | 2 | yes | 1 | 1 | raster wait CD/3B82 until scanline b (no state) |
| C2 | CD/1D56 | 6 | no | 2 | 0 | X=b0: $CADA/$CADC/$CADE,X += b2..b4, each kept below b1 |
| C3 | CD/1D4D | 1 | no | 6 | 6 | INC $5D8F (no operand) |
| C4 | CD/1C7B | 2 | no | 2 | 2 | CD/1C7B: anchor wobble ($CD28 every 4 frames, table CD:1C5B[b] or CD/1CD5), negated on odd $7C |
| C5 | CD/1C63 | 2 | no | 1 | 1 | CD/1C63: anchor := -(bound point 0), then the C4 wobble path |
| C6 | CD/1BCE | 8 | no | 2 | 0 | X=b0: $CAE4/6/8,X += b1..b3; $CAF6/8/A,X := trig(...) + b4..b6 |
| C7 | CD/1BAB | 2 | no | 0 | 0 | X=b: $CAD6,X -= 6F - $1D26 (unused) |
| C8 | CD/1B9F | 2 | no | 1 | 0 | slot point Y += 6F - $1D26 (CD/2E13) |
| C9 | CD/1B78 | 1 | no | 7 | 5 | EOR #08 on $993C..$9946 and $9F46, INC $A101 (no operand) |
| CA | CD/1B6D | 3* | yes | 46 | 20 | JUMP to word |
| CB | CD/1B5E | 3 | yes | 30 | 23 | JUMP to word when ($99D2 & 7F) == 1, else skip it |
| CC | CD/1B1F | 2 | no | 0 | 0 | CD/1B1F: clamp points, $CD08 := angle (CE/E7CF) (unused) |
| CD | CD/1AEB | 2 | yes | 2 | 1 | interpolation arm: step $FFFF/b, latch $CA60,X |
| CE | CD/1AE6 | 2 | yes | 2 | 1 | D1/F559: one eased 32-bit step of $CA60,X |
| CF | CD/1AE1 | 2 | no | 0 | 0 | D1/FD7E: window pairs of the shown buffer mirrored into the drawn one (unused) |
| D0 | CD/1ACB | var | no | 1 | 1 | computed JUMP: word[1 + 2*($5D80[b0] & 0F)] |
| D1 | CD/1ABE | var | no | 1 | 1 | computed JUMP: word[1 + 2*($5D80[b0] >> 4)] |
| D2 | CD/1AB2 | 2 | no | 8 | 11 | $CA95 = $CA96 := $5D8A, JMP CD/1004 (effect palette reload from the script's $5D8A) |
| D3 | CD/1A9C | 2 | no | 1 | 1 | X=b: $CAA4,X += $5D8C, capped at $5D8E |
| D4 | CD/1A8B | 2 | no | 1 | 1 | X=b: $CAA4,X -= $5D89, floored at 0 |
| D5 | CD/1A7A | 3 | no | 1 | 1 | X=b0: branch back by b1 while $CAA4,X != 0 |
| D6 | CD/1A66 | 3 | no | 1 | 1 | X=b0: branch back by b1 while $CAA4,X != $5D8E |
| D7 | CD/19E5 | 2 | no | 1 | 2 | CD/19E5: step the slot point by $CA7A..,X (b selects; bit6 = both axes via CD/2E13/2E4D) |
| D8 | CD/19C2 | 3 | yes | 3 | 3 | $5D80[b0] += b1, or STZ $5D80[b0&7F] with bit 7 |
| D9 | CD/19B1 | 3 | yes | 3 | 3 | quad of the bound-point pair b1 into window b0 (CD/2433 + CD/1B22 + CD/2466), D1/F9AF raster |
| DA | CD/1937 | 3 | no | 2 | 3 | CD/1937: triangle from bound points (b1) rotated by $CD08 into $12..$17, CD/367C raster into window b0 |
| DB | CD/1932 | 2 | no | 0 | 0 | D1/F5F5: copy window lines of the shown buffer into the drawn one (unused) |
| DC | CD/1923 | 2 | yes | 8 | 3 | sound: $CD24 := b, $CD25 := 0080, INC $CD23 |
| DD | CD/191E | 2 | no | 2 | 0 | D1/F5CD: = E3 (triangle := bound points 0-2) |
| DE | CD/1918 | 2 | no | 21 | 15 | $CD29 := b |
| DF | CD/1913 | 4 | no | 1 | 1 | D1/F47C: anchor := -(b0,b1 + (C0:FE00[$7C..] & b2)) (jitter) |
| E0 | CD/190D | 2 | no | 1 | 1 | $CD2D := b |
| E1 | CD/1B9A | 1 | no | 7 | 5 | D1/E70A: swap live CGRAM $2120-217F<->$21A0-21FF and $2320-237F<->$23A0-23FF, $0575 ^= 40 if $2A21 bit1, STZ $A101 (no operand) |
| E2 | CD/1907 | 2 | no | 3 | 0 | $CD2E := b |
| E3 | CD/18DF | 2 | yes | 2 | 1 | X=b: triangle := bound points 0-2 ($5DA0/$5DA2/$5DA4 ; $5DA1/$5DA3/$5DA5) |
| E4 | CD/18CF | 2 | no | 1 | 0 | sound with pan = point 0's x: $CD24 := b, $CD25 := $5DA0, INC $CD23 |
| E5 | CD/18C8 | 2 | yes | 6 | 3 | INC $5D80[b] |
| E6 | CD/18C2 | 2 | yes | 124 | 79 | $CFFF := b (the effect lock) |
| E7 | CD/187E | 2 | no | 1 | 1 | CD/187E: $CA92 := 3, $CC95 := b, $CD37 += b (FF/negatives reset), per-line table $0A00/$0A80 |
| E8 | CD/1874 | 2 | no | 1 | 1 | D1/FB72: $CD38 += b, window strip from $CD38/$CD39 |
| E9 | CD/1879 | 2 | no | 1 | 1 | D1/FB68: $CD39 += b |
| EA | CD/1864 | 3 | no | 2 | 2 | colour 0 of both pages $2000/$2200 := word |
| EB | CD/185F | 2 | no | 1 | 0 | D1/EFE3: $CD3A += b, window bars |
| EC | CD/185A | 2 | no | 1 | 0 | D1/EEA5: window bars from $CD3A, INC $CD3A |
| ED | CD/1855 | 2 | no | 1 | 0 | D1/EEC4: $CD3A += b, window bars |
| EE | CD/1850 | 2 | no | 1 | 1 | D1/EDCD: window around point 0 (b != 0: other arm), $CD3A grows |
| EF | CD/1834 | 1 | yes | 122 | 79 | INC $CD3C; wrap -> records 8-11 off (no operand) |
| F0 | CD/182A | 2 | no | 1 | 1 | X=b: $CAA4,X := $5D8F |
| F1 | CD/1813 | 2 | no | 6 | 0 | branch back by b while $CD44 != 0 |
| F2 | CD/180A | 2 | no | 6 | 0 | $CD45 := b, INC $CD44 |
| F3 | CD/184B | 2 | no | 1 | 0 | D1/EFD0: INC $CD3B, window bars |
| F4 | CD/1805 | 2 | no | 1 | 1 | D1/EBFF: sine(frame) x 28 window pattern |
| F5 | CD/17FF | 2 | no | 1 | 0 | $CD46 := b (the layer mask of 93/9B/9D-A0) |
| F6 | CD/17E5 | 2 | no | 1 | 0 | branch back by b while $CE10 == 0, then STZ $CE10 (waits for 80 19) |
| F7 | CD/17DF | 2 | no | 0 | 0 | $CE0E := b (unused) |
| F8 | CD/17BE | 3 | no | 1 | 0 | colour b1 := colour b0 on both pages ($2000/$2200) |
| F9 | CD/17A9 | 3 | no | 0 | 0 | WRAM byte at word(b1,b2) := b0 -- reads 3 operand bytes but spans 3 (overlap; unused) |
| FA-FF | -- | -- | -- | 0 | 0 | not handlers: CD:16B5 ends at F9 (FA..FE = handler code bytes); FF = end of slot (CD/1691) |

| 80.sub | handler | port | progs | PC acts | meaning |
|---|---|---|---|---|---|
| 80.00 | CD/2D4A | yes | 4 | 4 | per-line COLDATA ramp |
| 80.01 | CD/2D1F | yes | 5 | 4 | rotate the ramp |
| 80.02 | CD/2CF5 | no | 2 | 0 | triangle := (80,00)(80,D4)(80,D4) |
| 80.03 | CD/2D0C | no | 2 | 0 | $CAEE += 2, $CAF2 -= 2 |
| 80.04 | CD/2CDC | yes | 4 | 1 | zero $C15D..$C4DC (window buffer A from the record before line 0, $380 bytes), STZ $CC90 (op 8F's phase) |
| 80.05 | CD/2CB3 | yes | 3 | 2 | rotate the triangle's three points |
| 80.06 | CD/2C98 | no | 1 | 0 | triangle := fixed (00,01)(00,9F)(C0,50) |
| 80.07 | CD/2C8B | no | 1 | 0 | $CAEC += 2, $CAF0 -= 2 |
| 80.08 | CD/2C7F | no | 0 | 0 | STZ $CAE4/$CAE6/$CAE8 |
| 80.09 | CD/2BFC | yes | 2 | 2 | sprite cloud $4500 -> $0710 |
| 80.0A | CD/2BD2 | yes | 3 | 2 | wobble + |
| 80.0B | CD/2BA8 | yes | 2 | 2 | wobble - |
| 80.0C | CD/2B69 | no | 2 | 0 | sine x 0C table |
| 80.0D | CD/2B64 | no | 2 | 0 | JSL D1/F108 |
| 80.0E | CD/2D44 | no | 2 | 0 | ramp, $45 = 4 without the STZ $CD04 |
| 80.0F | CD/2B5C | no | 1 | 0 | STZ $99B5[$CD00] |
| 80.10 | CD/2B45 | yes | 122 | 79 | COLDATA records := E0 |
| 80.11 | CD/2B27 | no | 1 | 0 | $99D2 OR= 80; records 8/9 dst/count from $99D3 |
| 80.12 | CD/2B12 | yes | 2 | 1 | clamp $5DA1 to 31..D4 |
| 80.13 | CD/2D39 | no | 1 | 2 | $5D9A := 1, ramp with $45 = 14 |
| 80.14 | CD/2B05 | no | 1 | 0 | $CD3A := 0060, $7C := 0040 |
| 80.15 | CD/2AEF | no | 1 | 0 | zero $7E:E500..EBA0 |
| 80.16 | CD/2AC6 | no | 1 | 0 | 3x CD/2AAF, $E500 -= 10 where $E850 != 0 |
| 80.17 | CD/2AAA | no | 1 | 0 | JSL D1/E984 |
| 80.18 | CD/2AA5 | no | 1 | 0 | JSL D1/E91A |
| 80.19 | CD/2AA1 | no | 1 | 0 | INC $CE10 (op F6's flag) |
| 80.1A | CD/2A9C | no | 1 | 0 | JSL D1/E899 |
| 80.1B | CD/2A8B | yes | 1 | 0 | INC $9FFA..$A001 (enemy hide flags) |
| 80.1C | CD/2A97 | no | 1 | 0 | JSL D1/E8C1 |

Op 88's sub-ops (D1:FBD2): 00 D1/FCF4 both window pairs `FF 00` (ported) | 01 D1/FC38 the W1 pair `FF 00` |
02 D1/FC96 the W2 pair `FF 00` | 03 D1/FBDA the W1 pair `00 FF` (full line) -- all on the buffer not shown (`$7C & 1`).
88.01 is reached by 2 PC actions (T49 T71), 88.02 by 3 (T34 T37 T3C), 88.03 by none. All four are ported (88.03 on
2026-09-30, battle.c battleFxWinPairFull) and graded through the table itself, D1/FBCE with X = sub * 2, by the
`sweepfxops-88` line of `ct --sweepfxops` (64/64 each on seeded WRAM, both `$7C` parities).
Op 8B's six shapes (CD:319E[`$CAD1,X`] = CD/33B5 33BB 33C1 33C7 33CD 33D3): the PC programs select all six
(8A/BD operand >> 4: shape 0 x7, 1 x8, 2 x9, 3 x4, 4 x16, 5 x16 sites) -- 8B is one op but six routines.

## 4. Census (c)

* **PC techs 01-74 (116):** 79 load an effect (fx != FF) and run programs; 37 have `+6 = FF` and run none: 01 02 04 07
  09 0A 0C 0F 11 13 15 19 1E 21 22 23 25 27 29 2A 2B 2C 2D 30 35 3D 40 45 51 57 5B 5E 5F 61 62 64 69.
* **Consumables BC-CF (20):** every CD/5C26 record has `+6 = FF` -> no item runs a program.
* **Physical attacks:** never (section 1.1).
* The 79 actions reach 75 distinct programs (00 01 02 03 04 06 07 09 0A 0B 0C 0F 10 11 15 16 18 19 1B 1D 1F 20 21 23 24
  25 26 2B 2C 2E 35 36 37 38 3A 3B 3C 3D 41 44 4B 4C 4D 4E 4F 50 51 52 53 54 55 56 59 5C 62 63 64 65 66 67 6A 6B 6C 6F
  75 76 77 78 79 7C 7E 83 89 8A 8B). 122 of the 148 programs CALL D1/A208 (`91 08 A2`), the common teardown
  (81 01 / 9D 13 / 88 00 / blk 17 / 88 00 / 7F / 9C E0 00 / 93 00 00 00 / 94-99 00 / 9A 00 / 9B 00 / A0 00 / EF / 7F /
  BF 04 / E6 00 / 80 10 / 7F / 92) -- every op of it is ported.
* **Fully ported today: 9 of 79** -- T03 Lightning, T06 Life, T0B Ice, T10 Life 2, T12 Hypno Wave, T28 Frog Squash,
  T3A Ice Sword, T42 X Strike, T46 Volt Bite. The other 70 contain a non-ported op on some path (the port STOPs the slot when one is reached).

Per action (`act` = tech id; programs = D1:6240 bytes 0/1; the op keys are the NON-ported ones the programs contain):

| act | name | fx | D1:6240 | programs | not ported |
|---|---|---|---|---|---|
| 03 | *Lightning | 45 | 50ff2525 | 50 | - |
| 05 | *Lightning2 | 02 | 01020404 | 01,02 | 97 C9 DE E1 |
| 06 | *Life | 1D | 20ff1010 | 20 | - |
| 08 | *Luminaire | 01 | 04ff0303 | 04 | 89 8A 8B A1 A2 A3 A4 A5 A6 BD E0 |
| 0B | *Ice | 44 | 4dff0c0b | 4D | - |
| 0D | *Haste | 56 | 62ff2828 | 62 | 89 8A 8B AB |
| 0E | *Ice 2 | 57 | 63ff0c0b | 63 | 89 8A 8B AB DE |
| 10 | *Life 2 | 35 | 3bff1010 | 3B | - |
| 12 | Hypno Wave | 07 | 09ff0505 | 09 | - |
| 14 | Napalm | 49 | 55ff2828 | 55 | 89 8A 8B AB |
| 16 | *Fire 2 | 1C | 1fff0e0e | 1F | 85 86 89 8A 8B C3 DE |
| 17 | Mega Bomb | 20 | 23ff2828 | 23 | 89 8A 8B AB |
| 18 | *Flare | 0D | 0fff2828 | 0F | 89 8A 8B DE |
| 1A | Cure Beam | 5A | 66ff0c0b | 66 | B7 D2 DA |
| 1B | Laser Spin | 09 | 0bff2929 | 0B | AC D2 |
| 1C | Robo Tackle | 09 | 0bff2929 | 0B | AC D2 |
| 1D | Heal Beam | 59 | 65ff0c0b | 65 | 89 8A 8B 8F AB B7 |
| 1F | Area Bomb | 50 | 5cff2828 | 5C | 89 8A 8B AB |
| 20 | Shock | 22 | 26251515 | 26,25 | 85 86 A2 A5 A6 A7 B1 BC BE C9 DE E1 |
| 24 | *Heal | 58 | 64ff0c0b | 64 | 8F B7 |
| 26 | *Water 2 | 32 | 38ff1717 | 38 | 8F |
| 28 | Frog Squash | 41 | 4b4c0000 | 4B,4C | - |
| 2E | Tail Spin | 06 | 07ff0808 | 07 | 8F C5 |
| 2F | Dino Tail | 0E | 10ff0a0a | 10 | 80.04 B3 DE |
| 31 | *Lightning2 | 02 | 01020404 | 01,02 | 97 C9 DE E1 |
| 32 | *Ice 2 | 57 | 63ff0c0b | 63 | 89 8A 8B AB DE |
| 33 | *Fire 2 | 1C | 1fff0e0e | 1F | 85 86 89 8A 8B C3 DE |
| 34 | *Dark Bomb | 21 | 24ff0101 | 24 | 80.12 88.02 89 8A 8B AB BD |
| 36 | *Dark Mist | 2F | 35ff1a1a | 35 | 80.05 8F A2 A5 A6 AF B8 E3 |
| 37 | *Black Hole | 08 | 0aff0505 | 0A | 88.02 89 8A 8B AB |
| 38 | *DarkMatter | 1E | 21ff1212 | 21 | A1 A2 A3 A7 A8 B7 C3 DE |
| 39 | Aura Whirl | 03 | 03ff2f2f | 03 | 80.13 89 8A 8B AB |
| 3A | Ice Sword | 44 | 4dff0c0b | 4D | - |
| 3B | Ice Sword 2 | 69 | 75ff0c0b | 75 | E7 |
| 3C | Fire Whirl | 0A | 0cff0505 | 0C | 88.02 89 8A 8B AB |
| 3E | FireSword 2 | 42 | 4eff0202 | 4E | 8A 8B AB D0 D1 D2 D3 D4 D5 D6 |
| 3F | Rocket Roll | 16 | 19ff0909 | 19 | 89 8A 8B |
| 41 | Super Volt | 48 | 54532627 | 54,53 | 85 86 97 A2 A5 A6 A7 B1 B9 BC BE C9 DE E1 |
| 42 | X Strike | 46 | 51412828 | 51,41 | - |
| 43 | SwordStream | 31 | 37ff1d1d | 37 | 89 8A 8B AB B7 |
| 44 | Spire | 5F | 6bff2525 | 6B | C3 C9 DF E1 |
| 46 | Volt Bite | 45 | 50ff2525 | 50 | - |
| 47 | Falcon Hit | 74 | 7cff2f2f | 7C | AC D2 |
| 48 | Antipode | 0F | 11ff0c0b | 11 | 85 86 B9 C3 |
| 49 | Antipode 2 | 34 | 3aff0f0f | 3A | 88.01 89 8A 8B 8F AB B7 |
| 4A | Antipode 3 | 00 | 00062902 | 00,06 | 85 86 8F B9 C3 E8 E9 |
| 4B | Aura Beam | 03 | 03ff2f2f | 03 | 80.13 89 8A 8B AB |
| 4C | Ice Tackle | 5C | 67ff1010 | 67 | AC B9 D2 |
| 4D | Cure Touch | 1A | 65ff0c0b | 65 | 89 8A 8B 8F AB B7 |
| 4E | Ice Water | 6B | 77ff0c0b | 77 | 89 8A 8B AB DE |
| 4F | Glacier | 31 | 37ff1d1d | 37 | 89 8A 8B AB B7 |
| 50 | Double Cure | 28 | 3dff0c0b | 3D | 8F B7 |
| 52 | Ice Toss | 4D | 59ff2929 | 59 | 89 8A 8B AB |
| 53 | Cube Toss | 60 | 6cff0c0b | 6C | 89 8A 8B AB |
| 54 | Fire Punch | 4A | 56ff2828 | 56 | 89 8A 8B AB |
| 55 | Fire Tackle | 43 | 4fff0202 | 4F | 85 86 D7 |
| 56 | DoublevBomb | 30 | 2eff2828 | 2E | 89 8A 8B AB |
| 58 | Fire Whirl | 36 | 36ff0808 | 36 | 8F C4 |
| 59 | Blaze Kick | 43 | 4fff0202 | 4F | 85 86 D7 |
| 5A | Blade Toss | 5A | 66ff0c0b | 66 | B7 D2 DA |
| 5C | Cure Wave | 1A | 65ff0c0b | 65 | 89 8A 8B 8F AB B7 |
| 5D | Boogie | 7A | 83ff2424 | 83 | 8A 8B AB B7 D2 DA F0 |
| 60 | Slurp Kiss | 6A | 76ff3a3a | 76 | 89 8A 8B AB |
| 63 | Red Pin | 09 | 0bff2929 | 0B | AC D2 |
| 65 | Frog Flare | 26 | 2b2c0000 | 2B,2C | 89 8A 8B DE |
| 66 | Delta Force | 18 | 1bff0f0f | 1B | 80.05 8F A2 A5 A6 AF B7 B8 B9 |
| 67 | Lifeline | 39 | 3cff2424 | 3C | 8F B7 |
| 68 | Arc Impulse | 76 | 7eff2929 | 7E | EE |
| 6A | Fire Zone | 15 | 18ff2828 | 18 | 89 8A 8B AB |
| 6B | Delta Storm | 47 | 521d2525 | 52,1D | A2 A5 A6 A7 B1 B7 B9 BA DE |
| 6C | GatlingKick | 6C | 78ff3434 | 78 | 89 8A 8B AB |
| 6D | Triple Raid | 6D | 79413c3c | 79,41 | 85 86 EA |
| 6E | Twister | 3C | 446a0808 | 44,6A | 8F C4 |
| 6F | 3D Attack | 63 | 6f412828 | 6F,41 | EA |
| 70 | DarkEternal | 82 | 8aff1212 | 8A | A1 A2 A7 A8 B7 |
| 71 | Omega Flare | 81 | 89ff0e0e | 89 | 88.01 89 8A 8B B9 D2 DE F4 |
| 72 | Spin Strike | 83 | 8bff3737 | 8B | 85 86 8A 8B AB |
| 73 | PoyozoDance | 13 | 15160404 | 15,16 | A1 A2 A3 A5 A6 A7 A8 |
| 74 | Grand Dream | 09 | 0bff2929 | 0B | AC D2 |

## 5. Non-ported opcodes ranked by the PC actions that need them

8A 35 | 8B 35 | 89 32 | AB 28 | B7 16 | DE 15 | 8F 14 | D2 11 | 85 10 | 86 10 | A2 9 | A5 7 | A6 7 | B9 7 | A7 6 | AC 6 |
C3 6 | C9 5 | E1 5 | A1 4 | 88.02 3 | 97 3 | A3 3 | A8 3 | B1 3 | DA 3 | 80.05 2 | 80.13 2 | 88.01 2 | AF 2 | B8 2 |
BC 2 | BD 2 | BE 2 | C4 2 | D7 2 | EA 2 | 80.04 1 | 80.12 1 | A4 1 | B3 1 | BA 1 | C5 1 | D0 1 | D1 1 | D3 1 | D4 1 |
D5 1 | D6 1 | DF 1 | E0 1 | E3 1 | E7 1 | E8 1 | E9 1 | EE 1 | F0 1 | F4 1  (the actions per op: `fxcensus_actions.txt`).

By family, cumulative (actions fully runnable, of 79):

| add | total | newly complete |
|---|---|---|
| (today) | 9 | T03 T06 T0B T10 T12 T28 T3A T42 T46 |
| **89 8A 8B AB** (the shape window: set/step/draw/centre-on-point; 8B = 6 shape routines) | 21 | T0D T14 T17 T1F T3F T52 T53 T54 T56 T60 T6A T6C |
| DE (`$CD29 := b`) | 26 | T0E T18 T32 T4E T65 |
| 8F B7 (line-scroll wave; BG3 upload quadrant) | 35 | T1D T24 T26 T43 T4D T4F T50 T5C T67 |
| AC D2 (rotated quad; palette reload from `$5D8A`) | 40 | T1B T1C T47 T63 T74 |
| DA | 42 | T1A T5A |
| 85 86 C3 (block-offset loop; `INC $5D8F`) | 45 | T16 T33 T72 |
| 88.01 88.02 80.13 | 50 | T37 T39 T3C T49 T4B |
| C4 C5 (anchor wobble) | 53 | T2E T58 T6E |
| A2 A5 A6 A7 B1 B9 (3-D object set/save/restore/rotation, palette reload) | 55 | T48 T4C |
| 97 C9 E1 | 57 | T05 T31 |
| D7 | 59 | T55 T59 |
| EA | 61 | T6D T6F |

Still blocked after that: T08(A1 A3 A4 BD E0) T20(BC BE) T2F(80.04 B3) T34(80.12 BD) T36(80.05 AF B8 E3) T38(A1 A3 A8)
T3B(E7) T3E(D0 D1 D3 D4 D5 D6) T41(BC BE) T44(DF) T4A(E8 E9) T5D(F0) T66(80.05 AF B8) T68(EE) T6B(BA) T70(A1 A8)
T71(F4) T73(A1 A3 A8).

## 6. Dynamic confirmation (d)

Recipe: gato_wait.state + PEEK_POKE (party = char solo, all 8 techs, MP 99), oracle `--monitor 7EAE93:1,7E987C:1,
7ECA93:4,7E5D9B:1,7ECCEA:1,7ECA32:1,7ECA35:2,7ECA42:1,7ECA45:2,7EA013:1 --diff --writer` and `--hookpc CD16AF
--hookwatch 7E0040` (at CD/16AF, X = op*2 and Y = `$40` = op address + 1: the executed negative ops, the first 8 of
each frame, with the frame's visit count); the port from the same WRAM, `CT_BATTLE_TRACE=1`. `fxcmp.py` compares the
port's `fleeprog` lines with the oracle frame by frame (op, address, per-frame count) and checks every oracle op
address against the static decode.

| run | oracle: $AE93 / $987C / $CA93..96 | oracle executed | port | static vs oracle |
|---|---|---|---|---|
| Crono k=2 | 03 Lightning / 45 (C1:45EC) / 50 FF 25 25 (CD:0F56), slot 0 -> D1/8243 (CD:1600) | 99 frames, 170 negative ops, 31 distinct | arm id=50 ptr=D1/8243; **99/99 dispatch frames equal** (ops, addresses, counts); no STOP | all 31 at static op starts |
| Crono k=4 | 05 Lightning 2 / 02 / 01 02 04 04; slots D1/A1AD div 5, D1/A1BE div 1 | 146 frames, 598 ops; C9@A1CB E1@A1CD DE@A1CE 97@A1D0.. = the static set {97 C9 DE E1} | **STOP slot1 C9 at D1/A1CB** (equal until then) | 49 distinct, all op starts |
| Marle k=4 | 0D Haste / 56 / 62 FF 28 28 | 8A@7D12 AB@7D17 89@7D22 8B@7D27 8B@7D2B .. | **STOP slot0 8A at D1/7D12** | 32 distinct, all op starts |
| Robo k=2 | 1B Laser Spin / 09 / 0B FF 29 29, then **0B FF 0C 0C by CD:1ABB** (op D2's palette reload, from `$5D8A`) | D2@9CD3 AC@9CDE D2@9CDC | **STOP slot0 D2 at D1/9CD3** | 26 distinct, all op starts |
| Magus k=6 | 37 Black Hole (the C1/4847 `CMP #37` exception) / 08 / 0A FF 05 05 | 8A@9D46 AB@9D4B 89 8B CB CA A9 .. | **STOP slot0 8A at D1/9D46** | 57 distinct, all op starts |

Full CPU trace of Black Hole's first 4 program frames (`CPUTRACE_FROM=CD16AF` -- note: `strtoul`, so no colon, main.c
7274): 34 dispatches, every one at a static op start and every in-frame transition equal to the static length or a
branch/jump target, 0 exceptions (`9A 9C 9C 98 99 96 8A AB 8A AB CB 9B 94 CA 83 88 89 8B` then `84 88 89 8B` x3):
confirms 8A = 5, AB = 2, CB = 3 not taken, CA, 83/84, 89 = 5, 8B = 2, 9C = 3 directly.
Timing seen on the Lightning run: load f719 (`$CA93` CD:0F56), arm f725, start f825 (`$5D9B` C1:5938), first block
request f826 (`$CA35` CD:1689), last op f943, `$5D9B = 0` / `$CCEA = 1` f944 (CD:1650 / CD:1653).

## 7. Open questions

1. **The `$AE94` gate** (C1/484B `LDA $AE94 / BNE 485B`, bypassed only for tech 37 Black Hole): which PC actions reach
   C1/4844 with `$AE94 != 0` and so skip the effect load? All five runs had the load (single techs, solo party).
   battle_miss_evade.md names a `$AE94` bit-6 writer (C1/E91D); whether it runs before C1/4844 for a tech, and what
   double/triple techs put there, is not measured.
2. **Double/triple techs dynamically**: only single techs were run; the static census covers 39-74 through CD/45A6
   the same way, but the program's bound points `$5DA0..` come from the script and were not seen for a combo.
3. **D0/D1** (program 4E, T3E FireSword 2): the table extent is not encoded; the walker bounds each table by its lowest
   target (D1/82CE: 9 entries, 8347 82E2 .. 8337; D1/8347: 5; D1/8378: 3; D1/83AE: 3). The index is `$5D8B` / `$5D8D`
   & 0F or >> 4, written by the anim script -- which entries are really used is not measured.
4. **Op 8B**'s six shape routines, B5/B6 (circle), A1/B8/AD/AE (triangle fills), 97 (CE/EA7E), E8/EB/EC/ED/EE/F3/F4
   (D1 window-bar routines) and 8F/B3/E7 (line tables) are sized here but their pixel semantics are only summarised
   in one line; each needs its own derivation before porting.
5. **B9/D2 -> CD/1004** re-run the effect load's palette part mid-program (seen: `$CA95/$CA96` rewritten by CD:1ABB);
   the port's battleFxLoad does this only at load time.
6. **Frame costs**: CD/1654 ends a slot's frame only at 00-7F; whether a long negative-op run (e.g. the 21-op first
   frame of Lightning) costs the cartridge a frame (the runner runs inside the CD/09AF frame service) is not measured under the
   logical-frame rule. (Frame numbers differ between modes: Lightning's first dispatch frame is `--hookpc` f823, and the
   `$CA35` commit that frame makes shows in `--monitor` at f826.)
7. F9's word operand overlaps the next op (reads b0..b2 at p+1..p+3, spans 3) -- moot, no program uses F9.
8. Enemy techs (CD/5526 +6) and the non-PC entries (fx 40 / 84 / 87) run programs through the same runner; they are not
   in this census.

## 8. The load's frames, and the frames its CPU work spills over (2026-09-30)

### 8.1 The frames (ported: src/battle.c battleFxActionLoadFrame)

C1/4833-48EB on the rule's machine is one wait per frame (a CD/3E75 packet or a C1/007E), rel from the action's
`$A013` rise, A = the arm frame, w = the palette wait (0 / 15 / 31):

| rel | ROM | what lands |
|---|---|---|
| 0 | C1/4833 `JSL CD/0015` -> CD/1323 | `$CACE = $987A`, `$CACF = 0`, `$CAD0 = 4`; CD/132E's wait |
| 1-4 | CD/1331-1365 | one caster sheet a frame (CD/1368 -> $7E:2D00, `$CC9D..`), its packet; INC pair before 2-4 |
| 5 | INC pair, C1/483A `JSL CD/002A` -> CD/1314 | `$CACE = $987B`, `$CACF = 4`, `$CAD0 = 6` |
| 6-7 | the same | sheets 4 and 5 |
| 8 | INC pair, C1/4841's wait | |
| 9 (f 0) | C1/4857 `JSL CD/0018` -> CD/0D28 `STZ $5D9B`, CD/0EBD-0EF5 | `$CCA2`, `$CD28 $CD3C $CC91.. $CD38..` := 0; the palette arm (w > 0) |
| 9 + w | CD/0F0C / 0F28, CD/0F2D | `$BE89`, `$BE8B` := 0000; CD/0F33's wait |
| 10 + w | CD/0F36-0F3C | `$BE93 $BE91` := 0000; CD/0F3F's wait |
| 11 + w | CD/0F42-0FA4 + sheet 0 | `$CA93..96`, `$CD1F..22`, `$C9FC..CA01`, `$CA33`, `$CA1B`, `$CD04`, `$BC87 = 7C`, `$BE99 = 49`, `$BE8B = 1717`, `$CACE = 0`; CD/11E4 -> $2D00, the packet |
| 12-14 + w | CD/0FDD-0FE3, CD/0FA6.. | `INC $CD0C / INC $CACE`, sheets 1-3 (3 clears $2FE0/$30E0) |
| 15 + w | the fourth INC pair, CD/1052, CD/0FF0 | the fill $7E:D000-FFFF = 00EE, the map's first $400 packet |
| 16 + w | | the map's second packet |
| 17 + w = A | CD/1000-1051, CD/0D33 | CD/107D's blocks, CD/1004's palette, the slots armed |
| A+1 | C1/485B's wait, C1/4861 | C1/48EC's animation table `$A280..` |
| A+2 | C1/4864's wait, C1/486A | the LZ frame packet -> $2D00, `$0306` |
| A+3 | C1/488D's wait | `$A650 = $A652 = 0` |
| A+4..A+11 | C1/4943 x8 (wait first) | four frame records a call; C1/48B3's one-cell records with the eighth |
| A+12 | C1/40B0's wait | the script's first op |

Measured on the logical oracle (`--monitor`, Dark Matter / Flare / Shock from gato_wait; `--hookpc`, the 07D Frog
Squash with w = 15: CD/0EDE f86, CD/0F33 f101, CD/0F3F f102, CD/0FA6 f103-f106, CD/0FED f107). The port did the whole
of it on A (and the frame table on A too); tools/fx_load_stage_cmp.py grades every write, rel 1-45, for Dark Matter and
Flare (check battle-fx-load-stage-vs-logical-oracle). Rel 0's CD/1323 lands with rel 1's in the port: its PC dispatch
opens the action after the staging's pass in its frame. The no-graphics ids (86, 7B..7F: the run-off, enemy techs A0-A4)
keep the one-step load on A -- their pre-roll is the real oracle's, not re-measured on the rule's. CD/0EBD's remap is
ported with it: effect 88 (enemy tech 7A) is 88 / 89 / 8A on `$A018` / `$A017` == 7.

### 8.2 The CPU spill -- NOT ported, and why

Two stretches of the load are CPU work long enough to outlast a frame, with the NMI ON:
* CD/107D's blocks (f 17 + w -> the arm): a present tile costs ~1,440 master cycles of pure CPU, an absent one ~240, a
  full 256-tile block ~372k -- more than a frame (357,368).
* CD/1052's fill (f 15 + w): 3072 iterations, ~500k.

The rule's oracle holds the vblank for a stretch only while the CPU reaches a wait within one frame of CPU
(snes.c LF_VALVE); past that the frame is rolled back and runs in REAL time, so such a stretch keeps its real frames:
every vblank at which more than one frame of CPU work is left to the next wait is one more frame (CT_ORACLE_LOGICAL_TRACE
shows the rollbacks: Shock `frame 6267..6275 no wait after 357368 cycles at CD:1xxx -- ROLLED BACK`, then `frame 6276
held 341952`). Measured on the logical oracle, from gato_wait (hookpc CD/1000 -> CD/1004 = the build's frames):

| tech | fx | blocks x rows, present tiles | build frames | A/B span cartridge / port |
|---|---|---|---|---|
| Crono k=7 Luminaire | 01 | 2 x 16, 512 | 2 | 611 / 609 |
| Lucca k=5 Fire 2 | 1C | 13 x 16, 907 | 9 | 450 / 441 |
| Magus k=2 Fire 2 | 1C | same | 9 | 498 / 489 |
| Robo k=7 Shock | 22 | 20 x 16, 690 | 9 | 485 / 466 (+10 below) |
| Magus k=7 Dark Matter | 1E | 1 x 16, 256 | 0 | 533 / 533 |
| Lucca k=7 Flare | 0D | 1 x 16, 256 | 0 | 577 / 501 (76 below) |

THE PRICE OF THE BUILD IS DERIVABLE. A walk of CD/107D's control flow over the layout (D1:A233 row, D1:A575 records:
the stride byte, the $CA01*2 presence masks, the tile stream; per tile the mask reload every 8, the present/absent
arms, the stride / cycling arms, CF:E200's attribute test, the row and block ends), each instruction at its LakeSnes
cost read off the beam traces, gives Dark Matter 371,716 / Luminaire 743,064 / Fire 2 1,917,850 / Shock 1,972,652
against the traces' interrupt-free CD/1001 -> CD/1004 cycles 371,776 / 743,364 / 1,918,990 / 1,973,792 -- the
residue is the interrupt entries (60 each). CD/1004 -> C1/007E's wait adds 5,806.

THE FRAME'S BUDGET IS NOT. How much of each real-time frame the build gets is the frame less the battle NMI, the IRQ,
HDMA and the refresh:
* The NMI (00:FF10) during an action (`$A013 != 0`, `$A028 = 0`, `$CD2E = 0`) runs the frame service CD/0997.. and
  then `LDA #40 / JSR CD/3B82` (CD/09AA): a raster wait to line $40, then the tail (CD/1609 the runner, CD/0B1E,
  D1/F426, FD/FFF7 the objslot records, CD/0ADE, C2/8002, CD/0340; ~18k on GATO's). On GATO's battle (one PC, one
  enemy) the work before the wait ends before line 64 and the NMI returns at line 77 in every frame of the load; on
  the attract's 07D (three PCs, the field's colour records under the battle) the OAM compose CF/E58A.. runs past line
  64 (the raster wait falls through), the tail is ~33k (FD/E5xx-E6xx), and the NMI returns at line 106-112.
* The IRQ D1/FE97 (the field's tiles under the battle, odd `$7C`) takes ~11.7k in the frames it fires.
* HDMA and the refresh take ~31.5k of GATO's frame (148 lines x ~213).
The main thread's pure CPU per real frame: GATO 169,274-170,434 (37 frames measured); the 07D 128,386-147,674. With
W the build's price, P the budget and V = 357,368, the spill is the least K with W - P0 - K*P <= V: GATO Shock
(2,039,966 - 169,368 - 357,368) / 169,900 = 8.91 -> 9 (the held last frame: 341,494, 15.9k under the valve), Fire 2
8.58 -> 9, Luminaire 1.43 -> 2, Dark Matter < 0 -> 0 -- all four as measured. The same Shock on the 07D's budget would
spill 11-12. And the fill: 500k against GATO's ~170k + V fits (held ~301k); against the 07D's ~135k + V it does not
-- the 07D's Frog Squash spills one frame at CD/0FED (hookpc CD/0FED f107, CD/0FFD f108) where GATO's never does.
The menu pass C1/10E3 (55-66k on GATO, ~110k on the 07D) also runs inside a load wait whenever `$993A` is up (every
fourth frame from CC/F0E6's reset: on a w = 0 load at CD/0F33's wait and the third sheet's, on the 07D's w = 15 at
CD/0F3F's and the fourth sheet's -- the fill's own frame).

So the spill is the CPU-time class (CLAUDE.md, the zelda3 model) and, unlike the NMI-off reload (src/field_cost.c,
the phase-0 rule), an NMI-ON stretch's frame count needs the battle NMI's own CPU in every frame it spans -- the OAM
compose, the objslot service, the IRQ, HDMA -- which the port does not price. Predicted at GATO's budget for every
multi-block effect: fx 00 15, 01 2, 02 1, 0F 3, 10 1, 1C 9, 1F 1, 22 9, 23 1, 3E 1, 3F 7, 44 3, 47 4, 48 13, 5F 1,
60 1, 61 11, 65 2, 66 15, 67 5 (0.99 of a frame: at the edge), 68-6D 3, 71 3, 72 1, 73 2 (0.02: at the edge), 77 7,
83 11 (fx 00 = Antipode 3's 22-frame load, +11 -> +33). Porting it needs either the battle NMI priced (the pre-wait
work, the tail, the IRQ, HDMA per frame) or a rule for NMI-on stretches (user decision).

### 8.3 What the census's other span differences are (not the load)

* Robo k=7 Shock's other 10 and Lucca k=7 Flare's 76: the anim script's `98 00 18` (CE/235D, track 0) and `12 18`
  (CE/1B30, CE/1B3A, track 8) slide to the resolved point MODE 0x18, and battleAscResolve has no mode 0x18. C1/762E
  `JSR ($79D3,X)`: mode 18 -> C1/77D1 `LDA #80 / STA $A2B0 / STA $A2B2 / RTS`, the point (80, 80). The cartridge
  slides 10 frames (Shock) and 19 + 29 (Flare); the port resolves nothing and steps over each op in one frame. With
  mode 18 = (80, 80) (tried, not committed: the interpreter is another lane's) Flare is 577 / 577 and Shock 485 / 476
  (the build's 9). Modes 12-17 are absent too: 12 C1/767D (RTS: $A2B0/$A2B2 as they were), 13-15 C1/767E/7686/768E
  (the midpoint of role 0 and role 3 / 1 / 2), 16 C1/76D8, 17 C1/774E.

## The window-shape routines in detail (ops 8A/89/AB/8B, 97, A1/B8/DA)

All of them write the window buffer that is not on screen (`$7C & 1`: odd -> $C4E1.., even -> $C161..; HDMA channel 7
shows the other one, CD/0AEE), one line record [W1L W1R W2L W2R] per line. Handlers run with M = 8, X = 16 and B = 0
(the runner's `ASL A / TAX` dispatch, CD/16A8, needs it), so a `LDA [$40] / TAX` is the whole operand byte.

### The shape window (op 8B k, CD/2FB7 -> CD/3189)

Two shape records k = 0 / 1: centre ($CAA6+k x, $CAA8+k y), radius $CAA4+k, shape number $CAD1+k (0..5). Op 8A
(CD/2FED) sets one (X = b0 & 1, shape = b0 >> 4), op 89 (CD/2FBC) steps one (X = the whole b0; x += b1, y += b2 capped
at D3, radius += b3), op AB (CD/24F3) clamps bound points 0 and 1 (x 08..F8, y 08..D4) and centres record 0 (or record
1 with b bit 7) on point b & 7F.

Op 8B: `STA $CAAA`, then CD/31AA builds the circle's half-chords and returns with the carry clear, drawing nothing,
when `$CAA4,k & 7F` is 0 (the buffer keeps what it held). CD/31AA (X 8-bit) is the midpoint circle of radius r over one
octant, in 8 bits: `STA $49 / TAX / STZ $51 / TDC / TAY / DEC A`, then at 31C1 `STA $47 / TXA / CMP $51 / BCC ->31E6`
(x < y: done), `STA $B800,Y / INY`, $49 := $49 + ((2y) EOR FF), `INC $51`, and `CMP $47 / BCC ->31C1` (no wrap below
the value at the loop head), else `DEX` and $49 += 2x. Then `JMP (CD:319E)[$CAD1,k]`, one of six shapes (a table
builder and a placer each); `ASL A` (8-bit) drops bit 7 of the shape number, and 6..7F index code bytes past the table:

```
0 CD/33B5 = 31FE + 35A5    1 CD/33BB = 3273 + 352D    2 CD/33C1 = 3273 + 347D
3 CD/33C7 = 31FE + 33D9    4 CD/33CD = 3337 + 35A5    5 CD/33D3 = 3337 + 33D9
```

The table builders (X 8-bit): each row is [cx - h, cx + h] (`SBC` floored at 0, `ADC` capped at FF), stored at
$B900+X / $B901+X with X from FE down by two (the upper half, the equator written highest) and, on rows that go through
`STA $2180`, streamed from $BA00 up (the lower half). The first octant's rows take h = $B800,y for y = 0..n-1; then
`STZ $B800,n` and, walking y = n-1 down to 1, a row only where $B800,y differs from $B800,y-1 (`CMP $B7FF,Y / BEQ`),
with h = $47 (from $B800,n-1 - 1, `DEC $47` every step). 31FE (shapes 0 3): every row both ways. 3273 (1 2): $4F counts
the rows from 0 and the odd ones skip the $2180 stream, so the lower half has half the rows. 3337 (4 5): h doubled.

The placers (`REP #10`, then M = 16) copy the table's rows around its centre $BA00 (row index 80) into the window lines
from the shape's top, clipped at line 0 (`$49 += -($4F)`) and at line DE. r and cy are the whole bytes $CAA4,k /
$CAA8,k (bit 7 of the radius stays). 35A5 (0 4): the whole circle, rows 80-r .. 80+r. 352D / 347D (1 2): the lower
half at half height, rows 80-r .. 80+r/2; 347D (shape 2) then fills lines 0 .. cy-1 with the equator word at $BA00 (a
do-while to X = cy*4 + w2: with cy = 0 the 16-bit X goes all the way round). 33D9 (3 5): every row onto two lines, rows
80-r .. 80+r from line cy - 2r (also a do-while).

CD/3616, the placers' copy: `$4B := (E7C7 - 3 * ($4B - Y))`, Y += B900, X += the buffer, `JSL CE/E7C8` = `JSR E7CC /
RTL` with E7CC `JMP ($004B)`, into the unrolled copy that ends at CE/E7C7 (RTS): 256 pairs `LDA $2k,Y / STA $4k,X` (six
bytes each, k = 255 at CE/E1C7 down to k = 0 at CE/E7C1; CE/E1C1 is not one). Entering 3*d bytes before the RTS runs
the last d/2 pairs: word k of the table (two bytes a line) to line k of the window (four bytes a line), k = d/2-1 .. 0,
16-bit (the indexed operands carry into bank $7F). The entry is outside that block when d > 512 (a radius with bit 7)
or "negative" (the shape's top below line DE and the bottom trim exceeding it): the ROM would then run code that is not
the copy. None of the techs run (Haste, Napalm, Mega Bomb, Area Bomb, Black Hole up to its op 88 02) meets it; the port
reports a stop.

### The half-plane (op 97, CD/30BE -> CE/EA7E)

Op 97 b0 b1 b2: X = the slot, +1 with b2 bit 1 (window 2's angle $CA78/$CA88). b2 bit 7: `$CA77,X := b0` and nothing
else. Else the angle += b1 into $CA77,X and $CAA3; the segment is CE:F28E[angle] (x, y) -> $CA9B/$CA9C and the screen's
centre (80, 70) -> $CA9D/$CA9E, swapped when $CA9E < $CA9C so the top point comes first; CD/3134 stores their deltas
($CA9F = sign(x2 - x1), $CAA0 the same for y, $CAA1 |dx|, $CAA2 |dy|), `$4F = $CA9B / $51 = $CA9C` and JSL CE/EA7E with
A = b2 & 7F. Either arm ends `LDY $40 / INY / INY / STY $40`.

CE/EA7E fills window w's half-plane on the side of the segment the angle $CAA3 faces (`a & 2` picks W2; the code exists
twice, at bases 0x380 apart, for the two buffers). PHA / TAY / STY $45 (16-bit, B = 0), then:

```
E885  every one of the 224 lines: angle < 80 -> R := FF; angle >= 80 -> L := 00 and INC $45
      (so $45 = the byte the edge below writes: L for angle < 80, R for >= 80)
EA8D  $47 = $51 * 4 + $45 (the segment's first line)
EAA4  angle >= 80 and $51 != 0, the lines above it: $4F == FF -> their R := FF and line 223-k's L := FF;
      $4F == 0 -> `00FF` (empty); else R := $4F
EAF9  the edge: dx = $CAA1, dy = $CAA2; X = $4F; per line `LDA $45 / CMP $49 / BCC / SBC $49 / INX (DEX when
      $CA9F < 0)` until below dy, `ADC $47` (8-bit), the byte := X; dy lines. With dy == 0 nothing, and X keeps dy.
EB4F  PLA / AND #02: angle >= 80 -> the lower half's R := ~ the upper half's (line 223-k from line k, k < 112);
      angle 0 -> lines 112..223 `00FF`; else the lines below the edge (to line 224, `CPY #0380+w`): the last X FF ->
      `00FF`, 0 -> L := 0 and `STZ R,X` (X from 0, the ROM's own index), else L := X; then the upper half's L := ~ the
      lower half's (line k from line 223-k).
```

### The triangle fill (ops A1, DA: CD/3685; op B8: CD/3952)

CD/3685 (JSR from CD/367C, which sets DB = 0 around it) fills window pair `$CC64 & 1` of the buffer being drawn with
the triangle v = $12..$17 = x0 y0 x1 y1 x2 y2 (CD/38B6 = the same base table CD:38AA as D1/F9AF). All in DP scratch,
which the battle NMI restores (D1/E7A9 MVN $00-$2A -> $CC65, D1/E885 back); only the buffer and $CC64 stay.

```
sort  3685/3693/36A1: points 0-1, 1-2, 0-1 swapped (16-bit, x with y) when y >= the next one's
36AF  y0 == y1 (flat top): x0 >= x1 swaps 0-1, then 3777: X = x0<<8 (+= slope 0->2), Y = x1<<8 (+= slope 1->2),
      y2-y0 lines
36C6  else x1 >= x2 swaps 1-2 (their y may now be out of order); y1 == y2 (flat bottom, 37BF): X = Y = x0<<8,
      X += slope 0->1, Y += slope 0->2 for y2-y0 lines
36DD  general: s03 = slope 0->1, s05 = slope 0->2 (their flags $00 / A); same flag and s03 >= s05 (16-bit, CPY)
      swaps points 1-2 and the two slopes (370B-3723); X = Y = x0<<8 and
        y2 >= y1 (3725): y1-y0 lines X += s03, Y += s05, then y2-y1 lines X += slope 1->2, Y += s05   (37FE)
        y2 <  y1 (3750): y2-y0 lines X += s03, Y += s05, then y1-y2 lines X += s03, Y += slope 2->1   (3854)
```

The slopes are CD/38E5: an 8.8 x-step through the divider, with the flag in A: 0 on the add path, `LDA #01` on the
negated one. Each line is `TXA / ADC / TAX / STA $217F / TYA / ADC / TAY / STA $217F / LDA $217F / LDA $217F`: the
16-bit stores put X>>8 then Y>>8 through WMDATA ($2180; the low bytes go to APU port 3) and the two reads step over the
other pair; one `REP #21` (carry clear) per section, the carry chaining through the ADCs after it; a zero count skips.
CD/38B6 leaves two direct-page words above the NMI's $00-$2A: `ASL / ASL / STA $45` = y0*4 and `STA $47` = ($7C&1)*2.

The wedge, CD/3952 (JSR from CD/3924, op B8's fill; CD/3924-3951 is CD/3657-3684 byte for byte but for the JSR), is the
same routine with three differences (CD/396A-3A30 = 3685-374D, 3A33-3A78 = 3777-37BC, 3A7B-3AB7 = 37BF-37FB, 3ABA-3B0F =
37FE-3853, instruction for instruction):

- CD/3952-3969: point 2 is replaced before the sort by the corner of points 0 and 1: y0 < y1 -> (x1, y0), else
  (x0, y1), a right triangle whose hypotenuse is 0-1;
- CD/3A0A: the general path has no `BCC 3750`; it always takes the y2 >= y1 arm (the counts wrap if y2 < y1);
- CD/3B10 (the writer's head) sets WMADD to the pair's line 0 (no + y0*4), `STA $45` (8-bit) = y0, `STA $47` =
  ($7C&1)*2, and fills lines 0..y0-1 first with the start pair X>>8, Y>>8 (the band above the triangle, from the top of
  the screen down to its apex). $45 ends 0.

### Window pair clears (op 88 sub-ops, CD/29DA -> D1/FBD2)

Sub-op 0 (D1/FCF4) empties the whole buffer being drawn (`FF 00` on every line). Sub-ops 1 / 2 (D1/FC38 / D1/FC96)
empty one window's pair: `LDA $7C / AND #01` picks the buffer, then `REP #20 / SEP #10 / LDX #70 / LDA #00FF` and eight
16-bit stores 0x70 apart (`STA $C4DD,X` .. `$C7ED,X`, or `$C4DF,X` .. `$C7EF,X`) for X = 70, 6C .. 04: the line records
at +4 .. +0x380 (lines 0-223), bytes 0-1 (W1) or 2-3 (W2). Sub-op 3 (D1/FBDA) is FC38's twin with `LDA #FF00`: the W1
pair set to `00 FF` on lines 0-223, window 1 covering the whole line (odd -> `STA $C4DD,X` .., even -> `STA $C15D,X`
..). No PC action's program reaches sub-op 3.

### The per-line COLDATA ramp (CD/2D51-2DCA)

Shared by 80.00 (CD/2D4A: `STZ $CD04` first, $45 = 4), 80.0E (CD/2D44: $45 = 4) and 80.13 (CD/2D39: $45 = 14).
`LDA $CD04 / BEQ / DEC $CD04`, then `$47 := $CD04`; `SEP #10 / TDC / TAX / TAY` and sixteen records up (`INX x2 / INY
x4 / CPY #40 / BNE`), `DEX x2`, sixteen down (`DEX x2 / INY x4 / CPY #80 / BNE`), `REP #10 / RTS`. Each record Y:
$BB06,Y = $BB86,Y = $BC06,Y = $45; $49 = X - $47 (0 on a borrow); $BB07,Y = $49 | 80, $BB87,Y = $49 | 40,
$BC07,Y = $49 | 20 (the B / G / R intensity). The records are HDMA channel 5's four-byte units (CGWSEL CGADSUB COLDATA
SETINI, one a line; the table $C948 reads `E0 05 BB | C0 05 BB | B0 2F C0 ..` in Aura Whirl), so $45 lands in CGADSUB:
04 = BG3's colour math (80.00, 80.0E), 14 = BG3's and the sprites' (80.13).
Note for disassembly: the loop ends `C0 80 D0 CE` = CPY #80 (8-bit index) / BNE 2D96, then `C2 10 60` = REP #10 / RTS.
Decoding CPY #80 as 16-bit misreads the tail as `DEC $10C2`, a write the ROM never makes.

## The 3-D objects (ops A2 A3 A5 A6 A7 A8 BA BC BE)

The object block $CAD4..$CAFD (42 bytes) holds two objects interleaved: object 0 at the even address, object 1 at the
odd one. A2 o x y z (CD/278E) initialises one ($CAD4 x, $CAD6 y, the three radii $CADA/$CADC/$CADE := z, the vertex
angles $CAE4/$CAE6/$CAE8 := C0/15/6A, the scale angles $CAE0/$CAE2 := 40). A5 k / A6 k (CD/2690 / CD/26C0) save /
restore the block at $CB0E + CD:26B0[k] (0000 002A 0054 007E 00A8 00D2 00FC 0126). A7 o a b c (CD/2660) sets the
rotation $CAF6/$CAF8/$CAFA, `STZ $CAD8,X`, $CAFC,X := FF. BC o da db dc dz (CD/1E45) steps the rotation and $CAD8
with no projection. DP $00-$2A is the battle NMI's scratch (D1/E7A9 saves it to $CC65, D1/E885 puts it back; the
runner runs inside that NMI), so of the direct page only $45-$51 outlive a handler.

CD/3C8F (X/Y 8-bit) turns the point ($CB06, $CB08, $CB0A) = (x, y, z) by the angle $4B about y, then $49 about x, then
$4D about z, all 8-bit (S = CD/3C58: CE:F48E[a] times |y|, the product's high byte negated when exactly one of a >= 80
and y < 0 holds; C = CD/3C55 = S of the angle + 40; the adds and subtracts wrap):

```
$CAFE = C(b,x) + S(b,z)          $CB02 = S(b,-x) + C(b,z)       (b = $4B; -x = `EOR #FF / INC A`)
$CB00 = C(a,y) - S(a,$CB02)      $CB02 = S(a,y) + C(a,$CB02)    (a = $49; y = the old $CB00, still in Y at
$CB00 = C(c,$CB00) + S(c,$CAFE)  $CAFE = C(c,$CAFE) - S(c,y')    CD/3D17 / CD/3D68; c = $4D)
```

$47 holds each first product and is left as the last one, S(c, y').

A8 o dx dy dz dr da db dc ds (CD/27CA, `SEP #10`: X = o, 8-bit) steps object o (`CLC / ADC [$40],Y` each: $CAD4 += dx,
$CAD6 += dy, $CAD8 += dz, the three radii += dr, the rotation += da/db/dc, the scale $CAFC += ds), then CD/2843 and
`REP #30 / $40 += 8` (ten bytes). CD/2843, per vertex i (radius $CADA+2i, angle $CAE4+2i): `$47 := radius`, CD/28A1
(`$CB08 := CD/3D90(angle + 40)`, `$CB06 := CD/3D90(angle)`, `$CB0A := $CAD8`, `$49/$4B/$4D := $CAF6/$CAF8/$CAFA`,
`$21 := $CAFC` with no reader here, CD/3C8F), then vertex i = ($CAFE + $CAD4, $CB00 + $CAD6) into $CAEA+4i / $CAEC+4i
(8-bit stores: $CB07/$CB09/$CB0B keep their high bytes). CD/3D90 is `CE:F48E[a] LSR` times the radius, the $4217 byte,
negated when a >= 80. Op A1 draws what A8 leaves.

BA o / BE o (CD/1E91 / CD/1E27, 16-bit X = o): the spin of object o around a model. BE: $4F := 20, $51 := $CAD8,X. BA:
$4F := o << 4 (8-bit), `STZ $51`, then the rotation stepped (`INC $CAF8,X / INC $CAF6,X` x2 `/ INC $CAFA,X`). Both go
on at CD/1EAA: for vertex i = 0, 1, 2, CD/1EF7 indexes the model table D1:EC5F at `i*4 + $4F` (8-bit) for (x, y, z):
`$CB06 := [0]`, `$CB08 := [1]`, `$CB0A := [2] + $51` (each `TAY / STY`, 16-bit, the high bytes 0), `$49/$4B/$4D :=
$CAF6/$CAF8/$CAFA,X` (also `STY`, so $4A/$4C/$4E := 0; and `$21 := $CAFC,X`), `SEP #10 / JSR CD/3C8F / REP #10`, and
vertex i = ($CAFE + $CAD4,X, $CB00 + $CAD6,X) into $CAEA+4i / $CAEC+4i. Then `JMP CD/2173` (op AE's body, same operand):
`$CC64 := o & 1` = the window and X = o & 1; the six vertex bytes pulled toward the centre by the scale $CAFC,X (CD/211B,
the x's with $47 = $CAD4,X, the y's with $47 = $CAD6,X) into $12..$17, `STZ $0020`, `JMP CD/367C`: the triangle filled
into window pair $CC64 of the buffer being drawn. CD/211B: `TAY / STY $45 / STZ $48`, `REP #20`, d = v - c into $49;
d >= 0: c + (d * $21 >> 8), else c - (|d| * $21 >> 8), 16-bit into $4B, the low byte returned.
