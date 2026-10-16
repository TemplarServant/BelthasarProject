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

`--flight`: the same save driven on (A every 32 frames through the Black Omen's rising, loc 06B and 1DD) to the free flight
on 1F6 (the cartridge's f3073), then Right 40 frames, Down 16, A (the landing). The port presses the flight 358 frames after
the cartridge -- its field scenes in between run a few frames apart, so its world is 358 behind there, and the presses are
placed on the world's clock. Graded: every change of `$027C $027E $0294`, the Epoch's position `$0290/$0292` and its state,
from the first step; three pictures (steps, the turn, the landing).
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


FLIGHT_O = ['0:60', '100:2'] + ['0:30', '100:2'] * 93 + ['0:44', '80:40', '20:16', '0:20', '100:2', '0:150']
FLIGHT_P = ['n:133', 's:1', 'n:125', 'a:1', 'n:160', 'a:2'] + ['n:30', 'a:2'] * 93 + ['n:42', 'r:40', 'd:16', 'n:20', 'a:2', 'n:150']
FLIGHT_SHIFT = 358


def flight(tag='epoch-flight', drive_o=None, drive_p=None, anchor='04', shots=(3100, 3130, 3200, 3300), frames=3330):
    drive_o = drive_o or FLIGHT_O; drive_p = drive_p or FLIGHT_P
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', '7E027C:3,7E0294:1,7E0290:4,7E0DB2:1', '--from',
                        os.path.join(JK, 'states', 'ch24.state'), '--btnscript', ','.join(drive_o), '--frames', str(frames),
                        '--diff'], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    oe = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s+f(\d+)\s+7E027C=([0-9A-F]{6})\*?\s+7E0294=([0-9A-F]{2})\*?\s+7E0290=([0-9A-F]{8})\*?\s+7E0DB2=([0-9A-F]{2})', l)
        if m:
            c, q = m.group(2), m.group(4)
            oe.append((int(m.group(1)), (c[0:2], c[4:6], m.group(3), q[2:4] + q[0:2], q[6:8] + q[4:6], m.group(5))))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SRAM=os.path.join(JK, 'srm', 'ch24.srm'), CT_OWVEHTRACE='1', CT_SHOTS=os.path.join(OUT, tag),
               CT_SHOTS_AT=','.join(str(f + FLIGHT_SHIFT) for f in shots), CT_SHOTS_EXIT='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct_field.exe'), '--assets', 'ct_assets.dat', '--boot', '--walkscript',
                        ','.join(drive_p)], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    pe = []
    for l in r.stdout.splitlines():
        m = re.match(r'owveh: pass (\d+) 027C=(..) 027E=(..) .* 0294=(..) 0290=(....),(....)', l)
        e = re.search(r':42DD/(..)', l)
        if m and e:
            pe.append((int(m.group(1)), (m.group(2), m.group(3), m.group(4), m.group(5), m.group(6), e.group(1))))
    def fromstep(ev):
        t0 = next((f for f, v in ev if v[5] == anchor), None)
        out, last = [], None
        for f, v in ev:
            if t0 is None or f < t0 or v == last:
                continue
            out.append((f - t0, v)); last = v
        return out[:80], t0
    if os.environ.get('EPOCH_DUMP'):
        print('O', fromstep(oe)[0]); print('P', fromstep(pe)[0])
    o, ot = fromstep(oe)
    p, _ = fromstep(pe)
    if ot is not None:                                # the same window on both sides: up to the last picture
        o = [e for e in o if e[0] <= max(shots) - ot]; p = [e for e in p if e[0] <= max(shots) - ot]
    same = o == p and len(o) > 0
    print(tag + ': timeline %s -- %d changes from the first step (cartridge f%s); last %s' % (
        'same' if same else 'DIFFERENT', len(o), ot, ('+%d:%s' % (o[-1][0], '/'.join(o[-1][1]))) if o else '-'))
    if not same:
        if len(o) != len(p): print(tag + ':   %d changes on the cartridge, %d in the port' % (len(o), len(p)))
        for k, (a, b) in enumerate(zip(o, p)):
            if a != b:
                print(tag + ':   first difference #%d cartridge +%d:%s port +%d:%s' % (k, a[0], '/'.join(a[1]), b[0], '/'.join(b[1])))
                break
    for f in shots:                                  # the port's picture P is the cartridge's --peek P + 2
        o_raw = os.path.join(OUT, '%s_o%d.raw' % (tag, f))
        bs, t = [], 0
        for it in drive_o:
            b, n = it.split(':'); n = int(n)
            if t + n >= f + 2: bs.append('%s:%d' % (b, f + 2 - t)); break
            bs.append(it); t += n
        subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', os.path.join(JK, 'states', 'ch24.state'), o_raw,
                        '--btnscript', ','.join(bs)], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
        po = raw_rgb(os.path.join(OUT, '%s_f%d.raw' % (tag, f + FLIGHT_SHIFT)), False)
        oo = raw_rgb(o_raw, True)
        n = sum(po(x, y) != oo(x, y) for y in range(8, 216) for x in range(256))
        print(tag + ': picture port f%d / cartridge f%d %d px differ' % (f + FLIGHT_SHIFT, f + 2, n))
    return 0


