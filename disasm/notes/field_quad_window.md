# The combat tutorial (loc 0FE, scene 254): the backdrop, the quad window, the jump -- and the CALL's budget

User report, 2026-10-07 (F5 `ct_debug_011`, the Truce mayor's manor 2F, loc 00E): the "sample battle" of the manor's
tutor (obj11, `C0 61 0B` / `CASE 2` / `E0 FE 02 08 08` -> loc 0FE) showed (1) its circle BLACK where the cartridge shows
MAGENTA, (2) no yellow indication for the line attacks, (3) at the end, the imps did not move into the damage zone. And
(4), the same day: talking to the NPC above Crono in loc 00E put the box at the BOTTOM; on the cartridge (the user's own
snes9x save, aux `caixa_dialogo.state`) it is at the TOP.

Scene 254 has no `D8`: obj0's autorun draws the lesson with field objects and two screen effects. Every claim below is
graded against the rule's cartridge from the same WRAM by `tools/tutorial_cmp.py` (circle 8/8, line 7/7, jump 8/8 frames,
state + table + picture identical) and the C3 routine by `ct.exe --sweeppoly` (3294/3294).

## 1. The circle is the BACKDROP (scene.c CtComposeScene)

obj0's `4B 00 22 7E D8 5C` (C0/67FB) writes 5CD8 to $7E:2200 -- colour 0 of the live page the NMI uploads (R 24, G 6,
B 23: magenta; `--ppuregs` on saves/field_re/scene254_tutorial.state reads CGRAM[0] = 5CD8). `FF 90 98 2C 25` opens the
iris (field_iris_window.md): window 1 takes BG1/BG2 off the main screen (W12SEL $22, TMW $17), and the scene has no BG3
and no sub screen (screen_flags 0F, effect 00), so inside the circle no layer covers the pixel and the PPU shows the
backdrop, CGRAM 0 (snes/ppu.c ppu_handlePixel). The port's compose left uncovered pixels at calloc's black -- the same
colour in nearly every location, whose $2200 is 0000 -- so only a script that paints colour 0 shows it. Fixed in the
compose: a main pixel no layer covers (and no sub-screen pixel shows through, the existing arm) is palc[0].

## 2. The line is C0/21E1's entry 6 (ct_vm.c ctLineStep / ctPolyQuad, field.c fieldColWindowRows)

`FE 30 48 48 4E 4E 48 48 52 52 48 FF 52 80 48 FF 4E 7C` is C0/3F26 (move.c CtVmScreenFxOp): eight 8.8 channels at
$7F:1CE9.. from (start, target) pairs over D = $30 frames, their deltas through the SNES divider at $7F:1CF9.., the
countdown $7F:1CE8 = D, `$0139 = 6`. The live VM stepped over it (and over `F1`): no channels, no line.

C0/21E1 runs `$0139` before the object pass (C0/00B1); table C0/21EE = 220E 2269 22D8 232A 238A **2409** 2514 2514 2589
2589 260E 264F 2329 **23E2**. Entry 6, C0/2409, one frame:
- `$7F:1CE8` counts down; at 0: `STZ $39` and the X channels' deltas (1CF9 1CFD 1D01 1D05) zeroed -- the line stays.
- TMW/TSW $7F:1522/1523 = 17; W12SEL/W34SEL $0BD9/$0BDA = 00; **WOBJSEL $0BDB = 20** (window 1 is the colour window);
  **CGWSEL $0BDE = 12** (math only inside it, sub screen add).
- `$0360+k` = channel k's high byte: the quad's four vertices (x0,y0) (x1,y1) (x2,y2) (x3,y3).
- `TSB $28 #20` (HDMA channel 5), `$0350 = $53 ? 17C8 : 1AC8`, `$0352 = 7F`, `JSL C3/0011` -> **C3/0EFA**.
- every channel += its delta (16-bit, CLC).
`FF 9C` (C0/41C4: one tail C0/41B0-41C6 for subs 98-9C, `LDA #05|07|09|0B|0E / STA $39 / TYX / INX / INX`) gives entry
0E, C0/23E2: `STZ $39`, TMW/TSW 00, W12SEL/W34SEL/WOBJSEL 00, CGWSEL 02, `$21 = $22 = E0`, `TRB $28 #20`.

