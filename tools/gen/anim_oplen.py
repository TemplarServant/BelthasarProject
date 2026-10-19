#!/usr/bin/env python3
"""The battle anim interpreter's opcode lengths, from your ROM: saves/anim_oplen.bin (256 signed bytes, -1 = none).

    python tools/gen/anim_oplen.py

ct.exe --animoplen runs the engine's own battleAnimOpLen (src/battle.c: each C1/7A6B handler's advance, read from the
handler) over ops 00-FF, so the closure of the anim scripts (assets/rom_closure_battle.py) walks them with the same
lengths the interpreter uses. Not packed itself: it only sizes what extract_assets.py packs.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join('saves', 'anim_oplen.bin')


def main():
    os.makedirs(os.path.join(ROOT, 'saves'), exist_ok=True)
    p = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--animoplen', OUT], cwd=ROOT, capture_output=True,
                       text=True, timeout=120)
    sys.stdout.write(p.stdout)
    if p.returncode or not os.path.exists(os.path.join(ROOT, OUT)):
        sys.stderr.write(p.stderr[-2000:])
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
