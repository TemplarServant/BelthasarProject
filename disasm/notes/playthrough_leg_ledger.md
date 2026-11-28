# The playthrough's DIFFERENT legs, one by one (2026-11-08)

Every leg `playthrough-legs-vs-rule-cartridge` pins as DIFFERENT, taken in route order from the CARTRIDGE's state (no
harness pokes, no state carried between legs): `tools/playthrough_triage.py` grades the end state (story / items / stats /
party / gold / loc / tile); a GAMEPLAY-DIFFERENT leg is then aligned (`build/_pt77/align3.py`: the keys moved onto the
game's own events, as a player reacting would -- the port's memory is never touched). A leg is closed only as
**timing** (aligned SYNC-ONLY, end regions the same, the warps named and their class one the rules accept) or **fixed**
(the port's mechanism corrected from the ROM + oracle, the suite green). Nothing moves on while a leg is open.

Build: main 28ccdba (the battle host fresh per fight) + the two fixes below (the run-away counter's CF pass, leg 85; frame B's
C1/3714 after the menu pass, leg 91). With both, the pinned cmp: 91, 92 and 94 SAME (76/137); 71 and 86 turn DIFFERENT on the
end picture only (every gameplay chain equal; 71 aligned SYNC-ONLY, its run-away counter now starts f3177 on both sides).

| leg | triage | aligned | verdict |
|---|---|---|---|
| 07 merchant | GAMEPLAY-SAME | -- | timing (picture only; fix07 load clock, closed 2026-10-13) |
| 20 forest fights | GAMEPLAY-SAME | -- | timing (picture only) |
| 24 sanctum hall | DIFFERENT (gold, a Tonic, stats, $7F:01EE, loc) | SYNC-ONLY, 2 warps [f3008-3 f9334-1], end regions same | **timing**: f3008 = fight 1's exit 3 frames early (the battle-exit SPC fade lattice, battle_exit_fade_poll.md; CLAUDE.md 2026-11-07); fight 1 itself identical (every window and `$26` draw on the same frame) |
| 25 sanctum switches | DIFFERENT (story, items, stats, gold, loc) | SYNC-ONLY, 4 warps [f8397+1 f18818+1 f30731-1 f34550-1], end regions same | **timing**: all four warps sit on a fight's exit (`$95D5` -> 80 on that very frame): the battle-exit class |
| 33 escape | GAMEPLAY-SAME | -- | timing |
| 43 arris info center | DIFFERENT (story, stats, gold, loc) | SYNC-ONLY, 5 warps [f2257+1 f3793+1 f5751-1 f6666-1 f8384-1], end regions same | **timing**: f2257/f5751/f8384 on fight exits (`$95D5` -> 80); f3793/f6666 on a fight's first window (`$95D5` -> 00): the boot waits on the battle set's upload, whose pace in C7 is the same SPC lattice (battle_exit_fade_poll.md sec. 3) |
| 45 bike key | DIFFERENT (port: game over at f25745 -> title) | SYNC-ONLY, 1 warp [f4231+1], end regions same | **timing**: f4231 = the fade-in after 0D7's load (`$0119` 00 -> 01): the NMI-off load length class. With the keys 1 frame off from there, seat 0's queued window (cart f6584) met other presses and the party starved |
| 48 lab32 road | GAMEPLAY-SAME | -- | timing |
| 51 security | DIFFERENT (stats: Crono's HP, 2 bytes) | -- | **timing** (user decision 2026-11-07): fight 1's exit 2 frames early -> play clock -> C0/0C70's `$01F8` seed at 0E5 (17 vs 19) -> robot 0B's path (f4956) -> another fight. Measured chain in battle_exit_fade_poll.md |
| 54 r-series | DIFFERENT (port: game over at f15692) | SYNC-ONLY, 3 warps [f349+1 f27828+1 f31177+2], end regions same | **timing**: all three on a load's fade-in (`$0119` 00 -> 01): the NMI-off load length class. The R-Series fight's win needs its presses on the frame -- one frame from f349 on lost it |
| 56 medina gate | GAMEPLAY-SAME | -- | timing |
| 62 steel saber | (pinned SAME; DIFFERENT on the end picture, 54011 px, since the run-off's pre-roll is the cartridge's 21 -- 2026-11-10; every gameplay chain equal) | SYNC-ONLY, 12 warps [f213+1 f6428+2 f6927+2 f7729+1 f10032+1 f15069+1 f15890+1 f16426-1 f19248-1 f22850-1 f23192+1 f23368-1], end regions same | **timing**: the leg's four run-offs (opened f2254, f6159, f18909, f22547) keep the cartridge's offsets on both sides (`$CA32` +9, `$5D9B` +21, their falls +199 / +200, `$A013` 0 at +206); what moves is around them -- three world loads (1F1 f213, 1F0 f10032, 1F1 f23192), six load fade-ins (f6927, f7729, f15069, f15890, f16426, f23368), a fight exit (f6428) and the two fled fights' hand-backs (f19248, f22850). With the cartridge's pad unwarped the fourth fight begins a frame apart and its L+R hold loses a race to an action (`$9877` = 16 at f22451; the run-off opens 164 frames after the cartridge's), so the leg ends 161 frames behind on another picture. The old SAME had the run-off's extra frame cancelling an exit's missing one |
| 63 zombor | DIFFERENT (port: game over) | SYNC-ONLY, 1 warp [f587+2], end regions same | **timing**: f587 = a load's fade-in (load length class) |
| 64 to denadoro | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11, found from 134): the end picture's 463 px were sprites under BG priority-1 pixels that have the colour math -- the sprite is that pixel's sub pixel and shows through (disasm/notes/field_bg3_wave_and_obj_under_bg.md sec. 3). It had been filed as timing unseen; the pinned cmp is SAME (0 px) |
| 65 denadoro | SAME until 2026-11-13; DIFFERENT (ctrl: a three-frame blip at the leg's end) since the effect teardown's menu pass is made after its frame's NMI | SYNC-ONLY, 6 warps [f36160+1 f39818-1 f54854+2 f60670-2 f80673+1 f81489+1], end regions same | **timing** (the battle-exit SPC fade lattice, battle_exit_fade_poll.md): the pass's cursor blips leave a frame earlier -- on the cartridge's own frames now (f33322 / f33334, f37959 / f37971, f43178 / f43190; a frame late before) -- and the exits' poll of `$2141` then releases on another step (01 01 00 where it read 02 02 01 01 00): control back f68297 / f69712 / f80719 / f80873 / f80968 for the cartridge's f68301 / f69711 / f80718 / f80872 / f80966 (before: f68299 / f69711 / f80718 / f80872 / f80966). After the last one the control drops for three frames (f80969-f80971), which the cartridge's does not -- the leg's DIFFERENT. The in-fight rows and the battle's trace are identical with and without the change up to each module's exit (battle_picture_lags.md sec. 4) |
| 67 masa & mune | SAME | -- | **picture FIXED** (2026-11-11), two port bugs: (1) with 68, the 600 A.D. map's mist (7919 px -> 126); (2) the 126 left were the second follower's walk frame, a frame apart from the lead's -- the leg ends inside the map's armed event (scene 097's cell) and the port started it a pass late: the lead's init C2/34F8 is its block's first visit, in the pass whose bootstrap allocates it, and the scanner starts the event in that pass; the port's host initialised the lead a frame later (disasm/notes/ow_event_cells.md sec. 5). The pinned cmp is SAME (0 px); `playthrough-leg-67-map-event-in-the-spawn-pass-vs-cartridge` and `leg-window-map-entry-event-spawn-pass-vs-cartridge` pin it |
| 68 porre | GAMEPLAY-SAME | -- | **picture FIXED** with 131 (2026-11-11; 7749 px, the 600 A.D. map's mist, filed as timing unseen): the map's dust / mist is BG3 under a per-line H table that HDMA reads from RAM while the picture is drawn: its words are the running pass's (the rest of the camera is the NMI's latch) and a word is the NEXT line's; the port drew the latched table a word late (disasm/notes/ow_bg3_animator.md, its last section). The pinned cmp is SAME (0 px); `playthrough-leg-68-map-mist-line-table-vs-cartridge` pins it |
| 70 bent hilt | DIFFERENT (items: `$2413` 00/51) | SYNC-ONLY, 3 warps [f147+1 f1826+1 f3178+2] (a fade-in, a fight exit, a fade-in), end regions items `$2413` 00/51 | **timing (the leg's cut)**: obj 10 (08D) runs `5A 69 / CA 51` on a 3-frame cadence; the cartridge's visit that adds item 51 is f5618, the frame after the leg's last, and the cartridge holds `$2413` = 51 x1 at 71's end; the port's dump at its warped end (+4) is one visit later. Same op, same slot |
| 72 mystic mountain | GAMEPLAY-SAME | -- | **picture FIXED, the rest timing** (2026-11-11): the End of Time's wave (disasm/notes/field_bg3_wave_and_obj_under_bg.md) takes the end picture from 3396 px to 63. The 63 are a wedge of the platform's glow at rows 200-210, a palette animation a frame off the lag the rest fits at; from the leg's own end state, both sides idle, the picture is the cartridge's (0 px at +40): the drawing is right, the leg ends on another frame of the cycle |
| 73 ayla | DIFFERENT (story, items, stats) | SYNC-ONLY, 4 warps [f640+1 f933+1 f7744+2 f16105-1], end regions same | **timing**: f640/f933 fade-ins (load length), f7744/f16105 fight exits |
| 76 forest maze | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11, found from 134): the end picture's 150 px were sprites under BG priority-1 pixels that have the colour math -- the sprite is that pixel's sub pixel and shows through (disasm/notes/field_bg3_wave_and_obj_under_bg.md sec. 3). It had been filed as timing unseen; the pinned cmp is SAME (0 px) |
| 83 gate home | GAMEPLAY-SAME | -- | **picture FIXED, the rest timing** (2026-11-11): the wave takes the end picture from 3588 px to 258. The 258 are the caps of the five pillars of light (an animated tile, one step of its cycle apart) and the rim of the lead standing in one; from the leg's own end state, both sides idle, 0 px at +40 |
| 84 masamune reforged | GAMEPLAY-SAME | -- | timing |
| 71 melchior | (pinned SAME; with the flee fixes DIFFERENT on the end picture only; SAME again, picture 0 px, since the run-off's pre-roll is 21 -- 2026-11-10) | with the flee fix: SYNC-ONLY, 8 warps, end regions same | **timing**, then **fixed**: the flee fix's check -- fight 2's run-away counter starts f3177 on both sides; the end picture's 53982 px were the run-off action one frame late (battle_flee.md sec. 8, leg 107's row) |
| 86 magic cave | (pinned SAME; with the fixes DIFFERENT on the end picture only, 117 px at -2) | -- | timing (picture only; every gameplay chain equal); the leg ends inside a walk -- at its end state the lead's `$1900` is 0010 and with an idle pad the cartridge takes one more pixel (`$1800` 06A0 -> 06B0), so the 117 px are the lead a step apart, not its drawing) |
| 85 frog joins | DIFFERENT (story, stats, party, loc) | main: DIVERGES (f13329 ctrl -9). **With the flee fix**: SYNC-ONLY, 18 warps, end regions same | **FIXED (port bug)**: the fled fight's escape came 6 frames early -- the port counted the run-away counter (CF/E706) before the bank-CF frame-service pass starts (the band's arm, as CF/E5AD / CF/E73E): `$99CF` ran ahead, the command window closed at f5077 against f5083, the exit 7 early, the clock 7 behind, `$01F8` 22 against 28 at 08C, NPC 12's wander another path. battle.c `cfPass` gate. Remaining warps: fight exits +-2, the NPC meet (f13329, rolls -> decision 2) |
| 87 magus lair | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11; 299 px at every lag from the leg's end state): loc 0A5's dead trees are BG3 priority-1 tiles and its bats OBJ of priority 3; under BGMODE 09 (the field NMI's C0/EA7D) BG3.1 is above every OBJ, so the bats hang behind the silhouettes -- the port drew every sprite over BG3's priority tiles (disasm/notes/field_bg3_wave_and_obj_under_bg.md sec. 7). It had been filed as timing unseen; the pinned cmp is SAME (0 px), `field-picture-bg3-above-every-sprite-vs-cartridge` pins it |
| 91 flea | DIFFERENT (gold, hp, picture) | flee build: SYNC-ONLY, 11 warps [f285+1, then ten inside Flea's fight, -1..+3, each on an action's end], end regions same | **FIXED (port bug)**: the idle loop's frame B runs C1/3714 AFTER its menu pass (C1/10C4 INC $E5, C1/10C6-10DA, C1/10DC JSR C1/3714); the port ran it before. At the command confirm (cart f30969: `$99E0` 40 -> 00, `$B18B` 03 -> 02) the cartridge's mode machine stepped Flea's cadence `$9897` (02 -> 01), the port's still saw the Wait submenu; after the action her walk segment (`$98A7`) armed a frame late, ended a step short ((165,150) against (167,152)), and every later Flea action slid from 2 px off: SLIDE n=28 against 27, the action 2 frames longer, the ATB behind it. battle.c `mvDefer` (frame B's C1/3714 after the menu pass). Verified: `$9897`, the walk and her f35175 foot (136,121) equal |
| 92 slash | DIFFERENT (gold, hp, picture) | flee build: SYNC-ONLY, 8 warps [f1295+1 f4680-1 f30849+1 f32185-1 f102884+1 f108153-1 f108519-1 f109560+1], end regions same | **timing**: f1295/f30849/f32185/f108153/f108519 load fade-ins (`$0119` 00 -> 01); f4680/f102884 fight exits (`$95D5` -> 80); f109560 Slash's fight's first window (`$95D5` FF from f109472 -> 00: the boot's upload pace) |
| 88 porre shopping | GAMEPLAY-SAME | -- | fixed with 87 (the same room, 299 px): the pinned cmp SAME (0 px) |
| 93 ozzie way | DIFFERENT (picture; pinned) | with the three fixes: SYNC-ONLY, 6 warps [f51737+1 f60844+1 f91416-1 f93707-1 f97134-2 f100223-1] -- every one a fight's exit (`$95D5` -> 80), end regions same | **FIXED x3, then timing**: (1) 0A7 f25869, an op 0xDA reset the follower ring after follower obj 2's own B0 had advanced on the old cursors (ct_vm.c g_daObj, 52402e0); (2) the equip menu's list slide left `$22-$27` in the direct page and `$27` is the battle's iteration counter, so Crono's first box came a menu pass early (field.c menuEqSlideDp, a696d14); (3) a removed battle enemy keeps its step in flight -- C0/07F3 clears no mover -- obj 19 on 1BE stepped at f87861 (CtSceneBattleRemoveObj, 2c60584). What is left is the battle-exit fade lattice |
| 94 ozzie barrier | GAMEPLAY-SAME | -- | timing |
| 95 magus | DIFFERENT (port: end dump missed) | with the fix: SYNC-ONLY, 3 warps [f1270+1 f83984-2 f85154-1], end regions same | **FIXED (port bug)**: 1BF's opening walk -- the lead's B0 pushes the ring before a ten-frame song wait and the step comes after it; the port gated the push on the lead having moved and cleared `$1D32` during the wait, so the ring lost an entry and both followers ran a frame behind (the opening's -4/-2/+2 warps). The push's gate decoded (C0/9E29) and `$1D32` kept while parked (party_follower_ring.md, 2026-11-08). Left, timing: f83984-2 is the field menu's opening (its stash, an NMI-off stretch: `$95D5`/`$A013` are the menu's scratch there, written with `$0D9A`/`$0D13` the frame it opens, 18 frames after the key on the cartridge, 16 on the port), f1270 a fight's start and f85154 a fade-in -- the load-length class **End picture, read 2026-11-11: timing (a roll the clock moved).** The 11 px (at port -2) are the lead's eyes: on the cartridge his idle blink runs at the cut (`$1682` = 21, the roll C0/A232 at f91773), in the port it does not (`$1682` = 00, its roll at f91770). Both rolled on the idle counter's boundary; the cursor `$01F8` they read is 3 apart -- 14 / 11 from loc 10F's load (f85146 / f85144) and 11 / 0E from loc 113's (f88608 / f88606), the play clock C0/0C70 seeds it from, moved by this leg's warps -- so `$FE00[$13]` armed the blink and `$FE00[$10]` did not. From the cartridge's own end state both sides draw the same blink (the one-state sweep: 0 px). |
| 96 laruba | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11, found from 134): the end picture's 813 px were sprites under BG priority-1 pixels that have the colour math -- the sprite is that pixel's sub pixel and shows through (disasm/notes/field_bg3_wave_and_obj_under_bg.md sec. 3). It had been filed as timing unseen; the pinned cmp is SAME (0 px) |
| 101 ioka rest | GAMEPLAY-SAME | -- | **picture FIXED** with 125 (2026-11-11; 5 px at -2): the party's order on one pixel, the party tail's exit. It had been filed as timing unseen; the pinned cmp is SAME (0 px) |
| 106 end of time | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11, found from 134; 3325 px): the End of Time's sky is BG3 under the window wave (`$1DF9`), which the port did not draw, and the lead in a pillar of light is the pillar's sub pixel (disasm/notes/field_bg3_wave_and_obj_under_bg.md). It had been filed as timing unseen; the pinned cmp is SAME (0 px), and `field-picture-end-of-time-pillar-vs-cartridge` pins the picture from this leg's end state |
| 113 algetty shop | (pinned SAME; DIFFERENT on the end picture since bc6478c) | ring fix build: the five fights' acks early by 12/10/7/4/16 (f8021/14584/21553/23546/38007 against f8009/14574/21546/23542/37991); every victory (`$A0FE`) within 3 and fight 1's three result lines on the cartridge's frames (f7564/7758/7952) | **timing (user decision 2026-11-07, the battle exit follows the SPC fade)**: the whole gap is the exit's fade wait -- the cartridge posts the fade at f7975 and CD/0432 polls the driver's fade count for 36 frames before C0/1BE6's `11 45 B0` (f8011); the port's exit leaves 16 frames after its post (battle f7450 -> 7466). The fade's tick lattice is the decided class; the size here (up to 16, always early) is noted for a look at the driver's fade under this song. Before bc6478c the same acks were early too: the old pin's SAME was the end picture coinciding (its ring was an entry behind for 1706 frames) |
| 115 dalton | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11, found from 134): the end picture's 444 px were sprites under BG priority-1 pixels that have the colour math -- the sprite is that pixel's sub pixel and shows through (disasm/notes/field_bg3_wave_and_obj_under_bg.md sec. 3). It had been filed as timing unseen; the pinned cmp is SAME (0 px) |

| 97 dactyl nest | DIFFERENT (port: end dump missed) | with the fix: **SYNC-ONLY after 0 warps**, end regions same; the pinned cmp SAME | **FIXED (port bug)**: the battle pose machine C1/424C publishes a combatant's window from the tables the staging laid (C1/4283-42C9: `$96DB/$A214`, `$966E`, `$A2C8`, `$A1FE,Y`) and never reads the alive list `$AF02`; the port took the enemy id from `$AF02` and stopped the window on FF. Slot 6 (id 61) attacks at f2382 off the list (`$AF02+6` = FF since f2359): the cartridge publishes its anim 03 at f2386, which takes the publish slot the victim's `06 03` then waits a frame for; the port's victim went a frame early, and every later window with it. battle.c battleAscPoseTick reads the staged id (`$AF0D`) |

| 99 kino | DIFFERENT (picture) | SYNC-ONLY, 4 warps [f3958+1 f6460+1 f10880-1 f14979+1], end regions same | **timing**: three fight exits (`$95D5` -> 80) and a fade-in |

| 102 nizbel | DIFFERENT (hp, picture) | with the fixes: **SAME**; SYNC-ONLY, 3 warps [f357+1 f15210-1 f19212+1] (a load, two fight exits), end regions same | **FIXED (two port bugs)**: (1) op D9's ring reset (C0/327D) is op DA's and a follower below its object advances first (dc3da14; party_follower_ring.md 2026-11-09) -- two enemy actions had run 2 and 1 frames long on PCs staged 1 px low; (2) a decoded counter runs C1/4058 on its decode frame (1aec7ef; battle_counter_gating.md 8.7): CC/F0E6's `$9931` restart sets the counter's menu-pass phase, which the port put two frames early, so a held Left switched the chooser at f6410 for f6412 |

| 103 lever rest | DIFFERENT | SYNC-ONLY, 6 warps [f1719+1 f7513+1 f11031-1 f12537+1 f13659-1 f14784+1], end regions same | **timing**: four load fade-ins (`$0119` 00 -> 01), a fight exit (`$95D5` -> 80), a load (1F3) |

| 104 black tyrano | DIFFERENT (port: end dump missed) | SYNC-ONLY, 4 warps [f12816-1 f17200-2 f19023+1 f42448+1], end regions same | **timing**: two fight exits (`$95D5` -> 80), two load fade-ins (`$0119` 00 -> 01) |

| 105 zeal | DIFFERENT (loc) | with the fixes: **SAME**; SYNC-ONLY, 13 warps [f1728+1 f6098+1 f6643+1 f8421+1 f8778+1 f11047+1 f11980+1 f13651+1 f16802+1 f22043+2 f27597-1 f49416+1 f51290+1] (load fade-ins and world loads), end regions same | **FIXED (two port bugs)**: (1) loc 185's map has an RLE plane that reads on past its packet through `$2180` (C0/A521): the walk refused it and a median priced 26 black frames for 34 (4492a63, field_load_cost.md sec. 18); (2) the world entrance is taken on the lead's input frame with A HELD (C2/3577 -> 35E5), the port took A's first frame only, which fell inside the step onto the skyway (eb4c209, menu2_overworld_entry.md 2.3.1) |

| 100 lair grind | DIFFERENT (picture, stats) | with the fixes: SYNC-ONLY, 35 warps (16 load fade-ins, 16 fight exits `$95D5` -> 80, 3 fights' first windows `$95D5` 80 -> 00), end regions same | **FIXED (four port bugs)**: (1) C1/881C ends with C1/AC46: the target lists back to FF after every pass (dba6ad9) -- a tech's victims stayed behind a physical attack's target and armed a false counter, whose walk shortened an action by 4 frames; (2) op D9's ring reset is op DA's and a follower below its object advances first (dc3da14) -- Marle and Lucca staged 1 px low, an effect a frame long; (3) a decoded counter's C1/4058 on its decode frame (1aec7ef); (4) Ayla's weapon is 0x44 + level / 24 at every boot (C1/FABE, 00db969) -- the end stats' `$27B9` 45/44 and `$27D5` 19/14 |

| 107 keepers dome | DIFFERENT | with the fix: SYNC-ONLY, 16 warps [f3900+1 f4191+1 f5326+1 f7337-1 f7847+1 f10321+2 f12008+2 f12696+2 f13304+1 f13547-1 f14903-1 f18372-1 f24734-1 f39832+2 f60672+1 f64637-1], end regions same | **FIXED (port bug), then timing**: the run-off action (`$9877` = 79, opened f11727) ran one frame long -- the port's pre-roll was the observed constant 22, and the effect load of an id with no sheets (86; 7B..7F by the listing) has no wait: CD/0EF8 `JSR CD/0E9D / BCC ->0F42` copies four ids and returns, CD/0D33 arms, so the arm is open+9 and the script's first op open+21 (`$CA32` f11736, `$5D9B` f11748, `$A013` 0 at f11933; the forest fixture gives the same +9 / +21 / +206). battle.c CT_FLEE_ACT_PREROLL 21 and battleActPreroll's no-sheet class (battle_flee.md sec. 8); the three forest flee checks re-pinned, leg 71's end picture 0 px with it. Warps: six load fade-ins (f3900, f4191, f12696, f13547, f24734, f64637), three world loads (1F2: f7847, f13304, f60672), six fight exits (`$95D5` -> 80: f7337, f10321, f12008 -- the fled fight's --, f14903, f18372, f39832), a fight's first window (f5326) |

| 109 mt woe climb | DIFFERENT (ctrl) | `--tol 40`: SYNC-ONLY, 3 warps [f22618-1 f87164-16 f107381-17], end tile (24,5)/(25,5) | **timing**: f87164 / f107381 are fight exits whose victory (`$A0FE`) lands on the cartridge's frame (f86071) while the cartridge's exit waits on the SPC fade 36 frames (post f87115, C0/1BE6 `11 29` f87151) and the port's leaves 16-17 earlier -- the 2026-11-07 class, as leg 113's; f22618 a fade-in. The end tile: every position change of objects 0-7 and `$0197` from f107381 to the leg's last frame lands on the same frame on both sides (chgcmp, no difference): the port's end dump is sampled a frame apart while the lead crosses a tile edge |
| 114 giga gaia | DIFFERENT | SYNC-ONLY, 23 warps, end regions same: six fight exits f6917/13870/23211/26229/31451/34700 -13..-18 (the class of 109 / 113, the SPC fade lattice; by the pattern, their victories not measured one by one), load fade-ins and world loads (+-1), and f39658-1: the fight's whole action list runs one frame early from its first `$A013` on (f38634 / f38633), no drift inside it, so the boot's pace (first window class) | **fixed + timing**: effect program ops 80 0C / 0D and C2, `$A012`'s two steps (3d5f5d2), op 12 on track 4 (2e2234a) |
| 116 ocean palace halls | DIFFERENT | with the fixes, and the cartridge's clock and cursor put back at the first load (`$0400` = `$01F8` = 1D at f2095: the 2026-11-07 class): SYNC-ONLY, 15 warps [f2144-4 f2737-1 f17687-1 f19508-1 f27407+1 f52421-1 f65411-1 f74919+1 f76414-1 f76782-1 f87446+1 f99777-1 f108858+1 f124778+1 f132672+1], end regions same | **FIXED (two port bugs), then timing**: (1) the battle's main loop counts its iterations in `$0027` (C1/8121) and never initialises it; the parity it starts on picks which iterations run the cursor pass and the handlers. On the cartridge the byte is what the last MENU left on the direct page (the field never writes `$0000-$00FF`); the port kept its own direct page across a menu, so the fight at f87971 began 00 -> 01 for the cartridge's 03 -> 04 and every action in it landed a frame or two apart. field.c: C2/9503's fill; menu.c: the amounts' pass C2/8ECC and its operand `$0DBF` (C2/B17F an item, C2/B469 a tech); (2) no idle visit while the handler is in its counter loop C1/8671-881C (f88218-88228: the port's enemies' poses flickered). Warps: six load fade-ins (f2144, f2737, f74919, f76414, f76782, f87446), five fight exits (f17687, f27407, f65411, f99777, f108858: `$95D5` -> 80), four fights' first windows (f19508, f52421, f124778, f132672: `$95D5` -> 02) |
| 119 elevator + 120 scouts (graded joined: 120 starts inside a fight the port cannot load) | DIFFERENT | with the fixes: SYNC-ONLY, 5 warps [f2025+1 f16004-1 f39463+1 f39703-1 f68108+1], end regions same | **FIXED (six port bugs), then timing**: (1) the armed fade reads `$0119` (C0/1F24; a stale copy started the fade at 03, a load 40 frames early); (2) the bounds `$1D1A-1D` before the init (a pan 15 frames short, loc 1A0); (3) a menu's return stores the stash's WORDS (C0/1A6F-1A97: after the Form exchange Lucca's object stood at `$1886` = 0DF0 for 0DFF and the hand-off at f15560 came a frame early); (4)-(6) the PC pose machine in loc 1A0's fight (ids 59 + 6D x3): C1/49FF turns a PC to its target as a physical opens (Lucca shot facing the wrong way), the NMI's stepper runs before the walk dispatch (a re-aim reads the feet of its own frame), and the action close's scan C1/2F97 is whole (a new icon restarts the window). The port's machine ran in another phase, so after the three Thrashers died resolver 17 read another seat's stale `$9873`, enemy 59's counter held a condition the cartridge fails and drew once more on `$26`; `$29AD` came back 12 for 11, the scouts' crit stream `$B3E6` started one entry on and Lucca's last shot was a crit the cartridge has not (the old warps f33341+2 and f34737+1; battle_pc_physical_script.md sec. 6). Pinned: leg-window-menu-return-vs-cartridge, leg-window-fight-poses-vs-cartridge. Warps: f2025 a fight's first window, f16004 / f39703 / f68108 load fade-ins (`$0119` 00 -> 01), f39463 a fight exit (`$95D5` -> 80) |
| 122 golem twins, lavos | DIFFERENT (story `$7F:00FD`) | SYNC-ONLY, 7 warps [f3221+1 f60598+1 f70289+1 f70774+2 f77703-1 f82387-1 f107745+2], end regions same (the same list on the final build, 2026-11-11) | **fixed, then timing**: op 9A is one step of its fourth byte (C0/4FEA; `$7F:00FD` holds obj 19's row). The two control hand-offs f70289+1 / f70774+2 are loc 1A7's cutscene after a song upload: both sides reach obj 0's `EA 09` on f66966 (script 0631) and go on at f67005 on the cartridge, f67004 on the port -- the upload's pace in C7, the 2026-11-07 class. From there the scene runs a frame early against the pad, and each dialogue the pilot's A closes resumes on its caller's 4-frame cadence, a frame early or three late: obj 20's `BB A6` box 108 frames for the port's 112 and the `04 26 44` call after it 136 for 132, `BB A8` 240 / 244; the next upload `EA 0D` (f69965) takes 22 frames for 24; the hand-offs land +1 and +2. The same ops in the same order, the cadence counters `$1000-$103F` equal after 1A7's load (f65960 on). Other warps: a fight's first window (f3221) and load fade-ins |
| 118 ocean palace return | DIFFERENT (ctrl) | with the fixes, and the `$01F8` cursor put back at f35198 / f39424 (room 199's shots are rolls the play clock moved: the 2026-11-07 class): SYNC-ONLY, 7 warps [f7553+1 f8346+1 f13682+2 f27600-1 f39471-1 f46146+1 f46773-1], end regions same | **FIXED (three port bugs), then timing**: (1) an object's tile past the coordinate's wrap: the move ops read the mover's own tile from the position's high BYTE (`$1801,X` / `$1881,X` = FF past the left edge), so room 199's two hazards, paused by a fight at `$1816` = FF2B, fly LEFT across the whole 256-tile coordinate (about 1190 frames) before they arrive; the port took the tile from its unwrapped pixel position, aimed right and arrived at once, and the leg's next fight paid its gold at f45633 for the cartridge's f45479 (ct_vm.c vmTileByte: C0/4F7C, 50B9, 5006, 5144, 51C8, 5237, op 21, the draw window A947, the AA07 clamp); (2) the arrival snap C0/305D tests the position's low byte only; (3) ops 02 / 03 / 04 store the callee's pointer in `$1180,X` themselves (C0/6027, 60F0, 618C). Pinned: leg-window-hazard-wrap-vs-cartridge (f35190 + 5510, 1335 steps, 0 differing). Warps: f7553 a world load (1F5, the walker's first move), f8346 / f13682 / f46773 load fade-ins (`$0119` 00 -> 01), f27600 a fight's first window (the boot's pace), f39471 / f46146 fight exits (`$95D5` -> 80 at f39419 / f46101, control back 52 / 45 frames later) **Re-measured 2026-11-11** (the lead's visit in its slot of the object pass, disasm/notes/ow_event_cells.md sec. 5): SYNC-ONLY after 4 warps [f8346+1 f39471-1 f45479-1 f46773-1], end regions same, without putting the cursor back. The world load's f7553+1 ('the walker's first move') was not the load clock: the port's lead read the pad a frame after the map opened, a port bug, fixed. The end picture stays whole-screen (54016 px for 57344): the cut is inside loc 19B's load ramp. |

| 123 last village, blackbird | GAMEPLAY-SAME | -- | **FIXED (port bug), the pinned cmp SAME (0 px)**: the lead is drawn with ITS character's sheet. The host draws the lead apart from the VM's actors and used sprite 0 -- Crono's sheet and anim set -- with the lead object's frame number and palette row; from Crono's fall to his return the lead is another character's object (`$0197` = 04, `$1101+4` = 01: Marle) and the port showed Crono's frame of the same number in her colours (a spiky blond figure facing the camera; legs 132 / 136: 390 / 395 px, the whole sprite; the same on a real battery save, jackie-saves-census's ch23). field.c takes the lead object's own sprite (field_sprite_prio_and_player_gate.md sec. 7) |
| 124 blackbird decks | GAMEPLAY-SAME | -- | fixed with 123. The pinned cmp reads DIFFERENT since 2026-11-12 (end picture 14 px at +0): a light's colour cycle 19 frames out of step from the leg's first load -- the SEED, see "Seeds cut inside a blocking loop" below |
| 125 blackbird wing | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11; 312 px at +0): the party has just come into loc 1DE, its three members on one pixel, and the cartridge draws the third (Ayla) in front where the port drew the lead (Marle). C0/A810's party arm closes the loop itself and ends on `A88C RTS`: with the room's last object a party member the party tail (A846-A867) never runs, so the draw list keeps the loop's order -- bucket $66's chain 5, 2, 1 on the cartridge, 1, 2, 5 in the port (disasm/notes/field_draw_list_order.md sec. 6). The pinned cmp is SAME (0 px); `field-picture-party-stack-order-vs-cartridge` pins it |
| 126 wing, dalton | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11; 275 px at -2, all of it Ayla): not the "four-frame cycle two frames off the lag" of the first reading -- over the 67 frames around the leg's end the port never drew the cartridge's pose, at any camera shift. A frame's chip word carries an 11-bit tile index (the anim drain's `AND #07FF`, C0/E687 / C0/E534); the asset extractor read ten bits and capped a character's pack at 1024 tiles. Ayla's pack has 1120 and her frames 1D / 1E (29 frames in all) use tiles $400-$45F, drawn from the tiles 1024 below (disasm/notes/sprite_chip_tile_index.md). The pinned cmp is SAME (0 px); `playthrough-leg-126-ayla-frames-vs-cartridge` and `sprite-pack-tile-index-is-11-bits` pin it |
| 127 winged epoch, 179 | GAMEPLAY-DIFFERENT (the port's end dump reads zeros) | SYNC-ONLY, 6 warps [f38+4 f1028+2 f17566+2 f20680-1 f21021+1 f32041+1], end regions same | **timing**: f38 is the leg's cut -- the route cuts it inside a walk (Down held from its first frame), the port's keys start a frame later, the lead walks a step behind from f1 (`$1802` 09C0 for 09D0) and control is taken four frames late; f1028 the fight's first window (`$95D5` -> 02); f17566 / f20680 / f21021 load fade-ins; f32041 a fight exit (`$95D5` -> 80) |
| 129 north cape | GAMEPLAY-SAME | -- | fixed with 123: the pinned cmp SAME (0 px at -3) |
| 130 black omen, gaspar | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11, found from 134; 1845 px): what was read here as "the End of Time's backdrop streaks, an animation's phase" was the window wave on BG3, missing in the port (disasm/notes/field_bg3_wave_and_obj_under_bg.md). The pinned cmp is SAME (0 px) |
| 131 c. trigger | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11; 3772 px, not "an animation's phase"): (1) the BG3 visit's camera deltas are the WRAM words `$1BF1` / `$1BF3` (a pixel off each way after the Epoch sets down; `leg-window-world-entry-bg3-vs-cartridge`); (2) the map's dust / mist is BG3 under a per-line H table that HDMA reads from RAM while the picture is drawn: its words are the running pass's (the rest of the camera is the NMI's latch) and a word is the NEXT line's; the port drew the latched table a word late (disasm/notes/ow_bg3_animator.md, its last section). The pinned cmp is SAME (0 px); `playthrough-leg-131-map-dust-line-table-vs-cartridge` pins it |
| 132 keeper's dome | GAMEPLAY-SAME | -- | fixed with 123: the pinned cmp SAME (0 px) |
| 133 clone | GAMEPLAY-SAME | -- | fixed with 131 (3744 px, the 2300 A.D. map's dust): the pinned cmp SAME (0 px) |
| 134 death peak, wind | GAMEPLAY-SAME | -- | **picture FIXED** (2026-11-11; 19440 px at -2): three mechanisms the port lacked (disasm/notes/field_bg3_wave_and_obj_under_bg.md). (1) BG3's scroll is HDMA channel 3's, a line at a time: the snow waves +-5 px over 16 lines, picture row r on transfer r - 1; (2) the wave's phase `$1DFB` moves four a pixel with BG3's vertical committers (C0/9721 / C0/975F) -- that, not a load's length, was the "scroll counters a frame apart" of the first reading; (3) loc 0F6 is TM 17, TS 13, CGWSEL 02, CGADSUB 44 under BGMODE 09: the snow's priority-1 pixels are above every OBJ and half-add the sprite under them, their sub pixel. From this leg's end state, idle, the port's picture is the cartridge's to the pixel at +12, +40 and +133 frames; the pinned cmp is SAME (0 px), `field-picture-death-peak-snow-vs-cartridge` pins it |
| 135 death peak, 0F6 | GAMEPLAY-SAME | -- | fixed with 134 (20626 px at -2): the pinned cmp SAME (0 px) |
| 136 death peak, terrace | GAMEPLAY-SAME | -- | fixed with 123: the pinned cmp SAME (0 px) |
| 137 lavos spawns, summit | GAMEPLAY-DIFFERENT (story `$7F:0000` D5 / D4: with the unwarped pad the port does not win the summit's fight) | SYNC-ONLY, 11 warps [f19158-1 f23752+1 f25722-1 f26719-1 f35413+1 f35504-1 f42386+2 f46014-1 f47727-1 f56464+1 f70396+1], end regions same | **timing**: five fight exits (`$95D5` -> 80: f19158, f23752, f25722, f35413, f42386) and the hand-back 91 frames after the fourth, which takes its frame back (f35504, control 00 -> 01); two fights' first windows (`$95D5` -> 02: f26719, f47727); three load fade-ins (`$0119` 00 -> 01: f46014, f56464, f70396). Every class is one the rules accept |

**Open, in route order** (2026-11-11; 24-137 closed above as timing or fixed, but for what is named here):
none on the map (67 closed 2026-11-11: the lead's init in the spawn pass). The warp lists of
107, 116 and 118 were measured before the pose-machine fixes of 2026-11-11; their pinned cmp lines are the final
build's. 95's end picture is read (its row: the idle blink, a roll the clock moved). 104 and 122, read 2026-11-11: with
the route's unwarped pad the port's party is wiped in the leg's boss fight and the game goes back to the title -- 104: two
members at 0 HP from f77957, `$2980-82` = 80 80 80 at f78352 (the Black Tyrano; the cartridge wins and changes the party at
f89037); 122: all three at 0 HP at f17558, `$0100` = 0000 at f18084 (the Golem Twins). The end picture is then the New
Game's name screen against the cartridge's room: whole-screen at every lag, and the `port: end dump missed` of their rows.
Re-anchored on the cartridge's events (tools/playthrough_align.py) both are SYNC-ONLY with the end regions same: the keys
after a fight's first window land a frame or two off and the commands with them. Their end states draw the cartridge's
picture (the one-state sweep: 0 px).

**109 / 113 / 114, re-read 2026-11-12: their 4-18 frame early exits were a PORT BUG, not the lattice.** The exit
routine's head CD/03B4 (`LDA $2A21 / BPL / LDA $2A1F / AND #BF / STA $2A1F`) gives up "the field's music plays through"
when the fight carries `$2A21` bit 7, so those fights fade, wait fifteen frames and poll like any other; the port's
exit began at the next test and left without the fade (disasm/notes/battle_exit_fade_poll.md sec. 6; battle.c;
`leg-window-fight-exit-kept-music-vs-cartridge`). With it 113's exits are -1 / +2 / +2 / -2 and 114's -1 / -1 / 0 / 0 /
0 / +2: the lattice. Both legs stay DIFFERENT -- the unwarped pad meets their last long fight on other frames (113's
end picture goes 549 px -> 54016 px: the old one had the early exits cancelling) -- and 59 (Heckran) stays SAME, its end
picture a frame nearer (port -2 for -3). The rows above keep their first reading.

**A lesson of 134 for every "timing (picture only)" above:** six legs filed as timing on a GAMEPLAY-SAME grade (64, 76,
96, 106, 115, 130) were picture bugs of one mechanism, found only when it was fixed for another leg. A leg's end
picture is graded after its whole pad, so it cannot tell a draw bug from a frame of lag; `tools/field_picture_cmp.py`
can (both sides from ONE cartridge state, an idle pad: any pixel apart is the drawing). Every end state of the route is
to be put through it before a picture difference is called timing again.

**The one-state sweep (2026-11-11).** Every end state of the route, both sides seeded from it, 40 idle frames
(scratch `picsweep.py`, kept in build/_pt77/scratch_tools; results build/_pt77/picsweepAK and picsweepAL): 116 of 138
at 0 px with the fixes above. It found 87 / 88 (the same count at every lag: a draw bug). What is left, by what the
pictures show -- none of it a leg closed above, all of it still to be taken one by one:

- **A seed inside the load's brightness ramp** (118 `$0119` = 04, 120 = 04, 121 = 08, with `$0118` clear): C0/2848 is a
  loop inside the load (`INC $19 / JSR C0/881E / JSR C0/00BF / JSR C0/EC60 / CMP #0F / BMI`), CPU state a WRAM seed does
  not carry, so the seeded port stays dark. A limit of the harness, not of the game -- and the reason 118-120's pinned
  end pictures are whole-screen.
- **A seed on the overworld** (03, 67, 68, 131, 133): the sea's / the dust's phase is not the cartridge's after a seed;
  a leg that walks onto the map itself ends on the cartridge's picture (03's own cmp: 0 px).
- **Tiles a script drew** (124 loc 175, 53 loc 0EB): the seeded port shows the room without them; the legs' own cmp
  lines are SAME.
- **An object's sprite after a seed** (55, Spekkio's room): the seeded port keeps the sprite its own load picked before
  the seed (the script chooses the form by the party's level); the cartridge's is the id the loader stored at `$1101,X`
  (C0/448A op 0x81, C0/4564 op 0x82). The leg's own cmp is SAME. A limit of the seed.
- **A seed in mid-motion** (86, 126, 135; 02's cat, whose walk ticket `$1A00` is half spent): the step in flight is not
  resumed the same way. 86's own 117 px are the lead one step apart at the leg's cut.
- **Mt. Woe** (109 loc 188, 111 loc 189): the fog's layer sits elsewhere after a seed (its rebuilt-layer origin is not in
  the WRAM); 111's own cmp is SAME.
- **Read since** (2026-11-11): 100 (loc 130), 114 (loc 191) and 119 (loc 1A0) end with the cartridge's brightness at 0
  (`ct.exe --ppuregs` on the end state): the leg's cut is the first frame of the load's ramp, the class of 118-121
  above. 91's light discs show in the seeded port only (the leg's own cmp is SAME). 131 and 133 were the 2300 A.D.
  map's dust, a draw bug, fixed (their rows).

**The mid-leg sweep (2026-11-11).** The same question asked INSIDE the legs: the cartridge's state every 1500 frames
of each leg (cut in one pass), both sides from it for 24 frames, the pictures compared (scratch `s12_midsweep.py`,
`s12_midone.py` for one state; build/_pt77/scratch_tools, results build/_pt77/midsweep3). Two things about the harness
first:

- Both sides must go on with the LEG'S OWN pad, and the port's first key must be the leg's pad frame before the cut
  (the port's keys are the cartridge's one frame later). With an idle pad the port lets a held direction go a frame
  early and every state cut inside a step shows a static 280-350 px: the "seed in mid-motion" class of the one-state
  sweep above was this artefact (30 f3000 and 21 f1500: 280 / 322 px idle, 0 px with their own pad).
- Only FREE-ROAM states say anything: the player in control (`$011F` != 0) and no fight (`$95D5` = 80). A state inside
  a cutscene, a dialogue's opening or a fight's boot holds CPU state the seeded port does not resume -- it runs ahead
  (leg 65 f76500: the port is in the fight 36 frames before the cartridge) or types the page again.

344 states compared (every state up to leg 99, before the free-roam filter went in; free-roam states only from leg
100 on), 204 at 0 px. Up to leg 99 what differed was a dialogue being typed, a fight or its boot, a shop, tiles a
script drew (33 f4500, loc 047) or a palette effect in flight (31 f1500's stained glass) -- the seed's limits above.
From leg 100 on, by what the pictures show:

- **A port bug, fixed:** leg 122 f61500, loc 19F -- the fixed colour's math over the map with no colour window (op
  0xF1's fade; 49,965 px; disasm/notes/field_quad_window.md, its last section;
  `field-picture-fixed-colour-over-the-map-vs-cartridge`).
- A cut on the first frames of a load's brightness ramp (the port stays black): 100 f43500, 116 f132000, 117 f4500,
  122 f1500, 124 f27000 (`$0119` = 00 at the cut), 127 f21000.
- A text box open in free roam: 101 f6000.
- A map a script drew: 125 f4500 / f6000 (loc 170, the Blackbird's corridors).
- The lead on a ladder, mirrored (125 f70500 / f72000, loc 1BB, 200 px): the host's own copy of the lead's facing and
  tile mode is not the seed's. The port's OWN run of leg 125 cut at f70524 draws her as the cartridge does.
- Death Peak's snow (134 f15000, 137 f24000): the layer's position is not the seed's; the legs' own end pictures on
  those rooms are 0 px.
- **A port bug, fixed (2026-11-12) -- not a seed limit:** leg 86 f16500, loc 0A3, the mountain's cutscene (`$011F` = 0;
  found before the free-roam filter). BG2 stands at hscroll -16 there (the script's offset, 80 px behind BG1), so the
  screen's left 16 px show columns 62-63 of BG2's 64-column tilemap. The cartridge shows what the tilemap HOLDS: the
  map's chip columns 9 and 8 (row 24: `5945 5944`), which the camera's left-edge builder C0/83E5 wrote there on leg
  frames 16431-16446 -- BG2's and BG3's edge builders read the CAMERA's window -- where the port drew the map's wrap
  (`5947 5946`). 269 px. The port now keeps the field's tilemaps as the ROM writes them and draws from them where they
  are not the map's (disasm/notes/field_bg_tilemap.md sec. 7; `field-ring-camera-columns-vs-cartridge`, 0 px).
  (This bullet first said the columns were leftovers of an earlier room, from a `--teleport` test that only showed
  the room's LOAD does not write them. The frame-by-frame VRAM dumps showed the streamer does.)

## Seeds cut inside a blocking loop (2026-11-12)

A per-leg run seeds the port from the cartridge's WRAM at the leg's cut. Where the cut lands inside a loop the
cartridge's main thread does not leave -- a fact of the program counter, in no WRAM byte -- the port's main loop runs
from the first frame and the leg is off before it begins. Found with the whole-run picture audit:

- **The load's fade-in, C0/2848** (`$0119` rising, the state's first frame writes it at C0:284F): legs 106, 119, 121,
  122. HANDLED: tools/playthrough.py `seed_in_load_fade` asks the cartridge and the port goes on with the ramp
  (CT_SEED_LOADFADE; check `leg-window-seed-inside-the-load-fade-vs-cartridge`). Leg 121's pictures were ~24000 px
  off on every sample; leg 119's end picture went from 54009 to 10829 px (its elevator fight still goes another way).
- **The exit's fade-out before a load, C0/0C99** (`$0119` falling): leg 124 (0B at the cut; the cartridge fades to
  f10, loads, and the new room's colour-slot record 0 is armed at f31; the port loads at once and arms it at f12).
  NOT handled. It showed when the effect lock began to park the bit-7 records as D1/F426 does
  (battle_effect_lock_park.md): until then the record ran 200 frames through the leg's two locks in the port and the
  19 frames were hidden by the cycle's length. The leg's cmp is DIFFERENT by its end picture alone, 14 px (the light at
  x 192-197, y 191-195); its sequences are the cartridge's.
- **Mid-walk** (leg 127, Down held on a slope at the cut): the port's first frame moves otherwise, every later one is a
  frame behind, and the room's rotating tile checks fire its cutscene at f42 for the cartridge's f38. NOT handled (the
  leg's first key should be the pad frame before the cut, as tools/leg_window_cmp.py port_keys does).
- **Inside a fight** (leg 120): the port cannot be seeded there at all.

None of these is the game: a continuous run (tools: build/_pt77/chain5.py) never starts inside them.

## What a load a few frames short does downstream (2026-11-12, the picture re-audit)

The whole-run picture audit after the day's fixes (7351 samples: 3419 differing before, 2398 after; 47 legs better) had
two legs with MORE differing samples. Both are the load clock's class (a room's load a few frames short in the port),
carried on by mechanisms that quantise time:

- **Leg 78, the fight of loc 11D.** The room loads at f760 for the cartridge's f763; the enemies' field walks run those
  three frames more, and at the transfer (f1233, C0/0616's records) the second enemy stands one pixel lower (`$29CE` 0F
  for 0E -> its foot `$1D27` 7F for 7E). Its mode-1 walk never reaches its spot, so C1/3833's `INC $9829,X` -- the
  arrival latch C1/84B8 tests (`LDA $9826,X / BEQ`: no turn) -- does not come, and the fight's fourth action is another
  combatant's (the cartridge's f1957, the port's f2094). Three actions in, everything was a frame apart.
- **Leg 130, loc 1A7's cutscene.** The controller's first op runs at f2164 for the cartridge's f2167 (f2163 before
  c834524: the load function's waits). From there: the text boxes close on the pad's own A but on a two-frame lattice
  from their opening (144 frames on the cartridge, 148 in the port, both even); a call that waits on an object with a
  three-frame visit divisor (`$1000,X` counting 3-2-1: obj 22, `04 2C 69`) takes 6 frames or 4 by the phase; and the
  two `EB 20 00 / ED` fades end on the SPC's lattice, two frames later each. The room's exit went from the cartridge's
  f4349 + 2 to + 5. The song command `EA 3A` after `04 2C 58` shows its own address in `$1180` for the upload's seven
  frames in the port only: the cartridge stores the address when the object yields, after the JSL.
- **Leg 91, DIFFERENT by its end picture since 2026-11-13 (120 px, the room's animated tiles).** With the tile animation's
  run counter exact across fights and loads (field_bg_anim.md), legs 07, 48, 83 and 86 lost their end-picture differences
  and this one gained one: loc 0A9's load is a frame longer in the port (the counter resumes on f287 for the cartridge's
  f286), so the field's `$0153` runs on the other phase, and at the fight's transfer (f2388) the cartridge has the bit
  clear on the frame the fight is called in and set on the module's tick 1 -- steps on f2387, f2389, f2391, an even
  cadence through the transfer -- while the port has it set on the transfer's frame: f2386, f2388, f2391. One run behind
  from there. Before, the port ran no step on the transfer's frame at all and the two errors cancelled on this leg.

- **Leg 26 (SAME), a fight's exit a frame early (2026-11-14, the picture audit's 37 samples).** The first fight's return
  lands on f4687 in the port for the cartridge's f4688 (`$0117` 01 -> 00, `$0118` -> 01: the battle exit's lattice,
  closed 2026-11-07), and from there the field runs a frame ahead: `$0153` on the other phase. Two quantised mechanisms
  carry it into the pictures. The next fight stages its second enemy one pixel lower (record `$29BF + 5 * 12` fineY 0C
  for 0B; its foot `$1D2B` 5C for 5B), a knight drawn one line down for the whole fight (~470 px a picture,
  f5000-f6400). And loc 0C6's camera pan, which steps on the phase, starts a frame late (`$1D8D` one count behind from
  f7414) and stops on the cartridge's own frame, a step short: `$1D0E` 0B where the cartridge's is 0A from f7550 -- the
  room one line off (s20_shift: dy -1) until the next move (16000-26000 px, f7550-f9900). Every object position is the
  cartridge's on f4600 and f4686; nothing else was found to differ.
- **Leg 134 (SAME), Death Peak's walkers (2026-11-15).** Every field load of the leg ends a frame or more early in the
  port (the load clock, closed 2026-10-13), so the play clock `$0400` runs ahead: +1 frame from 0F4's load (the clock
  resumes on f64 for the cartridge's f65), +3 by 0F5's (f16266: 2A05 for 2705). C0/0C70 seeds the field's RNG cursor
  `$01F8` from the clock at each load -- the cursor is the clock's low byte on both sides (cartridge 1A at clock 1A36, the
  port 1B at 1B36; 27 / 2A at 0F5) -- so the room's wandering enemies take other rolls: the same walkers on other paths
  (400-1200 px a picture f16200-f21000, the audit's 39 samples). The user's 2026-11-07 class: a random roll moved by a
  timing artefact.
- **Leg 130 (SAME), the rest of the picture audit (2026-11-15).** Fixed: the naming screen's arrows over a full name
  (C2/E34A, f9500-f10800) and four differences of the exchange after it (the entry fade from the resume's brightness, the
  pose request on P, the hands' order, the live close: disasm/notes/menu2_form_screen.md, the 2026-11-15 section). Timing,
  carried: loc 1A7's cutscene starts a frame or more off (above); the port enters loc 1D0 on f4724 for the cartridge's
  f4719, the scene there runs on the pad's A presses and its exit lands on f5304 for f5305, and the port stays a frame
  ahead through 071 and into 1AC (its
  load f5854 for f5855, the same 33 frames on both sides). In 1AC object 7 runs every fourth frame (`$100E` 4-3-2-1) and
  waits for the lead's walk; its visits fall a frame earlier in the port (f8350 for f8351), so the condition the walk makes
  true between them is seen a visit later: its `E3 00` (C0/3C0D, `$011F` = 0) on f8362 for f8359, the camera three lines
  further down for the rest of the scene (`$1D0E`/`$1D96`; 19000 px a picture, f8360-f9000). After the exchange the close's
  song handshakes end a frame early (the SPC/C7 pace, closed 2026-11-07) and the field resumes on f11798 for f11799; the
  same lattice moves the camera again (f11900-f12050) and the followers by a pixel or two after it.
- **Leg 104 (DIFFERENT), the port's party falls (re-read 2026-11-15).** Without the cartridge's play clock and cursor put
  back, the port reaches loc 12D at f42503 for the cartridge's f41847 and the Black Tyrano fight takes other rolls (the
  2026-11-07 class): the party's HP reaches 0 on f77957, the game restarts (loc 000 on f78352, the load shell `C8 41` on
  f78817, New Game and Crono's naming `C8 C0` on f78853) and the leg's pad types into that screen. So the pinned end picture
  is the naming screen against the cartridge's field in loc 133; it moved by the naming arrows' fix (53929 -> 53927 px).
  The verdict with the clock and cursor put back is the row above (SYNC-ONLY).
- **Leg 92 (DIFFERENT, timing), the picture audit (2026-11-15).** Fixed: the Flea? fight (f32800-f33600, 6144 px and
  632 px a picture): her death counter's name box comes before Crono's tech's effect teardown, which runs inside the
  counter's open, and the relisted fallen enemy is drawn as the combatant (disasm/notes/battle_counter_gating.md sec. 9).
  Timing: the torches' 116 px samples (f26400-f117000) are the tile animation's run counter `$051D` one run behind from
  loc 0A6's load (the counter resumes on f1297 for the cartridge's f1296: the load clock, the f1295+1 warp above), the
  room's flames a step apart wherever they stand.
- **Leg 49 (SAME), loc 0E2's fades (2026-11-15).** The leg's picture audit samples f9000-f11100 (7000-35635 px, whole
  frames) are fade levels two frames apart. Loc 0E2's load ends a frame off in the port (its ramp starts f657 for the
  cartridge's f656: the load clock); from there the scene's script runs off the cartridge's frames (the `$0117` 0001
  of f5360 lands on f5364), its text boxes open two frames early (`$0127` growing from f8033 for f8035) though they close on the pad's own A on both sides (f8088), and
  the op after each box, the `$0117` = 80 fade, lands two frames early (f8103 for f8105, f8294 / f8296, f8485 / f8487,
  f8804 / f8806); Robo's naming screen (`C8 C3`) fades out from f11061 for f11063 and in from f11094 for f11096.
- **Leg 35 (SAME), the Dragon Tank fight a frame off (2026-11-15).** The fight starts on f1705 on both sides (`$0117`
  0100), but the port's first idle iteration comes a frame early (`$993A` 01 on f1728 for the cartridge's f1729: the
  boot's pace, as legs 109 / 118). The menu pass then runs a frame ahead of the cartridge's, so the pad's A of f1809 is
  seen on f1813 for f1810, and the command commits on f1837 for f1834; in those three frames the tank's own turn comes
  due, and its message (`CD/0295`, id 02) plays before Crono's action in the port (the action opens on f1856 for
  f1838). From there the ATB timers run apart (Lucca's 1C for 1E at f11857), the turns land a few frames apart and the
  Grinder's wheel stands in other poses (200 px a picture in the audit's 39 samples).
