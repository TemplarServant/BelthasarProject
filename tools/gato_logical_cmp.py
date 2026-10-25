#!/usr/bin/env python3
r"""GATO's whole fight against the LOGICAL-FRAME oracle -- the battle main loop's two-frame iteration (the zelda3 model,
CLAUDE.md's one exception; disasm/notes/oracle_logical_frames.md §4).

    python tools/gato_logical_cmp.py [last oracle frame]

The cartridge: saves/battle_re/gato_wait.state with `--input 100:8,0:2400` on CT_ORACLE_LOGICAL=1, `--monitor` of
$A013, $AFAB..$AFAD (Crono / Marle / the unused third seat), $AFB1 (GATO's ATB), $B185 (the cursor), $0027 (the
iteration counter), $00A4 (the group counter) and $00E5 (the idle tick's wait flag). The port: the --battletest
fixture saves/battle_re/gato_t0.raw.wram with no input (CT_BATTLE_SCRIPT n:2000), CT_BATTLE_MEMMON of the same bytes.
Port frame p is oracle frame p + 227: the fixture's pinned band arm (CT_BHUD_ARM_FRAME 69) against the oracle's band
seed ($CA1E = FF, f296). Graded from the oracle's first iteration head (f372, `$0027 = 01`) to the given frame
(default 1935): GATO's attack A, his op-00 turn, his song, the teardown, attack A again, and every ATB tick between.
Per byte: `identical` or the first frame and both values. And GATO's WALK: every mover step the port prints
(CT_BATTLE_MVTRACE `battlemove ... cmb6 ... live=(x,y)`) against the oracle's live foot $1D12 / $1D29 on that frame --
the mode pass C1/3714 on both frames of the iteration and the NMI's segment service CF/F978.

UNDER THE RULE (the migration switches CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1, CLAUDE.md "The infinitely fast CPU"):
the cartridge is the rule's oracle, CT_ORACLE_LOGICAL=1 + CT_ORACLE_VIRTUAL_BEAM=1 -- with every CPU cycle free the real
beam stands while the field works, so the event runner's deadline C0/5A46 (`$213D` against `$69`) would read a frozen
line and cut every pass: gato_wait never reaches its battle without the virtual beam. C1/0000 runs at f222 in every model
(--hookpc C10000); the rule's boot is C1/0000 + 57 to the band seed (CD:0512, `$CA1E = FF` f279 -- the valve's f296) and
the first iteration head is f355. The fixture's arm under the rule is CT_BHUD_ARM_FRAME - 11 = 58 (src/battle.c
battleBootBandArm), so port frame p is oracle frame p + 221 (its arm 58 against the seed f279, as 227 was 69 against
f296), and the default window is the same span from the rule's first head: f355 .. f1918.
"""
import os
import re
import subprocess
import sys

RULE = os.environ.get('CT_ORACLE_NMION_HOLD', '1') != '0' and os.environ.get('CT_ORACLE_IFAST', '1') != '0'
OFF = int(os.environ.get("CT_GATO_OFF", "221" if RULE else "227"))   # (measurement: the oracle model can move the boot)
KEYS = ['A013', 'AFAB', 'AFB1', 'B185', '0027', '00A4', '00E5']


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    os.chdir(root)
    first = 355 if RULE else 372                    # the oracle's first iteration head (`$0027 = 01`)
    last = int(sys.argv[1]) if len(sys.argv) > 1 else first + 1563
    env = dict(os.environ, CT_BATTLE_SCRIPT='n:2000', CT_BATTLE_DEBUGEND=str(last - OFF + 5), CT_NOPACE='1',
               SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
               CT_BATTLE_MEMMON='A013:1,AFAB:3,AFB1:1,B185:1,0027:1,00A4:1,00E5:1', CT_BATTLE_MVTRACE='1')
    env.pop('CT_ORACLE_LOGICAL', None)
    pr = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--battletest',
                         'saves/battle_re/gato_t0.raw.wram'], env=env, capture_output=True, text=True, errors='replace')
    oenv = dict(os.environ, CT_ORACLE_LOGICAL='1')
    if RULE:
        oenv['CT_ORACLE_VIRTUAL_BEAM'] = '1'
    orr = subprocess.run([os.path.abspath('build/ct.exe'), '--monitor',
                          '7EA013:1,7EAFAB:3,7EAFB1:1,7EB185:1,7E0027:1,7E00A4:1,7E00E5:1,7E1D12:1,7E1D29:1',
                          '--from', 'saves/battle_re/gato_wait.state', '--input', '100:8,0:2400',
                          '--frames', str(last + 10), '--diff'], env=oenv, capture_output=True, text=True, errors='replace')
    port, orc, steps = {}, {}, []
    for l in (pr.stdout + pr.stderr).splitlines():
        m = re.match(r'memmon f(\d+) rel=\S+\s+(.*)', l)
        if m:
            port[int(m.group(1)) + OFF] = dict(re.findall(r'([0-9A-F]{4})=([0-9A-F]+)', m.group(2)))
        m = re.match(r'battlemove f(\d+) slot3 cmb6 \S+ \S+ live=\((\d+),(\d+)\)', l)
        if m:
            steps.append((int(m.group(1)) + OFF, int(m.group(2)), int(m.group(3))))
    for l in (orr.stdout + orr.stderr).splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if m:
            orc[int(m.group(1))] = {k[2:]: v for k, v in re.findall(r'(7E[0-9A-F]{4})=([0-9A-F]+)', m.group(2))}
    if not port or not orc:
        sys.exit('gato-logical: a side printed nothing (port %d lines, oracle %d)' % (len(port), len(orc)))

    def fill(d):
        out, cur = {}, {}
        for f in range(0, last + 1):
            if f in d:
                cur = dict(cur); cur.update(d[f])
            if f >= first:
                out[f] = cur
        return out
    P, O = fill(port), fill(orc)
    edges = [f for f in range(first + 1, last + 1) if O[f].get('A013') != O[f - 1].get('A013')]
    print('gato-logical: oracle f%d-f%d, port f%d-f%d; the oracle\'s $A013 edges %s' % (
        first, last, first - OFF, last - OFF, ' '.join('f%d' % f for f in edges)))
    for k in KEYS:
        bad = [f for f in range(first, last + 1) if P[f].get(k) != O[f].get(k)]
        if not bad:
            print('gato-logical: $%s identical on all %d frames' % (k, last - first + 1))
        else:
            f = bad[0]
            print('gato-logical: $%s differs on %d frames, first oracle f%d: port %s oracle %s' % (
                k, len(bad), f, P[f].get(k), O[f].get(k)))
    st = [(f, x, y) for f, x, y in steps if first <= f <= last]
    bad = [(f, x, y) for f, x, y in st if (x, y) != (int(O[f].get('1D12', '0'), 16), int(O[f].get('1D29', '0'), 16))]
    if not st:
        print('gato-logical: GATO took no mover step')
    elif not bad:
        print("gato-logical: GATO's %d mover steps (f%d-f%d) all on the oracle's foot" % (len(st), st[0][0], st[-1][0]))
    else:
        f, x, y = bad[0]
        print("gato-logical: GATO's mover steps: %d of %d off the oracle's foot, first oracle f%d: port (%d,%d) oracle (%d,%d)"
              % (len(bad), len(st), f, x, y, int(O[f].get('1D12', '0'), 16), int(O[f].get('1D29', '0'), 16)))


if __name__ == '__main__':
    main()
