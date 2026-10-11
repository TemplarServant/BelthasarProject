#!/usr/bin/env python3
r"""A chest's message box closed by the camera, port against the rule's cartridge from the same state -- every frame,
the whole picture.

    python tools/chest_box_close_cmp.py [first last]          (default: frames 1..320)

C0/274D's bit-5 arm (C0/2758-277D, disasm/notes/field_caption_plane.md sec. 7): with a chest's box up ($0154 bit 5,
C0/1F1C) the main loop compares the chest's tile $015D/$015E (C0/1E52) with the camera's columns and rows ($1D0A..$1D10
/ 2) every frame and, when the tile is outside, writes `$29 = 0D` -- the box machine's close ramp. The box does not hold
the lead: he walks with it on screen, and it goes when the chest leaves the view.

The cartridge state saves/field_re/s49_chest_walk.state is made from s19_power_tab.state when missing: `--teleport 49 12
38 0` (loc 031, the tile below the chest (12,37), facing it). The drive, on the cartridge (the port's keys are the
oracle's + 1, memory ct-frame-alignment-p2): A on frame 10 (opens the chest), Down held from 34, Right from 134, Down from
534. The cartridge closes at f280 ($1D0E = 4A: the camera's top row is the chest's) and clears $0129 at f291. Port frame
k is set beside the cartridge's `--peek` k + 2 (the alignment P + 2), the whole 256x224:
  chest-close: cartridge $0129 <v>@f<n> ...      (the box machine's state on each change, `--monitor 0129 --diff`)
  chest-close: port      $0129 <v>@f<n> ...      (CT_TBTRACE; the port's frame n is the cartridge's n - 1, the key)
  chest-close: f<k> <n>/57344 identical          (only the frames that differ)
  chest-close: <k>/<n> frames identical
"""
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import peekshots  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
OUT = os.path.join(ROOT, 'build', '_chest_close')
STATE = os.path.join(ROOT, 'saves', 'field_re', 's49_chest_walk.state')
BASE = os.path.join(ROOT, 'saves', 'field_re', 's19_power_tab.state')
RULE = dict(os.environ, CT_ORACLE_LOGICAL='1')
EVENTS = [(10, '100', 4), (34, '20', 100), (134, '80', 400), (534, '20', 200)]   # (frame, the oracle's button, held)
KEY = {'100': 'a', '20': 'd', '80': 'r'}
W, H = 256, 224


def drive(k):
    out, t = [], 0
    for at, b, n in EVENTS:
        if t < min(at, k):
            out.append('0:%d' % (min(at, k) - t)); t = min(at, k)
        if t >= k:
            break
        m = min(n, k - t); out.append('%s:%d' % (b, m)); t += m
    if t < k:
        out.append('0:%d' % (k - t))
    return ','.join(out) or '0:0'


def walk(n):
    out, t = [], 0
    for at, b, m in EVENTS:
        at += 1                                                     # the port's key: the oracle's + 1
        if at > t:
            out.append('.:%d' % (at - t))
        out.append('%s:%d' % (KEY[b], m)); t = at + m
    out.append('.:%d' % max(1, n - t))
    return ','.join(out)


def ensure_seed():
    if not os.path.exists(STATE):
        subprocess.run([CT, '--teleport', '49', '12', '38', '0', '--from', BASE, '--frames', '120', '--out', STATE],
                       cwd=ROOT, env=RULE, capture_output=True)
    raw = os.path.join(OUT, '_seed.raw')
    subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT, env=RULE, capture_output=True)
    lw = os.path.join(OUT, '_seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    for ext in ('', '.wram', '.vram', '.oam'):
        try:
            os.remove(raw + ext)
        except OSError:
            pass
    return lw


def oracle_all(script, ks):
    got = peekshots.shots(STATE, script, ks, env=RULE, prefix=os.path.join(OUT, 'c'))
    out = {}
    for k in ks:
        fb = open(got[k], 'rb').read()
        out[k] = bytes(fb[((y + 8) * 2 * 512 + x * 2) * 4 + c] for y in range(H) for x in range(W) for c in (3, 2, 1))
    peekshots.clean(got)
    return out


def oracle_0129(n):
    r = subprocess.run([CT, '--monitor', '0129:1', '--from', STATE, '--input', drive(n), '--diff'], cwd=ROOT, env=RULE,
                       capture_output=True, text=True)
    return ['%s@f%s' % (v, f) for f, v in re.findall(r'^\s*f(\d+)\s+000129=([0-9A-F]{2})\*', r.stdout, re.M)]


def port(lw, frames):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_SEEDSTATE=STATE,
               CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(map(str, frames)), CT_SHOTS_EXIT='1',
               CT_TBTRACE='1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '49', '--loadwram', lw,
                        '--walkscript', walk(max(frames) + 4)], cwd=ROOT, env=env, capture_output=True, text=True)
    seen, prev = [], None
    for f, v in re.findall(r'^tbtrace: f(\d+)\s.*?\$0129=([0-9A-F]{2})', r.stdout, re.M):
        if v != prev and int(f) <= max(frames):
            seen.append('%s@f%s' % (v, f))
        prev = v
    out = {'0129': seen}
    for k in frames:
        p = os.path.join(OUT, 'p_f%d.raw' % k)
        d = open(p, 'rb').read()
        w, h = struct.unpack('<II', d[:8])
        out[k] = d[8:8 + w * h * 3]
        os.remove(p)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    a, b = (int(sys.argv[1]), int(sys.argv[2])) if len(sys.argv) > 2 else (1, 320)
    frames = list(range(a, b + 1))
    lw = ensure_seed()
    pp = port(lw, frames)
    print('chest-close: cartridge $0129 ' + ' '.join(oracle_0129(max(frames))))
    print('chest-close: port      $0129 ' + ' '.join(v for v in pp['0129'] if not v.startswith('00@f0')))
    shots = oracle_all(drive(max(frames) + 2), [k + 2 for k in frames])
    good = 0
    for k in frames:
        p, c = pp[k], shots[k + 2]
        same = sum(1 for i in range(W * H) if p[3 * i:3 * i + 3] == c[3 * i:3 * i + 3])
        if same != W * H:
            print('chest-close: f%d %d/%d identical' % (k, same, W * H))
        good += same == W * H
    print('chest-close: %d/%d frames identical' % (good, len(frames)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
