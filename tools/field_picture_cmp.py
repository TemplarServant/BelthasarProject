#!/usr/bin/env python3
r"""A field picture, the port against the cartridge from one cartridge state.

    python tools/field_picture_cmp.py [name ...] [--png]

The playthrough's legs grade their last picture, but a leg ends wherever the route cut it: after a walk, a load or a
fight whose length is the timing class, so a picture that differs there says little about how the room is drawn. This
tool takes the cartridge's state at a leg's START (made from the route by tools/playthrough.py, keyed on the route and
the oracle) -- or at a frame inside the leg (`at`: the leg's pad played that far on the cartridge, cached in the work
folder with the same key) -- leaves the pad alone for a few frames on both sides, and compares the pictures: nothing
moved but what the room itself animates, and both sides began on the same WRAM.

A state cut inside a walk cannot have its pad let go: the port's keys are the cartridge's one frame later, so an idle
pad releases the port's direction a frame early and the walkers stop a pixel apart. Such a picture is `own`: both sides
go on with the LEG'S OWN next frames of pad, and the port's first key is the leg's pad frame before the cut.

    fieldpicture: <name> leg <leg>[ f<at>] +<n> idle frames: <d> px differ at port <lag>
    fieldpicture: <name> leg <leg> f<at> +<n> frames of the leg's pad: <d> px differ at port <lag>

`<lag>` is the port frame that fits best in -6..+6 around the cartridge's (the port's picture P is the cartridge's P+2).
A picture with `lag` is graded at that port frame alone: what it pins is WHEN a thing is drawn, in a scene so still that
another frame of the port would fit (FIELDPIC_LAGS=1 prints every port frame's count).
--png writes build/_fieldpic/<name>.png: the cartridge, the port, the pixels apart.

Regression checks: the `field-picture-*` family (docs/TESTING.md).
"""
import hashlib
import os
import shutil
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.environ.get('FIELDPIC_TMP') or os.path.join(ROOT, 'build', '_fieldpic')
P.TMP = WORK                                                  # (its own scratch: not the playthrough's)
# One run's own files -- the port's seed, key file and shots, the cartridge's picture. The suite runs these checks side
# by side, and two that share them read each other's pictures ("the port drew no picture"). The states cut inside a leg
# stay in WORK: they are cached by their key.
SCRATCH = os.path.join(WORK, 'run%d' % os.getpid())

