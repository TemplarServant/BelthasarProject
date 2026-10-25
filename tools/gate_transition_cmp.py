#!/usr/bin/env python3
r"""The Telepod's whole transition -- scene 8 up to `DD 71 02 08 07 / FF 80`, the op's own fade, the gate warp's tunnel,
the black and the load, and the 600 A.D. arrival -- port against the cartridge FROM THE SAME WRAM.

    python tools/gate_transition_cmp.py

  * the cartridge: saves/field_re/scene8_gate_c2200.state (saves/field_re/scene8_gate_cutscene.state run 2200 frames under
    the rule with the cutscene's own A mash, `0:15,100:3,0:27,...` truncated at 2200 -- `--peek` runs a btnscript to its
    end), run on under the rule (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1) with the same mash, PEEK_THUMBS every frame;
  * the port: `--scene 8 --loadwram saves/field_re/scene8_gate_c2200.lakewram` (the same state's WRAM), no input, a
    thumbnail every frame (CT_SHOTS_THUMBS: the port's frame k is the cartridge's c2200 + k).
The port is seeded, not driven there: the check drive of field-scene8-gate-warp-flow enters scene 8 cold, with $0119 = 0
(no C0/2848 ramp), so its `FF 80` finds the screen "black" and skips its fade, and its crowd is still standing. From the
cartridge's own WRAM the two sides reach the DD on the same frame (the same monitor label: `--monitor` on the
cartridge, CT_FIELD_MEMMON on the port). Each stretch is graded at its own relation (a pixel differs when a channel
differs by more than 24 -- one INIDISP step does not; 128x112):
  * scene 8 and the arrival are the FIELD: the image of the logic P is the cartridge's thumbnail P+2 and the port's
    frame P (ct-frame-alignment-p2). The arrival is compared one frame later than that, P+1: its logic runs one frame
    behind the cartridge's (the iris's `$0139 = 1` at label 1065 against 1064 -- the load clock);
  * the op's fade is the field's too (C0/3FBB-3FD1 run in the field's frame): graded at P+2, and by the mean brightness,
    which sees an INIDISP step the pixel threshold does not;
  * the tunnel and the load are the warp stage's frames (src/seq_gwarp.c), from the one after the jump (`gatewarp: ... takes
    over at f<J>`), which the port presents as the cartridge's thumbnails (gwarp_cmp's relation).

  gate-transition: DD c<D>; scene 8 + the op's fade at P+2 c<B>..c<J>: <a> / <n> differ, mean within <d>;
                   tunnel + load c<J+1>..c<L-1>: <b> / <n> differ; arrival from the lit c<L> at P+1 (the load one frame
                   long): <k> / 300 differ, max <m> px

(disasm/notes/gate_warp_fd_family.md sec. 10.)
"""
import os, re, struct, subprocess, sys

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/field_re/scene8_gate_c2200.state'
SEED = 'saves/field_re/scene8_gate_c2200.lakewram'
BASE = 2200
N = 1400
REC = 6 + 128 * 112 * 3
OUT = os.path.join(ROOT, 'build', '_gatetransition')


def thumbs(path):
    d = open(path, 'rb').read(); out = {}
    for i in range(len(d) // REC):
        f, loc = struct.unpack('<IH', d[i * REC:i * REC + 6])
        out[f] = (loc, np.frombuffer(d[i * REC + 6:(i + 1) * REC], np.uint8).reshape(112, 128, 3).astype(np.int16))
    return out


def ndiff(a, b):
    return int((np.abs(a - b).max(axis=2) > 24).sum())


def main():
    os.chdir(ROOT); os.makedirs(OUT, exist_ok=True)
    ct, pt = os.path.join(OUT, 'cart.thumbs'), os.path.join(OUT, 'port.thumbs')
    for p in (ct, pt):
        if os.path.exists(p): os.remove(p)
    # the mash continues where the state left it: A on 15 + 30k, so the next press is c2215
    script = ','.join(['0:15'] + ['100:3,0:27'] * (N // 30 + 1))
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1', PEEK_THUMBS=ct, PEEK_THUMBS_EVERY='1')
    subprocess.run([os.path.abspath('build/ct.exe'), '--peek', STATE, os.path.join(OUT, 'c.raw'), '--btnscript', script],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', TB_AUTOADV='1',
               CT_SHOTS_THUMBS=pt, CT_SHOTS_THUMBS_EVERY='1', CT_FIELD_MEMMON='0100:2')
    log = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '8', '--loadwram',
                          SEED, '--selftest', os.path.join(OUT, 'p.raw'), '--frames', str(N)],
                         env=env, capture_output=True, text=True, errors='replace').stdout
    m = re.search(r'fmemmon f(\d+) loc=\w+\s+0100=7100\*', log)
    if not m or not os.path.exists(ct) or not os.path.exists(pt):
        print('gate-transition: FAIL -- no DD 71 on the port, or a side wrote no thumbnails'); return 1
    D = BASE + int(m.group(1))
    C, P = thumbs(ct), thumbs(pt)
    cf = lambda c: C.get(c - BASE)                      # cartridge frame c (its thumbs count from the state)
    pf = lambda c, s=0: P.get(c - BASE + s)             # the port's picture of the same frame
    # the arrival: the first lit frame after the black that follows the tunnel (the warp runs 880 frames after the fade)
    dark = min((c for c in range(D + 900, BASE + N) if cf(c) and cf(c)[1].mean() <= 2), default=None)
    L = None if dark is None else min((c for c in range(dark, BASE + N) if cf(c) and cf(c)[0] == 0x071 and cf(c)[1].mean() > 2),
                                      default=None)
    if L is None:
        print('gate-transition: FAIL -- the cartridge never lit loc 071'); return 1
    # The stage opens where the port says: `gatewarp: the native program takes over at f<J>` is the jump's frame (C0/3FD3);
    # its first picture is J+1, the cartridge's own frame. Up to J it is the field -- scene 8 and the op's own fade (C0/3FBB,
    # C0/3FC1's DEC per frame) -- graded at P+2, and by the mean brightness too: one INIDISP step is below the threshold.
    j = re.search(r'gatewarp: the native program takes over at f(\d+)', log)
    if not j:
        print('gate-transition: FAIL -- the port never opened the warp'); return 1
    J = BASE + int(j.group(1))
    pre = [ndiff(cf(c)[1], pf(c, -2)[1]) for c in range(BASE + 3, J + 1) if cf(c) and pf(c, -2)]  # the field's P+2
    dmean = max((abs(float(cf(c)[1].mean()) - float(pf(c, -2)[1].mean())) for c in range(BASE + 3, J + 1)
                 if cf(c) and pf(c, -2)), default=0.0)
    mid = [ndiff(cf(c)[1], pf(c)[1]) for c in range(J + 1, L) if cf(c) and pf(c)]                 # the stage's frames
    arr = [ndiff(cf(c)[1], pf(c, -1)[1]) for c in range(L, L + 300) if cf(c) and pf(c, -1)]       # the field, one late
    print('gate-transition: DD c%d; scene 8 + the op\'s fade at P+2 c%d..c%d: %d / %d differ, mean within %.1f; '
          'tunnel + load c%d..c%d: %d / %d differ; arrival from the lit c%d at P+1 (the load one frame long): %d / %d '
          'differ, max %d px'
          % (D, BASE + 3, J, sum(1 for v in pre if v), len(pre), dmean, J + 1, L - 1, sum(1 for v in mid if v),
             len(mid), L, sum(1 for v in arr if v), len(arr), max(arr) if arr else 0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
