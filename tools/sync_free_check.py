#!/usr/bin/env python3
r"""The FREE sync mode against FRAME-PERFECT, on the attract's first 3600 frames.

    python tools/sync_free_check.py

Runs ct_field --scene 433 twice with a settings file (CT_SETTINGS) choosing the sync mode -- the interactive
presentation forced by CT_OVERLAY_SHOT (the overlay drawn at the end, under the dummy video driver), since a headless
run never reads the file -- and prints every location load (CT_LOADCOST) of each:

    sync: frame_perfect  scene 433 6 black at f2058 | world 5 38 black at f2136 | ...
    sync: free           scene 433 0 black at f2058 | world 5 0 black at f2100 | ...
    sync: same locations in the same order (7 loads); free reaches the last one 158 frames sooner

The free mode keeps what the game does (the same loads, in the same order) and drops the black frames the SNES spent
decompressing and uploading.
"""
import os
import re
import subprocess
import sys
import tempfile

LOAD = re.compile(r'^loadcost: (scene|world) (\d+) -- .*= (\d+) black frame\(s\) at f(\d+)')


def run(mode, d):
    ini = os.path.join(d, mode + '.ini')
    open(ini, 'w').write('[timing]\nsync=%s\n' % mode)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_SETTINGS=ini, CT_LOADCOST='1',
               CT_OVERLAY_SHOT=os.path.join(d, mode + '.bmp') + '@3600:3')
    p = subprocess.run([os.path.abspath('build/ct_field.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                       env=env, capture_output=True, text=True, errors='replace', timeout=900)
    return [(m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)))
            for m in (LOAD.match(l) for l in p.stdout.splitlines()) if m]


def main():
    d = tempfile.mkdtemp(prefix='ct_sync_')
    res = {m: run(m, d) for m in ('frame_perfect', 'free')}
    for m, loads in res.items():
        print('sync: %-13s %s' % (m, ' | '.join('%s %d %d black at f%d' % x for x in loads)))
    a, b = res['frame_perfect'], res['free']
    same = [(x[0], x[1]) for x in a] == [(x[0], x[1]) for x in b]
    blackFree = sum(x[2] for x in b)
    if same and a:
        print('sync: same locations in the same order (%d loads); free: %d black frames; it reaches the last one %d frames sooner'
              % (len(a), blackFree, a[-1][3] - b[-1][3]))
    else:
        print('sync: the two modes loaded DIFFERENT locations')
    return 0


if __name__ == '__main__':
    sys.exit(main())
