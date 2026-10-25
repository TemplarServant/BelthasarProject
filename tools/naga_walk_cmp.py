#!/usr/bin/env python3
r"""The four Naga-ettes' positions in the Manoria cathedral fight, port against the rule's cartridge, frame by frame.

    python tools/naga_walk_cmp.py [frames]

The fight saves/field_re/m81_nuns_lake.state starts by itself (no input on either side). The enemies walk to the PCs and
back on their own (the movement modes, C1/3714, and the walkers, CF/F978), and their turns -- the kiss with its name box
(CD/0295), the moves with no action -- hold the main loop inside the handler. What a Cyclone or a Fire Whirl hits is the
enemies within radius^2 9 of the target in 16-px cells (C1/C1DD -> C1/C586 area 3 -> C1/2701, `$1D0C` / `$1D23 >> 4`),
so a pixel there decides a victim. (The user, 2026-10-04: "when crono used cyclone hitting 3 of them, only 1 took
damage / with fire whirl this also happened".)

  cartridge  build/ct.exe --monitor 7E1D0F:4,7E1D26:4 (the rule: CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1)
  port       build/Belthasar.exe --scene 129 --loadwram saves/field_re/m81_nuns_lake.lakewram, CT_BATTLE_POSTRACE
             (every combatant's live foot at the frame's end)

Aligned on each side's first enemy step; compared over `frames`:

    naga-walk: aligned c<C> p<P>; <n> frames, <k> differ [runs ...]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def dense(D):
    out, last = {}, None
    ks = sorted(D)
    for f in range(ks[0], ks[-1] + 1):
        if f in D:
            last = D[f]
        out[f] = last
    return out


def first_move(D):
    base = None
    for f in sorted(D):
        v = D[f]
        if v is None or v[0][0] == 0 or v[0] == (0, 0x23):
            continue
        if base is None:
            base = v
        elif v != base:
            return f
    return None


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 3600
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1')
    c = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', '7E1D0F:4,7E1D26:4', '--diff', '--from',
                        'saves/field_re/m81_nuns_lake.state', '--btnscript', '0:%d' % (frames + 900)],
                       cwd=ROOT, env=env, capture_output=True, text=True).stdout.splitlines()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_BATTLE_SCRIPT='n:%d' % (frames + 900), CT_BATTLE_POSTRACE='1')
    p = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '129',
                        '--loadwram', 'saves/field_re/m81_nuns_lake.lakewram', '--selftest', os.devnull,
                        '--frames', str(frames + 500)],
                       cwd=ROOT, env=env, capture_output=True, text=True).stderr.splitlines()
    C, P = {}, {}
    for l in c:
        m = re.match(r'\s+f(\d+)\s+7E1D0F=([0-9A-F]{8})\*?\s+7E1D26=([0-9A-F]{8})', l)
        if m:
            C[int(m.group(1))] = tuple((int(m.group(2)[2 * i:2 * i + 2], 16), int(m.group(3)[2 * i:2 * i + 2], 16))
                                       for i in range(4))
    for l in p:
        if l.startswith('bpos f'):
            q = l.split()
            P[int(q[1][1:])] = tuple((int(x[:2], 16), int(x[2:], 16)) for x in q[5:9])
    if not C or not P:
        print('naga-walk: FAILED -- a side produced no positions (cartridge %d, port %d rows)' % (len(C), len(P)))
        return 1
    C, P = dense(C), dense(P)
    cf, pf = first_move(C), first_move(P)
    if cf is None or pf is None:
        print('naga-walk: FAILED -- no enemy step (cartridge %s, port %s)' % (cf, pf))
        return 1
    n, bad, runs, cur = 0, 0, [], None
    for d in range(frames):
        a, b = C.get(cf + d), P.get(pf + d)
        if a is None or b is None:
            break
        n += 1
        if a != b:
            bad += 1
            if cur is None:
                cur = [d, d]
            else:
                cur[1] = d
        elif cur:
            runs.append(cur)
            cur = None
    if cur:
        runs.append(cur)
    rs = ' '.join('+%d..+%d' % (r[0], r[1]) for r in runs[:6])
    print('naga-walk: aligned c%d p%d; %d frames, %d differ%s' % (cf, pf, n, bad, (' [' + rs + ']') if runs else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
