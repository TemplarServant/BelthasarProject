#!/usr/bin/env python
"""The Epoch's boarding and lift-off on the overworld, the port against the cartridge (disasm/notes/ow_vehicle.md).

Jackie's ch24 (loc 1F6, the party standing on the parked Epoch, storyline D3): A once. The cartridge from its own state
(saves/downloaded/ctjackie/states/ch24.state, A at f60), the port booted from the same battery save (Belthasar --boot,
CT_SRAM, the Load screen, A at f420). Graded:
  - the vehicle machine's timeline -- every change of `$027C $027E $0294` and of the Epoch's state ($42DD +2), counted
    from the frame `$027E` leaves 0 (oracle `--monitor`, port CT_OWVEHTRACE);
  - two pictures: the lift-off (port A+20 against cartridge A+22, the P+2 alignment; the era placard's box masked -- the
    port loads the save at another frame, so its placard counter $1BF7 starts lower) and the fade into loc 1DD (A+60 / A+62).
Prints `epoch-board: timeline <same|DIFFERENT> ...` and `epoch-board: picture A+<n> <px> px differ`.
Regression checks: ow-epoch-boarding-and-liftoff-vs-oracle and the other ow-*-vs-oracle checks that call it.

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
PRESS_O, PRESS_P = 60, 420 + 65   # +65: --boot starts at the power-on (boot_loader.md sec. 10), its title 65 frames later
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
    ws = 'n:198,s:1,n:125,a:1,n:%d,a:2,n:120' % (PRESS_P - 325)
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot',
                        '--walkscript', ws], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    ev = []
    for l in r.stdout.splitlines():
        m = re.match(r'owveh: pass (\d+) (?:f\d+ )?027C=(..) 027E=(..) .* 0294=(..) .*?(?:\S+:42DD/(..))?', l)
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
FLIGHT_P = ['n:198', 's:1', 'n:125', 'a:1', 'n:160', 'a:2'] + ['n:30', 'a:2'] * 93 + ['n:42', 'r:40', 'd:16', 'n:20', 'a:2', 'n:150']
FLIGHT_SHIFT = 358 + 65   # (+65: the power-on's title)


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
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot', '--walkscript',
                        ','.join(drive_p)], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    pe = []
    for l in r.stdout.splitlines():
        m = re.match(r'owveh: pass (\d+) (?:f\d+ )?027C=(..) 027E=(..) .* 0294=(..) 0290=(....),(....)', l)
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
    BG2, C2/7CD8 / 7D5E's steps; TM 15 / TS 12, BG3 added to the sub screen): five pictures, the port 361 frames on -- the
    world's P + 1 (its WRAM runs 362 frames behind here: the wave buffer's tail at cartridge f698 = port f1060)."""
    os.makedirs(OUT, exist_ok=True)
    drive_o = ['0:60', '100:2'] + ['0:30', '100:2'] * 50
    drive_p = ['n:198', 's:1', 'n:125', 'a:1', 'n:160', 'a:2'] + ['n:30', 'a:2'] * 50
    shots = (700, 900, 1100, 1300, 1500)
    # (+65: the power-on title; -3: the 1DD -> 1F6 world load walked from the ROM, 37 black frames as the cartridge's)
    OMEN_BOOT = 65 - 3
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SRAM=os.path.join(JK, 'srm', 'ch24.srm'), CT_SHOTS=os.path.join(OUT, 'om'),
               CT_SHOTS_AT=','.join(str(f + 361 + OMEN_BOOT) for f in shots), CT_SHOTS_EXIT='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot', '--walkscript',
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
        po = raw_rgb(os.path.join(OUT, 'om_f%d.raw' % (f + 361 + OMEN_BOOT)), False)
        oo = raw_rgb(o_raw, True)
        n = sum(po(x, y) != oo(x, y) for y in range(0, 224) for x in range(256))
        print('epoch-omen: picture port f%d / cartridge f%d %d px differ' % (f + 361 + OMEN_BOOT, f, n))
    return 0


GAUGE_O = ['0:60', '100:2'] + ['0:30', '100:2'] * 93 + ['0:44', '2:2', '0:98', '10:2', '0:88', '100:2', '0:520']
GAUGE_SHOTS = [3182, 3202, 3242, 3580, 3610]   # the gauge (needle turning, turning, still), the arrival on 1F1 (P+2)
GAUGE_P = ['n:198', 's:1', 'n:125', 'a:1', 'n:160', 'a:2'] + ['n:30', 'a:2'] * 93 + ['n:143', 'u:2', 'n:88', 'a:2', 'n:600']


