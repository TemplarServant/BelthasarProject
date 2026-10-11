# The effect's sprites: OBJ sheets, the frame table $7E:4500, a program's sprite cloud (2026-09-17)

Check: `attract-07d-frog-squash-eyes-vs-oracle` (tools/attract_fx_sprite_cmp.py). Survey item: the Frog Squash's eyes
(tech $28, the attract's 07D battle) are OBJ; the port drew per-tech bakes (Cyclone, Ice) and nothing else.

## 1. Who loads what (C1/4833, the tail of every tech and item staging)

C1/45BB (PC tech, CD/45A6[$AE93*7]), C1/4693 (enemy tech, CD/5526[$AE93*7]) and C1/476F (item,
CD/5C26[($AE93-BC)*7]) stage the same seven bytes and three words, then `JMP C1/4833`:

| byte | CD/45A6 | CD/5526 | CD/5C26 | read by |
|---|---|---|---|---|
| +0 $9877 | script selector | same | same | CD/5EF0 / CD/61F0 / CD/63F0 -> $9878 |
| +1 $987A | OBJ sheets 0-3 | | | CD/1323 |
| +2 $987B | OBJ sheets 4-5 | | | CD/1314 |
| +3 $987E | D1:0060[*2] -> $987F | | | C1/486A (the frame packet) |
| +4 $9881 | CD:692C / CD:6A2C / CD:6C2C [*2] -> $9882 | | | anim op 60 |
| +5 $9884 | CD:656E / CD:666E / CD:656E [*2] -> $9885 | | | C1/48EC (not ported) |
| +6 $987C | the effect load (CD/0018) | | | C1/4850 |

C1/4833: `LDA $987A / JSL CD0015` (CD/1323), `LDA $987B / JSL CD002A` (CD/1314), the effect load when `$987C != FF`,
`JSR C1/48EC`, then C1/486A. A physical's six-byte records (C1/4497 by `$9853`, C1/432C by the weapon) are not derived.

## 2. The OBJ sheets (CD/1323, CD/1314 -> CD/1368)

`$CACE` = the first sheet, `$CACF` = the VRAM slot (0..3 for CD/1323, 4..5 for CD/1314), `$CAD0` its end; per sheet
CD/1368 builds 32 tiles into `$7E:2D00` and CD/3ED2 ships the $400 bytes to VRAM `CD:1308[$CACF*2]` = 0C00 0E00 1000
1200 1400 1600 (OBJ names $0C0-$17F).

CD/1368 with X = sheet: the words CF:A000 + X*64 (32 of them), the palette-quadrant word CF:E000[X*2] stored twice at
`$CC9D..$CCA0`. For tile t: the byte `$CC9D[(t & 18) >> 3]`, shifted right by `t & 06` (CD/13CA's four entries),
AND 3 = q; the mask CD:1300[q] = 0000 00FF FF00 FFFF. The word w names the pool tile D0:0000 + ((w & 1FFF) << 3):

* CD/1488: eight words -> `$2D00+Y` (planes 0/1) and `(w | swap(w)) & mask` -> `$2D10+Y` (planes 2/3 = "any pixel",
  in the quadrant the mask picks).
* w bit 13 clear (CD/1525): a third plane p (eight more bytes): `$2D10+Y+2r = swap(((word | p) & mask)) | p`.
* bits 15-14 through CD:13E6 = 1509 1445 13F6 13EE: plain, H-flip (CD/144A, 32 bytes through C0:FD00), V-flip of both
  halves (CD/13FE twice), V-flip then H-flip. (CD/1509 and CD/1488 run with a 16-bit A: disassemble them `m=0`.)

GRADED (Python, then the C port): VRAM $0C00-$17FF against the `$987A/$987B` the same capture holds -- 84 oracle
captures in build/ over nine sheet sets (DA/EF Cyclone, 61/00, E0/00 the Frog Squash with 3bpp tiles and every flip
kind, C0/C4, 20/24, 2C/30, 36/3A, D2/00, 0A/0E), 6144/6144 each. Three captures match only in part, each explained by
a later loader that wrote the same VRAM without going through CD/1368 (a PC physical in bat9, a credits sequence).

## 3. The frame table (C1/486A, C1/4943)

`$0300 = $987F`, bank D1, destination `$7E:2D00`, `JSL C3/0002` (C3/08A6 leaves the length in `$0306`), two zero
bytes after it, `$A650 = $A652 = 0`, eight `JSR C1/4943` (each one frame, C1/007E). C1/4943 builds up to four frames:
`$84 = CC:F7C0[$A650*2]` (frame f at `$4500 + f*C0`), `$A05B[f] = 0`; a zero header ends the call (without `$A650++`);
else `n = header & 0F` row masks go to `$A2D3..` and each set bit (MSB first, `ASL` written back) takes two stream bytes
as a cell: `$4502 = tile`, `$4503 = attr | $A5E3`, `$4501 = CC:F820[row]`, `$4500 = CC:F820[col]` (00 10 .. 70),
`$A05B[f]++`; two stream bytes are skipped at the end of a frame. C1/48B3 then writes the sixteen one-cell records
`$5D00 + i*8` (00 00, D1:50A2[i*6], D1:50A3[i*6] | $A5E3) and `$A07B[i] = 1`.

GRADED: the attract's Zombor (Python, 6144/6144) and the Frog Squash (port WRAM: $4500 6144/6144, $A05B, $5D00, $A07B).

## 4. The effect palette (anim op 60, C1/5D1D)

`60 k`: the record CD:($9882 + k*2) = [p, f]; `Y = (f & 7) * 8`; f bit 7 clear: six bytes D1:4100[p*6], set (C1/5D5F):
fourteen bytes D1:4400[p*14], into `$2302 + Y` -- the LIVE page only (colours 1-3 / 1-7 of a four-colour block),
which the NMI's CF/E3C0 uploads. Free (`INC $A00F`), two bytes. Oracle: `$2300 = 0000 FF7F 3A5B B646 323A AE29 2A19
0000` at f4672 (C1:5D85), the Frog Squash's eye colours. The block layout matches the sheets' quadrants: q = 0/1/2/3
puts a 2bpp tile's colours at +0/+8/+4/+12.

## 5. The sprite cloud (program op 80 09, CD/2BFC) and when it is on screen

CD/2BFC: `MVP` $0710..$08BF -> $0750..$08FF and $0901..$091B -> $0905..$091F (everything from entry 4 moves down
sixteen entries), sixteen records `$0710 + 4k` = the slot's point + `$4500 + 4k` (the Y clamp to F0), `$0713 = attr |
30`, and `LDX #AAAA / STX $0901 / STX $0903` (entries 4-19 large, X below 256).

Order in a frame (oracle `--hookpc`, the frog): the OAM compose CF/E8AF at vPos 13 (`LDY #0010 / STY $C9`: the
combatants from entry 4), CD/2BFC at vPos 155, the NMI's OAM DMA after both. So the upload carries the cloud exactly
when the op ran after that frame's compose, and the combatants sit at 20+ (`--monitor 0710 --writer`: CD:2C6C last
every frame of the tech). The port snapshots `$0710-$074F` / `$0901-$0904` in its NMI head (battle.c
`battleNmiHead`) with a flag the op sets, and field.c `battleFxCloudRender` draws the sixteen entries (entry 19 first)
from the sheets and the live CGRAM, after the combatants and before the popups; BG3's priority pixels are restored
after (the frog covers what it covers).

Which upload a picture shows: the LakeSnes frame boundary puts the NMI at the start of the emulated frame, so the
OAM dumped at the end of frame F is the one frame F was drawn with -- the Python render of that OAM equals the
cartridge's OBJ-only picture 886/886 at f5042 (and 169/886 with the previous frame's).