# name: the leg whose START state seeds both sides (its name in tools/playthrough/route.txt), `at` a frame inside the
# leg instead, and the idle frames.
PICTURES = {
    # Death Peak, loc 0F6 (the start of leg 135): the snow is BG3 with a speed of its own, two pixels up and one left a
    # frame, under the window wave ($1DF9): HDMA channel 3 hands BG3HOFS out a line at a time, +-5 px over 16 lines.
    # TM 17, TS 13, CGWSEL 02, CGADSUB 44, BGMODE 09: the snow's priority-1 pixels are the main screen's top, above the
    # sprites, and are half-added to the sub screen -- the room, and the party and the Poyozos where they stand. Three
    # things make the picture: the sprite as the SUB pixel under a BG pixel that beats it (scene.c BG_OBJ_LVL and
    # CtFieldSceneObjLevels, field.c g_objSubMask), the line's own scroll (field.c: picture row r shows transfer r - 1
    # of channel 3's table), and the wave's phase riding BG3's vertical steps (ct_vm.c: C0/9721 / C0/975F's
    # `INC|DEC $FB` x4 a pixel).
    'death-peak-snow': dict(leg='135_death_peak_0f6', frames=40),
    # The End of Time, loc 1D0 (the start of leg 107): the lead stands in a pillar of light. TM 17, TS 12, the math adds:
    # the pillar is a BG priority-1 pixel over the lead's OBJ, so the main pixel is the pillar's and the sub pixel the
    # lead -- the character shows through the light, where the foreground's re-draw used to cover it. The sky around
    # the platform is BG3 under the same wave as the snow.
    'end-of-time-pillar': dict(leg='107_keepers_dome', frames=40),
    # The Blackbird's wing, loc 1DE (the start of leg 126): the party has just come in and its three members stand on one
    # pixel. The room's LAST object is the third member, so C0/A810 ends on its party arm's own `RTS` (C0/A88C) and never
    # runs the party tail (C0/A846-A867): the draw list keeps the loop's order, the third member in front -- Ayla, where
    # the port's tail put the lead, Marle (ct_vm.c, the tail's `lastCls`).
    'party-stack-order': dict(leg='126_wing_dalton', frames=40),
    # Loc 1A7, the Ocean Palace's disaster (leg 130 f2531, Melchior's box open at the top): TM 13, TS 04, CGWSEL 02,
    # CGADSUB 02, W12SEL 22, TMW 17. Window 1 takes BG1 and BG2 off the main screen inside the disc; what is left is the
    # backdrop, which is not in the math (CGADSUB bit 5 clear), so the disc is solid black -- BG3's plasma, on the sub
    # screen alone, does not show through (scene.c CtComposeScene's `back_en`). Around it the plasma waves under an
    # open text box, whose lines are the box's and take no shift.
    'backdrop-out-of-the-math': dict(leg='130_black_omen_gaspar', at=2531, frames=30),
    # Magus's lair, loc 0A5 (the start of leg 88): TM 17, TS 00, CGADSUB 00 -- no colour math at all. The dead trees'
    # silhouettes are BG3 priority-1 tiles and the bats that hang in them OBJ of priority 3: under BGMODE 09 (the field
    # NMI's C0/EA7D) BG3.1 is above every OBJ, so a bat shows only where a tree lets it. The port drew the sprites over
    # BG3's priority tiles (field.c, the foreground pass's BG3 loop).
    'bg3-above-every-sprite': dict(leg='88_porre_shopping', frames=40),
    # Arris dome, loc 0D6 (the start of leg 39): TM 04, TS 13, CGADSUB 44 -- BG3 alone on the main screen, the room
    # and the sprites on the sub screen, half-added: everything is seen through the dust. A sprite pixel there is the
    # sub pixel of BG3's (mixed by drawSprite), and the re-draw of BG3's priority-1 pixels must leave it -- the first
    # form of that pass covered the party with the dust (415 px here, nine playthrough legs).
    'sprites-on-the-sub-screen-under-bg3': dict(leg='39_arris_shop', frames=40),
    # The Ocean Palace's throne room, loc 19F (leg 122 f61500, the party walking up the carpet): TM 13, TS 00, CGWSEL
    # 02, CGADSUB `$0BE0` = 97 (subtract; BG1, BG2, BG3, OBJ) and op 0xF1's fade walking the fixed colour `$0121`
    # (23..21, then 61..64: C0/1F5A). No sub layer and no colour window, so every BG pixel's operand is the FIXED colour
    # (channel 6's rows, FD/C6E7; the NMI's `E0` then `$0121`, C0/EC3D): the room is its colours less (2, 2, 0) here,
    # the sprites of palettes 0-3 untouched. The port did the fixed colour's math only inside a colour window: 49,965 px
    # (field.c g_colFixMap, fieldColWindowApply).
    'fixed-colour-over-the-map': dict(leg='122_golem_twins_lavos', at=61500, frames=24, own=True),
    # The Golems' room, loc 19E (leg 122, cut at f1400 in the room before it; the load at f1490, then the camera pans
    # up to the room's top by f1640). The controller's function 1, run to its terminator at the load (C0/56C8 ->
    # C0/595C), is `E4 06 00 09 0F 16 00 04 / AD 02 / E4 00 00 03 0F 06 00 04`: the light column's BG3 tiles moved
    # aside, a wait, blank tiles put in their place. C0/595C ignores a handler's carry (`JSR ($5D6E,X) / BRA ->5967`),
    # so the wait is dispatched again and again until its count is 2 and the second copy runs in the same call. The
    # port stopped fn1 at the first op that left the IP on itself: the column stood in the room (1584 px here, about
    # 3000 through the fight under it).
    'load-function-spins-its-waits': dict(leg='122_golem_twins_lavos', at=1400, frames=240, own=True),
    # Zeal, loc 195's second fight of leg 117 (cut at f5100 in the field; the fight boots itself). An action whose anim
    # script holds the effect lock (ops 2D / 2E, `$CFFF`) runs at f7364: D1/F426 parks every colour-slot record whose mode
    # has bit 7 or is 1x (`LDA $0520,X / BMI ->F449 / AND #F0 / CMP #10 / BNE / STZ $0520,X`) and puts the modes back when
    # the lock drops -- the lit floor's cycle (records 8B / 87) holds one picture from f7378 to f7485 and goes on from
    # there. The port parked the 1x records alone: the floor cycled through the lock and stood 15 frames out of step for
    # the rest of the fight and of the room (731 px on most frames). The picture 75 frames after the lock.
    'effect-lock-parks-the-colour-cycles': dict(leg='117_kajar_restock', at=5100, frames=2460, own=True),
    # Magus's castle, loc 1BF, the Vamps' fight of leg 95 (cut at f600 in the field; the fight boots at f641). The room's
    # screens stand through the fight: TM 1D (BG1 BG3 OBJ), TS 19 (BG1 OBJ), the sub screen half-added -- the mist is
    # BG3's priority tiles, above every sprite on the main screen, and a sprite under it is the mist pixel's SUB pixel.
    # The fight's sprites are that OBJ layer too: the lone Vamp and Marle's head are seen through the mist. The port drew
    # its combatants over it (408 px of this frame, at every lag).
    'fight-sprites-through-the-mist': dict(leg='95_magus', at=600, frames=130, own=True),
    # The same fight, run away from (L + R held): 136 frames into the run-off action. The run-off is an action like any
    # other -- its loops call the menu pass C1/10FA -- and C1/820A's teardown has left no chooser (`$95D5` = FF), so the
    # first pass under it repaints the strip without the command menu: the enemy's name alone, no hand, no bracket, from
    # the fourth frame of the run-off to the exit. The port ran no pass after the run-off's dispatch and kept the menu,
    # the hand and the bracket on the screen for the whole run-off (1007 px of this frame, at every lag).
    'run-off-closes-the-command-menu': dict(leg='95_magus', at=600, frames=600, own=True),
    # Magus himself, f10500 of the leg (his fight is called on f9962): an enemy whose sheet is a RAW one. The staging hands
    # an enemy the sheet pointer its field object carried (CC/EAC4-EACF: `$1C60,Y` = `$29C8,X` / `$29CA,X`), and his is
    # the ROM's own address, D2:0000 -- E4:2000[6], the sheet he has as a party member -- where every other enemy's is
    # its decompressed sheet in bank $7F. The tile builder reads through that pointer wherever it points. The port
    # LZ-decompressed the address by the enemy's id and drew nothing: no Magus on the screen for the whole fight (544 px
    # of this frame, at every lag).
    'magus-is-drawn-from-his-raw-sheet': dict(leg='95_magus', at=600, frames=9900, own=True),
    # The same fight at f15000: Magus's ground shadow. His script's D0 sets the flag `$A5DB` on row f14190 and nothing
    # clears it; CF/EA8A lays the shadow as the last OAM entry of his own entry (cells 16-19, the shadow 20: tile 002
    # at (112,107), attribute 32) -- an OBJ of the fight's priority (`$A5E3` 30: 3), in front of the floor's BG
    # priority tiles. The port drew the shadows unmarked and its foreground pass painted the floor over this one
    # (42 px on every picture of the fight from f14190 on).
    'shadow-over-the-priority-floor': dict(leg='95_magus', at=600, frames=14400, own=True),
    # Flea's fight, loc 0AD (playthrough leg 92 from its start; the room is entered on f30824), f37200:
    # the starfield is BG2 drifting under the fight -- the field's C0/2C41, which the battle's NMI calls AFTER it has
    # filled the scroll records from the same words (D1/E7E3 after CD/0929), so the records, what the screen shows,
    # hold the counter of the frame before (`$2C74` 0DE4 / 0DE6 / 0DE8 and `$BD09` 0DE2 / 0DE4 / 0DE6 on rows
    # f37196-98). The port placed the layer on the live counter: the stars one step (2,1) ahead of the sprites, 4362 px
    # of this frame at the fight's lag (442 px, Flea alone, a lag further).
    'starfield-drifts-with-the-records': dict(leg='92_slash', at=0, frames=37200, own=True),
    # Death Peak's summit, playthrough leg 137 from its start, f9300: Magus on seat 1, 133 frames after the entrance
    # ladder. A PC's cells are the tiles its seat has in VRAM, which the NMI's group upload CF/E3F8 sends (`$E5` set, no
    # action; group 0 = the seats), not the builder C1/1C4A; the ladder's 64th and last call is a build pass with no
    # upload after it, so the pose it publishes (his 00, on row f9167) is not on the screen until group 0 is next
    # uploaded -- here 970 frames later, on the exit (f10139). The port drew the published pose at once: the record 00
    # where the cartridge still shows the 5C, his cape spread (301 px of this frame, at every lag).
    'ladders-last-pass-is-not-uploaded': dict(leg='137_lavos_spawns_summit', at=0, frames=9300, own=True),
    # The Tyrano Lair's hall, loc 12A (playthrough leg 102 from its start), f8400: a fight whose arena shakes -- the
    # scroll records CD/06B4 fills, `$BC89..` (BG1) and `$BD09..` (BG2), are (00B0, 0040 + n), n a new amount 0-7
    # every two to four frames (rows f8309-f8401; 7 on row f8399). The PPU scrolls a layer whole, so the skull door at the
    # top of the hall -- priority tiles -- moves with the floor. The port drew the map through the records' rows and
    # re-drew the priority tiles over the sprites from the unshaken place: the door stood still, 624 px of this frame
    # (132-624 on every picture of f8340-f8400 with an amount in its records).
    'priority-tiles-shake-with-the-arena': dict(leg='102_nizbel', at=0, frames=8400, own=True),
    # Heckran's cave (playthrough leg 59 from its start), f6000: the Tech list is open under an enemy's action and the
    # hand has just moved (row f5994: `$9EE3` 01). An action's loops call C1/10FA after their wait -- the menu pass,
    # THEN `JSL CD0009`, whose end is the typewriter step CD/04DF -- so the description the pass posts gets its first
    # character on that frame (row f5982: `$CD06` 02 and `$CCED` 01 together); the port made the step before the pass
    # on every frame and typed a character behind: `Spi` where the cartridge shows `Spin` (43 px of this frame,
    # 27-50 px on each picture of f5985-f6010 but two).
    'description-typed-on-the-pass-frame': dict(leg='59_heckran', at=0, frames=6000, own=True),
    # The Blackbird's elevator fights (playthrough leg 119 from its start), f6600: Crono and Lucca carry a status
    # icon, the blue ring over the head -- ONE large OAM entry each (the cartridge's OAM 21 and 12: name 1A8, 16x16).
    # The port drew it with four calls of a blitter that draws a 16x16 block a call, and the second put the name
    # n+16 again 16 px to the right: 19 px of the ring's lower left beside each ringed PC (38 px of this frame).
    'status-icon-is-one-entry': dict(leg='119_elevator', at=0, frames=6600, own=True),
    # Nizbel's fight (playthrough leg 82 from its start), f23100: the PC of seat 0 uses an item. An item's script
    # publishes its user's poses like any other (C1/1C66 -> `$9907,X`: on leg 92, `$9907` 2C on row f42546 and 6C
    # on f42561, the port's rows the same) and the picture shows them -- the arm raised, facing the screen. The port
    # drew seat 0 with the baked swing's run-up for every action of his that was not a tech: 406 px of this frame
    # (398 on leg 92's f42600).
    'an-item-is-not-the-swing': dict(leg='82_nizbel', at=0, frames=23100, own=True),
    # Masa & Mune's tornado (playthrough leg 67 from its start). The arena's lines are TM 13, TS 10, CGWSEL 02,
    # CGADSUB 07 -- OBJ on both screens, the sub screen added to BG1 / BG2 -- and a background program's op 0F
    # (CD/2B5C `LDX $CD00 / STZ $99B5,X`) puts the enemy's cells at OBJ priority 0 on row f32379 (`$99BB` 00,
    # CD:2B63): its twelve OAM entries go from attribute 38 to 08, under BG1 and BG2 at either priority, and it shows
    # as the sub pixel added to the cave's. The port drew it opaque on top: 1474-2066 px on every picture from f32382.
    # f32401 is inside the form (an odd frame: the form steps every second one, so only one lag fits); f32381 is the last picture before it -- the priority reaches the mirror a frame
    # after the row, like a pose (the port switched a picture early: 2044 px).
    'low-priority-sprite-is-the-sub-pixel': dict(leg='67_masa_mune_fight', at=0, frames=32401, own=True),
    'low-priority-reaches-the-mirror-a-frame-later': dict(leg='67_masa_mune_fight', at=0, frames=32381, own=True),
    # 2300 A.D.'s world map, entered on f6444 of playthrough leg 48. Its dust is BG3 under a per-line H scroll (C2/7759,
    # HDMA channel 1: the odd lines get `$24 + S(p + k)`), and the wave's phase p is the word at $7E:01E2 -- the object
    # reads `$0022,X` with X left at $01C0 -- which nothing on the world writes first: it is what the field left there,
    # and $01E3 is the direct page's $E3, the sheet's id the last allocating loader handed C0/5C90 (C0/4620 `LDA
    # $E4F023,X / STA $E3`). Rows f6181 / f6183: 03 25, then 03 61 (C0:4626); the wave steps the word by 4 from 6103.
    # The port's loaders did not store it (its page kept 8003): the dust never met the cartridge's, 1351 px here.
    'dust-begins-on-the-loaders-byte': dict(leg='48_lab32_road', at=0, frames=6600, own=True),
    # ...and a leg that BEGINS on that world (leg 132: the harness's world-capture start, field.c). The placement began
    # the camera's BG3 state afresh -- `$24` FFFC on the port's row f0 for the cartridge's F4DC -- and CtWorldCamSeed
    # gave every world the 64-word table's top, which a C2/7759 world does not have (its first 64 lines repeated down
    # the screen): 4003 px, then 1045 from row 65 on, then 0.
    'world-capture-seeds-the-dust': dict(leg='132_keepers_dome', at=0, frames=900, own=True),
    # Arris Dome's info centre (playthrough leg 43 from its start), a fight in a room seen through BG3: TM 04, TS 13,
    # CGWSEL 02, CGADSUB 44 -- BG1, BG2 and OBJ on the SUB screen alone. f600: Marle fallen (OAM 4-7 at y 159,
    # attribute 2E, OBJ priority 2) lies over BG1 tiles of priority 1, which beat her on the sub screen: she is not
    # on the cartridge's picture. The port showed her whole (391 px).
    'sub-screen-priority-tile-covers-the-fallen': dict(leg='43_arris_info_center', at=0, frames=600, own=True),
    # ...f660: a rising number over her cells -- OBJ priority 3, in front of the tile. Six of its white pixels fall on
    # white pixels of her cells; the priority mask was marked where a sprite CHANGED the picture, so they kept her 2
    # and the tile took them (6 px).
    'a-number-over-a-covered-pc-keeps-its-pixels': dict(leg='43_arris_info_center', at=0, frames=660, own=True),
    # ...f1800, the victory: Marle's ladder publishes 62 on row f1403 and 63 on f1407, its last pass, which is not
    # uploaded (VRAM words $0281-$03FE change between f1404 and f1406 and not again). The builder stores the new
    # record's cell offsets at once, so the picture is the tiles of 62 at the offsets of 63; the port drew 62 whole
    # (279 px of her, above the tile that hides her feet).
    'held-tiles-stand-on-the-new-cells': dict(leg='43_arris_info_center', at=0, frames=1800, own=True),
    # Magus (leg 95), f69600: a status has landed on Marle at an action's close. The close runs its status loop
    # (C1/411D-4131) on the wipe's frame (C1/4119) and only then walks the victims, whose PC entries wait three frames
    # more (C1/414B, 41B4, 41B7) before `STZ $A013`: her mark `$A46F` is 01 on row f69395 and `$A013` clears on f69398.
    # The port ran the loop with the STZ, so the mark's colour ramp began three frames late and stayed three steps
    # behind for the rest of the fight (142 px a picture; disasm/notes/battle_status_render.md, the last section).
    'close-status-loop-runs-with-the-wipe': dict(leg='95_magus', at=600, frames=69000, own=True),
    # Magus (leg 95), f11648: Crono's Tech list with the cursor on a combo tech. C1/17FE -> C1/180F puts a hand on
    # the chooser and one on each partner (`$9EE7`'s nibbles) in OAM entries 0-2, name 00, attribute 32, at the
    # target hand's own place: `$9EE3` 3A / `$9EE7` F2 on f11640 (Crono and seat 2), 42 / F1 on f11700 (Crono and
    # seat 1). The port left the entries empty (267-274 px a picture, f11640-f11730). f11648 is the frame the
    # second hand has just moved, so one lag alone shows it.
    'combo-tech-shows-its-partners-hands': dict(leg='95_magus', at=600, frames=11048, own=True),
    # Denadoro (leg 65, loc 092: a room under BG3's mist -- TM 17, TS 13, CGWSEL 02, CGADSUB 64), the party runs from
    # a fight and the screen goes to mosaic: a block's pixel is its first column's on the anchored row for every BG,
    # so the mist's priority pixel covers the whole block and Marle under it is its sub pixel, half the colour
    # ((110,56) 16,3,0 -> 8,1,0 with the mist faded to black); the port read the mask at each pixel's own place and drew
    # her whole (101-400 px a picture, f28060-f28229).
    'mosaic-block-makes-the-sprite-its-sub-pixel': dict(leg='65_denadoro', at=27600, frames=505, own=True),
    # The Blackbird (leg 125, loc 171): on f183147 a script takes the control and the lead goes on 2 px on his momentum --
    # C0/881E runs with explore off and leaves `$1D33` 20, which the lead's B0 pushes and the followers' gap test reads.
    # The port moved him without publishing it: Lucca took a ring entry where she stands a frame on the cartridge, and
    # the followers stopped 2-3 px short -- Marle drawn in front where Lucca is (419 px a picture to f195000).
    'momentum-step-is-a-moving-lead': dict(leg='125_blackbird_wing', at=183040, frames=1160, own=True),
    # Magus's name (leg 130, the naming screen): after the fifth letter `$0F06` is 5 (C2/E341), and C2/E34A puts the arrows
    # at min($0F06, 4) -- over the last letter. The port drew them a letter further right (292 px, f9500-f10800).
    'full-name-keeps-the-arrows-on-its-last-letter': dict(leg='130_black_omen_gaspar', at=0, frames=10000, own=True),
    # The exchange after Magus's naming (leg 130): the naming's resume lights the field (`$0119` 01 .. 0F, C0/2829) and
    # the `C8 00` exchange fades from there (f11307-f11321); with the scene's brightness left at the naming's 0 the port
    # opened it at once, fifteen frames early (its wave 7000 px off). On P the build's C2/C973 request is played by
    # C2/F643's task in the same scan: Marle's pose steps on f11370, f11377 .. (f11407 is a step's picture, on a frame
    # whose wave pins the offset: the pictures before and after it show other steps of the wave).
    'exchange-opens-after-the-naming-resume': dict(leg='130_black_omen_gaspar', at=0, frames=11407, own=True),
    # ...the picked cell's second hand ($0774) is the OAM entry after the cursor's ($0770): the cursor's is in front.
    'exchange-cursor-hand-over-the-picked': dict(leg='130_black_omen_gaspar', at=0, frames=11650, own=True),
    # ...and its close (C2/CC0E's fade, no task killed) fades the live screen: the wave and the sprite blocks go on.
    'exchange-close-fades-the-live-screen': dict(leg='130_black_omen_gaspar', at=0, frames=11758, own=True),
    # Leg 92, Flea? (killed by Crono's tech on f33095) counters with "MP Buster" (its block 2): the counter's name box
    # (CD/0295) runs in the action's handler tail on f33095-f33106, and the tech's effect teardown CD/0453 only inside
    # the counter's open (C1/4058 -> C1/40A3 JSR C1/10FA -> CD/0009) on f33108-f33128; f33110 shows the box.
    'counter-box-before-the-teardown': dict(leg='92_slash', at=32200, frames=910, own=True),
    # ...and, relisted for that counter (`$AF02,X := $AF0D,X`), it is the combatant with the counter's poses, no corpse.
    'fallen-counterer-keeps-its-live-pose': dict(leg='92_slash', at=32200, frames=1000, own=True),
    # Leg 93, a chest's "Got 1 Dark Mail!" (`06` + 13 x `EF` + "Got 1 {item}!"): the `06` token's indent is drawn, never
    # typed (`$0129` 07 on f26089, 30 iterations), so f26084 shows the text as far as the cartridge's ...
    'chest-message-indent-is-not-typed': dict(leg='93_ozzie_way', at=0, frames=26084, own=True),
    # Leg 94 (Ozzie's barrier), Crono's tech on the Juggler (f9561): its graphics record flags it (E4/F604 bit 5 ->
    # `$A00A` 01, CC/EBA0) and its offset `$971A` is E0, so C1/5B5E puts its "147" 32 lines above its foot.
    'flagged-enemy-number-takes-its-offset': dict(leg='94_ozzie_barrier', at=5500, frames=4100, own=True),
    # ...and the barrier's statues (objs 16 / 17): the first fight's epilogue marks them removed (`$1A81` 0, `$1000` bit
    # 7), a script brings them back (op 0x0C, then op 0x90 on f18867) and they rise from the pits.
    'removed-enemy-shown-again-is-drawn': dict(leg='94_ozzie_barrier', at=0, frames=18900, own=True),
    # Leg 137 (the summit's fight), action `$9877` 84: lines whose sub screen is the sprites alone (TS 10) add them to
    # BG3's purple disc inside op 8B's circle window (f4585-f4715) ...
    'bg3-disc-adds-the-sprites-under-it': dict(leg='137_lavos_spawns_summit', at=0, frames=4640, own=True),
    # ...and slot 0's corpse (type 3: four $400 packets a level) shows its first packet a level ahead (f9000).
    'corpse-level-ships-in-packets': dict(leg='137_lavos_spawns_summit', at=0, frames=9000, own=True),
    # Leg 91 (loc 0A9's fight, from f42586), the enemy's action `$9877` 7B: cmb 3 jumps with an afterimage (`A4`), four
    # copies of its cells at the recorded points behind the body, every other frame.
    'enemy-afterimage-is-drawn': dict(leg='91_flea', at=0, frames=69900, own=True),
    # Leg 95 (Magus's fight), action `$9877` 9F: Magus's shadow in OBJ palette 5 (`$9F46` bit 3, CD/1B8B), f14400.
    'shadow-in-palette-5-is-drawn': dict(leg='95_magus', at=0, frames=14400, own=True),
    # The same fight, Magus's fire (f17340-f17406): lines 0-160 run TM 17 TS 13 TMW 17 TSW 00 CGWSEL 02 CGADSUB 04 with no
    # window enabled on any layer (W12SEL / W34SEL / WOBJSEL 00), so TMW masks nothing and the sub screen is the main
    # screen less BG3: the bursts are added over the hall and the party. f17380.
    'fire-adds-where-no-window-is-on': dict(leg='95_magus', at=600, frames=16780, own=True),
    # Leg 29 (the fight from f778): the ball's chosen victim fell; C1/8DE4's re-draw makes Marle the script's target too.
    'redrawn-victim-is-the-scripts-target': dict(leg='29_gate_home', at=0, frames=1800, own=True),
    # Leg 110 (the fight from f20536, run away): the run-off's end reloads no gauge -- seat 0's stays full (f21000).
    'run-off-reloads-no-gauge': dict(leg='110_mt_woe_summit', at=20400, frames=600, own=True),
    # Leg 74 (the fight from f589, four reptites on one foot line): the boot's pictures keep the field's OAM -- the
    # draw list's order -- until the first compose is uploaded (f644); graded inside the boot.
    'fight-boot-keeps-the-fields-oam': dict(leg='74_dreamstone', at=0, frames=620, own=True),
    # Leg 29's fight (from f778). Marle (seat 2) uses a Revive on Lucca (seat 1): on the close's four frames (f2116-f2119)
    # Lucca stands and Marle stands at her own place -- no baked trajectory's pose at seat 1's anchor. A still scene: `lag`.
    'marle-keeps-her-published-pose-in-the-close': dict(leg='29_gate_home', at=0, frames=2117, own=True, lag=-1),
    # ...and the enemy's hop in place (action 36, op C4 / C5 stepping `$A4AF` in the main loop, C1/6FCC): each step is on
    # the picture a frame after a service's hop would be (row + 3). f3734: the first step's row + 2 -- still on the ground.
    'script-hop-reaches-the-mirror-a-frame-later': dict(leg='29_gate_home', at=0, frames=3734, own=True, lag=-1),
    # Leg 51, the factory's fight (from f585): the boot's init stores the HDMA preset base `$CA25` = 08 (D1/F290) and
    # leaves the request counter `$CA24` alone, so a leftover there makes the first V-IRQ install preset 08 + `$CA22`.
    # Without the store the leftover base (29) installed preset 2A: the room's top 88 lines torn for the boot (5774 px).
    'fight-boot-installs-its-own-hdma-preset': dict(leg='51_security', at=570, frames=30, own=True, lag=-1),
    # ...and the same fight's palettes. The field's staging copies the base page onto the live page the frame before the
    # module's init (C0/0409, f584: the room's lights at their base colour on pictures f586 / f587 -- 304 px without it),
    # and the boot's stager copies it again only when the song's upload is over (CC/E6EC, f622: pictures f624-f627, where
    # the room's colour record had just written its 4701 -- 300 px with the copy made at the entry).
    'fight-staging-puts-the-base-palette-back': dict(leg='51_security', at=570, frames=16, own=True, lag=-1),
    'fight-boot-palettes-wait-for-the-upload': dict(leg='51_security', at=570, frames=55, own=True, lag=-1),
    # A door's block and a chest's lid are written to VRAM by the IRQ (C0/ED24, C0/EFD1), which does its map jobs only on a
    # frame whose `$0153` bit 0 is clear: the handler's frame or the one after. Leg 26, the door before loc 0C6 (the latch
    # on f7245, the metatiles on f7246 with the bit set, VRAM on f7247): still shut on picture f7248 (568 px with the block
    # released in the handler). Leg 93's chest (`Got 1 Dark Mail!`): the lid on picture f26048 (146 px).
    'door-opens-on-the-irqs-frame': dict(leg='26_yakra', at=7200, frames=48, own=True, lag=-2),
    'chest-lid-opens-on-the-irqs-frame': dict(leg='93_ozzie_way', at=0, frames=26048, own=True, lag=-2),
    # ...and the lead walking up into that door: the step commit's priority pair (`$0C02` / `$0C03` = 20 20, C0/9D62) is
    # on the screen only from his next template -- his pieces keep priority 3, in front of the door frame's priority
    # tiles, on pictures f7272-f7275 (23 px each with the pair read live).
    'lead-keeps-his-templates-priority': dict(leg='26_yakra', at=7200, frames=72, own=True, lag=-2),
    # Leg 65, Denadoro (TM 11101 / TS 11001, half-add): the target hand (attr 32, OBJ priority 3) is the sub pixel of BG3's
    # mist like every battle sprite -- f37630, a target select of the fight from the state cut on f36900 (131 px opaque).
    'target-hand-under-the-mist': dict(leg='65_denadoro', at=36900, frames=730, own=True, lag=-1),
    # Leg 92, the Flea fight: anim-script op 72's point modes (19-23, C1/7639 / C1/764A) leave the facing latch `$A2B4` and the
    # op stores it all the same (C1/6006), so Frog turns to the facing the last mode wrote -- LEFT on the cartridge, f49200
    # from the leg's start (a state cut inside the fight cannot seed the port; 602 px with the point modes read as inert:
    # Frog mirrored for ~70 frames).
    'facing-latch-on-a-point-mode': dict(leg='92_slash', at=32400, frames=16800, own=True, lag=-1),
    # ...and the status icons the compose builds (CF/EDB8-EDC0 `LDA $A013 / BNE / LDA $A0FE / BNE`): the compose runs before
    # the frame's main loop, so an action's open (C1:40A9 on row f80398) takes them off picture f80401; read the same frame,
    # the port took them off a picture early (f80400: 181 px -- Frog's poison bubbles, Marle's star, Crono's face).
    'status-icons-gate-on-the-frame-before': dict(leg='92_slash', at=32400, frames=48000, own=True, lag=-1),
    # Leg 127, the Golem Boss's exit from the wing ('I HATE heights!'): the anim-script pose engine names the enemy's tables by
    # what the staging laid, not by the staged list `$AF0D` a removal cleared (FF from f17042): the boss rises with its arms
    # spread, poses 00/01/02/01 from f17066 (f17069: 1652 px folded before).
    'golem-boss-exit-poses': dict(leg='127_winged_epoch_179', at=800, frames=16269, own=True, lag=-1),
    # ...and once op 29 hides it (`$9FFA` = 01 from f17159) its attached effect object 0 still draws the smoke cloud: the
    # compose's hidden gate is in the cells' routine only (CF/EAD7), not in the objects' passes CF/EDB8 / CF/EDF7 (f17165: 817 px).
    'hidden-boss-keeps-its-smoke': dict(leg='127_winged_epoch_179', at=800, frames=16365, own=True, lag=-1),
    # Leg 134, Death Peak's wind (loc 0F4), Ayla leading (`$0197` = 06): the wind stops her and she plays the head-shake (anim
    # 17, `$1686` 17 01/02/03/00 f13733-f13802) -- C0/9E84's run tail tests `CPY $97` (C0/A19A) with Y = the lead's index; the
    # port ran it on its own page (the lead in slot 2) with the scene's `$97` and kept the run (anim 06, 411 px at f13800).
    # (From the leg's start, where this room lines up at -3. From a state cut on f13700 the head-shake runs too but a step of
    # its 10-frame cycle apart at f13800, 260 px at -2 -- not looked at.)
    'lead-head-shake-on-its-own-slot': dict(leg='134_death_peak_wind', at=0, frames=13800, own=True, lag=-3),
    # Leg 58, Heckran's cave: the return from fight 1 waits in C7's poll inside C0/0304's segment -- C0/0330's C0/595C re-posts
    # the room's song (obj0 slot 1 `EC 11 ..`, C7/0A5A's counted poll, 2 frames) before that segment's C0/00EB: `$0153` stays 00
    # on f3012-f3013 and the tiles step on f3014. Run two frames early, the port had the torches a step off, then obj0's ambush
    # and the formation (f4500: 961 px) and the fight's places (f5100: 532 px).
    'return-waits-out-the-songs-poll': dict(leg='58_heckran_cave', at=0, frames=4500, own=True, lag=-2),
    # Leg 35, the Dragon Tank's fight: its boot (f1711-f1727) keeps the field's last OAM upload -- the tank's head and the
    # Grinder wheel in the poses the field last showed -- until the first battle picture f1728 (631 px a picture before).
    'boot-shows-the-fields-last-oam': dict(leg='35_dragon_tank', at=0, frames=1720, own=True, lag=-2),
    # Leg 55, the End of Time (loc 1D0): Gaspar's `C8 00` opens the exchange screen and the room comes back through C0/01A5.
    # Its reload clears the whole $1D page (C0/01CC JSR C0/7F7E), BG3's wave phase $1DFB with it, and C0/0B28 builds the
    # tables once before the vblank wait; screen $07's close waits one frame more in C2/82B2's C2/83CA (no song to put back);
    # and the NMI-off stretch is 31 frames, obj0's slot-1 run priced op by op as the VM runs it. Each one off put the void's
    # wave or the room's clocks off for the rest of the room (1280-11892 px a picture, f3000-f4800).
    'menu-reload-clears-the-1d-page': dict(leg='55_spekkio', at=0, frames=4201, own=True, lag=-2),
    # Leg 49, Proto Dome (loc 0E2): Robo's naming screen (`C8 C3`) comes back through C0/0D1C -> C0/01A5, whose obj0 slot-1
    # run takes ops 18 / 7C / 7D besides 12 / 16. The walk refused op 18 and the load estimate priced the NMI-off stretch
    # 35 frames for the cartridge's 37 (f11192-f11228 + 1): the room's visit countdowns restarted 2 frames early and Robo's
    # later walks ended 1 px off (511 px at f12900). The run is now priced op by op as the VM runs it.
    'naming-reload-prices-the-slot-1-run': dict(leg='49_robo', at=0, frames=12900, own=True, lag=-2),
    # Leg 131, the Epoch's dock (loc 1D9) after the time trip: Marle's fn0 `8D` seats her at the pilot's place on C0/2848's
    # first ramp frame; the lead's step starts from his own words $1800/$1880 (the ROM's only copy), so she stays there.
    'lead-steps-from-its-own-words': dict(leg='131_c_trigger', at=0, frames=3900, own=True, lag=-1),
    # Leg 96, the hut's exit onto 65,000,000 B.C. (1F3): the world loader's C2/23B4 / 23B7 free the fade objects' mutex
    # $1BEC / $1BF6, which the seed carried set from the last field menu; the world fades in (it stayed black, 57344 px).
    'world-load-frees-the-fade-mutex': dict(leg='96_laruba', at=0, frames=300, own=True, lag=-1),
    # Leg 100, the Volcano absorbing a hit: C1/DDAD-DDB6 negates the slot's -dmg back, so the record and $AE7B take +dmg
    # and the green popup reads 676 (the port printed FD5C as 64860).
    'absorb-record-is-positive': dict(leg='100_lair_grind', at=0, frames=15900, own=True, lag=-1),
    # Leg 111 (loc 189), fight 2: its message strip counter $CE14 starts from the map image C0/A33B decompressed at $7E:B500
    # on fight 1's return (the tail past $C800 survives C0/09DD / C0/0A14); the name types in on the cartridge's parity.
    'map-image-tail-seeds-the-strip-counter': dict(leg='111_mt_woe_chests', at=0, frames=12600, own=True, lag=-1),
    # Leg 93, the Item overlay (a Mid Tonic on Marle at 1/242 HP): C2/AFED's B02B-B032 asks her sprite for the weak pose 12
    # (the kneeling pair); no frame resets the request. OPEN: 18 px of her bow, drawn dark on the cartridge (colour math).
    'item-overlay-asks-the-weak-pose': dict(leg='93_ozzie_way', at=0, frames=27900, own=True, lag=-2),
    # Leg 125 (loc 170, the Blackbird): a fight's entry (C0/18A4 JSR C0/EC60) and its ack frame (C0/00EB's BRL C0/EC60) each
    # count the idle counter $0158; without them every idle roll after a fight came 2 frames late (Marle's blink, f29400).
    'idle-counter-counts-the-fights-waits': dict(leg='125_blackbird_wing', at=0, frames=29400, own=True, lag=-2),
    # Leg 67 (the Masamune fight's exit): the exit tail's NMI is D1/F4C0 alone -- no CD/0650 `INC $CE14`, no strip half,
    # and CD/09CE's CGRAM page only inside C7 (DB = 0) -- so the next fight's typed box keeps the cartridge's parity.
    'exit-tail-nmi-is-d1-f4c0': dict(leg='67_masa_mune_fight', at=0, frames=16200, own=True, lag=-1),
    # Leg 44, the Day of Lavos's record on 1F7 (the overworld: port P = cartridge P + 1). Three pictures of one scene:
    # - f2400, the record's grid: TS 04, CGWSEL 02, CGADSUB 03 -- BG3 on the SUB screen added to BG1 / BG2 (the green
    #   lines every 32 px); the compositor took sub-screen math for BG3's own pixels only.
    # - f2700, the static: C2/7A56 aims channels 1 and 2 at BG1HOFS / BG2HOFS through one indirect table whose lines
    #   are 0x1C0 bytes of ROM C0:0000 (`MVN $7E,$C0`), C2/7AAF stepping the pointers. Every tilemap column shows, so
    #   the picture also holds C2/09C5's extent (36 columns from the cursor's block, zeros beyond after C2/26A8).
    # - f3900, the rain: C2/7A0C / 7A31 place each needle at a random spot (two draws of C2/2336).
    'record-grid-is-the-sub-screen-added': dict(leg='44_day_of_lavos', at=0, frames=2400, own=True, lag=-1),
    'record-static-scrolls-both-layers': dict(leg='44_day_of_lavos', at=0, frames=2700, own=True, lag=-1),
    'lavos-rain-falls-at-random-places': dict(leg='44_day_of_lavos', at=0, frames=3900, own=True, lag=-1),
    # Leg 86, the Magic Cave opening on 1F1: C6/E000 installs window 1's table (the bytes of C6/E448) and C2/7911 draws
    # a circle a frame into its double buffer (C3/0008 around the caller's +14 / +18, radius +22) -- the beam.
    'magic-cave-beam-is-a-window-circle': dict(leg='86_magic_cave', at=0, frames=15104, own=True, lag=-1),
    # Leg 115, the Blackbird over 1F4 after Dalton: the ship is BG1 -- blocks 71-C4 of the world's own map packet
    # (C6:FF20[header +11], C2/298C), which the loader decompresses at every load; a script takes it out when the story
    # has no Blackbird. TS 01, CGADSUB 12: BG1 on the sub screen added to the clouds. (The port's world runs three
    # frames ahead here, from its first frame on -- the cause, before the load, is not traced -- so it is graded at port -3.)
    'blackbird-stands-in-the-map-packet': dict(leg='115_dalton', at=0, frames=10200, own=True, lag=-3),
    # Leg 105, the Blackbird's deck after Dalton's Golem beats the party (a fight the story loses: `$7F01EC` = 1 with
    # `$2A1F` bit 5): C0/0298 `BIT #20 / BNE ->0304` joins the won fight's tail, whose C0/0715 dead-marks the Golem's
    # object and puts the party back on its battle cells in its last battle frames before C0/0738 stages the fallen pose.
    'lost-fight-that-continues-runs-the-return-epilogue': dict(leg='105_zeal', at=0, frames=43200, own=True, lag=-2),
    # Leg 115, the Comb row opened on Dalton's Golem (f4500-f4503): the open C1/12BC runs no grey pass and no resolver;
    # the next pass head does, so a combo's partner hands come up one menu pass (four frames) after the A.
    'tech-open-resolves-on-the-next-pass': dict(leg='115_dalton', at=0, frames=4501, own=True, lag=-1),
    # Leg 110, Mt. Woe's summit: `10 45` parks the loop on f26097-f26100 with BG1 / BG2 a step behind their counters and
    # the fog (BG3) not moving: each layer is drawn at ITS shown word (BG3's stood a pixel off, taken back by BG1's lag).
    'park-shows-each-layers-own-word': dict(leg='110_mt_woe_summit', at=0, frames=26099, own=True, lag=-2),
    # Leg 105, the second gate of the leg (`FF 81`, the jump on f3106): C0/3FD3's quiesce, the JML and FD/DB97's setup run in
    # the frame whose head found `$19` at 0, so the tunnel's pictures are the stage's own P+1 (they sat a frame late).
    'gate-warp-opens-on-the-jumps-frame': dict(leg='105_zeal', at=0, frames=3300, own=True, lag=-1),
    # Leg 95, Magus's fight (f64185-f64205): anim-script ops 11 / 12 store the bearing in `$A355,Y` and as the effect
    # object's heading `$A5B5,Y` (C1/50BD-50C2); the orbit that follows with an FF angle (C1/6E9A) reads it back, so the
    # sparkle starts where the slide left off (it spawned about half a turn round the circle).
    'slide-bearing-is-the-objects-heading': dict(leg='95_magus', at=0, frames=64190, own=True, lag=-1),
    # Leg 127, the same wing: op `EA 29` parks the main loop on f714 (the song's upload, to f754), after that iteration's
    # head stepped the clouds' counter (`$1D8D` F988 -> F987) and before FD/C2C1 built a table set from it -- the screen
    # keeps the set it had (`$7F:1514` = F988) for the whole park. f730: the clouds a line on with the counter read live
    # (4013 px on every picture of the park).
    'park-shows-the-published-scroll': dict(leg='127_winged_epoch_179', at=700, frames=30, own=True, lag=-2),
    # Leg 127, the Blackbird's wing (loc 16B): the Golem Boss is placed with its OBJ palette row zeroed by a colour record
    # (FD:E699, the fade-in's first state: `$2340-$235F` all 00 on f410-f412) -- a black sprite on the black circle until
    # the record's first step on f413. A row of zeros is the row: the drawer must not put the sheet's own colours back.
    'object-with-a-zeroed-palette-row-is-black': dict(leg='127_winged_epoch_179', at=380, frames=33, own=True, lag=-2),
    # Leg 56 begins inside the End of Time's back room (loc 1D1), whose master picks his sprite from the lead's level
    # (object 10's fn0: `83 E0` up to level 9, `83 E1` to 19, ..). The state says `$1114` / `$1115` = 06 E1; a seed that
    # kept the sprite its own run of fn0 chose -- on a fresh cartridge's levels -- drew E0 (505-623 px on every picture).
    'seeded-object-keeps-its-sprite': dict(leg='56_medina_gate', at=0, frames=300, own=True, lag=-2),
    # Leg 61, Truce Canyon's fight (from f2387): until the fight's first compose is uploaded the OAM is the field's, and
    # so is each cell's OBJ priority -- object 11's cells read attribute 22 (priority 2) on f2387-f2447 and 32 from f2448.
    # The enemy's wing stays behind the foliage's priority tiles for the boot's 60 pictures (62 px a picture in front).
    'fight-boot-keeps-the-fields-priority': dict(leg='61_zenan_supplies', at=2360, frames=40, own=True, lag=-1),
    # Leg 76, the overworld (world 1F3): the party walks onto an entrance with A held. The frame the lead takes it the
    # followers stand where they are; the port walked them a pixel on, in the walk's pose (192 px through the fade).
    'overworld-followers-stand-on-the-exit-frame': dict(leg='76_forest_maze', at=0, frames=600, own=True, lag=-1),
    # Leg 68, the overworld (loc 1F1): the walk an event handed back. The followers were a pixel behind the cartridge's
    # from the first frame (their init copied a lead that had not stepped yet), so their trail and their walk frames
    # were out of step for the whole walk (78-230 px on every picture). The overworld's pictures are the port's a frame on.
    'overworld-followers-after-an-event': dict(leg='68_porre', at=0, frames=300, own=True, lag=-1),
    # Leg 11, the Telepod's room (loc 008): the Gate's ring is BG3 seen between the iris's edge and the swirl's sprites,
    # and BG3 drifts a fraction of a pixel a frame. The song's upload (`EA 1F`, f1569-f1609) parks the main loop inside
    # the runner; a camera leg run again on the return frame put the drift a frame ahead for the rest of the room -- one
    # pixel of BG3 on three pictures in eight (f3274: 18 px along the ring). The state is cut before the upload.
    'gate-ring-after-a-song-load': dict(leg='11_gate', at=1560, frames=1714, own=True, lag=-2),
    # Leg 63, Zenan Bridge (loc 087): three objects (21-23) come into the camera's window as the party walks west. The
    # latch C0/A88D takes them on f1543 (`$0F00,X` = 80, `$0F01,X` = FF, `$1B00,X` = 0), the drain stages two on f1544
    # and the third on f1545 (`$0F01,X` = 13, `$1B00,X` = 80 from the draw pass), and the picture shows none on f1545,
    # two on f1546, three on f1547: the draw pass draws on C0/B701's carry, which a latched object with nothing staged
    # does not have. (The port drew a latched object at once, with its own ticker's frame: 1100 px a picture early.)
    'latched-object-waits-for-its-template': dict(leg='63_zombor', at=1530, frames=15, own=True, lag=-2),
    'staged-objects-show-one-by-one': dict(leg='63_zombor', at=1530, frames=16, own=True, lag=-2),
    # Leg 34, the prison's fight (loc 048, enemies 3E 3E 3F 3F): two guards on the floor and two HIDDEN ones (`$1A81,X`
    # bit 7) that action 7D calls in -- its caster blinks (track 0, CD/D208: `29` / `2A` every six frames), the called
    # one jumps to the caster's place and comes into sight (track 3, CD/D235: `1B 00 / 06 00 / 06 03 / 5D / 2A`). The
    # state is cut 75 frames before the fight (f15400); the fight's pictures are the port's a frame on (`lag=-1`).
    #   f15945: the caster is off the alive list `$AF02` while its action runs and stays on the screen (595 px).
    #   f15963: the script's `29` reached `$9FFB` on f15961; the picture hides the caster from f15964 (574 px a frame early).
    #   f15968: the called guard is in sight from op `5D` (bit 7 of `$9FFC` cleared on f15965), a frame before the `2A`.
    #   f16790: a called guard dissolves in the palette the staging allocated for it (C0/05F0 `JSR C0/72B4`; 506 px).
    'enemy-off-the-alive-list-stays-on-the-screen': dict(leg='34_to_bridge', at=15400, frames=545, own=True, lag=-1),
    'script-hide-reaches-the-screen-a-picture-later': dict(leg='34_to_bridge', at=15400, frames=563, own=True, lag=-1),
    'called-enemy-in-sight-from-op-5d': dict(leg='34_to_bridge', at=15400, frames=568, own=True, lag=-1),
    'hidden-enemy-dissolves-in-its-own-palette': dict(leg='34_to_bridge', at=15400, frames=1390, own=True, lag=-1),
    # A sprite's pieces keep the priority their template took (`$0C00,X` / `$0C01,X` ORed into each record's attribute, C0/CD47..):
    # the pair the mover changes since is on the screen from the object's next template. Leg 32 f7504 (the castle's cell
    # block): the guard stands with his template's priority 3 while the pair reads 20 20, and the camera's scroll brings the
    # arch over his head -- he stays in front of it (81 px). Leg 21 f1500 (the inn): Lucca steps off a tile whose pair
    # hides her feet's row; the row stays hidden until her next frame (10 px).
    'sprite-keeps-its-templates-priority': dict(leg='32_cell', at=0, frames=7504, own=True, lag=-2),
    'walker-keeps-its-templates-priority': dict(leg='21_inn', at=0, frames=1500, own=True, lag=-2),
    # Leg 08, Leene's Bell (loc 1B7): Marle's `96 19 07` reaches the tile and snaps to its centre (C0/305D). The snap's own
    # velocity is 10 / 10, and the selector the op runs on it (4FA5 JSR C0/5614) reads 10 + 10 = $20 as a RUN for a
    # character below 4: eight pictures of the run's frames (f1187-f1194), then the walk. f1190 (222 px: she walked on).
    'tile-snap-reselects-the-animation': dict(leg='08_candy', at=0, frames=1190, own=True, lag=-2),
    # Leg 48, Lab 32's road (loc 0E0): the Octopods are class-1 sprites (`$1201,X` & 3 = 1), staged in two drain visits --
    # pass 1 (C0/D28A) commits `$0F01,X`, pass 2 (C0/D30D) a frame later queues the tiles and writes the OBJ template -- so
    # the cartridge shows the new frame a picture after a class-0 sprite would. f5091: object 20 between the two (commit
    # f5089, `$1B28` 81) still stands sideways (861 px of this frame; the room is still, so `lag`).
    'multi-pass-sprite-shows-its-templates-frame': dict(leg='48_lab32_road', at=0, frames=5091, own=True, lag=-2),
    # Johnny's race WON (the branch leg 47b; the race's picture is the port's P+1, so `lag`). f2985: inside the fanfare's
    # post (f2971-f2990) the main loop stands under the NMI that made it, so the bikes keep the frames they had -- Johnny's
    # is not redrawn rearing (298 px of this frame). f2997: the run-out begins on the cartridge's frame -- the post took its
    # twenty frames against the race's own HDMA, not the field's mean (18855 px: the whole road a frame early). f3700: the
    # script's camera sweep ($41A6..) over what the camera's task $416D left.
    'race-win-bikes-stand-under-the-fanfare': dict(leg='47b_race_win', at=0, frames=2985, own=True, lag=-1),
    'race-win-run-out-on-the-cartridges-frame': dict(leg='47b_race_win', at=0, frames=2997, own=True, lag=-1),
    'race-win-camera-sweep': dict(leg='47b_race_win', at=0, frames=3700, own=True, lag=-1),
    # ...and the A that closes it (f26096) sets `$0129` = 0D, the ramp from the box machine's next frame (f26098).
    'chest-box-closes-from-the-next-frame': dict(leg='93_ozzie_way', at=0, frames=26100, own=True),
    # Leg 17 (the queen's room): the route's `100:2,100:2` is A held four frames, one press on the cartridge; the port's
    # walkscript raised a second A on the second token and probed obj 21 while the box was closing (f2891), its scene
    # forty frames ahead. f3000 shows the queen's box on both sides.
    'held-key-is-one-press': dict(leg='17_queen', at=0, frames=3000, own=True),
    # The Epoch's naming (leg 108: the screen opens on f35404 and stands), f35700. The naming screen's portrait is read
    # by the name's id through `$FFCE70,X` (base tile) and `$FFCE78,X*2` (palette) at C2/EC38, and the tiles are the
    # PCs' portrait block for ids 0-6; id 7, the Epoch, has no place in that block: C2/E44D (`CMP #07 / BCC / LDX
    # #FC68 / JSR C2/8385`) queues the record C2:FC68 -- $0480 bytes from FF:EB88 to VRAM $2010 -- and its palette is
    # FF:CE50. The port packed and drew seven portraits: the Epoch's frame stood empty (2295 px of this frame).
    'naming-portrait-of-the-epoch': dict(leg='108_epoch', at=35300, frames=400, own=True),
    # The forest's fight run away from (the recipe's state: walk left into the Hetakes, hold L + R), 51 frames into the
    # run-off action, on a frame the program's op 90 raised the mosaic: `$BE9A` 77 -> 87 on line 64, so HDMA channel 6's
    # run of line 0 read the old size and its run of line 80 the new one -- 8-line blocks down to row 79, 9-line blocks
    # from row 80, each run anchoring its blocks on its own line (the first row of blocks one line short at the top).
    # BG1, BG2 and BG3 take it together: the canopies (priority tiles) are blocks like the ground, over the sprites
    # wherever a block's first pixel is a canopy's. And the Hetakes stand where the frame service's walk stepper left
    # them under the "Ran away" box. The port's mosaic was the whole frame's, anchored on its top row with the new size,
    # its canopies sharp over it, and its Hetakes stood where the flee's trigger found them (32,000 px of this frame).
    'run-off-mosaic-by-the-line': dict(state='saves/battle_re/forest_holdleft_pre.state',
                                       pad=[(0x40, 120), (0, 230), (0xC00, 646)]),
}


