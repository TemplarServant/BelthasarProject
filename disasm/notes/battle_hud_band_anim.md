# The battle HUD strip OPENS and CLOSES as a 23-step vertical iris (defect #23)

*"the battle HUD appears and disappears without animation"* (translated from Portuguese) — 2026-09-02 list, item 23.
Everything below is measured on the ROM + the LakeSnes oracle. Nothing here is inferred.

The port drew the strip as a step function: `battlePhase && liveHost.frames >= 82`, and then all
48 rows at once. `battle_hud_render.md` Q7 guessed the missing animation was a bottom-anchored
slide ("the window slides/wipes up from the bottom edge") from six `gf*.png` frames. **That guess
is wrong**: the ROM's own table mutation is symmetric, and the pixels agree with the ROM, not with
the guess. Q7 is superseded by this file.

## 1. The machine — CD/04FD (open), CD/0516 (close), CD/05B2 (the per-frame animator)

Bank `$CD`'s entry-vector table at `CD/0009` is `JMP` thunks:

```
CD/0009  4C AA 04    -> CD/04AA
CD/000C  4C FD 04    -> CD/04FD     the HUD OPEN arm
CD/000F  4C 16 05    -> CD/0516     the HUD CLOSE arm
CD/0012  4C 34 05    -> CD/0534     the poll: A != 0 while the animation runs
```

`CD/04FD` has no direct `JSL` anywhere; it is only ever reached through `CD/000C`, and its ONE
caller is the battle boot:

```
C1/0031  JSR C1/34A7
C1/0034  JSL CD/0003
C1/0038  JSL CD/000C      -> CD/04FD:  LDA #08 / LDX #$0080 / JSR CD/3B71 / JSR CD/3E7D
C1/003C  JSL CD/0012                    LDA #$17 / STA $CA1F      <- 23 STEPS
C1/0040  BNE ->C1/003C                  LDA #$FF / STA $CA1E      <- mode OPEN-ARM
C1/0042  JMP C1/34DB                    JSR CD/3E7D / RTL
```

so **the boot BLOCKS on the open** (`CD/0534` returns 1 while `$CA1F != 0`; the counter is advanced
from the NMI, so the busy-wait terminates). The close is armed the same way from the battle
epilogue, and blocks identically:

```
CD/03D3  JSR CD/039E            the victory SFX post ($1E00=81 id=$10) -- already ported
CD/03D6  JSL CD/0516            LDA #01 / STA $CE16 / JSR CD/3E7D
CD/03DA  JSR CD/3E7D              LDA #$0C / STA $CA25 / INC $CA24 / JSR CD/3E7D
CD/03DD  JSL CD/0534              LDA #$17 / STA $CA1F   <- 23 steps
CD/03E1  BNE ->CD/03DA            LDA #$80 / STA $CA1E   <- mode CLOSE
CD/03E3  LDA #$0E / STA $CA25 / INC $CA24
```

The animator is `CD/05B2`, and the battle NMI calls it **TWICE per frame** — `CD/08F5 20 B2 05`
immediately followed by `CD/08F8 20 B2 05`, in the block that ends `LDA $CA1D / STA $00420C`
(HDMAEN). So 23 steps take 12 frames.

```
CD/05B2  LDA $CA1E / BEQ ->05FE            0 = idle
         BMI ->05D0
         LDA $CA1F / BEQ ->05F3            mode 1 = OPENING
         JSR CD/053C                         one open step
         DEC $CA1F / BNE ->05FE
         LDA #02 / STA $CA25 / INC $CA24     last step: REQUEST preset 2
CD/05D0  CMP #FF / BNE ->05E5              mode $FF = the arm
         STZ $CA25 / LDA $CA22 / JSL CE:EE6E  install the animating preset NOW (not a request)
         LDA #01 / STA $CA1E                  -> mode OPENING. no step this call.
CD/05E5  JSR CD/0577                       mode $80 = CLOSING, one close step
         DEC $CA1F / BNE ->05FE
         STZ $CA25 / INC $CA24
CD/05F3  LDA $CA1E / BMI ->05FB / STZ $CE16
CD/05FB  STZ $CA1E                         -> idle
```

## 2. One step = a symmetric iris over NINE band tables

`CD/053C` (open) and `CD/0577` (close) are mirror images. Open:

```
CD/053C  TDC / TAX                        X = 0, 16-bit
CD/053E  DEC $C86A,X                        entry1 count -= 1     (the gap ABOVE)
CD/0541  LDA $C86D,X / CLC / ADC #02 / STA $C86D,X
                                            entry2 count += 2     (the STRIP band)
CD/054A  DEC $C870,X                        entry3 count -= 1     (the gap BELOW)
CD/054D  REP #20 / TXA / CLC / ADC #$002D / TAX / TDC / SEP #20
CD/0558  CPX #$0195 / BNE ->053E          nine tables, stride $2D, base $7E:C867
CD/055D  LDA $C94F / SEC / SBC #$0004 / STA $C94F     ch5 entry2 indirect ptr -= 4
CD/0569  LDA $C8F5 / SEC / SBC #$0004 / STA $C8F5     ch3 entry2 indirect ptr -= 4
```

