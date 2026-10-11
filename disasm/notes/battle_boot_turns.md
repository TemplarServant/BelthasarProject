# The battle's opening and its turn machinery -- the ROM's, ported (2026-09-18)

User item, after the attract's 0AD: *"fix the battle's opening"* -- the demo battle's action started ~21 frames
late, the battle-start sound 84 frames late, and during the idle loop the port walked Flea's group and published a
pose (0B) the cartridge never shows (battle_enemy_cells.md section 3).

## 1. The boot, in the ROM's order

C1/0023 (C1/0051 entry 0) ends `JSL CD/000C / JSL CD/0012 / BNE ->003C / JMP C1/34DB`:

1. **CD/04FD posts `#08`** (`LDA #08 / LDX #0080 / JSR CD/3B71`, the battle-start sound) and seeds the band
   (`$CA1E = FF`, `$CA1F = 17`). The post's APU handshake carries the seed into the next frame: `$1E01 = 08`
   (CD:3B74) is always the frame BEFORE `$CA1E = FF` (CD:0512).
2. **The band**, 13 frames; C1/003C spins on CD/0012 until it is shut.
3. **The entrance ladder** C1/34DB (window 0x30) and its copier loop C1/3681, **64 frames**, ended by C1:3697.
4. C1/0003 returns to C1/8030: the two sampling-with-retry shuffles, **C1/8C09's AI init**, C1/80EB (the demo's
   `$AFAB..AD = 01`) -- CPU only, no frame wait anywhere.
5. **The main loop**: `C1/8120 TDC / INC $0027 / STA $B2C7 / LDA #03 / JSR C1/0003` (the idle tick C1/106E), then
   C1/8130 `JSR C1/B223` (the dead-flag sweep), the end tests, C1/81CA's Wait gate, the timer pass, the cursors.

MEASURED (`--monitor 7E1E01,CA1E,0027,B185,B03A,AFAB,A013 --diff --writer`), frames from the band seed:

| fight | seed | `INC $0027` | timer pass | first cursor find | k |
|---|---|---|---|---|---|
| attract 0AD (Flea) | f1778 | f1855 (+77) | f1857 | f1858 (+80) | 0 |
| attract 087 (Zombor) | f7368 | -- | -- | f7449 (+81) | 1 |
| GATO (gato_wait, `--input "100:8,0:600"`) | f298 | f376 (+78) | f379 | f379 (+81) | 1 |
| forest (forest_holdleft_pre, Left held) | f245 | f324 (+79) | f326 | f327 (+82) | 2 |
| attract 07D | f4530 | f4612 (+82) | f4614 | f4615 (+85) | 5 |

