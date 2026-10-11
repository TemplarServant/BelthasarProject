#!/usr/bin/env python3
r"""A cutscene leaves the lead on the plane it found him on -- port against the LOGICAL oracle.

    python tools/field_plane_cutscene_cmp.py

The lead's plane is ONE byte, $0155, with three writers in the ROM: the location load (C0/0C4C `LDA #01 / STA $55`),
the arrival-tile evaluation (C0/9DC3 -> 9DFE) and the step commit (C0/9D54-9D5A, unless the tile is IGNORE_Z). A
scripted move (op 0x96 -> C0/A810, op 0xD9's walk programs) writes none of them (disasm/notes/field_plane_0155.md).

  meal  saves/field_re/s07b_table_seat.state (loc $07B, the castle dining hall with the soldiers' tables; the oracle's own --teleport
        from saves/solo_forest.state with the story page of the user's capture build/ct_debug_023.state, then walked
        to the soldiers' table, Crono at 0780,0C7F on plane 1, facing down). A at the table (obj15 fn1), A through
        the choice and the boxes; op 0xD9 seats Crono at 0704,0C7F -- the upper, plane-1 half of a shape-0x19 strip
        -- and gives control back at f1099. Then Left 30, Right 60. Every frame, both sides: the lead's words
        $1802/$1882, the plane $0155, the control gate $011F.

Per drive:
  plane-cutscene: <drive> wram f0-fN: <k> frame(s) differ [runs]
  plane-cutscene: <drive> plane <values over the drive> cartridge|port
  plane-cutscene: <drive> control-on f<..> cartridge|port
  plane-cutscene: <drive> lead-end <x>,<y> cartridge|port
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
OUT = os.path.join(ROOT, 'build', '_planecut')

PRESSES = 13                                                # the boxes after the choice, one A a second

# The oracle's word for frame f reaches the ROM's logic on frame f+1 (memory ct-frame-alignment-p2): the port's drive
# is the oracle's shifted one frame later, as in tools/e4_visit_cmp.py.
DRIVES = {
    'meal': dict(state='saves/field_re/s07b_table_seat.state', seed='saves/field_re/s07b_table_seat.lakewram',
                 scene=0x7B,
                 btn='0:10,100:4,0:306,' + ','.join(['100:1,0:59'] * PRESSES) + ',0:50,40:30,80:60,0:10',
                 walk='n:11,a:4,n:306,' + ','.join(['a:1,n:59'] * PRESSES) + ',n:50,l:30,r:60,n:10',
                 last=1248),
}
WATCH = [(0x1802, 2), (0x1882, 2), (0x0155, 1), (0x011F, 1)]


def oracle_monitor(state, btn):
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    watch = ','.join('7E%04X:%d' % (a, n) for a, n in WATCH)
    r = subprocess.run([CT, '--monitor', watch, '--from', state, '--btnscript', btn], cwd=ROOT, env=env,
                       capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), {int(a, 16): v for a, v in re.findall(r'7E([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}))
    return rows


def port_run(d):
    os.makedirs(OUT, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_FIELD_MEMMON=','.join('%04X:%X' % (a, n) for a, n in WATCH))   # (its lengths are hex)
    argv = [FIELD, '--assets', 'ct_assets.dat', '--scene', str(d['scene']), '--loadwram', d['seed'], '--selftest',
            os.path.join(OUT, '_end.raw'), '--frames', str(d['last'] + 1), '--walkscript', d['walk']]
    r = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), {int(a, 16): v for a, v in re.findall(r'([0-9A-F]{4,5})=([0-9A-F]+)', m.group(2))}))
    return rows


def series(rows, addr, last):
    by = {}
    for f, vals in rows:
        by.setdefault(f, {}).update(vals)
    out, cur = [], None
    for f in range(last + 1):
        if f in by and addr in by[f]:
            cur = by[f][addr]
        out.append(cur)
    return out


def runs(bad):
    out = []
    for f in bad:
        if out and f == out[-1][1] + 1:
            out[-1][1] = f
        else:
            out.append([f, f])
    return ', '.join(('f%d' % a) if a == b else ('f%d-f%d' % (a, b)) for a, b in out)


def word(v):
    return None if v is None else int(v[2:4] + v[0:2], 16)  # the monitors print a word's two bytes in memory order


def drive(name):
    d = DRIVES[name]
    o = oracle_monitor(d['state'], d['btn'])
    p = port_run(d)
    if not o or not p:
        print('plane-cutscene: %s NO DATA (oracle rows %d, port rows %d)' % (name, len(o), len(p)))
        return
    last = min(d['last'], max(f for f, _ in o))             # the port's monitor prints changes only
    addrs = [a for a, _ in WATCH]
    so = list(zip(*[series(o, a, last) for a in addrs]))
    sp = list(zip(*[series(p, a, last) for a in addrs]))
    bad = [f for f in range(last + 1) if so[f] != sp[f]]
    print('plane-cutscene: %s wram f0-f%d: %d frame(s) differ%s' % (name, last, len(bad), (' [' + runs(bad) + ']') if bad else ''))
    for who, s in (('cartridge', so), ('port', sp)):
        planes = []
        for f in range(last + 1):
            if not planes or planes[-1] != s[f][2]:
                planes.append(s[f][2])
        ctl = [f for f in range(1, last + 1) if s[f][3] == '01' and s[f - 1][3] == '00']
        print('plane-cutscene: %s plane %s %s' % (name, ' '.join(str(v) for v in planes), who))
        print('plane-cutscene: %s control-on %s %s' % (name, ' '.join('f%d' % f for f in ctl) or '-', who))
        print('plane-cutscene: %s lead-end %04X,%04X %s' % (name, word(s[last][0]) or 0, word(s[last][1]) or 0, who))


def main():
    names = [a for a in sys.argv[1:] if a in DRIVES] or list(DRIVES)
    for n in names:
        drive(n)


if __name__ == '__main__':
    main()
