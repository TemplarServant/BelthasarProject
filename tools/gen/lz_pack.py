#!/usr/bin/env python3
"""The LZ streams the game decompresses at run time, decompressed from your ROM: saves/lz_pack.bin
(assets/extract_assets.py packs it as CT_ASSET_LZ; src/lz.h serves it in place of the ROM's stream).

    python tools/gen/lz_pack.py

The streams are named by the ROM's OWN pointer tables -- never by a trace of what some run touched: a run reaches only
part of the game, and a stream missing here would be read from the ROM again. Each table is one entry of SOURCES; the
work packages of the ROM-free runtime (memory rom-free-runtime-goal) add theirs as they move their system. ct.exe
--lzpack decompresses every listed offset with src/lz.c and prices it both ways (C3/0557's $7E and $7F arms), so the
container's copy is the same code's output; `ct.exe --lzassets ct_assets.dat` checks it stream by stream.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'assets'))
from ctlib import rom as romlib  # noqa: E402
import rom_closure  # noqa: E402

OUT = os.path.join('saves', 'lz_pack.bin')
LIST = os.path.join('build', 'lz_pack.list')


def ptr24(rom, at):
    """A 3-byte HiROM pointer -> its file offset, or None outside the ROM."""
    bank = rom[at + 2]
    if bank < 0xC0:
        return None
    return (bank - 0xC0) << 16 | rom[at] | rom[at + 1] << 8


# Each source is a function of the ROM -> {what: set of stream offsets}, a closure over the ROM's own tables
# (assets/rom_closure.py). The work packages add theirs here.
SOURCES = [
    rom_closure.location_streams,          # the location load: maps, tilesets, assemblies, BG3 assemblies, scripts
    rom_closure.sequence_streams,          # the $C3 stages' packets through $FE:0000
]
SKIPPED = os.path.join('saves', 'lz_pack.skipped')   # the listed offsets that are not streams (the raw blocks)


def main():
    rom = romlib.load(os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'))
    offs, per = [], []
    for source in SOURCES:
        for what, got in source(rom).items():
            per.append('%s %d' % (what, len(got)))
            offs.extend(got)
    offs = sorted(set(offs))
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    os.makedirs(os.path.join(ROOT, 'saves'), exist_ok=True)
    with open(os.path.join(ROOT, LIST), 'w') as f:
        f.write('# tools/gen/lz_pack.py: %s\n' % ', '.join(per))
        f.writelines('%06X\n' % o for o in offs)
    exe = os.path.join(ROOT, 'build', 'ct.exe')
    p = subprocess.run([exe, '--lzpack', LIST, OUT, SKIPPED], cwd=ROOT, capture_output=True, text=True, timeout=300)
    sys.stdout.write(p.stdout)
    if p.returncode or not os.path.exists(os.path.join(ROOT, OUT)):
        sys.stderr.write(p.stderr[-2000:])
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