**C3/0EFA** (DP $0300): the walk starts at the vertex with the least Y (`CMP / BCC`: a tie goes to the later vertex) and
rasterizes the four edges in order through C3/1025 (Bresenham with the carry its loops keep: 1076/109F enter the ADC on
the SBC's borrow), each writing one byte per line into the table's (left, right) pairs -- the edges going down into the
first byte, and from the first edge going up (`INC $50`, $6A = its top = the quad's bottom) into the second. A flat edge
(dy 0, C3/1001) writes its min at line y and its max at line y+1 (`INY / INY`), signed compare. Then line 0 takes 01 00, an
MVN carries it from line $6A to line $FF and another from line 0 down over line $68 (the top vertex's own line is emptied
too). A vertex on line FF makes the bottom MVN 64 KB (`$F0 = FFFF`): in $7F harmless, in $7E it overwrites the ROM's own
stack -- the sweep draws the $7E twin without that line (the game always hands $7F).

The colour: `F1 7F 00` (C0/3D07, its `o2 == 0` arm) sets `$22 = $21 = 7F` and `$0BE0 = 17`. The NMI writes COLDATA `E0`
then `$0121` (C0/EC3D-EC45) -- R and G at 31, B 0 -- and FD/C6F6 / C846 copy `$0BE0` / `$0121` into channel 6's rows
($7F:152C-152F: CGADSUB / COLDATA for the lines outside the text box). The cartridge's `--hdma` trace (f1060 of the fixture
drive, HDMA_TRACE_BG=1): lines 80-126, W1 72..80 to 251..251, w1en 20, prevent 1, math 17, fixed (31,31,0). So inside the
quad BG1-3 and the OBJs of palettes 4-7 get (31,31,0) added; the imps (OBJ palette 1, attr 23) are drawn over it untouched.
The port: fieldColWindowRows reads that set (and reports any other once), fieldColWindowApply adds the fixed colour per
the PPU rule (snes/ppu.c: mainLayer 4 = OBJ 4-7, 5 = backdrop, half only without a sub screen add).

## 3. The CALL's completion spends one dispatch (ct_vm.c step(), the waitObj release)

Before the first `FE`, obj0 waits `04 18 11` on obj12 (the cursor hand). The cartridge (`--monitor 7E1180:2` from
saves/field_re/scene254_line_c960.state): obj12 returns at f7; obj0's next visit f11 runs `04` + `71 06` + `02 1A 21` +
`4F 03 08` + `02 18 11` and stops ON the `FE`, which runs at f15. The visit budget is C0/5A93's `$67 = $68` (5), and every
SEC exit spends one (5AB6 `DEC $67`); the parked IP is the 04's own byte (61A4 `TYX / CLC`), so the visit that finds the
call complete dispatches the 04 again and its exit 61E0 (`INX x3 / SEC`) is dispatch 1. The port released the wait for free
and fitted the `FE` into f11: the line four frames early. Now the release returns "continue" (one dispatch spent). And the
runner's write-back of $1180,X holds the call op while it waits (the ROM parks on it; --vmdiff already compared callAddr).

## 4. The jump is op 0x7A + C0/AAC7 (move.c CtVmOp7A, ct_vm.c's integration pass)

The end: obj0 REQs each imp's `7A xx yy 02 / 71 09` and polls `loc[9] == 5`. C0/4D06 (move.c, --sweepops-graded) aims with
C0/ABA2, builds the velocity with C0/AC69, the ticket = |delta| << 8 / |velocity| through the divider (X axis first, then Y,
then the literal), `$1B81` = the third operand, `$1980 -= (ticket * op3) / 2`, `$1A80 = $1B80 = 1`; it parks until the
ticket is spent, then `STZ $1A80 / STZ $1B80`, ip + 4. Both integrators open with `LDA $1B80,X / BEQ` (AA09 in AA07,
AAFF in AAFD) and take C0/AAC7: ticket - 1, X += the 16-bit $1900, the 8-bit $1980 += $1B81 (carry into $1981 -- the
gravity), Y += the 16-bit $1980. The live VM had no arm for 0x7A (the imps never left, obj0 waited forever) and no $1B80
arm in its integration. From saves/field_re/scene254_jump_c2830.state the five imps' $1800/$1880/$1A01 are the cartridge's on
every one of 200 frames.

## 5. The box above Crono in loc 00E is the ENTRY CAMERA (ct_vm.c CtSceneCameraEntry)

C0/1FCD puts the box by the speaker's screen Y against 128. In loc 00E the NPC above Crono (obj10, y 159) is at screen 143
on the cartridge (camera $1D0E = 02, the user's save) and 127 on the port ($1D0E = 04). The camera of that room is the
entry law C0/7553 and nothing after it: the room is 13 tiles tall ($1D1C = 02, $1D1D = 0E), shorter than the screen, so
no scroll moves it vertically (9A1F: `$1D10 >> 1` past `$1D1D`). The law's bottom pin is `($1D1D + 1) * 2 - $1C` = 02 --
ONE coarse above the top pin `$1D1C * 2` = 04, and 757B-7596 stores it as it is. The port had a "guard" pinning anything
under `$1D1C * 2` to it -- no routine behind it. The ROM's loader (`--teleport 14 44 11` / `40 13`: $1D0E = 02; `44 5`:
04) and the port now agree. Eight rooms carry such a short mask (F6:000A-000D): 00E (2 not 4), 088/08B (34 not 36),
089/099/09A/0F9/0FA (98 not 100); none makes a pin negative and no X pin crosses its edge.

## 6. Measuring off the rule shows a false divergence

`ct.exe --monitor` / `--hookpc` WITHOUT `CT_ORACLE_LOGICAL=1` cut the runner at `$69` (vPos 72) while the box types (C2/5DFA
renders the glyphs before the event pass): the imps' WAITs starve for 24 frames and their blink looks out of phase with the
port's. Under the rule (the runner's virtual beam, V = 0) the cartridge's blink is the port's frame for frame.

