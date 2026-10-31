#!/usr/bin/env python3
r"""Op 0xE5's queued tile copy and the $0117 bit-5 service C0/1770, port against the logical oracle.

    python tools/e5_copy_cmp.py [curtain|intro ...]

disasm/notes/field_tile_copy_e5.md. Op 0xE5 (C0/3DC5) only stages $013E-$0145 and sets $0117 bit 5; at the start of
the next frame the main loop's dispatch reaches C0/1770, which runs one event frame inside its align loop, copies the
rect in the WRAM map (`1790 JSR C0/AF4E`) and then holds the main loop in the style branch on $0145 (2 frames for
BG1/BG2/BG3 alone, 4 for BG1+BG2) while the tilemaps are rebuilt -- no event VM, no player, no box machine.
Regression check: field-e5-copy-service-vs-logical-oracle.

  curtain  saves/field_re/loc002_curtain_ctl.state: walk up to loc 002's curtain and press A (obj10's fn1: E5 `..3C`,
           style 4, then E5 `..3B`, style 3). Every frame, both sides: $0117 $0138 $013E-$0146 $0153, obj10's cadence
           $1015 and ip $1194, the layer words $1D7C-$1D83 $1D87-$1D92 $1D99-$1D9E, and the curtain rect (rows $02-$0C,
           cols $13-$1B) on the six planes C0/AF4E writes. And the picture: a thumbnail per frame, the port's P against
           the cartridge's P+2 (docs/TESTING.md, the alignment), from f5 (f1-f4 differ in the lead's sprite rows at the
           seed, outside this mechanism).
  intro    saves/field_re/loc002_intro_curtain.state: the new game's own opening, Mom opening the curtain (E5 `..3B`
           then `..3C`), the same bytes, and the picture from f0: both copies, obj10's walk and the first frame of the
           message box's open ramp (the band FD/C2C1 builds from this iteration's $0127). Graded to last - 3 = f57, as
           the curtain is; from P58 the ramp's bottom edge row and then the window's left-edge weave differ (textbox.md's
           two open items, not this mechanism).

Per drive:
  e5-copy: <drive> wram f0-fN: <k> frame(s) differ [runs]
  e5-copy: <drive> copies f<a> f<b> cartridge|port        (the frames the rect's planes change)
  e5-copy: <drive> held f<..> cartridge|port              (the frames obj10's cadence $1015 does not step)
  e5-copy: <drive> picture f<first>-fN: <k> frame(s) differ [runs]
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_e5copy')

DRIVES = {
    'curtain': dict(state='saves/field_re/loc002_curtain_ctl.state', seed='saves/field_re/loc002_curtain_ctl.lakewram',
                    btn='0:4,10:40,0:10,100:2,0:61', walk='n:5,u:40,n:10,a:2,n:60', last=116, picture=5),
    'intro': dict(state='saves/field_re/loc002_intro_curtain.state', seed='saves/field_re/loc002_intro_curtain.lakewram',
                  btn='0:61', walk=None, last=60, picture=0),
}
PLANES = (0x3000, 0x3040, 0x3080, 0x30C0, 0x7000, 0x7040)   # C0/AF4E's bases for mask bits 0-5
ROWS = range(0x02, 0x0D)
COL = 0x13
BYTES = [(0x0117, 1), (0x0138, 1), (0x013E, 9), (0x0153, 1), (0x1015, 1), (0x1194, 2),
         (0x1D7C, 8), (0x1D87, 12), (0x1D99, 6)] + [(p + (r << 8) + COL, 9) for p in PLANES for r in ROWS]
MONITOR_MAX = 22                                            # ct.exe --monitor takes 32 regions


def oracle_monitor(state, btn):
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    rows = []
    for k in range(0, len(BYTES), MONITOR_MAX):
        watch = ','.join('7E%04X:%d' % (a, n) for a, n in BYTES[k:k + MONITOR_MAX])
        r = subprocess.run([CT, '--monitor', watch, '--from', state, '--btnscript', btn], cwd=ROOT, env=env,
                           capture_output=True, text=True, errors='replace')
        for l in r.stdout.splitlines():
            m = re.match(r'\s*f(\d+)\s+(.*)', l)
            if m:
                rows.append((int(m.group(1)), {int(a, 16): v for a, v in
                                               re.findall(r'7E([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}))
    return rows


def port_run(d, thumbs):
    os.makedirs(OUT, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_FIELD_MEMMON=','.join('%04X:%X' % (a, n) for a, n in BYTES))   # (its lengths are hex)
    if thumbs:
        env.update(CT_SHOTS_THUMBS=thumbs, CT_SHOTS_THUMBS_EVERY='1')
    argv = [FIELD, '--assets', 'ct_assets.dat', '--scene', '2', '--loadwram', d['seed'], '--selftest',
            os.path.join(OUT, '_end.raw'), '--frames', str(d['last'] + 1)]
    if d['walk']:
        argv += ['--walkscript', d['walk']]
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


def thumbs(path):
    rec = 6 + 128 * 112 * 3
    data = open(path, 'rb').read() if os.path.exists(path) else b''
    return {struct.unpack('<I', data[i * rec:i * rec + 4])[0]: data[i * rec + 6:(i + 1) * rec]
            for i in range(len(data) // rec)}


def drive(name):
    d = DRIVES[name]
    last = d['last']
    tp = os.path.join(OUT, '_%s_port.thumbs' % name)
    to = os.path.join(OUT, '_%s_cart.thumbs' % name)
    for p in (tp, to):
        if os.path.exists(p):
            os.remove(p)
    o = oracle_monitor(d['state'], d['btn'])
    p = port_run(d, tp if d['picture'] is not None else None)
    last = min(last, max(f for f, _ in o))                  # --monitor's rows stop a frame before the script's end
    addrs = [a for a, _ in BYTES]
    so = list(zip(*[series(o, a, last) for a in addrs]))
    sp = list(zip(*[series(p, a, last) for a in addrs]))
    bad = [f for f in range(last + 1) if so[f] != sp[f]]
    print('e5-copy: %s wram f0-f%d: %d frame(s) differ%s' % (name, last, len(bad), (' [' + runs(bad) + ']') if bad else ''))
    rect = [addrs.index(pl + (r << 8) + COL) for pl in PLANES for r in ROWS]
    i1015 = addrs.index(0x1015)
    for who, s in (('cartridge', so), ('port', sp)):
        cp = [f for f in range(1, last + 1) if any(s[f][i] != s[f - 1][i] for i in rect)]
        held = [f for f in range(1, last + 1) if s[f][i1015] is not None and s[f][i1015] == s[f - 1][i1015]]
        print('e5-copy: %s copies %s %s' % (name, ' '.join('f%d' % f for f in cp) or '-', who))
        print('e5-copy: %s held %s %s' % (name, runs(held) or '-', who))
    if d['picture'] is not None:
        env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_THUMBS=to, PEEK_THUMBS_EVERY='1')
        subprocess.run([CT, '--peek', d['state'], os.path.join(OUT, '_%s_cart.raw' % name), '--btnscript', d['btn']],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
        po, pc = thumbs(tp), thumbs(to)
        first, end = d['picture'], last - 3                  # (the first graded frame, per drive: see the docstring)
        pbad = [f for f in range(first, end + 1) if f not in po or f + 2 not in pc or po[f] != pc[f + 2]]
        print('e5-copy: %s picture f%d-f%d: %d frame(s) differ%s' % (name, first, end, len(pbad),
                                                                     (' [' + runs(pbad) + ']') if pbad else ''))


def main():
    names = [a for a in sys.argv[1:] if a in DRIVES] or list(DRIVES)
    for n in names:
        drive(n)


if __name__ == '__main__':
    main()