def gauge():
    """The time gauge from the free flight (C2/261D -> C2/6A34, disasm/notes/ow_time_gauge.md): Y (the cartridge's f3082,
    the port's f3440 on the world's clock), Up (12000 B.C. -> 600 A.D.), A: the trip, loc 1F1 and the arrival (C2/4387).
    Every change of `$0100 $027C $0294 $0290/$0292 $02AF $1BB7`, counted from the frame `$027C` becomes 8."""
    os.makedirs(OUT, exist_ok=True)
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor',
                        '7E0100:2,7E027C:1,7E0294:1,7E0290:4,7E02AF:2,7E1BB7:1', '--from',
                        os.path.join(JK, 'states', 'ch24.state'), '--btnscript', ','.join(GAUGE_O), '--frames', '3820',
                        '--diff'], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True, text=True,
                       errors='replace')
    oe = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s+f(\d+)\s+7E0100=(....)\*?\s+7E027C=(..)\*?\s+7E0294=(..)\*?\s+7E0290=(........)\*?\s+'
                     r'7E02AF=(....)\*?\s+7E1BB7=(..)', l)
        if m:
            w = lambda h: h[2:4] + h[0:2]
            q = m.group(5)
            oe.append((int(m.group(1)), (w(m.group(2)), m.group(3), m.group(4), w(q[0:4]), w(q[4:8]), w(m.group(6)), m.group(7))))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SRAM=os.path.join(JK, 'srm', 'ch24.srm'), CT_OWVEHTRACE='1', CT_HOLDBTN='04@%d:2' % (3082 + FLIGHT_SHIFT + 1),   # (CT_HOLDBTN reaches the latch a frame before the walkscript)
               CT_SHOTS=os.path.join(OUT, 'gauge'), CT_SHOTS_AT=','.join(str(f + FLIGHT_SHIFT) for f in GAUGE_SHOTS + [3820]),
               CT_SHOTS_EXIT='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot', '--walkscript',
                        ','.join(GAUGE_P)], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    pe = []
    for l in r.stdout.splitlines():
        m = re.match(r'owveh: pass \d+ f(\d+) 027C=(..) 027E=.. 027F=.. 028F=.. 0294=(..) 0290=(....),(....) 0100=(....) '
                     r'02AF=(....) 1BB7=(..)', l)
        if m:
            pe.append((int(m.group(1)), (m.group(6), m.group(2), m.group(3), m.group(4), m.group(5), m.group(7), m.group(8))))
    def rel(ev):
        # `$1BB7` only while the gauge is open: outside it the byte is whatever the field's fill left (the cartridge's 06B ->
        # 1F6 entry fills $1B82-$1BFF with `00 01`, the port's does not) -- the controller writes it before any reader.
        t0 = next((f for f, v in ev if v[1] == '08'), None)
        # ...and only from the controller's first write (C2/6B58, its first pass): before that the gauge is not up yet.
        t1 = next((f for f, v in ev if t0 is not None and f > t0 and v[6] != ev[[k for k, (g, _) in enumerate(ev) if g == t0][0]][1][6]), None)
        ev = [(f, v if (v[1] == '08' and t1 is not None and f >= t1) else v[:6] + ('--',)) for f, v in ev]
        out, last = [], None
        for f, v in ev:
            if t0 is None or f < t0 or v == last:
                continue
            out.append((f - t0, v)); last = v
        return out, t0
    o, ot = rel(oe)
    p, pt = rel(pe)
    hor = 3820 - (ot or 0)
    o = [e for e in o if e[0] <= hor]; p = [e for e in p if e[0] <= hor]
    # (1) the gauge itself: every change until `$027C` leaves 8 and the main thread's 18 NMIs after it
    def until7(ev):
        t7 = next((d for d, v in ev if v[1] == '07'), None)
        return [e for e in ev if t7 is None or e[0] <= t7], t7
    o1, o7 = until7(o); p1, p7 = until7(p)
    print('gauge: the gauge %s -- %d changes to `$027C = 7` (cartridge +%s, port +%s)' % (
        'same' if o1 == p1 else 'DIFFERENT', len(o1), o7, p7))
    if o1 != p1:
        for a, b in zip(o1, p1):
            if a != b: print('gauge:   first difference cartridge +%d:%s port +%d:%s' % (a[0], '/'.join(a[1]), b[0], '/'.join(b[1]))); break
    # (2) the way back: `$027C` 7 -> 1 (the fade-out's 18 NMIs, C2/2E72 + C2/2C93 NMI off, the fade-in's 17)
    def back(ev, t7):
        t1 = next((d for d, v in ev if t7 is not None and d > t7 and v[1] == '01'), None)
        return (t1 - t7) if t1 is not None else None
    print('gauge: the way back %d frames on the cartridge, %d in the port' % (back(o, o7) or -1, back(p, p7) or -1))
    # (3) the trip: the frame the location changes, and the state at the end
    def trip(ev):
        return next((d for d, v in ev if v[0] != (ev[0][1][0] if ev else '')), None)
    print('gauge: the trip into $%s at +%s on the cartridge, +%s in the port; at the end %s' % (
        o[-1][1][0] if o else '-', trip(o), trip(p), 'the same' if o and p and o[-1] == p[-1] else 'DIFFERENT'))
    # ...and the world's load there (C2's loader, priced by the load clock -- ROADMAP F1): the Epoch's first step on the new world
    def first_move(ev, t):
        y0 = next((v[4] for d, v in ev if t is not None and d >= t), None)
        return next((d for d, v in ev if t is not None and d > t and v[4] != y0), None)
    print('gauge: on the new world the Epoch first moves at +%s on the cartridge, +%s in the port' % (
        first_move(o, trip(o)), first_move(p, trip(p))))
    if os.environ.get('EPOCH_DUMP'):
        oi = {d: v for d, v in o}; pi = {d: v for d, v in p}
        for d in sorted(set(oi) | set(pi)):
            print('gauge:   +%d cartridge %s port %s' % (d, '/'.join(oi[d]) if d in oi else '-', '/'.join(pi[d]) if d in pi else '-'))
    for f in GAUGE_SHOTS:                                # the port's picture P is the cartridge's --peek P + 2
        o_raw = os.path.join(OUT, 'gauge_o%d.raw' % f)
        bs, t = [], 0
        for it in GAUGE_O:
            b, n = it.split(':'); n = int(n)
            if t + n >= f + 2: bs.append('%s:%d' % (b, f + 2 - t)); break
            bs.append(it); t += n
        subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', os.path.join(JK, 'states', 'ch24.state'), o_raw,
                        '--btnscript', ','.join(bs)], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
        po = raw_rgb(os.path.join(OUT, 'gauge_f%d.raw' % (f + FLIGHT_SHIFT)), False)
        oo = raw_rgb(o_raw, True)
        n = sum(po(x, y) != oo(x, y) for y in range(0, 224) for x in range(256))
        nl = sum(po(x, 223) != oo(x, 223) for x in range(256))
        print('gauge: picture port f%d / cartridge f%d %d px differ (%d on the last line)' % (f + FLIGHT_SHIFT, f + 2, n, nl))
    return 0