GRADED: port f7081 (the 07D alignment, host = battle + 6547) vs the cartridge's f5042 -- the cloud's visible pixels
882/882.

## 6. Open

* ~~C1/48EC and the scratch objects' composer CF/EEEB~~ -- ported, section 8; the Cyclone / Ice / song-note bakes
  are retired.
* The OAM high table's entries 0-3 (`$0900` = FF, the cursor) and the base page's OBJ palette 0 colours 13-15
  (`1F7C` on the cartridge, 0 in the port) -- not read by the cloud.
* The 32-sprite / 34-tile line limits (ppu.c) are not modelled by the port's sprite layers.

## 7. The white flash: ops 69 / 6A / 6C, the loop's tracks, and when the picture shows it (2026-09-17)

Survey item: the 07D's enemies turn white beside the frog on the cartridge; in the port they did not.

* **The attribute.** Op 69 (`C1/5DD3`, then `C1/5DDC`) writes `$993C,X = 08` (OBJ palette 4) for track t's combatant
  (t < 4: `$A014,t`) or for the whole `$A018` list (t >= 4), then the colours: D1:4D90[k*24] into the live page at
  `$2382` (colours 1-12) and FFFF into colours 13-15; an enemy that is part of the arena (`$99D2`, `$A08C,X`) also gets
  the twelve words repeated over the live BG page from word `$99D3`. Op 6A (`C1/5E87` / `C1/5E8B`) writes `$9888,X`
  back (and copies the base BG page back for such an enemy). Oracle, the Frog Squash: `$2380 = 0900 FF7F x11 FFFF x3`
  f4830 (C1:5E54); `$993C = 0A 0C 0E 08 08 08 08 08` f4834 (C1:5E1A, the list path).
