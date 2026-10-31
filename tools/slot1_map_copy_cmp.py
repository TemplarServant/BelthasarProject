#!/usr/bin/env python3
r"""obj0's slot-1 tile copies on the map in WRAM, after a location load and after the menu's reload, port against the
logical oracle.

    python tools/slot1_map_copy_cmp.py [reload|load ...]

disasm/notes/obj0_slot1_runner.md section 6. The ROM builds the map in WRAM ($7E:3000.., C0/A33B) before it runs obj0's slot
1 (C0/595C) -- in the location load (C0/00F4 at 0088, then C0/56A6 -> C0/56C8) and in the menu's reload (C0/01CF, then
C0/0206) -- and slot 1's `E4` copies (C0/3D97 -> C0/AF4E) land on that fresh map. Loc 002's slot 1 is
`16 E4 E4 16 E4 E4`: `E4 13 02 1B 0C 13 12 3B` parks the curtain rect (rows $02-$0C, cols $13-$1B) at rows $12-$1C, and
with $7F:0142 bit 6 set `E4 33 32 3B 3C 13 02 3B` pastes the open curtain (rows $32-$3C) over it. Mask $3B = the planes
$7E:3000 / 3040 / 30C0 / 7000 / 7040.

  reload  saves/field_re/loc002_curtain_ctl.state (the curtain open, control on), X at f4, B at f60: the cartridge on
          --monitor, the port on CT_FIELD_MEMMON, the rect's eleven rows on all five planes, every frame.
  load    the same location loaded through each side's own loader -- the cartridge's --teleport (C0:0066) from that
          state, the port's --reentertest 2 from its seed -- and the curtain rect and the parked rect (22 rows x 5
          planes) read after the load.

Per drive:
  slot1-map: reload rect f0-fN: <k> frame(s) differ [runs]
  slot1-map: reload end $7E:3253 <9 bytes> $7E:3953 <9 bytes> cartridge|port
  slot1-map: reload end -> same|DIFFER (<n> of 55 rows)
  slot1-map: reload port C0/0206 ops <trail>
  slot1-map: reload cartridge C0/3D97 f<N> x<visits>       (op E4's handler: the copies ran on the cartridge)
  slot1-map: load rows <n>/110 identical
  slot1-map: load $7E:3253 <..> $7E:4253 <..> cartridge|port
  slot1-map: load port C0/56C8 ops <trail>
The run that differs in `reload` is the port's frame model, not the copies: the menu program's own use of $7E:3000
(f21-f90) and the reload's NMI-off frames (the ROM's C0/A33B at f82, C0/0206 at f100; the port does the reload's work at
the RTL frame, f69) -- the same run field-door-list-vs-logical-oracle names.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_slot1map')

STATE = 'saves/field_re/loc002_curtain_ctl.state'
SEED = 'saves/field_re/loc002_curtain_ctl.lakewram'
PLANES = (0x3000, 0x3040, 0x30C0, 0x7000, 0x7040)          # C0/AF4E's bases for mask $3B (bits 0, 1, 3, 4, 5)
CURTAIN = range(0x02, 0x0D)                                 # the E4's destination rows
PARKED = range(0x12, 0x1D)                                  # where the first E4 parks the rect
COL = 0x13
RELOAD_BTN, RELOAD_WALK, RELOAD_LAST = '0:4,200:2,0:54,1:2,0:100', 'n:5,m:1,n:55,b:1,n:100', 160
MONITOR_MAX = 22                                            # ct.exe --monitor takes 32 regions


def cells(rows):
    return [p + (r << 8) + COL for p in PLANES for r in rows]


def slot1_trail(stderr, site):
    last = '-'                                              # the last run at the site (a reentry's, not the seed's load)
    for l in stderr.splitlines():
        m = re.match(r'slot1: %s ops (.*?) obj0' % re.escape(site), l)
        if m:
            last = m.group(1)
    return last


def oracle_monitor(addrs, state, btn):
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    rows = []
    for k in range(0, len(addrs), MONITOR_MAX):
        watch = ','.join('7E%04X:9' % a for a in addrs[k:k + MONITOR_MAX])
        r = subprocess.run([CT, '--monitor', watch, '--from', state, '--btnscript', btn], cwd=ROOT, env=env,
                           capture_output=True, text=True, errors='replace')
        for l in r.stdout.splitlines():
            m = re.match(r'\s*f(\d+)\s+(.*)', l)
            if m:
                rows.append((int(m.group(1)), {int(a, 16): v for a, v in
                                               re.findall(r'7E([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}))
    return rows


def port_run(addrs, extra, frames):
    os.makedirs(OUT, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_SLOT1TRACE='1',
               CT_FIELD_MEMMON=','.join('%04X:9' % a for a in addrs))
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '2', '--loadwram', SEED, '--selftest',
                        os.path.join(OUT, '_end.raw'), '--frames', str(frames)] + extra,
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), {int(a, 16): v for a, v in
                                           re.findall(r'([0-9A-F]{4,5})=([0-9A-F]+)', m.group(2))}))
    return rows, r.stderr


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


def drive_reload():
    addrs = cells(CURTAIN)
    o = oracle_monitor(addrs, STATE, RELOAD_BTN)
    p, perr = port_run(addrs, ['--walkscript', RELOAD_WALK], RELOAD_LAST + 2)
    so = list(zip(*[series(o, a, RELOAD_LAST) for a in addrs]))
    sp = list(zip(*[series(p, a, RELOAD_LAST) for a in addrs]))
    bad = [f for f in range(RELOAD_LAST + 1) if so[f] != sp[f]]
    print('slot1-map: reload rect f0-f%d: %d frame(s) differ%s' % (RELOAD_LAST, len(bad),
                                                                   (' [' + runs(bad) + ']') if bad else ''))
    i1, i2 = addrs.index(0x3253), addrs.index(0x3953)
    for who, s in (('cartridge', so), ('port', sp)):
        print('slot1-map: reload end $7E:3253 %s $7E:3953 %s %s' % (s[-1][i1], s[-1][i2], who))
    nd = sum(1 for i in range(len(addrs)) if so[-1][i] != sp[-1][i])
    print('slot1-map: reload end -> %s (%d of %d rows)' % ('same' if nd == 0 else 'DIFFER', len(addrs) - nd, len(addrs)))
    print('slot1-map: reload port C0/0206 ops %s' % slot1_trail(perr, 'C0/0206'))
    r = subprocess.run([CT, '--hookpc', '3D97', '--from', STATE, '--btnscript', RELOAD_BTN, '--frames',
                        str(RELOAD_LAST + 2)], cwd=ROOT, env=dict(os.environ, CT_ORACLE_LOGICAL='1'),
                       capture_output=True, text=True, errors='replace')
    hits = re.findall(r'^\s*f(\d+)\s+visits=(\d+)', r.stdout, re.M)
    print('slot1-map: reload cartridge C0/3D97 %s' % (', '.join('f%s x%s' % h for h in hits if int(h[1])) or 'none'))


def drive_load():
    addrs = cells(CURTAIN) + cells(PARKED)
    os.makedirs(OUT, exist_ok=True)
    tp = os.path.join(OUT, '_tp002.state')
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    subprocess.run([CT, '--teleport', '2', '8', '8', '1', '--from', STATE, '--frames', '120', '--out', tp], cwd=ROOT,
                   env=env, capture_output=True, text=True, errors='replace')
    o = oracle_monitor(addrs, tp, '0:1')
    ov = {a: series(o, a, 1)[-1] for a in addrs}
    p, perr = port_run(addrs, ['--reentertest', '2'], 120)
    pv = {a: series(p, a, 120)[-1] for a in addrs}
    same = sum(1 for a in addrs if ov[a] is not None and ov[a] == pv[a])
    print('slot1-map: load rows %d/%d identical' % (same, len(addrs)))
    for who, v in (('cartridge', ov), ('port', pv)):
        print('slot1-map: load $7E:3253 %s $7E:4253 %s %s' % (v[0x3253], v[0x4253], who))
    print('slot1-map: load port C0/56C8 ops %s' % slot1_trail(perr, 'C0/56C8'))


def main():
    names = [a for a in sys.argv[1:] if a in ('reload', 'load')] or ['reload', 'load']
    for n in names:
        (drive_reload if n == 'reload' else drive_load)()


if __name__ == '__main__':
    main()
