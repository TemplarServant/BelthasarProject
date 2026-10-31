#!/usr/bin/env python3
"""seed_history_cmp.py -- the attract's C3/02DD seed history, the port against the cartridge, call for call.

The zelda3 model (CLAUDE.md) makes the seed routine C3/02DD deterministic on both sides: its beam
reads are the virtual beam (snes.c g_ctVirtualBeam) and the oracle runs in logical frames. Under that rule the two
sides must hand the routine the same inputs at every call -- the seed folds A, the carry, X, $0008, $00F0/$F2/$F4 and
the virtual $213C (= $0351) into $0386 -- so its whole history is comparable byte for byte:

  cartridge  CT_ORACLE_VIRTUAL_BEAM=1 CT_ORACLE_LOGICAL=1 ct.exe --hookpc C302DD --from boot --frames N
             --hookwatch 7E0008  (and again with 7E0386): one line per frame, the byte at each visit
  port       CT_XLAT_SEEDTRACE=1 Belthasar.exe --scene 433 --selftest --frames N: one `seedtrace` line per call
             (the field's wipe and the world's window object, native -- wipe.c, no frame number --, and the translated stage)

For each call it compares the caller's X (which routine), $0008 and $0386 as the routine finds them, and prints

  seed-history: <n>/<m> calls equal ($0386, $0008, caller X) from power-on to cartridge f<F>; first divergence: ...

usage: python tools/seed_history_cmp.py [--frames 8900] [--show K]
"""
import argparse, os, re, subprocess, sys, concurrent.futures as cf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(argv, env_add):
    env = dict(os.environ)
    env.update(env_add)
    p = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    return p.stdout + p.stderr


def cart_calls(text, key):
    out = []
    pat = re.compile(r'\s*f(\d+) visits=(\d+)\s+end=\S+ \[' + key + r'\]=([0-9A-F,]+)\s+\$6D: [0-9A-F ]+\s+A/X/Y: (.*?)\s+vPos')
    for l in text.splitlines():
        m = pat.match(l)
        if not m or int(m.group(2)) == 0:
            continue
        axys = m.group(4).split()
        for i, v in enumerate(m.group(3).split(',')):
            x = axys[i].split('/')[1] if i < len(axys) else '?'
            out.append((int(m.group(1)), int(v, 16) & 0xFF, x))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=8900)
    ap.add_argument('--show', type=int, default=4)
    a = ap.parse_args()
    ct = os.path.join('build', 'ct.exe')
    ctf = os.path.join('build', 'Belthasar.exe')
    rule = {'CT_ORACLE_VIRTUAL_BEAM': '1', 'CT_ORACLE_LOGICAL': '1'}
    hook = [ct, '--hookpc', 'C302DD', '--from', 'boot', '--frames', str(a.frames), '--hookwatch']
    with cf.ThreadPoolExecutor(3) as ex:
        f8 = ex.submit(run, hook + ['7E0008'], rule)
        f386 = ex.submit(run, hook + ['7E0386'], rule)
        fp = ex.submit(run, [ctf, '--assets', 'ct_assets.dat', '--scene', '433', '--selftest', '--frames', str(a.frames)],
                       {'CT_XLAT_SEEDTRACE': '1', 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'CT_NOPACE': '1'})
        c8, c386, ptxt = cart_calls(f8.result(), '7E0008'), cart_calls(f386.result(), '7E0386'), fp.result()
    port = []
    for l in ptxt.splitlines():
        m = re.search(r'seedtrace (\w+) (?:f(\d+) )?.*X=([0-9A-F]+) .*\$0386=([0-9A-F]+) \$0351=([0-9A-F]+) \$0008=([0-9A-F]+)', l)
        if m:
            port.append((m.group(1), m.group(3), int(m.group(4), 16), int(m.group(6), 16)))
    if not c8 or len(c8) != len(c386):
        print('seed-history: FAIL -- the cartridge runs disagree (%d / %d calls)' % (len(c8), len(c386)))
        return 1
    n = min(len(c8), len(port))
    first = None
    for i in range(n):
        if c8[i][2] != port[i][1] or c386[i][1] != port[i][2] or c8[i][1] != port[i][3]:
            first = i
            break
    eq = n if first is None else first
    last = c8[eq - 1][0] if eq else 0
    if first is None and len(port) < len(c8):
        first = len(port)
    div = 'none' if first is None else (
        'call %d (cartridge f%d X=%s $0386=%02X $0008=%02X; port %s' % (first, c8[first][0], c8[first][2],
                                                                        c386[first][1], c8[first][1],
                                                                        ('X=%s $0386=%02X $0008=%02X)' % port[first][1:])
                                                                        if first < len(port) else 'no such call)'))
    print('seed-history: %d/%d calls equal ($0386, $0008, caller X) from power-on to cartridge f%d; first divergence: %s'
          % (eq, len(c8), last, div))
    if first is not None:
        for i in range(max(0, first - 1), min(n, first + a.show)):
            print('  %3d cart f%-5d X=%s 386=%02X 08=%02X | port %-5s X=%s 386=%02X 08=%02X' % (
                i, c8[i][0], c8[i][2], c386[i][1], c8[i][1], port[i][0], port[i][1], port[i][2], port[i][3]))
    return 0 if first is None else 1


if __name__ == '__main__':
    sys.exit(main())
