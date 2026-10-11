#!/usr/bin/env python3
"""src/data/ct_ng_pan.h -- the new-game 1000 A.D. overworld pan (loc 496): its starting camera and its length.

    python tools/gen/ct_ng_pan.py [--capture STATE] [--out PATH]

Reads the cartridge with `ct.exe --monitor 7E0100:2,7E0020:2,7E0026:2,7E00E3:1,7E00E7:1` from the pan's capture
instant: f0 gives the BG1/BG2 scroll words $20/$26 and the stepper cells $E3/$E7 the port seeds the pan's camera with
(field.c), and the first frame whose $0100 reads $010F is where the cutscene's own script requests the next location
(C2:1150 `$0100 = 010F`) -- the pan's length.

Default: the recipe ow_intro_pan (it stands in for the owner's start_scene.state; tools/recipes/ow_intro_pan.json says
why it is one frame before the capture). --capture start_scene.state reads the original capture instead.
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctgen import ROOT, CT, recipe  # noqa: E402

OUT = os.path.join(ROOT, 'src', 'data', 'ct_ng_pan.h')
REGS = '7E0100:2,7E0020:2,7E0026:2,7E00E3:1,7E00E7:1'

HEADER = """\
// The new-game 1000 A.D. overworld intro (loc 496): the STARTING camera of start_scene.state -- the same capture
// instant as the intro object snapshot the world blob carries (introSnap) -- and the pan's length.
//   $20 = {x:04X}, $26 = {y:04X} (BG1/BG2 scroll), cells $E3 = {e3:02X}, $E7 = {e7:02X}       (--monitor at f0)
//   the cutscene's own script requests loc $10F at frame {n}              (C2:1150 `$0100 = 010F`, C2:1163 `$027C = 02`)
// The per-frame trajectory that used to be recorded here is gone: the cutscene's object scripts (C2/066C, C2/0568 --
// world.c obScroll) move the camera, and the port's $20/$26 equal the cartridge's on every frame of the pan.
#ifndef CT_NG_PAN_H
#define CT_NG_PAN_H
#define NG_PAN_FRAMES {n}
#define NG_PAN_START_X {x}
#define NG_PAN_START_Y {y}
#endif
"""


def word(s):
    return int(s[2:4] + s[0:2], 16)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--capture', metavar='STATE', help='read this state (e.g. start_scene.state) instead of the recipe')
    ap.add_argument('--out', default=OUT)
    a = ap.parse_args()
    state = a.capture or recipe('ow_intro_pan')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_ORACLE_LOGICAL='1')
    p = subprocess.run([CT, '--monitor', REGS, '--from', state, '--frames', '2000'], cwd=ROOT, env=env,
                       capture_output=True, text=True, errors='replace')
    rows = {}
    for line in p.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+7E0100=([0-9A-F]{4})\*?\s+7E0020=([0-9A-F]{4})\*?\s+7E0026=([0-9A-F]{4})\*?'
                     r'\s+7E00E3=([0-9A-F]{2})\*?\s+7E00E7=([0-9A-F]{2})', line)
        if m:
            rows[int(m.group(1))] = (word(m.group(2)), word(m.group(3)), word(m.group(4)),
                                     int(m.group(5), 16), int(m.group(6), 16))
    if 0 not in rows:
        sys.exit('ct_ng_pan: ct.exe --monitor printed no rows: %s' % (p.stdout + p.stderr)[-600:])
    loc0, x, y, e3, e7 = rows[0]
    if loc0 != 0x1F0:
        sys.exit('ct_ng_pan: the state is not on the 1000 A.D. overworld (loc %03X)' % loc0)
    req = [f for f in sorted(rows) if rows[f][0] == 0x10F]
    if not req:
        sys.exit('ct_ng_pan: the pan never requested loc $10F within 2000 frames')
    with open(a.out, 'w', newline='\n') as f:
        f.write(HEADER.format(x=x, y=y, e3=e3, e7=e7, n=req[0]))
    print('ct_ng_pan: start $20=%04X $26=%04X $E3=%02X $E7=%02X, loc $10F at f%d -> %s' % (x, y, e3, e7, req[0], a.out))


if __name__ == '__main__':
    main()
