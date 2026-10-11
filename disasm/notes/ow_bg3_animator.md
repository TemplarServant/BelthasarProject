# The overworld's BG3 drift is an OBJECT, and each world has its own (2026-09-17)

Survey item: in the attract's closing overworld leg (loc 1F0, the pan over 1000 A.D.) the port's clouds sat about 48
pixels from the cartridge's, and the whole 270-frame leg carried it -- the worst overworld median in
tools/attract_survey.py (13.0).

## 1. What the port did

`owBg3Tick` (src/world.c) ran on every overworld frame of every world: `$24` (BG3 H) -1 every fourth frame, `$2A`
(BG3 V) +1 every eighth, then the camera deltas added in. Its own comment cited C2/75CE, and the law is that
routine's -- but C2/75CE is a CLASS HANDLER, not an engine tick.

## 2. What the ROM does

The object dispatcher `JMP ($0050)`s to the class word at a block's +0 (C2/0535), so the class word IS the handler's
address. Two of them drive the BG3:

```
C2/75C3  LDA $0294 / BIT #10 / BNE ->75FB      class $75C3 (world 0)
         REP #20 / LDX $4E / INC $0022,X
         LDA $0022,X / AND #0003 / BNE / DEC $24        every fourth frame
         LDA $0022,X / AND #0007 / BNE / INC $2A        every eighth
75E5     $24 += $1BF1 / $2A += $1BF3 / STZ $1BF1 / STZ $1BF3
C2/75FD  LDA $0294 / BIT #10 / BNE ->7650      class $75FD (world 1)
         INC $0022,X / AND #0003 / BNE / DEC $24 / INC $2A    BOTH every fourth frame
7617     $24 += $1BF1 / $2A += $1BF3 (and a $7E:A916 word this port does not use) / STZ both
```

The object bootstrap builds a different set per world (sequence_table.md section 9; the port's own sets are graded by
ow-object-bootstrap-set-vs-oracle), and only some carry an animator:

| world | extra classes | BG3 drift |
|---|---|---|
| 0 | $75C3 | H every 4th frame, V every 8th |
| 1 | $75FD, $7652 | H and V every 4th frame; $7652 = a per-line H wave (sec. 6) |
| 2 | $7702, $7759 | $7702 = a 16.16 drift (-3.46, +2.00) px/frame; $7759 = a per-line H wave on the odd lines (sec. 6) |
| 3 | -- | none |
| 4 | $77F2 | the $7702 drift with its own velocity (-17.32, -10.00) (sec. 6) |
| 5 | $7849 | none of the two (C2/7849 nudges BG1/BG2 instead) |
| 6, 7 | -- | none |

ORACLE:

* the interactive world 0 (saves/ow_w0.state): `--hookpc C275CE` reports a visit every frame with `$4E` = 0CF0, the
  block whose class word is $75C3, and `--monitor 7E0024:2 --diff --writer` the -1 every fourth frame by C2:75ED;
* the attract's 1F0 leg (saves/attract_cena_inicial.state): no $75C3/$75FD object in `$0B30..$1B30`, and `$24`/`$2A`
  hold 0000 for the whole visit -- the last write is the world entry's own clear (C2:0140) at f8613.

## 3. The world entry's direct-page clear (C2/012F)

```
C2/012F  PHB / REP #20 / STZ $00 / LDX #0000 / LDY #0001 / LDA #00EE / MVN $00,$00   ; $0000-$00EE
         SEP #20 / PLB / LDA #80 / STA $1C / LDA #09 / STA $1E
         LDA #01 / STA $2D / STA $33 / INC $3B / INC $3D / LDA #17 / STA $41 / LDA #13 / STA $42
```

