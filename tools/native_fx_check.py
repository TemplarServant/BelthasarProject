#!/usr/bin/env python3
r"""The field's native frames between over what is composed on the map: the iris, the colour window, the caption plane.

    python tools/native_fx_check.py [roll] [line] [gate]

The native drawer (src/fieldnative.c, a mod: the frames between at 60+ fps) redraws a field frame from the map's layers,
the sprites' own pixels and -- since 2026-10-10 -- the window and the captions composed over them (CtFieldNativeFx). At
t = 256 and 1x it must be the composed frame (CT_NATIVECHECK). Three runs reach the three:

  roll  the staff roll's caption plane: 05B's roll from tools/caption_plane_cmp.py's seed, 1400 frames
  line  the tutorial's colour window: scene 254's line from tools/tutorial_cmp.py's seed, 380 frames
  gate  the Telepod's gate and the arrival's iris: field-scene8-gate-warp-flow's own drive, 400 frames past it

and each prints the drawer's own counts:

  native-fx: roll: effects 1300 frames, 0 differ, 0 px (caption now); every field frame 1000, 0 differ, 0 px

(the counts are printed every 100 effect frames / every 500 field frames: a run shorter than 500 has no field line)
"""
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(argv, extra):
    env = dict(os.environ); env.update(extra)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_NATIVECHECK='1')
    out = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe')] + argv, cwd=ROOT, env=env, capture_output=True,
                         text=True, errors='replace', timeout=1200).stdout
    fx = re.findall(r'nativecheck-fx: (\d+) frames \(iris (\d) colour (\d) caption (\d) now\), (\d+) differ, (\d+) px', out)
    al = re.findall(r'nativecheck: (\d+) frames, (\d+) differ, (\d+) px', out)
    return fx[-1] if fx else None, al[-1] if al else None


def case(name):
    if name == 'roll':
        import caption_plane_cmp as C
        return (['--assets', 'ct_assets.dat', '--scene', '91', '--loadwram', C.ensure_seed(), '--walkscript', '.:1500'],
                {'CT_AUDIO_SEEDSTATE': C.STATE, 'CT_SHOTS': os.path.join(ROOT, 'build', '_nfx'), 'CT_SHOTS_AT': '1400',
                 'CT_SHOTS_EXIT': '1'})
    if name == 'line':
        import tutorial_cmp as T
        _, lw = T.ensure_seed('line')
        phase = T.SEGMENTS['line'][0] % (T.PER + T.PRESS)
        return (['--assets', 'ct_assets.dat', '--scene', '254', '--loadwram', lw, '--walkscript', T.walk(400, phase)],
                {'CT_SHOTS': os.path.join(ROOT, 'build', '_nfx'), 'CT_SHOTS_AT': '380', 'CT_SHOTS_EXIT': '1'})
    chk = [c for c in json.load(open(os.path.join(ROOT, 'tools', 'regress_checks.json'), encoding='utf-8'))['checks']
           if c['name'] == 'field-scene8-gate-warp-flow'][0]
    argv = list(chk['argv'][1:]); i = argv.index('--frames'); argv[i + 1] = str(int(argv[i + 1]) + 400)
    return argv, dict(chk.get('env', {}))


def main():
    for name in sys.argv[1:] or ['roll', 'line', 'gate']:
        fx, al = run(*case(name))
        if not fx:
            print('native-fx: %s: no native frame with an effect was checked' % name); continue
        kinds = ', '.join(k for k, on in zip(('iris', 'colour', 'caption'), fx[1:4]) if on == '1')
        tail = '; every field frame %s, %s differ, %s px' % al if al else ''
        print('native-fx: %s: effects %s frames, %s differ, %s px (%s now)%s' % (name, fx[0], fx[4], fx[5], kinds, tail))
    return 0


if __name__ == '__main__':
    sys.exit(main())
