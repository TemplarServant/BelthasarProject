#!/usr/bin/env python3
r"""Crono's Cyclone victim blink (anim op 6C, C1/5F13) against the cartridge, toggle by toggle.

    python tools/cyclone_blink_cmp.py

The oracle runs saves/battle_re/gato_cyclone.state with the Cyclone cast driver and streams `$A187` (op 6C's per-
combatant ON bit) and `$993C` (the combatant palette byte op 69 / 6A write) with --monitor --diff; the port runs
saves/battle_re/gato_cyclone.raw.wram with its own cast driver and CT_BATTLE_ASCTRACE (`ascblink` = op 6C's toggles,
`ascflash` = op 69 / 6A). The two captures do not start the cast on the same frame, so what is compared is the
machine: the intervals between consecutive toggles of the victim, the number of toggles before the loop ends, and
the frames from the last toggle to the loop's exit (`$993C` restored by op 6C's C1/5E8B arm, the port's first OFF
after the last toggle that is not a toggle). Prints

    cyclone-blink-cmp: oracle <n> toggles <intervals>; port <m> toggles <intervals>
    cyclone-blink-cmp: intervals identical <k> / <n-1>, toggle count <same|differs>, exit +<a> vs +<b>

Regression check: battle-anim-op6c-cyclone-blink-vs-oracle.
"""
import os
import re
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
DRIVER = '100:8,0:372,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:300'
PORT_SCRIPT = 'n:1000,d:2,n:10,a:2,n:10,a:2,n:10,a:2,n:600'


def oracle():
    r = subprocess.run([CT, '--monitor', 'A187:11,993C:11', '--from', 'saves/battle_re/gato_cyclone.state',
                        '--btnscript', DRIVER, '--frames', '700', '--diff', '--writer'],
                       capture_output=True, text=True, errors='replace')
    toggles, restores = [], []
    prev = None
    for line in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+00A187=([0-9A-F]{22})\S*\s+00993C=([0-9A-F]{22})(\S*)', line)
        if not m:
            continue
        f, a187 = int(m.group(1)), m.group(2)
        bit = int(a187[12:14], 16)                    # combatant 6 (GATO, $A014[3] = 06)
        if prev is not None and bit != prev:
            toggles.append((f, bit))
        prev = bit
        if 'C1:5EAE' in m.group(4) and m.group(3)[12:14] != '08':
            restores.append(f)
    while toggles and toggles[0][1] != 1:            # the state's leftover byte, cleared by CC/F06B's open wipe
        toggles.pop(0)
    return toggles, restores


def port():
    env = dict(os.environ, CT_BATTLE_SCRIPT=PORT_SCRIPT, CT_BATTLE_TRACE='1', CT_BATTLE_ASCTRACE='1',
               CT_NOPACE='1', SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--battletest', 'saves/battle_re/gato_cyclone.raw.wram'],
                       env=env, capture_output=True, text=True, errors='replace')
    toggles = []
    first_arm = None
    exit_f = None
    for line in (r.stdout + r.stderr).splitlines():
        m = re.match(r'asc arm f(\d+) sel=01 ', line)
        if m and first_arm is None:
            first_arm = int(m.group(1))
        m = re.match(r'ascblink f(\d+) rel=\d+ t3 cmb6 (ON|OFF)', line)
        if m:
            toggles.append((int(m.group(1)), 1 if m.group(2) == 'ON' else 0))
        m = re.match(r'ascexit f(\d+) rel=\d+ t3 ', line)
        if m and exit_f is None and toggles:
            exit_f = int(m.group(1))
    return toggles, exit_f


def main():
    ot, orest = oracle()
    pt, pexit = port()
    if not ot or not pt:
        sys.exit('cyclone-blink-cmp: no toggles (oracle %d, port %d)' % (len(ot), len(pt)))
    oi = [b[0] - a[0] for a, b in zip(ot, ot[1:])]
    pi = [b[0] - a[0] for a, b in zip(pt, pt[1:])]
    oexit = next((f - ot[-1][0] for f in orest if f > ot[-1][0]), None)
    same = sum(1 for a, b in zip(oi, pi) if a == b)
    print('cyclone-blink-cmp: oracle %d toggles %s; port %d toggles %s'
          % (len(ot), ','.join(map(str, oi)), len(pt), ','.join(map(str, pi))))
    print('cyclone-blink-cmp: intervals identical %d / %d, toggle count %s, exit +%s vs +%s'
          % (same, len(oi), 'same' if len(ot) == len(pt) else 'differs',
             oexit, (pexit - pt[-1][0]) if pexit is not None else None))


if __name__ == '__main__':
    main()
