#!/usr/bin/env python3
r"""A fight's pictures frame by frame, the port against the cartridge from one recipe state.

    python tools/battle_picture_seq_cmp.py [name ...]

tools/field_picture_cmp.py grades ONE picture at the port frame that fits it best. A fight's picture is made of two
kinds of things with two latencies (disasm/notes/battle_picture_lags.md): what the NMI uploads -- the OAM, the strip's
tiles, the palettes -- is on the screen a frame after it was computed, and what HDMA reads from the shadows the frame's
own service writes -- the mosaic, the bands, the effect registers -- in the frame itself. A port that draws both from
the same instant fits one picture at one lag and its neighbour at another, and no single picture shows it. This tool
plays a recipe's state under its own pad on both sides and compares EVERY whole picture of a span at ONE port lag:

    battlepicseq: <name> <state> f<a>-f<b>: <n> of <m> whole frames are the cartridge's at port <lag>; not: f<x> ...

Regression checks: the `battle-picture-seq-*` family (docs/TESTING.md).
"""
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.environ.get('BATTLEPICSEQ_TMP') or os.path.join(ROOT, 'build', '_battlepicseq')
SCRATCH = os.path.join(WORK, 'run%d' % os.getpid())           # (one run's own files: the suite runs checks side by side)

# name: the recipe's state, the pad from it (button word, frames), the cartridge's pictures [a, b] and the port's lag.
SEQUENCES = {
    # The forest's fight run away from (the recipe: walk left into the Hetakes, hold L + R). f840-f1140 is the flee:
    # the Hetakes walking under the "Ran away" box (their feet stepping every fourth frame while the box's band opens
    # and closes), the run-off (the strip repainted without its menu), the mosaic's rise and fall, the two fade
    # records stepping on alternate frames -- the OBJ palettes' (uploaded) against the mosaic (read live). With the
    # fight's sprites and strip drawn as the NMI uploaded them every picture is the cartridge's at one lag; drawn from
    # the frame's own state, 51 of these 301 fit another lag or none (every frame a Hetake stepped; from f1000 every
    # other picture, the Hetakes' palette a step ahead of the mosaic).
    'forest-flee': dict(state='saves/battle_re/forest_holdleft_pre.state', pad=[(0x40, 120), (0, 230), (0xC00, 900)],
                        a=840, b=1140, lag=-1),
    # GATO's fight left alone (the fair's recipe: A starts it): the party and GATO idle, their cells changing every few
    # frames -- poses the MAIN LOOP sets, which the frame service's compose reads a frame later than the feet its own
    # movers wrote (CF/E778 JSR CF/E83A runs before the frame's main-loop iteration). Drawn from the frame's own state a
    # cell was two pictures ahead of the bands (104 of these 241 pictures fitted); from a copy taken at the frame's
    # END, one (161).
    'gato-idle': dict(state='saves/battle_re/gato_wait.state', pad=[(0x100, 8)], a=600, b=840, lag=-1),
    # The forest's command phase, a Hetake's turn: its animation script publishes a new pose on row f439 (`$990A` 03, the
    # pose machine C1/42C4's C1/4307 `JSR C1/1C4A`) and its travel starts on f444. The script is the main loop's, so
    # the pose is in the NEXT frame's mirror -- on the cartridge's picture f442, a picture after a foot of the same
    # row. The port steps an action's script and the service's engines together (battleAdvanceAction), ahead of its
    # compose's copy: battleOamPosesKeep puts the poses of the frame before back into the copy.
    'forest-turn-pose': dict(state='saves/battle_re/forest_holdleft_pre.state', pad=[(0x40, 120), (0, 230), (0xC00, 900)],
                             a=436, b=448, lag=-1),
    # The forest's command phase whole: two Hetakes hop to Crono, hit him and hop back. More of the main loop's state
    # the mirror has a frame late: the ground shadow's flag (ops D0 / D1; f442), the damage numbers' spawn and rise
    # (f521-f545, f578, f741-f765, f798), the foot a script's teleport snaps to the slide's goal (C1/561F; f717). And
    # the victim's white flash ends with its mask (f476, f687): the single-victim view of it is as uploaded too.
    'forest-commands': dict(state='saves/battle_re/forest_holdleft_pre.state', pad=[(0x40, 120), (0, 230), (0xC00, 900)],
                            a=300, b=840, lag=-1),
    # The forest's boot, from the fight's second picture to the command phase: the band's opening ladder (the seed
    # FF 17 stands a whole frame: CD/04FD seeds after its wait, past that frame's NMI), the settled tables two
    # pictures after the open's last step (D1/FE4C is the V-IRQ's and runs on an even `$7C` only: f244), the PCs'
    # ready poses a picture after the entrance ladder publishes them (C1/369A is the main thread's: f246-f278).
    'forest-boot': dict(state='saves/battle_re/forest_holdleft_pre.state', pad=[(0x40, 120), (0, 230), (0xC00, 900)],
                        a=174, b=300, lag=-1),
    # GATO's fight left alone, from its boot through his first attack to the frame before his song: the boot as the
    # forest's, and the attack's pre-roll -- before the script's first playAnim the ROM holds the previous publish,
    # the idle cell through picture f460 (the port's fallback ladder had him reared up from f438).
    'gato-first-attack': dict(state='saves/battle_re/gato_wait.state', pad=[(0x100, 8)], a=232, b=1008, lag=-1),
    # GATO's song, from its pre-roll to the frame before its damage lands: the arena darkened and lit by the beam
    # (BG3 on the sub screen added to BG1 / BG2). A BG1 / BG2 priority tile is under that math like any other pixel
    # (the tree's canopy and the tent's edge at the cone's top: 443 px a picture from the raw tiles), and the fair's
    # pennants step on frames the song's records fade the page: a fight's tile DMAs are held a frame as DMAs.
    'gato-song': dict(state='saves/battle_re/gato_wait.state', pad=[(0x100, 8)], a=1009, b=1315, lag=-1),
    # The song's teardown (CD/0453, rows f1306-f1327) and the turns after it: each teardown frame ends in CD/3E75 --
    # the NMI, THEN the menu pass C1/10E3 -- so a rebuild request the service raises is consumed in its own frame and
    # a gauge's repaint is on the next picture (f1316: the tile `$0DAA` 78 -> 77 on row f1314).
    'gato-song-end': dict(state='saves/battle_re/gato_wait.state', pad=[(0x100, 8)], a=1300, b=1420, lag=-1),
    # GATO's fight WON (A mashed, four frames on and four off): Crono's and Marle's attacks, the victory, the results
    # and the exit, to the fight's last picture. A damage number is its combatant's -- the compose lays its row at
    # the head of that combatant's entry (CF/EDB8 -> CF/EE9F), so Crono, standing in front of GATO, covers the `17`
    # of his own hit (f448-f456, f1468-f1478). Marle's bolt is an effect object a mover flies and the script takes
    # back to the bow: the script's point is on the screen a picture after a mover's (f830, f1911). And the shot's
    # spark and rings are those objects and nothing else (a baked copy beside them: f1917-f1931).
    'gato-won': dict(state='saves/battle_re/gato_wait.state', pad=[(0x100, 4), (0, 4)] * 300, a=232, b=2206, lag=-1),
}


