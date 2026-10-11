#!/usr/bin/env python3
r"""The battle's effect objects (ops 02-07/26, 6E-71, 73/74, D9/DA; CC/F278) against the cartridge, frame by frame.

    python tools/fxobj_cmp.py <case> [frames]

Cases (the oracle state + its drive, the port fixture + its CT_BATTLE_SCRIPT):
    cyclone   saves/battle_re/gato_cyclone.state  /  saves/battle_re/gato_cyclone.raw.wram  (Crono's Cyclone)
    song      saves/battle_re/gato_cyclone.state  /  saves/battle_re/gato_cyclone.raw.wram  (GATO's song after the Cyclone)
    zombor    build/_z_f7085.state (made from the attract)  /  saves/attract_zombor_pre.lakewram  (the 087 triple tech)

Both sides stream the same bytes -- $A1A8 (mode) $A1B0 (dwell) $A1B8 (cursor) $A2C0 (frame) $A1D8 (shown) $AB5F
(priority) $A230 (facing) $A260/$A270 (the record's offsets) and the $A3DF matrix -- the oracle with --monitor, the
port with CT_BATTLE_FXOBJTRACE. Each side is aligned on the first frame an object is armed in the chosen action (the
first at or after a per-case frame), and the next <frames> frames are compared field by field. Prints

    fxobj-cmp: <case> oracle arm f<a> port arm f<b>: object field sequences <same>/<n> identical, matrix sequence identical, ...
"""
import os
import re
import subprocess
import sys

FIELD = os.path.abspath('build/ct_field.exe')
CT = os.path.abspath('build/ct.exe')
CASES = {
    'cyclone': dict(state='saves/battle_re/gato_cyclone.state',
                    drive='100:8,0:372,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:300', oframes=760,
                    wram='saves/battle_re/gato_cyclone.raw.wram', script='n:40,d:2,n:10,a:2,n:10,a:2,n:10,a:2,n:600',
                    omin=0, pmin=0),
    'zombor': dict(state='build/_z_f7085.state', drive='', oframes=800,
                   pargs=['--scene', '135', '--loadwram', 'saves/attract_zombor_pre.lakewram', '--frames', '700'],
                   penv={'CT_AUDIO_LOCKSTEP': '1'}, omin=0, pmin=0),
    'song': dict(state='saves/battle_re/gato_cyclone.state',
                 drive='100:8,0:372,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:3000', oframes=2000,
                 wram='saves/battle_re/gato_cyclone.raw.wram', script='n:40,d:2,n:10,a:2,n:10,a:2,n:10,a:2,n:3000',
                 omin=1500, pmin=600),
}
FIELDS = [('A1A8', 8), ('A1B0', 8), ('A1B8', 8), ('A2C0', 8), ('A1D8', 8), ('AB5F', 8), ('A230', 8), ('A260', 16), ('A270', 16)]


def arms(rows):
    """Indices of rows where some object's mode goes from 0 to non-zero."""
    out, prev = [], None
    for i, (f, d) in enumerate(rows):
        cur = d['A1A8']
        if prev is not None:
            if any(int(cur[2 * k:2 * k + 2], 16) and not int(prev[2 * k:2 * k + 2], 16) for k in range(8)):
                out.append(i)
        prev = cur
    return out


def oracle(c):
    regs = ','.join('%s:%d' % (n, l) for n, l in FIELDS) + ',A3DF:88'
    if not os.path.exists(c['state']) and c['state'] == 'build/_z_f7085.state':   # the 087 leg's start (zombor_orbit_cmp.py)
        subprocess.run([CT, '--peek', 'saves/attract_cena_inicial.state', 'build/_ztmp.raw', '--frames', '7085'],
                       env=dict(os.environ, PEEK_SAVE=c['state']), capture_output=True)
    argv = [CT, '--monitor', regs, '--from', c['state'], '--frames', str(c['oframes'])]
    if c['drive']:
        argv += ['--btnscript', c['drive']]
    r = subprocess.run(argv, capture_output=True, text=True, errors='replace')
    rows = []
    for line in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)$', line)
        if not m:
            continue
        d = {}
        for n, v in re.findall(r'00([0-9A-F]{4})=([0-9A-F]+)', m.group(2)):
            d[n] = v
        if all(n in d for n, _ in FIELDS) and 'A3DF' in d:
            mat = d['A3DF']
            d['A3DF'] = ','.join('%02X:%s' % (k, mat[2 * k:2 * k + 2]) for k in range(0x58) if mat[2 * k:2 * k + 2] != '00')
            rows.append((int(m.group(1)), d))
    return rows


