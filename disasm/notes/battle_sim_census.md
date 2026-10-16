# BATTLE — the battle_sim census: every catalogued enemy and pack, port vs the rule's cartridge (2026-10-15)

Read-only triage: no engine code changed. Every claim cites a ROM address (`tools/dis.py`), an oracle observation
(`build/ct.exe` under the rule, `CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`) or a port line. The per-fight result,
all 809 fights, is in [battle_sim_census_fights.tsv](battle_sim_census_fights.tsv).

## 0. What ran

- `tools/battle_sim.py sweep all hp=1` (237 enemy ids a script loads, alone) and `formations hp=1` (572 catalogued packs),
  each fight 1500 frames from `saves/battle_re/gato_wait.state`, A mashed from f640, every enemy's HP poked to 1 at f600.
  Run through a wrapper that calls battle_sim's own `staging_pokes` / `layout` / `grade` and battle_ab's `run`, adding
  only the per-frame rows and the per-frame picture match saved to disk (the graded text is the tool's). Five
  formation tags repeat in the catalog (one object placed twice: f01C_13, f047_29, f086_11, f087_11, f0B5_8); the
  second gets a `b` suffix (`battle_sim.py formations` itself crashes on them: two jobs delete each other's thumbs).
- Exes frozen in `build/_census_bin` (copies of build/ at 12:37, commit 3caa430 + the working tree's battle_sim
  changes), so a rebuild by another session could not change them mid-run.
- **Two passes.** `as-is` = battle_sim exactly as it is. `fixed` = the same fights with the port's buttons mapped from
  field f216 instead of f220 (family 1 below: battle_ab's own mapping is 4 frames off). The as-is pass is what the task
  asked for; the fixed pass separates the engine's residuals from that harness offset.
- Output (gitignored, `build/_battle_sim/`): `sweep_hp1.txt`, `formations_hp1.txt` (as-is, battle_sim's text);
  `*_start216.txt` (fixed); `rows/`, `rows216/` (per fight: both sides' monitored WRAM per frame + picture match);
  `classified_rows*.json`; `scripts/` (the wrapper, the classifier, the probes used below); `native_hp1.txt` (GATO's
  own fight, the baseline).

Classification (`scripts/classify2.py`), first class that applies:
(a) the staged ids differ / no fight; (b) a monitored WRAM value sequence differs (frames ignored), the known `$993C`
residual masked (bytes of combatants the cartridge never staged; the port writes FF there, battle.c host-lifetime
restore) and a difference that is only a change missing in the last 30 frames on one side treated as timing;
(c) the death palette (OBJ pal 4 `$2380` while a combatant draws with it) differs; (d) same values, other frames;
(e) same values and frames, picture differs; `same` = nothing differs.

## 1. Counts

| pass | kind | a | b | c | d | e | same |
|---|---|---|---|---|---|---|---|
| as-is | sweep (237) | 1 | 31 | 7 | 188 | 10 | 0 |
| as-is | formations (572) | 14 | 106 | 13 | 421 | 18 | 0 |
| fixed | sweep (237) | 1 | 28 | 7 | 176 | 25 | 0 |
| fixed | formations (572) | 14 | 91 | 11 | 411 | 45 | 0 |

No fight is `same`: the port's death recolor is one frame late on every corpse (family 2) and the picture has
hundreds of small mismatched frames per fight (§8), GATO's unstaged fight included.

Families (number of fights; a fight can carry several):

| # | family | as-is | fixed | where |
|---|---|---|---|---|
| 1 | port buttons 4 frames early (harness) | 532 | — | tools/battle_ab.py |
| 2 | death recolor one frame late (+ multi-kill queue) | 614 | 614 | port, CF/FE20 timing |
| 3 | the next action opens one menu pass (4 f) early | — | 213 | port, C1 main loop (open) |
| 4 | a fallen enemy's block-2 counter never runs | 60 | 60 | port, battle.c:12167 |
| 5 | boss death `CD/0E05` #38 never runs | 27 | 27 | port, FD/B121 + CD/0E05 |
| 6 | the cartridge cannot stage the fight (sprite buffer) | 15 | 15 | staging artefact |
| 7 | picture: post-battle field, tech BG effects, fleeing enemy | 295 | 251 | mixed |

## 2. Family 1 — the port's buttons land 4 frames early (HARNESS, not the engine)

The "port drifts 2-8 frames early under the A mash" residual. `battle_ab.run` maps the drive to the port's
`CT_BATTLE_SCRIPT_ABS` frames from `battlephase: begins at field fN` (tools/battle_ab.py:214), which it searches in the
port's **stdout**; src/field.c:17795-17796 prints it to **stderr** and only under `CT_BATTLE_TRACE`, so `start` keeps its
default 220 (battle_ab.py:198). Measured with `CT_BATTLE_TRACE=1`: the battle begins at **field f216** (enemies 06 and
00 alike).

- e06, as-is: the menu-pass clock `$9931` is in phase on both sides (…6D at f639, 6E at f640…), but the NMI's edge
  latch `$EE` = 80 lands at port **f637** for the A pressed at cartridge f640 (cartridge `$EE` f641, CD:038B,
  battle_menu_pass.md); `$9609` f639/f643, `$95D5` f651/f655, `$A013` f655/f659.
- e06 with start 216: `$EE` f641/f641, `$9609` f643/f643, `$A013` f659/f659 — the first action on the same frame.
- Port frame − cartridge frame over every matching change of the 809 fights: as-is +0 ×12002, −3 ×6942, −4 ×6581
  (of 37099); fixed +0 ×17610, +1 ×9763 (family 2's fade states), −3 ×2763, −4 ×2499 (of 37418). The −3/−4 that remain
  are family 3 and what follows it.

Fix (tooling, not done here): set `CT_BATTLE_TRACE=1` for the port run and search stderr, or take 216.

## 3. Family 2 — the death recolor is one frame late (854 corpses, 614 fights, every fight with a dissolve)

Cartridge, Nu (e00), `--monitor 7E993F:1,7E2382:2,7EAF02:1,7E5FCA:1,7E0598:2,7E1E01:1 --writer`:
f894 `$AF02` = FF (C1:B2C3), dead flag `$5FCA` = 80 (C1:B2B9), **`$993F` = 08 (CF:FE5C) and `$2382` = A508 (CF:FE6B) in
the same frame**; the #95 post f895 (CD:3B74); the fade record `$0598` = 40 C0 f896 (CD:0C5D); fade steps f896-f910
(FD:E989); record off f911 (FD:E849). CF/FE20 is the post-action death scan (C1/AC75 → C1/006D `JSL CF/FE20`,
battle_death_dissolve_gate.md PART 5), so it runs in the delist's own frame.

Port, same fight (start 216): delist f894, dead flag f894, **recolor f895**, post f896, record f897 — the whole chain
+1. The port copies at its countdown's `deathSfxDue == 2` step (src/battle.c:14885, `battleDeathPalCopy`), a frame after
the death, and the post and record follow it. Over the census: (cartridge recolor − delist, port recolor − delist) =
**(0, +1) on 854 corpses**, never equal.

Multi-kill variant (8 fights): the cartridge recolors the corpses one at a time, 43 frames apart (CF/FE20 walks X = 10
down to 3 and calls CD/0043 inside the loop, battle_death_dissolve_order.md). In those 8 the port's `$993C` := 08 for a
queued corpse lands at +1 anyway ((43, +1) ×5, (86, +1) ×2, (129, +1)); in 4 others the port queues it too, 1-5 frames
early ((43, 42), (86, 83), (129, 124)).

## 4. Family 3 — the next action opens one menu pass early (fixed pass, ~220 fights)

What remains of the −4 once family 1 is removed: the first offset of 184 fights is `$AE93` −4, of 26 `$9877` −4, of 10 a
status byte −4. e8F (fixed): an action closes at f740 on both sides; the cartridge restarts `$9931` (CC/F0E6 in
CC/F06B, writer CC:F0E9) at **f748** and raises `$A013` f750; the port restarts it at **f744** and raises `$A013` f746.
CC/F06B at that point is C1/4058's open wipe (battle.c:11500 comment), so the port opens the following action one
4-frame menu pass before the cartridge. In the cartridge `$993A` is raised again at f745 and consumed f746 before the
open; the port opens without that pass. Seen after a PC is KO'd at the close (e02 f840, e8F f740, eB8 f909) and after
plain closes (f1A0_21 f826). Downstream: the anim selector of the next PC attack differs (e9E: `$9877` 02 vs 03 at
f709/f705) and every later action shifts.

Open: the C1 main-loop path between the close (C1/40D3) and the next C1/4058 — which pass gates the open. A smaller
cluster (~35 fights) has an enemy's next action decided 11-13 frames early right after a close (eA1: `$AE93` = 41 at
c811 / p800; f1CB_1: 86 at c390 / p379), same open question.

## 5. Family 4 — a fallen enemy's block-2 counter never runs (60 fights, 28 enemy ids)

**FIXED 2026-10-16** (battle_counter_gating.md sec. 8; check battle-sim-fallen-enemy-counter-vs-rule-cartridge): the
loop's gates at the victim's decode, the death scan after the counters (C1/881C), the counter's name box and its C1/8787
sweep. 19 of the 22 ids (20 with Azala in a 1450-frame window) and 29 of the 38 formations now match the cartridge's whole WRAM sequence; the rest are
families 3 and 5, a staging difference (f0AE_10) and a port stall on Crono's physical (f1BE_18).

ROM, C1/8694-86C6 (`dis.py 18640 0x1E0 1 0`): in the counter loop, a victim enemy whose `$AEFF,X` = FF (fallen) is
taken anyway when its AI block 2 opens with condition `20` (C1/86A5 `LDA $AF0A,X / JSR C1/B488`, C1/86B4 `CMP #20`): its
`$AF02,X := $AF0D,X` (C1/86BC-86C3) and its block 2 runs. The only gate after it is C1/86FF `LDA $5E4B,X / BIT #8F`
(+1E: stop, lock, chaos, sleep, blind) — **no dead-flag test**. Condition 20 (C1/97AB) commits when `$AEB3,X` is set.

Block 2 of every affected id opens `20 00 00 00 FE 02 <tech> ...` (CC/8B08 pointers): tech 7F (Mud Imp 0F, 6E, A8, BC,
C2, D1, D4, E6, E7, EF, F1, B0), 57 (B4, C4, F0), 76 (C0), 18 (F2), 54 (9D), 1F (9A, DF) …

Cartridge, Mud Imp (e0F, fixed): HP → 0 and `$61CA` (+1D) = 80 at **f930**, `$AE93` = 7F f930, `$9877` = 7F and
`$A013` = 01 f932, the action to f956, **`$AF02` delisted f956**, then the dissolve. Port: HP 0 and dead flag f926, no
`$AE93` change, delisted f926 — the death tech never plays and the corpse dissolves 30 frames early. The port's arm
(battleCounterArmROM, battle.c:7745) does take the fallen enemy and restore `$AF02`; its dispatch,
battleReactiveCounterTick, then drops it: `if (!battleAlive(s, cmb) || …)` (src/battle.c:12167), and `battleAlive` is
false on the dead flag (+1D bit 7) the ROM never reads here.

Ids (fixed pass): 0F, 6E, 95, 97, 98, 9A, 9D, A1, A2, A6, A8, B0, B3, B4, BC, C0, C2, C4, C7, D1, D4, DF, E6, E7, EF, F0,
F1, F2 (Mud Imp, Golem, Masa, Mune, Azala, Slash, Dalton, Dalton Plus, Ozzie, Great Ozzie, Zeal, Zombor, Black Tyrano,
Atropos XR, the Lavos forms, Yakra XIII, Giga Gaia, Guardian, Flea, Roly Bomber …).

## 6. Family 5 — the boss death (`CD/0E05` #38) never runs (27 fights, 11 ids died in the census)

**FIXED 2026-10-16** (battle_boss_death_0e05.md; check battle-sim-boss-death-cd0e05-vs-rule-cartridge): FD/B121, CC/EBA4
and CC/EC63-ECAF staged, CD/0E05 on its frames, program op 80's sub-ops 0F and 11, and the dissolve's level paced by
CD/3ED2's packets. Every family-5 fight, and CC / CD / D1 / D4 / BC / BD, now shows the cartridge's death palette
sequence. Left: Ozzie's second death 8 frames early (A6 and its packs; -8 x10, the rest +0 -- family 3's class);
f0A9_11 and f1D3_17 keep their earlier timing residuals (the same before this fix); f05E_7's first death 63 frames late
(also before).

ROM: CD/0043, the corpse's death routine (`dis.py D0043 0x90 1 0`): after the `$CCEA` wait, `$99D2` = 0 → D0083:
`LDA $A01D,X / AND #01 / BEQ D0093` else **`LDA #38 / JMP CD/0E05`** — no #95 post, no CD/00A5 fade record. `$A01D,X`
for an enemy is written once at the boot by **FD/B121-B13E**: `$A020,Y := $5E47,X & 03` for the eight enemy blocks
(block +0x1A = the stat record CC/4700 + id·23 + 21, FD/B438). 25 catalogued ids have bit 0 (04, 25, 8F, 90, 99, 9B, 9C,
9D, 9E, 9F, A1, A6, A9, BC, BD, BE, BF, C3, C5, C6, C7, CB, E6, EB, EC).

Cartridge, Yakra (e90): `$A020` = 03 at f271 (FD:B12F); death f834; then a scripted sequence — three palette flashes on
colour 0x10 (`$05A4` slot armed by CD:2A10 at f845, f862, f912; FD/E863 steps it every 2 frames) and from **f947** an
OBJ-pal-4 fade toward white, one step every 4 frames (`$05A4` = 40 C0 0F 08 …, FD/E8D7-E92D, `$2380` 0900 → 2A04 →
4B08 …): 190 palette states against the port's 18. The party's level-up comes at c1458 / p942.

Port: `$A01D..` stays 00 (no FD/B121; the port runs the FD/B0FB-B10C loop right before it, battle.c:16444), so the
D0087 arm it already names (battle.c:14898-14905, "CD/0E05 poster: NOT PORTED -> trace, no guess") is never reached and
the regular 18-step dissolve plays. Two gaps: FD/B121's copy, then CD/0E05's effect #38 itself.

Same evidence family, unexplained: Lavos CC/CD (eCC, eCD) — the cartridge copies the death palette at f379 and runs no
fade (1 state), the port fades (17); `$A01D` bit 0 is clear for them, so it is the other arm (D0057-D007E, `$99D2` ≠ 0
or `$A08C,X`) — not traced.

## 7. Family 6 — the cartridge cannot stage 15 fights (staging artefact)

The port stages all 15 at f216; the cartridge never sets `$AF02`. 10 of them (eCE Lavos Core; packs f01F_8, f05C_2,
f082_17, f08C_15, f0CB_12, f12A_25, f131_11, f155_9, f1DF_8): the CPU loops at **C0/2E65** with a magenta backdrop
(`--cputrace`: 2723 × C0:2E65 per frame on f082_17, f05C_2, f131_11). C0/2E1E is reached by `LDX #7C1F / BRL` only from
C0/46D1 and C0/483A, each right after op 0x83's sprite decompress (`JSL C3/0002`) and `$0171 += $0306 / CMP #8000 /
BPL` — the field's sprite buffer is exhausted: GATO's arena already holds its own graphics, and these packs add up to
five more. 4 (f03F_10, f0B4_8, f0CA_8, f1DB_12) never complete frame 0 under the rule's oracle (no frame row in 120 s;
the oracle had to be killed — the rule's infinitely fast CPU never reaches a wait); not traced. f0CD_9 (Ozzie alone,
op2 0A): the battle boots into a garbled RAM (`$AF02` = FF AA FF AA …, `$A013` = AA at f271) and the oracle later stops
advancing. None of these is reachable from the vanilla game's own scenes; the port has no such halt. Staging these packs
needs a lighter arena than gato_wait.

## 8. Family 7 — the picture

Baseline: every fight, GATO's unstaged one included (`native_hp1.txt`: 564 of 1224 frames with no exact match), has a
few hundred frames with small differences (HUD gauge segments, the entry pose, the 1-frame recolor of family 2). The
ones that matter are frames over 400 px:

- **Post-battle field (harness):** 30+ fights whose fight ends early enough show 3000-5600 px from ~f1150 to the end:
  the port keeps GATO's post-fight box ("I lost! You won! …") open while the cartridge closed it (its obj0 IP 018B →
  018D at c1300). The drive mashes A on the cartridge to f1480, but the port's button script covers the battle only —
  the port's field receives nothing after the return. Not the engine.
- **Enemy tech background effect missing:** enemy B9's "Chaotic Zone / Chaos" (enemy tech 58, CD/5526 record →
  fx 23 → D1:6240 programs 27 and 28, palettes 14/14): the cartridge darkens the arena to blue and draws a black disc
  with lightning (c520-760), the port keeps the arena lit and draws neither; the PCs' white flash matches
  (`build/_battle_sim/eB9_pic.png`). Similar in-battle runs on 9C (Nizbel II, c579-632). battle_fx_program_census.md
  covers PC techs and items; enemy-tech programs are not in it.
- **A fleeing enemy is not drawn:** Retinite (3A) "Runs away!" (`$AF02` change at f413): the cartridge still draws it
  leaving, the port draws nothing (`e3A_pic.png`, f420).
- **A victory line out of step:** e3A f900, "Learns Dual Tech, Ice Sword!" up on the port, not on the cartridge.

## 9. Smaller (b)/(c) groups left in the fixed pass (not traced)

- `$993C` value / missing on the port in 13 packs (f013_30, f070_23, f077_38, f077_53, f083_20, f0AB_14; f083_41,
  f0D7_8, f0E0_9, f0E8_8, f10B_9, f169_8, f1A0_15): an OBJ palette slot sequence differs (a hurt flash or a recolor).
- Death palette only (c) in 8 packs (eFA, f077_17, f077_27, f081_20, f096_14, f0D4_29, f199_24, f199_29).
- eA7 (Ozzie 2nd form): `$62CA` +0x21 = 20 on the cartridge at the staging (f273), 00 on the port — an initial buff.
- e1B, f0B5_8b, f0D7_12, f13A_16/19, f1AE_8, f1D3_14, f0DB_14, f125_11/19, f126_11: one each, see the TSV's
  `first_diff_fixed`.

## 10. Repro

```
python build/_battle_sim/scripts/oprobe.py 0F "7EAE93:1,7EA013:1,7EAF02:8,7E61B0:2,7E61CA:1" 1000 --writer   # family 4
python build/_battle_sim/scripts/oprobe.py 90 "7EA01D:11,7E05A4:12,7E2380:2" 1000 --writer                 # family 5
python build/_battle_sim/scripts/oprobe.py 00 "7E993F:1,7E2382:2,7EAF02:1,7E0598:2,7E1E01:1" 920 --writer  # family 2
python build/_battle_sim/scripts/abprobe216.py 8F "9931:1,00EE:2,A013:1,993A:1" 736 752 780                # family 3
python build/_battle_sim/scripts/abprobe.py 06 "00EE:2,9931:1,9609:1" 630 662 700                          # family 1
```
(`oprobe` = the census fight's cartridge side with any `--monitor`, on `build/_census_bin/ct.exe`; `abprobe[216]` = both
sides through battle_ab.run, on `CT_BUILD_DIR` or build/ (unchanged since 12:37, the same exes); both take battle_sim's
enemy spec.)

## Open questions

- Family 3: the C1 main-loop pass between an action's close and the next C1/4058 that the port skips.
- Family 5: CD/0E05's effect #38 (its program, the flashes at colour 0x10, the 4-frame fade) and the D0057 arm (Lavos CC/CD).
- Family 7: enemy-tech effect programs 27/28 (fx 23) — which ops the port lacks; the fleeing enemy's sprite.
- Family 6: the four packs that hang the rule's oracle at the staging.