def omen():
    """The Black Omen rising on 1F6 after the first flight (C2:0632: C2/7AFC / 7B20's pictures, C2/7C56 / 7C97's HDMA wave on
    BG2, C2/7CD8 / 7D5E's steps; TM 15 / TS 12, BG3 added to the sub screen): five pictures, the port 360 frames on."""
    os.makedirs(OUT, exist_ok=True)
    drive_o = ['0:60', '100:2'] + ['0:30', '100:2'] * 50
    drive_p = ['n:133', 's:1', 'n:125', 'a:1', 'n:160', 'a:2'] + ['n:30', 'a:2'] * 50
    shots = (700, 900, 1100, 1300, 1500)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SRAM=os.path.join(JK, 'srm', 'ch24.srm'), CT_SHOTS=os.path.join(OUT, 'om'),
               CT_SHOTS_AT=','.join(str(f + 360) for f in shots), CT_SHOTS_EXIT='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct_field.exe'), '--assets', 'ct_assets.dat', '--boot', '--walkscript',
                        ','.join(drive_p)], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    und = sorted({l.strip() for l in (r.stdout + r.stderr).splitlines() if 'UNDERIVED' in l})
    print('epoch-omen: %d underived native(s)%s' % (len(und), (' -- ' + und[0]) if und else ''))
    for f in shots:
        o_raw = os.path.join(OUT, 'oo%d.raw' % f)
        bs, t = [], 0
        for it in drive_o:
            b, n = it.split(':'); n = int(n)
            if t + n >= f: bs.append('%s:%d' % (b, f - t)); break
            bs.append(it); t += n
        subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', os.path.join(JK, 'states', 'ch24.state'), o_raw,
                        '--btnscript', ','.join(bs)], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
        po = raw_rgb(os.path.join(OUT, 'om_f%d.raw' % (f + 360)), False)
        oo = raw_rgb(o_raw, True)
        n = sum(po(x, y) != oo(x, y) for y in range(0, 224) for x in range(256))
        print('epoch-omen: picture port f%d / cartridge f%d %d px differ' % (f + 360, f, n))
    return 0


def main():
    if '--omen' in sys.argv:
        return omen()
    if '--flight' in sys.argv:
        os.makedirs(OUT, exist_ok=True)
        return flight()
    if '--land' in sys.argv:                          # A every 32 frames: the Epoch lands at once, the party steps out, boards
        os.makedirs(OUT, exist_ok=True)               # again and lifts off (the cartridge's f3101-f3229)
        return flight('epoch-land', ['0:60', '100:2'] + ['0:30', '100:2'] * 110,
                      ['n:133', 's:1', 'n:125', 'a:1', 'n:160', 'a:2'] + ['n:30', 'a:2'] * 93 + ['n:28', 'a:2'] + ['n:30', 'a:2'] * 16,
                      anchor='05', shots=(3110, 3152, 3170, 3200, 3330), frames=3330)
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
