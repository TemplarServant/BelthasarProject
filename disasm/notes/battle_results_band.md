# The battle message band is 24 scanlines with its own colour ramp

Derived 2026-09-03 from the ROM + the oracle only (defect #10 of the 2026-09-02 list, "the message with the
battle's rewards does not have the correct HDMA").

## 1. The band

The results/rewards window is one HDMA band of the preset D1/FE5E selects:

```
D1/FE5E  LDA $CA25 / CLC / ADC $CA22 / CLC / ADC $CCEC / JSL CE:EE6E
```

`$CA22` is the strip side (`$7E:2989` bit1) and `$CCEC` = $10 is "this message window is open", so opening
it selects a different preset. Read out of the armed ch0 indirect header at `$7E:C867` on the forest
fixture (preset $13 = 2 + 1 + $10):

```
[C867] cnt=08 ind=$BC85   lines 1..8
[C86A] cnt=18 ind=$BE9B   lines 9..32     <- THE MESSAGE WINDOW: 0x18 = 24 scanlines
[C86D] cnt=30 ind=$BC85   lines 33..80
[C870] cnt=50 ind=$BC85   lines 81..160
[C873] cnt=30 ind=$BE9B   lines 161..208  <- the HUD strip
[C876] cnt=04 ind=$BC85 / [C879] 00 END
```

ch4 ($212C) gives TM = TS = $05 (BG1+BG3 only) for exactly those 24 lines. The same `cnt=$18` comes back
at the opposite position (preset $12, scanlines 185..208), so 24 is the band's own height, not one
fixture's accident.

## 2. The ramp

ch5 is `$2130 mode 4, table $7E:C948`, whose window entry is `cnt=98 (repeat 24) ind=$C0EF`: 24 groups of
{CGWSEL, CGADSUB, COLDATA, SETINI}. Decoded live, and built by D1/F88C..F8CE as an accumulator stepped by
`$00AA` whose high byte is the COLDATA value:

```
D1/F894  acc = $E856, step -$00AA, 12 entries     -> ADD 8,7,7,6,5,5,4,3,3,2,1,1   ($2131 = $01)
D1/F898  acc = $E0AA, step +$00AA, 12 entries     -> SUB 0,1,1,2,3,3,4,5,5,6,7     ($2131 = $81)
D1/F8E1  forces entry 23 to math OFF
```

The FIELD dialogue box's band is a different shape entirely: ADD 8..1 then SUB 0..7 at FIVE lines a step
over 80 rows.

## 3. The port

`battleResultsRender` drew `drawCtWindow(fb, boxTop, 40)` -- 40 rows of `CT_ASSET_WINDOW`, which is a lift
of the FIELD box (so the field band's ramp is baked into the texture) stretched by the generic 12/18/10
nine-patch. Three errors in one call: the height, the row mapping and the per-row colour math. The SIDE
was already right (it keys on `$2989` bit1, the ROM's own byte) and stays untouched.

Now `drawCtMsgWindow24`: 24 rows, an 8/8/8 TILE-ROW patch (box rows 0..15 from bake rows 0..15, box rows
16..23 from bake rows 72..79) and the field ramp swapped for the battle band's per output row.

## 4. Measured

Oracle: `saves/battle_re/forest_battle_3hetake.state` driven to the results box (`--peek` truncated to the
exact frame). Compared with the port's render in the text-free columns x=170..250, in 5-bit units:

| | rows exact | rows off |
|---|---|---|
| after the fix | 22 of 24 | box rows 19 and 20, +2 |

Those two are a limit of the SOURCE, not of the derivation: `CT_ASSET_WINDOW` is a lift of the field box
and its own band CLIPPED bake rows 75/76 to 0, so no de-ramp can recover the base; the code takes the
largest base the clamp allows. The clean fix is a 256x24 lift of the battle box itself, the way
`saves/ct_window.bin` was made -- then the whole de-ramp disappears. Until then those two rows are 2/31
too dark.

Sampled at x=210 (text-free), the oracle reads 19,20,20 at box row 3, 5,6,7 at row 13 and 4,5,6 at row 16,
and so does the port.

Checks `battle-results-band-24-lines` (top) and `-bottom` (the opposite position), two mutations each,
all killed: the generic nine-patch, and keeping the bake's field ramp. Row 13 is the sample that tells the
two patches apart -- 8/8/8 takes it from bake row 13, the 12/18/10 from bake row 41.
