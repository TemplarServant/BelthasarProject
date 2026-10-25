# The sequence stages' picture, native (2026-10-17)

The user's decision (2026-10-17): the `$C3` sequence stages (the race, entry 3, Lavos's fall, the Epoch into the shell, the Day
of Lavos, the ending) get a NATIVE compositor, not the vendored PPU (src/seq_ppu.c, which keeps drawing the translated attract
stages until they go native). Six stages are ported as logic only (stage 1, their picture black); this is the picture's
foundation, shared by all of them.

## 1. The compositor (src/seq_pic.c)

A stage draws by writing the display registers ($2100-$213F) in its NMI and per scanline through HDMA, and by filling VRAM,
CGRAM and OAM. A `CtSeqPic` holds those: the arrays, the last byte written to each register, and the hardware's latches the
registers are written through (the BG scroll pairs `$210D-$2114` with their two previous bytes, the mode-7 pairs `$211B-$2120`
with theirs, COLDATA's three channels, the line `$2106` was written on). `CtSeqPicWrite` is the register write;
`CtSeqPicFrame` walks HDMA as the hardware does (the init and a first transfer before line 1, then a transfer after each line,
direct and indirect, the eight transfer modes) and composes each line from the registers as they stand then.

What a line shows is the oracle's PPU's (snes/ppu.c is the picture the port is graded against), written over the register
bytes with no bus, no ports and no clock: the layer order by mode (with BG3's priority bit and EXTBG), the 2 / 4 / 8 bpp fetch
with big tiles and the 64-column / 64-row maps, offset per tile (modes 2, 4, 6), mode 7's line start (each product's low six
bits dropped, the 13-bit scroll and centre) and step, its large field and character fill, the mosaic, the windows (two per
layer, inverted, four mask logics), the colour window's clip and prevention, the colour math (sub screen or fixed colour, add or
subtract, the half -- not where the operand is the sub screen and the sub screen is its backdrop), direct colour, and the
sprites (OBSEL's sizes and bases, the 32-sprite and 34-sliver limits, the priority rotation). The data ports ($2104, $2118/9,
$2122) are not modelled: a native stage fills the arrays itself (said once if written).

## 2. Graded

`ct.exe --seqpiccmp <state> <frames>` (like --seqppucmp): the oracle stepped until it leaves vblank, its PPU's state turned back
into the register bytes it was written as, VRAM / CGRAM / OAM copied, the frame drawn natively with the oracle's WRAM as HDMA's
tables, the oracle run to its next vblank, the pictures compared pixel for pixel. Every frame equal:

| Check | Stage | Frames |
|---|---|---|
| `seq-pic-race-attract-vs-oracle` | the race (mode 1 sky over a mode-7 road, per-line matrix, CGADSUB and BGMODE per line, the window's curtain) | 430 |
| `seq-pic-entry3-vs-oracle` | entry 3 (mode 2, offset per tile, BG2's map base changed at line 70) | 300 |
| `seq-pic-ending-vs-oracle` | the ending | 400 |
| `seq-pic-day-of-lavos-vs-oracle` | the Day of Lavos (FF 07) | 400 |

## 3. A stage's picture state (CtSeqStagePic)

The compositor plus what a stage's code sets around it: the HDMA channels ($43x0-$43x7 as the stage programmed them), HDMAEN,
and the data ports' addresses ($2116 / $2115, $2121). `CtSeqStagePicDma` is a DMA from WRAM into $2118 / $2119 (VRAM through the
port's address and VMAIN's step), $2122 (CGRAM, the low byte buffered) or $2104 (OAM and its high table); `CtSeqStagePicVramRead`
reads VRAM back through $2139 (the read buffer's one-word lag: the first word comes twice). The shared stage NMI (seq_race.c
CtSeqStageNmiHooks, C3/08D0) writes INIDISP $80 and HDMAEN 0 before the hooks, HDMAEN = `$038C` and INIDISP = `$038D` (or $80)
after them; a script's `FB` (C3/0C5F) writes its register. A host gives the stage a picture through `CtRaceIo.pic`.

## 4. The race (src/seq_racepic.c) -- DONE for the story race (2026-10-17)

`ct.exe --racepiccmp <state> <btnscript> <handoff> <operand> <frames>` lays the native stage against the oracle NMI by NMI from
Johnny's `FF 00` (saves/race_re/race_user.state, A at f30, the hand-off at f65): VRAM, CGRAM, OAM, the registers, the HDMA
channels and the race's own WRAM (all but the field's pages $0100-$06FF and the stack / sound queue $1E00-$1FFF) as the oracle
leaves vblank, then the pictures. `RACEPICCMP_SEEDVRAM=1` starts the native VRAM as the cartridge's at the hand-off (the field's
leftovers, which the race never writes and the port's field does not keep).

