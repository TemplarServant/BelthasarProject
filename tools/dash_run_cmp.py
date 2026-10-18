#!/usr/bin/env python3
r"""The field lead's RUN, port against the LOGICAL oracle: the Dash row of the button map, read from the pad latch.

    python tools/dash_run_cmp.py [hold|ctrun|remap ...]

disasm/notes/field_dash_run.md. The run is C0/8925 `LDA $00F8 / BIT #02` (C0/88EE's d-pad dispatch: accumulator delta
0x20 instead of 0x10). $00F8 is the HELD byte the NMI's pad latch decodes (C2/84D2 -> C2/8545, the X = 2 pass of
C2/8555) and its bit 1 is `AND $2996 / CMP $2996` -- the Config screen's Dash row (C2/C2E8), factory map B (C2/FCA9).
Each drive runs the same buttons on both sides (the cartridge on `--monitor`, the port on CT_FIELD_MEMMON from the same
WRAM) and compares, every frame, the lead's words $1802/$1882, its velocity $1D32/$1D33, its anim mode $1682 and the
latched $00F8:

  hold    party3_field: B+Left 30 frames, then Left alone 30 -- run, then walk (the keyboard's X is pad B).
  ctrun   the same field driven by CT_RUN=1 + `r:30` (the headless hook: B held while the drive holds a direction).
  remap   $2996 poked to 04 (Dash on Y) on both sides: Y+Left 30, then B+Left 30 -- Y runs and B walks.

Per drive:
  dash-run: <drive> f0-fN: <k> frame(s) differ [runs]
  dash-run: <drive> run frames (mode 06, |$1D32| = 0x20) cartridge <ranges> port <ranges>
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_dashrun')

WATCH = [('1802', 2), ('1882', 2), ('1D32', 2), ('1682', 1), ('00F8', 1)]
STATE, SEED, SCENE = 'saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130
# name: (pokes {flat: value}, oracle --btnscript, port --walkscript, port env, frames). The port's frame N carries the
# cartridge's pad of frame N-1 (the oracle's `0:4` against the port's `n:5`, as door_list_cmp.py drives it).
DRIVES = {
    'hold': ({}, '0:4,41:30,40:30,0:10', 'n:5,l:60,n:10', {'CT_HOLDBTN': '08@5:30'}, 72),
    'ctrun': ({}, '0:4,81:30,0:10', 'n:5,r:30,n:10', {'CT_RUN': '1'}, 42),
    'remap': ({0x2996: 0x04}, '0:4,42:30,41:30,0:10', 'n:5,l:60,n:10', {'CT_HOLDBTN': '04@5:30,08@35:30'}, 72),
}


def run_oracle(d):
    pokes, btn, _, _, frames = d
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    if pokes:
        env['PEEK_POKE'] = ','.join('%04X=%02X' % kv for kv in pokes.items())
    watch = ','.join('7E%s:%d' % (a, n) for a, n in WATCH)
    r = subprocess.run([CT, '--monitor', watch, '--from', STATE, '--btnscript', btn], cwd=ROOT, env=env,
                       capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), {a[2:]: v for a, v in re.findall(r'(7E[0-9A-F]{4})=([0-9A-F]+)', m.group(2))}))
    return rows


def run_port(d, name):
    pokes, _, walk, penv, frames = d
    os.makedirs(OUT, exist_ok=True)
    b = bytearray(open(os.path.join(ROOT, SEED), 'rb').read())
    o = b.index(b'RAM:131072:') + 11
    for a, v in pokes.items():
        b[o + a] = v
    sp = os.path.join(OUT, '_%s_seed.lakewram' % name)
    open(sp, 'wb').write(bytes(b))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_FIELD_MEMMON=','.join('%s:%X' % (a, n) for a, n in WATCH))
    for k in ('CT_RUN', 'CT_HOLDBTN'):
        env.pop(k, None)
    env.update(penv)
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(SCENE), '--loadwram', sp, '--selftest',
                        os.path.join(OUT, '_%s_end.raw' % name), '--frames', str(frames + 2), '--walkscript', walk],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    rows = []
    for l in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', l)
        if m:
            rows.append((int(m.group(1)), {a.upper(): v for a, v in re.findall(r'([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}))
    return rows


def series(rows, key, last):
    out, cur = [], None
    it = iter(sorted(rows))
    pend = next(it, None)
    for f in range(last + 1):
        while pend is not None and pend[0] <= f:
            if key in pend[1]:
                cur = pend[1][key]
            pend = next(it, None)
        out.append(cur)
    return out


def runs(bad):
    out, cur = [], None
    for f in bad:
        if cur and f == cur[1] + 1:
            cur[1] = f
            continue
        if cur:
            out.append(cur)
        cur = [f, f]
    if cur:
        out.append(cur)
    return ', '.join(('f%d' % a) if a == b else ('f%d-f%d' % (a, b)) for a, b in out) or '-'


def run_frames(ser, last):
    v, mode = ser['1D32'], ser['1682']
    bad = []
    for f in range(last + 1):
        if v[f] is None or mode[f] is None:
            continue
        vx, vy = int(v[f][:2], 16), int(v[f][2:4], 16)
        if mode[f] == '06' and 0x20 in (vx, vy, 0x100 - vx, 0x100 - vy):
            bad.append(f)
    return runs(bad)


def main():
    names = sys.argv[1:] or list(DRIVES)
    for name in names:
        d = DRIVES[name]
        last = d[4] - 1
        o, p = run_oracle(d), run_port(d, name)
        if not o or not p:
            print('dash-run: %s NO DATA (cartridge %d rows, port %d rows)' % (name, len(o), len(p)))
            continue
        so = {a: series(o, a, last) for a, _ in WATCH}
        sp = {a: series(p, a, last) for a, _ in WATCH}
        diff = [f for f in range(last + 1) if any(so[a][f] != sp[a][f] for a, _ in WATCH)]
        print('dash-run: %s f0-f%d: %d frame(s) differ%s' % (name, last, len(diff), (' [%s]' % runs(diff)) if diff else ''))
        for f in diff[:4]:
            print('  f%d cartridge %s | port %s' % (f, ' '.join('%s=%s' % (a, so[a][f]) for a, _ in WATCH),
                                                    ' '.join('%s=%s' % (a, sp[a][f]) for a, _ in WATCH)))
        print('dash-run: %s run frames (mode 06, |$1D32| = 0x20) cartridge %s port %s'
              % (name, run_frames(so, last), run_frames(sp, last)))


if __name__ == '__main__':
    main()