**k (step 4) is CPU work** -- not derived; `CT_BATTLE_BOOT_CPU = 2` (the forest's).
(2026-09-28, the attract 07D on the LOGICAL oracle under the rule, `--monitor 7ECA1E:2,7E0027:1,...`: the seed `$CA1E =
FF17` f4483, `INC $0027` f4563 = +80 -- k = 5 there too: step 4 has no wait loop, so the rule's valve runs it in real
time. The port's 75 + 2 lands the 07D's combat start 3 frames early (f6575 -> f6652), the bulk of that leg's -9. A price
for k needs step 4's cycles: the shuffles' retries (the RNG is the cartridge's now) and C1/8C09's AI init per enemy.) The port's intro is the main
loop's start less two (its first logic tick fires four frames after the intro, CT_BATTLE_TICKACC0, and is the timer
pass): `battleBootIntro = arm + 75 + k`, for a field-entered fight AND for a `--battletest` fixture (whose arm is the
pinned 69). The old `arm + 84` / `159` put the first pass 7..14 frames late.

`$0027` **survives between fights** (the 087 starts at 10, left by the 07D) and is cleared only by the full-WRAM
reset C0/000E (no CPU writer: the attract's reboots at s1740+596/+1625). Its parity decides whether the first
iteration runs the cursors.

## 2. One iteration's turn machinery (C1/8120-8345)

* **C1/81CA** `LDA $99E0 / BEQ ->81EC`: the Wait gate skips EVERYTHING below -- timers, cursors and handlers.
* **C1/8231 the timer pass**: 143 timers `$AF27` = thirteen arrays x eleven combatants, flags `$AFB6` (bases at
  C1/B947). Array 12 is the ATB (`$AFAB`, flags `$B03A`). A timer counts only while its flag is non-zero with bit 7
  clear; at 0 the flag gets bit 7 (C1/826F). With array 0's flag set for the same combatant, the pass counts array 0's
  timer instead (C1/8247). The demo's enemies, parked at FF, DO count (0AD f1857 `$AFB2 = FE`).
* **`$B03A`**: 0 = stopped (dead/hidden C1/B2BB, a status C1/B0C9), 1 = running, bit 7 = ran out. Every reload of the
  timer re-arms it the same way (`if ($B03A) $B03A = 1`: C1/BCEE, BE2D, BEAA, BF27); the enemy's own turn writes the
  1 outright (C1/84E4); C1/B392 does not touch it.
* **C1/82A2, odd `$0027` only -- the cursor pass**: for each array Y, the cursor `$B179,Y` walks `$B163` (the turn
  order the boot shuffled) from where it stands to the first combatant whose Y-flag has bit 7 -> `$B186 |= bit(Y)`
  (Y = 12 -> bit 0) and the cursor = that index + 1 (no wrap test on this arm); walking off the end (>= 11) puts it
  back to 0 and finds nothing.
* **C1/8313, odd `$0027` -- the handlers**: `JSR ($B92D,X)` for each Y whose `$B186` bit is set. Handler 12 =
  **C1/8461**; its C1/847E reads the combatant at `$B185 - 1` (`$AEC7 - 1` when the cursor is 0). Enemy (>= 3):
  `$AEFF != FF`, `$AFAB == 0`, the mode gate C1/84A6 / arrival latch `$9826`, `$B247`, then C1/8CF9 (the AI), the
  reload + `$B03A = 1`. PC: C1/8511 `LDA $B03A,X / BMI` -> C1/851B (FD/AB30, `$AF23` -> C1/BCE1, the `$93EE` latch,
  the enqueue + chime C1/0003(1) -- or, with `$2989` bit 5, the DEMO C1/B70E -> C1/B725). **C1/8570-8607** copies the
  records' bit 7 into `$B188..$B18A` and pops the confirm FIFO `$99D4` into **C1/B967, which runs the command
  synchronously**. C1/860D-8620 clears `$B186` bit 0 only when no record is pending; **C1/8626 `JSR C1/B223`**.
* Two cadences: `$0027` counts iterations; the idle tick's waits (2 or 3 frames) set the iteration's length.

MEASURED, the whole pattern on the GATO fixture against the cartridge: `$B185` 04, 00, 04, 00 ... every five frames and
Marle's ATB 1E, 1D, 1C ... at the same pace, both sides, shifted by a constant.

## 3. The demo's command (C1/B725 -> C1/B967)

It does not wait out the menu path's latency: C1/8461 -> C1/8588 -> C1/B725 -> C1/8592 pops the FIFO -> C1/B967. On the
0AD (`--hookpc`, labels one ahead): C1/8461, B725, B967 f1858; C1/4058 f1859; `$A013` f1862 (C1:40A9). C1/4058 ->
C1/40A6 is `JSL CC/F06B` (the ladder wipe) + three waits (C1/405F, C1/40A0, C1/10FA) -- 3 frames, structural; B725 ->
C1/4058 is C1/BA33's staging (CPU: 1 frame here, 2 on the 07D) -- `CT_DEMO_STAGING_SPILL = 1`, not derived. While
C1/B967 runs the main loop does not iterate: no timer pass, no cursor, no walk dispatch -- but `$A4` still counts
(its INC rides C1/4058's waits: f1858..f1861).

## 4. What the port does now (battle.c)

`battleTimerPass`, `battleCursorPass12`, `battleAtbRearm`, `s[0x0027]++` per logic tick, the Wait gate over the whole
pass, the handler on `$B186` bit 0 reading `$B185 - 1`, the dead-flag sweep per tick (C1/8130) and at the handler's
tail (C1/8626), the demo's synchronous open (`demoOpenAt` / `demoDispatchAt`), the `$A4` scheduler held until the main
loop, `#08` on the frame before the seed.

RESULT, 0AD: `#08` f1777 = cartridge; the demo turn f1859 (cartridge f1858), `$A013` f1863 (cartridge f1862); Flea
publishes 00 -> 0F -> 04 -> 00, the cartridge's sequence, no 0B. GATO fixture: the ready chime at `#08 + 82` =
cartridge. Forest: the first Hetake action at `#08 + 205` (cartridge + 207; the old model + 225).

## 5. Exposed, not changed (each was hidden by the old model's lateness)

* ~~**The enemy's approach walk.**~~ **MISREAD, and FIXED (battle_ai_interp.md §17).** Cartridge f892 is GATO's
  **op 00 turn**: C1/8CF9 runs the move with no action and steps his pointer; the song is the NEXT turn (f1148). The
  port consumed the op 00 inside the eval and walked past it inside the turn, so it sang one ATB early. The same
  defect was the forest's missing turn (below): the Hetakes' op 00 turns were being "aborted" instead of run.
* ~~**The forest's missing turn**~~ -- FIXED with the above: 7 actions and 3 op 00 turns, the cartridge's slots and
  order. What drifts now is the Hetake's attack span (177 frames on the cartridge, 202 in the port).
  **FIXED 2026-09-18: an enemy action closes at its script's op 01, as every action does.** C1/40CE `LDA $9876 / BEQ
  ->40B0` is the action loop's only exit and the enemy scripts end in the same op 01 (C1/4CF7 -> C1/4D00 `INC $9876`)
  as the techs; the port closed enemy actions on a stored span instead -- `CT_SPAN_ENEMY_AVG` (202, an average) for
  every non-GATO enemy, GATO's three measured spans for him. Measured: the Hetake's first attack runs op for op on the
  cartridge's frames (`--monitor 7E5DBD:64` against `CT_BATTLE_ASCIP`, identical through CD/72F2 at rel 171), and on the
  cartridge every enemy action closes `$9876 = 1` (C1:4D03) + 5 frames (C1:41BD) with a PC victim -- the forest's six
  (f623/628, f847/852, f1317/1322, f1523/1528, f1722/1727, f2206/2211) and GATO's (f630/635, f993/998, f1372/1377,
  song f1457/1462). Port now, forest: 177 202 188 189 192 187 -- the cartridge's six spans exactly (before: 202 each);
  the action starts at `#08` + 205 402 899 1102 1296 1798 2270 against 207 406 890 1095 1291 1780 2254 (before: up to
  +93). What is left is the ATB rate (the CPU clock, above).
  **GATO, and why his stored spans had looked right.** His spans hold LAG FRAMES the port has no clock for: during his
  attack B's flurry the action loop visits no track every 8th frame (f799, 807 .. 887 -- `--hookpc C14CA2 visits=0`), the
  frame on which C1/40B6 -> C1/10FA -> C1/0C2D -> C1/0C76 runs its heavy job (C1/1C3A copies 384 bytes of $D1:5800 into
  $0B40) and the NMI catches it mid-work (C1:1C40..1C44) 2,902 / 13,434 / 15,958 cycles over; the other frames keep
  130k-169k spare. Twelve such frames: the port's attack B is 213 against 222 / 226. Attack A: 153 against 157 -- the
  pre-roll's 3 (the C0/C4 key) and one lag frame (f505). The ending law is exact on all of them; the stored 224 only
  matched by carrying the lags inside a constant. Same wall as the 0AD action (below): a CPU overrun, not derivable.
  **...and the close exposed a missing piece: the EFFECT TEARDOWN the main loop runs (ported 2026-09-18, battleFxTeardown).**
  Each idle-state frame wait is followed by `JSL CD/0009` (C1/10A0, C1/10BD) -> CD/04AA, and CD/04B5-04C4 sends it into
  CD/0453 when `$CE11 = 0`, `$CCEA != 0` (the program service CD/1609 finished: `STZ $5D9B / INC $CCEA`) and `$A013 = 0`.
  CD/0453 is frame-waiting code -- VRAM $5000 back from $7F:5080 (4 packets), `STZ $CD0C / $CA92`, CD/0E9D/0E61, one
  frame, CD/0CF6 (the layer shadows from $2C58..), fifteen frames, `$CCA2 = 0 / STZ $CCEA`, CD/0E23 (colours 24-31
  from $CCEE), one frame: 21 frames (short path, `$99D2` bit 7 or `$CD07`: 5) with NO main-loop iteration. MEASURED
  (gato_wait, the song): `$A013 = 0` f1462, CD/0453 entered f1463 (`--hookpc CD0453`: label f1464), CD/0CF6 f1468,
  `$CCEA = 0` + `$2030` restored f1483, and `$0027` / `$A4` / GATO's ATB / GATO's position all still from f1462 to f1485.
  The port had none of it: `$CCEA` never went back to 0, so the program service never ran for a second program -- with
  the op-01 close, GATO's second song's C1/40E1 `$5D9B` wait held forever (field-battle-gato-attack-sfx's run: no action
  after f1634); the colours and `$BC87` an effect changed stayed changed; and the main loop lost no frames after a
  program, so GATO's mover even started a mode-6 return during what is the teardown on the cartridge (the mode machine
  C1/3714 runs from the idle state too). Now: song end -> GATO's next attack 281 frames against the cartridge's 279 (was
  257: the "-16 after the song" above was THIS, not the CPU clock). Only when the combat goes on: an action that ends the
  battle leaves through the end path (the demo: C1/83AE -> 83B8 with `$2989` bit 5, CD/03B4's own CD/0453 = exitCd0c;
  0AD/07D exits unchanged, f410 / f664). NOT ported: C1/356D (the victory stager) opens with `JSL CD0009`, so a victory
  whose last action ran a program would tear down there; no measured fight ends on one. The Cyclone runs no program
  service (`$CCEA` stays 0 on the cartridge, gato_cyclone) and gets no teardown on either side.
  Also: the boot runs CD/0CF6 from its first instruction (gato_wait f223 `$BC87 = 71` CD:0CFC, `$BE89 = 1300`); the port
  started at CD/0D02 and left the seed's `$BC87 = 0D`, `$BE89/$BE8A = 25 EE` (battleShadowStage).
* **The 0AD action is 12 frames short** (`$A013` 252 frames against 264): 4 of them the pre-roll's spill (X-Strike
  34 vs 30, battleActPreroll), the rest not measured. **MEASURED 2026-09-18 -- all twelve are CPU overruns.** `--hookpc
  C14CA2` over the cartridge's span (saves/attract_demo_battle_f1850.state, `$A013` f12..f276): after the pre-roll the
  action loop visits no track on exactly 8 frames (f123 128 135 159 163 243 255 267) -- the unmeasured 8. A
  `CPUTRACE_BEAM` trace of the span shows the NMI catching WORK instead of a wait loop (C1/007E's `$9E` poll, CD/0036 ->
  CD/04A6 -> CD/3E44, CD/3E7D's `$CA20` poll) on those frames, and the margin (scratchpad margin.py: idle master cycles
  from the last wait entry to the NMI, or work cycles from the RTI to the next wait entry) is THIN: overruns of 876,
  1,736, 2,536, 5,106, 5,994, 6,284, 9,070, 9,094, 38,104 cycles; the pre-roll's decompressions CD:1070 174,808 and
  CD:1181 72,020; and on the frames that make it, as little as 924 / 1,476 / 1,862 cycles to spare out of 357,368.
  Deciding the lag frames needs the battle frame's cost to a few hundred master cycles -- CPU, DMA, HDMA, refresh and
  the wait loop's own ~250-cycle turn -- the same clock wall as the field wipe (field_window_wipe.md). Not derivable
  in the port; not fitted.
  **RESOLVED BY THE MODEL, not by a clock (user decision 2026-09-18, CLAUDE.md's ONE exception).** The zelda3 project
  grades its C against an original that runs one main-loop iteration + one NMI per frame, so a lag frame exists on
  neither side. The oracle now has that mode, `CT_ORACLE_LOGICAL=1` (oracle_logical_frames.md). On it: the X-Strike
  `$A013` f9 -> f260 = **251** (real 264), GATO's attack A **153** (157), attack B **213** (226 / 222), the song **299**
  (311) -- GATO's three are the port's own spans to the frame, the ending law untouched. The X-Strike is the port's 252
  less ONE frame: the pre-roll. On the logical oracle CD/0018's arm counts EIGHT CD/3E75 waits (X-Strike f18 -> f26, the
  song f19 -> f27), and battleActPreroll's `u += 9` carried the one frame the real cartridge overruns there. NOW
  `u += 8` (the arm is the first frame of the twelve C1/007E waits, not a ninth of its own), and the X-Strike is the
  logical oracle's whole: pre-roll 29, the victim track 13 / 13, span 251 (attract-xstrike-vs-logical-oracle).
  The 36 checks it moved are classified in oracle_logical_frames.md §3. That oracle also shows what is left: GATO's
  song end -> next attack is 231 on it against the real 279 and the port's 281, so the port's ATB fill matched the
  LAGGING cartridge. **PORTED the same day (§4 there):** the iteration is C1/106E's two waits, `$A4` is the NMI's
  CF/E3F8 INC gated on `$E5`, the head's C1/8121 INC `$0027` follows the tail, an enemy turn is C1/4058's two waits.
  GATO's whole fight is now the logical oracle's frame for frame (battle-gato-fight-vs-logical-oracle), and the
  ATB fills ~20% faster than on the lagging cartridge.