MAP_O = ['0:60', '100:2'] + ['0:30', '100:2'] * 93 + ['0:44', '4:2', '0:240', '1:2', '0:250']
MAP_SEL, MAP_B = 3082, 3324                       # Select and B on the cartridge's clock
MAP_SHIFT = 358 + 65    # the world's own scripts run 358 frames behind from the free flight (+65: the power-on title)
MAP_SHOTS = [3100, 3105, 3200, 3215, 3250, 3300, 3340, 3355, 3412, 3430, 3445, 3470]   # the mosaic and fade, the zoom, the map, B, the way back
MAP_P = ['n:198', 's:1', 'n:125', 'a:1', 'n:160', 'a:2'] + ['n:30', 'a:2'] * 93 + ['n:600']


def world_map():
    """The world map from the free flight (C2/258D -> C2/631F, disasm/notes/ow_world_map.md): Select (the cartridge's f3082,
    CT_HOLDBTN Sel on the port's world clock), the mosaic and the fade, the map zooming out, B 242 frames on (the zoom back), the
    world rebuilt and back. Every change of `$0100 $027C $1C $1D $1F $1B48` and of the controller's state ($0C30 = 666C, +2),
    counted from the frame `$027C` becomes 6; pictures at MAP_SHOTS: the port's P against the cartridge's --peek P + 3 (on the
    world's clock; the gauge's P + 2 is on the press's, a frame behind it)."""
    os.makedirs(OUT, exist_ok=True)
    frames = MAP_B + 252
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', '7E0100:2,7E027C:1,7E001C:4,7E1B48:1,7E0C30:3',
                        '--from', os.path.join(JK, 'states', 'ch24.state'), '--btnscript', ','.join(MAP_O), '--frames',
                        str(frames), '--diff'], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_RUNFRAME_TRACE='1'),
                       capture_output=True, text=True, errors='replace')
    # The cartridge's lost pictures: snes_runFrame cannot stop inside an opcode, and C2/26A8's 64 KB VRAM clear (one DMA,
    # 1.47 frames) ends inside the vblank after the one it began before -- one call (one picture) for two frames. The
    # console shows both; the port, on the console's clock, is a frame later than the harness from there. A call that ran
    # over a frame boundary to a line of the vblank (>= 225) is a lost picture at monitor index call - 1.
    lost = sorted(int(m.group(1)) - 1 for m in re.finditer(r'runframe: call (\d+) ran frames \d+-\d+ \(to V=(\d+)\)', r.stderr)
                  if int(m.group(2)) >= 225)
    lost_by = lambda f: sum(1 for x in lost if x <= f)
    w = lambda h: h[2:4] + h[0:2]
    oe = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s+f(\d+)\s+7E0100=(....)\*?\s+7E027C=(..)\*?\s+7E001C=(........)\*?\s+7E1B48=(..)\*?\s+'
                     r'7E0C30=(......)', l)
        if m:
            q, b = m.group(4), m.group(6)
            oe.append((int(m.group(1)), (w(m.group(2)), m.group(3), q[0:2] + q[2:4], q[6:8], m.group(5),
                                         b[4:6] if w(b[0:4]) == '666C' else '--')))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_OWGAUGETRACE='1',
               CT_SRAM=os.path.join(JK, 'srm', 'ch24.srm'), CT_OWVEHTRACE='1',
               CT_HOLDBTN='02@%d:2,08@%d:2' % (MAP_SEL + MAP_SHIFT + 1 + lost_by(MAP_SEL), MAP_B + MAP_SHIFT + 1 + lost_by(MAP_B)),
               CT_SHOTS=os.path.join(OUT, 'map'),
               CT_SHOTS_AT=','.join(str(f + MAP_SHIFT + lost_by(f)) for f in MAP_SHOTS + [frames]),
               CT_SHOTS_EXIT='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot', '--walkscript',
                        ','.join(MAP_P)], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    pe = []
    for l in r.stdout.splitlines():
        if l.startswith('owmap:'): print('map: port says ' + l)
        m = re.match(r'owveh: pass \d+ f(\d+) 027C=(..) .* 0100=(....) .* 1C=(....) 1F=(..) 1B48=(..) \|(.*)', l)
        if m:
            c = re.search(r':666C/(..)', m.group(7))
            pe.append((int(m.group(1)), (m.group(3), m.group(2), m.group(4), m.group(5), m.group(6), c.group(1) if c else '--')))
    def rel(ev):
        # `$1B48` from the map's own first write on (C2:6A0A `10 48 1B`, spawned as the zoom ends: the controller's state 2):
        # before it the byte is whatever the field left (the cartridge's F3, the port's 00) and nothing reads it.
        ev = [(f, v[:4] + ('--',) + v[5:] if v[1] == '06' and v[5] in ('--', '00', '01') else v) for f, v in ev]
        # ...and the controller from its first pass (state 1): the port's set-up is made at once, the cartridge's over the
        # NMI-off stretch, so a block standing in state 0 meanwhile is no state either side shows.
        ev = [(f, v[:5] + ('--',) if v[5] == '00' else v) for f, v in ev]
        t0 = next((f for f, v in ev if v[1] == '06'), None)
        out, last = [], None
        for f, v in ev:
            if t0 is None or f < t0 or v == last:
                continue
            out.append((f - t0, v)); last = v
        return out, t0
    o, ot = rel(oe)
    p, pt = rel(pe)
    o = [(d + lost_by(ot + d), v) for d, v in o]                 # the cartridge's events on the console's clock
    print('map: the cartridge lost %d picture(s) to a long DMA (monitor %s)' % (len([x for x in lost if x >= (ot or 0)]),
          ', '.join('f%d' % x for x in lost if x >= (ot or 0)) or '-'))
    hor = frames - (ot or 0) + lost_by(frames)
    o = [e for e in o if e[0] <= hor]; p = [e for e in p if e[0] <= hor]
    print('map: timeline %s -- %d changes from `$027C = 6` (cartridge f%s, port f%s; %d in the port)' % (
        'same' if o == p else 'DIFFERENT', len(o), ot, pt, len(p)))
    if o != p:
        for a, b in zip(o, p):
            if a != b: print('map:   first difference cartridge +%d:%s port +%d:%s' % (a[0], '/'.join(a[1]), b[0], '/'.join(b[1]))); break
    t7o = next((d for d, v in o if v[1] == '07'), None); t7p = next((d for d, v in p if v[1] == '07'), None)
    t1o = next((d for d, v in o if v[1] == '01' and t7o is not None and d > t7o), None)
    t1p = next((d for d, v in p if v[1] == '01' and t7p is not None and d > t7p), None)
    print('map: `$027C = 7` at +%s on the cartridge, +%s in the port; `$027C = 1` at +%s / +%s' % (t7o, t7p, t1o, t1p))
    if os.environ.get('EPOCH_DUMP'):
        oi = {d: v for d, v in o}; pi = {d: v for d, v in p}
        for d in sorted(set(oi) | set(pi)):
            print('map:   +%d cartridge %s port %s' % (d, '/'.join(oi[d]) if d in oi else '-', '/'.join(pi[d]) if d in pi else '-'))
    for f in MAP_SHOTS:
        o_raw = os.path.join(OUT, 'map_o%d.raw' % f)
        bs, t = [], 0
        for it in MAP_O:
            b, n = it.split(':'); n = int(n)
            if t + n >= f + 1: bs.append("%s:%d" % (b, f + 1 - t)); break
            bs.append(it); t += n
        subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', os.path.join(JK, 'states', 'ch24.state'), o_raw,
                        '--btnscript', ','.join(bs)], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
        q = f + MAP_SHIFT + lost_by(f)
        pp = os.path.join(OUT, 'map_f%d.raw' % q)
        if not os.path.exists(pp):
            print('map: picture port f%d MISSING' % q); continue
        po = raw_rgb(pp, False)
        oo = raw_rgb(o_raw, True)
        n = sum(po(x, y) != oo(x, y) for y in range(0, 224) for x in range(256))
        print("map: picture port f%d / cartridge f%d %d px differ" % (q, f + 1, n))
    return 0


