#!/usr/bin/env python3
r"""The attract's X-Strike (loc 0AD's demo battle) against the LOGICAL-FRAME oracle -- the zelda3 model's reference
(CLAUDE.md, the ONE exception; disasm/notes/oracle_logical_frames.md).

    python tools/xstrike_logical_cmp.py

The cartridge: saves/attract_demo_battle_f1850.state, `ct.exe --monitor 7E5DBD:16,7EA013:1 --diff` for 300 frames,
once with CT_ORACLE_LOGICAL=1 and once in real time. The port: the attract from scene 433 (`--selftest --frames 3832`,
CT_BATTLE_TRACE + CT_BATTLE_ASCTRACE + CT_BATTLE_ASCIP), its tech `sel=42` action. Three numbers per side:

  * the PRE-ROLL: the frame `$A013` turns 1 (the port's `pc-act-tech`) to the frame the script's first cursor moves
    (track 0 off its header, `$5DBD` on the cartridge, the first `ascip` line on the port);
  * the VICTIM TRACK t3 (CE/3D08 and its super CE/D428): each of its cursor steps as an op-frame (frames since that
    first cursor move), paired in order -- the track the lag frames used to bend (the real cartridge's body of C1/40B0
    misses vsync on f122/127/134/158/162...);
  * the SPAN, `$A013` 1 -> 0 (the port's act-end `span=`).

Tracks 0 and 1 are not graded here: their cursors show the ROM's `ip` parked ON a waiting op where the port shows it
past it, so the two sides print different addresses on the same frames (a representation, not a timing).
"""
import os
import re
import subprocess
import sys

CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/ct_field.exe')
STATE = 'saves/attract_demo_battle_f1850.state'


def oracle(logical):
    env = dict(os.environ)
    env.pop('CT_ORACLE_LOGICAL', None)
    if logical:
        env['CT_ORACLE_LOGICAL'] = '1'
    r = subprocess.run([CT, '--monitor', '7E5DBD:16,7EA013:1', '--from', STATE, '--frames', '300', '--diff'],
                       env=env, capture_output=True, text=True, errors='replace')
    rise = fall = None
    cur = prev = None
    steps = []
    for l in (r.stdout + r.stderr).splitlines():
        m = re.match(r'\s*f(\d+)\s+7E5DBD=([0-9A-F]{32})\*?\s+7EA013=0([01])(\*?)', l)
        if not m:
            continue
        f = int(m.group(1))
        if m.group(4) == '*':
            if m.group(3) == '1' and rise is None:
                rise = f
            elif m.group(3) == '0' and rise is not None and fall is None:
                fall = f
        h = m.group(2)
        cur = [h[i * 8 + 2:i * 8 + 4] + h[i * 8:i * 8 + 2] for i in range(4)]
        if prev is not None and rise is not None and fall is None:
            for t in range(4):
                if cur[t] != prev[t] and cur[t] != '0000' and prev[t] != '0000':
                    steps.append((f, t, cur[t]))
        prev = cur
    return rise, fall, steps


def port():
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_TRACE='1',
               CT_BATTLE_ASCTRACE='1', CT_BATTLE_ASCIP='1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433', '--frames', '3832', '--selftest'],
                       env=env, capture_output=True, text=True, errors='replace')
    rise = span = None
    steps = []
    armed = False
    for l in (r.stdout + r.stderr).splitlines():
        m = re.match(r'battletrace f(\d+) pc-act-tech .*9877=42 ', l)
        if m and rise is None:
            rise = int(m.group(1))
            continue
        if rise is not None and re.match(r'asc arm f\d+ sel=42 ', l):
            armed = True
            continue
        m = re.match(r'ascip f(\d+) rel=\d+ t(\d) ([0-9A-F]{4})$', l)
        if armed and span is None and m:
            steps.append((int(m.group(1)), int(m.group(2)), m.group(3)))
            continue
        m = re.match(r'battletrace f\d+ act-end .*9877=42 .*span=(\d+)', l)
        if armed and m and span is None:
            span = int(m.group(1))
    return rise, span, steps


def main():
    p_rise, p_span, p_steps = port()
    if p_rise is None or not p_steps or p_span is None:
        sys.exit('xstrike-logical: the port never ran the X-Strike (rise %s, %d cursor steps, span %s)'
                 % (p_rise, len(p_steps), p_span))
    p_first = p_steps[0][0]
    p_t3 = [(f - p_first, c) for f, t, c in p_steps if t == 3]
    for tag, logical in (('logical', True), ('real', False)):
        rise, fall, steps = oracle(logical)
        if rise is None or fall is None or not steps:
            print('xstrike-logical: %s oracle: no X-Strike window (rise %s fall %s)' % (tag, rise, fall))
            continue
        first = steps[0][0]
        o_t3 = [(f - first, c) for f, t, c in steps if t == 3]
        same = sum(1 for a, b in zip(o_t3, p_t3) if a == b)
        n = max(len(o_t3), len(p_t3))
        print('xstrike-logical: %s oracle pre-roll %d, port %d' % (tag, first - rise, p_first - p_rise))
        print('xstrike-logical: %s oracle victim track t3 %d / %d cursor steps on the same op-frame' % (tag, same, n))
        print('xstrike-logical: %s oracle span %d, port %d' % (tag, fall - rise, p_span))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    CT = os.path.abspath('build/ct.exe')
    FIELD = os.path.abspath('build/ct_field.exe')
    main()
