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

Not here: the attract's arm (`$0384` bit 6, `FF 60`): its curtain task `3D72` (C3/01E4 over the virtual beam, C3/02DD) -- the
native stage does not open that arm yet (the attract runs the translated stage). Paths this drive never takes (their mutations
survive): a sprite past x 255, a repeated source row (a zoom past 1), a column step's carry, $449C's LSR carry, a score on a
step's exact value at the finish.

IN PLAY (2026-10-17): the field gives the race its picture (src/field.c g_racePic, reset when the race opens) and draws
CtSeqStagePicFrame after each NMI. The user's drive in the port is the cartridge's pixel for pixel, 31 frames later on the
cartridge -- the opening's residual (the port prices 44 black frames before the setup's NMI, the cartridge takes 75: the setup's
song handshake), not the picture's: port f150 / 400 / 1000 / 2000 / 2700 / 2760 = cartridge f181 / 431 / 1031 / 2031 / 2731 /
2791, every one 57344 / 57344 (race-story-picture-midrace-in-the-port-vs-oracle, race-story-picture-finish-in-the-port-vs-oracle).

## 5. Next


Each stage's code that FILLS the compositor -- its setup's uploads, its NMI's register writes and hooks (OAM, CGRAM, VRAM DMAs),
its picture tasks -- ported natively over the stage's WRAM, graded on VRAM / CGRAM / OAM / the registers frame by frame and then
on the picture. The race first (sequence_table.md sec. 12).