DACTYL_O = ['0:60', '20:8', '0:20', '100:2', '0:90', '80:40', '0:20', '40:40', '0:20', '100:2', '0:160']
DACTYL_P = ['n:198', 's:1', 'n:125', 'a:1', 'n:160', 'd:8', 'n:20', 'a:2', 'n:90', 'r:40', 'n:20', 'l:40', 'n:20', 'a:2', 'n:160']
DACTYL_SHOTS = [120, 160, 200, 240, 300, 330, 370, 440]


def dactyl():
    """Vehicle B, the Dactyls (loc 1F3; disasm/notes/ow_vehicle.md sec. 7): Jackie's ch17 at the Tyrano Lair, a step down onto
    the Dactyl, A (aboard), the lift-off, Right 40 frames, A (the landing). Every change of `$027C $027E $029A/$029C $029E
    $1BF0` and the followers' places `$02A6-$02AD`, from the frame `$027E` leaves 0; six pictures (P+2)."""
    os.makedirs(OUT, exist_ok=True)
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', '7E027C:3,7E029A:5,7E02A6:8,7E1BF0:1', '--from',
                        os.path.join(JK, 'states', 'ch17.state'), '--btnscript', ','.join(DACTYL_O), '--frames', '480',
                        '--diff'], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True, text=True,
                       errors='replace')
    w = lambda h: h[2:4] + h[0:2]
    oe = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s+f(\d+)\s+7E027C=([0-9A-F]{6})\*?\s+7E029A=([0-9A-F]{10})\*?\s+7E02A6=([0-9A-F]{16})\*?\s+'
                     r'7E1BF0=(..)', l)
        if m:
            c, b, q = m.group(2), m.group(3), m.group(4)
            oe.append((int(m.group(1)), (c[0:2], c[4:6], w(b[0:4]), w(b[4:8]), b[8:10], m.group(5),
                                          w(q[0:4]), w(q[4:8]), w(q[8:12]), w(q[12:16]))))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SRAM=os.path.join(JK, 'srm', 'ch17.srm'), CT_OWVEHTRACE='1', CT_SHOTS=os.path.join(OUT, 'dactyl'),
               CT_SHOTS_AT=','.join(str(f + 360 + 65) for f in DACTYL_SHOTS + [480]), CT_SHOTS_EXIT='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot', '--walkscript',
                        ','.join(DACTYL_P)], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    pe = []
    for l in r.stdout.splitlines():
        m = re.match(r'owveh: pass \d+ f(\d+) 027C=(..) 027E=(..) .*029A=(....),(....) 029E=(..) 1BF0=(..) '
                     r'02A6=(....),(....),(....),(....)', l)
        if m:
            pe.append((int(m.group(1)), tuple(m.group(k) for k in range(2, 12))))
    def rel(ev):
        t0 = next((f for f, v in ev if v[1] != '00'), None)
        out, last = [], None
        for f, v in ev:
            if t0 is None or f < t0 or v == last:
                continue
            out.append((f - t0, v)); last = v
        return out, t0
    o, ot = rel(oe); p, pt = rel(pe)
    hor = 480 - (ot or 0)
    o = [e for e in o if e[0] <= hor]; p = [e for e in p if e[0] <= hor]
    same = o == p and len(o) > 0
    print('dactyl: timeline %s -- %d changes from `$027E = 4` (cartridge f%s, port f%s)' % ('same' if same else 'DIFFERENT', len(o), ot, pt))
    if not same:
        oi = {d: v for d, v in o}; pi = {d: v for d, v in p}
        n = 0
        for d in sorted(set(oi) | set(pi)):
            if oi.get(d) != pi.get(d):
                print('dactyl:   +%d cartridge %s port %s' % (d, '/'.join(oi[d]) if d in oi else '-', '/'.join(pi[d]) if d in pi else '-'))
                n += 1
                if n >= 12: break
    off = (pt - ot) if (ot is not None and pt is not None) else 359 + 65
    for f in DACTYL_SHOTS:                               # the port's picture P is the cartridge's --peek P + 1 on the clock of
                                                         # the boarding (its A reaches the latch a frame later than the port's)
        o_raw = os.path.join(OUT, 'dactyl_o%d.raw' % f)
        bs, t = [], 0
        for it in DACTYL_O:
            b, n = it.split(':'); n = int(n)
            if t + n >= f + 360 + 65 - off + 1: bs.append('%s:%d' % (b, f + 360 + 65 - off + 1 - t)); break
            bs.append(it); t += n
        subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', os.path.join(JK, 'states', 'ch17.state'), o_raw,
                        '--btnscript', ','.join(bs)], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
        po = raw_rgb(os.path.join(OUT, 'dactyl_f%d.raw' % (f + 360 + 65)), False)
        oo = raw_rgb(o_raw, True)
        # the lava's four colours cycle on the world's own clock since the save's load -- a frame the port and the cartridge's
        # state do not share (the cartridge's state was taken some frames after its load): a pixel both sides draw in one of
        # them is the lava, whatever its phase
        lava = {(189, 41, 0), (189, 57, 0), (189, 74, 0), (189, 82, 0)}
        n = sum(1 for y in range(8, 216) for x in range(256) if po(x, y) != oo(x, y) and not (po(x, y) in lava and oo(x, y) in lava))
        print('dactyl: picture port f%d %d px differ (the lava cycle aside)' % (f + 360 + 65, n))
    return 0


def main():
    if '--map' in sys.argv:
        return world_map()
    if '--dactyl' in sys.argv:
        return dactyl()
    if '--gauge' in sys.argv:
        return gauge()
    if '--omen' in sys.argv:
        return omen()
    if '--flight' in sys.argv:
        os.makedirs(OUT, exist_ok=True)
        return flight()
    if '--land' in sys.argv:                          # A every 32 frames: the Epoch lands at once, the party steps out, boards
        os.makedirs(OUT, exist_ok=True)               # again and lifts off (the cartridge's f3101-f3229)
        return flight('epoch-land', ['0:60', '100:2'] + ['0:30', '100:2'] * 110,
                      ['n:198', 's:1', 'n:125', 'a:1', 'n:160', 'a:2'] + ['n:30', 'a:2'] * 93 + ['n:28', 'a:2'] + ['n:30', 'a:2'] * 16,
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
