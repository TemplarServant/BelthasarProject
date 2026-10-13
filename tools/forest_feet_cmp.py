#!/usr/bin/env python
"""The forest fight's movement, port against the rule's oracle, frame for frame.

The cartridge: saves/battle_re/forest_holdleft_pre.state with `--input 40:2600` under CT_ORACLE_LOGICAL=1, its
three Hetakes' feet ($1D0F-$1D11 X, $1D26-$1D28 Y) streamed by `--monitor --diff`. The port: the same fight from
saves/battle_re/forest_pre.raw.wram (`--battletest`, CT_BATTLE_SCRIPT=n:40, CT_BATTLE_DEBUGEND=2400) with the same
bytes streamed by CT_BATTLE_MEMMON. The port's fight runs FRAME_OFS frames earlier than the cartridge's (its fixture
starts later); every change of the six bytes is compared in value and in frame.

Prints `forest-feet: <k> of <n> changes equal in value and frame (port + 172)`, then the first difference if any.
Used by the check battle-forest-feet-vs-oracle.
"""
import os, re, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
FRAME_OFS = 172
PORT_END = 2400


def run(cmd, env_extra, merge=False):
    env = dict(os.environ)
    env.update(env_extra)
    r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT if merge else subprocess.DEVNULL, timeout=1800)
    return r.stdout.decode('utf-8', 'replace').splitlines()


def main():
    ct = os.path.join('build', 'ct.exe')
    cf = os.path.join('build', 'ct_field.exe')
    o_lines = run([ct, '--monitor', '7E1D0F:3,7E1D26:3', '--from', 'saves/battle_re/forest_holdleft_pre.state',
                   '--input', '40:2600', '--frames', str(PORT_END + FRAME_OFS + 30), '--diff'],
                  {'CT_ORACLE_LOGICAL': '1'})
    p_lines = run([cf, '--assets', 'ct_assets.dat', '--scene', '19', '--battletest', 'saves/battle_re/forest_pre.raw.wram'],
                  {'CT_BATTLE_MEMMON': '1D0F:3,1D26:3', 'CT_BATTLE_SCRIPT': 'n:40', 'CT_BATTLE_DEBUGEND': str(PORT_END),
                   'CT_NOPACE': '1', 'SDL_AUDIODRIVER': 'dummy', 'SDL_VIDEODRIVER': 'dummy'}, merge=True)
    o, p = [], []
    for l in o_lines:
        m = re.match(r'\s*f(\d+)\s+7E1D0F=(\w+)\*?\s+7E1D26=(\w+)', l)
        if m and FRAME_OFS < int(m.group(1)) < PORT_END + FRAME_OFS:   # (the port stops at PORT_END)
            o.append((int(m.group(1)), m.group(2).upper(), m.group(3).upper()))
    o = o[1:]                                          # the first row is the starting value, not a change
    for l in p_lines:
        m = re.match(r'memmon f(\d+) .*1D0F=(\w+)\*?\s+1D26=(\w+)', l)
        if m and int(m.group(1)) > 1:                  # f1 is the starting value, not a change
            p.append((int(m.group(1)), m.group(2).upper(), m.group(3).upper()))
    n = max(len(o), len(p))
    k = 0
    while k < min(len(o), len(p)) and o[k][1:] == p[k][1:] and o[k][0] - p[k][0] == FRAME_OFS:
        k += 1
    print('forest-feet: %d of %d changes equal in value and frame (port + %d)' % (k, n, FRAME_OFS))
    if k < n:
        for j in range(max(0, k - 2), min(k + 3, n)):
            a = o[j] if j < len(o) else (0, '-', '-')
            b = p[j] if j < len(p) else (0, '-', '-')
            print('  oracle f%d X=%s Y=%s | port f%d X=%s Y=%s' % (a[0], a[1], a[2], b[0], b[1], b[2]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