* The menu path's command still uses CT_BATTLE_DISPATCH_LAT (a fitted latency) where the ROM executes it in the next
  odd iteration's C1/8570 -- the `$93EE`/`$B188`/`$99D4` records are not the port's FIFO yet.
* Arrays 0..11 (the AI's condition timers) and their handlers are not ported; FD/A982's clear keeps them 0.

## 6. The suite

120 checks moved. Classified by rerunning each (build/_pal/classify*.json): 47 only shifted their frames; the oracle
comparisons that pair fixed frames re-anchored to IDENTITY at the new frame (the 07D Frog Squash BG3 4030/4030 and its
cloud 882/882 at f7057, entry 3 57344/57344 at f8900); six battle shots keep their exact picture at a proven offset
(victory settle, crit flash 2, crit leap, crit beat-B, the PC approach, the flee end). The rest re-pinned to the new
model: their fixed frames now fall on moments where GATO/Marle stand elsewhere (the wander's phase) or where the fight
has taken its actions in another order (the Ice shot's `strike#2` is now GATO's song) -- measured on samples, not on
each one. They grade agreement with this model, as before.

## 7. The boot's RNG stream, across fights (2026-09-18)

User finding: the 07D's first cursor pass wrote `$B185 = 06` in the port, 05 on the cartridge (already at c4e1ba5).
The turn order was ROTATED, not mis-shuffled -- port `0A07040903000602050801`, cartridge `0704090300060205080A01`:
every draw after the first identical, the shuffle one table entry behind. The stream is ONE across the attract's
fights: C1/800A seeds `$26` from `$29AD`, C1/8443 hands the final `$26` back, and bank C0 never touches it. The port's
0AD handed back 14 where the cartridge hands back 15 (C1:8448 f2127). Three ROM mechanisms, all in the boot's draws:

1. **C1/80E8 `JSR C1/8C09` runs BEFORE C1/80EB restores the demo bit** (1802D JSR C1/0003 -> the shuffles -> 180D8
   `$B242 = FF` -> 180E5 `STZ $B252` -> 180E8 -> 180EB). So the AI init's draws advance `$26`: AF22's `BIT #20` exit
   is not taken. `--cputrace attract_cena_inicial 1860` from C1:8030: twelve C1:807B draws, eight C1:80B1, C1:8C09 ->
   ONE C1:A48A draw (resolver 05, count 63 -> 19, `$26` 14 -> 15), then C1:80EB; the next draw (C1:E177) takes the
   demo exit C1:AF3F. The port ran its boot eval at the END of battleCombatStart, after the restore. MOVED.
2. **op 04, C1/98C5** (battle_ai_interp.md §18): the 07D's slot 3 draws its roll and the chosen entry's resolver.
   Without it the 07D handed back 52, the cartridge 54 (f5136). PORTED.
3. **C1/FD5E-FDA3, the setup's last arithmetic** (inside C1:8012 `JSR C1/FA8B`): per present enemy `id * 7`
   through C1/C90B (the reward record, CC/5E04/5E05 -> `$AE5D` bits 6/7). An 8-bit id times 7 has no high word, so
   `$2E` = 0000 when the first shuffle draw's C1/C92A reads it as its dividend's high word. From a state 15 frames
   before the 087's seed (`--hookpc C1C90B / C1C92A --hookwatch 7E002E`): the field leaves `$2E` = 00FF, the
   multiplies end two frames after C1:800F with this loop's `id 07` (C1:FD75), and the next arithmetic is the
   shuffle, `$2E` = 0000 at its entry. The port ran no setup arithmetic, so `$2E` held its last damage roll's high
   word and the 087's first draw read raw BA as 6 (0xBA % 11 = 10). PORTED (before the shuffle; the ROM has nothing
   between that touches `$2E`).

RESULT, tools/attract_battle_rng_cmp.py (both sides live; values, not frames): 0AD `01090604020A0508000703` / 01 /
15, 07D `0704090300060205080A01` / 05 / 54, 087 `030106070905020A000408` / 02 / 73 -- all nine = cartridge. `$AE5D`
= cartridge on all three (0AD slot 4 = 80, 07D slot 4 = 40, 087 none). Check: attract-battle-rng-stream-vs-oracle.


## 2026-09-19: D1/F83D, $EC, and the loader's price on a paced host

* **D1/F83D** (JSL'd at D1/F3FA, right after the boot preset): clears $C02F..$C14E and builds the HUD band's per-line
  COLDATA gradient -- 24 one-line records `01 hi(a)` at $C030 and `81 hi(b)` at $C090 (a = E8AB - 55k, b = E055 + 55k),
  12 at $C0F0/$C120 with steps of AA, and the ends. The port never ran it: the band's records were the field's leftovers,
  and their COLDATA carried down the screen -- the attract 07D's Frog Squash frog took R = G = 24 (the field's `$78`)
  where the cartridge's last band line leaves 0. Port f6854 against cartridge f4849: 41/224 -> 128/224 lines identical
  (22 more of the band differ only in how the cartridge's `--hdma` prints the subtract bit; lines 135..224 are the
  known one-line mid-frame rotation), the frame 78% -> 89% identical.
* **CF/FB7D-FB8C** (`$EC = ($2989 & 02) >> 1`, the band layout), see battle_hud_band_anim.md section 8.
* **The boot's first wait on a paced host.** The band arm is 1 + (the song loader's NMIs) + 18. The loader's price was
  charged only under CT_AUDIO_LOCKSTEP, so an interactive run armed at the fallback 69: the attract 07D (the demo's `70
  01` set, 13 NMIs; cartridge arm f4530 = 32 frames in) held Crono's entrance pose 85 frames against 51. It is charged
  now whenever the SPC tracks the engine's frames -- lockstep, or a paced real-time host (not --selftest, not
  CT_NOPACE), whose wall-clock ticks are one a frame. Unpaced headless runs keep the fallback: their SPC advances by
  wall clock, a handful of ticks for hundreds of frames, and its answer would move run to run.