def png(path, c, p):
    rows = []
    for y in range(224):
        cr, pr = c[y * 768:(y + 1) * 768], p[y * 768:(y + 1) * 768]
        dr = bytearray()
        for x in range(0, 768, 3):
            dr += b'\xff\x00\x00' if cr[x:x + 3] != pr[x:x + 3] else bytes(v // 3 for v in cr[x:x + 3])
        rows.append(b'\0' + cr + pr + bytes(dr))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 768, 224, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))


def cut(pad, n):
    """The first n frames of a pad, as a pad."""
    out = []
    for b, k in pad:
        if n <= 0:
            break
        out.append((b, min(k, n)))
        n -= k
    return out


def span(pad, lo, n):
    """Frames lo..lo+n-1 of a pad, as a pad."""
    out, c = [], 0
    for b, k in pad:
        a, e = max(c, lo), min(c + k, lo + n)
        if e > a:
            out.append((b, e - a))
        c += k
    return out


def state_at(legs, i, at):
    """The cartridge's state at frame `at` of leg i: the leg's pad played that far from its start state (cached with
    the key of what made it -- every pad from power-on through the leg, the oracle, the frame)."""
    lg = legs[i]
    path = os.path.join(WORK, 'leg%s_f%d.state' % (lg['name'].split('_')[0], at))
    key = hashlib.sha1(('%s|%d' % (P.key_of(legs, i), at)).encode()).hexdigest()
    if os.path.exists(path) and os.path.exists(path + '.key') and open(path + '.key').read().strip() == key:
        return path
    if os.path.exists(path):
        os.remove(path)
    if not P.oracle_play(P.start_of(legs, i), cut(lg['pad'], at), path):
        sys.exit('fieldpicture: the cartridge did not make %s' % path)
    open(path + '.key', 'w').write(key)
    return path


