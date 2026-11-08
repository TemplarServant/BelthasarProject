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
| 93 ozzie way | DIFFERENT (picture; pinned) | fix build: SYNC-ONLY, 13 warps (fight exits +-1, first windows -4/-5, two control stretches -9/+11 after fights on 1BE), end regions same | **FIXED (port bug, one of its chains)**: at 0A7 f25869 an op 0xDA reset the follower ring in the visit of an object above follower obj 2, whose own B0 had already advanced on the old cursors (C0/A2CE, `$01AD` 48 -> 49, a 1-px step, C0:A31D); the port ran its followers after the runner and lost the step. ct_vm.c g_daObj. **FIXED (port bug, a second chain)**: the equip menu's list slide (C2/A4AF climb, C2/A697 descent; C2/A518 each pass) keeps its offsets in the direct page `$22/$24/$26`, and `$27` is the battle's iteration counter (C1/8121 `INC $0027`): the cartridge's last descent left FF (C2:A6CD, f28546), its next fight counted from 00, the port's from the last fight's 20 -- the enqueue's parity one pass apart, the first window -4 (f29431). field.c menuEqSlideDp; the window now f29431 on both sides. **FIXED (a third chain)**: a staged enemy the epilogue removes (C0/07F3-0809: `$1000,Y |= 80`, `$1A81,Y := 0`, C0/734C the draw list) keeps its mover -- `$1A80/$1A01` and its velocity are the page C0/0905 restored -- so a step in flight when the fight began is taken after it (1BE: obj 19 restored with `$1A27` = 01 stepped at f87861, C0:AA98); the port's CtSceneBattleRemoveObj zeroed the task, the ticket and the velocity, and its enemy stood until its cadence: 2 px and the control hand-back -9 later. Being re-aligned with the three fixes |
| 94 ozzie barrier | GAMEPLAY-SAME | -- | timing |
| 96 laruba | GAMEPLAY-SAME | -- | timing |
| 101 ioka rest | GAMEPLAY-SAME | -- | timing |
| 106 end of time | GAMEPLAY-SAME | -- | timing |
| 115 dalton | GAMEPLAY-SAME | -- | timing |

**Open, in route order** (GAMEPLAY-DIFFERENT, 2026-11-08 triage; 24-92 closed above): 93, 95, 97, 99,
100, 102, 103, 104, 105, 107, 109, 114, 116, 118, 119, 120, 122. (95, 97, 104, 122: the port's side
reads zeros -- its end dump missed, i.e. the port never reached the leg's last frame on a field: to be seen leg by leg.)
