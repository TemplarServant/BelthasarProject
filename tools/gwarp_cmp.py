#!/usr/bin/env python3
"""gwarp_cmp.py -- the GATE WARP (op 0xFF 80, scene 8's Telepod), EVERY frame from its hand-off to its exit, the port's
native program (src/seq_gwarp.c: its logic and its picture through src/seq_pic.c) against the cartridge.

    python tools/gwarp_cmp.py [--realtime]

  * the cartridge: saves/field_re/scene8_gate_warp_f2300.state (FD:FFF1 at its f17, `ct.exe --xlatentries`), `--peek
    --frames 18+N` with PEEK_THUMBS_EVERY=1 -- UNDER THE RULE (CT_ORACLE_VIRTUAL_BEAM=1 CT_ORACLE_LOGICAL=1, as the port's
    stage runs) unless --realtime.
  * the port: field-scene8-gate-warp-flow's drive (scene 8, the Telepod flags, the A mash through the cutscene), with
    CT_SHOTS_THUMBS over the warp; `gatewarp: the native program takes over at f<F>` locates it, `ran <N> frames`
    ends it.
Port frame F + k is compared with cartridge frame 18 + k, k = 0 .. N-1 (the jump is in f17's vblank, see H below):

  gwarp-cmp: port f<F>..f<F+N-1> vs cartridge +18..+<17+N> <mode> -> <n> / <N> frames identical
"""
import json, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/field_re/scene8_gate_warp_f2300.state'
# FD:FFF1 runs on the state's f17 (`ct.exe --xlatentries`), in the vblank (v=239): that frame's picture is still the
# host's, so the program's FIRST picture is the cartridge's f18 -- the same relation entry 3 has against its real-time
# reference (saves/attract_entry3_handoff_f6750.state: C3:0000 f17, the port's first stage frame = +18, 88/88 frames).
H = 18
REC = 6 + 128 * 112 * 3


def thumbs(path):
    d = open(path, 'rb').read(); out = {}
    for i in range(0, len(d) - REC + 1, REC):
        out[struct.unpack('<I', d[i:i + 4])[0]] = d[i + 6:i + REC]
    return out


def main():
    os.chdir(ROOT)
    rt = '--realtime' in sys.argv
    chk = next(c for c in json.load(open('tools/regress_checks.json', encoding='utf-8'))['checks']
               if c['name'] == 'field-scene8-gate-warp-flow')
    pth = 'build/_gwarp_port.thumbs'
    if os.path.exists(pth): os.remove(pth)
    env = dict(os.environ, **chk['env'])
    env.pop('CT_GWTRACE', None)
    env.update(CT_SHOTS_THUMBS=pth, CT_SHOTS_THUMBS_EVERY='1', CT_SHOTS_THUMBS_FROM='1000', CT_SHOTS_THUMBS_TO='4000')
    log = subprocess.run([os.path.abspath(chk['argv'][0])] + chk['argv'][1:], env=env, capture_output=True, text=True,
                         errors='replace', timeout=1800).stdout
    m = re.search(r'gatewarp: the native program takes over at f(\d+)', log)
    n = re.search(r'gatewarp: op-0xFF 80 ran (\d+) frames', log)
    if not m or not n:
        print('gwarp-cmp: FAIL -- the port never ran the native warp'); return 1
    F, N = int(m.group(1)), int(n.group(1))
    ot = 'build/_gwarp_orc.raw'
    oenv = dict(os.environ, PEEK_THUMBS=ot + '.thumbs', PEEK_THUMBS_EVERY='1')
    if not rt: oenv.update(CT_ORACLE_VIRTUAL_BEAM='1', CT_ORACLE_LOGICAL='1')
    if os.path.exists(ot + '.thumbs'): os.remove(ot + '.thumbs')
    subprocess.run([os.path.abspath('build/ct.exe'), '--peek', STATE, ot, '--frames', str(H + N + 1)], env=oenv,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)
    pt, oth = thumbs(pth), thumbs(ot + '.thumbs')
    bad = [k for k in range(N) if pt.get(F + k) is None or oth.get(H + k) != pt.get(F + k)]
    print('gwarp-cmp: port f%d..f%d vs cartridge +%d..+%d %s -> %d / %d frames identical%s'
          % (F, F + N - 1, H, H + N - 1, 'in real time' if rt else 'under the rule', N - len(bad), N,
             (', differ at +' + ' +'.join(str(H + k) for k in bad[:16]) + (' ...' if len(bad) > 16 else '')) if bad else ''))
    for p in (pth, ot + '.thumbs'):
        if os.path.exists(p): os.remove(p)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