`-1 +2 -1 = 0`: the total scanline span never changes, so the band grows ONE line at the top and
ONE at the bottom per step. The two pointer adjustments are the two channels whose entry2 is in
REPEAT mode (per-scanline data): ch3 and ch5, `-4` = one scanline of a 4-byte-per-line channel.

Measured, `--monitor "7EC867:405" --diff` on `saves/battle_re/forest_holdleft_pre.state` (ch0's
header, one row per frame of the open):

```
f246  50 66 04 1A      f250  50 5E 14 12      f254  50 56 24 0A
f247  50 64 08 18      f251  50 5C 18 10      f255  50 54 28 08
f248  50 62 0C 16      f252  50 5A 1C 0E      f256  50 52 2C 06
f249  50 60 10 14      f253  50 58 20 0C      f257  50 50 30 04
```

`80 + 103 + 2 + 27 = 212` at the preset and `80 + 80 + 48 + 4 = 212` at the end: **the band grows
from 2 scanlines to `$30` = 48 — which is exactly `CT_BHUD_H`.** The centre never moves.

## 3. The band in strip rows

`--hdma` (per-scanline register truth, independent of decoding the tables) and `--peek`
framebuffers agree exactly. With `s` = steps taken (1..23), in strip-local rows (`renderY - 160`
at the bottom strip position, `- 8` at the top one):

```
band = strip rows [23 - s, 24 + s]        s = 1 -> rows 22..25,  s = 23 -> rows 0..47
```

Pixels, `--peek saves/gato_t0.state --btnscript 0:N` (renderY of the rows that differ from the
pre-battle frame):

```
f155 182..185   f158 176..191   f161 170..197   f164 164..203   f167 160..207
f156 180..187   f159 174..193   f162 168..199   f165 162..205   f168 160..207 (settled)
f157 178..189   f160 172..195   f163 166..201   f166 160..207
```

top -2 / bottom +2 per frame about renderY 183/184 — **a centred iris, not a slide.**

## 4. The strip CONTENT does not move; the ramp is shifted ONE row

Every animating frame's band rows are **byte-identical to the settled strip's own rows at the same
renderY** except at the colour-math ramp's group boundaries. And, decisively:

> every one of the twelve animating frames is a byte-exact ROW-WINDOW of ONE 48-row image —
> the settled strip with the COLDATA ramp moved up one row (peek f167).

`--hdma` names the ramp. Settled (`0:174`), ch5 = `$2130` mode 4, table `$7E:C948`:

```
scanlines 161..163 ADD 8 | 164..166 ADD 7 | ... | 182..184 ADD 1
          185..187 SUB 0 | 188..190 SUB 1 | ... | 206..207 SUB 7 | 208 math OFF
```

i.e. `R(r) = 8 - r/3` for strip rows r = 0..46 (+ = ADD, - = SUB), and r = 47 is math OFF. During
the open, strip row r carries `R(r-1)` instead — measured at three independent points (`0:155`
band rows 20..27, `0:158` rows 14..33, `0:164` rows 2..45; all three are the settled ramp shifted
down exactly one row), and at s = 23 row 0 takes the entry BEFORE the table, which `--hdma` reads
out as `math=05 fixed=(0,0,0)` — i.e. `R(-1) = 0`. That is the `$C94F -= 4` above: the ramp
pointer tracks the band's top edge but starts one entry low.

Consequently exactly **17 of the 48 strip rows differ** between the settled and the opening
chrome: `r % 3 == 0` (delta `R(r-1) - R(r)` = +1, and -8 at r = 0) plus r = 47 (-7, the math-OFF
entry). The other 31 rows are byte-identical. The delta lands ONLY on the pixels the colour math
reaches — the window bodies; the bevel highlights, the frame and the glyph ink are untouched
(measured per pixel at renderY 175/178/181/184 of `0:158`).

**The CLOSE is NOT shifted.** It starts from the re-installed preset and `CD/0577` moves the same
pointer `+4` per step, so top edge and ramp track exactly: `--hdma` on the GATO victory tail
(`f2156` rows 4..43, `f2160` rows 12..35, `f2164` rows 20..27, `f2166` rows 23..24) reads the
settled ramp phase at every one. The close therefore needs the band clip and NOTHING else.

## 5. The ladder, in the ROM's own RAM

`--peek` sidecar `.wram` vs the displayed band, gato_t0, no input:

```
peek  $CA1E $CA1F $CA24 $CA25 | band     | s
f153   00    1E    00    08   | none     | -     pre-arm ($CA25 = 8, the field preset)
f154   FF    17    00    08   | none     | -     the arm frame: tables not installed yet
f155   01    16    00    00   | 182..185 | 1     <- 12 frames, step 22,20,...,0
 ...
f166   01    00    01    02   | 160..207 | 23    last step: preset-2 REQUEST raised
f167   00    00    00    02   | 160..207 | 23    request consumed, TABLES still the animator's
f168   00    00    00    02   | 160..207 | 23    settled
```

