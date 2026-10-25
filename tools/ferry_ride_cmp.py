#!/usr/bin/env python3
r"""The ferry, both directions, port against the cartridge under the rule: the crossing frame for frame, and where it lands.

    python tools/ferry_ride_cmp.py [truce|porre|both]

Both ticket offices pay with the same `E0 F0 03 3F 28` (loc $1F0 at the Truce office's entrance cell 63,40) after setting a
direction bit in the story page -- scene 18 (Truce) `65 82 F0` = $7F:01F0 bit 2, scene 56 (Porre) `65 83 F0` = bit 3 --
and world 0's own bootstrap ($7F:0400, reading the bits through C2/26D6's copy at $1BA7) plays the crossing and ends it
with the script's op $05 into the far office. Nothing of the interactive set is spawned: no lead ($3404), no entrance
scanner ($3264), no name banner ($5628).

  cartridge: build/ct.exe --peek from the pay prompt (saves/ferry_re/truce_pay_prompt.state, porre_pay_prompt.state:
             the rule, CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1), A to pay (Porre: and A to close its "Here you go")
  port:      build/Belthasar.exe --loadwram from the office before the talk (office_at_clerk / porre_office_at_counter
             .lakewram), --talk + --choose/--select (Truce) or a --walkscript (Porre)

Both write a 128x112 thumbnail every frame. The two sides are aligned on the frame each one first shows the destination
office (op $05's load), and every overworld frame before it is compared (a pixel differs when a channel differs by more
than 24):

    ferry <dir>: -> loc <L> (cartridge <Lc>): overworld <n> frames, <k> differ [first c<f>]; lit->fade c<a> p<b>

`lit->fade` is the crossing's length on each side (first lit overworld frame to the first black one). Until 2026-09-29 the
port installed a captured Truce->Porre boat over the bootstrap object: 1380 black frames, the "Ticket Office" banner over
the whole crossing, a heuristic drop onto the Porre pier, and the Porre pay replaying the Truce ride.
The saves/ferry_re states and seeds are how they were made: disasm/notes/ow_ferry.md.
"""
import os
import struct
import subprocess
import sys

# tools/dis.py would shadow the standard library's `dis`, which numpy's import chain needs (as tools/attract_divergence.py).
sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_ferry')
REC = 6 + 128 * 112 * 3

RUNS = {
    # dir: (oracle state, oracle btnscript, oracle frames, port scene, port seed, port drive args, port frames, dest loc)
    'truce': ('saves/ferry_re/truce_pay_prompt.state', '100:4,0:1950', 'saves/ferry_re/office_at_clerk.lakewram', 18,
              ['--talk', '8', '--choose', '0', '--select'], 2050, 0x038),
    # (the Porre crossing also HOLDS the d-pad and presses A mid-sail on both sides: nothing reads the pad without a lead
    #  in free roam -- C2/3567 `LDA $027C / CMP #01` -- so the cartridge ignores them, and the port must too)
    'porre': ('saves/ferry_re/porre_pay_prompt.state', '100:4,0:60,100:4,0:540,80:120,0:60,20:120,100:4,0:1500',
              'saves/ferry_re/porre_office_at_counter.lakewram', 56,
              ['--walkscript', 'r:2,.:18,a:1,.:119,a:1,.:119,a:1,.:540,r:120,.:60,d:120,a:1,.:2000'], 2500, 0x012),
}


def load(path):
    d = open(path, 'rb').read()
    out = {}
    for i in range(len(d) // REC):
        f, loc = struct.unpack('<IH', d[i * REC:i * REC + 6])
        out[f] = (loc, np.frombuffer(d[i * REC + 6:(i + 1) * REC], np.uint8).reshape(112, 128, 3).astype(np.int16))
    return out


def lit(img):
    return img.mean() > 2


def run(name):
    state, script, seed, scene, drive, pframes, dest = RUNS[name]
    os.makedirs(OUT, exist_ok=True)
    ct, pt = os.path.join(OUT, name + '_cart.bin'), os.path.join(OUT, name + '_port.bin')
    for p in (ct, pt):
        if os.path.exists(p):
            os.remove(p)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1', PEEK_THUMBS=ct, PEEK_THUMBS_EVERY='1')
    subprocess.run([CT, '--peek', state, os.path.join(OUT, name + '_c.raw'), '--btnscript', script], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_SHOTS_THUMBS=pt,
               CT_SHOTS_THUMBS_EVERY='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(scene), '--loadwram', seed] + drive +
                   ['--selftest', os.path.join(OUT, name + '_p.raw'), '--frames', str(pframes)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(ct) or not os.path.exists(pt):
        print('ferry %s: FAILED -- a side wrote no thumbnails' % name)
        return
    C, P = load(ct), load(pt)
    world = 0x1F0
    # each side's destination: the first non-world frame after its first world frame
    def landing(D):
        w0 = min((f for f in D if D[f][0] == world), default=None)
        if w0 is None:
            return None, None
        after = [f for f in sorted(D) if f > w0 and D[f][0] != world]
        return w0, (after[0] if after else None)
    cw0, cd = landing(C)
    pw0, pd = landing(P)
    if cd is None or pd is None:
        print('ferry %s: FAILED -- no landing (cartridge world f%s dest f%s, port world f%s dest f%s)'
              % (name, cw0, cd, pw0, pd))
        return
    off = pd - cd
    n = k = 0
    first = None
    for f in range(cw0, cd):
        if f in C and f + off in P and C[f][0] == world:
            n += 1
            if (np.abs(C[f][1] - P[f + off][1]).max(axis=2) > 24).any():
                k += 1
                first = f if first is None else first
    def crossing(D, w0, d):              # the world's own fade-in to its fade-out (the entry's first frames can still
        blk = min((f for f in range(w0, d) if f in D and not lit(D[f][1])), default=None)   # show the field's picture)
        on = min((f for f in range(blk, d) if f in D and lit(D[f][1])), default=None) if blk is not None else None
        off_ = min((f for f in range(on, d) if f in D and not lit(D[f][1])), default=None) if on is not None else None
        return (off_ - on) if on is not None and off_ is not None else -1
    print('ferry %s: -> loc %03X (cartridge %03X): overworld %d frames, %d differ%s; lit->fade c%d p%d'
          % (name, P[pd][0], C[cd][0], n, k, (' [first c%d]' % first) if first is not None else '',
             crossing(C, cw0, cd), crossing(P, pw0, pd)))
    if C[cd][0] != dest:
        print('ferry %s: NOTE the cartridge landed in %03X, not the expected %03X' % (name, C[cd][0], dest))


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else 'both'
    for name in (('truce', 'porre') if which == 'both' else (which,)):
        run(name)


if __name__ == '__main__':
    main()
