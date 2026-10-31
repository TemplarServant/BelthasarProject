#!/usr/bin/env python3
"""Extract Work RAM ($7E0000-$7FFFFF, 128KB) + VRAM (64KB) from a snes9x save state.

snes9x save states are gzip-compressed, signature "#!s9xsnp", then blocks "NAME:LLLLLL:<data>".
The blocks read are RAM (the 128KB WRAM) and VRA (64KB VRAM). Used to render the overworld from a live
engine snapshot (the displayed world map is assembled at runtime in WRAM; see assets/overworld_live.py).

Usage: python assets/snes9x_extract.py <save.state> [out_dir=build]
Writes <out_dir>/ow_wram.bin and <out_dir>/ow_vram.bin.
"""
import sys, os, gzip, re


def extract(path):
    data = open(path, 'rb').read()
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    if not data.startswith(b'#!s9xsnp'):
        raise SystemExit('not a snes9x save state (bad signature)')
    out = {}
    for name in ('RAM', 'VRA'):
        m = re.search((name + r':(\d{6}):').encode(), data)
        if not m:
            raise SystemExit('block %s not found' % name)
        n = int(m.group(1)); o = m.end()
        out[name] = data[o:o + n]
    return out['RAM'], out['VRA']


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    out_dir = sys.argv[2] if len(sys.argv) > 2 else 'build'
    wram, vram = extract(sys.argv[1])
    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, 'ow_wram.bin'), 'wb').write(wram)
    open(os.path.join(out_dir, 'ow_vram.bin'), 'wb').write(vram)
    loc = wram[0x0100] | (wram[0x0101] << 8)
    print('wrote %s/ow_wram.bin (%dB) + ow_vram.bin (%dB); WRAM $0100=0x%04X%s' %
          (out_dir, len(wram), len(vram), loc,
           ('  (overworld world %d)' % ((loc & 0x1FF) - 0x1F0)) if loc >= 0x1F0 else '  (NOT overworld)'))
