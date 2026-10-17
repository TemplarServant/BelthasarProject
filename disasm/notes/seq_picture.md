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

## 4. The race (src/seq_racepic.c, in progress)

`ct.exe --racepiccmp <state> <btnscript> <handoff> <operand> <frames>` lays the native stage against the oracle NMI by NMI from
Johnny's `FF 00` (saves/race_re/race_user.state, A at f30, the hand-off at f65): VRAM, CGRAM, OAM, the registers, the HDMA
channels and WRAM as the oracle leaves vblank, then the pictures. `RACEPICCMP_SEEDVRAM=1` starts the native VRAM as the
cartridge's at the hand-off -- the field's leftovers, which the race never writes and the port's field does not keep (a
region the race displays would show them; none measured so far).

Ported and equal after the setup's NMI: the setup's uploads ($7E:3D8C -- the OBJ clear, the palette packet, the mode-7
characters and map (packets 15, 18, 1B; `$7E:37A6` doubles each map row), the sky's characters and the three BG maps;
$7E:3B5D-3C4E -- the bikes' packets 06 / 09 / 0C, the 8 x 8 blocks of `$7E:3EB7`, the mirrored streams of `$7E:3ED3` through
WMDATA, the multiplier fills, the HDMA channels from the record table $7E:3D45; $7E:3CEA-3D44 -- the layers' registers and the
start line's column read back into $7E:C995; $7E:388B), the common init's PPU reset ($7E:30F9), the tasks `42CE` / `357B` (the
road's per-line matrix: cos / X and sin / X through the hardware divider; the routine patches its own loop -- the products
as immediates, the negation as a branch, the sine tables' order, a tail that duplicates lines -- and those bytes are written
too), `428B`, `4447` and the fades, the hooks `380B` (OAM, the priority rotation), `3843` (the OAM shadow cleared from the
multiplier), `5221` (the bike frames' VRAM), `43F2` (the mode-7 centre, BG2 / BG3 scrolls), `44FE` (the curtain's table bytes,
$7E:E100 to VRAM $5180), `4643` / `464D` (the start line's column from $D2:0000 or $7E:C995). Check
`race-picture-native-vs-oracle` (a ratchet).

Not yet: the bikes (`4D0D`, ~770 instructions: their projection and sprites), the HUD's drawing (`4F54`), the bike frames (the
main thread's `$51D1`: C3/0307 decompresses a frame, the code C3/09E9 wrote at $7E:B574 reformats it, hook 5221 sends it), the
opening and closing curtains (`3D72`, C3/01E4 over the virtual beam). Then the field draws the race through it.

## 5. Next


Each stage's code that FILLS the compositor -- its setup's uploads, its NMI's register writes and hooks (OAM, CGRAM, VRAM DMAs),
its picture tasks -- ported natively over the stage's WRAM, graded on VRAM / CGRAM / OAM / the registers frame by frame and then
on the picture. The race first (sequence_table.md sec. 12).
