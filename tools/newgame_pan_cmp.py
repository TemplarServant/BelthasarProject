#!/usr/bin/env python3
r"""Compare the new-game 1000 A.D. overworld pan's camera in the port against the cartridge, frame by frame.

    python tools/newgame_pan_cmp.py

The oracle runs start_scene.state (a real new-game capture at the start of the pan, loc 1F0) and prints $20/$26 (the
BG1 scroll words) every frame with --monitor. The port runs `--newgame` with CT_OWCAMTRACE=1, which prints the same
words after every overworld frame of the pan (`ng` = the pan's frame). Frame k of the port (ng = k) is compared with
the oracle's f(k). The pan is 1533 frames; the last one is where the cutscene's script requests loc $10F.
Also printed: the transitions the port takes (the pan's end and loc 271 -> 2), so a check pins the whole chain.
"""
import os
import re
import subprocess

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'start_scene.state'
FRAMES = 1533


def main():
    orc = subprocess.run([CT, '--monitor', '7E0020:2,7E0026:2', '--from', STATE, '--frames', str(FRAMES + 2)],
                         capture_output=True, text=True).stdout
    O = {}
    for line in orc.splitlines():
        m = re.match(r'\s*f(\d+)\s+7E0020=([0-9A-F]{4})\*?\s+7E0026=([0-9A-F]{4})', line)
        if m:
            w = lambda s: int(s[2:] + s[:2], 16)
            O[int(m.group(1))] = (w(m.group(2)), w(m.group(3)))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_NAMEAS='Crono',
               TB_AUTOADV='1', CT_OWCAMTRACE='1')
    port = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--newgame', '--selftest', '--frames', '2200'],
                          capture_output=True, text=True, env=env).stdout
    P = {}
    for line in port.splitlines():
        m = re.match(r'owcam t\d+ .*\$20=([0-9A-F]{4}) \$26=([0-9A-F]{4}) .* ng=(\d+)', line)
        if m:
            P[int(m.group(3))] = (int(m.group(1), 16), int(m.group(2), 16))
        elif line.startswith('ngintro:') or line.startswith('changeloc:') or line.startswith('ow-script-cam:'):
            print('newgame-pan-cmp: port ' + line)
    same = total = 0
    first = None
    for k in range(1, FRAMES):
        if k in P and k in O:
            total += 1
            if P[k] == O[k]:
                same += 1
            elif first is None:
                first = k
    print('newgame-pan-cmp: camera $20/$26 port vs cartridge -> %d / %d frames identical%s' %
          (same, total, '' if first is None else ' (first difference ng=%d port=%s cartridge=%s)' % (first, P[first], O[first])))
    for k in (1, 500, 1000, 1532):
        if k in O:
            print('newgame-pan-cmp: cartridge f%d $20=%d $26=%d' % (k, O[k][0], O[k][1]))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
