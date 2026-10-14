#!/usr/bin/env python3
r"""The race's stage, native (src/seq_race.c: CtRaceStageOpen + one CtRaceStageFrame a frame) against the cartridge, frame by frame.

    python tools/race_stage_cmp.py [--drives idle,boost,steer,camera] [-v]

From saves/race_re/race_user.state (A at f30: Johnny's `FF 00` at f66): the cartridge runs the drive under the rule
(`--monitor` of the race's LOGIC bytes, on change); the port runs the native stage from the cartridge's own WRAM at the
hand-off (f65) with the same pad, frame for frame (`ct.exe --racestage`). Both timelines are aligned on the race clock's
first tick ($BC = 1), and every frame from there to the END is compared. Prints, per drive,
`racestage: <drive> <n> frames compared, <d> differing, END cartridge f<c> port f<p> (<same|DIFFERENT>)`.

The bytes graded are the race's own: the clock and the pad ($02-$09), the race's variables ($44, $4A-$BD), the sequence
shadows ($0380-$039B), both script pages' registers, IPs, loop counters and return ($0A00-$0A2F, $0B00-$0B2F, minus
$0A02 and $0A14 -- the camera's BG offset and register 10, which the picture's tasks $428B / $4F54 write) and the END flag.
The picture's tasks and hooks are stage 2; the bytes only they write are not graded.
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
OUT = os.path.join(ROOT, 'build', '_racestage')
HANDOFF = 65
TOTAL = 3000
REGIONS = [(0x0000, 2), (0x0002, 8), (0x0044, 2), (0x004A, 0x40), (0x008A, 0x34), (0x0380, 0x1C), (0x0A00, 0x02),
           (0x0A04, 0x10), (0x0A16, 0x1A), (0x0B00, 0x30)]
# the picture's bytes inside the graded regions: the projection's ($4D0D: $5B-$62, $6B-$6E, $80-$87, $90-$97, $B4-$B5, and its
# scratch in page B's r0 / r6), the HUD's ($AA-$AD), the brightness ($038D, $038F)
SKIP = set(list(range(0x5B, 0x63)) + list(range(0x6B, 0x6F)) + list(range(0x80, 0x88)) + list(range(0x90, 0x98)) +
           [0xB4, 0xB5, 0xAA, 0xAB, 0xAC, 0xAD, 0x038D, 0x038F, 0x0B00, 0x0B01, 0x0B0C, 0x0B0D])

sys.path.insert(0, os.path.join(ROOT, 'tools'))
from race_step_cmp import drive, btnscript   # noqa: E402


def parse(out):
    rows = {}
    for line in out.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', line)
        if not m:
            continue
        vals = {}
        for a, v in re.findall(r'7E([0-9A-F]{4})=([0-9A-F]+)', m.group(2)):
            base = int(a, 16)
            for i in range(0, len(v), 2):
                vals[base + i // 2] = int(v[i:i + 2], 16)
        rows[int(m.group(1))] = vals
    return rows


def expand(rows, last):
    """rows on change -> a value map per frame from the first row to `last`."""
    out, cur = {}, {}
    for f in range(min(rows), last + 1):
        if f in rows:
            cur = dict(cur); cur.update(rows[f])
        out[f] = cur
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--drives', default='idle,boost,steer,camera')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('--lag', type=int, default=1, help='frames between a press and the NMI that reads it')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    spec = ','.join('7E%04X:%d' % r for r in REGIONS)
    for name in a.drives.split(','):
        pads = drive(name, TOTAL)
        env = dict(os.environ, CT_ORACLE_LOGICAL='1')
        o = subprocess.run([CT, ROM, '--monitor', spec, '--from', STATE, '--input', btnscript(pads), '--diff'],
                           cwd=ROOT, env=env, capture_output=True, text=True, errors='replace').stdout
        orows = parse(o)
        raw = os.path.join(OUT, 'h_%s_%d.raw' % (name, os.getpid()))
        subprocess.run([CT, ROM, '--peek', STATE, raw, '--btnscript', btnscript(pads[:HANDOFF])], cwd=ROOT, env=env,
                       capture_output=True)
        def native(padlist):
            return subprocess.run([CT, '--racestage', raw + '.wram', '00', str(TOTAL - HANDOFF), btnscript(padlist), spec],
                                  cwd=ROOT, capture_output=True, text=True, errors='replace').stdout
        # The port's frame 0 is the state after the setup, which ran SETUP frames on the cartridge (NMI off): the port's
        # frame k is the cartridge's k + shift, found on the race clock's first tick with no input, and the pad the
        # cartridge's NMI reads at frame F is the one held at F - LAG.
        p0 = expand(parse(native([0] * (TOTAL - HANDOFF))), TOTAL)
        pb0 = next((f for f in sorted(p0) if p0[f].get(0xBC) == 1), 0)
        ofull0 = expand(orows, max(orows))
        ob0 = next((f for f in sorted(ofull0) if f > HANDOFF and ofull0[f].get(0xBC) == 1), 0)
        sh0 = ob0 - pb0
        p = native([pads[k + sh0 - a.lag] if 0 <= k + sh0 - a.lag < len(pads) else 0 for k in range(TOTAL - HANDOFF)])
        prows = parse(p)
        pend = re.search(r'END at frame (\d+)', p)
        oend = None
        for f in sorted(orows):
            if orows[f].get(0x0000) == 1 and f > HANDOFF:
                oend = f; break
        ofull, pfull = expand(orows, max(orows)), expand(prows, max(prows))
        ob = next((f for f in sorted(ofull) if f > HANDOFF and ofull[f].get(0xBC) == 1), None)
        pb = next((f for f in sorted(pfull) if pfull[f].get(0xBC) == 1), None)
        if ob is None or pb is None:
            print('racestage: %s -- no race clock (cartridge %s, port %s)' % (name, ob, pb)); continue
        shift = ob - pb
        n = bad = 0
        first = None
        for pf in sorted(pfull):
            of = pf + shift
            if pf < pb or of not in ofull:
                continue
            ov, pv = ofull[of], pfull[pf]
            d = [k for k in sorted(set(ov) | set(pv)) if k not in SKIP and ov.get(k) != pv.get(k)]
            n += 1
            if d:
                bad += 1
                if first is None:
                    first = (of, d)
                if a.v and bad <= 5:
                    print('  %s f%d: %s' % (name, of, ' '.join('%04X o=%s p=%s' % (k, ov.get(k), pv.get(k)) for k in d[:10])))
        pe = (int(pend.group(1)) + shift) if pend else None
        print('racestage: %s %d frames compared, %d differing, END cartridge f%s port f%s (%s)'
              % (name, n, bad, oend, pe, 'same' if oend == pe else 'DIFFERENT'))
        if first and not a.v:
            of, d = first
            print('  first: f%d %s' % (of, ' '.join('%04X o=%s p=%s' % (k, ofull[of].get(k), pfull[of - shift].get(k)) for k in d[:8])))


if __name__ == '__main__':
    sys.exit(main())
