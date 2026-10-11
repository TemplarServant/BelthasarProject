# A PC physical runs its own script (2026-09-18)

A PC's plain attack is loaded and played by the same machine as a tech: C1/432C picks a script, bank CE holds it,
and the anim interpreter C1/4CA2 runs it until its op 01 ends the action (C1/40CE). The port used to replace all of
this with a Crono-only route: the op-18 walker run outside any script, a pose ladder anchored on the arrival, and the
weapon sound, crit washes, strike and span at measured offsets. Now the script runs on the port's interpreter
(`battleAscTick`). Graded against the LOGICAL-FRAME oracle (CLAUDE.md's one exception;
[oracle_logical_frames.md](oracle_logical_frames.md)) by `tools/pc_physical_logical_cmp.py`.

## 1. The ROM path (`python tools/dis.py 1432C 0xD8 1 1`)

C1/4310 (the action dispatcher) clears `$9877..`, sets `$987C = FF` and jumps through `$7A63` on `$AE92`. The
physical branch C1/432C:

| step | ROM | what |
|---|---|---|
| verdict | C1/433A JSR C1/49FF | `$9930`: 1 = close. The distance check C1/2AE3/2B49 against 0x640, facing C0/F700, CC/F63C's melee flag, then C1/4A71's path test (`$92` bit 7 = far). |
| weapon | C1/433D-4349 | `$A4E7 = $1C48[CC/F83F[$AE91]]` = `$1C48[5 * seat]` (op 7C/7D read it) |
| config | C1/434C-4400 | `X = $A4E7 * 15`; five bytes into `$987A/$987B/$987E/$9881/$9884` from one variant: crit CD/401F (`$AE9B` bit 7, C1/435B), close CD/401A (`$9930 != 0`), far CD/4015 |
| script | same | `$9877` = CD/400E (crit) / CD/4007 (close) / CD/4000 (far) `[$2980[$AE91]]`, the character id |
| tables | C1/4401-4424 | `$987F = D1:0000[$987E*2]`, `$9882 = CD/686E[$9881*2]`, `$9885 = CD/64B0[$9884*2]` |

Selector bytes, by character 0..6: far `0E 02 04 11 0E 0E 12`, close `00 03 05 06 08 0A 0C`, crit
`01 0F 10 07 09 0B 0D`. Crono's slash is sel 00, his crit sel 01; Marle's far shot sel 02, her crit sel 0F.

`$AE9B` bit 7 is the crit roll's own write (C1:EDB8, from the roll C1/ED8C on block+0x45). The boot computes that
rate (FD:B233), so a poke to `$5E72/$5EF2` at load does not survive; the tool's `nocrit` scenario pokes a
post-boot state.

## 2. The ops the physical scripts reach that the port did not run

Crono's slash, track 0: `1E 0A | 36 | 7C | 03 31 | 36 | 1E 2A | ...`. The super CE/D4C3 = `07 | 02 06 | 18 03 |
72 0D | 1F`; the return CE/D776 = `76 03 | 19 | 08 | A8 01 | 26 0E 02 | 0C | D0 | 98 02 19 | 26 0E 01 | 20 03 | D1
| 1B 19 | 06 0E | 20 05 | 72 0D | 06 03 | 1F`.

- **`18 rr`** (C1/542D -> C1/5437): the grid walk. The first visit arms the walker (C1/5480), later visits are the
  segment controller C1/54C1 after the stepper CF/EC78, and the op completes when the victim is one clear cell ahead
  (C1/55FE). The port's walker (`battlePcWalkArm`/`battlePcWalkTick`) was already this; the op now drives it.
- **`7C` / `7D`** (C1/61EB / C1/61F5): the weapon's two sounds, CD/6D4C / CD/6DAC `[$A4E7]`, cmd 18, pan = the
  actor's `$1D0C`. FREE: the `INC $A00F` is at C1/6213, after the JSL C7/0004. The free-flag scan read the
  `LDA $CD6D4C,X` operand byte 4C as a JMP, so both are on `battleAscOpFree`'s explicit list.
- **`44`** (C1/5A3E): stores the track's own position into `$5D9E/$5D9F` (a combatant's `$1D0C/$1D23` of
  `$A014[track]`, an effect object's `$A240/$A250`), advance 1, the frame spent. Marle's crossbow crit stores the
  bolt's point with it at CE/0842 (oracle f983, C1:5A6B), and CE/084B's `1A` puts the bolt back there for its second
  flight. Without it `1A` read (0,0) and the second flight was 8 steps instead of 2: t8 and t3 ran 6 frames late and
  the span was 145 against 139.
- **`07`** (C1/4F1A): `$A324,X = $98BF,X`, `$A2FE,X = $973F,X` for the track's combatant (track 4 loops its list,
  an effect object is left alone), the frame spent. A PC seat is speed 2 / rate 1 (CC/E3DE-E3EC); an enemy's pair is
  its E4/F600 record's byte +9 (& 7 into `$98C2`, >> 4 into `$9742`, CC/EBCC-EBE0). In the scripts reached, a speed
  op rewrites the pair before any move reads it, so nothing here grades it.

## 3. Two laws of the move ops, now the ROM's

**A parked move op keeps its cursor.** The arm (C1/5008 for 11/12, C1/63E3 for 98..9B) sets `$A337`, stores the op's
length in `$A3A9,X` and ends `LDA #00 / JMP C1/75BB`: the cursor stays ON the op. Every later visit re-tests the
service's finished flag `$A19D`: clear, advance 0 (C1/653B); set, `STZ $A337` and advance by `$A3A9` (C1/63F0 ->
C1/6540). A8/A9 the same with a fixed 2 (C1/6AEB / C1/6CEE). The port advanced the cursor at the arm, so its
`CT_BATTLE_ASCIP` trace ran a frame ahead of the cartridge's `$5DBD` on every parked move (the frames the ops ran on
were already right).

