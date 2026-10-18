r"""FD/C2C1, the field's HDMA table builder: the native builder (src/hdma_table.c) against the CARTRIDGE, call for call.

    python tools/hdma_build_cmp.py [--keep] [drive ...]

For each drive the oracle (CT_ORACLE_LOGICAL=1, the rule) runs `--hookpc FDC2C1` with CT_HOOK_DUMPCALLS: the whole WRAM at
every call's first instruction and at its RTL (FD:C2D2 set B / FD:C2DE set A; a call an interrupt fell into is set aside).
Each entry image goes through `ct.exe --hdmabuild` (the native builder every caller runs) and is graded against the
cartridge's exit image:

  * EXACT: all 128 KB equal outside the stack page $0600-$06FF (the routine's JSR / PHB frames land wherever its caller's
    S was; no caller reads them back);
  * WRITTEN: most calls rewrite tables that are already there, so an exact image alone would pass a builder that writes
    nothing. The entry image's tables and data ($7F:0F80-$1E26, the DP bytes $01D9-$01EF -- not $1DFB, the wave's step, which the call reads) are overwritten with
    two poisons ($5A, $A5) and built again: a byte both poisoned builds agree on is one the builder WROTE, and it must be
    the cartridge's exit byte.

One line a drive, then the census of what the calls exercised (both sets, the three `$0126` modes, `$0127`, the window
wave `$1DF9` / `$1DFD`, the battle exit's `$53` bit 7, `$7F:0351`'s parity):
    hdma-build: <drive> <N> calls (<k> interrupted, set aside) -> exact <N>/<N>, written <W> bytes, <D> wrong
    hdma-build: census set A modes 0/1/2 a/b/c, set B modes 0/1/2 d/e/f, $0127 <k> values (<j> <= 0A), wave ch3 <k>
                (under a box <k>) ch2 <k> tail <k>, $53 bit 7 <k>, $0351 odd <k>
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_hdmabuild')
CT = os.path.join(ROOT, 'build', 'ct.exe')

# name, oracle state, button script, frames, extra oracle env. Each is a context FD/C2C1 runs in on the cartridge; a
# PEEK_POKE starts an effect the ROM then runs itself (its own handlers make the builder's inputs).
DRIVES = [
    # a field textbox at the bottom (mode 2): typed out, its open and close ramps on `$0127`
    ('tent-keeper', 'saves/field_re/tent_keeper_lake.state', '0:11,100:1,0:160', 170, {}),
    # the Manoria cathedral after the Naga-ette fight: a box closed by the press
    ('naga', 'saves/field_re/m81_naga_after_fight_lake.state', '0:300,100:1,0:460', 760, {'CT_HOOK_DUMPCALLS_FROM': '250'}),
    # Mom's box flipped to the TOP by Y (C0/1985 `EOR #03`: mode 1), then closed by A -- the top window's ramp
    ('top-box', 'saves/intro_box.state', '0:10,2:1,0:30,100:1,0:80', 125, {}),
    # the attract: loc 16E's window wipe (`FF 94`) and the field around it
    ('attract', 'saves/attract_cena_inicial.state', '0:900', 900, {'CT_HOOK_DUMPCALLS_FROM': '540'}),
    # the attract's 07D demo battle: the exit tail's C0/0AFF builds twice a frame with `$53` bit 7 (FD:C401 / C95D)
    ('demo-battle', 'saves/attract_07d_battle_f4540.state', '0:660', 660, {'CT_HOOK_DUMPCALLS_FROM': '600'}),
    # Jackie's ch08 save, the End of Time: `$1DF9` = 1 -- the window wave on channel 3 and in the tail (C5E2 / C732)
    ('wave-ch3', 'saves/downloaded/ctjackie/states/ch08.state', '0:200', 200, {}),
    # `$0139` = 0C, the field effect C0/264F (C0/21EE's 12th entry): `$1DFD` = 11, the wave on channel 2 (C4D7 / C550)
    ('wave-ch2', 'saves/field_re/tent_keeper_lake.state', '0:120', 120, {'PEEK_POKE': '0139=0C'}),
    # the window and the wave together (`$1DF9` = 1 under a box): channel 3's run list over the wave, CC5E / CCB8 -- mode 2
    # (D0CA / D379) on the tent keeper's box, mode 1 (CA8A / CE01) on Mom's flipped to the top
    ('wave-box', 'saves/field_re/tent_keeper_lake.state', '0:11,100:1,0:160', 170, {'PEEK_POKE': '1DF9=01'}),
    ('wave-top-box', 'saves/intro_box.state', '0:10,2:1,0:30,100:1,0:80', 125, {'PEEK_POKE': '1DF9=01'}),
    # `$7F:0351` odd: channel 7's other table pair (C45B / C47D) and the wave's first carry
    ('odd-0351', 'saves/downloaded/ctjackie/states/ch08.state', '0:60', 60, {'PEEK_POKE': '7F0351=01'}),
]


def run_drive(name, state, script, frames, extra):
    d = os.path.join(OUT, name)
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        if f.endswith('.wram'):
            os.remove(os.path.join(d, f))
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1',
               CT_HOOK_DUMPCALLS=os.path.join(d, 'c'), CT_HOOK_DUMPEXIT='FDC2D2,FDC2DE', **extra)
    r = subprocess.run([CT, '--hookpc', 'FDC2C1', '--from', os.path.join(ROOT, state), '--btnscript', script,
                        '--frames', str(frames)], cwd=ROOT, env=env, capture_output=True, text=True)
    calls, interrupted = [], 0
    for ln in r.stdout.splitlines():
        if ln.startswith('hookpc: call '):
            k = int(ln.split()[2])
            if 'interrupted' in ln:
                interrupted += 1
            else:
                calls.append(k)
    return d, calls, interrupted


def grade(d, ncalls, cen):
    """ct.exe --hdmabuildcmp over the drive's pairs: (exact, written, wrong, first failure)."""
    r = subprocess.run([CT, '--hdmabuildcmp', os.path.join(d, 'c'), str(ncalls)], cwd=ROOT, capture_output=True, text=True)
    exact = written = wrong = 0
    first = ''
    for ln in r.stdout.splitlines():
        w = ln.split()
        if ln.startswith('hdmabuildcmp: key '):
            setb, mode, n = w[3] == 'B', int(w[4]), int(w[5], 16)
            dfd, df9, tail, b7, odd = (int(x) for x in w[6:11])
            cen['mode'][(setb, mode)] = cen['mode'].get((setb, mode), 0) + 1
            if mode:
                cen['n'].add(n)
            cen['ch2'] += dfd
            cen['ch3'] += df9
            cen['boxwave'] += 1 if (mode and df9) else 0
            cen['tail'] += tail
            cen['b7'] += b7
            cen['odd'] += odd
        elif ln.startswith('hdmabuildcmp: call ') and not first:
            first = ' (' + ln[len('hdmabuildcmp: '):] + ')'
        elif ln.startswith('hdmabuildcmp: ') and ' calls, ' in ln:
            calls, exact, written, wrong = int(w[1]), int(w[3]), int(w[5]), int(w[8])
    return exact, written, wrong, first