def cart_rgb(path):
    o = open(path, 'rb').read()
    c = bytearray()
    for y in range(224):
        rr = (y + 8) * 1024
        row = o[rr * 4:(rr + 512) * 4]
        c += bytes(row[k] for x in range(256) for k in (x * 8 + 3, x * 8 + 2, x * 8 + 1))
    return bytes(c)


def port_rgb(path):
    if not os.path.exists(path):
        return None
    d = open(path, 'rb').read()
    pw = struct.unpack('<I', d[:4])[0]
    return b''.join(d[8 + y * pw * 3:8 + y * pw * 3 + 768] for y in range(224))


def run(name):
    d = SEQUENCES[name]
    os.makedirs(SCRATCH, exist_ok=True)
    P.TMP = SCRATCH                                    # (P.wram_of's files: the suite runs these checks side by side)
    st = os.path.join(ROOT, d['state'])
    a, b, lag = d['a'], d['b'], d['lag']
    n = b + 4
    pad, left = [], n
    for btn, q in d['pad']:
        q = min(q, left)
        if q > 0:
            pad.append((btn, q)); left -= q
    if left > 0:
        pad.append((0, left))
    w = P.wram_of(st)
    lw = os.path.join(SCRATCH, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=st, CT_WS_BATTLE_LEAD='1', CT_SHOTS=os.path.join(SCRATCH, 'po'),
               CT_SHOTS_AT=','.join(str(f + lag) for f in range(a, b + 1)), CT_SHOTS_EXIT='1', CT_SRAM_FRESH='1',
               CT_SRAM=os.path.join(SCRATCH, 'port.srm'))
    wf = os.path.join(SCRATCH, 'walk.txt')
    open(wf, 'w').write(P.walkscript(pad))
    subprocess.run([P.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw, '--walkfile', wf],
                   cwd=ROOT, env=env, capture_output=True)
    envc = dict(P.RULE, PEEK_SHOTS=os.path.join(SCRATCH, 'co'), PEEK_SHOTS_AT=','.join(str(f) for f in range(a, b + 1)))
    subprocess.run([P.CT, '--peek', st, os.path.join(SCRATCH, 'co_end.raw'), '--frames', str(n), '--btnscript',
                    P.btnscript(pad)], cwd=ROOT, env=envc, capture_output=True)
    same, bad, missing = 0, [], 0
    for f in range(a, b + 1):
        cp = os.path.join(SCRATCH, 'co_f%d.raw' % f)
        q = port_rgb(os.path.join(SCRATCH, 'po_f%d.raw' % (f + lag)))
        if not os.path.exists(cp) or q is None:
            missing += 1
            continue
        if cart_rgb(cp) == q:
            same += 1
        else:
            bad.append(f)
    if missing:
        print('battlepicseq: %s: %d of %d pictures were not drawn' % (name, missing, b - a + 1))
        return 1
    tail = ('; not: ' + ' '.join('f%d' % f for f in bad[:12]) + (' ...' if len(bad) > 12 else '')) if bad else ''
    print("battlepicseq: %s %s f%d-f%d: %d of %d whole frames are the cartridge's at port %+d%s" % (
        name, os.path.basename(d['state']), a, b, same, b - a + 1, lag, tail))
    return 0


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('--')]
    bad = [x for x in args if x not in SEQUENCES]
    if bad:
        print('battlepicseq: no sequence named %s (known: %s)' % (', '.join(bad), ', '.join(sorted(SEQUENCES))))
        return 1
    rc = 0
    try:
        for name in args or sorted(SEQUENCES):
            rc |= run(name)
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