**A zero-count slide still parks one tick.** The service's modes 0/1 (CF/F03E, CF/F087; the effect object's CF/F4B2,
CF/F503) step and DEC the count, and the tick that zeroes it sets `$A19D`. A count already zero steps nothing and
sets `$A19D` on its FIRST tick (CF/F045 `BEQ ->F081`; CF/F091 `BNE / JMP CF/F18B`, which also zeroes `$A4AF`). So a
slide of 0 steps takes two frames, arm and release. Crono's return `98 02 19` from his own home: arm f681 (C1:63E8),
f682 CF:F193 then C1:63F3. The port released it at the arm, one frame early.

The SLIDE-MID op (C1/7235) arms a slide with `$A337 = 0`: its count runs without parking, and a later WAIT-MOVE op
holds the track. The port's park condition keeps both (`ascMvOn || ascMvN > 0`).

## 4. What is graded (`tools/pc_physical_logical_cmp.py press|crit|nocrit`)

The two fights are different fights (another fixture, another input), so the actions are paired by order and each is
graded against its own `$A013` rise: the span, every cursor change of tracks 0, 3 and 8, and the actor's and the
victim's feet frame by frame from the script's arm (the victim's MOVE, from its own start, where GATO had wandered
elsewhere before the action).

| scenario | actions | result |
|---|---|---|
| press (gato_press_action) | Crono sel 00 x2 (the second walks 24 steps and slides home), Marle sel 0F x2 | spans 124 / 139 / 141 / 141 both; every cursor and foot identical |
| crit (bat3crit / forest_pre) | Crono sel 01, the weighted leap | span 169 both; cursors and his 15 moves identical |
| nocrit (press f300, rates 0) | Crono sel 00, Marle sel 02 | spans 124 / 135 both; identical |

The two crit washes are the script's own flash ops now: port rel 50 / 88 at peak brightness, oracle `--peek` 52 / 90
(the same 38-frame gap; the two measure frames differently). The slash sounds post at rel 31 / 70 on the port and
the logical oracle's `$1E00` shows 32 / 71: a 39-frame gap, where the REAL cartridge's 41 held a lag frame.

## 5. What is left of the old route

- `battlePcAtkArm`'s route, `battlePcAtkTick`, the arrival-anchored skeleton and the crit leap run only when no ROM
  is loaded.
- The `strike` trace event of a scripted action is still a constant rel from the dispatch (`CT_CRONO_STRIKE`,
  `CT_MARLE_STRIKE`, `CT_CRONO_CRIT_STRIKE`), as it already was for techs: no ROM event is called "the strike"
  (battle_hurt_flash_sync.md A9.1). While the interpreter drives, the flash and the enemy's hurt pose are the
  script's; the constant only moves the trace anchor.
- GATO's counter after a PC physical opens at the teardown + 3 in the port, + 2 on the logical oracle (f735 ->
  f737: no iteration head between, the next turn's C1/4058 runs straight after C1/41BD). `CT_BATTLE_COUNTER_DELAY` is
  still the fitted constant.
- C1/49FF's path test C1/4A71 is not in the port's close/far verdict (a melee weapon is always close).
- Ops that still fall to the interpreter's inert default in the suite's fights: none (tools/battle_asc_census.py,
  2026-09-30: 152 / 152 PC scripts use only executed ops; 27/28, 43, C0-C3 and 50-55 have their cases --
  battle_asc_inert_ops.md, battle_asc_ops_orbit_shake_offsets.md).
