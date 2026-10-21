#!/usr/bin/env python3
r"""The location load, port against cartridge, load by load over the playthrough's route (tools/playthrough/route.txt).

    python tools/load_cost_cmp.py [leg ...]                 the frames: commit -> first ramp INC, both sides
    python tools/load_cost_cmp.py --cycles [leg ...]        ...and every load's work, leg by leg of the walk
    python tools/load_cost_cmp.py --cycles --keep [leg ...] ...keeping the beam traces in build/_loadcost/ (~200 MB each:
                                                            what tools/load_cost_consts.py reads)

FRAMES. A load commits ($0100 changes, C0/0C9A) and its ramp's first INC lands $0119 = 01 (C0/284D). The cartridge is the
logical oracle (CT_ORACLE_LOGICAL=1) under the leg's pad, the port Belthasar from the leg's start state; each prints
`<loc> oracle <commit>-><ramp> d=<n>  port ... d=<n>` and the count that agree.

CYCLES. The cartridge's load is beam-traced (`ct.exe --cputrace`, CPUTRACE_BEAM=1, from C0/0B86) and summed: its CPU work
from the NMI-off write to C0/2848's first vblank wait (C0/0B5D -> 0B86 is a constant 716; C0/0B86 -> C0/2848 the window;
C0/2848 -> C0/0B38 the pass) less the sound driver's (C7: the loader clock's, priced by the port's SPC), and by leg: the
packets (C0/0960 -> C0/56D4, + the walk's 7 JSRs), the pass, the object pass's ops (every C0/5910 / C0/5977 dispatch +
CT_VM_DISPATCH). The port's are its `loadwalk:` line (CT_LOADCOST=1; src/field_cost.c ct_field_load_cycles, the ops priced
as the VM runs them). A load the walk refuses names the leg (`NOT WALKED`) and is charged ct_lz_load_cost instead.
disasm/notes/field_load_cost.md section 9.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('PT_TMP', os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'build', '_lcc'))
import playthrough as p  # noqa: E402
from ctlib import beam  # noqa: E402

KEEP = os.path.join(p.ROOT, 'build', '_loadcost')
F = 346888


def cut(pad, a, b):
    o, t = [], 0
    for m, k in pad:
        s, e = max(t, a), min(t + k, b)
        if e > s:
            o.append((m, e - s))
        t += k
    return o


def loads(tl):
    """(loc, commit frame, ramp frame) for every change of $0100 the timeline holds"""
    out, prev, pend = [], None, None
    for f, kv in tl:
        loc = kv['loc']
        if prev is not None and loc != prev:
            pend = (f, p.loc_of(loc))
        if pend and kv['bri'].upper() == '01' and f > pend[0]:
            out.append((pend[1], pend[0], f))
            pend = None
        prev = loc
    return out


def port_run(start, pad):
    """the port over the leg with CT_LOADCOST: its `loadwalk:` lines"""
    w = p.wram_of(start)
    lw = os.path.join(p.TMP, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=start, CT_WS_BATTLE_LEAD='1', CT_LOADCOST='1', CT_SHOTS_EXIT='1',
               CT_SHOTS=os.path.join(p.TMP, 'p'), CT_SHOTS_AT=str(sum(n for _, n in pad)))
    r = subprocess.run([p.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw,
                        '--walkscript', p.walkscript(pad)], cwd=p.ROOT, env=env, capture_output=True, text=True,
                       errors='replace')
    return [l for l in r.stdout.splitlines() if l.startswith('loadwalk:')]


def cart_trace(start, pad, frame, keep_as=None):
    """the cartridge's load beam-traced from C0/0B86, summed (see the module doc)"""
    a = max(0, frame - 3)
    st = os.path.join(p.TMP, 'pre.state')
    src = start
    if a:
        p.oracle_play(start, cut(pad, 0, a), st)
        src = st
    tr = keep_as or os.path.join(p.TMP, 'lc.trace')
    env = dict(p.RULE, CPUTRACE_BEAM='1', CPUTRACE_FROM='C00B86', CPUTRACE_BTN=p.btnscript(cut(pad, a, a + 160)) or '0:160')
    subprocess.run([p.CT, '--cputrace', src, '160', tr], env=env, capture_output=True)
    rows = beam.load(tr)
    cyc = [beam.nonrefresh(rows[k], rows[k + 1]) for k in range(len(rows) - 1)] + [0]
    at = lambda pc, s=0: next(k for k in range(s, len(rows)) if rows[k][0] == pc)
    i = at('C0:0B86'); j = at('C0:2848', i); w = at('C0:0B38', j)
    c7 = sum(cyc[k] for k in range(i, j) if rows[k][0].startswith('C7'))
    out = {'cpu': 716 + sum(cyc[i:w]) - c7, 'c7': c7, 'pass': sum(cyc[j:w])}
    # the packets: from C0/0960's first instruction to C0/0112 (the JSL FD:FFFA) -- the routines and C0/00F4's seven JSRs
    # between them, as the walk counts them
    k0 = at('C0:0960', i); k1 = at('C0:0112', k0)
    out['packets'] = sum(cyc[k0:k1])
    ops, k = 0, i
    while k < j:
        if rows[k][0] in ('C0:5910', 'C0:5977'):
            back = 'C0:5913' if rows[k][0] == 'C0:5910' else 'C0:597A'
            m = k + 1
            while rows[m][0] != back:
                m += 1
            ops += sum(cyc[t] for t in range(k, m) if not rows[t][0].startswith('C7')) + 152
            k = m
        k += 1
    out['vm'] = ops
    if not keep_as:
        os.remove(tr)
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    cycles, keep = '--cycles' in sys.argv, '--keep' in sys.argv
    os.makedirs(p.TMP, exist_ok=True)
    os.makedirs(KEEP, exist_ok=True)
    legs = p.route()
    todo = [i for i, lg in enumerate(legs) if i and (not args or lg['name'] in args)]
    if todo:
        p.ensure_states(legs, max(todo) - 1)        # the route's states, from power-on where missing or stale
    tot = same = 0
    for i, lg in enumerate(legs):
        if i == 0 or (args and lg['name'] not in args):
            continue
        start = p.start_of(legs, i)
        o = loads(p.oracle_timeline(start, lg['pad']))
        q = loads(p.port_timeline(start, lg['pad'])[0])
        walks = port_run(start, lg['pad']) if cycles else []
        print('==', lg['name'], flush=True)
        for k in range(max(len(o), len(q))):
            a = o[k] if k < len(o) else None
            b = q[k] if k < len(q) else None
            da = a[2] - a[1] if a else None
            db = b[2] - b[1] if b else None
            tot += 1
            same += da == db
            print('  %-5s oracle %-14s d=%-4s port %-14s d=%-4s %s' % (
                a[0] if a else (b[0] if b else '?'), '%d->%d' % a[1:] if a else '-', da, '%d->%d' % b[1:] if b else '-', db,
                '' if da == db else '<<< %+d' % ((db or 0) - (da or 0))), flush=True)
            if cycles and a:
                c = cart_trace(start, lg['pad'], a[1], os.path.join(KEEP, '%s_%s.trace' % (lg['name'], a[0])) if keep else None)
                wl = next((x for x in walks if int(x.split()[2]) == int(a[0], 16)), None)
                if wl:
                    walks.remove(wl)
                    v = dict(re.findall(r'(\w+) (\d+)', wl.split(' -- ', 1)[1]))
                    model = sum(int(v[n]) for n in ('packets', 'anim', 'objects', 'party', 'pass', '9dc3', 'vm'))
                    print('        cart cpu %d (c7 %d): packets %+d pass %+d vm %+d%s' % (
                        c['cpu'], c['c7'], int(v['packets']) - c['packets'], int(v['pass']) - c['pass'],
                        int(v['vm']) - c['vm'], (' -- ' + wl.split(' -- ', 2)[2]) if wl.count(' -- ') > 1 else ''))
                else:
                    print('        cart cpu %d (c7 %d): the port walked no load of it' % (c['cpu'], c['c7']))
    print('loadcost: %d loads, %d with the cartridge\'s frames' % (tot, same))


if __name__ == '__main__':
    main()
