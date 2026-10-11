# The reward line's PEN ORIGIN inside the battle results window

*"the reward messages at the end of the battle are not aligned in the box"* — 2026-09-07. ROM + oracle only.
Companion to `battle_results_band.md` (the window CHROME, which is right and is graded) and
`battle_results_box_timing.md` (the line CADENCE). Neither of those can see the text: the band check
samples text-free columns and the shot checks are whole-frame hashes that pin whatever the text does.

## 1. What the port had

```c
int x = TB_X0 + 10, y = boxTop + 14;
for (const char* p = line; *p; p++) {
  if (*p == ' ') { x += 5; continue; }
  ...
}
```

Three host numbers: a `+10` indent, a `+14` row, and a magic 5-pixel space. Measured against the oracle,
the line sat **10 px right**, **8 rows low** inside a 24-row window, and ran **3 px wide**.

## 2. The X is the ROM's own $0234

`CD/025E` stages the message and calls the bank-C2 text renderer through the entry vector
(`C2:0003` → `JMP C2/57DF`; `C2:0009` → `JMP C2/5823`):

```
CD/025E  STA $020C / STZ $CCED / STZ $02A1
         LDX #$B400 / STX $0210 / LDA #$7E / STA $0212 / STZ $0214   ; dest buffer $7E:B400
         JSR $0239                                                    ; clear it
         JSL $C20003                                                  ; -> C2/57DF
```

```
C2/57DF  ...
   57FD  LDA $0214 / CMP #02 / BNE ->5807 / LDA #00 / BRA ->5809
   5807  LDA #08
   5809  STA $0234              <- $0214 is 0 (CD/0264 STZ'd it), so the pen X starts at 8
   580B  LDA $0214 / BPL ->5811 / STZ $0234      (the STZ is SKIPPED for $0214 = 0)
```

`$0234` **is** the pen X in pixels, and the composer proves it:

```
C2/5DE7  LDA $34 / AND #$0007 / STA $6A        ; sub-tile column
C2/5E02  LDA $34 / AND #$F8 / LSR / LSR / STA $6E   ; (x/8)*2 = the tile
C2/5E21  LDA $35 / SBC #$A0 / TAX              ; glyph = char code - $A0
C2/5E27  LDA $C260E6,X / ADC $34 / STA $34     ; pen += the glyph's ROM width
```

`$C2:60E6` is the 84-entry advance table this port's font blob already carries (`cGlyphW`). So:

- **the pen starts at x = 8**, and
- **the SPACE is a glyph like any other**: CT charset `$EF` → index `$EF-$A0 = 79` → table[79] = **4**.
  Three spaces in `Got N experience point(s)!` is where the port's extra 3 px came from.

## 3. The Y is measured, at both band positions

The vertical position does not come from a pen: the glyphs go into the linear tile buffer at `$7E:B400`
and the `$CA24` NMI upload (`D1/FE70`) publishes it. That upload's VRAM row is **not** derived here, so
the row is an oracle measurement — taken at BOTH band positions so it cannot be one fixture's accident:

| fixture | drive | band | white border row | glyph ink |
|---|---|---|---|---|
| `forest_battle_3hetake.state` | mash A, `$020C=0` + `$CCEC=$10` (line 1) | renderY **8**..31 | renderY 9 | rows **14..23**, x **8..154** |
| `bat4_lake.state` | mash A, same line | renderY **184**..207 | renderY 185 | rows **190..199**, x **8..154** |

Both: **glyph cell top = band top + 6**, pen x = 8. (`--peek` + `ctlib.fb`; the oracle fb is 2x pixel
doubled, image = every other row/column, and **render y = image y − 8**.)

Line 1 is `Got 9 experience point(s)!`; walking the ROM widths from x = 8 gives a 148 px advance and a
last-glyph ink edge at 154 — exactly the measured 154. With the port's 5 px space it is 151/157, and
from x = 18 that is the 167 the port drew.

## 4. What the port does now

`src/field.c battleResultsRender`:

```c
int x = TB_X0, y = boxTop + CT_BRESULT_TEXT_DY;         /* 8, band row 6 */
int gi = cGlyph(*p == ' ' ? 0xEF : (unsigned char)*p);  /* the ROM's space glyph */
if (*p == ' ') { x += cGlyphW(font, gi); continue; }    /* its own table width, 4 */
```

Result, per row, port vs oracle (lit-pixel count and x extent, line 1):

```
n:  20  15  80  71  80  58  64  73   6   4      <- identical
x:  9..154 / 8..154 x5 / 8..151 / 8..150 / 9..154 / 56..154 / 56..111   <- identical
```

## 5. Graded

`battle-results-text-origin` (quick, bat9 = band top) and the same signature appended to
`battle-results-band-24-lines-bottom` (bat4 = band bottom):

```
renderwram: msgtext=x8..154 dy=6..15
```

Read back out of the rendered pixels and reported RELATIVE to the band top, so one line holds at both
positions. The measurement skips the window's own full-width white border row (a ≥100 run) and the
single-pixel corner highlight at x = 4 that the oracle carries too. Three mutations — the `+10` indent,
`CT_BRESULT_TEXT_DY = 14`, and a hardcoded 5 px space — are all KILLED.

Re-pinned by this change: `field-battle-results-box-onset` and `field-battle-results-autoadvance`, both
whole-frame fnv shots of a frame whose text moved. Their chrome/onset/cadence subjects are unchanged.

## 6. Open

The `$CA24` upload's VRAM row (which would make the band-row-6 offset an instruction rather than a
measurement) is not derived. The same question would settle why the FIELD textbox's first line is at
`top + 9` in this port while this window's is at `top + 6`, since both go through the same bank-C2
composer into the same kind of buffer.
