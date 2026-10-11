# The selection bracket: C1/1115, the $993A rebuild and the $A4EF pulse (2026-09-17)

The pulsing corner frame around the party member who is choosing a command. The port drew it from a bake
(src/data/battle_bracket.h: the corner tiles, a grey triangle clocked off the port's own combat frame, the origin at
the chooser's STAGED foot). It is now the ROM's, and the bake is retired.

## 1. The entries (C1/1115)

C1/1153 (the menu-sprite rebuild) calls C1/1115 when a seat is choosing (`$95D5` positive) and no target is being
picked (`$9609` = 0). C1/1115 writes OAM entries 0-3 from the table CC:F604, four records `[dx, dy, name, attribute]`:

| entry | dx | dy | name | attribute |
|---|---|---|---|---|
| 0 | -12 | -36 | 06 | 32 |
| 1 | -4 | -36 | 06 | 72 (H flip) |
| 2 | -12 | -12 | 06 | B2 (V flip) |
| 3 | -4 | -12 | 06 | F2 (both) |

Position = the record plus the chooser's LIVE foot `$1D0C/$1D23,$95D5`; attribute = `(record & F1) | $9F46`; the high
table `$0900 = AA` (four large sprites). Then C1/17DD decides what entries 0-3 finally carry:

* nobody choosing -> C1/1AFE: `$0900 |= 55`, the four Y bytes = F0 (hidden);
* a target being picked -> C1/1979, the hand (port: battleTargetCursorRender);
* the tech list of a combo tech (`$95DB` = 1 with `$9EE3` >= 39 and `$9EE7` != FF) -> C1/180F, the partners' hands;
* the tech list otherwise -> C1/18AA (C1/1115 again); the item list (`$95DB` = 2) -> C1/18B0, whose hand is a BG3
  tile, so the bracket stays; `$95DB` = 0 -> nothing more.

## 2. When the entries are rebuilt ($9931 / $993A)

The bank-CF frame-service pass, right after the run-away counter (CF/E706), runs CF/E73B-E745:
`INC $9931 / LDA $9931 / AND #03 / BNE / INC $993A`. The pass starts on the band's arm frame (oracle gato_cyclone:
CD:0512 and the first increments at f298). CC/F0E6, inside the action wipe CC/F06B, zeroes `$9931`.

The request is consumed by `LDA $993A / BEQ / STZ $993A` followed by the rebuild (C1/1B67, C1/0C2D, C1/1153, C1/17DD):
C1/10C6 in the idle state, C1/10E3 / C1/10FA in the action loop. The boot's own loops never test it: on the cartridge
`$993A` climbs to 14 through the intro and is first cleared at f378 (C1:10CE), the first turn. In the idle state the
consumption can trail the request by up to two frames (f385 raised, f387 cleared) because the idle routine spans two
frame waits; the port consumes it on the frame it is raised.

The entries reach the picture with the next NMI. So the bracket moves when the rebuild runs, not when the foot does:
oracle gato_cyclone, Marle's foot `$1D0C` goes B0 -> B3 at f1147 and the entries follow at f1150.

## 3. The grey band ($A4EF)

The bracket's tile uses colours 13-15 of its palette (OBJ palette 1 for attribute 32). Colours 13 and 15 are the boot's
FF:9E0A copy (7FFF, 14A5). Colour 14 pulses: CF/E5AA-E5DB, in the same service pass,

```
E5AA  INC $A4EF / LDA $A4EF / CMP #3E / BNE E5B8 / TDC / STA $A4EF   ; 0..61
E5B8  ASL A / TAX
E5BA  LDA $A101 / BEQ E5C6 / LDA $9F46 / EOR #08 / BRA E5C9
E5C6  LDA $9F46
E5C9  AND #08 / BNE E5DD
E5CD  $233C/$233D := D1:5980,X / BRA E5EB                             ; colour 14 of OBJ palette 1
E5DD  $23BC/$23BD := D1:5980,X                                        ; colour 14 of OBJ palette 5
```

D1:5980 is 62 BGR555 words, the grey triangle 0000..7FFF..0421. The staging sets `$A4EF = FF` (CC/E321), so the arm
frame reads 00. The NMI uploads the CGRAM before this runs, so the picture carries the previous step: oracle
gato_cyclone f392, `$A4EF` = 1F, WRAM `$233C` = D1:5980[1F] = 7FFF, the PPU's colour 158 = 7BDE = D1:5980[1E].

## 4. The tiles

The battle boot's DMA channel 7 uploads (CC/E047-E0A1, CC/E0EB-E147), each 0x40 bytes from bank D1:

| source | VRAM word | names |
|---|---|---|
| D1/5EC2 | $0060 | 06 07 (the bracket's corner) |
| D1/5E82 | $0020 | 02 03 (the ground shadow) |
| D1/5F02 | $0160 | 16 17 (zero) |
| D1/5F02 | $0120 | 12 13 (zero) |

The same chain uploads the TARGET HAND (CC/E22E): FF:9CF0 -> VRAM $0000 = names 00 01, FF:9D30 -> $0100 = names
10 11. Its tiles use colours 13-15 too, so the hand's middle band pulses with the bracket's -- the port drew it
from a captured palette whose colour 14 was frozen at 6B5A (check field-battle-tgtcursor-cyclone-multi).

## 5. The port

* src/battle.c: `battleRebuildReqFrame` (CF/E73B) and `battlePulseFrame` (CF/E5AA) in the frame service from
  `battleBootBandArm`; `$A4EF = FF` in stageAll; `$9931 = 0` in CtBattlePcLadderWipe; `CtBattleBracketBuild`
  (C1/1115 + C1/17DD); `battleBracketRebuild` consumes `$993A` from `battleBootIntro` on, in CtBattleTick's wrapper;
  battleNmiHead copies the latch to `brkOamShown`; `CtBattleUiObjTile` (section 4).
* src/field.c `battleBracketRender`: the latch (a live battle) or a rebuild from the seed (the WRAM render host),
  the boot's tiles, the uploaded palette. The WRAM host puts the previous grey step back into its CGRAM copy.
* Traces: `bracket f<N> seat<S> oam=...` / `bracket f<N> off` under CT_BATTLE_TRACE; `renderwram: bracket ...` /
  `renderwram: bracket none` in the WRAM host.

GRADED:

* battle-bracket-rebuild-live-foot (gato_t0): the entries at Crono's foot (134,144) at f180, then (134,147) at f307
  after GATO's attack A pushes him -- the cartridge's gato_inbattle.state f0 shows the same `7A 6F 06 32 82 6F 06 72`
  with `$1D23 = 93`.
* battle-bracket-chooser-and-target-select (gato_cyclone): off at f228 for the target hand, Marle's entries at f240,
  byte for byte the cartridge's f1004 `A4 4B 06 32 AC 4B 06 72 A4 63 06 B2 AC 63 06 F2`.
* battle-enemy-cells-bat2-vs-oracle / -bat9-: the WRAM host's bracket against the cartridge's OBJ layer, 768/768.
* 31 whole-frame canaries moved; the old and new frames of every one differ only inside the bracket's 24x40 box
  (52 pixels = the grey band's phase; about 225 = the live foot 3 px lower after GATO's attack).

## 6. Open

* The idle state's consumption lag (0-2 frames) is not modelled.
* C1/1561 (the target-select path of C1/1153) and the combo tech's partner hands (C1/180F) are not drawn by the port;
  only their effect on the bracket (hidden) is.