def main():
    keep = '--keep' in sys.argv
    want = [a for a in sys.argv[1:] if not a.startswith('--')]
    os.makedirs(OUT, exist_ok=True)
    cen = {'mode': {}, 'n': set(), 'boxwave': 0, 'ch3': 0, 'ch2': 0, 'tail': 0, 'b7': 0, 'odd': 0}
    for name, state, script, frames, extra in DRIVES:
        if want and name not in want:
            continue
        if not os.path.exists(os.path.join(ROOT, state)):
            print('hdma-build: %s SKIP (no %s)' % (name, state))
            continue
        d, calls, interrupted = run_drive(name, state, script, frames, extra)
        exact, written, ndiff, first = grade(d, (max(calls) + 1) if calls else 0, cen)
        print('hdma-build: %s %d calls (%d interrupted, set aside) -> exact %d/%d, written %d bytes, %d wrong%s'
              % (name, len(calls), interrupted, exact, len(calls), written, ndiff, first))
        if not keep:
            for f in os.listdir(d):
                if f.endswith('.wram'):
                    os.remove(os.path.join(d, f))
    m = cen['mode']
    print('hdma-build: census set A modes 0/1/2 %d/%d/%d, set B modes 0/1/2 %d/%d/%d, $0127 %d values (%d <= 0A), wave ch3 %d '
          '(under a box %d) ch2 %d tail %d, $53 bit 7 %d, $0351 odd %d'
          % (m.get((False, 0), 0), m.get((False, 1), 0), m.get((False, 2), 0), m.get((True, 0), 0), m.get((True, 1), 0),
             m.get((True, 2), 0), len(cen['n']), len([n for n in cen['n'] if n <= 10]), cen['ch3'], cen['boxwave'],
             cen['ch2'], cen['tail'], cen['b7'], cen['odd']))


if __name__ == '__main__':
    main()
