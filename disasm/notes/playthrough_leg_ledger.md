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
| 63 zombor | DIFFERENT (port: game over) | SYNC-ONLY, 1 warp [f587+2], end regions same | **timing**: f587 = a load's fade-in (load length class) |
| 64 to denadoro | GAMEPLAY-SAME | -- | timing |
| 67 masa & mune | GAMEPLAY-SAME | -- | timing |
| 68 porre | GAMEPLAY-SAME | -- | timing |
| 70 bent hilt | DIFFERENT (items: `$2413` 00/51) | SYNC-ONLY, 3 warps [f147+1 f1826+1 f3178+2] (a fade-in, a fight exit, a fade-in), end regions items `$2413` 00/51 | **timing (the leg's cut)**: obj 10 (08D) runs `5A 69 / CA 51` on a 3-frame cadence; the cartridge's visit that adds item 51 is f5618, the frame after the leg's last, and the cartridge holds `$2413` = 51 x1 at 71's end; the port's dump at its warped end (+4) is one visit later. Same op, same slot |
| 72 mystic mountain | GAMEPLAY-SAME | -- | timing |
| 73 ayla | DIFFERENT (story, items, stats) | SYNC-ONLY, 4 warps [f640+1 f933+1 f7744+2 f16105-1], end regions same | **timing**: f640/f933 fade-ins (load length), f7744/f16105 fight exits |
| 76 forest maze | GAMEPLAY-SAME | -- | timing |
| 83 gate home | GAMEPLAY-SAME | -- | timing |
| 84 masamune reforged | GAMEPLAY-SAME | -- | timing |
| 71 melchior | (pinned SAME; with the fixes DIFFERENT on the end picture only) | with the flee fix: SYNC-ONLY, 8 warps, end regions same | **timing** (picture only): the flee fix's check -- fight 2's run-away counter starts f3177 on both sides |
| 86 magic cave | (pinned SAME; with the fixes DIFFERENT on the end picture only, 117 px at -2) | -- | timing (picture only; every gameplay chain equal) |
| 85 frog joins | DIFFERENT (story, stats, party, loc) | main: DIVERGES (f13329 ctrl -9). **With the flee fix**: SYNC-ONLY, 18 warps, end regions same | **FIXED (port bug)**: the fled fight's escape came 6 frames early -- the port counted the run-away counter (CF/E706) before the bank-CF frame-service pass starts (the band's arm, as CF/E5AD / CF/E73E): `$99CF` ran ahead, the command window closed at f5077 against f5083, the exit 7 early, the clock 7 behind, `$01F8` 22 against 28 at 08C, NPC 12's wander another path. battle.c `cfPass` gate. Remaining warps: fight exits +-2, the NPC meet (f13329, rolls -> decision 2) |
| 87 magus lair | GAMEPLAY-SAME | -- | timing |
| 91 flea | DIFFERENT (gold, hp, picture) | flee build: SYNC-ONLY, 11 warps [f285+1, then ten inside Flea's fight, -1..+3, each on an action's end], end regions same | **FIXED (port bug)**: the idle loop's frame B runs C1/3714 AFTER its menu pass (C1/10C4 INC $E5, C1/10C6-10DA, C1/10DC JSR C1/3714); the port ran it before. At the command confirm (cart f30969: `$99E0` 40 -> 00, `$B18B` 03 -> 02) the cartridge's mode machine stepped Flea's cadence `$9897` (02 -> 01), the port's still saw the Wait submenu; after the action her walk segment (`$98A7`) armed a frame late, ended a step short ((165,150) against (167,152)), and every later Flea action slid from 2 px off: SLIDE n=28 against 27, the action 2 frames longer, the ATB behind it. battle.c `mvDefer` (frame B's C1/3714 after the menu pass). Verified: `$9897`, the walk and her f35175 foot (136,121) equal |
| 92 slash | DIFFERENT (gold, hp, picture) | flee build: SYNC-ONLY, 8 warps [f1295+1 f4680-1 f30849+1 f32185-1 f102884+1 f108153-1 f108519-1 f109560+1], end regions same | **timing**: f1295/f30849/f32185/f108153/f108519 load fade-ins (`$0119` 00 -> 01); f4680/f102884 fight exits (`$95D5` -> 80); f109560 Slash's fight's first window (`$95D5` FF from f109472 -> 00: the boot's upload pace) |
| 88 porre shopping | GAMEPLAY-SAME | -- | timing |
| 93 ozzie way | DIFFERENT (picture; pinned) | with the three fixes: SYNC-ONLY, 6 warps [f51737+1 f60844+1 f91416-1 f93707-1 f97134-2 f100223-1] -- every one a fight's exit (`$95D5` -> 80), end regions same | **FIXED x3, then timing**: (1) 0A7 f25869, an op 0xDA reset the follower ring after follower obj 2's own B0 had advanced on the old cursors (ct_vm.c g_daObj, 52402e0); (2) the equip menu's list slide left `$22-$27` in the direct page and `$27` is the battle's iteration counter, so Crono's first box came a menu pass early (field.c menuEqSlideDp, a696d14); (3) a removed battle enemy keeps its step in flight -- C0/07F3 clears no mover -- obj 19 on 1BE stepped at f87861 (CtSceneBattleRemoveObj, 2c60584). What is left is the battle-exit fade lattice |
| 94 ozzie barrier | GAMEPLAY-SAME | -- | timing |
| 95 magus | DIFFERENT (port: end dump missed) | with the fix: SYNC-ONLY, 3 warps [f1270+1 f83984-2 f85154-1], end regions same | **FIXED (port bug)**: 1BF's opening walk -- the lead's B0 pushes the ring before a ten-frame song wait and the step comes after it; the port gated the push on the lead having moved and cleared `$1D32` during the wait, so the ring lost an entry and both followers ran a frame behind (the opening's -4/-2/+2 warps). The push's gate decoded (C0/9E29) and `$1D32` kept while parked (party_follower_ring.md, 2026-11-08). Left, timing: f83984-2 is the field menu's opening (its stash, an NMI-off stretch: `$95D5`/`$A013` are the menu's scratch there, written with `$0D9A`/`$0D13` the frame it opens, 18 frames after the key on the cartridge, 16 on the port), f1270 a fight's start and f85154 a fade-in -- the load-length class |
| 96 laruba | GAMEPLAY-SAME | -- | timing |
| 101 ioka rest | GAMEPLAY-SAME | -- | timing |
| 106 end of time | GAMEPLAY-SAME | -- | timing |
| 113 algetty shop | (pinned SAME; DIFFERENT on the end picture since bc6478c) | ring fix build: the five fights' acks early by 12/10/7/4/16 (f8021/14584/21553/23546/38007 against f8009/14574/21546/23542/37991); every victory (`$A0FE`) within 3 and fight 1's three result lines on the cartridge's frames (f7564/7758/7952) | **timing (user decision 2026-11-07, the battle exit follows the SPC fade)**: the whole gap is the exit's fade wait -- the cartridge posts the fade at f7975 and CD/0432 polls the driver's fade count for 36 frames before C0/1BE6's `11 45 B0` (f8011); the port's exit leaves 16 frames after its post (battle f7450 -> 7466). The fade's tick lattice is the decided class; the size here (up to 16, always early) is noted for a look at the driver's fade under this song. Before bc6478c the same acks were early too: the old pin's SAME was the end picture coinciding (its ring was an entry behind for 1706 frames) |
| 115 dalton | GAMEPLAY-SAME | -- | timing |

**Open, in route order** (GAMEPLAY-DIFFERENT, 2026-11-08 triage; 24-93 closed above): 97, 99,
100, 102, 103, 104, 105, 107, 109, 114, 116, 118, 119, 120, 122. (95, 97, 104, 122: the port's side
reads zeros -- its end dump missed, i.e. the port never reached the leg's last frame on a field: to be seen leg by leg.)
