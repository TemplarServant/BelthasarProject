#!/usr/bin/env python3
r"""Op 0xE4 in a LIVE object script ends the object's visit -- port against the LOGICAL oracle.

    python tools/e4_visit_cmp.py

C0/3D97 (op 0xE4) copies the rect in the WRAM map at once (`3DBD JSR C0/AF4E`) and then exits `3DC0 LDX $C7 / INX /
CLC / RTS`; the runner's `5AB4 BCC ->5ABA` ends the visit on CLC, so the op after an E4 runs on the object's NEXT
visit (disasm/notes/field_tile_copy_e5.md section 5). The slot-1 runner C0/595C ignores the carry; this is about a
live object.

  lever  saves/field_re/s236_lever.state (loc $0EC, the oracle's own --teleport from saves/truce_market.state to tile
         24,36 facing up): A at the lever (obj9, fn1) twice. The 1st press takes the flag-clear arm (E3 00, E5, WAIT, E5,
         WAIT, E4 `08 21 0A 22 08 15 3B`, then 65 01 EC / E3 01 / 00), the 2nd the flag-set arm (the same with the E4
         `08 17 0A 19 08 15 3B`, then 66 01 EC). obj9 runs every 4th frame ($1013). Every frame, both sides: obj9's ip
         $1192 and cadence $1013, $011F, the flag $7F:00EC, $0117, and the three copies' destination rows (cols
         $08-$18 of rows $15/$16/$22/$26/$27) on the six planes C0/AF4E writes.

Per drive:
  e4-visit: <drive> wram f0-fN: <k> frame(s) differ [runs]
  e4-visit: <drive> e4-end f<a> f<b> cartridge|port     (the frames a visit ended right after an E4: ip = E4 + 8)
  e4-visit: <drive> control-on f<a> f<b> cartridge|port (the frames $011F goes 0 -> 1: the op after the ops after E4)
  e4-visit: <drive> copies f<..> cartridge|port         (the frames the destination rows change)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
OUT = os.path.join(ROOT, 'build', '_e4visit')

# The oracle's word for frame f reaches the ROM's logic on frame f+1 (memory ct-frame-alignment-p2): the port's drive
# is the oracle's shifted one frame later, as in tools/e5_copy_cmp.py.
DRIVES = {
    'lever': dict(state='saves/field_re/s236_lever.state', seed='saves/field_re/s236_lever.lakewram', scene=236,
                  obj=9, btn='0:10,100:4,0:100,100:4,0:130', walk='n:11,a:4,n:100,a:4,n:130', last=240),
}
PLANES = (0x3000, 0x3040, 0x3080, 0x30C0, 0x7000, 0x7040)   # C0/AF4E's bases for mask bits 0-5
ROWS = (0x15, 0x16, 0x22, 0x26, 0x27)
COL, NCOL = 0x08, 0x11


def watched(obj):
    x = obj * 2
    return [(0x1180 + x, 2), (0x1001 + x, 1), (0x011F, 1), (0x0117, 1), (0x100EC, 1)] + \
           [(p + (r << 8) + COL, NCOL) for p in PLANES for r in ROWS]


MONITOR_MAX = 22                                            # ct.exe --monitor takes 32 regions


def oracle_addr(a):
    return '7F%04X' % (a & 0xFFFF) if a > 0xFFFF else '7E%04X' % a


def oracle_monitor(state, btn, byts):
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    rows = []
    for k in range(0, len(byts), MONITOR_MAX):
        watch = ','.join('%s:%d' % (oracle_addr(a), n) for a, n in byts[k:k + MONITOR_MAX])
        r = subprocess.run([CT, '--monitor', watch, '--from', state, '--btnscript', btn], cwd=ROOT, env=env,
                           capture_output=True, text=True, errors='replace')
        for l in r.stdout.splitlines():
            m = re.match(r'\s*f(\d+)\s+(.*)', l)
            if m:
                rows.append((int(m.group(1)), {(int(a, 16) if b == '7E' else 0x10000 | int(a, 16)): v for b, a, v in
                                               re.findall(r'(7E|7F)([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}))
    return rows


def port_run(d, byts):
    os.makedirs(OUT, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_FIELD_MEMMON=','.join('%04X:%X' % (a, n) for a, n in byts))   # (its lengths are hex)
    argv = [FIELD, '--assets', 'ct_assets.dat', '--scene', str(d['scene']), '--loadwram', d['seed'], '--selftest',
            os.path.join(OUT, '_end.raw'), '--frames', str(d['last'] + 1), '--walkscript', d['walk']]
    r = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), {int(a, 16): v for a, v in
                                           re.findall(r'([0-9A-F]{4,5})=([0-9A-F]+)', m.group(2))}))
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


def script_image(seed):
    data = open(os.path.join(ROOT, seed), 'rb').read()
    i = data.index(b'RAM:131072:') + len(b'RAM:131072:')
    return data[i + 0x12001:i + 0x20000]                    # the event script as the ROM reads it, $7F:2001+ip


def word(v):
    return None if v is None else int(v[2:4] + v[0:2], 16)  # the monitors print a word's two bytes in memory order


def drive(name):
    d = DRIVES[name]
    last = d['last']
    byts = watched(d['obj'])
    o = oracle_monitor(d['state'], d['btn'], byts)
    p = port_run(d, byts)
    last = min(last, max(f for f, _ in o))                  # --monitor's rows stop a frame before the script's end
    addrs = [a for a, _ in byts]
    so = list(zip(*[series(o, a, last) for a in addrs]))
    sp = list(zip(*[series(p, a, last) for a in addrs]))
    bad = [f for f in range(last + 1) if so[f] != sp[f]]
    print('e4-visit: %s wram f0-f%d: %d frame(s) differ%s' % (name, last, len(bad), (' [' + runs(bad) + ']') if bad else ''))
    img = script_image(d['seed'])
    iip, ictl = addrs.index(0x1180 + d['obj'] * 2), addrs.index(0x011F)
    rect = list(range(5, len(addrs)))
    for who, s in (('cartridge', so), ('port', sp)):
        ends = []
        for f in range(1, last + 1):
            ip, ip0 = word(s[f][iip]), word(s[f - 1][iip])
            if ip is not None and ip != ip0 and ip >= 8 and img[ip - 8] == 0xE4:
                ends.append(f)
        ctl = [f for f in range(1, last + 1) if s[f][ictl] == '01' and s[f - 1][ictl] == '00']
        cp = [f for f in range(1, last + 1) if any(s[f][i] != s[f - 1][i] for i in rect)]
        print('e4-visit: %s e4-end %s %s' % (name, ' '.join('f%d' % f for f in ends) or '-', who))
        print('e4-visit: %s control-on %s %s' % (name, ' '.join('f%d' % f for f in ctl) or '-', who))
        print('e4-visit: %s copies %s %s' % (name, ' '.join('f%d' % f for f in cp) or '-', who))


def main():
    names = [a for a in sys.argv[1:] if a in DRIVES] or list(DRIVES)
    for n in names:
        drive(n)


if __name__ == '__main__':
    main()
