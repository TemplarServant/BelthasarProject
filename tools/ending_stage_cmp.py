#!/usr/bin/env python3
r"""The ending's $C3 stage, native (src/seq_ending.c: CtEndingStageOpen + one CtEndingStageFrame a frame) against the cartridge.

    python tools/ending_stage_cmp.py [-v]

From saves/ending_re/ending_pre_ff2b.state (Leene's Square, scene 81, the talk already made: obj10's `FF 2B` at f131 -- the
oracle route is in the memory note endgame-oracle-route): the cartridge runs under the rule with no input (`--monitor` of the
stage's LOGIC bytes, on change); the port runs the native stage (`ct.exe --seqstage`) from the cartridge's own state at the
hand-off (f130, saves/ending_re/ending_handoff_f130.state): its WRAM, and its sound processor seeded into the port's, which the
scripts' `F5` waits read. The port's frame 0 is the state after the setup (the cartridge's black frames): the port's frame k is
the cartridge's k + SHIFT, and the SPC runs HOLD frames before the port's first NMI -- both the cartridge's, measured (page A's
first visit after the setup at f179; HOLD puts the port's SPC on the cartridge's own frame). Every frame to the END is compared:

    endingstage: FF 2B <n> frames compared, <d> differing, END cartridge f<c> port f<p> (same|DIFFERENT)
    endingstage: FF 2B song markers cartridge <frames> port <frames>

The bytes graded are the stage's own: the END flag ($0000), the sequence shadows ($0380-$039B less the picture's: the credits
pointer $0382, which task $9B4F advances, and the RNG $039B, which only the shooting stars draw from), the task list ($0920-$093F) and both script pages' registers, IPs, loop counters
and return ($0A00-$0A2F, $0B00-$0B2F). The picture's tasks, hooks and inline calls are stage 2.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
ROM = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
STATE = os.path.join(ROOT, 'saves', 'ending_re', 'ending_pre_ff2b.state')
# Each drive: the operand, the state the cartridge runs from and the frame of the hand-off in it (the op runs on the next), the
# hand-off state the port starts from, SHIFT (the cartridge's frame of the port's frame 0: page A's first visit after the
# setup), HOLD (the SPC's frames from the hand-off to the port's first NMI: SHIFT - hand-off + 1), the frames run, and the song
# markers -- `past` addresses (the first frame page A is above one) or `wait` addresses (the frame page A leaves an `F5`).
DRIVES = {
    '2B': dict(state=STATE, handoff=130, hstate=os.path.join(ROOT, 'saves', 'ending_re', 'ending_handoff_f130.state'),
               shift=179, hold=50, total=5000, past=(0x94A7, 0x9526, 0x95A1, 0x95FE)),
    # `FF 2E` (scene 90, after `FF 2B`'s reboot: 02D -> 105 -> 059 -> 05A), from the cartridge at f6837 of the same run
    '2E': dict(state=os.path.join(ROOT, 'saves', 'ending_re', 'ending_handoff_f6837.state'), handoff=0,
               hstate=os.path.join(ROOT, 'saves', 'ending_re', 'ending_handoff_f6837.state'),
               shift=53, hold=54, total=11000, wait=(0x9669, 0x925E, 0x92C7, 0x92E2)),
}
REGIONS = [(0x0000, 2), (0x0380, 0x1C), (0x0920, 0x20), (0x0A00, 0x30), (0x0B00, 0x30)]
# the picture's bytes inside them: the credits pointer (task $9B4F) and the overlay's RNG $039B ($7E:3448), which only the
# shooting stars draw from (the setup's $9BB8 and task $9BF9: their positions and timings, stage 2)
# shooting stars draw from (the setup's $9BB8 and task $9BF9: their positions and timings, stage 2)
# and page A's r10-r15 ($0A14-$0A1F), the scratch of the picture's vblank hooks: $9AF3 (`LDA $0B0C / STA $0A1E`, then the
# mode-7 matrix from it) and $56C0 ($0A16 / $0A18 from the sine of r5) -- no script reads them (`r = rX` only reads r9)
# and page A's r3 / r4 ($0A06-$0A09), which the hook $9DBC writes once $7B is set (r1 / r2 + $80: the mode-7 centre)
# and $0386, what C3/02DD (the beam's RNG, the virtual beam under the rule) leaves for the stars of the last screens ($5777)
# and $038C, the HDMA channels' shadow (the NMI's `LDA $038C / STA $420C`), which the picture's hooks write too ($8D56)
SKIP = {0x0386, 0x038C, 0x039B} | set(range(0x0A14, 0x0A20)) | set(range(0x0A06, 0x0A0A))


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


def expand(rows, first, last):
    out, cur = {}, {}
    for f in range(min(rows), last + 1):
        if f in rows:
            cur = dict(cur); cur.update(rows[f])
        if f >= first:
            out[f] = cur
    return out


def marks(full, shift, d):
    ip = lambda f: full[f].get(0x0A20, 0) | full[f].get(0x0A21, 0) << 8
    res = []
    for a in d.get('past', ()):
        f = next((f for f in sorted(full) if a < ip(f) < 0x9700), None)
        res.append(None if f is None else f + shift)
    for a in d.get('wait', ()):
        f0 = next((f for f in sorted(full) if ip(f) == a), None)
        f = None if f0 is None else next((f for f in sorted(full) if f > f0 and ip(f) != a), None)
        res.append(None if f is None else f + shift)
    return res


def run(op, d, verbose):
    spec = ','.join('7E%04X:%d' % r for r in REGIONS)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    if not os.path.exists(d['hstate']):
        subprocess.run([CT, ROM, '--peek', d['state'], os.path.join(ROOT, 'build', '_ending_h.raw'), '--btnscript',
                        '0:%d' % d['handoff']], cwd=ROOT, env=dict(env, PEEK_SAVE=d['hstate']), capture_output=True)
    total, shift = d['total'], d['shift']
    o = subprocess.run([CT, ROM, '--monitor', spec, '--from', d['state'], '--input', '0:%d' % total, '--diff'],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace').stdout
    p = subprocess.run([CT, '--seqstage', d['hstate'], op, str(total - shift), '0:%d' % (total - shift), spec, str(d['hold'])],
                       cwd=ROOT, capture_output=True, text=True, errors='replace').stdout
    orows, prows = parse(o), parse(p)
    if not orows or not prows:
        print('endingstage: FF %s -- no output (cartridge %d rows, port %d rows)' % (op, len(orows), len(prows)))
        return
    oend = next((f for f in sorted(orows) if f >= shift and orows[f].get(0x0000) == 1), None)
    pm = re.search(r'END at frame (\d+)', p)
    pend = int(pm.group(1)) + shift if pm else None
    last = (oend or total)
    ofull = expand(orows, shift, last)
    pfull = expand(prows, 0, last - shift)
    n = bad = 0
    first = None
    for pf in sorted(pfull):
        of = pf + shift
        if of not in ofull:
            continue
        ov, pv = ofull[of], pfull[pf]
        dd = [k for k in sorted(set(ov) | set(pv)) if k not in SKIP and ov.get(k) != pv.get(k)]
        n += 1
        if dd:
            bad += 1
            if first is None:
                first = (of, dd)
            if verbose and bad <= 8:
                print('  f%d: %s' % (of, ' '.join('%04X o=%s p=%s' % (k, ov.get(k), pv.get(k)) for k in dd[:10])))
    print('endingstage: FF %s %d frames compared, %d differing, END cartridge f%s port f%s (%s)'
          % (op, n, bad, oend, pend, 'same' if oend == pend else 'DIFFERENT'))
    print('endingstage: FF %s song markers cartridge %s port %s'
          % (op, ' '.join(map(str, marks(ofull, 0, d))), ' '.join(map(str, marks(pfull, shift, d)))))
    if first and not verbose:
        of, dd = first
        print('  first: f%d %s' % (of, ' '.join('%04X o=%s p=%s' % (k, ofull[of].get(k), pfull[of - shift].get(k)) for k in dd[:8])))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('--drives', default='2B,2E')
    a = ap.parse_args()
    for op in a.drives.split(','):
        run(op, DRIVES[op], a.v)
    return 0


if __name__ == '__main__':
    sys.exit(main())