- **The harness's held keys (2026-11-15).** The route's pad has 26 places in 11 legs (14, 16, 17, 23, 25, 27, 42, 45, 46, 50,
  51) where one key fills two consecutive segments (`100:2,100:2`). The cartridge's --btnscript holds it with no release:
  one press. The port's --walkscript raised the key's edge again on the second token: a phantom press. Leg 17 shows it: the
  second A probed obj 21 while the queen's box was closing (f2891) and its scene ran forty frames early, where the
  cartridge activates it on the next real A (f2931). The H tokens now raise an edge only for a key the token before did
  not hold (field.c), and leg 17 is 0 px from f2100 to f6300.
- **Leg 94, the Juggler's number (2026-11-15).** Crono's tech on the Juggler (cmb 7, f9561): the cartridge's "147" stands
  32 lines above its foot (C1/5B5E: `$A00A` 01 from bit 5 of E4/F604 + id*10, offset `$971A` E0); the port rose it from
  the foot, under the battle window. battle.c stages the record's flag and offsets and battlePopupOrigin takes C1/5B4D's
  branch: f9560-f9630 0 px, and the audit's f10800-f16200 / f24300 / f24900 (the fight's other numbers on it) 0 px too.
- **Leg 94, the barrier's statues (2026-11-15).** The first fight's epilogue marks objs 16 / 17 removed (`$1A81` 0, the
  `$1000` pause); a script undoes both (op 0x0C, op 0x90 on f18867) and the cartridge draws them rising from the pits.
  The port's removal also set `dead`, so they stayed off the screen (1199 px on f18900); ct_vm.c now writes only the
  two bytes and f18860-f18960 is 0 px.