def port(c):
    env = dict(os.environ, CT_BATTLE_FXOBJTRACE='1', CT_NOPACE='1', SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
    if c.get('script'):
        env['CT_BATTLE_SCRIPT'] = c['script']
    env.update(c.get('penv', {}))
    args = c['pargs'] if 'pargs' in c else ['--battletest', c['wram']]
    if 'pargs' in c:
        env.update(CT_SHOTS='build/_fxobjcmp', CT_SHOTS_AT=c['pargs'][-1], CT_SHOTS_EXIT='1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat'] + args, env=env,
                       capture_output=True, text=True, errors='replace')
    rows, last = [], None
    for line in (r.stdout + r.stderr).splitlines():
        m = re.match(r'fxobj f(\d+) (.*)$', line)
        if not m:
            continue
        d = dict(re.findall(r'(\w{4})=([0-9A-F:,]*)', m.group(2)))
        d['A3DF'] = d.get('A3DF', '').rstrip(',')
        f = int(m.group(1))
        if last is None:                              # the frame before the first trace line: nothing armed
            z = {n: '00' * l for n, l in FIELDS}
            z['A3DF'] = ''
            rows.append((f - 1, z))
        # the trace is silent while nothing is armed or shown: fill the gap with an all-zero row
        if last is not None and f > last + 1:
            for g in range(last + 1, f):
                z = {n: '00' * l for n, l in FIELDS}
                z['A3DF'] = rows[-1][1]['A3DF'] if rows else ''
                z['AB5F'] = rows[-1][1]['AB5F'] if rows else z['AB5F']
                rows.append((g, z))
        rows.append((f, d))
        last = f
    return rows


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'cyclone'
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 120
    c = CASES[name]
    orows, prows = oracle(c), port(c)
    oa, pa = arms(orows), arms([(f, d) for f, d in prows])
    oa = [i for i in oa if orows[i][0] >= c['omin']]
    pa = [i for i in pa if prows[i][0] >= c['pmin']]
    if not oa or not pa:
        sys.exit('fxobj-cmp: %s: no arm (oracle %s, port %s)' % (name, oa, pa))
    oi, pi = oa[0], pa[0]
    # The cartridge runs the action loop -- and so CC/F278 and the script -- once per ITERATION, and an iteration
    # sometimes spans two frames or more (the CPU clock; the port runs one every frame). So each OBJECT is compared on
    # its own sequence of distinct states (a state held over frames is one iteration on screen), and the matrix on
    # its own: both are independent of how the iterations fall on frames.
    def seq(rows, i, key):
        out = []
        for f, d in rows[i:i + n]:
            k = key(d)
            if not out or out[-1][1] != k:
                out.append((f, k))
        return out
    held = sum(1 for k in range(1, min(n, len(orows) - oi)) if orows[oi + k][1] == orows[oi + k - 1][1])
    # ...and field by field: when an iteration spans a frame boundary the cartridge's sample can fall between CC/F278 and
    # the script, a state the port never shows -- but each FIELD is still written once per iteration, so each object's
    # sequence of distinct values per field is the same whatever the sampling.
    total = same = 0
    first = None
    lens = []
    for o in range(8):
        for fi, (fld, l) in enumerate(FIELDS):
            w = l // 8
            def key(d, o=o, fld=fld, w=w):
                return d.get(fld, '')[2 * o * w:2 * o * w + 2 * w]
            so, sp = seq(orows, oi, key), seq(prows, pi, key)
            m = min(len(so), len(sp))
            total += 1
            if [v for _, v in so[:m]] == [v for _, v in sp[:m]]:
                same += 1
            elif first is None:
                k = next(k for k in range(m) if so[k][1] != sp[k][1])
                first = 'obj%d %s value %d (oracle f%d, port f%d): oracle %s port %s' % (o, fld, k, so[k][0], sp[k][0], so[k][1], sp[k][1])
            if len(so) != len(sp):
                lens.append('obj%d.%s %d/%d' % (o, fld, len(so), len(sp)))
    mo = [k for _, k in seq(orows, oi, lambda d: d.get('A3DF', ''))]
    mp = [k for _, k in seq(prows, pi, lambda d: d.get('A3DF', ''))]
    print('fxobj-cmp: %s oracle arm f%d port arm f%d: object field sequences %d/%d identical, matrix sequence %s, oracle frames held %d%s%s'
          % (name, orows[oi][0], prows[pi][0], same, total, 'identical' if mo == mp else 'differs', held,
             '' if not lens else ', sequence lengths (oracle/port) ' + ' '.join(lens),
             '' if first is None else ' (first difference: %s)' % first))


if __name__ == '__main__':
    main()
