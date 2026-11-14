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
    if own and d.get('at'):                                   # the port's first key: the leg's pad frame before the cut
        prev = span(legs[i]['pad'], d['at'] - 1, 1)           # (a leg played from its start has none: the playthrough's own keys)
        P.walkscript = lambda p: ','.join('H%X:%d' % (mk, n) if mk else '.:%d' % n for mk, n in prev + p)
    try:
        _, pics = P.port_timeline(start, pad)
    finally:
        P.walkscript = keys
    c = P.oracle_picture(start, pad)
    if not pics:
        print('fieldpicture: %s leg %s: the port drew no picture' % (name, legs[i]['name'].split('_')[0]))
        return 1
    k, n = P.picture_grade(c, pics)
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
