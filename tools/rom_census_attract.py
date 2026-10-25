#!/usr/bin/env python3
r"""The ROM-free runtime's scoreboard: how many ROM pages the whole attract still reads, and which code reads them.

    python tools/rom_census_attract.py [frames]        (default 11200: the whole demo, title to title)

Runs Belthasar on the attract (`--scene 433`, CT_AUDIO_LOCKSTEP=1) with CT_ROM_CENSUS (src/rom_census.h) and prints
tools/rom_census.py's summary: the pages read, per bank and per source file. The goal (memory rom-free-runtime-goal) is
0; each work package of the plan moves its system's reads into ct_assets.dat and this count goes DOWN -- a re-pin that
raises it is a regression, never a re-pin.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 11200
    d = os.path.join(ROOT, 'build', '_rom_census')
    os.makedirs(d, exist_ok=True)
    out = os.path.join(d, 'attract.txt')
    if os.path.exists(out):
        os.remove(out)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_SHOTS=os.path.join(d, 'shot'), CT_SHOTS_AT=str(frames), CT_SHOTS_EXIT='1', CT_ROM_CENSUS=out)
    subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                   cwd=ROOT, env=env, capture_output=True)
    if not os.path.exists(out):
        print('rom-census: the run wrote no census (no ROM image, or it did not exit)')
        return 1
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'rom_census.py'), out], cwd=ROOT,
                       capture_output=True, text=True)
    sys.stdout.write(p.stdout)
    return p.returncode


if __name__ == '__main__':
    sys.exit(main())
