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

## 3. Next

Each stage's code that FILLS the compositor -- its setup's uploads, its NMI's register writes and hooks (OAM, CGRAM, VRAM DMAs),
its picture tasks -- ported natively over the stage's WRAM, graded on VRAM / CGRAM / OAM / the registers frame by frame and then
on the picture. The race first (sequence_table.md sec. 12).
