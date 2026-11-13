# The battle's message window: its rows, its text strip, its picture

Derived 2026-11-12 from the ROM and the oracle (playthrough leg 117, the fights of Zeal's first rooms; the whole-run
picture audit found the window 996 px off the cartridge on every frame it stood). Companions: `battle_results_band.md`
(the band's 24 lines and its ramp), `battle_results_text_origin.md` (the pen), `battle_results_box_timing.md` (a reward
line's cadence), `battle_top_message_box.md` (the typed boxes), `battle_hud_strip_position.md` (the presets).

One window serves every battle message: the Att picker's target box, a tech's description, an enemy tech's name,
"Can't run away", "Ran away!", "<name> got up!" and the victory's reward lines.

## 1. What is on the screen

The band preset the open window selects (`$CCEC = $10` added to the id, D1/FE5E) has a second `$BE9B` entry of 24 lines
on the side opposite the HUD strip. Read on the oracle with the window below (`--hdma`, leg 117 frame 900):

```
ch0 $2107 m4   185-208 @BE9B   78 78 78 78        all four BG maps at VRAM $7800
ch1 $210D m3   185-208 @CA02   0000 FF27          BG1: scanline 185 shows line 224 -> tilemap rows 28-30
ch3 $2111 m3   185-208 @CA0E   0000 000D          BG3: scanline 185 shows line 198 -> rows 24 (its last 2 lines) .. 27
ch4 $212C m4   185-208 @C02B   05 05              TM = TS = BG1 + BG3
ch5 $2130 m4   185-208R@C0EF   one record a line  add 8..1 (12 lines), subtract 0..7 (11), none (the last)
ch6 $2105 m1   185-208 @C14F   09                 mode 1, BG3 priority
```

With the window above (preset $13) the same rows come from the records `$CA06` (BG1, vofs `00D7`: 9 + $D7 = 224) and
`$CA12` (BG3, `00BD`: 9 + $BD = 198). D1/F35F-F377 writes the four words at the boot (`$CA04 = FF27`, `$CA08 = $CA0C =
00D7`, `$CA10 = 000D`, `$CA14 = 00BD`).

VRAM at that frame (the oracle's dump, word `$7800 + row * 32`):

```
row 24  29FF x32                                      the blank glyph tile 1FF, BG3 palette 2, priority
row 25  2900..290F 2930..293F                         the text's upper tile row
row 26  2910..291F 2940..294F                         the text's lower tile row
row 27  29FF x32
row 28  00F0 (00F1 00F2)x15 00F3                      the frame: top border
row 29  00F4 (00FC 00FD 03FC 03FD)x7 00FC 00FD 00F5   the weave
row 30  00F8 (00F9 00FA)x15 00FB                      bottom border
```

So the window is BG1's three frame rows -- the field window's tiles F0-FF / 3FC-3FF in the player's Window Color, palette
0 of the live page `$7E:2200`, through channel 5's fixed-colour math (`CGADSUB` 01 / 81: BG1 alone) -- under BG3's two
text rows, 2 bpp tiles 100-11F and 130-14F in palette 2: CGRAM 9 `1483` (the shadow), 10 `4A51`, 11 `7FFF` (the body) on
the battle's page. The math does not touch BG3.

## 2. Who writes the rows: the boot, CC/E4FC-E563

```
CC/E4FC  TDC / TAX
   E4FE  LDA #FF / STA $7FFD,X / LDA #29 / STA $7FFE,X / INX INX / CPX #0100 / BNE     rows 0-3: 29FF
   E50F  TDC / LDX #0040 -- STA $7FFD,X / INC A / INX INX / CPX #0060 / BNE            row 1, cols 0-15: 00..0F
   E51E  LDA #30 -- ... CPX #0080                                                      row 1, cols 16-31: 30..3F
   E52B  LDA #10 -- ... CPX #00A0                                                      row 2, cols 0-15: 10..1F
   E538  LDA #40 -- ... CPX #00C0                                                      row 2, cols 16-31: 40..4F
   E545  TDC / TAX -- LDA $D15D50,X / STA $80FD,X / INX / CPX #00C0 / BNE              rows 4-6: the frame
   E554  LDA #FF / STA $80FD,X / LDA #29 / STA $80FE,X / INX INX / CPX #0200 / BNE     rows 7-11: 29FF
```

A WRAM image of twelve rows at `$7E:7FFD` (oracle: `--monitor 7E7FFD:2,7E803D:2,7E80FD:2 --writer` shows CC:E508, E516,
E54E on leg frame 699, fourteen frames after D1:F365 wrote `$CA04`). It is the boot's alone: the fight takes the bytes
for other work as soon as actions run -- C1:54A1 copies the arena's 16x16 grid to `$81FD` on leg frame 833 and to
`$7FFD` later -- while VRAM keeps the rows (the dump of leg frame 7800 has them, with the grid in WRAM). The port builds
the image at the same point of its boot stager (battle.c `battleMsgWindowImage`, beside the port of CC/E45E-E4ED) and
the picture reads the rows as VRAM holds them (`CtBattleMsgWindowRows`).

`D1:5D50` (the frame's $C0 bytes) is not in the data pack. Its three rows are the field window's rows 0, 1 and 9 as
C0/29F7 writes them (field.c `fieldWindowFromRom`), without the priority bit; battle.c writes them by that rule and the
check `battle-message-window-target-name-vs-cartridge` compares the 512 bytes with the cartridge's.

## 3. Who writes the text: the engine, into `$7E:B400`

Every post goes through the same two routines:

```
CD/025E  STA $020C / STZ $CCED / STZ $02A1 / LDX #B400 / STX $0210 / LDA #7E / STA $0212 / STZ $0214
         JSR CD/0239 (zeroes $B400-$B7FF) / JSL C2/0003 / LDA #40 / RTS
CD/027F  STA $0213 / JSL C2/0009 / JSR CD/3E7D x2 / LDA #10 / STA $CCEC / INC $CA24 / RTS
CD/04DF  LDA $CD05 / BEQ -- LDA #01 / STA $0213 / JSL C2/0009 / LDA $0215 / STA $CD05 /
         JSR CD/01A5 / STA $CCEB / INC $CCED            the typed boxes: one glyph a frame
```

C2/0003 and C2/0009 are the proportional-text engine the menus use (src/vwf.c, `vwf_engine.md`): the string
`[$020D] + 2 * $020C`, its dictionary words, its number and name codes read from the argument cursor `$0200..`, the
glyphs composed 2 bpp into the strip at `[$0210]`. The strip is four runs of sixteen tiles: `$B400` the left half's
upper row (tiles 100-10F), `$B500` its lower row (110-11F), `$B600` and `$B700` the right half's (130-13F, 140-14F).

The battle's posts and their arguments:

| post | table, id | arguments |
|---|---|---|
| CD/0310 (system box, typed) | CC:3A09, the id | -- |
| CD/02D8 / 02CB (target box, typed) | CE:EF11, 0B / 0C | `$0200` the enemy id, `$0201` / `$0203` its HP words (C1/1A1C) |
| CD/0295 (enemy tech name) | CC:CBC9, the id | -- |
| CD/01B3 id 8 ("Ran away!"), id 9 ("{11} got up!") | CE:EF11 | id 9: `$0200` the character (C1/B379) |
| CD/01B3 ids 0-7, the reward lines | CE:EF11 | FD/AD17, below |

FD/AD17 stages each reward line before its `JSL CD/0021`:

```
id 0  Got {0F} experience point(s)!   AD26  STX $0200 / STZ $0202        ($B28C)
id 1  Got {0F} tech point(s)!         AD43  STX $0200 / STA $0202        ($B2DB, $B2DD)
id 2  Found {0F}G!                    AD5B  STX $0200 / STZ $0202        ($B2A5)
id 3  Got 1x {1F}!                    AD75  STA $7F0200                  ($B2A7,Y: the item; C2/5B9B reads it there)
id 4  {11} level up!                  ADA6  LDA $AEFF,X / STA $0200
id 5  {11} learns {12}                ADE1  $0200 := $AEFF,X ; ADEC $0201 := $B3CE,X
id 6  Learns Dual Tech, {12}          AE14  LDA $B305,X / STA $0200
id 7  Learns Triple Tech, {12}        AE37  LDA $B310 / STA $0200
```

The port ran none of this: field.c decoded the strings itself (`snprintf` for the reward lines -- "ASCII approx" --,
a truncated decode for the typed ones) and drew them with the field's glyph drawer in three fixed tones. It now makes
the ROM's calls where battle.c already mirrored the posts (`battleMsgPostTail`, `battleSysMsgFrame`,
`battleNameBoxPost`, `battleLifelineMsgPost`, the flee's post) and stages the reward lines as FD/AD17 does
(`battleResultLinePost`); `$CD05` is the engine's own `$0215`. On the oracle's frames the strip and the engine's block
`$0200-$023F` are the cartridge's byte for byte (0 of 1024, 0 of 64).

The host keeps bank $7E alone; `$7F:0200`, the one byte of bank $7F the engine reads here, lives in battle.c
(`g_btMsg7F`, `CtVwfBeginBanks` / `CtVwfRunBanks`).

## 4. The port's picture

field.c `battleMsgWindowDraw`: per line of the band, BG1 from the rows 28-30 at the scroll of `$CA02` / `$CA06`, the
frame's tiles from FF/9E10 + colour * $280, CGRAM from `$7E:2200`, the math from the record `$C0EF + 4 * line`; BG3 from
the rows 24-27 at the scroll of `$CA0E` / `$CA12`, the tiles from the strip, a word without the priority bit hidden.
Measured on leg 117: the target box (frame 900) and the first reward line (frame 7800) are the cartridge's 24 lines to
the pixel -- before, 996 px and 1622 px of those frames.

The lifted window this replaces (`CT_ASSET_WINDOW` de-ramped, `battle_results_band.md` sec. 3-4) had two rows 2/31 off;
the render fixtures now read `row19=0,1,2 row20=0,0,0` there, rows 3 / 13 / 16 as the oracle's samples before.

## 5. Open

- **The grid copies at `$7FFD` / `$81FD`** (C1:54A1) are not made: WRAM differs there during a fight.
- `D1:5D50` in the data pack (sec. 2).
- **`$CA24` reads 1 for a frame in the port.** D1/FE70 takes a request on the NMI after it; the cartridge's row shows
  the count back at 0 on the request's own frame, the port's on the next (its frame is the NMI first, then the main
  thread). The picture and the tables are the cartridge's (sec. 6); the byte is a frame off, as for the HUD's presets.
- **Other waits.** Sec. 6.3's list is every wait an action, the victory and the exit reach in the fights counted (legs
  117, 50, 70, 122). A `JSL C7/0004` the SPC keeps past a vblank (the port's `sndStall`) and the defeat's and the
  run-away's own sequences were not counted: the port types through them.

## 6. How the window comes and goes

Measured on leg 117 (the rule's cartridge; `tools/battle_msg_window_cmp.py typed-boxes` / `reward-line-comes-and-goes`
and `tools/battle_typewriter_cmp.py action-waits` are the checks).

### 6.1 The text tiles reach VRAM a half an NMI

The battle NMI D1/E780 dispatches `JSL CD/0C95` = `LDA $7E / ASL / TAX / JSR ($0C83,X)`, table `09F1, 088D, 088D`.
`$7E` has two writers: the boot's D1/F24B `STZ $7E` and C1/0034's `JSL CD/0003` = CD/0399 `LDA #01 / STA $7E`, a frame
before the HUD band's seed. So the boot's NMIs run CD/09F1 and the fight's run CD/088D:

    CD/088D  LDA $7ECE0F / BEQ ->08A0        (else CD/09CE + CD/3EB4, no upload)
    CD/08A4  LDA $CA1E / BNE ->08AC          the HUD band animating: no upload
    CD/08A9  JSR CD/0607                     the tilemap block when `$CA17`, then always CD/064B:
    CD/064B  LDX #0200 / STX $45 / INC $CE14
             LDA $CCEC / CMP #10 / BEQ ->0694
             CD/065A  the window down: a fixed-source DMA of CD:06B2's zero word, $200 bytes, to VRAM $5800 when
                      `$CE14` is odd, $5980 when even
             CD/0694  the window up: `$7E:B400 -> $5800` (odd: tiles 100-11F, the window's left 16 columns) or
                      `$7E:B600 -> $5980` (even: tiles 130-14F, the right 16), $200 bytes (CD/3E8E)

`$CE14` has no other writer (CD/0650's INC; CD/065F and CD/0694 read it): it is whatever the field left in
`$7E:CE14` plus the fight's counted NMIs. Leg 117's first fight: 00 at the boot, one count at f703 (the NMI between
CD/0399 and the band's seed), none while the band opens (`$CA1E` != 0, f704-f716), one a frame from f717.

The port: `battleMsgStripUpload` (battle.c, from battleNmiHead) keeps the two halves in `CtBattleHost.msgStripVram`
and the window draws its text from them. Its gate is `h->frames >= battleBootBandArm(h)` (the port's `$7E` != 0, as
battleStripNmiUpload's) with the same `$CA1E` / `$CE0F` tests; the port seeds the band a frame ahead of the
cartridge's main thread and so counts no NMI at the arm and one earlier at the band's end -- the count is the
cartridge's from the first frame the window can show.

### 6.2 The band is the installed table's

The presets D1/FE5E builds with `$CCEC` = $10 in their id put a second `$BE9B` entry in `$7E:C867`, on the side
opposite the HUD strip. D1/FE70 installs a preset on the NMI after `INC $CA24`, so a request of main-thread frame N is
on the cartridge's picture at N + 2 -- with whatever half of the text that same NMI uploaded:

    typed box    request f787 -> the band, empty, at f789; the name ("Red Scout", in the left half) two glyphs' worth
                 every other frame: f791 f793 f795 f797, whole at f799
    reward line  post f7792 (CD/025E + CD/027F's render), request f7794 (after CD/027F's two waits) -> the window with
                 HALF its text at f7796, all of it at f7797; CD/01FD (`STZ $CCEC / STZ $CCED / INC $CA24`) at f7970 ->
                 down at f7972; the next line's post at f7986 (CD/020A's sixteen waits), its request at f7988

The port: `ctBattleMsgBand` (field.c) reads the entry; `battleResultsRender` draws the window where it stands and
nothing where it does not. The results machine makes CD/01B3's requests in its order (battleResultsTick: the post at
hold - 4 and at each close's sixteenth frame, `$CCEC := 10 / INC $CA24` two frames later, CD/01FD at the dwell's end).
A host staged by hand (CT_BATTLE_RENDERWRAM, CT_BATTLE_RENDER_RESULTLINE) has no uploads and no installed table: it
keeps the preset byte's / the phase's gate and the WRAM strip.

### 6.3 The typewriter steps only where a wait hands back through it

CD/04DF (`LDA $CD05 / BEQ` -- one glyph: `$0213 := 1 / JSL C2/0009`, `$CD05 := $0215`, `JSR CD/01A5 / STA $CCEB`,
`INC $CCED`) has two callers:

    CD/3E89   the tail of bank CD's own wait CD/3E7D (`INC $CA20`, spin on it through CD/3E44, then `JSR CD/04DF`):
              every wait of bank CD -- the sheet packets' CD/3E75 (which adds `JSL C1/0012`, the menu pass), the effect
              load, the results' CD/020A and dwell
    CD/04DB   the end of the service CD/0009 (CD/04AA), which bank C1 calls after its wait C1/007E (`INC $9E / JSL
              CD/0036 / LDA $9E / BNE`): the main loop's C1/10A0 and C1/10C0, C1/10FA (after C1/40A0, 40B0, 40E1, 485B,
              4864, 488D, 4943), C1/354D, C1/356D and C1/3689

A C1/007E that hands back to neither types nothing on the frame it returns in (the NMI's D1/E7EC dwell still counts
`$CCEB` down). `--hookpc CD04DF` over leg 117's first fight, f780-f4010 (twelve actions, the victory, the exit): 46
such frames, in five kinds --

    C1/405F              C1/4058 `INC $A0FD / JSL CC/F06B / JSR C1/007E`: the frame after `$A0FD` rises (its only other
                         writer is the main loop's C1/1073 `STZ`), one before C1/40A6's `INC $A013`           f810 f1016 ..
    C1/4841              after the sheets' eight packets: the rise + 9                                        f820 f1026 ..
    C1/4116 .. C1/41B7   the close: C1/40E1's return has C1/40E4's service, C1/4116's does not, nor C1/414B /
                         41B4 / 41B7's when a PC is among the targets -- one frame, or four, ending on `STZ $A013`  f1011, f1712-f1715 ..
    C1/358E 3596 359E    the victory's three seat removals (C1/3686's loop has C1/3689's service)             f3156-f3158
    CF/FCFB              the module's exit CF/FBE5-FCE9 waits through its own routine                         f3999-f4005

The second target box of that fight is typed under an action's prologue: `$CCED` steps f818, f819, f821 (the cartridge's
rows), `$CCEB` reads 9F at f820, and the box's pictures from there are on the other upload phase.

The port: `battleTypewriterHeld` (battle.c) names the five from the state the port already keeps (`$A0FD`'s edge, the
action's `rel`, `actEndRel` / `actEndHold` / `actSpan`, `victoryHold`, `handoffLeft`), and `battleSysMsgFrame` skips
CD/04DF's step on them. Legs 50 (27 frames), 70's first fight (2) and 122 (80) count the same on both sides.
