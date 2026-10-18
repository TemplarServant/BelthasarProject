#!/usr/bin/env python3
r"""A corpse's foot from its delist through its death routine and after, port against the rule's cartridge (the forest
ambush: two Beetles and an Avian Chaos, A held through the fight on both sides).

    python tools/corpse_still_cmp.py

The user, 2026-10-13 (translated from Portuguese): "after the final blow on an enemy, it moves a little before the
dissolve". Two of these enemies die with a walk segment in flight (`$98A7,X = 01`). The death scan CF/FE20 clears it
(CF/FE33 `STZ $98A4,X`) before the corpse's routine, so on the cartridge the corpse holds its foot; the port stepped it
on through the dissolve.

Cartridge: build/ct.exe under the rule, `--monitor` of the alive list $AF02, the segment flags $98A7 and the feet
$1D0C / $1D23 from saves/battle_re/forest_wait_ambush_active.state. Port: the same WRAM (forest_wait_ambush_active.lakewram),
CT_BATTLE_MASHA, CT_FIELD_MEMMON of the same bytes (and $A0FD). For each delist ($AF02,slot := FF), over the 80 frames
from it; then the frames from it that $A0FD still reads 1 -- the loop's head C1/1073 `STZ $A0FD` waits for the death scan
(every corpse's routine CD/0043), so the NMI's stepper CF/F978 moves nobody through a dissolve -- and, over those frames,
the moves of every OTHER enemy's foot (the living walkers):

    corpse-still: death<k> slot <s> foot (<x>,<y>): cartridge <n> move(s), port <m> move(s); segment flag at the delist: cartridge <a> port <b>
    corpse-still: death<k> $A0FD held from the delist: cartridge <n> frame(s), port <m>; other enemies' moves meanwhile: cartridge <a> port <b>
    corpse-still: <k> of <n> corpse(s) moved on the port

The user, 2026-10-15 (translated from Portuguese): "when a final blow kills more than one enemy, they keep moving a little before the
dissolve". The port's head ran on the delist's frame: the living Avian Chaos stepped 7 px through the second Beetle's
dissolve (cartridge 0).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
CSTATE = 'saves/battle_re/forest_wait_ambush_active.state'
PSEED = 'saves/battle_re/forest_wait_ambush_active.lakewram'
REGS = [(0xAF02, 8), (0x98A7, 8), (0x1D0C, 11), (0x1D23, 11), (0xA0FD, 1)]
N = 1500
WIN = 80


def parse(text, pat):
    rows = {}
    for line in text.splitlines():
        m = re.match(pat, line)
        if m:
            rows[int(m.group(1))] = {int(a, 16) & 0xFFFF: v for a, v in
                                     re.findall(r'\b(?:7E)?([0-9A-F]{4})=([0-9A-F]+)', m.group(2))}
    out, cur = {}, {}
    for f in range(N + 1):                                # the monitors print changes only: carry each value on
        cur = {**cur, **rows.get(f, {})}
        out[f] = cur
    return out


def byte(rows, f, a, i):
    v = rows[f].get(a)
    return int(v[2 * i:2 * i + 2], 16) if v else None


def deaths(rows):
    out = []
    for f in range(1, N + 1):
        for s in range(8):
            a, b = byte(rows, f - 1, 0xAF02, s), byte(rows, f, 0xAF02, s)
            if a is not None and b is not None and a != 0xFF and b == 0xFF:
                out.append((f, s))
    return out


def moves(rows, f0, c):
    foot = [(byte(rows, f, 0x1D0C, c), byte(rows, f, 0x1D23, c)) for f in range(f0, min(N, f0 + WIN) + 1)]
    return sum(1 for a, b in zip(foot, foot[1:]) if a != b), foot[0]


def others(rows, f0, n, dead):
    """Foot changes of every enemy but `dead` over the n frames from f0 (the living walkers through the routine)."""
    k = 0
    for c in range(3, 11):
        if c == dead:
            continue
        foot = [(byte(rows, f, 0x1D0C, c), byte(rows, f, 0x1D23, c)) for f in range(f0, min(N, f0 + n) + 1)]
        k += sum(1 for a, b in zip(foot, foot[1:]) if a != b)
    return k


def held(rows, f0):
    n = 0
    while f0 + n <= N and byte(rows, f0 + n, 0xA0FD, 0) == 1:
        n += 1
    return n


def main():
    btn = ','.join(['100:2,0:2'] * (N // 4))             # A held 2 frames every 4, as CT_BATTLE_MASHA
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1')
    o = subprocess.run([CT, '--monitor', ','.join('7E%04X:%d' % r for r in REGS), '--from', CSTATE, '--btnscript', btn,
                        '--frames', str(N), '--diff'], cwd=ROOT, env=env, capture_output=True, text=True).stdout
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_BATTLE_MASHA='1',
                CT_FIELD_MEMMON=','.join('%04X:%X' % r for r in REGS),
                CT_SHOTS=os.path.join(ROOT, 'build', '_corpse_still'), CT_SHOTS_AT=str(N), CT_SHOTS_EXIT='1')
    p = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '19', '--loadwram', PSEED], cwd=ROOT, env=penv,
                       capture_output=True, text=True)
    C = parse(o, r'\s*f(\d+)\s+(.*)')
    P = parse(p.stdout + p.stderr, r'fmemmon f(\d+) loc=\w+\s+(.*)')
    cd, pd = deaths(C), deaths(P)
    if len(cd) != 3 or [s for f, s in cd] != [s for f, s in pd]:
        print('corpse-still: deaths cartridge %s port %s (want the same three)' % (cd, pd))
        return 1
    moved = 0
    for k, ((cf, s), (pf, _)) in enumerate(zip(cd, pd)):
        cm, foot = moves(C, cf, s + 3)
        pm, _ = moves(P, pf, s + 3)
        moved += pm > 0
        print('corpse-still: death%d slot %d foot (%d,%d): cartridge %d move(s), port %d move(s); segment flag at the '
              'delist: cartridge %02X port %02X' % (k + 1, s, foot[0], foot[1], cm, pm, byte(C, cf, 0x98A7, s),
                                                     byte(P, pf, 0x98A7, s)))
        ch, ph = held(C, cf), held(P, pf)
        print('corpse-still: death%d $A0FD held from the delist: cartridge %d frame(s), port %d; other enemies\' moves '
              'meanwhile: cartridge %d port %d' % (k + 1, ch, ph, others(C, cf, ch, s + 3), others(P, pf, ch, s + 3)))
    print('corpse-still: %d of %d corpse(s) moved on the port' % (moved, len(cd)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