## The colour window with a sub screen (FF 97's circle)

`FF 97`'s circle (C0/232A) can run in a room that keeps BG3 on the sub screen (loc 051 / 005: TM 13, TS 04; CGWSEL 12
adds the sub screen). Inside the window the main layers with math take the sub pixel, or the fixed colour where the sub
screen is transparent; the PPU's halving skips that case (snes/ppu.c ppu_handlePixel, `secondLayer != 5 ||
!addSubscreen`). Outside the window the math is prevented, so the pixel is the main screen alone: src/field.c
`fieldColWindowApply` takes the bake's sum off again from the bake's main screen before the math and its sub screen
(`CtFieldSceneScreens`). tools/field_colcircle_cmp.py pokes the circle in loc 005 (the blue half-height circle over
the shop) and loc 084 (TM 04 / TS 13: black outside the circle).

The NMI's COLDATA write is `E0` then `$0121` (C0/EC3D-EC45), and FD/C6F6 / C846 build channel 6's rows every frame from
CGADSUB `$0BE0` and COLDATA `$0121` (`LDA $21 / STA $152D|$152F`, `LDA $0BE0 / STA $152C|$152E`). So the fixed colour is
`$0121`'s channels at its intensity, the others 0. A main pixel whose layer has its CGADSUB bit (BG1-3, the OBJs of
palettes 4-7 with bit 4, the backdrop with bit 5) takes the fixed colour added or subtracted (bit 7), halved (bit 6)
unless CGWSEL bit 1 adds the sub screen, then clamped in 5 bits.

The sprites take their own colour math the same way, with or without a colour window: a main-screen OBJ pixel of
palettes 4-7 takes it when CGADSUB `$0BE0` has bit 4 set (C0/A412 sets `$0BE0` from the map header; op 0xF1 rewrites
it). In loc 005 after `F1 9F 00` (`$0BE0` = 17) the lead's palette-4+ pixels take BG3's sub pixel once the circle is gone.