* **The blink, op 6C** (`C1/5F13`, three bytes): see battle_tech_cyclone_fx.md §2 -- the Cyclone's measured curve is
  retired. Oracle-graded (battle-anim-op6c-cyclone-blink-vs-oracle): 29 toggles, 28/28 intervals, exit +1.
* **Which tracks the loop runs** (`C1/4BBE-4C9D`): 0-3 when live; 4 only with an `$AE99` list (`LDA $AE99 / ORA $AE9A
  / BEQ`); 5-7 never; 8-15 only while `$A3D1` is clear (no ShowDamage). GATO's song and the Cyclone on GATO arm track 4
  (`$5DAC` byte 4 = 01) and its cursor never moves. (No action script in the four tables uses 5-7, and no fixture
  holds an effect track across a ShowDamage: those two rules are ungraded.)
* **The target group** of a tech is CC:1ACB[id*2] (battle_multitarget_cursor.md §5.1): the Frog Squash is 08, all
  enemies, so its list track runs and lights all five.
* **When the picture shows it.** The OAM compose CF/E8AF runs in the V-IRQ chain at vPos 13 and reads `$993C` as the
  previous main loop left it; the next NMI uploads the shadow. Oracle: `$993C` f7 -> shadow attribute f8 -> drawn f9.
  The port: compose slot after battleNmiHead (objPalCmp), NMI head (objPalShown), the projection reads objPalShown
  and lights EVERY combatant carrying 08 (h->fxScriptFlashMask), with CGRAM row 4 as the colours. PC techs and items
  are the interpreter's too (their pinned physical flash is off). Graded: field-battle-flash-attribute-uploaded-two-
  frames-late (f412 white only with the two-frame lag).
* **Open, measured: the durations.** The 07D's flashes last 22/25/53 frames on the cartridge and 6/6/21 in the port.
  The script's `20 05` pauses count on interpreter VISITS, and on the frog the interpreter runs once every 3-4 frames
  (--hookpc C14CA2): the NMI + IRQ chain takes ~240k of the frame's 357k master cycles (CT_HOOK_CYCLES=1), the main
  loop gets 36-42k, and C1/1C4A (the pose builder) is most of that. The port runs the interpreter every frame. This is
  the CPU-clock class (the one the race sequence answered with a translation, src/xlat): a model of the battle's per-frame CPU budget,
  or a translation of the loop, is needed.

## 8. The effect objects' machine: C1/48EC, CC/F278, the links, the compose (2026-09-17)

The port drew Crono's Cyclone whirl, Marle's Ice shards and GATO's song notes from three oracle bakes
(src/data/battle_fx_whirl.h, battle_fx_ice.h, battle_fx_song.h). They are retired: every effect object is now drawn
by the ROM's own machine, for any tech, item or enemy action.