- **Leg 94 f32386-f32440, timing (2026-11-15).** The next fight's slot 0 (cmb 3, field obj 14) is staged with pose 0B
  on the cartridge and 0A in the port (371 px, static). C0/05EB copies the object's `$0F01` on f32343; the cartridge's
  drain committed 0B on f32342. The port's obj 14 animates three frames behind: its function starts with the call
  obj 11 makes on f32228 on both sides, but its cadence countdown `$1001,X` expires on f32231 in the port and on f32228
  on the cartridge -- every object's cadence in the room is one frame ahead in the port since the previous fight's exit
  (the field returns on f30754 in the port, f30755 on the cartridge), the battle exit's release of the 2026-11-07
  timing class. Carried as timing.
- **Leg 90 (castle wings), the candles' flames (2026-11-15).** The picture audit's 116 px samples (f3600, f3700, f4200,
  f4300; 0 px between) are the flames' tile animation a step apart: loc 0AB's load begins on f574 on both sides and the
  run counter `$051D` resumes on f614 on the cartridge, f616 in the port (the load clock, closed 2026-10-13), one run
  behind from there. Timing.
- **Leg 137, the summit's fight (2026-11-15).** Two picture bugs: action 84's purple disc (BG3 in front, the sprites
  alone on the sub screen and added into it, f4585-f4715, up to 6353 px) was not drawn, and slot 0's corpse showed its
  dither level whole where the cartridge ships it packet by packet (33-44 px, f8972-f9100). Both fixed (battle.c
  battleFxSubObjOnly, field.c battleDissolveBlitCells); the leg's audit samples f4400-f9300 are 0 px at the fight's lag.
