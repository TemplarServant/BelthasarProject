#!/usr/bin/env python3
r"""A window of a playthrough leg, the port against the cartridge step by step.

    python tools/leg_window_cmp.py [name ...] [-v]

The playthrough's legs (tools/playthrough.py) grade a handful of bytes that tell whether the story went the same way. A
window grades a mechanism inside a leg: it starts both sides from the cartridge's own state at a frame of the leg (made
here from the leg's start state and its pad, cached under build/_legwin/ with the route's key), plays the leg's pad from
there, and compares the named cells:

    legwindow: <name> leg <leg> f<start>+<frames>: <n> steps compared, <d> differing
      first: step <k> cartridge f<c> <cells> x<len>, port f<p> <cells> x<len>
      free:  cartridge f<a>-f<b>: both come back on <cells>, the port <+/-d> frame(s)

A STEP is a run of frames on which the cells do not change (their values and the run's length). While an object moves,
every frame is a step, so the comparison is frame for frame: the port's k-th step against the cartridge's k-th, value
and length. A window that crosses a fight or a load names the cartridge's frames of it (`free`, up to the frame before
the cells move again): nothing is graded inside them, and past them the port's steps are taken from its first one that
holds the cartridge's first value there -- how long a fight's exit or a load takes is the timing class the playthrough's
ledger keeps apart (disasm/notes/playthrough_leg_ledger.md, and the `free:` line says by how much); what is graded is
what the objects do before it and after it. The port's keys: the cartridge's one frame later, as in the playthrough,
the window's first frame taking the leg's own frame before it (port_keys).

Regression checks: the `leg-window-*` family (docs/TESTING.md).
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.path.join(ROOT, 'build', '_legwin')                 # the cartridge's states of the windows (cached)
SCRATCH = os.environ.get('LEGWIN_TMP') or WORK                # LEGWIN_TMP: a second run's own scratch
P.TMP = SCRATCH                                               # (oracle_play's end.raw: not the playthrough's own)
TAIL = 240                                                    # a window with a free span: the port's extra frames

# name: the leg (its prefix in tools/playthrough/route.txt), the window's first frame and its length in the leg's frames,
# the cells (cartridge address, bytes), and the cartridge's frames (of the leg) that are not graded (`free`).
WINDOWS = {
    # Ocean Palace, loc 199 (scene 409): the launcher (object 23) calls objects 11 and 12 (`02 16 14`, `02 18 14`, on a
    # draw of $01F8 every fourth frame), which SETCOORD to tile (04, 48) and fly at the party's tile with `97 11 12`
    # (C0/50B9). A fight starts while they fly: they cross the map's left edge on the ticket they had ($1816 0x002A ->
    # 0xFFFD at f35337) and stop at $1816 = FF2B / $1818 = FF67 (f35351). After the fight (f39422) the op issues again
    # from the tile BYTE the ROM reads ($1801,X = FF): toward tile 03 the heading is 03 - FF, a borrow, so both fly LEFT
    # across the whole 256-tile coordinate (about 1190 frames) and arrive at f40607 / f40611. The cells: each object's
    # event PC ($1180 + 2n: the caller's op 02 writes it, C0/6027) and its position words ($1800 / $1880 + 2n).
    'hazard-wrap': dict(leg='118_', start=35190, frames=5510, free=[(35351, 39421)],
                        cells=[('7E1196', 2), ('7E1816', 2), ('7E1896', 2), ('7E1198', 2), ('7E1818', 2), ('7E1898', 2)]),
    # Ocean Palace, loc 19B (the elevator room): the menu's Form screen puts Lucca in Ayla's seat ($2980-82 00 01 05 ->
    # 00 01 02) and the menu returns at f1524. The return's reconcile C0/1A03 respawns the characters' objects and
    # C0/1A6F-1A97 stores, by slot, the three words the open stashed ($7F1D09,X / $7F1D0F,X / $7F1D15,X) into the object
    # the table $0197 names NOW: Lucca's object 3 takes slot 2's position whole, $1806 = 0800 and $1886 = 0DFF (the
    # 1/16 px nibble with it), and its priority pair 3030. Then the party walks to the elevator (f1636-f1720, the two
    # followers on the ring from f1660) and to its fight's places (f1836-). The cells: the slot table, the party
    # shadow $0194-96, and objects 1-3's position words and priority pairs. The menu is not graded, to f1524, the frame
    # the cartridge's cells come back (the port's page shows them from f1506, the menu's last frames): the still step
    # from there to the walk is cut by it, and what it holds is in every step of the walk -- object 3 stands on the
    # restored words until its turn on the ring.
    'menu-return': dict(leg='119_', start=20, frames=1880, free=[(36, 1524)],
                        cells=[('7E0197', 6), ('7E0194', 3), ('7E1802', 6), ('7E1882', 6), ('7E0C02', 6)]),
    # Ocean Palace, loc 1A0: the fight with a Barghest (59) and three Thrashers (6D), Crono / Marle / Lucca, from the field
    # before it (f16200) through its first 5800 frames. The cells are the PC pose machine's: the window cursors $968C-8E
    # (C1/2EA9), the visits' deferral flags $9922-24 (C1/2E18, cleared by a publish C1/1C5A), the re-aim latch $A098
    # (C1/2F22) and the facings $96DB-DD. Three things the cartridge does in it:
    #   f20016  seat 2 re-aims at enemy seat 6 on the frame the NMI's stepper moved its foot (9B,97) -> (9C,96): the facing
    #           of the new foot, 02 (C1/2F54; the stepper CF/F978 runs before the walk dispatch C1/10A9)
    #   f20063  Lucca's shot at seat 3 opens: C1/49FF turns her to her target, $96DB+2 00 -> 02 (C1/4A3D), and her next
    #           re-aim (f20267) finds another facing, publishes, and defers seat 0's visit ($9922 01 00 00)
    #   f21445  the close of enemy tech 67 (+1E bit 2 on the three PCs): C1/305C runs the scan C1/2F97 whole, the new
    #           icon restarts the three windows ($968C FF FF FF) and the next pass starts them at entry 0 (f22045)
    # Not graded: the fight's boot to its first window (the port's boot is 13 frames shorter: the first-window class) and
    # the close's scan frame itself (the port's close service runs on the frame $A013 falls, three frames after C1/4127).
    # Leg 29 (the fight from f778): the ball's hit on Lucca, who has 1 HP. The close's walk of the victims finds a PC
    # (C1/4138), C1/41BE flags the seat the shown damage fells (`$A11A`) on the wipe's frame, and after C1/414B's wait
    # C1/416D-418B publishes her fallen pose D1:5F42[char * 4 + facing] through C1/1C4A: `$9908` 40 on f1656, two rows
    # before C1/ED12 writes her HP (f1658, with `STZ $A013`). The port published the pose with the HP write.
    # The cells: her published pose, her HP, the action's freeze. Not graded: the fight's boot.
    'doomed-pc-pose': dict(leg='29_', start=700, frames=1000, free=[(701, 940)],
                           cells=[('7E9908', 1), ('7E5EB0', 2), ('7EA013', 1)]),
    # Leg 51's fight (the factory; op D8 on f581): the two copies of the base palette page onto the live one. The field's
    # staging makes the first on f584 (C0/0409: `$229E` back to the base's B607, C0:0416), the boot's stager the second
    # on f622 (CC/E6EC, with the boot's own colours: `$2010`, `$235A`, `$237A`); between them the room's colour record
    # steps `$229E` every four frames, and writes its 4701 on f622 under the second copy. (Not in the cells: the boot's
    # other WRAM -- the port makes its clears and stores at the entry, the cartridge after the upload; `$999F` reads
    # 00 from f583 in the port and 80 to f621 on the cartridge.)
    'fight-boot-palette-copies-land-on-their-frames': dict(leg='51_', start=570, frames=60,
                                                             cells=[('7E229E', 2), ('7E2010', 8), ('7E235A', 6), ('7E237A', 6)]),
    # Leg 45, the load into loc 0D9 (f49-f50): the enemy loader's slot store is a word (C0/47AD REP #20 .. 47C6 STA $0D00,X),
    # so objects 18 and 19's attribute halves `$0D25` / `$0D27` lose the last room's 80 / 7E (on the cartridge's f81, inside
    # the load's black frames; the port runs the loaders at the commit, f50: those frames are a free span).
    'enemy-slot-store-is-a-word': dict(leg='45_', start=40, frames=100, free=[(41, 79)], cells=[('7E0D24', 4)]),
    # The overworld, world 1F3: A on an entrance. The lead's input frame stores `$027C` = 3 itself (C2/35EC), so the
    # followers -- later blocks of the same pass -- find it and stand (C2/3B1B): their Y stops on the frame before
    # (`$0D88` / `$0DC8` = 02B6 / 02A6 from f592) and their +28 loses the walk's bit on f593.
    'overworld-exit-stops-the-followers': dict(leg='76_', start=560, frames=45,
                                               cells=[('7E027C', 1), ('7E0D88', 2), ('7E0DC8', 2), ('7E0D98', 1), ('7E0DD8', 1)]),
    # The overworld, loc 1F1, Down held: an event's script hands the walk back (`$0280` 01 -> 00 on f23) and on the next
    # frame the lead's handler re-inits (C2/3419: `$0280` against `$0281`) and falls into its input frame -- the first
    # pixel of the walk is that frame's (`$0285` 0250 -> 0251 on f24). The followers' own init (C2/3BC2) takes their spot
    # from `$0283` / `$0285` on the same frame, after the lead's step: they stand at 0251 until their walk begins.
    'overworld-walk-starts-on-the-events-last-frame': dict(leg='68_', start=1, frames=60,
                                                           cells=[('7E0285', 2), ('7E0E48', 2), ('7E0E88', 2)]),
    # The Telepod's room, loc 008: the script's `EA 1F` parks the main loop inside the runner for 40 frames (f1569-f1609)
    # while BG3 drifts (5 / 16 and 6 / 16 of a pixel a frame). The iteration's camera leg ran before the runner, and on
    # the return frame the iteration goes on from the visit: the drift's remainders `$1D3F` / `$1D40` hold 0C 0C from
    # f1569 to f1609 and step on f1610. The cells: the remainders and BG3's two scroll counters.
    'drift-holds-across-a-song-load': dict(leg='11_', start=1560, frames=70,
                                           cells=[('7E1D3F', 2), ('7E1D8F', 2), ('7E1D91', 2)]),
    # The prison's fight, loc 048: the staging's records of the two HIDDEN enemies, slots 2 and 3 (`$29D7`, `$29E3`, 12
    # bytes each; C0/0557). +2 / +3 are the on-screen pixel C0/AB45 last stored (`$0A00,Y` / `$0A80,Y` & 0F: 8 / 3 and
    # 8 / F for two objects on the same spot), +4 the drain's frame byte `$0F01,Y` -- FF, nothing was ever staged for a
    # hidden object -- and +8 the palette C0/72B4 gave it (`$0F81,Y` >> 1 = 01). The fight opens a frame apart on the
    # two sides (the fight's lag), so the span before the staged frame is free: the step compared is the records'.
    'hidden-enemy-staged-record': dict(leg='34_', start=15400, frames=90, free=[(15401, 15475)],
                                       cells=[('7E29D7', 8), ('7E29DF', 4), ('7E29E3', 8), ('7E29EB', 4)]),
    # Leene's Bell, loc 1B7: Marle (object 3) walks to the stall, `96 19 07`. On the target's tile the op runs the snap
    # C0/305D each visit -- it publishes its own velocity (`$1906` / `$1986`: 00 1F the walk's, 10 10 from f1185, 00 10 from
    # f1193) -- and the selector C0/5614 on it: 10 + 10 is the run (`$1686` = 06, phase 1 then 2; `$0F07` 21, 9A), 00 + 10
    # the walk again (01; 0B). The cells: the pair, the animation and its phase, the committed frame.
    'tile-snap-anim': dict(leg='08_', start=1150, frames=100,
                           cells=[('7E1906', 1), ('7E1986', 1), ('7E1686', 2), ('7E0F07', 1)]),
    # Johnny's race WON (the branch leg 47b). The player's finish (`$53` == 8 first: $7E:4788) puts the run-out's step
    # $7E:4677 in the race step's slot and the wait $7E:4755 in the steering's, and asks for the fanfare (`$038A` = 1B).
    # `$0388` stays set the twenty frames its post takes (C3/09A4's JSL, inside the NMI: f2971-f2990), the lane and `$B2`
    # standing meanwhile; then `$08` = 0, page A at $41A6 and the camera's $416D (f2992), the player brought to the lane
    # $4000, `$B2` 1E -> 0, `$B0` = 2, the script's sweep and the END (f4129). The cells: the result with its points, the
    # post's flag, the frame counter `$08` ($7E:4755 clears it), the run-out's state, the player's lane, both positions,
    # page A's IP and the camera's three registers.
    # Not graded: the way in (10F's load and the stage's setup, to the race clock's first tick at f602).
    'race-win-run-out': dict(leg='47b_', start=296, frames=3832, free=[(297, 601)],
                             cells=[('7E0380', 4), ('7E0388', 1), ('7E0008', 1), ('7E00B0', 1), ('7E00B2', 1), ('7E004E', 2),
                                    ('7E0051', 4), ('7E0076', 4), ('7E0A20', 2), ('7E0A00', 2), ('7E0A04', 2),
                                    ('7E0A0C', 2)]),
    # The ATB's handler C1/8461 while a PC's turn is pending (`$B188` bit 7): `$B186` bit 0 stays set (C1/860D), so the
    # WHOLE handler runs on every odd pass, and its entry check C1/847E reads the combatant at the cursor - 1 again
    # (`$AEC7 - 1` once the cursor pass has walked off the end). Leg 51's second fight (loc 0E5, f5190-): enemy 6 sits
    # ready behind the arrival gate C1/84B8; its walk's C1/3833 sets `$9826,X` on f7072 and the next odd pass, f7074,
    # gives it its turn (`$982C` = 00 by C1/84C0, `$986F` = 06 by C1/A712) -- the port gave it when the cursor came
    # round, f7082, and every position after was eight frames behind. The cells: the enemy's arrival latch and mode
    # request, its move target and angle, and three position words. Not graded: the way into the fight (to f5235).
    'enemy-turn-behind-the-arrival-gate': dict(leg='51_', start=5100, frames=2320, free=[(5101, 5235)],
                                               cells=[('7E982C', 1), ('7E986F', 1), ('7E974D', 1), ('7E9789', 1),
                                                      ('7E9760', 2), ('7E9776', 2), ('7E98B2', 2)]),
    # The touch latch `$01EB` is not consumed by the touch and is rewritten only by C0/881E's head (`LDA #80 / STA
    # $01EB`). The End of Time's pillar of light, obj24 of loc 1D0 (leg 55): its fn2 `E5`s a tile block, C0/1770 arms the
    # camera's align (`$0138` = 1) and for ten frames the main loop runs the align entry C0/885A instead of C0/881E; on
    # the frame C0/88D8 clears `$0138` (f212) the lead's op 0xB0 calls the touch with the hit of f202, fn2 is installed
    # (`$11B0` = 10CA, `$1031` = 00) and the copy's four frames follow, so its E8 runs on f217 with the countdown
    # reloaded and its `00` four frames on. The port's one-shot latch was empty on f212: the call landed on f217 after
    # the queue's build, the `00` ran a frame later and the pillar chimed once more in the cycle. The cells: obj24's
    # script pointer and cadence pair, and `$0138`.
    'touch-latch-stands-through-the-camera-align': dict(leg='55_', start=150, frames=600,
                                                        cells=[('7E11B0', 2), ('7E1030', 2), ('7E0138', 1)]),
    # The End of Time's Gate (loc 1D0, op FF 8x -> the $FD warp): the warp's program takes over on the jump's own frame,
    # and that frame's head NMI was still the field's -- the play clock `$0400` ticks once more (38 -> 39 at the frame
    # FD/EAA6 saves the flags in, `$7F:74B3`); and FD/DB97's setup writes its WRAM on the frame its CPU reaches each
    # store -- the mode-7 map over the scratch `$7F:0000` three frames after the flags' copy. The port stopped the clock a
    # tick short (every later load seeded `$01F8` one short: leg 84's idle blink on another PC, 280 px) and wrote the
    # map in the hand-off's frame (legs 54, 56, 84: the story byte three frames early).
    # Op 0xFF's own fade (C0/3FBB, the Proto Dome's Gate): the visit never returns, so the iteration that began the fade
    # runs nothing after the runner -- no C0/00C7 table set, no `$0153` flip -- and the field IRQ steps the tile animation
    # (`$051D`) on every frame of the fade. The port flipped `$0153` in the op's frame and the tiles stood still: 15 steps
    # behind for the rest of the route (the End of Time's pillar flame, 133 px at leg 54's end).
    # Scene 0AC's `FF 9B` after Magus: C0/21E1's entry 0B (C0/260E) waits on two C0/EC60 and C0/024C's regroup before its
    # `LDA #85 / JML C3:0000` -- three frames of the field's NMI, whose C2/85AE ticks the play clock `$0400`. The port
    # opened the vortex on the first and stopped the clock there: every later load seeded `$01F8` three short. (`$010F` = 80
    # at C0/263D and `$0384` at C3/0038 come after the waits too.)
    # The Blackbird's deck (leg 124's fight): seats 0/1 are left out (`$AEFF` = FF) and only seat 2 fights. Their ATB
    # keeps C1/FBDD's FF (the normaliser FD/B8F8 tests `$AEFF,X`), and an enemy's resolver 06 (the nearest PC, C1/A4AF)
    # whose winner is unresolved names no target -- the enemy walks home 8 frames after its attack (C1/374D `$9753` = FF).
    # The port loaded the absent seats' ATB and fell back on the lead PC: the enemy stood 64 frames and the swing came early.
    'left-out-seats-are-no-targets': dict(leg='124_', start=27100, frames=800, free=[(27101, 27280)],
                                         cells=[('7EAFAB', 3), ('7E9753', 1), ('7EAECB', 1), ('7E1D10', 1)]),
    'vortex-opens-after-three-field-frames': dict(leg='95_', start=83960, frames=40, cells=[('7E0400', 1), ('7E0384', 1), ('7E010F', 1)]),
    'op-ff-fade-keeps-the-table-set': dict(leg='54_', start=30800, frames=60,
                                           cells=[('7E0153', 1), ('7E051D', 1), ('7E0119', 1)]),
    'gate-warp-takes-over-on-the-fields-nmi': dict(leg='56_', start=1650, frames=120,
                                                    cells=[('7E0400', 1), ('7F0000', 1), ('7F74B3', 1)]),
    'fight-poses': dict(leg='119_', start=16200, frames=5900, free=[(16201, 16420), (21440, 21449)],
                        cells=[('7E968C', 3), ('7E9922', 3), ('7EA098', 1), ('7E96DB', 3)]),
    # The 2300 A.D. map, world 1F2: the Epoch sets down (`$0294` = F3, the BG3 visit skipped) with the lead's velocity
    # standing in `$1BF1` / `$1BF3` (0001 / 0001), and the first visit of the BG3 drift after it (C2/7702: cartridge
    # f4879) takes them: `$24` / `$2A` = FFFD / 0003, the 16.16 step plus (1, 1), and both words cleared (C2/7737-7754).
    # The port's visit read a camera copy that was 0 and left the WRAM words at 1: the dust a pixel off for the stay.
    # The cells: BG3's scroll words and the two pending deltas. Not graded: the descent's length (the world load's
    # class -- the port's map is up three frames earlier).
    'world-entry-bg3': dict(leg='131_', start=4860, frames=100, free=[(4861, 4878)],
                            cells=[('7E0024', 2), ('7E002A', 2), ('7E1BF1', 2), ('7E1BF3', 2)]),
    # The 600 A.D. map after the Masa & Mune fight (scene 097's `E0 F1 03 35 3F`, the world's armed event cell): the
    # bootstrap's first pass allocates the lead ($0DF0, class $3404) and the two followers ($0E30 / $0E70, class $3AE2)
    # with op 35, and the same pass visits them -- the lead's init C2/34F8 (`INC $0002,X`, the arrival cell's `TSB
    # $0282 #03`), the pass's `INC $0003,X`, and the scanner's `$0280 = 1` after them. The cartridge's first frame with
    # the blocks (f36956) has +02 / +03 = 01 01 on all three, their own op's address at +07 ($0482 / $0488 / $048D,
    # C2/048A's copy of the pc the dispatcher stores after every advance) and `$0280` = 01. The cells: `$0280` / `$0281`
    # and each block's first nine bytes (class, phase, the visit counter, the return and the pc words). The load is not
    # graded (to f36955: the port clears the blocks the cartridge leaves stale).
    'map-entry-event-spawn-pass': dict(leg='67_', start=36860, frames=140, free=[(36861, 36955)],
                                       cells=[('7E0280', 2), ('7E0DF0', 9), ('7E0E30', 9), ('7E0E70', 9)]),
    # The 600 A.D. map opening for free roam (leg 68, out of Porre's office): the world's script stores `$027C = 1`
    # (`13 7C 02 01`) at f1516, and the lead's block -- visited later in that same pass -- already takes its input
    # frame there: no button, so the idle arm's `INC $02B1` (C2/35BD) gives 01 on that very frame; the step the pad
    # asks for at f1517 moves `$0283` on f1517 and the phase byte to 02. Before the opening the handler only ticks
    # (C2/356C `BNE ->35E0`): `$02B1` stands at 00. The cells: `$027C`, the lead's place `$0283`-`$0286`, the idle
    # counter `$02B1`, and its block's class, phase and visit counter.
    'map-opening-input-frame': dict(leg='68_', start=1500, frames=60,
                                    cells=[('7E027C', 1), ('7E0283', 4), ('7E02B1', 1), ('7E0DF0', 5)]),
    # Mt. Woe, loc 188: a fight the field's music plays through (`$2A1F` bit 6) and that carries `$2A21` bit 7. The
    # exit's head CD/03B4 `LDA $2A21 / BPL / LDA $2A1F / AND #BF / STA $2A1F` gives the bit up, so the exit posts
    # CD/039E's fade (`81 10 00`, cartridge f7976), waits its fifteen frames and polls the driver's fade count
    # (CD/042B-0436) to f8011 before C0/1BE6 brings the room's song back. The port did not have the head: no fade, no
    # wait, control back 12 frames early. The cell: the player's control `$011F` -- 01 before the encounter (f303) and
    # from f8071 on; the fight between is not graded, and the `free:` line is the exit's length (what is left is the
    # fade's tick lattice, the class of disasm/notes/battle_exit_fade_poll.md).
    'fight-exit-gives-up-the-kept-music': dict(leg='113_', start=250, frames=7900, free=[(303, 8070)],
                                               cells=[('7E011F', 1)]),
    # Crono's kitchen, loc 001: the cats chase the lead (`B6 00`, C0/5410 -> 5228) on a period of 17 frames, and a hidden
    # one (object 10) stands at tile (FF, FF) with a period of 4. Each visit issues a step through C0/ACFD, whose
    # landing test reads the mover plane at the tile bytes UNDER THE MAP'S MASKS (AD95-AD9E `LDA $E0 / REP #30 / AND
    # $1D1E / TAX`, `LDA $7E7040,X / BMI`): (FF, FF) is the 64x64 map's solid corner (3F, 3F), so the hidden cat walls on
    # every visit, draws a random angle (AE27 `INC $F8 / LDA $FE00,X`) and stalls -- `$01F8` steps every fourth frame, and
    # the visible cat's own wall bounces take later entries of the noise table: it steps left at f37 and on from there.
    # The port tested the unmasked cell (free), the hidden cat flew off and the cursor fell behind (1E for the
    # cartridge's 29 at f37): the cat stood where the cartridge's was not, and an A press 700 frames on made it meow.
    # The cells: the cursor, and objects 9 and 10's position words.
    'offmap-mover-walls-under-the-masks': dict(leg='03_', start=2, frames=236,
                                               cells=[('7E01F8', 1), ('7E1812', 2), ('7E1892', 2), ('7E1814', 2), ('7E1894', 2)]),
    # Reptite Lair, loc 11C (the holes): the controller object 13 idles on `AD 20` (period 4) until a hole calls it
    # (f667), then drops the party one by one -- `06 00 13 / AD 06 / 06 02 13 / AD 06 / 06 04 13 / AD 06` -- and leaves
    # the room (`E0`). The wait's counter `$7F0A01,X` is the object's: the call found it past 6, and C0/2F96's third arm
    # (2FA6 `LDA #00 / STA $7F0A01,X / TYX / SEC`: the count above N) zeroes it and runs the op again, so the first
    # `AD 06` lasts its six visits like the other two (f671-f699, f699-f723, f723-f747). The port ended a wait whose
    # count was at or above N: the first one took one visit and the room's exit came 24 frames early. The cells: the
    # controller's event PC and the three characters' (objects 1, 6 and 2 run the fall, function 3).
    'wait-starts-over-above-its-count': dict(leg='78_', start=660, frames=86,
                                             cells=[('7E119A', 2), ('7E1182', 2), ('7E118C', 2), ('7E1184', 2)]),
    # Ocean Palace, loc 196: the leg's own first frames are INSIDE the load's fade-in -- the cartridge's CPU stands in
    # C0/2848's loop (`INC $19 / .. JSR C0/00BF / JSR C0/EC60 / LDA $19 / CMP #0F / BMI`), `$0119` 06 at the window's
    # start, with the input dead and the scheduler under C0/00BF until the $0F iteration (f10). No WRAM byte says the
    # CPU is in the loop, so the seed's maker asks the cartridge (playthrough.py seed_in_load_fade: the first frame
    # writes `$0119` at C0:284F) and the port goes on with the ramp (CT_SEED_LOADFADE, field.c). The walk then meets the
    # room's encounter on the cartridge's frame (control off at f164). The port's main loop used to run from the
    # seed's first frame at the seed's brightness: the lead walked eleven frames ahead, the encounter came twelve
    # early (f152), and the fight under the leg's pad went another way (every sample of the picture audit). The cells:
    # the brightness, the control byte and the lead's position words.
    'seed-inside-the-load-fade': dict(leg='121_', start=1, frames=170,
                                      cells=[('7E0119', 1), ('7E011F', 1), ('7E1802', 2), ('7E1882', 2)]),
    # Magus's castle, loc 1BF, the Vamps' fight of leg 95: the field's tile animation across a fight. The line-211 IRQ
    # C0/ECCC steps FD/E022 (`INC $051D`) on a frame whose `$0153` bit 0 is set; the fight's IRQ D1/FE97 steps it on an odd
    # `$7C`. The frame the fight is called in (f641) is the field's whole -- its IRQ steps the tiles (11 -> 12, the bit
    # set) -- and the return's five waits flip the bit before line 211 under the infinitely fast CPU, so f1315 / f1317 /
    # f1319 step. The port ran neither the transfer frame's step (the battle's branch had the frame) nor the return's
    # first (the IRQ read the bit before the flip, the CPU's time against the beam): two runs short after every fight,
    # the room's torches four frames behind (98 px each on most frames of the leg). The cells: the run counter and the bit.
    'tile-animation-across-a-fight': dict(leg='95_', start=600, frames=740, cells=[('7E051D', 1), ('7E0153', 1)]),
    # The same leg's door out of the room (f1751 the fade, f1766 the commit, f1791 the new room's ramp): a location load
    # runs with the NMI and the IRQ off (C0/00F4), and the IRQ is the NMI's to arm (C0/EA70), so no tile step falls on
    # the commit's frame, on the load's, or on the first frame after it. `$051D` 3A from f1765 to f1791, 3B on f1792.
    # The port stepped on the commit's frame and on the frame before the ramp: the new room's tiles two runs ahead.
    # (The run counter alone: the bit's own flip of the commit's frame is the load's, and the port loads one iteration
    # after the commit -- everything the load writes stands a frame later through the black.)
    'tile-animation-across-a-load': dict(leg='95_', start=1740, frames=70, cells=[('7E051D', 1)]),
    # Leg 113's fight (the Bantam Imps and the Gargoyle), the strip at the bottom from the boot: an item's select opens
    # on a party member low on the screen (f2427: Ayla, the hand's OAM row `$0701` = 9F) and the hand routine's single
    # arm moves the windows out of its way -- C1/1A53-1A76: `$A4EE` (the hand was placed, counted by the open C1/14D2) and
    # `$960A` (a list one hand walks, the handler's C1/2042) set, the strip at the bottom and the row at 9C or below ->
    # `STZ $EC`.
    # The NMI takes it (CD/093A: `$CA21` f2428) and the preset follows (`$CA22` f2429): the strip is at the top for the
    # rest of the fight. The port wrote `$EC` at the boot alone: its strip stayed over the enemy, 24,577 px of every
    # frame from there on. The cells: the side byte, the layout's two, the one-hand flag, the hand's move count (00 on
    # every row: counted and cleared inside one pass).
    # (The fight's boot is not graded: the port stages the layout two frames ahead of the cartridge there, the ledger's.)
    'windows-side-follows-the-hand': dict(leg='113_', start=400, frames=2100, free=[(401, 600)],
                                          cells=[('7E00EC', 1), ('7ECA21', 2), ('7E960A', 1), ('7EA4EE', 1)]),
}


def cart_state(legs, i, start):
    """The cartridge's state at frame `start` of leg i, from the leg's start state and its pad (cached with the key of
    what made it: every pad from power-on through the leg, the oracle, the frame)."""
    lg = legs[i]
    path = os.path.join(WORK, 'leg%s_f%d.state' % (lg['name'].split('_')[0], start))
    key = hashlib.sha1(('%s|%d' % (P.key_of(legs, i), start)).encode()).hexdigest()
    if os.path.exists(path) and os.path.exists(path + '.key') and open(path + '.key').read().strip() == key:
        return path
    if os.path.exists(path):
        os.remove(path)
    if not P.oracle_play(P.start_of(legs, i), cut(lg['pad'], 0, start), path):
        sys.exit('legwindow: the cartridge did not make %s' % path)
    open(path + '.key', 'w').write(key)
    return path


def cut(pad, lo, n):
    """Frames lo..lo+n-1 of a pad, as a pad."""
    out, c = [], 0
    for b, k in pad:
        a, e = max(c, lo), min(c + k, lo + n)
        if e > a:
            out.append((b, e - a))
        c += k
    return out


def port_key(a):
    return ('1' if a[:2] == '7F' else '') + a[2:]


def port_keys(prev, pad):
    """The port's keys are the cartridge's one frame later (tools/playthrough.py walkscript). A leg's start is a still
    moment, so there the frame before is an idle token; a window starts anywhere, and its frame before is the leg's own
    (a held direction stays held across the window's first frame, as it does on the cartridge)."""
    return ','.join('H%X:%d' % (mk, n) if mk else '.:%d' % n for mk, n in prev + pad)


def port_timeline(state, prev, pad, cells, work):
    raw = os.path.join(work, 'seed.raw')
    subprocess.run([P.CT, '--peek', state, raw, '--btnscript', '0:0'], cwd=ROOT, env=P.RULE, capture_output=True)
    w = open(raw + '.wram', 'rb').read()
    lw = os.path.join(work, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    frames = sum(n for _, n in pad)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=state, CT_WS_BATTLE_LEAD='1', CT_SRAM_FRESH='1', CT_SRAM=os.path.join(work, 'port.srm'),
               CT_FIELD_MEMMON=','.join('%s:%X' % (port_key(a), n) for a, n in cells),
               CT_SHOTS=os.path.join(work, 'p'), CT_SHOTS_AT=str(frames), CT_SHOTS_EXIT='1')
    if P.seed_in_load_fade(state, w):
        env['CT_SEED_LOADFADE'] = '1'                    # (a window cut inside C0/2848's ramp: the port goes on with it)
    if os.environ.get('CT_OAMRENDER', '1') != '0':           # (the OAM renderer, stage 2: the cut state's VRAM; test surface)
        env['CT_SEED_VRAM'] = raw + '.vram'
    r = subprocess.run([P.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw,
                        '--walkscript', port_keys(prev, pad)], cwd=ROOT, env=env, capture_output=True, text=True,
                       errors='replace')
    out = []
    for line in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', line)
        if not m:
            continue
        kv = {k: v.rstrip('*') for k, v in re.findall(r'\b([0-9A-F]+)=([0-9A-F*]+)', m.group(2))}
        if all(port_key(a) in kv for a, _ in cells):
            out.append((int(m.group(1)), tuple(kv[port_key(a)] for a, _ in cells)))
    return out


def steps(events, first, last):
    """Change events [(frame, row)] -> the steps [(frame, row, length)] over frames first..last."""
    out, cur, j = [], None, 0
    for f in range(first, last + 1):
        while j < len(events) and events[j][0] <= f:
            cur = events[j][1]; j += 1
        if cur is None:
            continue
        if out and out[-1][1] == cur:
            out[-1][2] += 1
        else:
            out.append([f, cur, 1])
    return out


def segments(sc, free):
    """The cartridge's steps between the free spans: [[step, ...], ...], whole steps only."""
    segs, lo = [], None
    for a, b in sorted(free) + [(None, None)]:
        segs.append([s for s in sc if (lo is None or s[0] > lo) and (a is None or s[0] + s[2] - 1 < a)])
        lo = b
    return segs


def run(name, d, verbose):
    legs = P.route()
    i = next(k for k, lg in enumerate(legs) if lg['name'].startswith(d['leg']))
    lg = legs[i]
    work = os.path.join(SCRATCH, name)
    os.makedirs(work, exist_ok=True)
    start, n, cells = d['start'], d['frames'], d['cells']
    free = [(a - start, b - start) for a, b in d.get('free', [])]
    tail = TAIL if free else 0                                # the port's frames past the window (it may come back late)
    state = cart_state(legs, i, start)
    mon = [('c%d' % k, a, port_key(a), b) for k, (a, b) in enumerate(cells)]
    cart = [(f, tuple(kv['c%d' % k] for k in range(len(cells))))
            for f, kv in P.oracle_timeline(state, cut(lg['pad'], start, n), mon)]
    port = port_timeline(state, cut(lg['pad'], start - 1, 1) if start else [(0, 1)], cut(lg['pad'], start, n + tail),
                         cells, work)
    if not cart or not port:
        print('legwindow: %s leg %s: no timeline (cartridge %d event(s), port %d)' % (name, lg['name'], len(cart), len(port)))
        return
    f0 = max(cart[0][0], port[0][0])
    sc, sp = steps(cart, f0, n - 1), steps(port, f0, n + tail - 1)
    fmt = lambda s: 'f%d %s x%d' % (s[0] + start, ' '.join(s[1]), s[2])
    segs = segments(sc, free)
    total, bad, first, notes, j = 0, 0, None, [], 0
    for q, seg in enumerate(segs):
        if not seg:
            continue
        if q:                                                 # past a free span: the port's first step with this value
            k = next((k for k in range(j, len(sp)) if sp[k][1] == seg[0][1]), None)
            if k is None:
                total += len(seg); bad += len(seg)
                first = first or '  first: after the free span the port never shows cartridge %s' % fmt(seg[0])
                notes.append('cartridge f%d-f%d: the port does not come back' % (free[q - 1][0] + start, free[q - 1][1] + start))
                break
            notes.append('cartridge f%d-f%d: both come back on %s, the port %+d frame(s)'
                         % (free[q - 1][0] + start, free[q - 1][1] + start, ' '.join(seg[0][1]), sp[k][0] - seg[0][0]))
            j = k
        for x, a in enumerate(seg):
            b = sp[j] if j < len(sp) else None
            cutlast = (q == len(segs) - 1 and x == len(seg) - 1)      # the window's end cuts the cartridge's last step
            same = b is not None and a[1] == b[1] and (a[2] == b[2] or cutlast)
            total += 1
            if not same:
                bad += 1
                first = first or '  first: step %d cartridge %s, port %s' % (total - 1, fmt(a), fmt(b) if b else '(none)')
                if verbose and bad <= 60:
                    print('    step %d | cartridge %s | port %s' % (total - 1, fmt(a), fmt(b) if b else '-'))
            j += 1
    print('legwindow: %s leg %s f%d+%d: %d steps compared, %d differing' % (name, lg['name'].split('_')[0], start, n, total, bad))
    if first:
        print(first)
    for t in notes:
        print('  free:  ' + t)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*', help='the windows (default: all)')
    ap.add_argument('-v', action='store_true', help='every differing or free step')
    a = ap.parse_args()
    for name in a.names or sorted(WINDOWS):
        if name not in WINDOWS:
            sys.exit('legwindow: no window %s (%s)' % (name, ', '.join(sorted(WINDOWS))))
        run(name, WINDOWS[name], a.v)


if __name__ == '__main__':
    main()