So `$CA1E`/`$CA1F` determine the band completely, and the ONE extra opening frame (f167) is the
HDMA install lag: `f166` raises the request, `D1/FE70` consumes it on `f167`, and the preset's
tables are not on screen until `f168`. f167 and f168 are indistinguishable in WRAM — that frame is
table state, not RAM state, so a `--renderwram` fixture cannot pin it and none is asked to.

Thirteen non-settled frames in all: twelve at s = 1,3,...,23 and one more at s = 23.

## 6. What the port does with it

`src/battle.c battleHudAnimTick()` is `CD/05B2` byte-for-byte, run twice per host tick, writing the
ROM's own `$CA1E/$CA1F/$CA24/$CA25`. `CtBattleHudRender` reads those four bytes and clips the strip
to `[23-s, 24+s]`; `h->hudLiveOpen` carries the one-frame install lag of §5.

The arm frame is `82 - 13 = 69`: 82 is the existing oracle-pinned settle offset (`battle_hud_render.md`
Q6, module-boot f222 -> settled f304) and 13 is the ladder length measured here, so the arm is
determined, not chosen. The close is armed where the port already models `CD/039E` (the victory SFX
post at the handoff), and `CtBattleTick` withholds its "battle over" return until the animator goes
idle — the port's form of the `CD/03DA` spin.

## 7. Open / not ported

- The **opening chrome's one-row ramp shift** (§4) needs the 17 differing rows as oracle bytes; the
  port draws the settled chrome through the band, so those 17 rows are 1/32 dark (and row 0 / row 47
  are 8/32 and 7/32) during the 13 opening frames. Geometry is exact; this is colour only.
  **CLOSED 2026-10-15**: the strip's chrome is now drawn from the ROM's tiles and channel 5's records walked from WRAM
  (battle_hud_render.md, "THE WINDOW COLOR"), so the shift is drawn; the cartridge's mid-open frame gato_t2 (band rows
  180..187) is 2048/2048 px identical.
- `CD/039E`'s own `LDA #0F / JMP CD/3E3B` 15-frame wait before the close is still not modelled
  (a pre-existing gap in the ~35-frame handoff deficit already recorded in `battle.c`).
- The TOP strip position (`$2989` bit1 clear) is animated by the same rows-local machine; only the
  bottom position was measured frame by frame.

## 8. 2026-09-19: the band is READ from channel 0's table (the close's blink)

The band has exactly one home in the ROM: the table HDMA channel 0 reads at $7E:C867 (ch0 -> $2107). Every preset
CE/EE6E installs puts the strip's register block $BE9B behind ONE entry of it, and CD/053C / CD/0577 step that entry
by two lines and its neighbours by one:

| preset | ch0 table (bank CE) | strip |
|---|---|---|
| 00 (animator, top) | `04 .. / 1B .. / 02 9B BE / 60 .. / 53 ..` | lines 31..32 |
| 02 (settled, top) | `08 .. / 30 9B BE / 54 .. / 48 ..` | lines 8..55 |
| 0C (the close's, top) | `04 .. / 04 .. / 30 9B BE / 49 .. / 53 ..` | lines 8..55, then stepped |
| 01 / 03 (bottom) | `50 .. / 67 .. / 02 9B BE ..` and `50 .. / 50 .. / 30 9B BE / 04 ..` | 183..184 / 160..207 |
| 08 (boot), 0E (close end) | no $BE9B entry | none |

`CtBattleHudRender` now walks that table (field.c `ctBattleHudBand`) instead of inferring the band from $CA1E/$CA1F.
The mode could not say it: the close's last step leaves the two-line band with $CA1E = 00, and its own request (CD/05ED
`STZ $CA25 / INC $CA24`) re-installs preset 00 -- the same two lines -- before CD/03E3's 0E removes the strip. Oracle
0AD: 4 lines f2169, 2 at f2170 and f2171, none at f2172; the port drew all 48 lines on f4216 (`$CA1E = 00` read as
"idle, whole strip"), the blink the user saw. Now 10/6/3/1 chrome rows on both sides through f4216.

The same reading needed CF/FB7D-FB8C, which the port had never run: `STZ $9E / $9F / $A0 / $98CA` and `$EC = ($2989 &
02) >> 1`, right before `JSL CD/0000` -- the byte D1/F1DF copies into $CA21/$CA22, the preset offset. Without it $CA22
was the field's leftover: 0 in the attract (top strips anyway) and in the GATO fixture, whose $2989 = 92 wants the
BOTTOM presets. The renderer's own origin already came from $2989; the band now agrees with it.

The fixed arm gate at the live call site (`frames >= 69`) is gone: before the arm, the boot's preset 08 has no strip
entry. With lockstep the 0AD's opening ramp now runs where the cartridge's does (cart f1780 <-> port f3843, two rows a
frame). Residual: during the opening the cartridge shows the strip content one line lower (a grey row at 32 where the
port has it at 31) -- preset 00's scroll tables, which the strip renderer does not read. Open: after the close the ROM
requests 0E, waits, requests 08 and spins on CD/044A until APU port $2141 reads 0 (CD/0428-0436): about 30 frames the
cartridge holds the battle picture, while the port hands back to the field on the close's last frame.