- **Leg 91, the enemy's afterimage (2026-11-15).** The picture audit's f62700-f92100 samples (103-1005 px) are cmb 3's jump
  in action 7B: the cartridge draws four afterimage copies behind it (`A4`, CF/E92C), the port drew them for PCs only.
  Fixed (field.c battleEnemyTrail): f69860-f69970 is 0 px but for the record/draw coincidence frames (every sixth, up to
  415 px, open in battle_afterimage.md) and the landing (f69962-f69970, 197 px, not yet read).
- **Leg 95, Magus's shadow in palette 5 (2026-11-15).** During action 9F `$9F46` bit 3 is set (CD/1B8B) and the
  shadow's attribute is 3A: palette 5's colour 15 on the cartridge (f14400, 58 px). The port drew it in palette 1 only;
  fixed (field.c battleShadowRender). The leg's f14300 (10 px), f15400 (799 px), f16100 (1 px), f17100 (3 px) and
  f17400 (489 px) are other, not yet read.
- **Leg 93 f38400-f38700, open (2026-11-15).** The target hand at (184-199, 33-44) is dimmed on the cartridge: lines
  33-160 run TM 17 TS 13 CGADSUB 44 (BG3 math, half, the sub screen added) with BG3 at map $7000 / chr $5000 -- the
  room's BG3 layer, in front of the sprites (BGMODE bit 3) -- so the hand shows as (BG3 + hand) / 2. The battle's effect
  compositor keeps BG3's map at $7C00 only (battleFxBg3PixelP returns 0 below it) and BG3's characters as the effect's
  sheets, so the port has no BG3 there and draws the hand whole (128 px). Needs the field's BG3 map and characters
  carried into the fight; not done.
