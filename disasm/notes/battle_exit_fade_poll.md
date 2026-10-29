# The battle exit's SPC poll (CD/0428-0436) and the fade it waits for (2026-10-29)

The residual named on route leg 20 (check `playthrough-legs-vs-rule-cartridge`): the port's CD/0432 `LDA $002141 / BNE`
let go a frame or two off the rule's cartridge on some fights. **Measured: the poll is not the cause.** On every exit
measured, the port reads the SPC at the cartridge's instant. Where the release moves, the fade it waits for already runs
on another lattice. The lattice moved during an earlier song upload: the upload's pace in bank C7 is not the cartridge's.

## 1. What the poll reads -- the driver's fade count

CD/039E posts `81 10 00` (command $81: fade the music to volume 0 over $10 x 7 ticks; the driver's $0E2F stores the
count in ARAM `$F166`). Timer 0's tick ($02A0) writes port 1 with that count on every tick (`$02DB`: `$F4 := 00`, `$F5 :=
$F166`). The fades run every 7th tick (`$02E5 DEC $BD / BNE`, `$BD` reloaded with 7). So port 1 counts 10, 0F, .. 01, 00,
one step per 7 ticks (~37,600 APU cycles, ~2.2 frames). CD/0432 reads it each frame, after the exit NMI D1/F4C0, at line 225 under the
rule (the beam held from the vblank).

During an upload's kind-5 blocks the driver runs `$0415` (uploadTick) between bytes when timer 0 reads due. It runs the
fades and decrements `$BD` too. Every other block leaves timer 0 unread, and the upload's end drops the count (`$0CF1 MOV
A,$FD`). **So how many times `$BD` steps during an upload depends on how long the upload's kind-5 stretch lasts against the
timer**, i.e. on the CPU side's pace in C7.

## 2. Measured, leg 20 (the three forest ambushes), both sides from the leg's state

Cartridge: `ct.exe --peek` with `PEEK_APUTRACE=1:12124 PEEK_ARAMBYTE=BD` (the SPC's `$F5` stores, `$BD` at each frame's
end). Port: `CT_AUDIO_PORTTRACE=1 CT_AUDIO_SPC=ref CT_AUDIO_ARAMBYTE=BD`, with the walkscript and seed of `tools/playthrough.py cmp`.

| exit | `81 10 00` (apuc) | `$F5 := 00` port - cart | first post after the release, port - cart |
|---|---|---|---|
| 1 (f3263) | 946,411,411, port -1 | +3 | -5 APU (same frame) |
| 2 (f8627) | 1,038,070,927, port +2 | -1 | -1 APU (same frame) |
| 3 (f10947) | 1,077,714,869, port -4 | **+26,878** | **+34,179 APU = 2 frames late** |

On exit 3 the fade's first step comes 2 ticks after the post on the cartridge (10 at 1,077,718,094, 0F at 1,077,728,839)
and 7 ticks after it on the port (0F at 1,077,755,715). `$BD` is one tick apart. Per frame, `$BD` agrees on both sides
from the leg's start to f8704 (except for one-frame blips where a JSL ran the port's SPC ahead inside the frame). It stays
apart from **f8705**, the frame after exit 2's restore chain uploads its song (C0/1BE6's `11 00 FF 05`, f8662-f8703).

## 3. The upload's pace

The uploads' last token (`$2141 = E0 / $2140 = E0`, C7/0609-060C) on leg 20, port - cartridge in APU cycles. The starts
agree within 8 cycles.

| upload | cart frames | end, port - cart |
|---|---|---|
| battle 1's set (`14`) | f1001-f1052 | +205 |
| exit 1's restore (`11`) | f3299-f3340 | -1,537 |
| battle 2's set | f6217-f6259 | +1,049 |
| exit 2's restore | f8662-f8703 | -1,471 |
| battle 3's set | f9531-f9573 | +122 |

Exit 1's restore and exit 2's restore drift alike, about -37 APU (~-720 master) cycles a frame. Exit 1's happened to cost
no tick, exit 2's cost one.

The frame's budget, cartridge (`--hookpc C709E5 CT_HOOK_CYCLES=1`, f3300-f3324) against the port (`CT_AUDIO_LDFRAMES=1`):

| | cartridge | port |
|---|---|---|
| C7 main + poll | 331,316 - 331,644 | 331,720 |
| HDMA | 14,088 - 14,560 (mean ~14,273) | 14,112 |
| refresh | 10,480 | 10,440 (the one inside the IRQ's wait is not charged: it steals nothing) |
| units a frame | 275.0 | 275.6 |

The port's restore upload charges FD/C1EE's six channels 64 master cycles a line (`CT_AUDIO_HDMALINES=1`): six count
bytes, 8 of overhead and `LD_HDMA_SYNC` = 8 for snes_syncCycles' two alignments (160 / 240 on the two entry lines). The cartridge's average works out to an alignment of
~8.76 here (8.03 and 8.33 in the two contexts `LD_HDMA_SYNC` was measured on). In LakeSnes the run starts at the first
CPU access after h 1104 (dma_handleDma before the access). It aligns to the absolute clock (`8 - cycles % 8`: 2-8),
transfers, then re-aligns to the access it interrupted (`cpuCycles - elapsed % cpuCycles`). With this 56-cycle run, a
6-cycle access (ROM, B-bus, internal: the whole poll loop) totals 4 or 10, and an 8-cycle WRAM access 8 or 16. The port
also inserts the run at h 1104 itself, inside an instruction, not at the next access.

**The phase is not the port's to know.** Under the rule the hold advances `snes->cycles` (snes/snes.c, `snes->cycles += 2`
while the CPU works), so each frame's held NMI work moves the 8-cycle phase by an amount no beam position or listing
gives. EXPERIMENT (not a fix): a mean of 8.76 a run in the port (`CT_AUDIO_HDMASYNC=8.76`, a measurement switch) puts all
three of leg 20's releases on the cartridge's APU cycle (within 7). The restore uploads' ends come to -809 / -586, but the
battle sets' to +786 / +852. The mean is the code mix's, not a constant.

## 4. The vblank lines too

Units in the vblank lines (no HDMA) are 1.6 master cycles shorter on the port on average (1,262.4 against 1,264.0). The
same spacings occur on both sides (1142 / 1184 / 1226 / 1310 / 1322 + the 1144 / 1270 / 1280 variants), with other
frequencies. The poll's phase against the SPC enters each vblank from different visible lines. Not separated yet from §3.

## 5. The other legs: the same class

Each leg's first battle exit, the same two traces (later exits come after the leg has already parted):

| leg | release, port - cart | `$BD` apart from | what came just before |
|---|---|---|---|
| 24 (sanctum hall) | -2 frames | f2471 | inside the first fight, no CPU post near it (the battle set's upload f621-f665 ended +789) |
| 43 (Info Center) | +1 frame | f1798 | inside the first fight. All 26 command posts before it agree within 20 APU cycles. The driver was busy at `$0903-$0906` across the frame's tick on both sides, and on the port it ran past a second tick, so one tick dropped (the main loop's `$029C MOV A,$FD` steps once whatever the count). The set's upload f173-f218 ended +793. |
| 51 (security) | -2 frames | f627 | right after the battle set's upload (f586-f623, end +686) |

So the divider also parts outside uploads, where the driver's own busy stretch ends within a few cycles of a tick. That
needs an SPC whose state is already a little off. The likeliest source is the same upload drift (the song's sequencer
started against another phase), but that is not shown: `ct.exe --aram --from <state>` did not give the state's ARAM, so
the two ARAMs were not compared.

Leg 73 (the case the brief measured, the first Reptite fight) was not measured in this container. From the leg's
state, made here by `tools/playthrough.py play all`, the port never leaves 110's arrival scene: with the stock `cmp` too,
every graded chain stays at its f0 value, where the pinned line has 110 -> 111 and storyline 6C / 6F. That is a separate
problem, not this one. Two other things in this container: `attract-07d-frog-squash-colours-vs-oracle` fails the same
way on a clean tree (1108 / 12320 pixels at port f6820), and 27 Jackie saves did not fetch (`fantasyanime.com` refused),
so `fixtures.py make all` stops early.

## Open

The release frame follows the fade lattice. The lattice follows the uploads' pace in C7 to well under a timer tick
(~5,370 APU cycles: a few hundred cycles of drift over a 41-frame upload flip a tick on some uploads). Matching it needs
the HDMA's alignment run by run. On the rule's oracle that depends on held cycles. Either the rule's oracle aligns
HDMA/DMA on the beam's clock (the master cycles minus the held ones, so a hold moves nothing against the beam), and the
port prices C7 access by access with LakeSnes's alignment from that clock; or the class stays open. A rule decision
(CLAUDE.md, the zelda3 model): asked, not taken.
