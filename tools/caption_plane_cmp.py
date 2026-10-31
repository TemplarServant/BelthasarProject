#!/usr/bin/env python3
r"""The endings' staff roll -- the caption plane (BG3 through WRAM) -- port against the rule's cartridge, every frame, the
whole picture.

    python tools/caption_plane_cmp.py                 the graded segments (below)
    python tools/caption_plane_cmp.py <first> <last>  one span at the plain alignment, every differing frame listed

The roll is scenes 05B-05F: each fn0 runs `29 xx` (C0/3E84: the caption font to BG3's characters, $0BE9 -> BG3SC's size
at the load's C0/0AFE), obj1's function `2C 00 00 / 2A / 2B` clears BG3's two tilemap halves (C0/274D -> C0/75A0 ->
the NMI's C0/7F5D / 7F58) and writes the credits a line a frame (`2F col row / 32`: C0/278F from the ASCII table $FD:A000
into $7E:D800, the NMI's C0/EC77 to VRAM $0BE5). disasm/notes/field_caption_plane.md.

The cartridge state saves/field_re/s5b_roll_f2280.state is made when missing: `--teleport 91 8 8 0` from
saves/field_re/s19_power_tab.state (scene 05B, "PRODUCER"), then 2280 frames with no keys, 30 before 05B's own exit to
05C. The port is seeded from its WRAM (--loadwram) and its SPC (CT_AUDIO_SEEDSTATE) and runs the exits, the loads (fn0's
op 0x29 -> the font into its VRAM image), the clears and the lines itself -- a seed has no VRAM, so the plane must be
built by the port's own machine. Port frame k is set beside the cartridge's `--peek` k + 2 + d (the alignment P + 2,
docs/TESTING.md), the whole 256x224.

d is each scene's own: the exits are `E1` (C0/3B4B), whose C0/3B71 `LDA $4210 / BPL` spins on the NMI flag the field
NMI's own read races. The rule decides that race for the poll (disasm/notes/oracle_rdnmi_rule.md), so under it both
are 0; the tool still finds d per scene (-5..9, the most identical frames) and prints it:
  caption-plane: <scene> d=+<d> <k>/<n> frames identical, <c> with captions
05D is graded to its exit (the port leaves it after f1348): its NPCs are loaded hidden and shown by obj1's `7C`; obj6's
sprite 186 (`82 B3`) comes from the extractor's sweep of every scene's loads (sec. 9).

Regression check: field-caption-plane-staff-roll-vs-rule-cartridge.
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
OUT = os.path.join(ROOT, 'build', '_caption_plane')
STATE = os.path.join(ROOT, 'saves', 'field_re', 's5b_roll_f2280.state')
BASE = os.path.join(ROOT, 'saves', 'field_re', 's19_power_tab.state')
RULE = dict(os.environ, CT_ORACLE_LOGICAL='1')
W, H = 256, 224
SEGMENTS = [('05C', 40, 500), ('05D', 560, 1348)]     # port frames; 05B's are the seed's (its VRAM is not in a seed)
SHIFTS = range(-5, 10)


def ensure_seed():
    os.makedirs(OUT, exist_ok=True)
    if not os.path.exists(STATE):
        tp = os.path.join(OUT, '_tp.state')
        subprocess.run([CT, '--teleport', '91', '8', '8', '0', '--from', BASE, '--frames', '200', '--out', tp],
                       cwd=ROOT, env=RULE, capture_output=True)
        subprocess.run([CT, '--peek', tp, os.path.join(OUT, '_x.raw'), '--btnscript', '0:2280'], cwd=ROOT,
                       env=dict(RULE, PEEK_SAVE=STATE), capture_output=True)
    raw = os.path.join(OUT, '_seed.raw')
    subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT, env=RULE, capture_output=True)
    lw = os.path.join(OUT, '_seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    return lw


def port(lw, frames):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_SEEDSTATE=STATE,
               CT_SHOTS=os.path.join(OUT, 'p'), CT_SHOTS_AT=','.join(map(str, frames)), CT_SHOTS_EXIT='1')
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '91', '--loadwram', lw,
                    '--walkscript', '.:%d' % (max(frames) + 4)], cwd=ROOT, env=env, capture_output=True)
    out = {}
    for k in frames:
        p = os.path.join(OUT, 'p_f%d.raw' % k)
        d = open(p, 'rb').read()
        w, h = struct.unpack('<II', d[:8])
        out[k] = d[8:8 + w * h * 3]
        os.remove(p)
    return out


def oracle(ks):
    got = peekshots.shots(STATE, '0:%d' % max(ks), ks, env=RULE, prefix=os.path.join(OUT, 'c'))
    out = {}
    for k in ks:
        fb = open(got[k], 'rb').read()
        out[k] = bytes(fb[((y + 8) * 2 * 512 + x * 2) * 4 + c] for y in range(H) for x in range(W) for c in (3, 2, 1))
    peekshots.clean(got)
    return out


def caption_px(img):
    """The caption's colour at full brightness: palette 3's colours 1-3 read 7FFF (white) in 05B-05D."""
    return img.count(b'\xff\xff\xff')


def same_px(p, c):
    return sum(1 for i in range(W * H) if p[3 * i:3 * i + 3] == c[3 * i:3 * i + 3])


def main():
    lw = ensure_seed()
    if len(sys.argv) > 2:
        a, b = int(sys.argv[1]), int(sys.argv[2])
        frames = list(range(a, b + 1))
        pp, cc = port(lw, frames), oracle([k + 2 for k in frames])
        good = 0
        for k in frames:
            if pp[k] == cc[k + 2]:
                good += 1
            else:
                print('caption-plane: f%d %d/%d identical [%d caption px on the cartridge, %d on the port]'
                      % (k, same_px(pp[k], cc[k + 2]), W * H, caption_px(cc[k + 2]), caption_px(pp[k])))
        print('caption-plane: %d/%d frames identical' % (good, len(frames)))
        return 0
    frames = sorted({k for _, a, b in SEGMENTS for k in range(a, b + 1)})
    pp = port(lw, frames)
    cc = oracle(sorted({k + 2 + d for k in frames for d in SHIFTS}))
    for name, a, b in SEGMENTS:
        seg = range(a, b + 1)
        best = max(SHIFTS, key=lambda d: (sum(pp[k] == cc[k + 2 + d] for k in seg), -d))
        good = sum(pp[k] == cc[k + 2 + best] for k in seg)
        capt = sum(caption_px(pp[k]) > 0 and pp[k] == cc[k + 2 + best] for k in seg)
        print('caption-plane: %s d=%+d %d/%d frames identical, %d with captions' % (name, best, good, len(seg), capt))
    return 0


if __name__ == '__main__':
    sys.exit(main())
