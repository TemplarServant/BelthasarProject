#!/usr/bin/env python3
r"""The OPENED-DOOR LIST at $7F:1CC8 and the door cells it re-opens, frame by frame, port against the LOGICAL oracle.

    python tools/door_list_cmp.py [reload|walk|load ...]

disasm/notes/field_door_list.md. Three routines own the list (sixteen tile indices into the $7E:3000 map, $8080 = empty):
C0/28AA resets it on a location load, C0/28C0 appends the door request's tile (C0/0D49), and C0/28E1 -- C0/286C's first
JSR, and C0/18C4 after a battle -- re-opens every listed door over a freshly decompressed map. Each drive runs the same
input on both sides (the cartridge on `--monitor`, the port on CT_FIELD_MEMMON, which reads the flat image including bank
$7F) and compares, every frame, the list and the door's six cells:

  reload  party3_field as captured: its right-hand door ($2935, the $FA arm) is open and listed. X at f4, B at f60: the
          reload (C0/01A5) re-decompresses the map with the door closed and C0/28E1 opens it again.
  walk    the same field with that door CLOSED and the list EMPTY (the cells the reload's own decompress writes, measured
          F2F4/F6F8/FAFC, and C0/28AA's $8080): run right and up into the door -- the request appends $2935 and the handler
          bumps the block -- then X at f130, B at f190.
  load    the scene-468 stairs with $2935 poked into the list: the location loads (the stairs reload 468, then 07A) reset it.

Per drive:
  door-list: <drive> list f0-fN: <k> frame(s) differ [runs]
  door-list: <drive> cells f0-fN: <k> frame(s) differ [runs]
  door-list: <drive> end list <words> cells <3 words> cartridge|port
  door-list: <drive> end -> same|DIFFER
  door-list: <drive> port <C0/28C0 appends> append(s), C0/28E1 at <frame site reopened ...>
  door-list: <drive> cartridge C0/28C0 <n> visit(s), C0/28E1 <n> visit(s)
The runs that differ are the port's frame model, named in the check's notes (the menu's own use of $7E:3000, the reload's
and the load's NMI-off frames, the door request's two-frame lead); a frame the port gets WRONG after the reload or the
load, or a list the port keeps, shows up as a new run.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
OUT = os.path.join(ROOT, 'build', '_doorlist')

LIST = ('11CC8', 32)
CELLS = [('5735', 2), ('5835', 2), ('5935', 2)]      # rows $27-$29, cols $35-$36: the $2935 door's 2x3 block
CLOSED = {0x11CC8: 0x80, 0x11CC9: 0x80, 0x5735: 0xF2, 0x5736: 0xF4, 0x5835: 0xF6, 0x5836: 0xF8, 0x5935: 0xFA,
          0x5936: 0xFC}
# name: (oracle state, port WRAM seed, port scene, pokes (flat index: value), oracle --btnscript, port --walkscript,
#        port env, frames). The port reads a press one frame after the cartridge's pad goes down (menu_wram_cmp.py).
DRIVES = {
    'reload': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
               '0:4,200:2,0:54,1:2,0:100', 'n:5,m:1,n:55,b:1,n:100', {}, 160),
    'walk': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, CLOSED,
             '0:4,81:86,11:14,0:26,200:2,0:58,1:2,0:108', 'n:5,r:86,u:14,n:26,m:1,n:59,b:1,n:108', {'CT_RUN': '1'}, 290),
    'load': ('saves/field_re/scene468_stairs_to_120.state', 'saves/field_re/scene468_stairs_to_120.lakewram', 468,
             {0x11CC8: 0x35, 0x11CC9: 0x29}, '81:200', 'r:200', {'CT_RUN': '1'}, 195),
}
LIST_ONLY = {'load'}


def oracle_addr(a):
    v = int(a, 16)
    return ('7F%04X' % (v - 0x10000)) if v >= 0x10000 else ('7E%04X' % v)


def series(rows, key, last):
    out, cur = [], None
    it = iter(sorted(rows))
    pend = next(it, None)
    for f in range(last + 1):
        while pend is not None and pend[0] <= f:
            if key in pend[1]:
                cur = pend[1][key]
            pend = next(it, None)
        out.append(cur)
    return out


def run_oracle(d, name):
    state, _, _, pokes, btn, _, _, frames = d
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    if pokes:
        env['PEEK_POKE'] = ','.join('%04X=%02X' % kv for kv in pokes.items())
    watch = ','.join('%s:%d' % (oracle_addr(a), n) for a, n in [LIST] + CELLS)
    r = subprocess.run([CT, '--monitor', watch, '--from', state, '--btnscript', btn], cwd=ROOT, env=env,
                       capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if m:
            vals = {}
            for a, v in re.findall(r'(7[EF][0-9A-F]{4})=([0-9A-F]+)', m.group(2)):
                flat = int(a[2:], 16) + (0x10000 if a.startswith('7F') else 0)
                vals['%04X' % flat] = v
            rows.append((int(m.group(1)), vals))
    visits = {}
    for pc in ('28C0', '28E1'):
        r = subprocess.run([CT, '--hookpc', pc, '--from', state, '--btnscript', btn, '--frames', str(frames)], cwd=ROOT,
                           env=env, capture_output=True, text=True, errors='replace')
        visits[pc] = sum(int(v) for v in re.findall(r'^\s*f\d+\s+visits=(\d+)', r.stdout, re.M))
    return rows, visits


def run_port(d, name):
    _, seed, scene, pokes, _, walk, penv, frames = d
    os.makedirs(OUT, exist_ok=True)
    b = bytearray(open(os.path.join(ROOT, seed), 'rb').read())
    o = b.index(b'RAM:131072:') + 11
    for a, v in pokes.items():
        b[o + a] = v
    sp = os.path.join(OUT, '_%s_seed.lakewram' % name)
    open(sp, 'wb').write(bytes(b))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_DOORLIST='1',
               CT_FIELD_MEMMON=','.join('%s:%X' % (a, n) for a, n in [LIST] + CELLS))
    env.update(penv)
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(scene), '--loadwram', sp, '--selftest',
                        os.path.join(OUT, '_%s_end.raw' % name), '--frames', str(frames + 2), '--walkscript', walk],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    rows, events = [], []
    for l in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), {a.upper().rjust(4, '0'): v for a, v in
                                           re.findall(r'([0-9A-F]{4,5})=([0-9A-F]+)', m.group(2))}))
        m = re.match(r'doorlist: f(\d+) (C0/28C0|C0/28E1) (.*)', l)
        if m:
            events.append((int(m.group(1)), m.group(2), m.group(3)))
    return rows, events


def runs(bad):
    out, cur = [], None
    for f in bad:
        if cur and f == cur[1] + 1:
            cur[1] = f
            continue
        if cur:
            out.append(cur)
        cur = [f, f]
    if cur:
        out.append(cur)
    return ', '.join(('f%d' % a) if a == b else ('f%d-f%d' % (a, b)) for a, b in out)


def words(hexstr):
    ws = [hexstr[i + 2:i + 4] + hexstr[i:i + 2] for i in range(0, len(hexstr or ''), 4)]
    live = []
    for w in ws:
        if int(w, 16) & 0x8000:
            break
        live.append(w)
    return ' '.join(live) if live else '-'


def main():
    names = [a for a in sys.argv[1:] if a in DRIVES] or list(DRIVES)
    for name in names:
        d = DRIVES[name]
        last = d[7]
        orows, ovis = run_oracle(d, name)
        prows, pev = run_port(d, name)
        lo, lp = series(orows, LIST[0], last), series(prows, LIST[0], last)
        bad = [f for f in range(last + 1) if lo[f] != lp[f]]
        print('door-list: %s list f0-f%d: %d frame(s) differ%s' % (name, last, len(bad),
                                                                    (' [' + runs(bad) + ']') if bad else ''))
        co = list(zip(*[series(orows, a, last) for a, _ in CELLS]))
        cp = list(zip(*[series(prows, a, last) for a, _ in CELLS]))
        if name in LIST_ONLY:                           # another location's map: its bytes are not the door's
            co = cp = [()] * (last + 1)
        else:
            bad = [f for f in range(last + 1) if co[f] != cp[f]]
            print('door-list: %s cells f0-f%d: %d frame(s) differ%s' % (name, last, len(bad),
                                                                         (' [' + runs(bad) + ']') if bad else ''))
        for who, l, c in (('cartridge', lo, co), ('port', lp, cp)):
            print('door-list: %s end list %s%s %s' % (name, words(l[last]), (' cells ' + '/'.join(c[last])) if c[last]
                                                      else '', who))
        print('door-list: %s end -> %s' % (name, 'same' if (lo[last], co[last]) == (lp[last], cp[last]) else 'DIFFER'))
        app = [e for e in pev if e[1] == 'C0/28C0']
        rea = ['f%d %s' % (f, rest) for f, w, rest in pev if w == 'C0/28E1']
        print('door-list: %s port %d append(s), C0/28E1 at %s' % (name, len(app), '; '.join(rea) if rea else 'none'))
        print('door-list: %s cartridge C0/28C0 %d visit(s), C0/28E1 %d visit(s)' % (name, ovis['28C0'], ovis['28E1']))


if __name__ == '__main__':
    main()
