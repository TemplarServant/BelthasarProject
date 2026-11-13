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
