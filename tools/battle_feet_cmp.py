#!/usr/bin/env python
"""A fight's movement, port against the rule's oracle, frame for frame.

    python tools/battle_feet_cmp.py [fixture]      (fixture: forest (default), gato)

Every change of the combatants' feet -- the X bytes $1D0C,c and the Y bytes $1D23,c of the combatants the fixture
names -- compared in value and in frame between the cartridge (`--monitor --diff` under CT_ORACLE_LOGICAL=1) and the
port (`--battletest`, CT_BATTLE_MEMMON on the same bytes). The port's fight runs `ofs` frames earlier than the
cartridge's (its fixture starts later); the window ends at the port's debug end.

  forest  saves/battle_re/forest_holdleft_pre.state `--input 40:2600` / forest_pre.raw.wram, CT_BATTLE_SCRIPT n:40, the
          three Hetakes (cmb 3-5), port + 172
  gato    saves/battle_re/gato_wait.state `--input 100:8,0:2400` (+ CT_ORACLE_VIRTUAL_BEAM, tools/gato_logical_cmp.py) /
          gato_t0.raw.wram, CT_BATTLE_SCRIPT n:2000, Crono, Marle and GATO (cmb 0, 1, 6), port + 221

Prints `<fixture>-feet: <k> of <n> changes equal in value and frame (port + <ofs>)`, then the first difference if any.
Used by the checks battle-forest-feet-vs-oracle and battle-gato-feet-vs-oracle.
"""
import os, re, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

FIXTURES = {
    'forest': dict(state='saves/battle_re/forest_holdleft_pre.state', inp='40:2600', oenv={},
                   wram='saves/battle_re/forest_pre.raw.wram', scene='19', script='n:40', end=2400, ofs=172,
                   cmbs=[3, 4, 5], label='forest'),
    'gato':   dict(state='saves/battle_re/gato_wait.state', inp='100:8,0:2400', oenv={'CT_ORACLE_VIRTUAL_BEAM': '1'},
                   wram='saves/battle_re/gato_t0.raw.wram', scene=None, script='n:2000', end=2150, ofs=221,
                   cmbs=[0, 1, 6], label='gato'),
}


def run(cmd, env_extra, merge=False):
    env = dict(os.environ)
    env.update(env_extra)
    r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT if merge else subprocess.DEVNULL, timeout=1800)
    return r.stdout.decode('utf-8', 'replace').splitlines()


def main():
    fx = FIXTURES[sys.argv[1] if len(sys.argv) > 1 else 'forest']
    ofs, end, cmbs = fx['ofs'], fx['end'], fx['cmbs']
    ct = os.path.join('build', 'ct.exe')
    cf = os.path.join('build', 'ct_field.exe')
    oenv = {'CT_ORACLE_LOGICAL': '1'}
    oenv.update(fx['oenv'])
    o_lines = run([ct, '--monitor', '7E1D0C:11,7E1D23:11', '--from', fx['state'], '--input', fx['inp'],
                   '--frames', str(end + ofs + 30), '--diff'], oenv)
    pargs = [cf, '--assets', 'ct_assets.dat'] + (['--scene', fx['scene']] if fx['scene'] else []) + ['--battletest', fx['wram']]
    p_lines = run(pargs, {'CT_BATTLE_MEMMON': '1D0C:11,1D23:11', 'CT_BATTLE_SCRIPT': fx['script'],
                          'CT_BATTLE_DEBUGEND': str(end), 'CT_NOPACE': '1', 'SDL_AUDIODRIVER': 'dummy',
                          'SDL_VIDEODRIVER': 'dummy'}, merge=True)

    def pick(xs, ys):                                  # the named combatants' bytes, as one comparable key
        return ' '.join('%s/%s' % (xs[2 * c:2 * c + 2], ys[2 * c:2 * c + 2]) for c in cmbs)

    o, p = [], []
    for l in o_lines:
        m = re.match(r'\s*f(\d+)\s+7E1D0C=([0-9A-Fa-f]+)\*?\s+7E1D23=([0-9A-Fa-f]+)', l)
        if m and int(m.group(1)) < end + ofs:          # (the port stops at its debug end)
            o.append((int(m.group(1)), pick(m.group(2).upper(), m.group(3).upper())))
    for l in p_lines:
        m = re.match(r'memmon f(\d+) .*1D0C=([0-9A-Fa-f]+)\*?\s+1D23=([0-9A-Fa-f]+)', l)
        if m:
            p.append((int(m.group(1)), pick(m.group(2).upper(), m.group(3).upper())))

    def changes(rows, first):                          # changes of the named combatants only, after the fight's start
        out, prev = [], None
        for f, k in rows:
            if prev is not None and k != prev and f > first:
                out.append((f, k))
            prev = k
        return out
    # sync: the port's fixture starts staged; the cartridge's rows start before the staging. Compare from the cartridge's
    # first row equal to the port's starting state.
    start = p[0][1] if p else None
    i0 = next((i for i, r in enumerate(o) if r[1] == start), 0)
    o = changes(o[i0:], 0)
    p = changes(p, 0)
    n = max(len(o), len(p))
    k = 0
    while k < min(len(o), len(p)) and o[k][1] == p[k][1] and o[k][0] - p[k][0] == ofs:
        k += 1
    print('%s-feet: %d of %d changes equal in value and frame (port + %d)' % (fx['label'], k, n, ofs))
    if k < n:
        for j in range(max(0, k - 2), min(k + 3, n)):
            a = o[j] if j < len(o) else (0, '-')
            b = p[j] if j < len(p) else (0, '-')
            print('  oracle f%d %s | port f%d %s' % (a[0], a[1], b[0], b[1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
