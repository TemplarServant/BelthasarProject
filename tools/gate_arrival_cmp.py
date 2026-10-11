#!/usr/bin/env python3
r"""The Telepod's far end, port against the cartridge: after the gate warp reboots into 600 A.D. (loc 071), the gate opens
in the forest (op 0xFF 90 -> C0/21E1's iris, the window C3/0077 builds), Crono falls out of it, it closes, and he stands.

    python tools/gate_arrival_cmp.py [frames]

  * the cartridge: saves/field_re/scene8_gate_warp_f2300.state (the warp's hand-off at its f17, tools/gwarp_cmp.py) run on
    under the rule (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1), PEEK_THUMBS every frame, into loc 071;
  * the port: field-scene8-gate-warp-flow's drive, CT_SHOTS_THUMBS every frame, `gatewarp: the translated program takes
    over at f<F>` locating the warp.
The warp itself lines up at port = cartridge + (F - 18) (tools/gwarp_cmp.py). The ARRIVAL is aligned on its own: the offset
within two frames of the warp's that makes the most loc-071 frames identical -- the loc's load is where the two can part
(the field load clock). From the cartridge's first lit loc-071 frame, <frames> (default 300) are compared as 128x112
thumbnails (a pixel differs when a channel differs by more than 24):

  gate-arrival: warp +<W>, arrival +<A>; <n> frames from the lit c<L>: <k> differ, max <m> px, <t> px in all

(disasm/notes/field_iris_window.md.)
"""
import json, os, struct, subprocess, sys, re

# tools/dis.py would shadow the standard library's `dis`, which numpy's import chain needs (as tools/attract_divergence.py).
sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/field_re/scene8_gate_warp_f2300.state'
H = 18
REC = 6 + 128 * 112 * 3
OUT = os.path.join(ROOT, 'build', '_gatearrival')


def thumbs(path):
    d = open(path, 'rb').read(); out = {}
    for i in range(len(d) // REC):
        f, loc = struct.unpack('<IH', d[i * REC:i * REC + 6])
        out[f] = (loc, np.frombuffer(d[i * REC + 6:(i + 1) * REC], np.uint8).reshape(112, 128, 3).astype(np.int16))
    return out


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    os.chdir(ROOT); os.makedirs(OUT, exist_ok=True)
    chk = next(c for c in json.load(open('tools/regress_checks.json', encoding='utf-8'))['checks']
               if c['name'] == 'field-scene8-gate-warp-flow')
    argv = list(chk['argv']); fi = argv.index('--frames'); argv[fi + 1] = str(int(argv[fi + 1]) + n + 150)
    pt, ot = os.path.join(OUT, 'port.thumbs'), os.path.join(OUT, 'cart.thumbs')
    for p in (pt, ot):
        if os.path.exists(p): os.remove(p)
    env = dict(os.environ, **chk['env']); env.pop('CT_GWTRACE', None)
    env.update(CT_SHOTS_THUMBS=pt, CT_SHOTS_THUMBS_EVERY='1', CT_SHOTS_THUMBS_FROM='3300', CT_SHOTS_THUMBS_TO='9000')
    log = subprocess.run([os.path.abspath(argv[0])] + argv[1:], env=env, capture_output=True, text=True, errors='replace').stdout
    m = re.search(r'gatewarp: the translated program takes over at f(\d+)', log)
    k = re.search(r'gatewarp: op-0xFF 80 ran (\d+) frames', log)
    if not m or not k:
        print('gate-arrival: FAIL -- the port never ran the warp'); return 1
    F, N = int(m.group(1)), int(k.group(1))
    oenv = dict(os.environ, CT_ORACLE_VIRTUAL_BEAM='1', CT_ORACLE_LOGICAL='1', PEEK_THUMBS=ot, PEEK_THUMBS_EVERY='1')
    subprocess.run([os.path.abspath('build/ct.exe'), '--peek', STATE, os.path.join(OUT, 'c.raw'), '--frames', str(H + N + n + 80)],
                   env=oenv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    C, P = thumbs(ot), thumbs(pt)
    # $0100 already reads 0x071 through the tunnel (op 0xDD staged it before `FF 80`), so the arrival is the first lit
    # frame after the black that follows the warp's last frame (the hand-off H + its N frames).
    dark = min((f for f in C if f >= H + N and C[f][1].mean() <= 2), default=None)
    lit = None if dark is None else min((f for f in C if f > dark and C[f][0] == 0x071 and C[f][1].mean() > 2), default=None)
    if lit is None:
        print('gate-arrival: FAIL -- the cartridge never lit loc 071'); return 1
    W = F - H
    def score(off):
        out = []
        for c in range(lit, lit + n):
            if c in C and c + off in P:
                out.append(int((np.abs(C[c][1] - P[c + off][1]).max(axis=2) > 24).sum()))
        return out
    best = min((W - 2, W - 1, W, W + 1, W + 2), key=lambda o: (sum(1 for v in score(o) if v), sum(score(o))))
    s = score(best)
    print('gate-arrival: warp +%d, arrival +%d; %d frames from the lit c%d: %d differ, max %d px, %d px in all'
          % (W, best, len(s), lit, sum(1 for v in s if v), max(s) if s else 0, sum(s)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
