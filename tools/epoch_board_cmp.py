#!/usr/bin/env python
"""The Epoch's boarding and lift-off on the overworld, the port against the cartridge (disasm/notes/ow_vehicle.md).

Jackie's ch24 (loc 1F6, the party standing on the parked Epoch, storyline D3): A once. The cartridge from its own state
(saves/downloaded/ctjackie/states/ch24.state, A at f60), the port booted from the same battery save (ct_field --boot,
CT_SRAM, the Load screen, A at f420). Graded:
  - the vehicle machine's timeline -- every change of `$027C $027E $0294` and of the Epoch's state ($42DD +2), counted
    from the frame `$027E` leaves 0 (oracle `--monitor`, port CT_OWVEHTRACE);
  - two pictures: the lift-off (port A+20 against cartridge A+22, the P+2 alignment; the era placard's box masked -- the
    port loads the save at another frame, so its placard counter $1BF7 starts lower) and the fade into loc 1DD (A+60 / A+62).
Prints `epoch-board: timeline <same|DIFFERENT> ...` and `epoch-board: picture A+<n> <px> px differ`.
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JK = os.path.join(ROOT, 'saves', 'downloaded', 'ctjackie')
OUT = os.path.join(ROOT, 'build', '_epoch_board')
PRESS_O, PRESS_P = 60, 420
PLACARD = (8, 48, 192, 208)               # x0, x1, y0, y1 of the era placard (port coordinates)


def oracle_timeline():
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', '7E027C:3,7E0294:1,7E0DB2:1', '--from',
                        os.path.join(JK, 'states', 'ch24.state'), '--btnscript', '0:%d,100:2,0:150' % PRESS_O,
                        '--frames', '210', '--diff'], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    ev = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s+f(\d+)\s+7E027C=([0-9A-F]{6})\*?\s+7E0294=([0-9A-F]{2})\*?\s+7E0DB2=([0-9A-F]{2})', l)
        if m:
            c = m.group(2)
            ev.append((int(m.group(1)), (c[0:2], c[4:6], m.group(3), m.group(4))))
    return ev


def port_run():
    os.makedirs(OUT, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SRAM=os.path.join(JK, 'srm', 'ch24.srm'), CT_OWVEHTRACE='1', CT_SHOTS=os.path.join(OUT, 'p'),
               CT_SHOTS_AT='%d,%d,%d' % (PRESS_P + 20, PRESS_P + 60, PRESS_P + 90), CT_SHOTS_EXIT='1')
    ws = 'n:133,s:1,n:125,a:1,n:%d,a:2,n:120' % (PRESS_P - 260)
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct_field.exe'), '--assets', 'ct_assets.dat', '--boot',
                        '--walkscript', ws], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    ev = []
    for l in r.stdout.splitlines():
        m = re.match(r'owveh: pass (\d+) 027C=(..) 027E=(..) .* 0294=(..) .*?(?:\S+:42DD/(..))?', l)
        if m:
            e = re.search(r':42DD/(..)', l)
            ev.append((int(m.group(1)), (m.group(2), m.group(3), m.group(4), e.group(1) if e else '--')))
    return ev


def rel(ev):
    """From the frame $027E leaves 0, each change as (+frames, ($027C, $027E, $0294, Epoch state))."""
    t0 = next((f for f, v in ev if v[1] != '00'), None)
    if t0 is None:
        return []
    out, last = [], None
    for f, v in ev:
        if f < t0 or v == last:
            continue
        out.append((f - t0, v)); last = v
    return out


def raw_rgb(path, oracle):
    d = open(path, 'rb').read()
    if oracle:                                       # 512x480 [pad,B,G,R], 2x doubled; 8 lines of border above
        return lambda x, y: tuple(d[((2 * (y + 8)) * 512 + 2 * x) * 4 + k] for k in (3, 2, 1))
    w, h = struct.unpack('<II', d[:8])
    return lambda x, y: tuple(d[8 + (y * w + x) * 3: 8 + (y * w + x) * 3 + 3])


def picture(n_port, n_oracle, mask):
    o_raw = os.path.join(OUT, 'o%d.raw' % n_oracle)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', os.path.join(JK, 'states', 'ch24.state'), o_raw,
                    '--btnscript', '0:%d,100:2,0:%d' % (PRESS_O, n_oracle - PRESS_O - 2)], cwd=ROOT, env=env, capture_output=True)
    po = raw_rgb(os.path.join(OUT, 'p_f%d.raw' % (PRESS_P + n_port)), False)
    oo = raw_rgb(o_raw, True)
    n = 0
    for y in range(8, 216):
        for x in range(256):
            if mask and mask[0] <= x < mask[1] and mask[2] <= y < mask[3]:
                continue
            n += po(x, y) != oo(x, y)
    return n


def main():
    o = rel(oracle_timeline())
    p = rel(port_run())
    same = o == p and len(o) > 0
    print('epoch-board: timeline %s -- %d changes; the cartridge %s' % (
        'same' if same else 'DIFFERENT', len(o), ' '.join('+%d:%s' % (d, '/'.join(v)) for d, v in o)))
    if not same:
        print('epoch-board:   port %s' % ' '.join('+%d:%s' % (d, '/'.join(v)) for d, v in p))
    print('epoch-board: picture A+20 (lift-off, placard masked) %d px differ' % picture(20, PRESS_O + 22, PLACARD))
    print('epoch-board: picture A+60 (the fade into 1DD) %d px differ' % picture(60, PRESS_O + 62, None))
    return 0


if __name__ == '__main__':
    sys.exit(main())
