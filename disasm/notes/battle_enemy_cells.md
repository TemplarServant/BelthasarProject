# A combatant's cells for every record type, and the always-front entry (2026-09-17)

Survey item: in the attract's 087 (Zenan Bridge) the port drew Zombor without its legs. The legs are enemy `B3`
(slot 0, combatant 3) with a type-2 pose record (twelve cells); the port's generic enemy renderer handled types 0 and
1 only. Porting the ROM's law for all four types also showed that the upper body (`B4`, combatant 9) is drawn in
front of the legs although its foot is higher, through the render list's always-front entry.

## 1. The gather, C1/1C4A (on a pose change)

For combatant `cmb` (`$9679`) with record type `t = $967C,cmb` (the staged record's +7 & 7) and pose `p = $967B`:

* the record is `$1C78,cmb*3 + p * CC:F40A[t*2]` (strides 28 / 50 / 78 / F0 = 4 / 8 / 12 / 24 cells of 10 bytes:
  4 words per cell, then one dx,dy pair per cell);
* the row layout is `CC:F3B6[L*14]`, `L = t`, except a type 2 outside combatant 3, which takes `L = 5`
  (C1/1CA6-1CB8: `CMP #02 / BNE / LDA $9679 / CMP #03 / BEQ / LDA #05`). Each layout is six row offsets (words),
  then +12 = words per row, +13 = rows:

| layout | row offsets | words x rows |
|---|---|---|
| 0 | 0000 0200 | 8 x 2 |
| 1 | 0000 0200 | 16 x 2 |
| 2 | 0000 0200 0100 0300 0400 0600 | 8 x 6 |
| 3 | 0000 | 96 x 1 |
| 5 | 0000 0200 0300 0500 0400 0600 | 8 x 6 |

* the words are copied in order, row r at `$7E:(CC:F941[cmb*2] + offset[r]) + i*32` (C1/1CE7-1F39). A word whose
  low 11 bits are 0 clears its 32 bytes; otherwise it copies sheet tile `w & 7FF` (32 bytes of 4bpp planes from the
  combatant's sheet `$1C57,cmb*3`), bit-reversed through C0:FD00 when bit 14 is set (C1/1DD3).

## 2. The upload, CF/E414 (the NMI, one pair per frame, `$A4` = 0..3)

| k | source (CC:F957) | VRAM word (CC:F967) | length (CC:F977) | source 2 (CC:F95F) | VRAM word 2 (CC:F96F) | length 2 (CC:F97F) |
|---|---|---|---|---|---|---|
| 0 | 64AD | 0080 | 0100 | 66AD | 0180 | 0500 |
| 1 | 6BAD | 0400 | 0500 | 71AD | 0700 | 0100 |
| 2 | 70AD | 0680 | 0100 | 72AD | 0780 | 0500 |
| 3 | 77AD | 0A00 | 0400 | 7DAD | 0D00 | 0000 |

Every pair satisfies `source - 2 * VRAM word = $63AD`: mirror byte `m` is VRAM byte `m - $63AD`, i.e. OBJ tile
`(m - $63AD) / 32`. So `CC:F941` puts combatant 0 at tile 08, 1 at 20, 2 at 28, 3 at 40, 4 at 48, 5 at 60, 6 at 68,
7 at 80, 8 at 88, 9 at A0, 10 at A8.

## 3. The names, CC/ED87 and CF/EB94

CC/ED87 fills the OAM template `$11B0` with 44 entries `[00 00 name attribute]` from CC:FAF3 (`CPY #00B0`); CC/EDEB
then sets the enemies' attributes. Names, four per combatant: 08-0E, 20-26, 28-2E, 40-46, 48-4E, 60-66, 68-6E, 80-86,
88-8E, A0-A6, A8-AE. The compose (CF/E8E3) points at `CC:F84D[(0x0C + cmb)*2] = $11B0 + cmb*16` and CF/EB94 copies
the name and attribute of four entries per group for `($73,obj + 1) >> 4` groups -- a big enemy runs on into the
next combatants' entries. Cell `c` is then the 16x16 sprite of VRAM tiles `N, N+1, N+16, N+17`.

For layouts 0 and 1 in the slots the formations use, this is the old "cell c = words {2c, 2c+1, W+2c, W+2c+1}" law.

## 4. The render list, CF/E83A-E8A0, and the always-front entry

DP = `$1D00`, objects `$0C-$16` = combatants 0-10:

```
FE845  LDA $96E9,Y / BEQ skip        present ($96F5,cmb)
FE84A  LDA $1D17,Y / STA $8A,X        key = the foot Y ($1D23,cmb)
FE84F  TYA / ORA #40 / STA $A1,X      the body entry
FE855  LDA $A5C1,Y / STA $1D39,Y / BEQ skip
FE85D  LDA #01 / STA $1D39,Y          ... a flagged combatant ($A5CD,cmb) gets
FE862  LDA #FE / STA $8A,X            a second entry, key FE (in front of everything),
FE866  TYA / STA $A1,X                id without bit 6
FE87B  selection sort, descending (skip when key[Y] >= key[X+1])
```

The compose walks the list front first (CF/E8B4): `LDA $39,X / BPL` -- the FE entry draws the cells (and the effect
objects before and after them, CF/EDB8 / CF/EDF7), then `LDA #80 / STA $39,X / BRA` (FE8FA-FE904) marks the object
drawn and skips the shadow; the body entry finds `$1D39 = 80`, jumps to CF/E906 and draws only the shadow (CF/EA8A),
then, having bit 6, the afterimages (FE912-FE91E).

`$A5CD,cmb` is bit 7 of the enemy's E4:F600 record byte +4 (CC/EB75-EB86). Oracle, the attract Zombor
(build/_z_f7085.state, f330): `$A5CD[9] = 80`, keys `FE A0 7F 7F 7E 5F`, ids `15 4E 4F 4D 4C 55` -- the upper body
(foot Y 95) in OAM 4-11, the legs (Y 127) at 16-27. bat2's Avian Chaos (`19`, byte +4 = C0) is the other flagged
enemy in the captures.

## 5. The port

* src/battle.c `CtBattleEnemyCells` (sections 1-3), `CtBattleEnemyE3Rec` now holds 24 cells.
* src/field.c `battleEnemyPoseBlitT` draws its cells; `battleEnemyGenRender` takes types 0-3 (GATO in slot 3 keeps
  its dedicated renderer); `battleObjOrder` lists present combatants only and adds the front entry;
  `battleCombatantsRender` skips a flagged body entry's cells and the front entry's shadow.
* The WRAM render host (`CT_BATTLE_RENDERWRAM`) places enemies at the seed's live foot `$1D0C/$1D23` (CF/EAF2), no
  longer at the staged record's.
* `CT_BATTLE_CELLTRACE=1` prints each enemy's drawn cells when they change; `CT_BATTLE_ZTRACE` prints the front
  entry as `n(front)`.

GRADED by tools/enemy_cells_cmp.py (the port renders the cartridge's own WRAM; every on-screen cell of every staged
enemy against the cartridge's isolated OBJ layer; since the bracket port the tool compares rows y..y+15 of a sprite at
OAM y -- it compared y+1..y+16 -- and grades the selection bracket on its own line, battle_bracket.md):

| capture | frame | result |
|---|---|---|
| the attract Zombor (legs type 2 + upper body type 1, front entry) | 330 | 5040/5040 |
| bat2 (two Beetles, the airborne Avian Chaos) | 60 | 2947/2947 |
| bat9 (three Diablos, one walking) | 60 | 3057/3057 |
| bat8 (four Naga-ettes) | 60 | 4032/4032 |

Checks: battle-enemy-cells-zombor-vs-oracle, -bat9-, -bat2-. Before this work: legs absent, 2860/3040 with the legs
but without the front entry; bat9 2524/3072 and bat2 2078/3040 with the staged feet.

## 6. Open

* **The legs' pose in the live battle** -- RESOLVED 2026-09-27 by the anim drain's deadline on the zelda3 model's
  virtual beam (anim_drain_cad9.md sec. 16): the legs commit pose 00 before the staging, as on the cartridge. The staging copies the field object's current frame (`$0F01,X`) into the
  record's +4 byte, the boot's first published pose. The cartridge's field animation moves the legs (object 12) from
  frame 01 to 00 at f245 (C0/D55F), four frames before the staging at f249. Measured with `--hookpc C0CAD9` from
  build/_z_f7085.state (its frame labels run one ahead of `--monitor`'s): at f244 the drain visits object 11 twice
  (class 1, entry vPos 46 and 87) and the raster deadline (`$6B` = line 120) ends the pass with object 12 still
  queued; at f245 object 12's two visits (vPos 73 and 103) claim its three arena slots and change the frame, and the
  deadline holds its third pass to f246. The port's drain has no raster clock, visits object 12 at f244 right after
  object 11, fails the three-slot claim and pops it, so the frame stays 01 and the battle draws pose 01. This is the
  CPU-clock class (move.c's stated approximation).
* **GATO.** With the dedicated renderer switched off, this law draws bat1's GATO (type 2, combatant 6, layout 5)
  3024/3024 at f60, where the dedicated renderer gives 858/3024. Retiring that renderer needs its hurt pose, flash
  and dissolve paths checked against the generic ones.
* bat4 f60 keeps 5 pixels of Crono's sword inside an enemy cell: his published pose changes on that very frame
  (`$9907` CA -> 37 at f60) and the cartridge still shows the old pose's tiles -- the upload lag below.
* The VRAM upload lag (one pair per NMI, section 2) is not modelled: a pose change is drawn at once.

## Flea, and the two things that were wrong with her (2026-09-17)

User item, from an F5 savestate in loc 0AD and then a snes9x frame of the same fight
(`aux crono decompile/flea_frame.state`, spliced with tools/s9x_to_lake.py): *"flea has a different sprite
from what it should"*.

### 1. FIXED -- the cell layout is the PUBLISHED type, not the record byte

`CC/EA70 LDA $29C6,X / AND #07 / STA $967C,Y` reads the type ONCE, at the staging. The record byte it comes from
does not survive the fight: the end-of-battle survivor pass is `C1/B3E3 LDA $29C6,Y / AND #40 / ORA #40 /
STA $29C6,Y`, after which `$29C6,X & 7` reads 0 for every enemy.

MEASURED (`--monitor 7E29F6:1,7E9683:1 --diff --writer` on saves/attract_cena_inicial.state): Flea's `$29F6` is
`01` at f1744 (C0:05D8) and `40` at f2127 (C1:B3ED), while her `$9683` is `01` from f1773 (CC:EA78) and holds
until the field takes the page back at f2190 (C0:A689).

The port read the record byte live, so the moment the fight ended Flea collapsed from her eight type-1 cells to
four type-0 ones at the wrong offsets -- two fragments off the throne's left. It also never published `$967C` at
all. Both are ported now (the staging writes it, `battleEnemyGenRender` and the idle walk read it), and her box
against the cartridge's f2160 improves at every sampled frame.

### 2. OPEN -- the port gives her an idle animation the ROM does not

On the user's own frame the port's cells are **exactly** the cartridge's OAM shadow:

```
cartridge $7E:0700  (120,63)=80 (104,63)=82 (120,79)=84 (104,79)=86 (88,79)=88 (120,95)=8A (104,95)=8C (88,95)=8E
port                (78,3F)=80  (68,3F)=82  (78,4F)=84  (68,4F)=86  (58,4F)=88 (78,5F)=8A  (68,5F)=8C  (58,5F)=8E
```

-- the same eight, for `type=1 pose=00 foot=(120,111)`. What differs is WHICH POSE the port publishes. Measured
on the cartridge across the whole fight:

* `$990E` (Flea's `$9907`) reads only **00**, then **0F** at f1970 and **04** at f2012 (all by C1:1C69, the
  gather's own publish), back to 00 at f2189. The port also publishes **0B**, which the cartridge never does.
* Her idle-window rows `$9811` (`$97D9 + 7*8`) and `$97D1` (`$9799 + 7*8`) are **all zero** for the whole battle.
* Her committed mode `$974E` (`$974A + 4`) is **FF** from the staging (CC:E351) and never changes, although the
  request `$5E11` becomes 04 at f1855 (C1:9891).
* **The enemy idle walk barely runs at all.** `--hookpc` from build/_orc/s1850.state over 150 frames, with a
  positive control so a silent hook cannot be mistaken for a silent routine:

  | PC | what it is | visits in 150 frames |
  |---|---|---|
  | C1/1C4A | the gather (the control) | **13** |
  | C1/2DA2 | `LDA $A4`, the walk's head | **1** |
  | C1/3234 | the walk's ENEMY arm | 0 |
  | C1/333D | the enemy roster walk | 0 |
  | C1/3349 | its per-slot body | 0 |
  | C1/3364 | the window-row commit | 0 |

  So the head is reached ONCE in 150 frames and takes the PC branch (`C1/2DA4 BEQ`), and the enemy arm is never
  entered -- even though `--monitor 7E00A4:1` shows `$A4` itself cycling 0, 1, 2, 3 (CF:E479). The rosters are
  not the gate: group 2's arm (C1/32A5) always ends by writing `$992D..$992F` = 3, 4, 5 and falling into
  C1/333D, so had it been entered the body would have run.

The port instead calls `battleIdleWalk` with `h->schedA4` cycling 1..3, walks the enemies every dispatch and
gives Flea a window she does not have. Next step is the SCHEDULER, not the walk: find what reaches C1/2DA2 on
the cartridge and how rarely, and drive the port's dispatch from that. Same family as the other clock items
(the anim drain's raster deadline, the 087 pre-roll).

### 3. MEASURED 2026-09-18 -- the scheduler is not the fault either: the MAIN LOOP barely exists in this fight

User item, a second F5 in 0AD during the dual tech: *"she has a different sprite from the original ROM's"*. At
cartridge f1900 / port f3981 the cartridge draws her facing front (pose 00) and the port pose 0B -- the same
item as section 2. Hooks from a state at f1740 (`PEEK_SAVE`), 400 frames, positive control included:

| PC | visits | frames |
|---|---|---|
| C1/1C4A the gather (control) | 38 | f1793 .. f2082 |
| C1/106E the idle tick (C1/0051 entry 3) | **1** | f1856 |
| C1/2DA2 the walk's head | **1** | f1857, `$A4 = 0` -> the PC arm |
| C1/3234 the enemy arm | 0 | -- |
| C1/4058 the action (entry 4) | 1 | f1860 |

The main loop (C1/8127 `LDA #03 / JSR C1/0003`) is reached only after the boot's tail, and the boot BLOCKS twice:
`#08` battle-start post f1777 (CD:3B7D) -> the band f1778..f1791 (CD:0512/05C4/05FE, C1/003C spins on CD/0012) ->
the entrance ladder armed f1790 (C1:34FF) and stepped f1791..f1854 (`$E5` toggled by C1:36A0/3713, ended C1:3697)
-> the first idle tick f1856 -> `$A013 := 1` f1862 (C1:40A9). `$A4` does not move at all from the staging to f1858
(its INC rides the idle tick's frame waits). So in this battle the idle loop runs ONE iteration, it dispatches the PC
group, and no enemy walk ever runs -- Flea's rows stay zero because the walk never reached her, not because anything
gates her out.

The port (CT_BATTLE_WALKTRACE, battle frames): ladder f44..f80, `#08` at f116, combat start f116, the demo tech
committed f125, `$A013` f137 -- and the idle walk runs from f117: group 2 (Flea's) at f117 (commit + rows), f130
(publishes 0B), f137. Its boot order is inverted against the ROM's (ladder BEFORE `#08`), its scheduler advances
from `mvInited` (the boot) instead of from the first idle tick, and the window between combat start and the action
is 21 frames against the cartridge's 6. That is the battle boot/clock (the "battle's CPU clock" item and
the 0AD timing drift of the divergence assistant's triage), and the pose follows from it. Not changed here.

## 7. The demo pass never returns to the idle tick (2026-09-27)

The user, an F5 in the attract's 07D battle: *"one bat has a different animation from the other, that does not happen on the
cartridge"*. After the Frog Squash the right Vamp (slot 3) raised its wings for the ~36 frames of the teardown
(cartridge c5140-5176) while the left one, and both on the cartridge, kept the tech's closing pose 01.

The published pose `$9907+cmb` has two writers: the anim script's gather and the idle tick's walk (C1/10A9 ->
C1/2D9F -> C1/3407). On the cartridge the walk dispatch runs ONCE in the whole 07D battle (`--hookpc C12D9F` from
c4500: c4614, the first iteration's, before the demo's turn), and `$9694` holds 01 01 01 01 02 02 02 02 from staging to
the end. A demo battle is one pass: the ATB handler takes the demo's turn (C1/8461 -> C1/B725 -> C1/B967), C1/B967
returns only once the action has run, and the loop tail C1/83AE `LDA $2989 / BIT #20 / BNE ->83B8` ENDS the fight
instead of `JMP C1/8120`. Measured from c5100: the action ends c5133 (C1:40D3), C1/B223 c5136, C1/8345 / 83AE / EA9D
c5137; C1/8120 and C1/106E are never reached.

The port's frame-A gate (`battleIterFrameA`) knew the pre-dispatch half of that (`demoDispatchAt`) but not the rest:
between the action's end and its demo end it ran one more frame A in every demo battle (0AD f366, 07D f619, 087 f523).
At the 07D that dispatch committed slot 3's idle window (mode FF -> 00) and published its first pose, 00. The gate is
closed now while `h->demoPass` is set (from the demo's turn to its end, the span the ROM spends inside C1/B967): one
walk per demo battle, the cartridge's. Check `attract-demo-battles-walk-once-vs-cartridge`
(tools/attract_demo_walk_cmp.py).