def run(name, want_png):
    d = PICTURES[name]
    if d.get('state'):                                        # a recipe's state played under its own pad
        P.TMP = SCRATCH
        os.makedirs(SCRATCH, exist_ok=True)
        start = os.path.join(ROOT, d['state'])
        _, pics = P.port_timeline(start, d['pad'])
        c = P.oracle_picture(start, d['pad'])
        if not pics:
            print('fieldpicture: %s: the port drew no picture' % name)
            return 1
        k, n = P.picture_grade(c, pics)
        print('fieldpicture: %s %s +%d frames of its pad: %d px differ at port %+d' % (
            name, os.path.basename(d['state']), sum(q for _, q in d['pad']), n, k))
        if want_png:
            out = os.path.join(WORK, name + '.png')
            png(out, c, pics[k])
            print('  -> %s' % out)
        return 0
    legs = P.route()
    i = P.find(legs, d['leg'])
    if i > 0:
        P.ensure_states(legs, i - 1)
    P.TMP = WORK
    start = state_at(legs, i, d['at']) if d.get('at') else P.start_of(legs, i)
    P.TMP = SCRATCH
    os.makedirs(SCRATCH, exist_ok=True)
    own = bool(d.get('own'))
    pad = span(legs[i]['pad'], d['at'], d['frames']) if own else [(0, d['frames'])]
    keys = P.walkscript
    first = 0
    if own and d.get('at'):                                   # the port's first key: the leg's pad frame before the cut
        prev = span(legs[i]['pad'], d['at'] - 1, 1)
        P.walkscript = lambda p: ','.join('H%X:%d' % (mk, n) if mk else '.:%d' % n for mk, n in prev + p)
    elif own:                                                 # ...and from the leg's start, the previous leg's last
        first = P.last_key(legs, i)
    try:
        _, pics = P.port_timeline(start, pad, first)
    finally:
        P.walkscript = keys
    c = P.oracle_picture(start, pad)
    if not pics:
        print('fieldpicture: %s leg %s: the port drew no picture' % (name, legs[i]['name'].split('_')[0]))
        return 1
    k, n = P.picture_grade(c, pics)
    if os.environ.get('FIELDPIC_LAGS'):                       # every port frame's count (to choose a pinned `lag`)
        print('  lags: ' + ' '.join('%+d:%d' % (q, sum(1 for j in range(0, len(c), 3) if c[j:j + 3] != pics[q][j:j + 3]))
                                      for q in sorted(pics)))
    if 'lag' in d:                                            # a picture whose difference is WHEN a thing is drawn in
        k = d['lag']                                          # a still scene: graded at its own port frame, not the best
        if k not in pics:
            print('fieldpicture: %s leg %s: no port picture at %+d' % (name, legs[i]['name'].split('_')[0], k))
            return 1
        n = sum(1 for j in range(0, len(c), 3) if c[j:j + 3] != pics[k][j:j + 3])
    print('fieldpicture: %s leg %s%s +%d %s: %d px differ at port %+d' % (
        name, legs[i]['name'].split('_')[0], ' f%d' % d['at'] if d.get('at') else '', d['frames'],
        "frames of the leg's pad" if own else 'idle frames', n, k))
    if want_png:
        out = os.path.join(WORK, name + '.png')
        png(out, c, pics[k])
        print('  -> %s' % out)
    return 0


def main():
    os.makedirs(WORK, exist_ok=True)
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    bad = [a for a in args if a not in PICTURES]
    if bad:
        print('fieldpicture: no picture named %s (known: %s)' % (', '.join(bad), ', '.join(sorted(PICTURES))))
        return 1
    rc = 0
    try:
        for name in args or sorted(PICTURES):
            rc |= run(name, '--png' in sys.argv)
    finally:
        P.TMP = WORK
        shutil.rmtree(SCRATCH, ignore_errors=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
