#!/usr/bin/env python3
r"""The combat tutorial (loc 0FE, scene 254), port against the rule's cartridge from the same WRAM.

    python tools/tutorial_cmp.py [circle|line|jump ...]        (no argument: all three)

disasm/notes/field_quad_window.md. Scene 254 has no battle: obj0's script draws the lesson with field objects and two screen
effects. Its three moments (the magenta circle, the line attack's yellow window, the imps' jump into the damage area):
  circle  `FF 90 98 2C 25` -- the iris (C0/21E1 entries 1/2, C3/0077). Inside it BG1/BG2 are off and the backdrop shows:
          CGRAM 0, which obj0's `4B 00 22 7E D8 5C` makes magenta (5CD8). The state is the iris ($0139-$013D).
  line    `FE 30 ..` + `F1 7F 00` -- C0/21E1 entry 6 (C0/2409): eight channels, a quad from C3/0EFA in window 1's table,
          that window as the colour window with the fixed colour (31,31,0) added. `FF 9C` (entry 0E) takes it down. The
          state is the machine ($0139, $7F:1CE8-1CF8, $0360-$0367) and the table at [$0350] while the window is up.
  jump    `7A xx yy 02` x5 -- C0/4D06, each imp's jump into the zone (C0/AAC7's arc on $1B80/$1B81). The state is the five
          imps' positions $1800/$1880 and tickets $1A01 (obj7-11).

Each moment starts from a cartridge state a few frames before it (saves/field_re/scene254_<seg>_cNNN.state, made by this
tool from scene254_tutorial.state under the rule, the A every 32 frames, when missing); the port is seeded from that state's
WRAM (--loadwram) and both run the same presses. For each frame k the port's frame k is set beside the cartridge's: its WRAM
with `--peek` k + 1 (the port's CT_DUMPWRAM_AT is taken before the tick), its picture with `--peek` k + 2 (the alignment
P + 2, docs/TESTING.md), rows 0-130 (above the text box):
  tutorial: <seg> f<k> state <same|DIFF> [table <n>/448] picture <n>/33536 identical
  tutorial: <seg> <k>/<n> frames identical
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_tutorial')
SRC = os.path.join(ROOT, 'saves', 'field_re', 'scene254_tutorial.state')
RULE = dict(os.environ, CT_ORACLE_LOGICAL='1')   # the rule's cartridge (CLAUDE.md): off it the typewriter's CPU time cuts
                                                 #   the runner at $69 and the imps' WAITs starve
PER, PRESS = 30, 2                                # the A, held 2 frames every 32

IMPS = range(7, 12)
SEGMENTS = {
    # name: (seed frame, frames to compare, state reader)
    'circle': (270, [16, 20, 30, 40, 60, 120, 140, 170],
               lambda w: bytes(w[0x0139:0x013E])),
    'line': (960, [10, 20, 40, 60, 100, 140, 180],
             lambda w: (bytes([w[0x0139], w[0x11CE8]]) + bytes(w[0x11CE9:0x11CF9]) + bytes(w[0x0360:0x0368]))),
    'jump': (2830, [10, 20, 30, 40, 60, 80, 120, 160],
             lambda w: b''.join(bytes(w[a + 2 * o: a + 2 * o + 2]) for o in IMPS for a in (0x1800, 0x1880))
             + bytes(w[0x1A01 + 2 * o] for o in IMPS)),
}


def drive(n, phase=0):
    """The oracle's grammar: idle PER, A (0x100) for PRESS, from `phase` frames into the cycle."""
    out, t, k = [], 0, phase
    while t < n:
        if k < PER:
            a = min(PER - k, n - t); out.append('0:%d' % a); t += a; k += a
        else:
            a = min(PER + PRESS - k, n - t); out.append('100:%d' % a); t += a; k += a
        if k >= PER + PRESS: k = 0
    return ','.join(out)


def walk(n, phase=0):
    """The same presses in Belthasar's --walkscript grammar (`a` raises A on the token's first frame)."""
    out, t, k = [], 0, phase
    while t < n:
        if k < PER:
            a = min(PER - k, n - t); out.append('.:%d' % a); t += a; k += a
        else:
            a = min(PER + PRESS - k, n - t); out.append(('a:%d' if k == PER else '.:%d') % a); t += a; k += a
        if k >= PER + PRESS: k = 0
    return ','.join(out)


