#!/usr/bin/env python3
r"""The race's per-frame task $7E:485D, native (src/seq_race.c) against the cartridge, call by call.

    python tools/race_step_cmp.py [--step N] [--drives a,b,...]

From saves/race_re/race_user.state (the user's capture at Lab 32, A at f30 starts the race), each DRIVE is walked in
segments of N frames (`ct.exe --peek` with PEEK_SAVE, the WRAM sidecar beside it); at every stop the same frame's state is
handed to both:
  - the cartridge: `ct.exe --callrom 7E485D --from <state> --regdp 0000 --m16 --x16 --writes` (the task run once, every
    WRAM write it makes),
  - the port: `ct.exe --racestep <wram>` (CtRaceStep485D over the same 128 KB, every byte it changed).
Compared: the final value of every byte either side wrote (the stack page $0600-$06FF excluded -- the PEA/PLB of the
flash's bank switch), and the task the call leaves. Prints one line per stop that differs and
`racestep: <drive> <stops> stops, <n> differing` per drive.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
ROM = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
STATE = os.path.join(ROOT, 'saves', 'race_re', 'race_user.state')
OUT = os.path.join(ROOT, 'build', '_racestep')

# Per-frame pad words in LakeSnes order (B=1, Y=2, Sel=4, St=8, Up=10, Dn=20, L=40, R=80, A=100, X=200, L=400, R=800).
def drive(name, total):
    p = [0] * total
    p[30] = p[31] = 0x100                                     # A: talk to Johnny
    for f in range(72, total):
        k = f - 72
        if name == 'idle':
            pass
        elif name == 'boost':
            p[f] = 0x001 if k % 20 < 2 else 0
        elif name == 'steer':                                 # B taps and long weaves left / right: bumps into Johnny
            p[f] = (0x001 if k % 37 < 2 else 0) | (0x040 if (k // 90) % 2 == 0 else 0x080)
        elif name == 'camera':
            p[f] = (0x200 if (k // 60) % 3 == 0 else 0x100 if (k // 60) % 3 == 1 else 0) | (0x001 if k % 53 < 2 else 0)
    return p


def btnscript(pads):
    out, cur, n = [], None, 0
    for v in pads:
        if v == cur:
            n += 1
        else:
            if cur is not None:
                out.append('%X:%d' % (cur, n))
            cur, n = v, 1
    if cur is not None:
        out.append('%X:%d' % (cur, n))
    return ','.join(out) or '0:1'


def run(args, env=None):
    e = dict(os.environ, CT_ORACLE_LOGICAL='1')
    if env:
        e.update(env)
    return subprocess.run([CT, ROM] + args, cwd=ROOT, env=e, capture_output=True, text=True, errors='replace').stdout


def oracle_writes(state, pokes=None):
    args = ['--callrom', '7E' + TASK[0], '--from', state, '--regdp', '0000', '--regdb', '7E', '--m16', '--x16', '--writes',
            '--maxops', '4000']
    if pokes:
        args += ['--pokewram', ','.join('%06X=%02X' % kv for kv in sorted(pokes.items()))]
    out = run(args, {'CT_CALLROM_NONMI': '1', 'CT_CALLROM_ALLWRITES': '1'})
    w = {}
    for m in re.finditer(r'w ([0-9A-F]{6}) = ([0-9A-F]{2})  \(by ([0-9A-F]{2}):([0-9A-F]{4})\)', out):
        pc = int(m.group(4), 16)
        # $39A7 / $39B1 unwind the TASK DISPATCHER's pushes, which --callrom does not have: from the first write by the
        # dispatcher's code ($398A-$39BC) the micro-execution is not the cartridge's any more, so the grade stops there.
        if 0x398A <= pc <= 0x39BC:
            break
        a = int(m.group(1), 16)
        if a < 0x7E0000 and (a & 0xFFFF) < 0x2000:
            a = 0x7E0000 + (a & 0x1FFF)                       # the low-RAM mirror (I/O such as $2140 stays bank 00)
        if 0x7E2140 <= a <= 0x7E2143:
            a = 0x002140 + (a - 0x7E2140)                     # the oracle's log names the APU ports in bank $7E
        w[a] = int(m.group(2), 16)
    return w


SFX_1E03 = [0]
TASK = ['485D']            # the task under grade (--task)


def port_writes(wram):
    r = subprocess.run([CT, '--racestep', wram, TASK[0]], cwd=ROOT, capture_output=True, text=True, errors='replace').stdout
    w = {int(m.group(1), 16): int(m.group(2), 16) for m in re.finditer(r'w ([0-9A-F]{6}) = ([0-9A-F]{2})', r)}
    for m in re.finditer(r'sfx ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2})', r):   # $377E: $1E00..$1E03 -> $2140..$2143
        w.setdefault(0x002140, int(m.group(1), 16)); w.setdefault(0x002141, int(m.group(2), 16))
        w[0x002142] = int(m.group(3), 16); w[0x002143] = SFX_1E03[0]
    t = re.search(r'task -> ([0-9A-F]+)', r)
    return w, (t.group(1) if t else '?')


def poked_cases(state, wram):
    """The finish's two arms on a real race frame, the condition poked in (as --sweepops grades a routine)."""
    base = open(wram, 'rb').read()
    if TASK[0] == '4339':                                     # the story's call: L / R turn the camera ($0044 != 0)
        cases = []
        for pad in (0x0010, 0x0020, 0x0030, 0x0000, 0x0050, 0x00A0):
            for c3a, c3c in ((0, 0), (5, 0), (0x0C, 3), (2, 0x0C)):
                for fr in (0, 1):
                    cases.append(('pad %04X 3A=%X 3C=%X f%d' % (pad, c3a, c3c, fr),
                                  {0x7E0044: 1, 0x7E0045: 0, 0x7E0006: pad & 0xFF, 0x7E0007: pad >> 8, 0x7E0A3A: c3a,
                                   0x7E0A3B: 0, 0x7E0A3C: c3c, 0x7E0A3D: 0, 0x7E0008: fr}))
        for ang in (0x0000, 0x002F, 0x0030, 0x003F, 0x0040, 0x00BF, 0x00C0, 0x013F, 0x0140, 0x01BF, 0x01C0, 0x03C0, 0x03FF):
            cases.append(('angle $0A0A=%04X' % ang, {0x7E0A0A: ang & 0xFF, 0x7E0A0B: ang >> 8}))
        return grade_cases(state, wram, base, cases)
    cases = [('lose $78=8', {0x7E0078: 0x08, 0x7E0079: 0x00})]
    for score in (0x0612, 0x00F6, 0x09A4, 0x07D0, 0x0FA0, 0x1770, 0x0400, 0x0A28, 0x0BB8, 0x0FA2):
        cases.append(('win $69=%04X' % score, {0x7E0053: 0x08, 0x7E0054: 0x00, 0x7E0069: score & 0xFF, 0x7E006A: score >> 8}))
    # the BUMPS: both racers on one spot ($51-$53 = $76-$78, $4E = $73), so $A4 = 0, and every side $A8 says they came from
    for code in (0x01, 0x02, 0x10, 0x11, 0x12, 0x20, 0x21, 0x22):
        pk = {0x7E0051: base[0x76], 0x7E0052: base[0x77], 0x7E0053: base[0x78], 0x7E0054: base[0x79],
              0x7E004E: base[0x73], 0x7E004F: base[0x74], 0x7E00A8: code, 0x7E00A9: 0x00}
        cases.append(('bump $A8=%02X' % code, pk))
    tbl = [base[0x4770 + x] | base[0x4771 + x] << 8 for x in range(2, 0x18, 2)]
    for v in tbl[:3]:                                         # three of the jackpot table's own values
        cases.append(('win $69=%04X (table)' % (v * 2), {0x7E0053: 0x08, 0x7E0054: 0x00, 0x7E0069: (v * 2) & 0xFF, 0x7E006A: (v * 2) >> 8 & 0xFF}))
    return grade_cases(state, wram, base, cases)


