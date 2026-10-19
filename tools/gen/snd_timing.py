#!/usr/bin/env python3
"""The sound machine's cost table, from your ROM: saves/snd_timing.bin (assets/extract_assets.py packs it as
CT_ASSET_SND_TIMING; src/sound/timing.h has the format).

    python tools/gen/snd_timing.py

The game runs the sound processor's program in C (src/sound/driver.c), its time the listing's: every instruction the
cartridge's driver and the boot ROM can reach, timed on the vendored SPC700 (`ct.exe --sndtiming`, src/sound/
timing_build.c) -- its cycles (a branch both ways), its accesses to the registers and its stores, and block 0's code
map (which bytes are an instruction's: the container's copy of block 0 keeps only its data). Nothing of it is kept in
the repository: it is made from the ROM at setup, like every asset.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join('saves', 'snd_timing.bin')


def main():
    exe = os.path.join(ROOT, 'build', 'ct.exe')
    os.makedirs(os.path.join(ROOT, 'saves'), exist_ok=True)
    p = subprocess.run([exe, '--sndtiming', OUT], cwd=ROOT, capture_output=True, text=True, timeout=300)
    sys.stdout.write(p.stdout)
    if p.returncode or not os.path.exists(os.path.join(ROOT, OUT)):
        sys.stderr.write(p.stderr[-2000:])
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
