#!/usr/bin/env python3
r"""The race's two $C3 script tasks, native, against the cartridge: tools/race_step_cmp.py for $7E:3749 (page A) and
$7E:3754 (page B) -- along the start of the idle drive (every 13 frames to f900) and on 75 poked IPs each.

    python tools/race_scripts_cmp.py

Prints race_step_cmp's summary lines for both tasks.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

for t in ('3749', '3754'):
    out = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'race_step_cmp.py'), '--task', t, '--step', '13',
                          '--to', '900', '--drives', 'idle', '--poked'], cwd=ROOT, capture_output=True, text=True,
                         errors='replace').stdout
    for line in out.splitlines():
        if line.startswith('racestep:') or line.startswith('racestep %s:' % t):
            print('%s %s' % (t, line))
