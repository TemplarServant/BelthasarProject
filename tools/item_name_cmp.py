#!/usr/bin/env python3
r"""Guardia Forest's Power Tab, port against the rule's cartridge from the same state: the box's item name, the box's
timing, and the walk after it -- every frame, the whole picture.

    python tools/item_name_cmp.py [first last]          (default: frames 1..300)

Two user reports, 2026-10-07, both on scene 19's obj41 (the sparkle at tile 46,33: `E3 00 / 65 82 D3 / 91 / CA CD /
4F CD 00 / EC 14 3D 00 / BB 00 / EE / E3 01 / EA 08`):
  * F5 ct_debug_013: "it should show 'Found 1 Power Tab!' but it shows 'Found 1  item !'". The box's string ends
    `Found 1 <1F>!`; C2/5B9B names the item from the script local $7F:0200 (`4F CD 00`): CC:0B5F + id * 11, at most
    10 bytes, up to the first 0xEF (C2/5D23).
  * F5 ct_debug_018: "crono walked with a fixed animation (back turned) after the dialogue box closed". `E3 01 / EA 08`
    gives control back and posts the song (`JSL C7/0004`, 36 frames on the cartridge): the main loop is parked inside
    it, and the lead stands until it returns; the port's host walked him through it with the drain stopped.

The cartridge state saves/field_re/s19_power_tab.state is made from candy_shop_lake.state when missing: `--teleport 19
45 33 3` (the tile beside the sparkle, facing it). The port is seeded from its WRAM (--loadwram) AND its SPC
(CT_AUDIO_SEEDSTATE): with a cold driver the sound ops' handshakes are priced on another SPC -- the box opened 4 frames
late and `EE` (C0/3CC5, the wait for the fanfare) let go 48 frames early, which is what an earlier version of this tool
pinned as a "residual". The drive, on the cartridge (the port's keys are the oracle's + 1, memory
ct-frame-alignment-p2): A on frame 10 (opens the box), A on 160 (closes it), Down held from 218 (before control comes
back at 227). Port frame k is set beside the cartridge's `--peek` k + 2 (the alignment P + 2), the whole 256x224:
  item-name: f<k> <n>/57344 identical          (only the frames that differ)
  item-name: <k>/<n> frames identical
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import peekshots  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
OUT = os.path.join(ROOT, 'build', '_item_name')
STATE = os.path.join(ROOT, 'saves', 'field_re', 's19_power_tab.state')
BASE = os.path.join(ROOT, 'candy_shop_lake.state')
RULE = dict(os.environ, CT_ORACLE_LOGICAL='1')
EVENTS = [(10, '100', 3), (160, '100', 3), (218, '20', 120)]   # (frame, the oracle's button, frames held)
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
        out.append('.:%d' % (at - t)); out.append('%s:%d' % ('a' if b == '100' else 'd', m)); t = at + m
    out.append('.:%d' % max(1, n - t))
    return ','.join(out)


def ensure_seed():
    if not os.path.exists(STATE):
        subprocess.run([CT, '--teleport', '19', '45', '33', '3', '--from', BASE, '--frames', '120', '--out', STATE],
                       cwd=ROOT, env=RULE, capture_output=True)
    raw = os.path.join(OUT, '_seed.raw')
    subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT, env=RULE, capture_output=True)
    lw = os.path.join(OUT, '_seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    return lw


def oracle_all(script, ks, region):
    """Every frame k's crop from ONE oracle run (PEEK_SHOTS, tools/ctlib/peekshots.py: the same bytes as a --peek cut
    to k)."""
    got = peekshots.shots(STATE, script, ks, env=RULE, prefix=os.path.join(OUT, 'c'))
    out = {}
    for k in ks:
        fb = open(got[k], 'rb').read()
        out[k] = bytes(fb[((y + 8) * 2 * 512 + x * 2) * 4 + c] for y, x in region for c in (3, 2, 1))
    peekshots.clean(got)
    return out


def port(lw, frames):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_SEEDSTATE=STATE,
               CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(map(str, frames)), CT_SHOTS_EXIT='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '19', '--loadwram', lw,
                    '--walkscript', walk(max(frames) + 4)], cwd=ROOT, env=env, capture_output=True)
    out = {}
    for k in frames:
        d = open(os.path.join(OUT, 'p_f%d.raw' % k), 'rb').read()
        w, h = struct.unpack('<II', d[:8])
        out[k] = d[8:8 + w * h * 3]
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    a, b = (int(sys.argv[1]), int(sys.argv[2])) if len(sys.argv) > 2 else (1, 300)
    frames = list(range(a, b + 1))
    lw = ensure_seed()
    pp = port(lw, frames)
    shots = oracle_all(drive(max(frames) + 2), [k + 2 for k in frames], [(y, x) for y in range(H) for x in range(W)])
    cps = {k: shots[k + 2] for k in frames}
    good = 0
    for k in frames:
        p, c = pp[k], cps[k]
        same = sum(1 for i in range(W * H) if p[3 * i:3 * i + 3] == c[3 * i:3 * i + 3])
        if same != W * H:
            print('item-name: f%d %d/%d identical' % (k, same, W * H))
        good += same == W * H
    print('item-name: %d/%d frames identical' % (good, len(frames)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