* **The animation table, C1/48EC** (the effect load, C1/4861 after CD/0018). `$A280 = $9885`, the action's animation
  set in bank CE (the 7-byte config's word). The walk reads 4-byte records. A record whose byte 0 has none of bits
  7-5 is a frame. Bit 6 alone closes a FACING group: `$A2A0[anim]` = the group's length, that record included. Bit 7
  closes the animation (bit 5 alone too, with `$A2A0[anim] = 0`), and the next record opens the next one,
  `$A280[++anim*2]`, sixteen at most.
* **The arm, C1/4DDF** (ops 02 / 03 / 04-06 / 26 on an effect track; object o = `$A2B6` = track - 8).
  `$A238[o] = $A2A0[k]`, `$A220[o*2] = $A280[k*2]`, `$A1A8[o]` = the mode (1 once, 2 loop), `$A1B0[o] = 0` (the
  dwell), `$A1B8[o] = $A2B8[o] = FF` (the cursor).
  * 02 (C1/4D04): mode 2, free.
  * 03 (C1/4E78): latch `$A1C0`. The first visit arms mode 1 and shows the object (C1/5FC3 `INC $A1D8`) and stays.
    Later visits stay while `$A1A8` is set, then clear the latch, hide the object and advance.
  * 04-06 (C1/4EF7) and 26 nn (C1/57E1): latch `$A1C8` (1, or nn). The first visit arms (mode 1, or mode 2 for 26)
    and stays; the latch makes CC/F2A9 switch the object off when its cursor wraps. Then as 03, without the hide.
* **The service, CC/F278** (C1/40B9, once per iteration of the action loop, before the script). Every active object
  counts `$A1B0` down; every active object whose dwell is 0 loads its next record (CC/F2A9):
  * `X = cursor*4 + facing * (groupLength*4 & FF) + base` (CC/F365 is the 8x8 product through `$4202/$4203`); the
    cursor wraps FF to 0, and a latched object is switched off at the wrap.
  * record = `[frame | end bits, dx, dy, dwell | bit 7]`. The dwell goes to `$A1B0`, dx/dy sign-extended to
    `$A260/$A270`. Bit 7 of byte 3 selects the one-cell frames (`$A2C0 |= 20`, table `$5D00`).
  * an END record (any of bits 7-5 in byte 0) restarts a loop (mode 2: cursor FF) or switches the object off
    (`$A1A8 = 0`, frame `| 80`).
* **The palette cycles, CC/F1E7** (C1/40BD). Ops 61-64 arm cycle k (`$A3C5` period, `$A3C9` first record, `$A3CD`
  last, `$A3C1 = 1`, `$A3B9 = 1`, `$A3BD = FF`); 65-68 stop it. A live cycle reloads its period at 0, steps
  `$A3BD` and loads the record the way op 60 does (section 4).
* **Visibility and order.** 6E / 6F show / hide all eight objects (`$A1D8`), 70 / 71 the track's own. 73 / 74 rr put
  the object before (`| 80`, in front) or after (`| 40`, behind) combatant `$A014[rr]` in the OAM order:
  `$A3DF[o*11 + X]`, `$AB4F[o] = X`. D9 pp / DA set `$AB5F[o]` to pp or back to `$AB57[o]`.
* **Facing.** Op 72 on an effect track writes `$A230[o]` (C1/604B). The drift arms A8 / A9 stamp
  `$A230 = C0:F700[heading]` and `$A355 = heading` (C1/6AA7 / C1/6C65). The heading ops 76 / 85 / 77 store
  `$A5B5[o]` (`$A5AA,X` for a combatant); 77 dd adds dd (C1/6154, free). The attract's Zombor turns its four orbiting
  objects with 77 between two A8 drifts (`$A5B5` 00 00 40 80 C0 -> 00 80 40 80 40 at f433-435 from
  build/_z_f7085.state, C1:6196).
* **The links, CF/ECC2** (the IRQ chain, CF/E753, after the movers). Every object whose `$A437` is not negative takes
  a point: bit 6 = object (link & 7)'s 16-bit point, else combatant `link`'s foot (`$1D0C/$1D23`). 1C rr (C1/5694)
  sets it (`$A014[rr]`, or `(rr - 9) | 40` for rr >= 9), 1D kk (C1/56BB) clears it; both free. The Cyclone's track 8
  `1C 00` ties the whirl to Crono's foot (87,74) from f490.
* **The wipe** (CC/F0C4-F0D6, every action): `$A240-$A27F`, `$A124-$A1A7`, `$A1A8-$A1E7`, `$A3B9-$A3C0`, the matrix
  `$A3DF-$A436` to 0; the links `$A437-$A43E` to FF. The battle boot copies `$A5E3` into `$99B5/$9F3B` (11) and
  `$AB5F/$AB57` (8).
* **The compose, CF/EEEB** (the IRQ chain, vPos 13). Objects 7..0, drawn beside the combatant the matrix names
  (`80` before, `40` after), the frame's cells last to first from `$4500` (or the one-cell `$5D00`), at
  `($A240 + $A260, $A250 + $A270)`, attribute `(cell & CF) | $AB5F`, shown while `$A1D8` and `$A1B0` are set.
  Its timing was graded in Python against the cartridge's OAM shadow: 59/59 frames of the Cyclone whirl's cells
  come out with the points of the frame and the animation state of the frame before. The port snapshots that state
  in its compose slot (after battleNmiHead) and the points at the end of the tick, after the links.

GRADED (tools/fxobj_cmp.py, `CT_BATTLE_FXOBJTRACE` against `--monitor` over the same bytes). Each object's fields
are compared as sequences of distinct values, because the cartridge's action loop sometimes spans two frames:

| case | window | result |
|---|---|---|
| GATO's Cyclone (gato_cyclone.state) | 260 frames | 72/72 object fields, matrix identical, 4 frames held |
| GATO's song | 100 frames | 72/72 |
| the attract's Zombor (087) | 230 frames | 72/72, 8 frames held |

Checks: battle-fxobj-machine-cyclone-vs-oracle, -song-vs-oracle, -zombor-vs-oracle. Past those windows the song and
the Zombor drift by the CPU clock (section 7: the pauses count interpreter visits, the drifts count frames).

Still open, not ported or not graded:

* the effect mover's mode 7 (CF/F884, a turning mover that writes `$A230` at CF/F916);
* the alternative group table C1/C264;
* the facing-group product and op D9 have no fixture whose picture depends on them;
* the Ice has no oracle capture in the repo (ice_precast.state is not committed), so its shards are drawn by the
  machine but not graded.