def seed_path(seg):
    return os.path.join(ROOT, 'saves', 'field_re', 'scene254_%s_c%d.state' % (seg, SEGMENTS[seg][0]))


def ensure_seed(seg):
    os.makedirs(OUT, exist_ok=True)
    sp = seed_path(seg)
    if not os.path.exists(sp):
        subprocess.run([CT, '--peek', SRC, os.path.join(OUT, '_mk.raw'), '--btnscript', drive(SEGMENTS[seg][0])],
                       cwd=ROOT, env=dict(RULE, PEEK_SAVE=sp), capture_output=True)
    raw = os.path.join(OUT, '_%s_seed.raw' % seg)
    subprocess.run([CT, '--peek', sp, raw, '--btnscript', '0:0'], cwd=ROOT, env=RULE, capture_output=True)
    lw = os.path.join(OUT, '_%s_seed.lakewram' % seg)
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    return sp, lw


def oracle(sp, k, phase):
    raw = os.path.join(OUT, 'c%d.raw' % k)
    subprocess.run([CT, '--peek', sp, raw, '--btnscript', drive(k, phase)], cwd=ROOT, env=RULE, capture_output=True)
    fb = open(raw, 'rb').read()
    pic = [[(fb[((y + 8) * 2 * 512 + x * 2) * 4 + 3], fb[((y + 8) * 2 * 512 + x * 2) * 4 + 2],
             fb[((y + 8) * 2 * 512 + x * 2) * 4 + 1]) for x in range(256)] for y in range(131)]
    return open(raw + '.wram', 'rb').read(), pic


def port(lw, k, phase):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=str(k), CT_SHOTS_EXIT='1',
               CT_DUMPWRAM=os.path.join(OUT, 'p%d.wram' % k), CT_DUMPWRAM_AT=str(k))
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '254', '--loadwram', lw,
                    '--walkscript', walk(k + 4, phase)], cwd=ROOT, env=env, capture_output=True)
    d = open(os.path.join(OUT, 'p_f%d.raw' % k), 'rb').read()
    W, H = struct.unpack('<II', d[:8])
    pic = [[tuple(d[8 + (y * W + x) * 3: 8 + (y * W + x) * 3 + 3]) for x in range(256)] for y in range(131)]
    return open(os.path.join(OUT, 'p%d.wram' % k), 'rb').read(), pic


def table(w):
    base = w[0x0350] | (w[0x0351] << 8)
    off = 0x10000 if w[0x0352] & 1 else 0
    return bytes(w[off + ((base + i) & 0xFFFF)] for i in range(448))


def run(seg):
    at, frames, state = SEGMENTS[seg]
    sp, lw = ensure_seed(seg)
    phase = at % (PER + PRESS)
    good = 0
    for k in frames:
        pw, pp = port(lw, k, phase)
        cw, _ = oracle(sp, k + 1, phase)
        _, cp = oracle(sp, k + 2, phase)
        same = state(cw) == state(pw)
        line = 'tutorial: %s f%d state %s' % (seg, k, 'same' if same else 'DIFF')
        tok = True
        if seg == 'line' and cw[0x0BDB] & 0xF0:     # the table while it is the colour window (before, $0350 is the iris's)
            tsame = sum(1 for a, b in zip(table(cw), table(pw)) if a == b)
            tok = tsame == 448
            line += ' table %d/448' % tsame
        psame = sum(1 for y in range(131) for x in range(256) if cp[y][x] == pp[y][x])
        line += ' picture %d/%d identical' % (psame, 131 * 256)
        print(line)
        if not same:
            print('   cartridge %s' % state(cw).hex())
            print('   port      %s' % state(pw).hex())
        good += same and tok and psame == 131 * 256
    print('tutorial: %s %d/%d frames identical' % (seg, good, len(frames)))


def main():
    os.makedirs(OUT, exist_ok=True)
    for seg in sys.argv[1:] or ['circle', 'line', 'jump']:
        run(seg)


if __name__ == '__main__':
    main()
