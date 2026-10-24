#!/usr/bin/env python3
r"""Compare ONE rectangle of a port frame against the cartridge.

    python tools/frame_region_cmp.py <oracle_state> <oracle_frame> <port_frame> <x0> <y0> <x1> <y1> [<7 more> ...] -- <Belthasar args...>

The port (build/Belthasar.exe, run with the given args) dumps its presented frame with CT_SHOTS and stops there
(CT_SHOTS_EXIT, so an interactive run -- which has no --frames cap -- ends);
the oracle (build/ct.exe --peek <oracle_state> --frames <oracle_frame>) dumps its framebuffer. Prints

    frame-region-cmp: port f<N> vs oracle f<M> box (x0,y0)-(x1,y1) -> <identical> / <total> identical

Oracle framebuffer = 512x480 [pad,B,G,R], downsampled 2x; the port's row y is the oracle's row y+8
(FRAME_CMP_DY=<n> adds n, for the measured one-line residual some modes carry; FRAME_CMP_ORACLE_ARGS=<args> are added to the
oracle's --peek, e.g. its --btnscript). Both sides run live.

Several rectangles (each its own 7 values, before the --) share ONE run of the port: it dumps every port frame named
and stops after the last, and each rectangle prints its own line, in the order given -- the same line its own run
prints. The suite's checks of one port run use this (tools/regress_share.py proves them equivalent).
"""
import os
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')


def main():
    k = sys.argv.index('--') if '--' in sys.argv else -1
    if k < 8 or (k - 1) % 7:
        sys.exit(__doc__)
    specs = [(sys.argv[i], int(sys.argv[i + 1]), int(sys.argv[i + 2]), tuple(int(v) for v in sys.argv[i + 3:i + 7]))
             for i in range(1, k, 7)]
    args = sys.argv[k + 1:]
    tmp = 'build/_frcmp_%d' % os.getpid()
    pfs = sorted({pf for _, _, pf, _ in specs})               # CT_SHOTS_EXIT stops after the LAST listed frame
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS=tmp, CT_SHOTS_AT=','.join(map(str, pfs)), CT_SHOTS_EXIT='1')
    subprocess.run([FIELD] + args, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    shots = {}
    for pf in pfs:
        shot = '%s_f%d.raw' % (tmp, pf)
        if not os.path.exists(shot):
            sys.exit('frame-region-cmp: the port never presented frame %d' % pf)
        d = open(shot, 'rb').read()
        shots[pf] = (struct.unpack('<II', d[:8]), d[8:])
        os.remove(shot)
    for st, of, pf, box in specs:
        (w, h), port = shots[pf]
        compare(st, of, pf, box, w, port, tmp)


def compare(st, of, pf, box, w, port, tmp):
    x0, y0, x1, y1 = box
    orc = tmp + '_orc.raw'
    extra = os.environ.get('FRAME_CMP_ORACLE_ARGS', '').split()   # e.g. --btnscript 0:30,100:2,0:3000 for a drive
    subprocess.run([CT, '--peek', st, orc, '--frames', str(of)] + extra, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(orc):
        sys.exit('frame-region-cmp: the oracle wrote no frame')
    o = open(orc, 'rb').read()
    for ext in ('', '.wram', '.vram', '.oam'):
        try:
            os.remove(orc + ext)
        except OSError:
            pass
    dy = int(os.environ.get('FRAME_CMP_DY', '0'))
    same = total = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = ((y + 8 + dy) * 2 * 512 + x * 2) * 4
            j = (y * w + x) * 3
            total += 1
            if (o[i + 3], o[i + 2], o[i + 1]) == (port[j], port[j + 1], port[j + 2]):
                same += 1
    print('frame-region-cmp: port f%d vs oracle f%d box (%d,%d)-(%d,%d) -> %d / %d identical'
          % (pf, of, x0, y0, x1, y1, same, total))


if __name__ == '__main__':
    main()
