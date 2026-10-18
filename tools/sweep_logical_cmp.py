#!/usr/bin/env python3
r"""A mid-battle RENDERWRAM sweep of the port against the LOGICAL-FRAME oracle's continuation of the same seed (the zelda3
model, CLAUDE.md's one exception; disasm/notes/oracle_logical_frames.md §4).

    python tools/sweep_logical_cmp.py <base.state> <frames> <seed.raw.wram | -> <mv|idle> <n> [slot ...]

The seed: the cartridge run `--peek <base.state> --btnscript 0:<frames>` in real time -- the same drive that produced
<seed.raw.wram>, whose bytes are checked identical to the run's WRAM -- saved as a LakeSnes state (PEEK_SAVE, cached
under build/_sweeps/). The reference: that state run <n> more frames on CT_ORACLE_LOGICAL=1, `--monitor` of the
per-slot bytes. The port: build/Belthasar.exe on CT_BATTLE_RENDERWRAM=<seed> with CT_BATTLE_MVSWEEP=<n> (mv) or
CT_BATTLE_IDLESWEEP=<n> (idle). A seed of `-` is the base state's own WRAM (<frames> 0): a committed image taken
apart from the state (bat2.raw.wram differs from bat2_lake.state in 84 bytes, $E5 among them -- the byte the sweep
reads its iteration phase from) cannot be continued by the state. The sweep frame sf=k is the monitor's f(k-1)
for a PEEK_SAVE seed, whose saved state sits one frame after its WRAM dump (wa2158: the dump's $E5 = 1 / $A4 = 01
read 00 / 02 at the monitor's f0 -- the NMI's INC and C1/10BB), and f(k) for the base state itself.

  mv   for every `battlemove f<k> slot<s> ...` line of the given slots: the slot's live foot ($1D0C/$1D23 + cmb) and
       mode ($974A + slot) on the oracle's frame k -- the mover's segment steps (CF/F978, every NMI) and the mode
       machine's commits and passes (C1/3714 on both frames of the iteration)
  idle for every `idletick f<k> slot=<s> ... cur=<c> tmr=<t>` line: the slot's window cursor $969C and dwell timer $9694
       on the oracle's frame k -- the walk dispatch C1/10A9, one group per iteration on its frame A
The port's own trace lines are echoed after the verdict, so a check can pin their detail as well.
"""
import os
import re
import subprocess
import sys


def main():
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    base, frames, seed, mode, n = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], int(sys.argv[5])
    slots = [int(s) for s in sys.argv[6:]]
    ct = os.path.abspath('build/ct.exe')
    os.makedirs('build/_sweeps', exist_ok=True)
    tag = 'build/_sweeps/%s_%d' % (os.path.splitext(os.path.basename(base))[0], frames)
    env = dict(os.environ)
    env.pop('CT_ORACLE_LOGICAL', None)
    if not os.path.exists(tag + '.state'):
        subprocess.run([ct, '--peek', base, tag + '.raw', '--btnscript', '0:%d' % frames],
                       env=dict(env, PEEK_SAVE=tag + '.state'), capture_output=True)
    if seed == '-':
        seed = tag + '.raw.wram'
    a = open(tag + '.raw.wram', 'rb').read(); b = open(seed, 'rb').read()
    lag = 1 if frames > 0 else 0
    diff = sum(1 for i in range(min(len(a), len(b))) if a[i] != b[i]) + abs(len(a) - len(b))
    print('sweep-logical: seed %s = the cartridge\'s WRAM after %d frames of %s: %s' % (
        os.path.basename(seed), frames, os.path.basename(base), 'identical' if diff == 0 else '%d bytes differ' % diff))
    mon = '7E974A:8,7E1D0C:11,7E1D23:11' if mode == 'mv' else '7E969C:8,7E9694:8'
    r = subprocess.run([ct, '--monitor', mon, '--from', tag + '.state' if frames > 0 else base, '--frames', str(n + 2), '--diff'],
                       env=dict(env, CT_ORACLE_LOGICAL='1'), capture_output=True, text=True, errors='replace')
    orc, cur = {}, {}
    for l in (r.stdout + r.stderr).splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if m:
            for k, v in re.findall(r'(7E[0-9A-F]{4})=([0-9A-F]+)', m.group(2)):
                cur[k[2:]] = bytes.fromhex(v)
            orc[int(m.group(1))] = dict(cur)
    last = {}
    for f in range(0, n + 3):
        if f in orc:
            last = orc[f]
        orc[f] = last
    penv = dict(env, CT_BATTLE_RENDERWRAM=seed, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1')
    if mode == 'mv':
        penv.update(CT_BATTLE_MVSWEEP=str(n), CT_BATTLE_MVTRACE='1', CT_BATTLE_WALKTRACE='1')
    else:
        penv.update(CT_BATTLE_IDLETRACE='1', CT_BATTLE_IDLESWEEP=str(n), CT_BATTLE_WALKTRACE='1')
    p = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat'], env=penv,
                       capture_output=True, text=True, errors='replace')
    rows = []
    for l in (p.stdout + p.stderr).splitlines():
        if mode == 'mv':
            m = re.match(r'battlemove f(\d+) slot(\d) cmb(\d+) mode(\d+) ph\d+ live=\((\d+),(\d+)\)', l)
            if m and int(m.group(2)) in slots:
                k, s, c, md, x, y = (int(v) for v in m.groups())
                o = orc.get(k - lag, {})
                want = (o['974A'][s], o['1D0C'][c], o['1D23'][c]) if o else None
                rows.append((k, s, (md, x, y), want))
        else:
            m = re.match(r'idletick f(\d+) slot=(\d) pose=\S+ chg=\d cur=(\d+) tmr=(\d+)', l)
            if m and (not slots or int(m.group(2)) in slots):
                k, s, c, t = (int(v) for v in m.groups())
                o = orc.get(k - lag, {})
                want = (o['969C'][s], o['9694'][s]) if o else None
                rows.append((k, s, (c, t), want))
    bad = [r for r in rows if r[2] != r[3]]
    echo = [l for l in (p.stdout + p.stderr).splitlines() if re.match(r'(battlemove|mv\w+|idle\w+|pc\w+) f\d+ ', l)]
    what = 'mover steps (mode, foot)' if mode == 'mv' else 'idle-window ticks (cursor, timer)'
    if not rows:
        print('sweep-logical: the port printed no %s' % what)
    elif not bad:
        print('sweep-logical: %d %s of slot(s) %s over sf%d-sf%d, all the logical oracle\'s' % (
            len(rows), what, ','.join(str(s) for s in sorted(set(r[1] for r in rows))), rows[0][0], rows[-1][0]))
    else:
        k, s, got, want = bad[0]
        print('sweep-logical: %d of %d %s off the logical oracle, first sf%d slot%d: port %s oracle %s' % (
            len(bad), len(rows), what, k, s, got, want))
    for l in echo:
        print(l)


if __name__ == '__main__':
    main()