def grade_cases(state, wram, base, cases):
    bad = 0
    for name, pk in cases:
        w = bytearray(base)
        for a, v in pk.items():
            w[a - 0x7E0000] = v
        tmp = wram + '.poked'
        open(tmp, 'wb').write(bytes(w))
        ow = oracle_writes(state, pk)
        SFX_1E03[0] = w[0x1E03]
        pw, task = port_writes(tmp)
        diffs = [('%06X o=%s p=%s' % (a, ow.get(a, w[a - 0x7E0000] if a >= 0x7E0000 else None),
                                     pw.get(a, w[a - 0x7E0000] if a >= 0x7E0000 else None)))
                 for a in sorted(set(ow) | set(pw)) if not (0x7E0600 <= a <= 0x7E06FF)
                 and ow.get(a, w[a - 0x7E0000] if a >= 0x7E0000 else None) != pw.get(a, w[a - 0x7E0000] if a >= 0x7E0000 else None)]
        print('racestep poked %-24s task %s: %s' % (name, task, 'same' if not diffs else ' | '.join(diffs[:10])))
        bad += 1 if diffs else 0
    print('racestep: poked %d cases, %d differing' % (len(cases), bad))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--step', type=int, default=37)
    ap.add_argument('--drives', default='idle,boost,steer,camera')
    ap.add_argument('--from', dest='start', type=int, default=140)
    ap.add_argument('--to', type=int, default=2880)
    ap.add_argument('--task', default='485D', help='the task: 485D (the race) or 4339 (the camera and the masks)')
    ap.add_argument('--poked', action='store_true', help='also grade the finish arms on a poked frame (f1500, idle)')
    a = ap.parse_args()
    TASK[0] = a.task.upper()
    os.makedirs(OUT, exist_ok=True)
    if a.poked:
        st = os.path.join(OUT, 'poke_%d.state' % os.getpid()); raw = os.path.join(OUT, 'poke_%d.raw' % os.getpid())
        run(['--peek', STATE, raw, '--btnscript', btnscript(drive('idle', 1500))], {'PEEK_SAVE': st})
        poked_cases(st, raw + '.wram')
    for name in [d for d in a.drives.split(',') if d]:
        pads = drive(name, a.to + 1)
        state, at, stops, bad = STATE, 0, 0, 0
        f = a.start
        while f <= a.to:
            seg = btnscript(pads[at:f])
            st = os.path.join(OUT, '%s_%d.state' % (name, os.getpid()))
            raw = os.path.join(OUT, '%s_%d.raw' % (name, os.getpid()))
            run(['--peek', state, raw, '--btnscript', seg], {'PEEK_SAVE': st + '.next'})
            os.replace(st + '.next', st)
            state, at = st, f
            wram = raw + '.wram'
            pre = open(wram, 'rb').read()
            tb = bytes([int(TASK[0][2:], 16), int(TASK[0][:2], 16)])
            if pre[0x0920:0x0940].find(tb) < 0 or pre[0x0920:0x0940].find(tb) % 2:
                f += a.step                                   # the task is not in the list (before the start / after it)
                continue
            ow = oracle_writes(st)
            SFX_1E03[0] = pre[0x1E03]
            pw, task = port_writes(wram)
            def val(d, addr):
                if addr < 0x7E0000:
                    return d.get(addr)                        # an I/O port: only what was written
                return d.get(addr, pre[(addr - 0x7E0000) if addr < 0x7F0000 else 0x10000 + (addr - 0x7F0000)])
            diffs = []
            for addr in sorted(set(ow) | set(pw)):
                if 0x7E0600 <= addr <= 0x7E06FF:
                    continue
                if val(ow, addr) != val(pw, addr):
                    diffs.append('%06X o=%s p=%s' % (addr, val(ow, addr), val(pw, addr)))
            stops += 1
            if diffs:
                bad += 1
                print('racestep %s f%d task %s: %s' % (name, f, task, ' | '.join(diffs[:12])))
            f += a.step
        print('racestep %s: %s %d stops, %d differing' % (TASK[0], name, stops, bad)) if TASK[0] != '485D' else print('racestep: %s %d stops, %d differing' % (name, stops, bad))


if __name__ == '__main__':
    sys.exit(main())