**Equal on every frame of the race** -- 2738 NMIs from the setup's to the END: state, the race's WRAM (with bank $7F) and every
pixel (`race-picture-native-vs-oracle`); without the seed 6765 VRAM words differ and not one pixel
(`race-picture-native-unseeded-vs-oracle`): no region the race displays shows the leftovers.

What is ported:

- the setup ($7E:3D8C, $7E:3B5D-3C4E, $7E:3CEA-3D44 with C3/09E9's code at $7E:B574, $7E:388B, the common init's PPU reset);
- the vblank hooks `380B`, `3843`, `5221`, `43F2`, `44FE`, `4643` / `464D` and the finish's `449C` (the sky's scrolls from the
  camera);
- the tasks `42CE` / `357B` (the road's self-patching matrix), `428B`, `4447`, the fades, and:
  - `4D0D`, THE BIKES' PROJECTION: the turn's sine / cosine, C3/094C's signed 16 x 8 products (⚠ `$F6` is not cleared: the 8-bit
    `STZ $F7` and the 16-bit `ADC $F5`), the depth's screen row through the hardware divider's search C3/0E29 (from `$B4` up,
    D = $4200: the first divisor whose quotient falls under the row, cached in `$1446,row`), the restoring divide `$32E2` (its
    first `ROL $F0` takes the caller's carry) and `$33D6`; the carry threaded through every `ADC` / `SBC` with no `CLC` / `SEC`
    -- the task enters with the dispatcher's (`$7E:398A` restores the flags of its `CPX #0020`: clear for every slot but the
    first);
  - `4F54`, THE HUD AND THE SPRITES through C3's OAM library over the shadow $0700 / $0900 (`0D0E` small, `0D5E` large with a
    9-bit x -- from $100 the word `$9801` -- , `0DBC` one record): the minimap markers (⚠ 50D3's 16-bit `SBC $4217` reads
    `$4218` too, the pad's auto-read: CtSeqStagePic keeps it), the boosts, the panel, REST (⚠ its `SBC #imm` at 5170 is the
    routine's own byte $7E:5171, stepped down every 64 frames of `$BC`) and the score as digit sprites (`$39F5`); the two bikes'
    frame requests ($1400.. / $140E..) and lists; ⚠ the finish writes `RTS` at $7E:510D (485D) and the markers end there;
  - `44CE` (logic, seq_race.c): the score counted down after the finish, 2000 / 200 / 20 / 2 every 4th frame.
- THE MAIN THREAD `$7E:51D1` (CtRacePicThread): the loop `3023 JSR $51D1 / LDA $00 / BEQ` runs after each NMI (the infinitely fast
  CPU: done before the beam moves) and again while a pass did work. A request (`$1402` / `$1410` = 1) is scaled by C3/0307 into
  $7F:0200 (one byte a pixel, 64 a row, from `$23`; rows stepped by `$27`, columns by `$29` = 2 x `$27` in 8.8; 8 pixels -> 4
  bit planes; a repeated row copied; the rows down to the last frame's end cleared), then -- once hook `5221` has sent the last
  one (`$39` = 0: the pass waits at 5211 across NMIs, `sp->thread`) -- `$39` raised, the rows reformatted into the tiles at
  $0C00 by the code at $B574 (C3/09E9's walk: 1024 `LDA $src / STA $dp`, the offsets C3:09D9 x 4 x 2 x $20), and the request
  marked sent (2). Hook `5221` sends $0C00 to VRAM $4000 / $4400 when 4FAD / 5047 raise `$AA` / `$AC`.

The attract's arm (`$0384` bit 6, `FF 60`) since 2026-10-18: the curtain task `3D72` (C3/01E4 native, C3/02DD over the virtual
beam, HDMA channel 0 on the half it wrote) and the button `424D` -- sequence_table.md sec. 18; graded every frame, the
attract's race in play 424 / 424 under the rule. Entry 3's `FF 63` the same way (task `6844`). THE WIDESCREEN (a mod): the
compositor draws the sides too (seq_pic.h g_seqPicWideSide, snes/ppu.c's window rule: a curtain crosses the whole picture). Paths this drive never takes (their mutations
survive): a sprite past x 255, a repeated source row (a zoom past 1), a column step's carry, $449C's LSR carry, a score on a
step's exact value at the finish.

IN PLAY (2026-10-17): the field gives the race its picture (src/field.c g_racePic, reset when the race opens) and draws
CtSeqStagePicFrame after each NMI. The user's drive in the port is the cartridge's pixel for pixel at the field's alignment
(port P = cartridge `--peek` P+2): port f400 / 2760 = cartridge f402 / 2762, 57344 / 57344 each
(race-story-picture-midrace-in-the-port-vs-oracle, race-story-picture-finish-in-the-port-vs-oracle). That needed the opening
priced whole: the song $2C's post inside the setup (`JSR $375F`, its JSL C7/0004 with the NMI off) costs the SPC time the
host's driver takes, 10177261 cycles here, so the stretch is 73 frames -- the cartridge's (f66 -> the first NMI at f139);
without it the port opened 29 frames early.

## 5. Entry 3 from the story (src/seq_entry3pic.c) -- DONE (2026-10-17)

`FF 23`, the night panorama after the Blackbird (sequence_table.md sec. 7.2 / 14). `--racepiccmp` runs any stage whose picture
is ported, by its operand (`... ff23_handoff_f25.state 0:3000 0 23 600`); the setup's NMI is found the same way for every stage
(the operand in `$0384`, `$0008` cleared by the common init, then its first `INC $08`).

**The stage draws with the FIELD's VRAM.** Clearing each region of the seed in turn (`RACEPICCMP_SEEDZERO`), three change the
picture and nothing else does: the location's tiles $2000-$4FFF, BG1's map $6000-$67FF (the setup reads its rows back from
$6140) and BG2's maps $6800-$6FFF (HDMA channel 3 switches BG2SC from $60 to $6A at line 70). So the stage's CtSeqStagePic
starts as the console's VRAM: the oracle's in the harness, the field's image (g_fieldVram) in the port.

What is ported: the setup -- M7A = $0100 / M7B = 1 and a MODE-1 `B -> A` DMA from $2134 / $2135 (the product's low and middle
bytes in turn: 00 01) into $7E:1400; $2116 = $6140, a dummy `LDA $213A` (VMAIN 0), VMAIN $80 and $580 bytes from $2139 / $213A
in turn into $7E:1680 (CtSeqStagePicVramReadWords: the buffer refills where VMAIN bit 7 says, the first word twice); BG12NBA
$11; after the script $783C sends $7E:1400 as BG1's map at VRAM $0000, $36CC installs the HDMA from $7E:685E, the layers'
registers (mode 2, W12SEL $33 -- $FF on the attract's arm --, TM / TS 13 / 12, the window) -- the hooks $388B / $380B (shared)
and $66DD (BG3's offset-per-tile row from $0043 to VRAM $7000, BG3 H / BG1 V scrolls), the tasks $73C6 (the sine and cosine
through the PPU's multiplier into $1F..$2D) and $6A23 (BG1's per-line H scroll at $7E:F000, r0 and 0 swapped every frame), the
logic tasks' TM (6A9F) and mosaic (6A16) writes. Two stage-1 bytes were wrong and are fixed: `STZ $7841` is 16-bit (6775
REP #20: $7842 too), and $37FA's `LDX $4202` reads a write-only register -- the open bus, the instruction's last byte: `$F0` =
$4242.

**Equal on every frame**: 582 NMIs from the setup's to the END, state, the stage's WRAM and every pixel
(`entry3-picture-native-vs-oracle`).

**In play** (field.c): the stage is given g_stagePic (opened with g_fieldVram) and drawn after each NMI. A seeded start
(`--loadwram`) skips the location's entry fill, so its VRAM image had the tiles and no tilemaps: `fieldSeedVramTilemaps` builds
them from the seed's map as C0/0A73 would (the VRAM image only). The story drive then shows the panorama, 56614 / 57344 pixels
the cartridge's (`entry3-story-picture-in-the-port-vs-oracle`, a ratchet), with two residuals of the FIELD, not the picture:
- the stage runs 3 frames early (the port's f200 = the cartridge's f205, P+2 and 3): the `FF 23` call comes early because the
  seed (ff23_story_f18) was captured inside the location load's ramp C0/2848 (`$0119` 01 rising to 04 on the cartridge), which
  a seeded start does not resume, so the op's own fade starts from 01 -- an artefact of the seed, not of a played load. (The
  opening is the cartridge's 18 frames since it is priced whole -- sequence_table.md sec. 15; it was 17, the stage 6 early);
- the water's and the city's colours: palette records 2-11 (the field's colour cycles FD/E39C keeps running) one step behind
  the cartridge's from the first frame (sequence_table.md sec. 14) -- OPEN; likely the same seed's doing (three field frames
  fewer before the op), not measured.
Not exercised: the mosaic ($C4 is 0 on the story's arm).

## 6. Entry 4, Lavos falls (src/seq_entry4pic.c) -- DONE (2026-10-17)

`FF 24` (sequence_table.md sec. 15): mode 7 (the planet's map and characters), the stars (OAM, the logic's $579B) and Lavos
burning in (task $56EB, C3/0D5E's list $7E:56FD at y $70). The setup $7E:53BB: packet $2D's bytes split into nibbles through
WMDATA by $350E (with D = $2100 it moves the stack onto $0C00 and PLAs the bytes; the old S, $06F9, stays in `$00FE`) and sent as
VRAM's HIGH bytes from $0000 (the mode-7 characters); the $FE-table's $3F ($60 tile numbers) into $7E:0C00 by $36AA, packet $42
into $7F:0200 and $54EA's loop -- each number read through WMDATA picks a 32-byte tile, 16 words to VRAM from $4000; packet $33
to $4600; packet $30's 64 x 64 map rows doubled with 64 zeros each by $37CA ($7F:2200..), $380 bytes of it to VRAM's LOW bytes
from $0000 (VMAIN 0), the rest ($3C80) filled from the one byte $7E:350B by $90A7 (DMAP $08, a fixed source); the palettes
($36AA: $FE-table $3C and $36 into $7E:2200 / 2300, $2200-01 cleared); TM $10 before the script, then mode 7, M7SEL $80, the
bases, CGWSEL 02. Hook $5654 (D = $0A00): M7A-M7D from r10-r13, M7X / M7Y from r3 / r4, BG1 H / V from r1 / r2.

**Equal on every frame**: 1711 NMIs to the END, state, the stage's WRAM, every pixel (`entry4-picture-native-vs-oracle`;
unseeded the picture is the same -- the stage uploads all it shows). **In play** (the Tyranno chain's drive): P+2 from the op,
Lavos to the pixel; the stars differ because they come from C3/02DD over the field's `$0386` at the hand-off and the two drives
(the port's tele301_req, the cartridge's s301_c1_hp1) leave it differently (`entry4-story-picture-in-the-port-vs-oracle`, a
ratchet).

## 7. Entry 7, the Day of Lavos (src/seq_entry7pic.c) -- DONE (2026-10-17)

`FF 07` (sequence_table.md sec. 16): mode 3, the planet, the sparkles and the text "BUT... THE FUTURE REFUSED TO CHANGE.". The
setup $7E:6248: `$0303-$0305` = $7F:0200 ($89ED); packet $63's 2bpp characters widened to 4bpp through WMDATA by $66B0 (two bytes
a row to $2118 / $2119, then eight zero words a tile; WMADD in the bank C3/0557 left) at VRAM $4000; packets $5D / $CF / $B1 to
$4800 / $5000 / $7000 by $38E7; the palettes into WRAM -- packets $54 / $57 / $5A / $60 at $2020 / $B574 / $B774 / $21C0, $8772's
colours $21E2-$21E7 and `$2000 = 0` -- and $38AF's MVN of $7E:2000 into the CGRAM shadow $2200; after the script mode 3, BG1SC
$70, BG12NBA $05, BG1 at ($FFB8, $FFCC), CGWSEL / CGADSUB 02 / 01, TM / TS 01 / 10, TMW / TSW 1F. The hooks are the common three
($380B, $3843, $388B every frame). Task $64BF draws the string $7E:6644 (`40` ends, `00 x y` moves the pen and rewrites the
code's own operands $64D6 / $64DA, `2F` a new line, `20` a space; a letter from $664F + c, attr $3E, width from $662F + c: from 9
up one 16 x 16 sprite, else two 8 x 8 with the tile + $10 below). The sparkles: the logic's records drawn through C3/0DBC from
$7E:6640 inside its loop. Found on the way: $37FA's `LDX $4202` open bus (`$F0` = $4242, as entry 3), and the hand-off is the
op's own frame -- the script clears glb[170] bit 5 ($7F:00AA, C0/6BCE) on the frame it calls `FF 07`.

**Equal on every frame** to the console's reset (995 NMIs; `entry7-picture-native-vs-oracle`; unseeded the same picture). **In
play**: 300 frames after the op the whole frame is the cartridge's at P+2 (`entry7-story-picture-in-the-port-vs-oracle`).

## 8. Entry 6, the Epoch into the shell (src/seq_entry6pic.c) -- DONE (2026-10-17)

`FF 26` (sequence_table.md sec. 17). **Part 1 (2026-10-17)**, VRAM / CGRAM / HDMA the cartridge's from the first NMI to the END
(`entry6-picture-native-vs-oracle`, a ratchet): the setup $7E:78DD -- packet $66 as the mode-7 characters (VRAM's high bytes from
$2000), packet $69's map doubled ($37A6) and sent twice to VRAM's low bytes ($38C3), the palette page (packet $6C, a MVN, $36AA's
$6F, `$2000` = $6F69, $38AF), $7A68 (the window circle's table pointer `$0356` = $7E:E060, $FF - sin / 2 at $E260, the ramps at
$E000 / $E030, packets $72 / $7B to $4400 / $5000, the backdrop's map at $6000 from packet $75's attributes, $7F:0200 cleared from
$2134), packet $78 (the Epoch's frames) at $7F:0A00 and C3/09E9's code at $B574; the hooks $81C4 (BG1 scrolls, the mode-7 centre),
$824A (the Epoch's frame to VRAM $4000), $7F58 (HDMA table bytes), the PPU halves of $7A35 (the dive: mode 1, TM $11) and $79F8
(the turn: mode 7), each installing HDMA by $36CC; the main thread $8278 (C3/0307 over $7F:0A00, `$1F` 4, `$25` $21, `$2B` $400, then
$B574 -- the race's, shared); the task $82D1, the shared $357B with the pre-routine its `JSR $35FD` names: $8042 (the table in two
halves on alternate frames -- `DEX / DEX` from $FF to the limit $01C0 with page A's r7 + 1, `INX / INX` from $20 to `$46` x 4) and
$801F (after the turn: each line's distance read through WMDATA from $E260, the tail `BRA $3662` copied from $368D). The harness
answers a script's `F5` with the oracle's own SPC, so the END is the cartridge's (1611).

**Part 2 (2026-10-17)**: the sprites. $7F7D (the Epoch's glow: three lists through $3A97, a list drawer read through WMDATA with a
9-bit x -- off the left edge bit 8 set, off the right edge y = $E0 -- whose exit A and carry the next RNG takes), $8205's own draw
(`LDA #82B0 / JSL C3/0D5E`, missing from the logic), $7FCB (C3/0077's midpoint circle into the window table $E060, mirrored, and the
HDMA bytes $7C5C..; then $E0E0 from it + page B's r1), $8079 (the lightning: two tile words from $80E1 by a 16-bit draw, four
sprites through WMDATA at $8112 from $80E9 by 8-bit draws), $835B (the explosion's frames from the table $84F6), $7BF2 (an HDMA
table opening, then channel 0's table address written at $4302).

⚠ C3/02DD WITH A 16-BIT A reads its three absolute operands as WORDS IN THE CALLER's DATA BANK. Under $00 (the dispatcher's): `CMP
$2137` = the CPU's open bus $21 and OAM data at the OAM address ($2138, advancing it); `ADC $213C` = the virtual beam ($0351, 0);
`SBC $213D` = 0 and STAT77 (1 | the PPU1 open bus' bit 4 | range over << 6 | time over << 7). So the compositor keeps the OAM
address as the PPU does ($2102 / $2103, $2104 writes and the DMA through them, $2138 reads; put back at a vblank not in forced blank
-- `$380B`'s `STX $2102` writes $2103 too) and the last frame's sprite flags (set while evaluating, cleared at the frame's start);
sprite evaluation starts at the OAM address under priority rotation. Under $7E (`$8079`'s `PHK / PLB`) the same operands are WRAM's
$7E:2137.. -- no PPU read at all. And the logic's $37FA (`LDX $4202`: the open bus, $4242) stores on its caller's direct page: $00F0
for the task, $0AF0 / $0BF0 for a script's call (`$00F0` keeps the call's target there).

**Equal on every frame**: 1611 NMIs to the END, state, the stage's WRAM, every pixel (`entry6-picture-native-vs-oracle`; unseeded
the same picture). **In play**: 429 frames after the op the whole frame is the cartridge's at P+2
(`entry6-story-picture-in-the-port-vs-oracle`). Not exercised: CMP $2137's low byte deciding a carry, a sprite past x 255 in
$3A97, a circle's edge past $FF.

## 9. The ending (src/seq_endingpic.c) -- all four entries DONE (2026-10-17)

One setup, $7E:8538, for entries 10 / 11 / 14 / 15 (sequence_table.md sec. 13); its logic is seq_ending.c.

**The setup's picture** (8538-86A1): packet $63's 2bpp tiles widened at VRAM $5000 by $66B0 (entry 7's), C3/09E9's code at
$7E:B574, the mode-7 characters (packet $7E, VRAM's high bytes), the map (packets $81 / $D2: $37CA's rows -- 64 bytes and 64 zeros --
sent to the low bytes by $8752, twice), $4000-$4FFF's low bytes the byte at $7E:350B ($90A7), packet $87 at $4000, entry 10's own
picture (packets $BD / $B4, 320 tiles of 32 bytes picked through WMDATA, to $5C00), the palettes ($36AA's $96 / $99 / $9C / $9F / $90,
$8772, packet $8A at $2180, $38AF). After the script: $9BB8 (the two shooting stars' scripts: $9BD8 picks one of $9C4E's by the RNG
$3448, or $A60A once `$9F` is set), $36CC's channels from $8FEA (M7A-D indirect from $355F / $356D, BG1's scroll from $9043, TM-TSW
from $904D, BGMODE from $902F, the window from $9070, CGADSUB / COLDATA from $452B), the layers (mode 7, M7SEL $C0).

**Page A's first op** `F0 87CC` (`F0 8790` reloads the map first: packet $84 doubled by $37A6, its VMAIN / VRAM address / M7SEL from
`TDC` -- D's low byte): the main thread's call `$3024` = $8930, HDMA off, the world's sprite sheet (packet $D8 at $7F:0A00, its
16-row blocks from $7F:4E00 up mirrored into $7F:5600 by $3ED3 -- the data bank $7F: the stride `$0200` is $7F:0200), `$0037` =
$0A00, the palettes $A5 at $2100 / $2300 (`$F0` on the script page's direct page).

**The hooks**: $9DBC (`$7B` clear: the mode-7 centre from $1408 / $140A and the scroll origin $1404-$140E; set: $5654 with page A's
r1 / r2 + $80), $9080 (the HDMA tables' line counts from r6 / 2 and + the LSR's carry), $9AF3 (the orbit: page B's r6 as the radius,
$33D6's signed products of cos / sin r5), $89C0 (the main thread's sheet, `$2B` bytes of $0C00, to VRAM $5800), $8850 (the planet:
C3/0D5E's list $88B9 / $88DA, the window's HDMA bytes). **The tasks**: $9ABD (`$7B` set: one matrix, $56B4 -- entry 4's; else
$357B with the pre-routine $9B46 only when r0 / r5 / r6 / r8 changed), $9BF9 (the shooting stars through $A03D: a list read through
WMDATA, its `BCS` at $A074 patched to `NOP / NOP` while the task runs and put back from `$F0`), $8850, the credits' pages ($9B4F ->
$64CA, entry 7's text drawer: its attribute is the operand at $6525, which the setup sets to $3F). **The main thread** $8930: the
sheet scaled by page B's r0 & $1FF (C3/0307, `$25` / `$2B` by `$7B`, `$4C` = $800 / r0 by the divide $32E2 -- whose first `ROL $F0`
takes the last pass's carry, the `CPX #0100` -- and $B574), the planet's list counts by the size.

The logic took the WRAM-only pieces it lacked: the tasks $37FA (the colour animations $0520..), $6439 (entry 7's palette fade),
$99B5, $9D48 (the twinkle: four colour pairs swapped every 8th frame in $2000 and $2200), $891B, and the calls $9919 (the credits'
pen into $64CA's operands), $9FC8 / $9FEA (colour animations), $98F6 / $98FB / $9900 ($6439's request).

**`FF 2B` equal on every frame**: 4746 NMIs to the END, VRAM / CGRAM / OAM / the registers / HDMA and every pixel
(`ending-ff2b-picture-native-vs-oracle`; unseeded the same picture -- only VRAM the stage never loads differs). One WRAM byte differs
from the first frame: $7F:0076 (cart 06, port 04), already 06 when the cartridge enters C3:0000 -- the field's frame after the op,
not the stage's (`--hookpc C30000 --hookwatch 10076`).

**`FF 2E`'s last screens** (page A from $9613: `F0 8790`, `F0 9702` -- the world's palette copied whole, $C995 -> $2200, $101
bytes -- then FF 2B's world; `F0 96FB` points the main thread back at the RTS $3176):
- **the stars** `F0 8BAF`: the lists cleared, `$038C` = 0, entry 4's $5777 (A = $80, the carry clear: the `F0` op's dispatch C3/0B6F
  ends on `ASL` of op & $0F), packet $33 to VRAM $4600, the palette $39 at $2120. Task $999C: once, the first 64 sprites kept
  (`$11` < $100: $3870's high-table byte, `$11` = $100, `$13` = $0910), then entry 4's $579B with X = 4 ($3870 leaves it) and the
  carry set; hook $8C05 clears the high table $0900-$091F every frame (`MVN $00,$D2`).
- **the planet** `F0 8C14`: packet $CF to $6000; the 8bpp mode-7 characters -- the call PATCHES THE OVERLAY's CODE ($5E44 = $5EF8,
  the step at $5E4A = $30) so $5E3C runs $5EF8 (eight planes rolled out per pixel into $2119) instead of $5E54 (four); packet $B1
  to $4400 and $5AB2's map (32 rows of every second byte through WMDATA from $0200 in the bank C3/0557 left -- `$0305`'s, the
  unpack sets `$2183` from it -- each row padded to 128 with zeros, then $1000 fill bytes); the palette $54 at $2020, $8CB2, $38AF,
  $388B. Hook $9E2B: mode 7 without a turn (M7A = M7D = page A's r0).
- **"The End"** `F0 8CD0` (entry 15 leaves here, $0A20 = $9315): packets $AE / $8D / $C0 to $7F:0200 / 0400 / 0F00; a WMDATA fill at
  $1000 of `x, 0, x+1, 0, ..` (its `STY $7F` with D = $2100 also writes 0 to $217F, the SPC's port 3 -- not modelled). Hook $8D56:
  $7F:0200 to $4000, $4800's low bytes from $350B and its high from $7E:9E7A, HDMA from $9023, mode 4. Hook $9EFC: the rolling
  text's line ($1414, 32 bytes) to `$48`'s row and BG2's scroll `$46`; $9E7B's 9ED6 moves `$46` by 8 with `$AD`, else by 1 on the
  frames the PPU's field bit is set (`LDA $213E` 16-bit: $213F bit 7). Hooks $9F2B / $9F39 / $9F47 / $9F73: one transfer each
  ($7F:0400 -> $5000, $7F:1000 -> $4C00, $7F:0F00 -> $0000, the fill byte to $20A0) and out ($39DE).
- **the last planet** `F0 8DF3` (palettes $A2 / $AB on the script page's `$F0`, $38AF; the logic's row clear and pan): task $9A0C
  writes the quad's corners at $0360-$0367 and runs C3/0EFA into $7E:E000 (ct_vm.c CtPolyQuadImage, the field's port of it), and
  while `$71` its sprite ($3A97's list $A1E2 at `$71` - $0C, `$73` / 4); task $99FA C3/0D5E's $A1A1 at ($3C, $90); hooks $8E44 (the
  layers, COLDATA $E0, HDMA from $9029, `$038C` = 1, out) and $8E88 (BG2's scroll = page A's r2). `F0 9F8A` the last colour fade.

**`FF 2E` equal on every frame**: 11000 NMIs, through "The End" held (`ending-ff2e-picture-native-vs-oracle`; unseeded the same
picture). The harness answers `$213F` from the oracle's PPU: `racepicField`, the field the oracle's NMI k read -- the harness steps
the oracle out of that vblank first, so its PPU already holds the next frame's.

**In play** (the field: `CtEndingPicHas` -- entries 11, 14, 15; `raceIoFor` now gives every stage `io.field`, flipping each frame, a
cold boot's phase): `FF 2B` pixel-exact at P+2 from the op (port f1500 / f4000 = cartridge f1501 / f4001,
`ending-ff2b-story-picture-in-the-port-vs-oracle`); `FF 2E` through the reboot chain exact at port f8000 = cartridge f8000, then page
B's wait on the song's marker ends two frames early in the port (f8679 vs f8681: the port's SPC in play, the class of
spc-phase-sensitivity; the harness takes the oracle's ports and is exact), and from the star screen on every frame checked is the
cartridge's at +2 (`ending-ff2e-story-picture-in-the-port-vs-oracle` pins f12800 / f12802, the rolling text).

**Which entry the game plays** (traced on the cartridge from lavos_core_defeated, `--cputrace` C0/69BA): scene 052's obj0 stores
`glb[$6E]` (op at C0/69A6, `$7F0000,X` = imm): `16 6D 80 06` tests `glb[$6D]` bit 7 and `CF 00` Crono in the party -- 1 (bit 7,
Crono), 2 (bit 7, no Crono), 3 (no bit 7, Crono: Jackie's run), 4 (neither). Scene 81's obj0 runs `FF 2A` when bit 1 of it is clear
(1, 4: `16 6E 02 00 08`), else the talk's `FF 2B`; scene 90 runs `FF 2F` when bits 0 and 1 are both clear, else `FF 2E`.

**`FF 2A` and `FF 2F`, graded** from the FF 2B / FF 2E hand-offs with the op's operand byte in the scene's script changed
($7F:2983 = $2A, $7F:2268 = $2F; saves/ending_re/*_ff2a / *_ff2f.state): the same world and song, entry 10 / 15's code.
Entry 10's own pieces: the setup's 85E1-864F (packet $BD, packet $B4's 320 tile indices read through WMDATA -- in $7F: THE UNPACK
C3/0557 SETS `$2183` FROM `$0305` -- each a 32-byte tile copied into $7F:D800.., to VRAM $5C00), the hook $9CE2 (the sprite's frame by
`$A3` & 3 from the table $9D25 to VRAM $5800, `$AB` / `$AC` its size, then out), the task $9C58 (the frame again when `$A3` changes,
the sprite at -(page B's r1 / r2 + `$6D` / `$6F`), its window's HDMA bytes, every 4th frame one of three tiles written into its list's
own bytes $A0F6 / $A0FA, the list through $A03D), $9D45 (r5 down, then $9D48), the calls $8A01 (its planet: packets $C6 / $C9 / $CC,
the 4bpp-nibble characters of packet $B7 through $353C -- low nibble first -- the map of packet $BA, the palette $93, HDMA from $9003)
and $8AF5 (HDMA from $900E, the tables' counts $64, BG2's scroll $FF70), the hooks $8B52 (the tables' counts from r5 / 2, BG2's
scroll, $140C / $140E) and $9E19 (BG4's scroll, then $9E2B).

⚠ $A03D WITH ITS `BCS` LIVE (entry 10's $9C58; $9BF9 patches it out): a skipped sprite (y >= $E0) jumps back to the loop's top OVER
the `SEP #20` -- the rest of the list runs with a 16-bit A: `DEC $19` a word, each `LDA $2180` the byte twice (the high byte is
`$2181`'s open bus, the byte just read), each store two bytes (the tile into $0702 / $0703, the attribute into $0703 / $0704, x +
`$15` a word into $0700 / $0701), until a kept sprite's `SEP #20`. Measured on the cartridge (sprite 4's x alone written).

**Equal**: `FF 2A` 15000 NMIs, through "The End" held (`ending-ff2a-picture-native-vs-oracle`; one WRAM byte, FF 2B's hand-off's
$7F:0076); `FF 2F` 3968 NMIs to its END on the cartridge's NMI (`ending-ff2f-picture-native-vs-oracle`). The logic
(`ending-stage-logic-ff2a-ff2f-vs-oracle`): both byte for byte -- FF 2F to the END, FF 2A for 11956 frames, page A's wait on the
song's 8th marker included. (That wait came out 5 frames a frame early while the port's SPC ran 17,088 cycles every frame: the
console's odd frames are 4 master cycles short -- snes.c's line 240 -- 17,087.81. The port's SPC runs that clock since 2026-10-17
(audio.c apuFrameCycles); the attract's checks it moved were renumbered a frame or two: overworld_entry_sound.md sec. 8.) In play the field draws all four (CtEndingPicHas); entries 10 / 15 are not
compared in play (no drive reaches them: the oracle walked the `glb[$6D]` branch from the core to the fair, saves/ending_re/a1_*).