It is what puts `$24`/`$2A` back at zero for a world that does not drift them (oracle: C2:0140 at the 1F0 entry).
The port wrote only `$41`/`$42` of this; the clear and the other stores are ported now. (The port's camera struct is
reset at the entry too, so the clear is not observable in the scroll on its own -- see the check's notes.)

## 4. The port

* src/world.c: `owBg3AnimatorClass` (scan the object blocks for $75C3 / $75FD) gates `owBg3Tick`, which now takes the
  class and the `$0294 & 10` skip; `CtWorldCamFrame` takes the WRAM for the same reason.
* src/field.c `worldEnterAt`: C2/012F's clear and stores.

GRADED:

* ow-bg3-animator-only-with-its-object: the attract's closing leg keeps `$24/$2A` at 0000 (port trace against the
  cartridge's measured 0000), two mutations.
* ow-camera (`--checkowcam saves/ow_w0.state`): where the animator DOES run, all six scroll shadows stay byte-exact
  against the cartridge for 750 frames.
* ow-world1-colour-math: world 1's own law (both axes every fourth frame), one mutation.

## 5. Open

* ✅ CLOSED: the 1F0 leg's remaining difference was not the camera or the tilemap -- both sides' `$20`/`$26` read
  0120/00A8 because they agree. The overworld composite drew the whole picture ONE SCANLINE LOW
  (disasm/notes/ow_bg_vline.md); at the matched frame the leg is now 56999/57344 pixels identical, and the residue
  is the white-out's palette row 8, whose fade starts ~40 frames later in the port.
* Classes $7652, $7702, $7759, $77F2 (worlds 1, 2, 4) are ported since 2026-09-26 (sec. 6); C2/7849 (world 5) is (obClass7849).

## 6. The drift and wave classes (ported 2026-09-26, user defects #6 / #5)

"the 2300 AD smoke regressed, demo screen" / "the 600 AD smoke is not animating properly". 4dd1cd0 made the BG3 drift the
object's -- right for worlds 0, 1, 3, 6, 7 -- but ported only $75C3/$75FD, so world 2's ash stood still (before it,
every world ran world 0's law). Located by the attract audit (build/_divergence/smoke/), each routine re-derived here:

* **C2/7702 (world 2) / C2/77F2 (world 4)**, byte for byte one routine but for the velocity: `$0294 & 10` -> nothing;
  the first visit `INC $0002,X` and arms +1A/+1C/+1E/+20 = 8931/FFFC/0000/0002 (77F2: ADF4/FFEE/0000/FFF6); every
  frame `$24`/`$2A` -> +14/+18, C2/0F38 (`+12/+14 += +1A/+1C`, `+16/+18 += +1E/+20`, two 32-bit adds), back into
  `$24`/`$2A`, `+= $1BF1/$1BF3`, both STZ. Port: world.c owBg3Tick, the fractions kept in the camera (bg3fx/bg3fy) so
  the model never writes the RAM it is handed. MEASURED: the attract's 1F2 leg, the port's `$24`/`$2A` at the frame
  paired with cartridge c5951 read FEA2/00CA, the cartridge's.
* **C2/7759 (world 2)**: HDMA channel 1, DMAP $42 -> `$2111` (BG3HOFS), indirect bank $7E, table `$7E:8A35` = F0
  ->$8B56, F0 ->$8C36, 00 (scanline L reads the word at $8B56 + 2L); even lines `$24` (77B5-77C5), line 2k+1 `$24 +
  S(p + k)`, k = 0..111 (77D3-77EE). p = the word at `$0022,X` with X left at $01C0 by the first loop -- i.e.
  `$7E:01E2` -- advanced by 4 (77C7-77D0). `$1C` bit 7 -> `TRB $4A #02` (the channel off).
* **C2/7652 (world 1)**: the same channel, table `$7E:A918` = C0/C0/C0/A0 (64+64+64+32 lines), every entry's address
  rewritten to $A925 by the NMI (C2/030A-0316, measured on saves/ow_w1.live), so scanline L reads word L & 63; word k =
  `$24 + (S(p + 16k) >> 2)`, sign kept (`LSR / LSR / ORA #C000`), p = the block's +22, advanced by 1; the words are also
  copied to $A9A5 (MVN, unread by the table).
* S = C2/225E: `C0:F900[i & 3FF]`, negated for i >= $200.

The port draws BG3 per line from the table (field.c owBg3HLine, row y = scanline y + 1). CHECKED against the
cartridge's own WRAM table at c5951: lines 0-7 FEA2 FF4A FEA2 FF49 FEA2 FF48 FEA2 FF46; the port's FEA2 FF49 FEA2 FF48 FEA2
FF46 FEA2 FF45 -- the even lines exact, the odd ones ONE SINE STEP ahead: C2/7759's phase is `$01E2`, a field scratch
byte, and the cartridge's value (E003) was left by op 02's handler (C0/5FFF `STA $E1` = `$D9 * $80 + $DB`, C0/6015
`STA $E3` = `$DB * 16`; last at f5720 of the attract), which the port's op 02 does not store -- the port starts from
its own leftover. A cartridge BG3 rendered from its VRAM with a plain scroll matches its isolated even-row specks
1284/1284, so the addressing is right. OPEN: the port's world-2 composite still differs from the cartridge's in ~3600
pixels per frame where the cartridge's BG3 is TRANSPARENT (the same before this change) -- a compose difference of
its own, not the scroll.


## 7. Why world 1's mist "moves faster" walking north or south (user question 2026-09-29)

"in 600ad (overworld) the smoke moves faster when walking north or south". It is the ROM's. MEASURED
on the rule's cartridge (saves/ow_w1.live, `--monitor 7E0020:2,7E0026:2,7E0024:2,7E002A:2,7E1BF1:4`, Up 40 frames then
Right 40): the camera moves one pixel a frame ($1BF3 = FFFF walking up, $1BF1 = 0001 walking right) and C2/75FD adds it
to BG3 1:1 on both axes, with its own drift (`DEC $24` / `INC $2A` every fourth frame) -- `$2A` 30 -> 12 over 40 frames
up, `$24` C3 -> D5 over 24 right: the mist's scroll is the same law on both axes. The port's CT_OWCAMTRACE shows the same
steps on its world-1 entry (`$26` -1 a frame, `$2A` -1 a frame with the drift's +1 every fourth).

What differs is C2/7652's wave (sec. 6): scanline L reads word `L & 63`, word k = `$24 + (S(p + 16k) >> 2)` -- the wave is
fixed to the SCREEN LINES and does not read `$2A`. A puff at BG3 row R sits on screen line L = R - `$2A`, so the phase it
sees changes by `dp + 16 dL` a frame: standing or walking east/west dL = -1/4 (the drift), -3 a frame; walking north
dL = +3/4, +13; walking south dL = -5/4, -19. The mist slides vertically through a wave that stays on the screen, and its
line-by-line sway runs four to six times faster. The port draws the same table by the same scanline (field.c
owBg3HLine, word L & 63 of world.c owBg3WaveTick's table, verified against the cartridge's $A925 in sec. 6).

## The wave's window follows the camera: C2/75FD moves `$7E:A916` (2026-10-04)

C2/7652's HDMA (channel 1, `$4310 = 42`: INDIRECT, two writes to `$2111`) runs from the table at `$7E:A918`: four
blocks `C0 / C0 / C0 / A0` (64, 64, 64, 32 lines), whose 2-byte pointers the NMI copies from `$7E:A916` every frame
(C2/0300-0312, `LDA $7EA916 / STA $7EA919, A91C, A91F, A922`). The wave object writes 64 words at `$A925` and C2/76F2's
`MVN` copies them to `$A9A5`, so a block reading 64 words from anywhere in `[A925, A9A5)` never leaves the table.
C2/7673-7678 seeds `$A916 = A925` on the object's first visit (and C2/2C9E on a world load). The DRIFT C2/75FD (600 A.D.'s
class) then, after `$2A += $1BF3`:

```
7627 ASL $1BF3 / LDA $7EA916 / CLC / ADC $1BF3     ; two bytes per pixel of the camera's vertical step
7632 CMP #A925 / BCS -> 763D / ADC #0080 / BRA     ; below the window: wrap up
763D CMP #A9A5 / BCC -> 7646 / SBC #0080           ; past it: wrap down
7646 STA $7EA916 / STZ $1BF1 / STZ $1BF3
```

So the wave scrolls WITH the mist: a band keeps its line offset as the camera moves. MEASURED, the rule's cartridge
(saves/field_re/catedral_world.state, the user's 600 A.D. capture walked out of the cathedral; Down 40 / Up 40 with
`--monitor 7EA916:2 --writer`): A925 -> A975 down and back, A92B -> A93B over eight 1-px frames (C2:764A). The port read
the table from line 0 always (`w[l & 63]`); walking up or down every band changed its horizontal offset line by line,
which reads as the mist running sideways (user report). world.c now keeps `waveTop` (= `$A916`), moved by the drift and
read as `w[(top + (l & 63)) & 63]`; CT_OWCAMTRACE's `owbg3line` lines end with `top=`. Pinned by
`ow-600ad-mist-wave-follows-the-camera-vs-oracle`.