- **Leg 29, the ball's re-drawn victim (2026-11-15).** The ball chose Lucca on f1422 and acted after she fell: the
  cartridge's re-draw C1/8DE4 gives Marle and stores her in `$AD8E`, `$AECC` and the mask `$AE95` (C1/8DF7-8DFF), so the
  script's target `$A017` is 02. The port hit Marle but left the mask on Lucca and the ball rolled to her; the fight
  went another way (f1724-f5400, 260-1700 px). Fixed (battle.c, the C1/8DE4 arm): f1800-f6600 is 0 px. Open: f1660
  (one frame, Lucca standing in the port, 503 px: her fall's pose shows the previous pose's tiles a frame in the
  port). Timing: f7500 (319 px) and the gate's swirl from f8400 (2300-2900 px, a palette step apart) -- the fight's exit
  lands a frame early in the port (`$0117` 00 on f6722 for the cartridge's f6723, the 2026-11-07 class) and the room's
  animations keep the offset.
- **Leg 58 (heckran cave), timing (2026-11-14).** The cave's torch a step off (f3300-f11400, 63-150 px): the field's tile
  animation (`$051D` / `$0530`) is a step ahead in the port from f3012 -- right after a fight's return the cartridge's
  main loop is parked two frames by the exit's sound handshake (the SPC fade lattice class, closed 2026-11-07).
- **Leg 74, the fight's boot (2026-11-15), open.** The fight from f589 opens on four reptites at the same foot Y; on the
  cartridge the OAM mirror stops changing when the fight begins (f588, no writer -- the field's last OAM stands) and
  the boot's pictures (f590-f640) show the field's order, cmb 6 in front of 5, 4, 3 (OAM 28-43). The port draws the
  boot with the compose's sort (CF/E87B, a tie keeps the lower object in front: 3, 4, 5, 6) -- 162 px a picture until
  the compose takes over. Needs the frame the cartridge's compose first writes the mirror and the field draw list's
  order for ties; not done. Also open: f2700 / f3000 (Ayla's pose, 246-734 px at both lags).
- **Leg 117 (Kajar), 2026-11-15.** The audit's samples f26100-f32700 are 0 px at the fight's lag with the current tree.
- **Leg 30 (the trial), open (2026-11-15).** From f3300 the scene's pictures differ in many places (99-4449 px at the
  field's lag), and from f4800 the guards stand in another arrangement (757 px on f5100, no port frame within +-20
  matches): a walk that ends elsewhere, not a shift in time. Not triaged.
- **Leg 56 (Medina's gate, loc 1D1), open (2026-11-15).** From the leg's first frames the cartridge draws a white
  furry creature at (104-136, 55-101) where the port draws another sprite (505-623 px, f30-f1200). The seed has obj 10
  at tile (7,9) with `$1100` 06 and `$1101` E1; whether the port's seeded actor takes another sprite (the prescan's,
  not the capture's -- the audit's known seeding gap) or the game draws another is not yet read.
- **Leg 82 (Nizbel), the room a line off (2026-11-15), timing.** The fight from f2214 ends on f28853 in the port for
  the cartridge's f28852 (`$0117` 0001; the 2026-11-07 class), so the field's `$0153` runs on the other phase and the
  cutscene's camera pan stops a step short: the room is one line off (s20_shift dy +1: 0 px) through f28960-f29040
  (25000-33000 px), the leg's f29100 / f30300 / f30600 samples with it.
- **Leg 82, inside the Nizbel fight (2026-11-15).** The arena's torches are a step apart (23-28 px, two frames in four:
  the tile animation's phase, the load clock class). Open: f14399-f14400 (430 px), Ayla's landing from a jump drawn
  differently for two frames while `$1D24`, `$A4B0` and the pose `$9908` are the cartridge's frame for frame.
- **Leg 110 (Mt. Woe), the run-off's gauge (2026-11-15).** Fixed: the run-off's end reloaded seat 0's ATB (42 / 42) where
  the cartridge's stays 00 / 01 (the flee leaves through C1/8212, not C1/BCE1): the exit's pictures showed the gauge
  empty (224 px, f21000). Open: f26100 (10480 px at both lags), f15300.
- **Leg 111 (Mt. Woe chests), open (2026-11-15).** f13800: the target hand's pulse a step apart (88 px, both lags);
  f14397-f14405: the target's name ("Gargoyle") a frame later in the port; f14406+: 34 px under the window's bottom
  line at (136-147, 32-36).
- **Leg 115 (Dalton), open (2026-11-15).** The overworld sequence of the Blackbird (loc 1F4, f9900-f10230): a vertical
  beam (x 125-131, f9960-f10050, 142-1308 px), whole frames (f10080-f10110, f10230) and the Blackbird itself, drawn by
  the cartridge on f10170-f10200 at (72-183, 95-182) with no OAM on screen (a background layer) and absent in the port.
  Not read.
- **Leg 110 f26100 (10480 px at both lags), open.** Mt. Woe's mist layer at another phase (as Death Peak's snow, 134).
- **Leg 74, the fight's boot: FIXED (2026-11-15).** The boot's pictures (f590-f643) keep the field's OAM, the draw
  list's order (battle_sprite_zorder.md, "The boot"); the port's are 0 px now. Still open: f2700 / f3000 (Ayla's pose).
- **Leg 29 f1659-f1660: FIXED (2026-11-15).** Lucca's fall two pictures late: the close's C1/41BE / C1/416A publish
  the fallen pose two rows before the HP write (battle_pc_pose_ladder.md sec. 8). Still open in the leg's fight: three
  windows of alternate frames (f1206-f1219, f3182-f3190, f3734-f3749; 294-676 px), not yet read.
- **Leg 29's fight is the cartridge's frame for frame (2026-11-15).** Fixed after the fall: the enemy's hop in place
  (a script's op C4 / C5: a picture early, three hops) and the close of Marle's actions (the baked crossbow pose at seat
  1's anchor, f2116-f2119 and three single pictures). f790-f6696, every frame at the fight's lag: 0 px. The last three
  (f6697-f6699, 25 px) are the exit's frame (timing).
- **Leg 47 never won the race; the win softlocked the port (2026-11-15, GitHub issue 10).** The route crosses the race
  with no input, so the player's finish ($7E:4788) and the three tasks it leaves ($4677, $4755, $416D) were never played:
  the port had none of them and its stage had no END. Ported, and played by the BRANCH leg `47b_race_win` (from
  46_johnny's state: Up off Johnny's lane, one boost before the line): SAME, the run-out's pictures 0 px, loc 0E1 on the
  cartridge's frame. The scene in 0E1 after it is a frame early in the port (control back f4573 / f4574); 0DF's after
  the lost race is two late (f3243 / f3241): the location's load clock (timing). sequence_table.md sec. 12, "The WIN".
  **What this says about the route:** it is ONE way through the game. Where the game branches on what the player does
  (a minigame won or lost, a choice in a box, an optional fight) only the branch the route took is exercised. Branch
  legs are the tool for the others; none but 47b exists yet.
- **Leg 95, Magus's fire (f17340-f17406): fixed 2026-11-15.** The bursts were not added over the hall (up to 11171 px,
  34 pictures): the effect's window masks compared raw where no window is on. 0 px now. battle_effect_orbit.md.
- **Open: the re-race with the Race Log (`FF 09`) is not played on either side (2026-11-16).** Scene 223 / 225: Johnny's
  talk opens with `C9 DD 12` (event op C9, C0/3780: no item $DD -> jump), so the robot's mode choice -- the only writer
  of `$7F0136` bit 7, which the bike's `16 36 80 86 05` turns into `FF 09` -- needs the Race Log, and no state of the
  route holds it (the chest on Lab 32's road is not opened by legs 48 or 107). A branch leg that takes it, picks the
  mode and races is what would grade `FF 09`'s setup (the six patched bytes) and $7E:4677's lanes at 46C3.
- **Leg 48 (Lab 32's road), the Octopods a picture early: FIXED (2026-11-16).** The audit's f5100 sample (25 px, an eye)
  was the visible end of a general bug: every animation step of a class-1 field sprite was drawn in the frame of its
  `$0F01,X` commit, one drain visit before its tiles and template (861-918 px on three pictures in five while the three
  walk). f5040-f5160, every frame: 0 px. anim_drain_cad9.md sec. 17.
- **Leg 08 (Leene's Bell), Marle's snap to the stall's tile: FIXED (2026-11-16).** The audit's f1200 sample (110 px). Op
  0x96 on its target's tile runs the centre snap C0/305D each visit, which publishes 10 / 10, then 00 / 10, and the
  selector C0/5614 on it: 10 + 10 = $20 is the RUN for a character below 4, so Marle runs eight frames (`$1686` 06 by
  C0:5684 on f1185) and walks again on f1193. The port's snap published nothing and re-selected nothing: `$1906` /
  `$1986` stale, the walk's frames through the snap (105-237 px on fifteen pictures). f1150-f1260, every frame: 0 px.
  ct_vm.c mvArriveSnap305D; checks field-picture-tile-snap-reselects-the-animation, leg-window-tile-snap-anim.
- **The audit's grid, 2026-11-16 (build with the race's win).** A picture every 300 frames over the 76 legs that had a
  differing sample: 2,900 samples, 539 differing at both of the usual lags -- stretches a frame or more off for a timing
  reason counted in. The windowed pass (best lag in -2..+5) is what separates them; legs 08-34 read so far have one
  sample each that fits no lag (11 f3300 6 px, 12 f2100 14 px, 17 f1800 98 px, 21 f1500 10 px, 32 f7500 48 px, 34 f16800
  148 px): open.
- **Legs 32 and 21, a sprite's priority is its template's: FIXED (2026-11-16).** The audit's leg 32 f7500 (48 px) and leg
  21 f1500 (10 px) samples. The pieces' OAM attributes are written with the template; the port read `$0C00,X` /
  `$0C01,X` live. 32: f7470-f7530 0 px; 21: f1480-f1520 0 px. anim_drain_cad9.md sec. 18. Still open from that batch:
  the Gate's window edge (legs 11 f3300, 12 f2100: 3-31 px on three frames in eight while it opens), leg 17 f1799-f1800
  (98 px, two frames), leg 34 f16800 (148 px, a fight).
- **Leg 17 (the castle, loc 07A), the torches two frames ahead: the tile animation's phase (2026-11-16).** The audit's f1800
  sample (98 px on f1799-f1800: the flames and the glows under the floor). The room loads on f1541 on both sides; from
  there the runner's call counter `$051D` is one behind in the port (BC for the cartridge's BD on f1794) with `$0153`
  equal, so records 0-5 take the other parity and their accumulators (`$05B0`: 2448, 24F4, 25A0) step on f1794 / f1797 /
  f1801 for the cartridge's f1795 / f1799 / f1803. One call's difference at the load -- the class of legs 58 and 90
  (the load's own frames). Not proven here from one state; the count is what was measured.
- **Leg 34, the prison's fight: FIXED (2026-11-16).** The audit's f16800 sample (148 px) and four more things the fight
  showed once it was read frame by frame: a hidden enemy's palette at the staging (C0/05F4 `JSR C0/72B4`), its stored
  on-screen pixel and the drain's FF in the record, an enemy off the alive list still drawn, the script's hide a picture
  later, and op 5D (never ported: the called guard a picture late). f15480-f16840, every frame: 1361 of 1361 at 0 px.
  battle_entry.md, battle_asc_ops_orbit_shake_offsets.md sec. 9.
- **The draw gate (2026-11-16): an object is drawn from its first staged template.** Found through leg 34's hidden
  enemies (the host wrote its ticker's frame over the drain's `$0F01,X` = FF) and measured on leg 63's bridge (objects
  latched on f1543 are on the picture of f1546 / f1547, not f1545). field.c draws on `$1B00,X` bit 7 and writes nothing
  to `$0F01,X` under the legs. anim_drain_cad9.md sec. 19. Leg 63 itself: by f1543 the port's script
  runs two frames behind the cartridge (the latch above: f1545 for f1543), and the party stands 2-3 px further west
  and a walk frame apart (`$0F21` 08 for 09; HYPOTHESIS: the script stops the pad's walk two frames later): 734 px in the party's box on every picture of f1500-f2030 at every lag, 0 px
  from a state cut on f1530. Where the two frames come from was not traced (the room loads on f504; the load's own frames are the class of legs 58 and 90).
- **Legs 11 and 12, the Gate's ring: FIXED (2026-11-16).** The audit's f3300 / f2100 samples (3-31 px on three pictures in
  eight). The ring is BG3, and BG3's drift was a frame ahead since the song's upload on f1569: the port ran the camera
  leg again on the frame the upload returned. Leg 11 f3250-f3340: 91 of 91 at 0 px. song_load_freeze.md sec. 9. Leg 12
  f2070-f2130: 59 of 61; f2101 / f2102 keep 2 px each on row 51 (x 126-127, then 133-134): open. They are pixels of OAM #6 (tile 10C, a sparkle
  moving 7 px a frame) that the cartridge shows and the port does not. The room's five sparkles (objects 16-20, class 0)
  commit their frame 03 one after the other -- f2096, f2097 (two), f2098, f2101 (the drain is refused while the
  upload gate `$09A0` holds work, C0/CA76's prologue). Not the uploads' latency: C0/B1B2 sends the whole queue in the
  NMI that reaches it (B1D1 `LDA $A0,X / BEQ ->B201`, two DMAs an entry, B201 `STZ $A0`). Five sprites overlap on
  those pixels (OAM #6, #11, #19, #23, #27); which one the port draws differently is not read yet.
- **The audit's window pass, 2026-11-16 (build 515bf1f, before the song-load fix above).** The 76 legs that ever had a
  differing sample, a picture every 300 frames, each graded at the port frames -5..+2 around the cartridge's: 4786 samples,
  4187 equal at one of the two usual lags, 104 equal at another lag only (a stretch a frame or more off), 495 differing at
  every lag. Of those 495, 402 are in fifteen legs' long stretches (26: 61, 35: 37, 49: 17, 55: 12, 58: 15, 65: 33, 78: 13,
  92: 26, 95: 10, 120: 18, 122: 17, 125: 21, 127: 61, 130: 22, 134: 39) and 93 in 48 legs with one to seven each (11, 12,
  17, 29, 30, 37, 38, 40, 42, 44-47, 51, 54, 56, 60, 61, 64, 67, 68, 71, 74, 76, 82, 86, 88-91, 93, 94, 96, 100, 101,
  103-106, 111-113, 115, 121, 131, 133, 135, 136). A sample that fits no lag is not yet a drawing difference: leg 63's
  party (above) and leg 37's mover (f4180-f4211: an object 6-7 px a frame along y 141-149, a frame apart on the two
  sides) are state a stretch's offset left behind. Each is to be read: fixed, or tied to the frame its offset began.
  Read so far from this list: 11 and 12 (fixed, above), 17 (the tile runner's counter since the load), 34 (fixed), 63.
  Leg 68 f290-f312 (the overworld, lag -1): the two followers' walk frames are out of step with the cartridge's (78-230 px
  on every picture); not read further.
- **Leg 68, the overworld's followers: FIXED (2026-11-16).** Not the seed: the walk an event hands back began a frame late
  in the port (the lead's init did not fall into its input frame) and the followers' init copied a lead that had not
  stepped. f20-f320: 76 of 76 at 0 px. ow_party_followers.md sec. 7. Leg 76 f600 (192 px in the same box): FIXED the same day -- the exit's `$027C` = 3 is the lead's own store, so the
  followers stand on that frame (ow_party_followers.md sec. 8); f570-f611 0 px.
- **Legs 56 and 61 (2026-11-16, read by a triage subagent and confirmed).** 56 f300 / f600 / f900 (590-623 px): not the
  game -- the WRAM seed kept the sprite its own run of fn0 chose on a fresh cartridge's levels (loc 1D1's master, `83 E0`
  for the state's E1); the seed takes `$1101,X` now. 61 f2400 (62 px): a real one -- the fight's boot pictures are the
  field's OAM with the field's OBJ priority; battle_sprite_zorder.md. f2388-f2447 0 px.
- **Legs 51, 54 and 64 (2026-11-16, read by triage subagents; the fixes confirmed here).**
  - 51 f600 (5774 px, the fight from f585): FIXED -- the boot never stored `$CA25` = 08 (D1/F290), so a leftover request
    in `$CA24` installed HDMA preset 2A; f586-f623 0 px. FIXED the same day beside it: f624-f627 (300 px, the room's lights) and the
    hand-over picture f585 (304 px) -- the base palette page is copied onto the live one twice, by the field's staging
    (C0/0409, f584) and by the boot's stager after the song's upload (CC/E6EC, f622); the port made one copy, at the
    entry. battle_boot.md, "The palette pages at the entry". f584-f640, every frame: 0 px.
  - 54 f300 (1853 px): not the game -- the room's script-drawn tiles are in VRAM, which a WRAM seed does not carry (the
    class of leg 53's loc 0EB above); played from leg 53's start the same frames are 0 px. 54 f600: the load's own frames
    -- 0E6's load is 37 frames on the cartridge and 38 in the port (the ramp's first step f349 / f350), and with one idle
    frame put in the port's pad at f349 every picture of f340-f740 is 0 px.
  - 64 f600 / f900 / f1500 / f1800 (all in loc 08A): the load's own frames again, the other way -- the ramp's first step is
    f412 on the cartridge and f411 in the port, and every room-clock cell runs a frame ahead from there while the pad's do
    not; two parks of the main loop then leave the tile runner 3 runs behind and 35 ahead (it runs from the line-211 IRQ
    on the parity `$0153` was left with). From one cartridge state cut after the load every picture is 0 px (f583-f640,
    f725-f942, f1480-f1520, f1780-f1820). Seen there and not followed: `$1C2E` (object 23's `$1C00,X`) reads 03 on the
    cartridge and 07 in the port from an A press on f726; the script pointer's mirror `$11AE` is rewritten at the
    object's next visit after a song park (0940 for the cartridge's 0942, eight frames).
- **Legs 26, 127 and 134 (2026-11-16, the second triage round; three subagents, read-only).** What they classified is
  theirs (cut-state runs, not re-measured here) unless marked CONFIRMED; the fixes were derived and measured here.
  - 26 (61 samples): two chains, each 0 px from one cartridge state. (a) f5100-f6300, a Hench one pixel lower: fight
    1's exit is released a frame apart (`$0117` 01 -> 00 on f4688 for the port's f4687; the 2026-11-07 class), so the
    pair's scripted walk starts a frame earlier and ends on the same frame -- one step more (`$189C` 28C3 for 28BC);
    from a state cut on f4760 every cell and the picture at f5110 are equal. (b) f7800-f24300, loc 0C6 a line off and
    the Yakra fight after it: the room's load is 77 frames on the cartridge and 78 in the port (the load clock), the
    pan's first step f7414 / f7415 and its stop on the same frame -- one step short (`$1D0E` 0B for 0A); from a state cut
    on f7380, 16 samples f7500-f24300 are 0 px. FIXED on the way (CONFIRMED, real ones): the door before that room
    opened a picture early (568 px, f7248) -- the block is the IRQ's to write, on its parity
    (field_doors_occluders_decisionbox.md sec. 4; a chest's lid the same, leg 93 f26048) -- and the lead's outline was
    behind the door frame on f7272-f7275 (23 px: his pieces keep their template's priority).
  - 127 (61 samples): the start is the mid-walk cut (played from leg 126 the first 407 frames are 0 px). From f753 a
    time shift whose first cause is found but NOT one of the closed classes: the song upload of op `EA 29` parks the
    main loop 41 frames on the cartridge (f714-f754) and 40 in the port -- `$0153` on the other phase from there, the
    fight's call a frame late (`$0117` = 21 on f894 / f895), the ATB a tick behind, another fight by f12300. FIXED
    inside the park: the clouds stood a line on for its 38 pictures (4013 px) -- the screen's layer scroll is the last
    table build's (song_load_freeze.md sec. 10). OPEN: why
    the upload is a frame short (and whether outside the chained run too). FIXED (CONFIRMED): the Golem Boss in full
    colour on three pictures before its fade from black (f412-f414, 1676 px) -- a zeroed palette row is black.
  - 134 (39 samples; f300, f4800, f12300, f12600 read): loc 0F4's load from the overworld is 35 frames for the
    cartridge's 36, all three times (f64 / f65, f12342, f13552); every other load of the first 15,100 frames is equal.
    The play clock is +1, +2, +3 from them, C0/0C70 seeds `$01F8` one on (06 / 07 at 0F2), and the idle animation's
    roll (C0/A22A `INC $F8 / LDA $FE00,X / CMP #08`) falls on another object (f4770: object 7 on the cartridge, 6 in
    the port). From states cut after each load the samples are 0 px. Not derived: why 0F4's load alone is short.
    Seen and not read: one picture at 0F4's exit (f382, 19,712 px: the old room very dim on the lower lines on the
    cartridge, black in the port).
- **Legs 35, 65 and 92 (2026-11-16/17, the third triage round; three subagents, read-only).** Classified by them from
  cut-state or shifted-pad runs (not re-measured here) unless marked.
  - 65: FIXED (CONFIRMED) -- the battle target hand drawn opaque where the cartridge half-adds it under the mist (131-138
    px, f32988-f33005 and f37621-f37638; check field-picture-target-hand-under-the-mist). FIXED 2026-11-19 (the step around CD/0453: none on its entry frame, two on its return's; checks field-picture-teardown-*): the description
    box's typewriter a character off for 10-20 frames at a time (f43170: the port steps on the frame `$A013` clears, the
    cartridge does not -- C1/007E's wait CD/04A6 types nothing; f43191 / f37973: the cartridge steps twice, a direct
    CD/3E75 on top of CD/04AA's step; battle_message_window.md sec. 6). OPEN, real: f37958, one picture -- after an enemy's
    action the cartridge restores BG3's tilemap / scroll on one frame and TM / TS / the colour math on the next; the port
    does both at once (30039 px). f37800 and f40500: 0 px from cut states.
  - 35: the Dragon Tank fight's boot is a frame short in the port (`$1E01` = 08 on f1724 for f1725, the band's seed f1725
    for f1726: 14 frames from `$7C` = 0 against 15); with one idle frame taken out of the port's pad at f1780 every
    sample f1800-f21600 is 0 px except the screen shakes, whose `$C0:FE00` reads run off `$7C` a frame apart. OPEN,
    picture: the tank's hull colours during the boot (631 px, f1709-f1727), one HUD-window colour step on f1740, a spark
    tile on the wreck (f21789-f21792 / f21809-f21812, 24 px), the wreck's debris four frames longer (f22532-f22535).
  - 92: timing -- loc 0A6's load is 45 frames for the cartridge's 44 (ramp f1296 / f1295); the play clock, `$051D` and
    `$01F8` stay a step behind and the torches flame a step apart. 0 px from states cut on f4000 and f26320.

## The overworld's unported natives and maps (2026-11-18: legs 44, 86, 115)

Port bugs, fixed (disasm/notes/ow_objvm.md "The natives of 2026-11-18", ow_colour_math_per_world.md's last section):

- **Leg 44** (the Day of Lavos, 1F7): f2382-f2511 the record's green grid (BG3 on the sub screen added to BG1 / BG2: the
  compositor's sub-screen math reached BG3's pixels only), f2445-f2460 / f2477-f2480 / f2697-f2700 the static (C2/7A56,
  7AAF, 7ADA, 7AEB unported; no BG1HOFS channel read; C2/09C5's extent), f3900 the rain (C2/7A0C / 7A31 unported). Every
  world frame f2370-f2760 and the samples to f4300 are 0 px at port -1.
- **Leg 86** (the Magic Cave opens, 1F1): f14947-f15146 the beam (C6/E000 and C2/7911 unported). The world stretch
  f14909-f15170 is 0 px at port -1.
- **Leg 115** (the Blackbird over 1F4): f10172-f10226 the ship (in the world's own BG1 map packet; the port loaded a
  capture taken with it removed) and the fixed colour's ramp (C6/E170 / E1FF / E237 unported). 0 px at port -3.

Timing left in leg 115 (TRACED 2026-11-19, the battle exit's class, closed 2026-11-07): the Golem fight's exit releases on
another frame -- the ack cells `$0117` / `$0118` part on f8493-f8498 and `$0153`'s parity is opposite from there on; the
room's objects run a frame out of phase (object 0's two-op loop, object 11's cadence), object 8's script posts `88 80` on
f9731 for the cartridge's f9733 and asks for the exit on f9829 for f9831. The port's world runs three frames ahead of the cartridge's --
on the first world frame f9888 the scripts' waits read 35 in the port for the cartridge's 38 (block $0DB0 +0A), and at
f9872-f9884 the port already holds the world's objects while the cartridge is still loading -- and every world picture
to the leg's next load fits port -3 (f10100: port f10097 0 px; the flashes of f10128-f10208: port f10205 / f10206 for
f10208).

Still open in these scenes, WRAM / VRAM only (no measured picture): the hidden tilemap columns during a scroll (leg 44
f1000-f1600: 30-100 words, the staging ring's leftovers in a row's whole-slot DMA); C2/2C93's rebuild after the time
gauge / world map (no VRAM clear before it).

## The gate warp a frame late (2026-11-18: legs 62, 72, 73, 85, 107, 105, 37)

A port bug, fixed: the native warp's program opened on the frame after the jump's (C0/3FD3 runs the quiesce, the JML and
FD/DB97's setup in the frame whose head finds `$19` at 0). Every scene after a gate ran a frame late against a pad that
did not: legs 62, 72, 85 and 107 were DIFFERENT for it and are SAME now (their rows above called them timing: the first
cause was this), leg 73's end picture went from 14407 px to 0 (still DIFFERENT on a chain). 104 of 138 legs SAME.
Exposed by it: loc 008's reboot load after the gate return is one frame short in the port (the NMI-off load clock).

## Leg 84's end picture (2026-11-19)

281 px, static: Crono's idle look (anim 21, C0/A1E9-A249) on the cartridge, Lucca's in the port. The roll is one of
three a 128-frame beat (`$0158` & 7F = 7F; `INC $F8`, `$FE00[$F8]` < 08) and the port's `$01F8` is two draws behind
(36 for 38 on f8355) with `$0158` three frames behind: loc 029's scene takes control on f6903 for the cartridge's
f6900 (both sides loaded 029 on f3846; the cadence after that load) and 028 loads on f7336 for f7333. Timing (the load
clock), not a draw bug. Found on the way and FIXED: the world's entry zeroed the whole object page (C2/0471 zeroes
the class words alone) -- `$1608` / `$160A` differed from f1210 to the leg's end.

## The legs' sound commands, command for command (2026-11-20)

`tools/leg_sound_cmp.py` (and, across the route, a scratch sweep of the same comparison): each leg's sound commands -- the
command byte and its first parameter, in order -- the port's against the cartridge's. A difference here is not timing:
it is another sound, a missing one or another order. Found and fixed that way (audio_commands.md): the Equip list's
second blip, a fight's script sounds after the menu pass, the naming screen's first post, the world's main loop inside an
upload, no script after op 0xFF < $90. Of the first 97 legs, 84 now send the cartridge's commands exactly.

Open, each the leg's FIRST difference (later ones follow from it):

| leg | first difference | what is known |
|---|---|---|
| 24, 25 | a menu blip `19 00` four frames before the cartridge's, beside the second PC's ready chime `19 42` (leg 24: cartridge chimes f5070 / f5074, blip f5076; port on the cartridge's clock f5071 / f5075, blip f5073) | the fight is in step until there; the pad's press reaches the battle menu four frames early -- the menu pass's input, not sound |
| 35, 63, 65, 94 | a `19 42` / `19 00` / `18 xx` two or three frames apart in the other order | the same family (which frame a pass or a chime falls on) |
| 43, 51, 99 | fights that part earlier in the leg | not looked at command by command |
| 54, 55 | one extra `18 xx` in a run of them, three frames after a regular one (leg 55, End of Time, obj24's function at $0C2A: cartridge f206, 218, 224, 230, 236, 244, 250; port 205, 217, 220, 226, ...) | a function called again as soon as its `00` has run; the port's object got a visit one tick after its E8 where the first time it waited four -- the call / return scheduling |
| 85 | extra `18 6E` | after the leg has already parted |
| 47 | -- | the tool's: the race stage posts from its own code, not from the two command PCs |

Leg 55's extra `18 A7`, measured further (obj24 of the End of Time: `$11B0` its script pointer, `$1C30` its priority,
`$1031` its countdown, both sides per frame). The steady cycle is the same on both: called (`$11B0` = 10CA, countdown
0), the E8 on the next frame (countdown 4), three frames of countdown, the `00`, called again -- six frames. They part
once, at the start: the cartridge's object is CALLED on f212 and its E8 only runs on f217 -- for f213-f216 nothing of
it moves, the countdown standing at 0, right after its own `E5` (the tile copy at $0C31, f201-f204) -- with the
countdown reloaded to 4; the port's call lands on f217, the E8 runs in that same frame with the countdown left at 0,
the `00` on f218 and the next call on f219: one cycle of three frames, one sound more. So it is the frames the main
loop spends on the E5's transfer (no event pass in them, `$0117` bit 5) against the caller's op -- which of them comes
first -- not the sound. Not fixed.

**Fixed 2026-11-20 (legs 54, 55).** It was the touch latch, not the scheduling: `$01EB` is written only by C0/881E's
head (`LDA #80 / STA $01EB`) and by the scan; while the camera aligns for the `E5`'s copy (`$0138` = 1 / 2, C0/1781's loop
runs C0/885A, not C0/881E) the hit of f202 stands, and on f212 -- `$0138` cleared by C0/88D8 -- the lead's op 0xB0 calls
the touch with it. The port's latch was one-shot. Check: `leg-window-touch-latch-stands-through-the-camera-align-vs-
cartridge`.

## The cartridge's clock, redone (2026-11-20)

`cmp --clock` now charges the CPU nothing for its own code (the tape holds it) and real time only for polls of the SPC's
ports; every store waits for the tape's cycle (leg 20: 293,093 of 293,093 stores, none late). The earlier mode priced C7
itself and was late by its own error, which moved the fade's lattice: most of what it reported was the harness's (the
menu blip beside a ready chime on legs 24 / 25 / 35 / 42 / 63 -- leg 24's fight is the cartridge's frame for frame in
plain mode). On the tape legs 20, 24, 25 and 33 -- DIFFERENT in plain mode -- are SAME: with the SPC fed at the
cartridge's cycles the port plays them as the cartridge does, so what plain mode shows there is the SPC lattice's timing
class and nothing else. Real differences the tape found: a song command's record left in the mailbox for a load's queued
JSLs (leg 29), $7E:317E's 8-bit STZ (leg 47), the ATB handler's whole run while a PC's turn is pending (leg 51: an
enemy's turn 8 frames late), the touch latch (legs 54 / 55), a $C3 stage's driver reset sixteen frames early (leg 47).

## The route's end: legs 138-160 (2026-11-27)

The route now runs from power-on to "The End" in one chain: 23 legs from leg 137's summit -- the End of Time, the Last
Village's shop, the Black Omen's 135 / 136 circuit for levels (eleven legs of fights, six of restocking), the bucket,
the nine fights of the shell as one battle chain (leg 157, 160,000 frames: no field moment between them), the form past
the shell and the Shelter (158), the inner form, the Lavos Core and the fall to the fair (159: 1DF's entry gives no
place to cut -- a state there has no lead yet and does not seed the port), the fair, the farewells and the credits (160).
The party is Marle / Magus / Ayla at level 54; the Core fell to Haste, Ayla's Triple Kick on the left bit and Magus's
DarkMatter while it is down (found by a backtracking search over the cartridge's own states: 147 turns, 8 steps back).

On the cartridge's clock all 23 are SAME, each end picture 0 px. On the port's own clock twelve are SAME and eleven
DIFFERENT (139, 141, 145, 146, 150, 151, 153, 154, 157, 158, 159): legs of fight after fight, where the SPC's lattice
moves a fight's exit a frame or two, the play clock with it, and `$29AD` -- the next fight's RNG cursor (C1/800A) --
with the clock: other rolls, another fight (the 2026-11-07 class; the tape puts the cartridge's clock back and the
legs are the cartridge's). What they found on the way, all fixed:

* **Leg 138 -- the maxHP clamp is every combatant's.** C1/EC7F walks all eleven at each action's close and brings a
  curHP above its maximum down (ECD1), staged delta or not. Incognito's op 07 turns into the hidden Peeping Doom, which
  takes the actor's 110 HP over its own maximum of 1; the port clamped only a combatant it had a delta for.
* **Legs 150, 154, 156 -- the fight loop's counter after a world map.** `$0027` (C1/8121 `INC`, handlers on odd counts,
  C1/82A2) is the high byte of the world program's BG1 vertical scroll (C2/09E2 `STA $26,X`): the fight's first handler
  pass falls on the parity the world map left. The port stored the scroll cells a frame after the rebuild that reads
  them, so `$26` was 0000 and every fight after a world map ran a pass -- two frames -- early. With a long practice fight
  (Spekkio's, lost on purpose for his heal) a party member's turn came ready inside those two frames and the pad's
  command went to another turn. This counter is the likeliest cause of other fights' two-frame leads in this ledger.
* **The pictures of the end's fight** (the private route's audit, then pinned on legs 157 / 159): an enemy sheet of
  more than 0x200 tiles (the Core's face), the shadow's attribute a frame early, the tilemap's cells past the staged view
  under a line wave, a sub screen with one BG layer more (the speed lines), the windows on the one arena layer the main
  screen carries (the revive's discs), one of two arena layers behind a window (the collapse), and OBJ priority 0
  behind BG3's priority tiles.

Open on the end's fight (the private route's audit of the Core's leg, 376 of 388 pictures at 0 px before the last four
fixes): the cross-fade to the square after the collapse (TM 11 TS 02 CGADSUB 01 with window 1 on BG1: the scenery's
priority tiles take another mix, 7120 px on one sample), two samples where the vortex is a frame apart from the sprites
(416 / 3631 px at port +0), and four samples of a menu list three frames apart (858-1005 px).

## The whole route on the cartridge's clock: 166 of 166 (2026-11-27)

`python tools/playthrough.py cmp all -j --clock`: every leg from power-on to "The End" and the five branch legs are
SAME, each leg's storyline, party, gold, HP and control chains and its last picture the cartridge's. It is a check now,
`playthrough-legs-on-the-cartridges-clock`, beside the plain one (`playthrough-legs-vs-rule-cartridge`, the port's own
clock: 138 of 166).

The 28 legs DIFFERENT on the port's own clock are therefore all the SPC lattice's timing class and nothing else: 20,
24, 25, 33, 43, 51, 63, 85, 93, 94, 99, 100, 109, 116, 118, 124, 127 and, of the end's legs, 139, 141, 145, 146, 150,
151, 153, 154, 157, 158, 159. In each the port's own SPC ends a fade or an upload a frame or two away from the
cartridge's, a fight's exit moves with it, the play clock with the exit, and the rolls the clock seeds (C0/0C70's field
cursor, `$29AD` at a fight's boot) with the clock. The decision of 2026-11-07 stands: the game works the same way with
other rolls, and the tape is how a replayed pad is held to the cartridge's.

What the two lists do not grade: the pictures inside a leg (the `field-picture-*` and `playthrough-branch-*` checks pin
the ones found so far; the end's fight has four open samples, above), and sound (the `sound-*` family).

## The pictures inside legs 138-160 (2026-11-27, the audit)

One picture every 300 frames of the 23 legs, the port on the cartridge's clock: 4,537 samples, 4,487 at 0 px (five legs
whole: 140, 148, 149, 152 and 157 -- the nine fights, 531 samples). Two rules came out of it, and with them 4,489:

* **Op `85 hh` on the target-list track sets every target's heading** (C1/627A; leg 158 f14564): the hit's shudder lifts
  the three PCs together. `field-picture-party-shudder-heading-is-the-whole-lists`.
* **OBJ priority 0 / 1 is behind the arena's BG1 / BG2 pixels, low-priority tiles too** (mode 1's order; leg 159
  f65310-f65407, the second call of the bits): the two enemies are not on the picture until the discs open.
  `field-picture-core-revive-enemies-behind-bg1`.

What the other samples are, by class:

* **A load's end** (legs 142 f23400, 144 f34200-f35100, 147 f16800-f17700, 156 f38100): the port ends a location load
  some frames before the cartridge (leg 147's entry into 135: the ramp from f16671 against f16705), walks from there on
  the pad's own frames and meets the cartridge's place again at the next stop. The length of an NMI-off load is the
  timing class; the tape holds most loads' ends and not these.
* **A scroll's phase after such a load** (leg 156 f38700, the Epoch's cockpit: the sea under it, 8661 px at every
  offset): the layer moves a step a frame from the load's end, so its phase is the load's.
* **A dissolve's packets** (10-16 px on a sample in legs 138, 139, 141, 143, 146, 147, 150, 151: a type-2 corpse, two
  packets a level) -- FIXED: the cells' tiles are where their OAM names put them, so for a type 2 outside combatant 3
  cells 4-7's bottom halves are in the level's second packet (battle_enemy_cells.md; leg 143 f12598 / f12600: 9 / 10 px
  -> 0). `field-picture-corpse-packets-follow-the-cells-names`.
* **The PPU's line limits in a fight** (legs 139 f2700 / f23100, 145 f35700, 153 f16200: 4-53 px) -- FIXED: a line with
  more than 34 OBJ slivers loses the front entries' (two Synchrites and a smoke over Magus: 36 on lines 50-61, the
  front enemy's first cell gone). The painter lists its entries in a first walk and plots the slivers the rule keeps
  (battle_enemy_cells.md). `field-picture-fight-obj-line-limit-drops-the-front-slivers`.
* **The effect layer's sub-screen backdrop** (leg 150 f6000 / f7500, Spekkio's sweep: 163 / 82 px) -- FIXED: the
  arena's coverage under the view is read at the row's own battle scroll (battle_effect_orbit.md).
  `field-picture-effect-sub-backdrop-follows-the-arena-rows`.
* **An orbit's depth** (leg 150 f27600, a flame round Magus: 13 px) -- FIXED: mover mode 5's matrix byte is the
  compose's on the same picture. `field-picture-orbit-depth-is-the-movers-on-its-own-picture`.
* **The iris's bare rim** (leg 160 f10800: 82 px at port -2; the first audit's legs 11 f3300 and 12 f2100, "the Gate's
  window edge") -- FIXED: inside the window the backdrop is out of the colour math (field_iris_window.md sec. 11).
  `field-picture-iris-backdrop-is-out-of-the-colour-math`.
* **The Tech screen** (legs 141 f50100, 143 f55500, 155 f28800: 16-107 px) -- FIXED, two rules: HDMA channel 2's TM
  record FF/BBDA has no OBJ on lines 1-16, so the caster's cast pose loses its top rows (menu2_tech_screen.md;
  `field-picture-tech-screen-lines-without-obj`), and the caster panel keeps the HP C2/BA2F drew while the heal's number
  rises (`field-picture-tech-caster-panel-keeps-its-drawn-hp`).
* **A felled PC's pose** (leg 154 f24300-f26400: 17-531 px) -- FIXED: the fallen pose D1:5F42 is the action close's
  (C1/414E-41AC, its only readers), not the HP write's (C1/ECDB-ED15 sets the dead flag alone).
  `field-picture-fallen-pose-is-the-action-closes`.
* **Leg 144 f41700** (63 px at port -3, a carpet's edge a colour step apart): the load-end class. The colour page
  `$7E:2200` is the cartridge's on the same frame at every frame of f41690-f41704 (WRAM), and the room has no colour
  math (`--hdma`: CGADSUB 00); the picture's best fit is a frame further than the field's usual -2 because the leg runs
  on from its early load end (f34200-f35100 above), so the palette's 4-frame step is met a frame apart.
* **Leg 151 f13200** (78 px): 0 px on the commit of the rules above (not traced to one of them).
* **Leg 159 f113400-f114300** (the fair after the fall, 102-1003 px at port -2: NPCs on other paths) -- the battle
  exit's timing class (CLAUDE.md, the 2026-11-07 decision). Loc 1DF's object 0 runs `D8 93 E0` (the Core's fight) at
  0x002C, then `48 EC 01 7F 06` (the outcome `$7F01EC` to local 6), an IF, and `CHANGELOC 52` (its pointer counted
  from the script's start, the 0x160-byte function table included: `$1180` 018F = 0x002F). The fight hands back on
  f104692 in the port and on f104693 on the cartridge (the exit's release on the SPC fade lattice); every later
  difference follows from that frame: the fade a frame ahead (`$0118`-`$011D`), loc 052 committed on f104973 for the
  cartridge's f104974, the play clock `$0400` a tick behind from there (the load frame's), C0/0C70's `$01F8` seeded
  from it (43 for 42 on the 050 load), and the fair's NPC rolls with it. Measured with s233_frame_cells_cut (the
  objects' `$1180`, `$0100`-`$011F`, `$0400`, `$01F8` over f0-f113720): before f104692 the two sides differ only on the
  frame a load writes the cursor (f2899, f39937: the cartridge's store a frame before the port's, equal on the next),
  and between the hand-back and the commit only in the fade's countdown.
