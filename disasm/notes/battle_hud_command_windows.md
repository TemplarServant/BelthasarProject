# The battle HUD strip is two tilemaps the ROM rebuilds from the queue (2026-09-23)

Found from the forest ambush's lockstep (field_d9_party_walk.md section 8: `vmdiff-forest-ambush-d9-vs-oracle` stops at
f282). That f282 is the harness's reach, not a divergence: op D8 runs at f279 on both sides, the consumer arms at f280 and
the cartridge's objects stop there, and `ct.exe` links neither field.c nor battle.c. The real host (`ct_field --scene 19
--loadwram` of the same state) stages the whole interface byte for byte on the cartridge's frame (`$29B0`, the eight
`$29BF` records, `$019D`, `$01A3`; `$0102` = 13 16 03 at f282 in the logical oracle, f283 in real time -- a lag frame).
Following the battle past the staging showed the three defects below.

## 1. The band animator's mode is cleared at the boot (D1/F2AC)

`python tools/dis.py 11F1D0 0x100 1 0`: D1/F2A4-F2B8 = `LDA #FF / STA $CA1D / STA $CD46 / STZ $CA1E / STZ $CA20 / STZ $CA26 /
STZ $5D9B`. The port took `$CD46` from that run and skipped the rest. A seed captured after an earlier fight carries that
fight's band mode: `forest_wait_ambush_active.state` has `$CA1E = 81` (a close), `gato_t0.raw.wram` has `6B`. Oracle
(forest): `$CA1E = 00 (D1:F2AF)` at f284, the open `FF (CD:0512)` at f348. The port kept the 81 and its animator ran close
steps over the boot's tables every frame; the open (armed only on `$CA1E == 0`) never came and the top strip showed as its
one centre line. Now CtBattleBegin writes the whole run.

## 2. The boot waits on the band's step COUNT (CD/0534)

C1/003C-0042 = `JSL CD/0012 / BNE ->003C / JMP C1/34DB`; CD/0012 = `JMP CD/0534` = `LDA $CA1F / BEQ / LDA #01`. The last
open step zeroes `$CA1F`; `$CA1E` clears on the animator's next call (CD/05BC), a frame later. Oracle `gato_t0.state`:
`$CA1F` 00 and the entrance ladder armed (`$5DDD = 01`, C1:3509) at f165, `$CA1E = 00` at f166. The port armed the ladder on
`$CA1E`'s edge: one frame late -- and on gato_t0.raw.wram, whose stale `6B` the port's animator stepped down as an "open",
at f16. Now `pcBandPrev` follows `$CA1F`: the ladder arms at port f80 = cartridge f165 (real time, the band's own offset 85;
logical 163 / 80, the band's offset 83), and its eight publications `$9907` fall on the cartridge's frames (38 34 39 35 3A
36 4F 37 at f167..f199 real time = port f82..f114).

## 3. The strip: one tilemap, two layers, written from the queue

In the strip's lines every BG reads the tilemap at VRAM word $7800 (HDMA channel 0's block `$BE9B` = 78 78 78 78; TM = 05 =
BG1 + BG3). BG3 draws the TEXT rows 1..7, whose WRAM image is `$0CC0 + $40 (r-1)`; BG1 draws the FRAME rows 16..21,
`$0B40 + $40 (r-16)` (6 rows). Both are DMA'd from WRAM.

| writer | what |
|---|---|
| CC/E4DC, CC/E4ED (boot) | text rows cleared to `2900`; frame rows := `$D1:5800` (the BASE: the enemy-name window cols 0..11 + the party window cols 12..31) |
| C1/1C10 -> C1/1C3A (every dequeue) | frame rows := the base again |
| C1/0299 (the rebuild; from C1/0027 and C1/0C5C) | text rows 1..6 cleared, HP/MP headers, party rows (C1/0305..), the NAME LIST at rows 2/4/6 cols 1..11 (C1/04DC-0545: `$ACBC + 24k`, 11 codes, the blank row above), then with `$A6DE != 0`, `$A86B == 0` and `$95DB == 0` (C1/0547/054C/0564) C1/0560 |
| C1/0560 -> C1/0785 | for each ENQUEUED seat s (`$A6D9,s` bit 7 clear), its command window `$CC:FA41` (or `$CC:FA6B`, the Combo variant) = 7 cols x 6 rows at text cols 6s..6s+6 |
| C1/0560 -> C1/081E -> C1/0929 | for each enqueued seat s, the frame `$D1:59FC` (7 cells x 6 rows) at frame cols 6s..6s+6 -- or its last 6 cells at 6s+1..6s+6 when seat s-1 is enqueued too, so s-1 keeps its right edge |
| C1/087E, C1/08A2-08E4 | the active seat's cols 6s, 6s+1 cleared; with `$9609 == 0` the hand 60 61 / 62 63 at `$CC:FADD[$95DC,seat] + 12 x $A6DD` = rows 1+2c / 2+2c, cols 6 x head, 6 x head + 1 |

So the whole strip is a function of the queue (and the names, the party). Measured: over the 22 in-battle WRAM fixtures of
saves/battle_re (queue empty, seat 0, seat 1, both), `$0B40` = base + C1/081E's frames for their queue, byte for byte.

## 4. What the port had, and why it was wrong

The port drew the strip from three captured pictures and a set of gates:

* **The chrome** = one of three BG1-only captures: "enemy-name", "command" and "target-select" (keyed on `$960A`, box0 4 px
  narrower). The "command" one is seats 0 AND 1 enqueued (`p600`); the "target-select" one is seat 1 ALONE: its fixture
  `gato_target.raw.wram` has `$A6D9 = FF 01` and `$9609 = 00` -- command level, not the picker -- and `$960A = 1` is a
  leftover of the previous pick. The recipe that captured it ("A on Att., A") had in fact picked Crono's target and left
  Marle choosing. So a fight with Crono alone ready (bat2/3/4/8/9, GATO's first turn, the forest's) drew Marle's window
  frame at cols 7..12, where the ROM keeps the party window's own corner (4 px, cols 96..103).
* **The names** were drawn only with the queue empty or with `$960A` up, on the reading that "box0 keeps the enemy name
  while the picker is up" -- the same fixture: "Gato" shows there because seat 0's window is not drawn. And ADDENDUM 3 §E of
  battle_hud_render.md read the cells a non-enqueued window leaves as "invisible filler": they are the name list, blank
  only when the name is short. The forest's "Avian Chaos" keeps "Chaos" at cols 7..11 while Crono alone chooses (oracle
  f440, `$0D8E` = A2 C1 BA C8 CC); with Crono dequeued and Marle choosing, box0 reads "Beetl" / "Avian" (f560). In the
  ROM's own picker (Crono choosing a target, f528) the windows stay and no name shows.
* **The hand**: col 7 for seat 1 (an extrapolation) and one column back while `$960A` was up -- the two errors cancelled on
  the one fixture. The ROM's column is 6 x `$A6DD` for every seat (oracle f560: `$0D0C/$0D0E` = 62 63).

## 5. The port now (src/battle.c, src/field.c, tools/gen/battle_hud_chrome.py)

* battle.c `battleHudChromeFrame` keeps `$0B40` as C1/1C3A + C1/081E at the end of every tick -- the main loop's pass,
  where C1/0560 runs after the enqueue (C1:1B83 and C1:0946 both at the forest's f431), so the frames change on the frame
  the text does; nothing with a submenu open (C1/0564). The boot copies the base (CC/E4ED). `CtBattleHudChromeRebuild` exposes it to a render host that
  patches a captured queue (`CT_BATTLE_RENDER_SETACT`, which now also sets the head `$A6DD` as C1/058A's invariant says) and
  to the post-resolve `--battletest` shot, whose `$0B40` C0/0905 has already given back to the field.
* field.c draws the chrome from `$0B40`: `src/data/battle_hud_chrome.h` (generated) holds, per frame tile (20: F0..FB,
  0FC..0FF, 3FC..3FF) and strip line, its 8 pixels. The generator lays the three captured pictures over the ROM's own
  tilemap for their queues: 36864 samples, 0 conflicts, every (tile, line) of the 8 queue states covered -- the pixels
  depend on the tile and the line (the COLDATA gradient), never on the column. The submenu panels keep their captures.
* The names are drawn wherever no enqueued seat's window (cols 6s..6s+6) covers the cell; the hand at 6 x `$A6DD`.

**2026-10-01, superseded by the upload pipeline** (battle_hud_upload_pipeline.md sec. 6): the port no longer keeps `$0B40`
every tick nor draws the text from live state. The ROM's own routines (C1/0299, C1/0785, C1/081E/0929, C1/0872, C1/05A7 ...)
write the images at their step of the menu pass, the NMI's copy (CF/E338, CF/E384) fills a VRAM image, and field.c draws
the strip from that image -- so the frames and the text change on the cartridge's frame (sec. 7's first item is closed).

## 6. Graded (tools/battle_hud_strip_cmp.py: the port's strip from the cartridge's WRAM against the cartridge's picture)

| state | queue | strip px identical |
|---|---|---|
| forest ambush f400 (top strip) | empty | 12288 / 12288 |
| forest ambush f440 | Crono | 12282 (6 px: the ATB gauge) |
| forest ambush f515 | both | 12288 |
| forest ambush, Crono's picker f540 | both, `$9609 = 1` | 12288 |
| forest ambush, Crono dequeued f954 | Marle | 12285 (ATB) |
| gato_inbattle f600 / picker f622 / old "target" capture f824 | both / both / Marle | 12288 each |
| gato_t0 f200 / f260 / f300 | empty / Crono / Crono | 12288 / 12276 (ATB) / 12282 (ATB) |
| bat1..bat9_lake (both strip sides, 2 and 3 PCs) | Crono or both | 12288 or all but ATB cells |
| bat9_lake + Right x3 (Frog active) f189 | Crono + Frog | 12221: ATB, and cols 20..21 = the HP-column reflow O3 (battle_3pc_lucca_frog.md) |

The residual cells are the ATB gauges of a running timer: the picture comes from the ROM's last rebuild, a few frames old
(ct-hud-rebuild-lag-vs-wram). The live `--battletest` GATO strip at port f100 = the cartridge's f185, 12288 / 12288.

## 7. Open

* The rebuild's own frames: the port keeps `$0B40` and the text every frame; the ROM rewrites them on its rebuild (forest:
  `$A6D9` dequeued f556, the frames f560).
* The hand's ROW reads the head seat's own cursor (`$95DC,X`); the port keeps one cursor byte for every seat.
* `$CC:FA6B` (the Combo command in place of Tech, C1/07A2-07BB) is not ported.
* In logical frames the band (and so the ladder) sits one frame later against the ATB/walk timeline than on the
  cartridge (offset 83 against 82): the band's arm frame (CT_BHUD_ARM_FRAME) belongs to the song-upload clock.
